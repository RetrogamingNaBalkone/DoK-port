#include "dok/runtime.h"

static const uint16_t native_sizes[14]={0x66,0xde,0x54,0x81,0x54,0xa2,0x5a,0x7e,0x83,0xf8,0x87,0,0x6a,0x6b};
static void clear(void *p,size_t n) {uint8_t *b=p;while(n--) *b++=0;}
static void copy(void *p,const void *s,size_t n) {uint8_t *d=p;const uint8_t *b=s;while(n--) *d++=*b++;}
static int actor_class(unsigned c) {return c==1 || c==5 || c==9;}
static int text_valid(const dok_runtime_level *l,dok_runtime_span s) {
    return s.offset<=l->text_size && s.size<=l->text_size-s.offset;
}
dok_runtime_status dok_runtime_validate_level(const dok_runtime_level *l) {
    uint8_t seen[DOK_RUNTIME_MAX_ACTORS]={0};size_t i,j;unsigned players=0;
    if(!l || !l->definitions || !l->fields || !l->text || !l->maps ||
       !l->initial_objects || !l->initial_actors || !l->initial_player) return DOK_RUNTIME_ARGUMENT;
    if(l->object_count>DOK_RUNTIME_MAX_OBJECTS || l->actor_count>DOK_RUNTIME_MAX_ACTORS ||
       l->field_count>DOK_RUNTIME_MAX_FIELDS || l->text_size>DOK_RUNTIME_MAX_TEXT ||
       l->cells>DOK_RUNTIME_MAX_CELLS) return DOK_RUNTIME_CAPACITY;
    if(!l->object_count || !l->settings[DOK_LEVEL_WIDTH] || l->settings[DOK_LEVEL_WIDTH]>64 ||
       l->settings[DOK_LEVEL_WIDTH]!=l->settings[DOK_LEVEL_LENGTH] || l->settings[DOK_LEVEL_HEIGHT]!=1 ||
       (size_t)l->settings[DOK_LEVEL_WIDTH]*l->settings[DOK_LEVEL_LENGTH]!=l->cells ||
       !l->settings[DOK_LEVEL_CELL_SIDE] || l->settings[DOK_LEVEL_CELL_SIDE]>32767 ||
       l->player_index>l->object_count || !text_valid(l,l->palette) || !text_valid(l,l->light))
        return DOK_RUNTIME_DEFINITION;
    for(i=0;i<l->object_count;++i) {
        const dok_runtime_definition *d=&l->definitions[i];
        if(d->class_id>=14 || !native_sizes[d->class_id] || l->glyph_index[d->glyph]!=i+1 ||
           d->field_begin>l->field_count || d->field_count>l->field_count-d->field_begin ||
           !text_valid(l,d->sprite) || !text_valid(l,d->animation) || !text_valid(l,d->sound))
            return DOK_RUNTIME_DEFINITION;
        if(actor_class(d->class_id)) {
            if(d->actor_index>=l->actor_count || seen[d->actor_index]) return DOK_RUNTIME_DEFINITION;
            seen[d->actor_index]=1;
        } else if(d->actor_index!=DOK_RUNTIME_NONE) return DOK_RUNTIME_DEFINITION;
        if(d->class_id==1 && ++players>1) return DOK_RUNTIME_DEFINITION;
        for(j=0;j<d->field_count;++j) {
            const dok_runtime_field *f=&l->fields[d->field_begin+j];
            unsigned width=f->kind==DOK_LEVEL_I32?4:(f->kind==DOK_LEVEL_CHAR?1:2);
            if(f->kind>DOK_LEVEL_TEXT || f->native_offset>=native_sizes[d->class_id]) return DOK_RUNTIME_DEFINITION;
            if(f->kind<=DOK_LEVEL_CHAR) {
                if(width>native_sizes[d->class_id]-f->native_offset) return DOK_RUNTIME_DEFINITION;
            } else if(!text_valid(l,f->value.bytes) ||
                (f->kind==DOK_LEVEL_TEXT && (f->value.bytes.size>39 ||
                  f->value.bytes.size>=native_sizes[d->class_id]-f->native_offset))) return DOK_RUNTIME_DEFINITION;
        }
    }
    for(i=0;i<l->actor_count;++i) if(!seen[i]) return DOK_RUNTIME_DEFINITION;
    for(i=0;i<256;++i) if(l->glyph_index[i]>l->object_count ||
       (l->glyph_index[i] && l->definitions[l->glyph_index[i]-1].glyph!=i)) return DOK_RUNTIME_DEFINITION;
    for(i=0;i<3u*l->cells;++i) if(!l->glyph_index[l->maps[i]]) return DOK_RUNTIME_DEFINITION;
    if(l->player_index && (l->definitions[l->player_index-1].class_id!=1 ||
        !l->definitions[l->player_index-1].placements)) return DOK_RUNTIME_DEFINITION;
    return DOK_RUNTIME_OK;
}
dok_runtime_status dok_runtime_reset(dok_runtime *r,const dok_runtime_level *l) {
    dok_runtime_status status;
    if(!r) return DOK_RUNTIME_ARGUMENT;
    status=dok_runtime_validate_level(l);if(status!=DOK_RUNTIME_OK) return status;
    clear(r,sizeof(*r));r->level=l;
    r->object_count=l->object_count;r->actor_count=l->actor_count;r->player_index=l->player_index;
    r->pickup_99_total=l->pickup_99_total;
    copy(r->objects,l->initial_objects,l->object_count*sizeof(*r->objects));
    copy(r->actors,l->initial_actors,l->actor_count*sizeof(*r->actors));
    r->player=*l->initial_player;copy(r->maps,l->maps,3u*l->cells);
    return DOK_RUNTIME_OK;
}
void dok_runtime_release(dok_runtime *r) {if(r) clear(r,sizeof(*r));}
const dok_runtime_definition *dok_runtime_definition_at(const dok_runtime_level *l,uint16_t i) {
    return l && l->definitions && i<l->object_count?&l->definitions[i]:0;
}
const dok_runtime_actor *dok_runtime_actor_at(const dok_runtime *r,uint16_t i) {
    const dok_runtime_definition *d;
    if(!r || !r->level || i>=r->object_count) return 0;
    d=dok_runtime_definition_at(r->level,i);
    return d && d->actor_index<r->actor_count?&r->actors[d->actor_index]:0;
}
int dok_runtime_position(const dok_runtime *r,uint16_t i,int32_t *x,int32_t *y,int32_t *z) {
    const dok_runtime_actor *a=dok_runtime_actor_at(r,i);
    if(!a || !x || !y || !z || (a->known&7u)!=7u) return 0;
    *x=a->x;*y=a->y;*z=a->z;return 1;
}
/* A byte adapter supports exact mask comparison without allocating a native
 * object. Constants below are verified constructor effects from stage6. */
static int byte_of(size_t target,size_t offset,unsigned width,uint32_t value,uint8_t *out) {
    if(target<offset || target-offset>=width) return 0;
    *out=(uint8_t)(value>>(8u*(unsigned)(target-offset)));return 1;
}
static int read_byte(const dok_runtime *r,uint16_t index,size_t off,uint8_t *out) {
    const dok_runtime_level *l=r->level;const dok_runtime_definition *d=&l->definitions[index];
    const dok_runtime_object *o=&r->objects[index];const dok_runtime_actor *a=dok_runtime_actor_at(r,index);
    const dok_runtime_player *p=&r->player;size_t j;
    if(off>=native_sizes[d->class_id]) return 0;
#define B(offset,width,value) do {if(byte_of(off,offset,width,(uint32_t)(value),out)) return 1;} while(0)
    B(0,2,0);B(2,2,4);B(0x12,2,d->class_id);B(0x14,1,d->glyph);
    B(0x15,2,o->flags);B(0x17,2,d->width_parameter);B(0x19,2,d->class_id==9 && o->state.monster.corpse?0:d->height_parameter);B(0x1b,1,d->marker);
    B(0x24,2,o->animation_budget);
    /* Active AI stores native nextcounter in its scheduler, not displayed frame. */
    if(d->class_id!=9 || !o->state.monster.state) {B(0x50,2,o->frame);}
    if(o->known&DOK_RUNTIME_INTERVAL) {B(0x52,2,o->interval);}
    if(d->flags&0x800) {
        if(o->known&DOK_RUNTIME_ANIMATION_A) {B(0x20,2,o->animation_a);}
        if(o->known&DOK_RUNTIME_ANIMATION_B) {B(0x22,2,o->animation_b);}
    }
    if(a) {
        if(a->known&DOK_RUNTIME_X) {B(0x26,4,a->x);}
        if(a->known&DOK_RUNTIME_Y) {B(0x2a,4,a->y);}
        if(a->known&DOK_RUNTIME_Z) {B(0x2e,4,a->z);}
        if(a->known&DOK_RUNTIME_BOUNDS) {B(0x9e,2,a->bound_9e);B(0xa0,2,a->bound_a0);}
        B(0x1c,4,a->motion_flags);B(0x32,2,a->heading);B(0x34,2,a->angle_34);B(0x36,2,a->mode);
        B(0x38,4,a->base_speed);B(0x3c,4,a->step_distance);B(0x5a,2,a->layer);
        B(0x5c,1,a->glyph);B(0x5d,1,a->previous_glyph);B(0x7c,2,a->direction);B(0x7e,2,a->step);
        B(0x80,2,a->ready);B(0x82,2,a->command);B(0x84,2,a->command_84);
        B(0x90,4,a->accumulator_a);B(0x94,4,a->accumulator_b);B(0x98,2,a->parameter_98);
        B(0x86,2,0);B(0x8c,4,0); /* empty native list: count/null data */
    }
    if(d->class_id==1) {
        B(0x5e,2,p->health);B(0x60,2,p->energy);B(0x62,2,p->health_max);B(0x64,2,p->energy_max);
        for(j=0;j<6;++j) {B(0x66+2*j,2,p->parameters_66_70[j]);}
        B(0x72,2,p->keys);
        for(j=0;j<3;++j) {B(0x76+2*j,2,p->parameters_76_7a[j]);}
        for(j=0;j<9;++j) if(!p->weapons[j]) {B(0xaa+4*j,4,0);}
        B(0xce,2,p->selected_weapon);B(0xd0,2,p->pending_weapon);B(0xd6,2,p->lives);
        B(0xd8,2,p->score);B(0xda,4,p->auxiliary_da);
    } else if(d->class_id==6) {
        B(0x54,2,o->state.toggle.command);B(0x56,2,o->state.toggle.progress);B(0x58,2,o->state.toggle.target);
    } else if(d->class_id==7) {
        const dok_runtime_door *v=&o->state.door;
        B(0x54,1,v->parameter_54);B(0x55,1,v->parameter_55);B(0x56,2,v->state_56);B(0x58,2,v->state_58);
        B(0x5a,2,v->state_5a);B(0x5c,2,v->parameter_5c);B(0x5e,2,v->parameter_5e);B(0x60,2,v->extent);
        B(0x66,4,v->parameter_66);B(0x6a,2,v->original_flags);
    } else if(d->class_id==8) {
        const dok_runtime_projectile *v=&o->state.projectile;
        B(0x3c,4,v->velocity);B(0x2a,4,v->y);B(0x58,2,v->state);B(0x5a,2,v->layer);
        B(0x5c,2,v->damage);B(0x5e,2,v->half_damage);B(0x68,1,v->previous_glyph);
        if(o->known&DOK_RUNTIME_ANIMATION_A) {B(0x54,2,o->animation_a);}
        if(o->known&DOK_RUNTIME_ANIMATION_B) {B(0x56,2,o->animation_b);}
        B(0x6d,2,0);B(0x73,4,0);
    } else if(d->class_id==9) {
        B(0xa2,2,o->state.monster.state);B(0xa4,2,o->state.monster.health);B(0xe6,1,o->state.monster.auxiliary_e6);
        if(o->state.monster.course_counter_known) {B(0xac,2,o->state.monster.course_counter);}
        B(0xb0,2,0);B(0xb6,4,0);
    } else if(d->class_id==10) {B(0x83,2,o->state.weapon_layer);}
    for(j=0;j<d->field_count;++j) {
        const dok_runtime_field *f=&l->fields[d->field_begin+j];
        if(f->kind<=DOK_LEVEL_CHAR) {
            unsigned width=f->kind==DOK_LEVEL_I32?4:(f->kind==DOK_LEVEL_CHAR?1:2);
            B(f->native_offset,width,f->value.number);
        } else if(f->kind==DOK_LEVEL_TEXT && off>=f->native_offset) {
            size_t at=off-f->native_offset;
            if(at<f->value.bytes.size) {*out=l->text[f->value.bytes.offset+at];return 1;}
            if(at==f->value.bytes.size) {*out=0;return 1;}
        }
    }
#undef B
    return 0;
}
int dok_runtime_read_u(const dok_runtime *r,uint16_t index,size_t off,unsigned width,uint32_t *out) {
    uint32_t value=0;unsigned i;uint8_t b;
    if(!r || !r->level || !out || index>=r->object_count || (width!=1 && width!=2 && width!=4) ||
       off>DOK_WORLD_STATE_BYTES || width>DOK_WORLD_STATE_BYTES-off) return 0;
    for(i=0;i<width;++i) {if(!read_byte(r,index,off+i,&b)) return 0;value|=(uint32_t)b<<(8*i);}
    *out=value;return 1;
}
int dok_runtime_read_s(const dok_runtime *r,uint16_t index,size_t off,unsigned width,int32_t *out) {
    uint32_t bits,limit;
    if(!out || !dok_runtime_read_u(r,index,off,width,&bits)) return 0;
    limit=width==4?UINT32_MAX:((UINT32_C(1)<<(width*8))-1);
    *out=bits<=limit/2?(int32_t)bits:-1-(int32_t)(limit-bits);return 1;
}

#ifndef DOK_RUNTIME_ROM_ONLY
static dok_runtime_status add_span(dok_runtime_level *out,dok_runtime_storage *s,const dok_level *l,
    dok_level_span in,dok_runtime_span *span) {
    if(in.offset>l->source_size || in.size>l->source_size-in.offset) return DOK_RUNTIME_SOURCE;
    if(in.size>DOK_RUNTIME_MAX_TEXT-out->text_size) return DOK_RUNTIME_CAPACITY;
    span->offset=out->text_size;span->size=(uint16_t)in.size;
    copy(s->text+out->text_size,l->source+in.offset,in.size);out->text_size=(uint16_t)(out->text_size+in.size);
    return DOK_RUNTIME_OK;
}
dok_runtime_status dok_runtime_build(dok_runtime_level *out,dok_runtime_storage *s,
    const dok_world *source,dok_runtime_error *error) {
    dok_runtime_error e={DOK_RUNTIME_OK,0,0};const dok_level *l;size_t i,j;unsigned players=0;
    if(!out || !s || !source || !source->level || !source->objects || !source->maps) {
        e.status=DOK_RUNTIME_ARGUMENT;goto done;
    }
    l=source->level;
    if(!l->source || !l->objects || source->cells>DOK_RUNTIME_MAX_CELLS ||
       l->object_count>DOK_RUNTIME_MAX_OBJECTS) {e.status=DOK_RUNTIME_CAPACITY;goto done;}
    clear(out,sizeof(*out));clear(s,sizeof(*s));
    out->definitions=s->definitions;out->fields=s->fields;out->text=s->text;out->maps=s->maps;
    out->initial_objects=s->objects;out->initial_actors=s->actors;out->initial_player=&s->player;
    out->object_count=(uint16_t)l->object_count;out->cells=(uint16_t)source->cells;
    out->player_index=source->player_index;out->pickup_99_total=source->pickup_99_total;
    copy(out->settings,l->settings,sizeof(out->settings));copy(out->glyph_index,l->glyph_index,sizeof(out->glyph_index));
    copy(s->maps,source->maps,3u*source->cells);
    e.status=add_span(out,s,l,l->palette,&out->palette);if(e.status) goto done;
    e.status=add_span(out,s,l,l->light,&out->light);if(e.status) goto done;
    for(i=0;i<l->object_count;++i) {
        const dok_level_object *d=&l->objects[i];const dok_world_object *w=&source->objects[i];
        dok_runtime_definition *rd=&s->definitions[i];dok_runtime_object *o=&s->objects[i];int32_t value;uint32_t u;
        e.object_index=(uint16_t)i;
        if(d->class_id>=14 || !native_sizes[d->class_id] || w->native_size!=native_sizes[d->class_id] ||
           d->field_count>DOK_LEVEL_MAX_FIELDS) {e.status=DOK_RUNTIME_SOURCE;goto done;}
        rd->class_id=d->class_id;rd->flags=d->flags;rd->sprite_flags=d->sprite_flags;
        rd->width_parameter=d->width_parameter;rd->height_parameter=d->height_parameter;
        rd->glyph=d->glyph;rd->marker=d->marker;rd->placements=w->placements;rd->actor_index=DOK_RUNTIME_NONE;
        rd->field_begin=out->field_count;rd->field_count=d->field_count;
        if(out->field_count+d->field_count>DOK_RUNTIME_MAX_FIELDS) {e.status=DOK_RUNTIME_CAPACITY;goto done;}
        for(j=0;j<d->field_count;++j) {
            const dok_level_field *f=&d->fields[j];dok_runtime_field *rf=&s->fields[out->field_count++];
            rf->native_offset=f->native_offset;rf->kind=f->kind;
            if(f->kind<=DOK_LEVEL_CHAR) rf->value.number=f->value.number;
            else {e.status=add_span(out,s,l,f->value.bytes,&rf->value.bytes);if(e.status) goto done;}
        }
#define SPAN(field) do {e.status=add_span(out,s,l,d->field,&rd->field);if(e.status) goto done;} while(0)
        SPAN(sprite);SPAN(animation);
        SPAN(sound);
#undef SPAN
#define READ(target,offset,width) do {e.native_offset=offset;if(!dok_world_read_s(w,offset,width,&value)) {e.status=DOK_RUNTIME_SOURCE;goto done;}target=value;} while(0)
        READ(o->flags,0x15,2);READ(o->frame,0x50,2);READ(o->animation_budget,0x24,2);
        if(dok_world_read_u(w,0x52,2,&u)) {o->interval=(uint16_t)u;o->known|=DOK_RUNTIME_INTERVAL;}
        if(dok_world_read_s(w,0x20,2,&value)) {o->animation_a=(int16_t)value;o->known|=DOK_RUNTIME_ANIMATION_A;}
        if(dok_world_read_s(w,0x22,2,&value)) {o->animation_b=(int16_t)value;o->known|=DOK_RUNTIME_ANIMATION_B;}
        if(actor_class(d->class_id)) {
            dok_runtime_actor *a;
            if(out->actor_count==DOK_RUNTIME_MAX_ACTORS) {e.status=DOK_RUNTIME_CAPACITY;goto done;}
            rd->actor_index=out->actor_count++;a=&s->actors[rd->actor_index];
            if(dok_world_read_s(w,0x26,4,&value)) {a->x=value;a->known|=DOK_RUNTIME_X;}
            if(dok_world_read_s(w,0x2a,4,&value)) {a->y=value;a->known|=DOK_RUNTIME_Y;}
            if(dok_world_read_s(w,0x2e,4,&value)) {a->z=value;a->known|=DOK_RUNTIME_Z;}
            if(dok_world_read_s(w,0x9e,2,&value)) {
                a->bound_9e=(int16_t)value;READ(a->bound_a0,0xa0,2);a->known|=DOK_RUNTIME_BOUNDS;
            }
            READ(a->base_speed,0x38,4);READ(a->step_distance,0x3c,4);
            READ(a->accumulator_a,0x90,4);READ(a->accumulator_b,0x94,4);READ(a->motion_flags,0x1c,4);
            READ(a->heading,0x32,2);READ(a->angle_34,0x34,2);READ(a->mode,0x36,2);READ(a->layer,0x5a,2);
            READ(a->direction,0x7c,2);READ(a->step,0x7e,2);READ(a->ready,0x80,2);READ(a->command,0x82,2);
            READ(a->command_84,0x84,2);READ(a->parameter_98,0x98,2);READ(a->glyph,0x5c,1);READ(a->previous_glyph,0x5d,1);
        }
        if(d->class_id==1) {
            dok_runtime_player *p=&s->player;
            if(++players>1) {e.status=DOK_RUNTIME_DEFINITION;goto done;}
            READ(p->health,0x5e,2);READ(p->energy,0x60,2);READ(p->health_max,0x62,2);READ(p->energy_max,0x64,2);
            for(j=0;j<6;++j) {READ(p->parameters_66_70[j],0x66+2*j,2);}
            READ(p->keys,0x72,2);for(j=0;j<3;++j) {READ(p->parameters_76_7a[j],0x76+2*j,2);}
            for(j=0;j<9;++j) {
                if(!dok_world_read_u(w,0xaa+4*j,4,&u) || u) {e.status=DOK_RUNTIME_SOURCE;goto done;}
                p->weapons[j]=0;
            }
            READ(p->selected_weapon,0xce,2);READ(p->pending_weapon,0xd0,2);READ(p->lives,0xd6,2);
            READ(p->score,0xd8,2);READ(p->auxiliary_da,0xda,4);
        } else if(d->class_id==6) {
            READ(o->state.toggle.command,0x54,2);READ(o->state.toggle.progress,0x56,2);READ(o->state.toggle.target,0x58,2);
        } else if(d->class_id==7) {
            dok_runtime_door *v=&o->state.door;
            READ(v->parameter_54,0x54,1);READ(v->parameter_55,0x55,1);READ(v->state_56,0x56,2);READ(v->state_58,0x58,2);
            READ(v->state_5a,0x5a,2);READ(v->parameter_5c,0x5c,2);READ(v->parameter_5e,0x5e,2);
            READ(v->extent,0x60,2);READ(v->parameter_66,0x66,4);READ(v->original_flags,0x6a,2);
        } else if(d->class_id==8) {
            dok_runtime_projectile *v=&o->state.projectile;
            READ(v->velocity,0x3c,4);READ(v->y,0x2a,4);READ(v->layer,0x5a,2);
            READ(v->damage,0x5c,2);READ(v->half_damage,0x5e,2);READ(v->state,0x58,2);READ(v->previous_glyph,0x68,1);
            READ(o->animation_a,0x54,2);READ(o->animation_b,0x56,2);
            o->known|=DOK_RUNTIME_ANIMATION_A|DOK_RUNTIME_ANIMATION_B;
        } else if(d->class_id==9) {
            READ(o->state.monster.health,0xa4,2);READ(o->state.monster.state,0xa2,2);READ(o->state.monster.auxiliary_e6,0xe6,1);
            if(dok_world_read_s(w,0xac,2,&value)) {o->state.monster.course_counter=(int16_t)value;o->state.monster.course_counter_known=1;}
        } else if(d->class_id==10) {READ(o->state.weapon_layer,0x83,2);}
#undef READ
    }
    e.status=dok_runtime_validate_level(out);
done:
    if(error) *error=e;
    return e.status;
}
#endif



