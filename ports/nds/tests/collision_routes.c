#include "collision_routes.h"

#include <stddef.h>
#include <string.h>
#include "render_trig.inc"

typedef struct replay_input { uint32_t tick,held,touch,ui; } replay_input;
/* Recorded physical samples, independent of presentation. UI1 enters, UI2
 * resumes; all UI samples are consumed through the same boundary API. */
static const replay_input inputs[]={
    {100u,DOK_BUTTON_R,0u,0u},{120u,0u,0u,0u},
    {510u,DOK_BUTTON_UP|DOK_BUTTON_L|DOK_BUTTON_R,0u,0u},
    {530u,DOK_BUTTON_UP|DOK_BUTTON_L,0u,0u},
    {800u,DOK_BUTTON_UP|DOK_BUTTON_Y,0u,0u},
    {1410u,DOK_BUTTON_UP|DOK_BUTTON_LEFT|DOK_BUTTON_Y,0u,0u},
    {1850u,0u,0u,0u},
    {2010u,DOK_BUTTON_X|DOK_BUTTON_B,0u,0u},{2030u,0u,0u,0u},
    {2210u,DOK_BUTTON_X,0u,0u},{2230u,0u,0u,0u},
    {2250u,DOK_BUTTON_B,0u,0u},{2270u,0u,0u,0u},
    {2600u,DOK_BUTTON_RIGHT|DOK_BUTTON_A,1u,0u},
    {3000u,DOK_BUTTON_R|DOK_BUTTON_X|DOK_BUTTON_A|DOK_BUTTON_START,1u,1u},
    {3100u,DOK_BUTTON_R|DOK_BUTTON_X|DOK_BUTTON_A,1u,0u},
    {3500u,DOK_BUTTON_R|DOK_BUTTON_X|DOK_BUTTON_A|DOK_BUTTON_START,1u,2u},
    {3520u,DOK_BUTTON_R|DOK_BUTTON_X|DOK_BUTTON_A,1u,0u},
    {3910u,0u,0u,0u},{4010u,DOK_BUTTON_X,0u,0u},{4030u,0u,0u,0u},
    {4300u,DOK_BUTTON_DOWN|DOK_BUTTON_RIGHT|DOK_BUTTON_L,0u,0u},
    {4800u,DOK_BUTTON_UP|DOK_BUTTON_DOWN|DOK_BUTTON_LEFT|DOK_BUTTON_RIGHT,0u,0u},
    {5300u,DOK_BUTTON_Y|DOK_BUTTON_A,0u,0u},
    {5700u,0u,0u,0u},
    {6200u,DOK_BUTTON_UP|DOK_BUTTON_R,0u,0u},{6800u,0u,0u,0u},
    {7200u,DOK_BUTTON_SELECT,0u,0u},{7220u,0u,0u,0u},
    {7600u,DOK_BUTTON_DOWN|DOK_BUTTON_A,0u,0u},{8300u,0u,0u,0u},
    {9000u,DOK_BUTTON_UP|DOK_BUTTON_RIGHT|DOK_BUTTON_L,0u,0u},{42000u,0u,0u,0u}
};

/* Explicit diagnostic service samples, independent of render observations.
 * The latter segment walks far enough to exercise real map obstacles. */
static uint64_t service_counter(uint32_t index)
{ return index<64u?(uint64_t)index*80u:5040u+(uint64_t)(index-63u)*700u; }

static uint32_t hash_word(uint32_t hash,uint32_t value)
{
    uint32_t i;
    for(i=0u;i<4u;++i) { hash^=(value>>(i*8u))&255u;hash*=UINT32_C(16777619); }
    return hash;
}
static uint32_t hash_motion(uint32_t h,const dok_motion_state *s)
{
    h=hash_word(h,s->flags);h=hash_word(h,(uint32_t)s->x);h=hash_word(h,(uint32_t)s->y);
    h=hash_word(h,(uint32_t)s->z);h=hash_word(h,(uint32_t)s->speed);
    h=hash_word(h,(uint32_t)s->distance);h=hash_word(h,(uint32_t)s->movement_accumulator);
    h=hash_word(h,(uint32_t)s->turn_accumulator);h=hash_word(h,s->last_flags);
    h=hash_word(h,(uint32_t)(int32_t)s->heading);h=hash_word(h,(uint32_t)(int32_t)s->angle_parameter);
    h=hash_word(h,(uint32_t)(int32_t)s->turn_step);h=hash_word(h,(uint32_t)(int32_t)s->linear_step);
    h=hash_word(h,(uint32_t)(int32_t)s->last_turn);return hash_word(h,(uint32_t)(int32_t)s->accel_sign);
}
static uint32_t hash_owner(uint32_t h,const dok_event_owner *o)
{
    uint32_t i;
    h=hash_word(h,o->reference.world_epoch);h=hash_word(h,o->reference.object_id);
    h=hash_word(h,o->reference.generation);
    for(i=0u;i<DOK_EVENT_SLOT_LIMIT;++i) {
        const dok_event_record *e=&o->events[i];
        h=hash_word(h,e->deadline);h=hash_word(h,e->interval);h=hash_word(h,e->parameter);
        h=hash_word(h,e->callback);h=hash_word(h,e->_reserved);
    }
    h=hash_word(h,o->count);h=hash_word(h,o->capacity);h=hash_word(h,o->_next);
    h=hash_word(h,o->_previous);h=hash_word(h,o->_pending_next);return hash_word(h,o->_flags);
}
static uint32_t hash_camera(uint32_t h,const dok_camera_history *camera)
{
    const dok_camera_snapshot *p[2]={&camera->previous,&camera->current};uint32_t i;
    for(i=0u;i<2u;++i) {
        h=hash_word(h,(uint32_t)p[i]->pose.x);h=hash_word(h,(uint32_t)p[i]->pose.y);
        h=hash_word(h,(uint32_t)p[i]->pose.z);h=hash_word(h,p[i]->pose.heading_q16);
        h=hash_word(h,p[i]->tick);
    }
    return hash_word(h,camera->count);
}
static uint64_t counter(void *context)
{ return ((dok_collision_routes_context*)context)->counter; }
static int trig(void *context,uint32_t h,int32_t *sine,int32_t *cosine)
{
    (void)context;if(h>360u) return 0;
    *sine=dok_render_sine[h];*cosine=dok_render_cosine[h];return 1;
}
static void check(dok_collision_routes_report *r,int condition,uint32_t line)
{
    ++r->checks;
    if(!condition) { ++r->failures;if(r->first_failed_line==0u) r->first_failed_line=line; }
}
#define CHECK(r,value) check((r),(value),(uint32_t)__LINE__)
static void status(dok_collision_routes_context *c,dok_status value)
{ if(value!=DOK_STATUS_OK) ++c->errors; }

static void callback(void *context,dok_event_scheduler *scheduler,
                     dok_object_ref owner,uint16_t callback_id,uint32_t parameter)
{
    dok_collision_routes_context *c=context;
    dok_motion_services svc={NULL,trig,&c->movement,dok_collision_move};
    dok_collision_report collision={0};
    dok_motion_report motion_report;
    uint32_t now=0u;
    (void)scheduler;(void)owner;(void)callback_id;(void)parameter;
    status(c,dok_world_clock_read(&c->clock,&now));
    c->last_elapsed=c->stopwatch_active!=0u?now-c->stopwatch_start:0u;
    if(c->callbacks==0u) c->first_elapsed=c->last_elapsed;
    status(c,dok_controls_take(&c->controls,&c->commands));
    c->motion.flags=(c->motion.flags&~UINT32_C(0x00182a3c))|c->commands.native_commands;
    if((c->commands.native_commands&DOK_INPUT_NATIVE_FIRE)!=0u) ++c->short_fire_pulses;
    if(c->commands.use_triggered!=0u) ++c->use_pulses;
    if(c->paused!=0u) ++c->pause_callbacks;
    if(c->paused==0u && (c->input._blocked&(DOK_BUTTON_R|DOK_BUTTON_X|DOK_BUTTON_A))!=0u) {
        ++c->ui_guarded_callbacks;
        if((c->commands.native_commands&(DOK_INPUT_NATIVE_FIRE|DOK_INPUT_NATIVE_USE|
                                         DOK_INPUT_NATIVE_TURN_NEG))!=0u)
            ++c->ui_action_leaks;
    }
    status(c,dok_collision_begin(c->collision,owner,&c->motion,c->last_elapsed,&c->movement));
    if(c->errors==0u) status(c,dok_motion_update(&c->motion,c->last_elapsed,&svc,&motion_report));
    status(c,dok_collision_end(&c->movement,&collision));
    c->collision_total.requested+=collision.requested;c->collision_total.committed+=collision.committed;
    c->collision_total.adjusted+=collision.adjusted;c->collision_total.rejected+=collision.rejected;
    c->collision_total.reason_mask|=collision.reason_mask;
    if(collision.adjusted!=0u || collision.rejected!=0u) dok_camera_reset(&c->camera);
    /* Explicit callback work cost: stopwatch restarts AFTER movement work;
     * events independently schedule from their later post-callback clock. */
    c->counter+=7u+(c->callbacks%5u);
    status(c,dok_world_clock_read(&c->clock,&now));
    c->stopwatch_start=now;c->stopwatch_active=1u;
    status(c,dok_camera_commit(&c->camera,&c->motion,now));
    ++c->callbacks;
}

static void capture(dok_collision_routes_context *c,const replay_input *input)
{
    dok_input_frame frame;
    memset(&frame,0,sizeof(frame));frame.sequence=++c->sequence;
    frame.held=input->held;frame.pressed=input->held&~c->raw_held;
    frame.released=c->raw_held&~input->held;
    if(input->touch!=0u) {
        frame.touch_flags=DOK_TOUCH_HELD|(c->raw_touch==0u?DOK_TOUCH_PRESSED:0u);
        frame.touch_x=120;frame.touch_y=80;
    } else if(c->raw_touch!=0u) frame.touch_flags=DOK_TOUCH_RELEASED;
    status(c,dok_input_capture(&c->input,&frame,c->counter));
    c->raw_held=input->held;c->raw_touch=input->touch;++c->input_samples;
    if(input->ui==1u) { status(c,dok_world_clock_pause(&c->clock));c->paused=1u; }
    if(input->ui!=0u || c->paused!=0u) {
        status(c,dok_input_ui_boundary(&c->input));
        status(c,dok_controls_ui_boundary(&c->controls));
    }
    if(input->ui==2u) { status(c,dok_world_clock_resume(&c->clock));c->paused=0u; }
}

static void service(dok_collision_routes_context *c)
{
    dok_input_sample sample;uint32_t available=0u;
    dok_event_dispatch_report dispatch;
    for(;;) {
        status(c,dok_input_pop_through(&c->input,c->counter,&sample,&available));
        if(c->errors!=0u || available==0u) break;
        status(c,dok_controls_feed(&c->controls,&sample.frame));
    }
    if(c->paused==0u) status(c,dok_events_dispatch(&c->scheduler,&dispatch));
}

static uint32_t map_hash(const dok_initial_world *world)
{
    uint32_t i,j,h=UINT32_C(2166136261);
    for(i=0u;i<4u;++i) for(j=0u;j<DOK_LEVEL_MAX_CELLS;++j)
        h=(h^world->maps[i][j])*UINT32_C(16777619);
    return h;
}
static uint32_t counter_hash(const dok_collision_state *state)
{
    uint32_t i,h=UINT32_C(2166136261);
    for(i=0u;i<256u;++i) {
        h=hash_word(h,state->flags[i]);h=hash_word(h,state->door_count[i]);
        h=hash_word(h,state->known[i]);
    }
    return h;
}
static void observe(dok_collision_routes_context *c,dok_collision_routes_report *r)
{
    dok_motion_state old=c->motion;dok_camera_history old_camera=c->camera;
    dok_camera_pose pose;uint32_t now=0u,clear=0u,callbacks=c->callbacks;
    uint32_t mh=map_hash(c->world),dh=counter_hash(c->collision),alpha,interval;
    status(c,dok_world_clock_read(&c->clock,&now));
    interval=c->camera.current.tick-c->camera.previous.tick;
    alpha=now-c->camera.current.tick;
    alpha=c->camera.count<2u || interval==0u || alpha>=interval?65536u:
          (uint32_t)((uint64_t)alpha*65536u/interval);
    status(c,dok_camera_interpolate(&c->camera,alpha,&pose));
    status(c,dok_collision_camera_clear(c->collision,c->world->hero,c->motion.x,c->motion.z,
                                        pose.x,pose.z,&clear));
    if(clear==0u) { pose=c->camera.current.pose;++c->camera_fallbacks; }
    CHECK(r,memcmp(&old,&c->motion,sizeof(old))==0);
    CHECK(r,memcmp(&old_camera,&c->camera,sizeof(old_camera))==0 && callbacks==c->callbacks);
    CHECK(r,mh==map_hash(c->world) && dh==counter_hash(c->collision));
    c->presentation_hash=hash_word(c->presentation_hash,(uint32_t)pose.x);
    c->presentation_hash=hash_word(c->presentation_hash,pose.heading_q16);
    ++c->presentation_observations;
}

static void record(dok_collision_routes_context *c,dok_event_bank *bank,
                   uint32_t index,dok_collision_routes_trace *out)
{
    uint32_t now=0u;
    memset(out,0,sizeof(*out));status(c,dok_world_clock_read(&c->clock,&now));
    out->motion=c->motion;out->camera=c->camera;out->owner=bank->owners[0];
    out->commands=c->commands;out->counter=c->counter;out->service=index;
    out->world_tick=now;out->callbacks=c->callbacks;out->last_elapsed=c->last_elapsed;
    out->stopwatch_active=c->stopwatch_active;out->stopwatch_start=c->stopwatch_start;
    out->paused=c->paused;out->input_count=c->input._count;
    out->buffer_raw_held=c->input._raw_held;out->control_held=c->controls._held;
    out->map_hash=map_hash(c->world);out->counter_hash=counter_hash(c->collision);
    out->collision=c->collision_total;
}

static int input_equal(const dok_input_buffer *a,const dok_input_buffer *b)
{
    uint32_t i;
    if(a->_last_sequence!=b->_last_sequence || a->_last_counter!=b->_last_counter ||
       a->_last_cutoff!=b->_last_cutoff || a->_tag!=b->_tag || a->_head!=b->_head ||
       a->_count!=b->_count || a->_raw_held!=b->_raw_held || a->_raw_touch!=b->_raw_touch ||
       a->_blocked!=b->_blocked || a->_touch_blocked!=b->_touch_blocked ||
       a->_has_capture!=b->_has_capture || a->_has_cutoff!=b->_has_cutoff) return 0;
    for(i=0u;i<a->_count;++i) {
        const dok_input_sample *x=&a->_samples[(a->_head+i)%DOK_INPUT_SAMPLE_LIMIT];
        const dok_input_sample *y=&b->_samples[(b->_head+i)%DOK_INPUT_SAMPLE_LIMIT];
        if(x->counter!=y->counter || x->frame.sequence!=y->frame.sequence ||
           x->frame.held!=y->frame.held || x->frame.pressed!=y->frame.pressed ||
           x->frame.released!=y->frame.released || x->frame.touch_flags!=y->frame.touch_flags ||
           x->frame.touch_x!=y->frame.touch_x || x->frame.touch_y!=y->frame.touch_y) return 0;
    }
    return 1;
}

static void scheduler_state(const dok_event_scheduler *s,uint32_t out[13])
{
    out[0]=s->_initialized;out[1]=s->_event_count;out[2]=s->_registered_count;
    out[3]=s->_owner_count;out[4]=s->_fault;out[5]=s->_dispatching;
    out[6]=s->_in_callback;out[7]=s->_keep;out[8]=s->_cursor;
    out[9]=s->_current;out[10]=s->_pending_head;out[11]=s->_pending_tail;
    out[12]=s->_config.world_epoch;
}

int dok_collision_routes_run(dok_collision_routes_report *r,
                            dok_initial_world *world,dok_collision_state *collision,dok_event_bank *bank,
                            dok_collision_routes_workspace *w)
{
    uint32_t run;
    if(r==NULL || world==NULL || collision==NULL || bank==NULL || w==NULL ||
       collision->_world!=world || collision->_active!=NULL || world->hero.object_id==0u ||
       world->hero.object_id>world->object_count) return 1;
    memset(r,0,sizeof(*r));memset(w,0,sizeof(*w));
    memcpy(w->saved_maps,world->maps,sizeof(w->saved_maps));w->saved_collision=*collision;
    for(run=0u;run<3u;++run) {
        dok_collision_routes_context *c=&w->context;
        dok_platform_services platform;
        dok_event_config config;
        dok_object_ref owner=world->hero;
        const dok_world_object_seed *hero=&world->objects[owner.object_id-1u];
        const dok_level_object *definition=&world->definition->objects[hero->definition_index];
        const dok_level_field *speed=dok_level_field_at(definition,0x38u);
        const dok_level_field *angle=dok_level_field_at(definition,0x34u);
        uint32_t next_input=0u,next_service=0u,next_observation=1u;
        uint32_t fps=run==0u?0u:run==1u?30u:60u;
        uint64_t end=service_counter(DOK_COLLISION_ROUTES_SERVICES-1u);
        memset(c,0,sizeof(*c));c->world=world;c->collision=collision;
        memcpy(world->maps,w->saved_maps,sizeof(w->saved_maps));*collision=w->saved_collision;
        platform.context=c;platform.read_counter=counter;
        platform.counter_hz_numerator=1193182u;platform.counter_hz_denominator=119u;
        platform.diagnostic=NULL;
        config.clock_context=&c->clock;config.read_ticks=dok_world_clock_read;
        config.callback_context=c;config.callback=callback;config.world_epoch=world->epoch;
        status(c,dok_input_buffer_initialize(&c->input));status(c,dok_controls_initialize(&c->controls));
        status(c,dok_world_clock_init(&c->clock,&platform,0u));
        status(c,dok_events_init(&c->scheduler,bank,&config));
        status(c,dok_events_register(&c->scheduler,owner,4u));
        status(c,dok_events_schedule(&c->scheduler,owner,1u,0u,416u));
        if(speed==NULL || angle==NULL) { ++c->errors; }
        else status(c,dok_motion_init(&c->motion,hero->x,hero->y,hero->z,hero->heading,
                                     (int16_t)angle->value,speed->value));
        dok_camera_reset(&c->camera);status(c,dok_camera_commit(&c->camera,&c->motion,0u));
        while(next_service<DOK_COLLISION_ROUTES_SERVICES && c->errors==0u) {
            uint64_t it=next_input<sizeof(inputs)/sizeof(inputs[0])?inputs[next_input].tick:UINT64_MAX;
            uint64_t st=service_counter(next_service);
            uint64_t ot=fps==0u?UINT64_MAX:
                ((uint64_t)next_observation*1193182u)/(119u*fps);
            uint64_t next=it<st?it:st;
            if(ot<next) next=ot;
            if(next>c->counter) c->counter=next;
            if(it==next) { capture(c,&inputs[next_input++]); }
            else if(st==next) {
                dok_collision_routes_trace current;
                service(c);record(c,bank,next_service,&current);
                if(run==0u) w->baseline[next_service]=current;
                else CHECK(r,memcmp(&current,&w->baseline[next_service],sizeof(current))==0);
                ++next_service;
            } else {
                observe(c,r);++next_observation;
            }
        }
        /* Normalize the endpoint independently of the last render observation. */
        if(c->counter<end) c->counter=end;
        { uint32_t now=0u;status(c,dok_world_clock_read(&c->clock,&now)); }
        CHECK(r,c->errors==0u && next_service==DOK_COLLISION_ROUTES_SERVICES);
        CHECK(r,c->first_elapsed==0u && c->callbacks>10u && c->pause_callbacks==0u);
        CHECK(r,c->input_samples==sizeof(inputs)/sizeof(inputs[0]));
        /* Three queued pre-menu Uses have only two service opportunities;
         * UI boundary intentionally drops the third. The fresh post-menu X
         * supplies the third executed Use, without replaying held UI keys. */
        CHECK(r,c->use_pulses==3u && c->short_fire_pulses==3u);
        CHECK(r,c->ui_guarded_callbacks!=0u && c->ui_action_leaks==0u);
        CHECK(r,c->input._count==0u && c->input._blocked==0u && c->input._touch_blocked==0u);
        CHECK(r,c->scheduler._registered_count==1u && c->scheduler._owner_count==1u &&
                 c->scheduler._event_count==1u && c->scheduler._fault==0u);
        if(run==0u) {
            uint32_t i,h=UINT32_C(2166136261);
            CHECK(r,w->baseline[6].callbacks==1u && w->baseline[6].last_elapsed==0u &&
                     w->baseline[6].world_tick==487u && w->baseline[6].owner.events[0].deadline==903u);
            CHECK(r,w->baseline[12].callbacks==2u && w->baseline[12].last_elapsed==473u &&
                     w->baseline[12].world_tick==968u && w->baseline[12].owner.events[0].deadline==1384u);
            CHECK(r,w->baseline[48].callbacks==7u && w->baseline[48].last_elapsed==453u &&
                     w->baseline[48].world_tick==3348u && w->baseline[48].counter==3848u);
            w->baseline_bank=*bank;w->baseline_input=c->input;w->baseline_controls=c->controls;
            w->baseline_clock_counter=c->clock._last_counter;
            w->baseline_clock_fraction=c->clock._fraction;w->baseline_clock_tick=c->clock._ticks;
            scheduler_state(&c->scheduler,w->baseline_scheduler);
            for(i=0u;i<DOK_COLLISION_ROUTES_SERVICES;++i) {
                const dok_collision_routes_trace *t=&w->baseline[i];
                h=hash_motion(h,&t->motion);h=hash_camera(h,&t->camera);h=hash_owner(h,&t->owner);
                h=hash_word(h,t->world_tick);h=hash_word(h,t->last_elapsed);
                h=hash_word(h,t->commands.native_commands);h=hash_word(h,t->callbacks);
                h=hash_word(h,t->map_hash);h=hash_word(h,t->counter_hash);
            }
            r->trace_hash=h;h=hash_motion(UINT32_C(2166136261),&c->motion);
            h=hash_camera(h,&c->camera);
            for(i=0u;i<DOK_EVENT_OWNER_LIMIT;++i) h=hash_owner(h,&bank->owners[i]);
            r->final_hash=hash_word(h,c->clock._ticks);r->final_motion=c->motion;
            r->callbacks=c->callbacks;r->service_calls=next_service;r->input_samples=c->input_samples;
            r->first_elapsed=c->first_elapsed;r->short_fire_pulses=c->short_fire_pulses;
            r->use_pulses=c->use_pulses;r->pause_callbacks=c->pause_callbacks;
            r->map_hash=map_hash(world);r->counter_hash=counter_hash(collision);
            r->requested=c->collision_total.requested;r->committed=c->collision_total.committed;
            r->adjusted=c->collision_total.adjusted;r->rejected=c->collision_total.rejected;
            memcpy(w->final_maps,world->maps,sizeof(w->final_maps));w->final_collision=*collision;
            CHECK(r,r->requested!=0u && r->committed!=0u);
            CHECK(r,r->adjusted+r->rejected!=0u);
        } else {
            uint32_t scheduler[13];scheduler_state(&c->scheduler,scheduler);
            CHECK(r,memcmp(bank,&w->baseline_bank,sizeof(*bank))==0);
            CHECK(r,memcmp(scheduler,w->baseline_scheduler,sizeof(scheduler))==0);
            CHECK(r,input_equal(&c->input,&w->baseline_input));
            CHECK(r,memcmp(&c->controls,&w->baseline_controls,sizeof(c->controls))==0);
            CHECK(r,c->clock._last_counter==w->baseline_clock_counter &&
                     c->clock._fraction==w->baseline_clock_fraction && c->clock._ticks==w->baseline_clock_tick);
            CHECK(r,memcmp(&c->motion,&r->final_motion,sizeof(c->motion))==0);
            CHECK(r,memcmp(world->maps,w->final_maps,sizeof(w->final_maps))==0);
            CHECK(r,memcmp(collision->flags,w->final_collision.flags,sizeof(collision->flags))==0);
            CHECK(r,memcmp(collision->door_count,w->final_collision.door_count,sizeof(collision->door_count))==0);
            CHECK(r,memcmp(collision->known,w->final_collision.known,sizeof(collision->known))==0);
            if(run==1u) r->observations_30=c->presentation_observations;
            else r->observations_60=c->presentation_observations;
        }
        r->camera_fallbacks+=c->camera_fallbacks;++r->runs;
    }
    memcpy(world->maps,w->saved_maps,sizeof(w->saved_maps));*collision=w->saved_collision;
    return r->failures!=0u;
}
