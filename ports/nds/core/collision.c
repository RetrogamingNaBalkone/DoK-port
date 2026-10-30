#include "dok/collision.h"
#include "dok/cell_math.h"
#include "dok/n23_profile.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define STATE_TAG UINT32_C(0x434f4c31)
#define CONTEXT_TAG UINT32_C(0x434f4c43)
#define NO_CELL UINT32_MAX
#ifndef N23_COLLISION_CORNERS
#define N23_COLLISION_CORNERS 1
#endif
#ifndef N23_COLLISION_OBJECT
#define N23_COLLISION_OBJECT 0
#endif

static int32_t signed32(uint32_t v)
{ return v<=INT32_MAX?(int32_t)v:(int32_t)((int64_t)v-INT64_C(4294967296)); }
static int32_t add32(int32_t a,int32_t b)
{ return signed32((uint32_t)a+(uint32_t)b); }
static int32_t neg32(int32_t a) { return signed32(0u-(uint32_t)a); }
static int32_t sub32(int32_t a,int32_t b) { return add32(a,neg32(b)); }
static int32_t mul32(int32_t a,int32_t b)
{ return signed32((uint32_t)((uint64_t)(uint32_t)a*(uint32_t)b)); }
static int32_t word(int32_t a)
{ uint32_t b=(uint16_t)(uint32_t)a; return b<=32767u?(int32_t)b:(int32_t)b-65536; }
static uint32_t valid_view(const dok_collision_view *v)
{ return v!=NULL&&v->width>0u&&v->width<=64u&&v->length>0u&&v->length<=64u&&
    v->cell_side>0&&v->mmap!=NULL&&v->tmap!=NULL&&v->flags!=NULL&&v->known!=NULL; }
static uint32_t native_index(const dok_collision_view *v,int32_t x,int32_t z)
{
    int32_t cx=word(dok_cell_quotient(x,v->cell_side)),cz=word(dok_cell_quotient(z,v->cell_side));
    if(cx<0)cx=0;
    if(cx>=v->width)cx=(int32_t)v->width-1;
    if(cz<0)cz=0;
    if(cz>=v->length)cz=(int32_t)v->length-1;
    return (uint32_t)cz*v->width+(uint32_t)cx;
}
static uint32_t clear_at(const dok_collision_view *v,int32_t x,int32_t z,
                         int16_t r,uint32_t ignore)
{
    uint32_t plane,i;
    int32_t xs[2]={sub32(x,r),add32(x,r)},zs[2]={sub32(z,r),add32(z,r)};
#if N23_COLLISION_CORNERS
    uint32_t columns[2],rows[2];
    for(i=0;i<2u;++i){
        int32_t cx=word(dok_cell_quotient(xs[i],v->cell_side));
        int32_t cz=word(dok_cell_quotient(zs[i],v->cell_side));
        if(cx<0)cx=0;if(cx>=v->width)cx=(int32_t)v->width-1;
        if(cz<0)cz=0;if(cz>=v->length)cz=(int32_t)v->length-1;
        columns[i]=(uint32_t)cx;rows[i]=(uint32_t)cz*v->width;
    }
#endif
    for(plane=0;plane<2u;++plane)for(i=0;i<4u;++i){
#if N23_COLLISION_CORNERS
        uint32_t index=rows[i%2u]+columns[i/2u];
#else
        uint32_t index=native_index(v,xs[i/2u],zs[i%2u]);
#endif
        if(plane==0u&&v->projectile_occupied!=NULL&&
           (v->projectile_occupied[index/8u]&(1u<<(index%8u)))!=0u)return 0;
        unsigned char g=plane==0u?(index==ignore?32u:v->mmap[index]):v->tmap[index];
        if(v->known[g]==0u||(v->flags[g]&0x10u)==0u)return 0;
    }
    return 1;
}
dok_status dok_collision_native_clear(const dok_collision_view *v,
    int32_t x,int32_t z,int16_t radius,uint32_t *out)
{
    if(!valid_view(v)||out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    *out=clear_at(v,x,z,radius,NO_CELL);return DOK_STATUS_OK;
}
static int32_t center(const dok_collision_view *v,int32_t a)
{ return add32(mul32(dok_cell_quotient(a,v->cell_side),v->cell_side),v->cell_side/2); }
static int32_t edge(const dok_collision_view *v,int32_t a,int32_t direction)
{
    int32_t origin=sub32(a,dok_cell_remainder(a,v->cell_side));
    return direction<0?add32(origin,v->min_distance):
        sub32(sub32(add32(origin,v->cell_side),v->min_distance),1);
}
static void resolve(const dok_collision_view *v,int32_t ox,int32_t oz,
    int32_t nx,int32_t nz,uint32_t ignore,int32_t *outx,int32_t *outz)
{
    int32_t dx=sub32(nx,ox),dz=sub32(nz,oz),sx=v->min_distance,sz=v->min_distance;
    int32_t cx=center(v,ox<nx?ox:nx),cz=center(v,oz<nz?oz:nz),ax=ox,az=oz;
    if(ox<nx){if(cx>nx||cx<ox)cx=ox;}else{if(cx>ox||cx<nx)cx=nx;}
    /* Native computes a constrained Z center first, then unconditionally
     * overwrites it with the lower-endpoint cell center (RU0A90..0AD7). */
    if(nx<ox){sx=neg32(sx);dx=neg32(dx);}
    if(nz<oz){sz=neg32(sz);dz=neg32(dz);}
    if(dx>dz){
        if(clear_at(v,nx,oz,v->min_distance,ignore)){
            ax=nx;
            if(clear_at(v,nx,nz,v->min_distance,ignore)){az=nz;goto done;}
        }else{
            if(clear_at(v,nx,cz,v->min_distance,ignore)){ax=nx;az=cz;goto done;}
            ax=edge(v,ax,sx);
            if(clear_at(v,ax,nz,v->min_distance,ignore)){az=nz;goto done;}
        }
        az=edge(v,az,sz);
    }else{
        if(clear_at(v,ox,nz,v->min_distance,ignore)){
            az=nz;
            if(clear_at(v,nx,nz,v->min_distance,ignore)){ax=nx;goto done;}
        }else{
            if(clear_at(v,cx,nz,v->min_distance,ignore)){ax=cx;az=nz;goto done;}
            az=edge(v,az,sz);
            if(clear_at(v,nx,az,v->min_distance,ignore)){ax=nx;goto done;}
        }
        ax=edge(v,ax,sx);
    }
 done:*outx=ax;*outz=az;
}
dok_status dok_collision_native_resolve(const dok_collision_view *v,
    int32_t ox,int32_t oz,int32_t nx,int32_t nz,int32_t *outx,int32_t *outz)
{
    int32_t x,z;
    if(!valid_view(v)||outx==NULL||outz==NULL||outx==outz)return DOK_STATUS_INVALID_ARGUMENT;
    resolve(v,ox,oz,nx,nz,NO_CELL,&x,&z);*outx=x;*outz=z;return DOK_STATUS_OK;
}
static uint32_t valid_state(const dok_collision_state *s)
{ return s!=NULL&&s->_tag==STATE_TAG&&s->_world!=NULL&&s->_world->definition!=NULL&&
    s->_world->epoch==s->_epoch; }
static uint32_t same_ref(dok_object_ref a,dok_object_ref b)
{ return a.world_epoch==b.world_epoch&&a.object_id==b.object_id&&a.generation==b.generation; }
static const dok_world_object_seed *object(const dok_collision_state *s,dok_object_ref ref)
{
    uint32_t i;
    if(!valid_state(s)||ref.world_epoch!=s->_epoch||ref.object_id==0u||ref.generation==0u)return NULL;
#if N23_COLLISION_OBJECT
    /* Initial-world references normally use object_id=index+1. Bind proves
     * uniqueness; reference identities remain immutable while bound. Keep
     * the full identity test and the original scan for other valid layouts. */
    if(ref.object_id<=s->_world->object_count) {
        const dok_world_object_seed *candidate=&s->_world->objects[ref.object_id-1u];
        if(same_ref(candidate->reference,ref)){N23_ADD(3,1);return candidate;}
    }
#endif
    for(i=0;i<s->_world->object_count;++i)if(same_ref(s->_world->objects[i].reference,ref)){
        N23_ADD(3,i+1u);return &s->_world->objects[i];
    }
    N23_ADD(3,i);
    return NULL;
}
dok_status dok_collision_get_view(const dok_collision_state *s,dok_collision_view *out)
{
    dok_collision_view v;
    if(out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    if(!valid_state(s))return DOK_STATUS_INVALID_STATE;
    v.width=(uint16_t)s->_world->definition->settings[DOK_LEVEL_WIDTH];
    v.length=(uint16_t)s->_world->definition->settings[DOK_LEVEL_LENGTH];
    v.cell_side=s->_world->definition->settings[DOK_LEVEL_CELL_SIDE];
    v.min_distance=s->_world->definition->settings[DOK_LEVEL_MIN_DISTANCE];
    v.mmap=s->_world->maps[DOK_WORLD_MMAP];v.tmap=s->_world->maps[DOK_WORLD_TMAP];
    v.known=s->known;v.flags=s->flags;
    v.projectile_occupied=s->_projectile_occupied;
    if(!valid_view(&v)||v.min_distance<0||2*(int32_t)v.min_distance>=v.cell_side)
        return DOK_STATUS_INVALID_STATE;
    *out=v;return DOK_STATUS_OK;
}
dok_status dok_collision_bind(dok_collision_state *s,dok_initial_world *w)
{
    const dok_level_definition *d;uint32_t i,j,count;
    if(s==NULL||w==NULL||w->definition==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    d=w->definition;count=w->object_count;
    if(w->epoch==0u||count==0u||count>256u||count!=d->object_count||d->language!=1u||
       d->settings[0]<=0||d->settings[0]>64||d->settings[2]<=0||d->settings[2]>64||
       d->settings[1]!=1||d->settings[3]<=0||d->settings[7]<0||2*(int32_t)d->settings[7]>=d->settings[3])
        return DOK_STATUS_INVALID_STATE;
    for(i=0;i<count;++i){const dok_world_object_seed *o=&w->objects[i];
        if(o->definition_index!=i||o->class_id!=d->objects[i].class_id||
           o->reference.world_epoch!=w->epoch||o->reference.object_id==0u||o->reference.generation==0u||
           w->glyph_index[d->objects[i].glyph]!=i+1u||d->glyph_index[d->objects[i].glyph]!=i+1u)
            return DOK_STATUS_INVALID_STATE;
        for(j=0;j<i;++j)if(same_ref(o->reference,w->objects[j].reference))return DOK_STATUS_INVALID_STATE;
    }
    if(w->glyph_index[32]==0u||w->glyph_index[32]>count||(d->objects[w->glyph_index[32]-1u].flags&0x10u)==0u)
        return DOK_STATUS_INVALID_STATE;
    for(i=0;i<3u;++i)for(j=0;j<(uint32_t)d->settings[0]*(uint32_t)d->settings[2];++j)
        if(w->glyph_index[w->maps[i][j]]==0u||w->glyph_index[w->maps[i][j]]>count)return DOK_STATUS_INVALID_STATE;
    memset(s,0,sizeof(*s));s->_world=w;s->_tag=STATE_TAG;s->_epoch=w->epoch;s->_revision=1;
    for(i=0;i<count;++i){unsigned char g=d->objects[i].glyph;s->known[g]=1;s->flags[g]=d->objects[i].flags;}
    return DOK_STATUS_OK;
}
static dok_status setter(dok_collision_state *s,dok_object_ref ref,uint16_t value,uint32_t door)
{
    const dok_world_object_seed *o=object(s,ref);unsigned char g;
    if(o==NULL)return DOK_STATUS_STALE_REFERENCE;
    if(s->_active!=NULL)return DOK_STATUS_INVALID_STATE;
    if(s->_revision==UINT32_MAX)return DOK_STATUS_LIMIT_REACHED;
    if(door!=0u&&o->class_id!=7u)return DOK_STATUS_INVALID_ARGUMENT;
    g=s->_world->definition->objects[o->definition_index].glyph;
    if(door!=0u)s->door_count[g]=value;else s->flags[g]=value;
    ++s->_revision;return DOK_STATUS_OK;
}
dok_status dok_collision_set_flags(dok_collision_state *s,dok_object_ref ref,uint16_t flags)
{ return setter(s,ref,flags,0); }
dok_status dok_collision_set_door_count(dok_collision_state *s,dok_object_ref ref,uint16_t count)
{ return setter(s,ref,count,1); }
dok_status dok_collision_projectiles(dok_collision_state *s,const unsigned char *bits)
{
    if(!valid_state(s)||s->_active!=NULL)return DOK_STATUS_INVALID_STATE;
    if(s->_revision==UINT32_MAX)return DOK_STATUS_LIMIT_REACHED;
    s->_projectile_occupied=bits;++s->_revision;return DOK_STATUS_OK;
}
uint32_t dok_collision_respawn_origin(const dok_collision_state *s,dok_object_ref ref,uint32_t cell)
{
    if(!valid_state(s)||cell!=s->_respawn_cell)return 0;
    const dok_world_object_seed *o=object(s,ref);if(!o)return 0;
    uint32_t n=(uint32_t)(o-s->_world->objects);
    return (s->_respawn_origins[n/8u]>>(n%8u))&1u;
}
dok_status dok_collision_respawn_overlap(dok_collision_state *s,uint32_t cell)
{
    dok_collision_view v;
    if(dok_collision_get_view(s,&v)!=DOK_STATUS_OK||s->_active)return DOK_STATUS_INVALID_STATE;
    const dok_world_object_seed *hero=object(s,s->_world->hero);
    if(!hero||hero->class_id!=1u||cell>=(uint32_t)v.width*v.length)return DOK_STATUS_INVALID_STATE;
    unsigned char bits[32]={0};uint32_t occupied=0;
    for(uint32_t i=0;i<s->_world->object_count;++i){const dok_world_object_seed *o=&s->_world->objects[i];
        if(o->class_id==9u&&o->pose_known&&native_index(&v,o->x,o->z)==cell){
            bits[i/8u]|=(unsigned char)(1u<<(i%8u));occupied=1;
        }
    }
    if(occupied){uint32_t n=(uint32_t)(hero-s->_world->objects);bits[n/8u]|=(unsigned char)(1u<<(n%8u));}
    s->_respawn_cell=occupied?cell:0u;
    memcpy(s->_respawn_origins,bits,sizeof(bits));return DOK_STATUS_OK;
}
static uint32_t origin_allowed(const dok_collision_state *s,dok_object_ref ref,
    uint32_t cell,unsigned char glyph)
{
    unsigned char current=s->_world->maps[DOK_WORLD_MMAP][cell];
    return current==glyph||current==32u||dok_collision_respawn_origin(s,ref,cell);
}
static uint32_t bounded(const dok_collision_view *v,int32_t x,int32_t z)
{
    int64_t r=v->min_distance;
    return (int64_t)x-r>=0&&(int64_t)z-r>=0&&
        (int64_t)x+r<(int64_t)v->width*v->cell_side&&
        (int64_t)z+r<(int64_t)v->length*v->cell_side;
}
static uint32_t context_valid(const dok_collision_context *c)
{ return c!=NULL&&c->_tag==CONTEXT_TAG&&valid_state(c->_state)&&c->_state->_active==c; }
dok_status dok_collision_begin(dok_collision_state *s,dok_object_ref ref,
    const dok_motion_state *motion,uint32_t elapsed,dok_collision_context *c)
{
    const dok_world_object_seed *o;dok_collision_view v;dok_motion_calculation calc;unsigned char g;
    uint32_t index;
    if(motion==NULL||c==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    if(dok_collision_get_view(s,&v)!=DOK_STATUS_OK||s->_active!=NULL)return DOK_STATUS_INVALID_STATE;
    o=object(s,ref);if(o==NULL)return DOK_STATUS_STALE_REFERENCE;
    if((o->class_id!=1u&&o->class_id!=9u)||o->pose_known==0u)return DOK_STATUS_INVALID_STATE;
    if(s->_revision>UINT32_MAX-DOK_MOTION_MAX_SUBSTEPS)return DOK_STATUS_LIMIT_REACHED;
    if(!bounded(&v,motion->x,motion->z))return DOK_STATUS_INVALID_STATE;
    (void)dok_motion_calculate(motion,elapsed,&calc);
    if(calc.movement_flags!=0u&&2*(((int32_t)calc.state.linear_step+3)/4)>=v.cell_side)
        return DOK_STATUS_LIMIT_REACHED;
    g=s->_world->definition->objects[o->definition_index].glyph;
    index=native_index(&v,motion->x,motion->z);
    if(!origin_allowed(s,ref,index,g))return DOK_STATUS_INVALID_STATE;
    memset(c,0,sizeof(*c));c->_state=s;c->_actor=ref;c->_x=motion->x;c->_y=motion->y;c->_z=motion->z;
    c->_tag=CONTEXT_TAG;c->_glyph=g;s->_active=c;return DOK_STATUS_OK;
}
dok_status dok_collision_query(dok_collision_context *c,const dok_motion_substep *r,dok_collision_plan *out)
{
    dok_collision_plan p;dok_collision_view v;int64_t dx,dz;
    if(r==NULL||out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    if(!context_valid(c)||c->_ordinal>=DOK_MOTION_MAX_SUBSTEPS)return DOK_STATUS_INVALID_STATE;
    if(r->ordinal!=c->_ordinal||r->x!=c->_x||r->y!=c->_y||r->z!=c->_z)
        return DOK_STATUS_INVALID_STATE;
    if(dok_collision_get_view(c->_state,&v)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    memset(&p,0,sizeof(p));p._context=c;p._revision=c->_state->_revision;p._ordinal=c->_ordinal;
    p._x=r->x;p._z=r->z;p._candidate_x=r->candidate_x;p._candidate_z=r->candidate_z;
    p._old_cell=native_index(&v,r->x,r->z);p._new_cell=p._old_cell;p._write=1;
    dx=(int64_t)r->candidate_x-r->x;dz=(int64_t)r->candidate_z-r->z;
    if(!origin_allowed(c->_state,c->_actor,p._old_cell,c->_glyph)){p._reason=DOK_COLLISION_FOREIGN;p._write=0;}
    else if(dx*2<=-v.cell_side||dx*2>=v.cell_side||dz*2<=-v.cell_side||dz*2>=v.cell_side||
            2*(int32_t)r->distance<=-v.cell_side||2*(int32_t)r->distance>=v.cell_side)
        p._reason=DOK_COLLISION_LARGE_STEP;
    else{
        resolve(&v,r->x,r->z,r->candidate_x,r->candidate_z,p._old_cell,&p._x,&p._z);
        if(!bounded(&v,p._x,p._z))p._reason=DOK_COLLISION_WORLD_EDGE;
        else if(!clear_at(&v,p._x,p._z,v.min_distance,p._old_cell))p._reason=DOK_COLLISION_OCCUPIED;
        if(p._reason!=0u){p._x=r->x;p._z=r->z;}
        p._new_cell=native_index(&v,p._x,p._z);
    }
    *out=p;return DOK_STATUS_OK;
}
static void door_change(dok_collision_state *s,unsigned char g,uint32_t increment)
{
    uint16_t i=s->_world->glyph_index[g];
    if(i!=0u&&i<=s->_world->object_count&&s->_world->definition->objects[i-1u].class_id==7u){
        if(increment!=0u)s->door_count[g]=(uint16_t)(s->door_count[g]+1u);
        else if(s->door_count[g]!=0u)--s->door_count[g];
    }
}
dok_status dok_collision_commit(dok_collision_context *c,const dok_collision_plan *p)
{
    dok_collision_state *s;dok_initial_world *w;
    if(p==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    if(!context_valid(c)||p->_context!=c||p->_revision!=c->_state->_revision||p->_ordinal!=c->_ordinal)
        return DOK_STATUS_INVALID_STATE;
    s=c->_state;w=s->_world;
    if(p->_write!=0u){
        door_change(s,w->maps[DOK_WORLD_TMAP][p->_old_cell],0);
        w->maps[DOK_WORLD_MMAP][p->_old_cell]=32u;
        door_change(s,w->maps[DOK_WORLD_TMAP][p->_new_cell],1);
        w->maps[DOK_WORLD_MMAP][p->_new_cell]=c->_glyph;
        if(p->_old_cell!=p->_new_cell&&p->_old_cell==s->_respawn_cell){
            const dok_world_object_seed *o=object(s,c->_actor);
            if(o){uint32_t n=(uint32_t)(o-w->objects);
                s->_respawn_origins[n/8u]&=(unsigned char)~(1u<<(n%8u));}
        }
        ++c->_report.committed;
    }
    ++c->_report.requested;
    if(p->_x!=p->_candidate_x||p->_z!=p->_candidate_z)++c->_report.adjusted;
    if(p->_reason!=0u)++c->_report.rejected;
    c->_report.reason_mask|=p->_reason;c->_x=p->_x;c->_z=p->_z;++c->_ordinal;++s->_revision;
    return DOK_STATUS_OK;
}
void dok_collision_move(void *context,const dok_motion_substep *r,int32_t *x,int32_t *z)
{
    dok_collision_context *c=context;dok_collision_plan p;
    if(r==NULL||x==NULL||z==NULL)return;
    *x=r->x;*z=r->z;
    if(dok_collision_query(c,r,&p)==DOK_STATUS_OK&&dok_collision_commit(c,&p)==DOK_STATUS_OK){
        *x=p._x;*z=p._z;
    }else if(context_valid(c)){++c->_report.requested;++c->_report.rejected;
        c->_report.reason_mask|=DOK_COLLISION_BAD_REQUEST;}
}
dok_status dok_collision_end(dok_collision_context *c,dok_collision_report *out)
{
    if(out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    if(!context_valid(c))return DOK_STATUS_INVALID_STATE;
    *out=c->_report;c->_state->_active=NULL;c->_tag=0;return DOK_STATUS_OK;
}
dok_status dok_collision_camera_clear(const dok_collision_state *s,dok_object_ref ref,
    int32_t selfx,int32_t selfz,int32_t x,int32_t z,uint32_t *out)
{
    dok_collision_view v;const dok_world_object_seed *o;uint32_t own;unsigned char g;
    if(out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    if(dok_collision_get_view(s,&v)!=DOK_STATUS_OK||s->_active!=NULL)return DOK_STATUS_INVALID_STATE;
    o=object(s,ref);if(o==NULL)return DOK_STATUS_STALE_REFERENCE;
    if((o->class_id!=1u&&o->class_id!=9u)||o->pose_known==0u)return DOK_STATUS_INVALID_STATE;
    if(!bounded(&v,selfx,selfz))return DOK_STATUS_INVALID_STATE;
    g=s->_world->definition->objects[o->definition_index].glyph;own=native_index(&v,selfx,selfz);
    if(!origin_allowed(s,ref,own,g))return DOK_STATUS_INVALID_STATE;
    *out=bounded(&v,x,z)?clear_at(&v,x,z,v.min_distance,own):0u;return DOK_STATUS_OK;
}

/* N11: distinct original direct wrapper, not the N09 three-quarter wrapper.
 * Keep accepted N10 functions above unchanged; share their exact primitives. */
dok_status dok_collision_displace(dok_collision_state *s,dok_object_ref ref,
    dok_motion_state *motion,int32_t nx,int32_t nz,dok_collision_report *out)
{
    const dok_world_object_seed *o;dok_collision_view v;
    dok_collision_context context;dok_collision_plan plan;
    dok_collision_report report;dok_status status,ended;unsigned char glyph;
    uint32_t cell;int32_t x,z;
    if(motion==NULL||out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    if(dok_collision_get_view(s,&v)!=DOK_STATUS_OK||s->_active!=NULL)
        return DOK_STATUS_INVALID_STATE;
    o=object(s,ref);if(o==NULL)return DOK_STATUS_STALE_REFERENCE;
    if((o->class_id!=1u&&o->class_id!=9u)||o->pose_known==0u)
        return DOK_STATUS_INVALID_STATE;
    if(s->_revision==UINT32_MAX)return DOK_STATUS_LIMIT_REACHED;
    if(!bounded(&v,motion->x,motion->z))return DOK_STATUS_INVALID_STATE;
    glyph=s->_world->definition->objects[o->definition_index].glyph;
    cell=native_index(&v,motion->x,motion->z);
    if(!origin_allowed(s,ref,cell,glyph))return DOK_STATUS_INVALID_STATE;
    memset(&context,0,sizeof(context));context._state=s;context._actor=ref;
    context._x=motion->x;context._y=motion->y;context._z=motion->z;
    context._tag=CONTEXT_TAG;context._glyph=glyph;
    memset(&plan,0,sizeof(plan));plan._context=&context;plan._revision=s->_revision;
    plan._old_cell=cell;plan._candidate_x=nx;plan._candidate_z=nz;plan._write=1;
    /* No scalar/quarter-distance admission: source class12 at C2=4 moves350.
     * Execute the resolver once even when the raw candidate is outside. */
    resolve(&v,motion->x,motion->z,nx,nz,cell,&x,&z);
    if(!bounded(&v,x,z))plan._reason=DOK_COLLISION_WORLD_EDGE;
    else if(!clear_at(&v,x,z,v.min_distance,cell))plan._reason=DOK_COLLISION_OCCUPIED;
    if(plan._reason!=0u){x=motion->x;z=motion->z;}
    plan._x=x;plan._z=z;plan._new_cell=native_index(&v,x,z);
    s->_active=&context;
    status=dok_collision_commit(&context,&plan);
    ended=dok_collision_end(&context,&report);
    if(status!=DOK_STATUS_OK)return status;
    if(ended!=DOK_STATUS_OK)return ended;
    motion->x=x;motion->z=z;*out=report;return DOK_STATUS_OK;
}
