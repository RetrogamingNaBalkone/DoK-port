#include "dok/world.h"
#include "dok/animation.h"

static const uint16_t sizes[14]={0x66,0xde,0x54,0x81,0x54,0xa2,0x5a,0x7e,0x83,0xf8,0x87,0,0x6a,0x6b};
static void put(dok_world_object *o,size_t offset,unsigned width,uint32_t value) {
    unsigned i;
    for(i=0;i<width;++i) {
        size_t n=offset+i;
        o->bytes[n]=(uint8_t)(value>>(8*i));
        o->known[n/8]|=(uint8_t)(1u<<(n%8));
    }
}
int dok_world_read_u(const dok_world_object *o,size_t offset,unsigned width,uint32_t *out) {
    uint32_t n=0;unsigned i;
    if(!o || !out || (width!=1 && width!=2 && width!=4) || o->native_size>DOK_WORLD_STATE_BYTES ||
       offset>o->native_size || width>o->native_size-offset) return 0;
    for(i=0;i<width;++i) {
        if(!(o->known[(offset+i)/8]&(1u<<((offset+i)%8)))) return 0;
        n|=(uint32_t)o->bytes[offset+i]<<(8*i);
    }
    *out=n;return 1;
}
int dok_world_read_s(const dok_world_object *o,size_t offset,unsigned width,int32_t *out) {
    uint32_t n,limit;
    if(!out || !dok_world_read_u(o,offset,width,&n)) return 0;
    limit=width==4?UINT32_MAX:((UINT32_C(1)<<(width*8))-1);
    *out=n<=limit/2?(int32_t)n:-1-(int32_t)(limit-n);
    return 1;
}
static int32_t signed_word(int32_t value) {
    uint32_t bits=(uint32_t)value&65535u;
    return bits<=32767?(int32_t)bits:-1-(int32_t)(65535u-bits);
}
static void list10(dok_world_object *o,size_t base) {put(o,base,2,0);put(o,base+6,4,0);}
static int span_valid(const dok_level *l,dok_level_span s) {
    return s.offset<=l->source_size && s.size<=l->source_size-s.offset;
}
static int first_frame(const dok_level *l,const dok_res_archive *a,dok_level_span name,dok_animation_frame *frame) {
    dok_res_entry e;dok_animation_view animation;
    if(!a || !span_valid(l,name) || !name.size ||
       dok_res_find(a,l->source+name.offset,name.size,&e)!=DOK_RES_OK) return 0;
    if(e.offset>a->size || e.size>a->size-e.offset) return 0;
    return dok_animation_open(a->data+e.offset,e.size,&animation)==DOK_ASSET_OK &&
        dok_animation_frame_at(&animation,0,frame)==DOK_ASSET_OK;
}
static dok_world_status initialize(const dok_level *l,const dok_res_archive *a,
    const dok_level_object *d,dok_world_object *o) {
    size_t i;dok_animation_frame f;unsigned type=d->class_id;
    if(type>=14 || !sizes[type] || d->field_count>DOK_LEVEL_MAX_FIELDS) return DOK_WORLD_DEFINITION;
    o->native_size=sizes[type];o->placements=0;
    for(i=0;i<DOK_WORLD_STATE_BYTES;++i) o->bytes[i]=0;
    for(i=0;i<sizeof(o->known);++i) o->known[i]=0;
    put(o,0,2,0);put(o,2,2,4); /* Event owner count/capacity; allocated pointers unknown. */
    put(o,0x12,2,type);put(o,0x14,1,d->glyph);put(o,0x15,2,d->flags);
    put(o,0x17,2,(uint32_t)d->width_parameter);put(o,0x19,2,(uint32_t)d->height_parameter);
    put(o,0x1b,1,d->marker);put(o,0x24,2,0);put(o,0x50,2,0);
    if(d->flags&0x800) {
        if(!first_frame(l,a,d->animation,&f)) return DOK_WORLD_RESOURCE;
        put(o,0x20,2,(uint32_t)f.parameter_a);put(o,0x22,2,(uint32_t)f.parameter_b);
        put(o,0x52,2,f.interval_bits); /* -1 then next -> frame 0. */
    }
    if(type==1 || type==5 || type==9) {
        list10(o,0x86);put(o,0x5c,1,d->glyph);put(o,0x5d,1,32);
        put(o,0x3c,4,0);put(o,0x36,2,3);put(o,0x7e,2,0);put(o,0x7c,2,0);
        put(o,0x94,4,0);put(o,0x90,4,0);put(o,0x98,2,1);put(o,0x80,2,1);
        put(o,0x84,2,0);put(o,0x82,2,0);put(o,0x5a,2,0);put(o,0x1c,4,0);
    }
    if(type==1) {
        put(o,0x5a,2,4);put(o,0xd6,2,3);
        for(i=0xaa;i<0xce;i+=2) put(o,i,2,0);
        put(o,0xd0,2,0);put(o,0xce,2,0);put(o,0xda,4,0);put(o,0xd8,2,0);
    } else if(type==6) {
        put(o,0x58,2,0);put(o,0x52,2,0);put(o,0x56,2,0);put(o,0x54,2,0);
    } else if(type==7) {
        put(o,0x54,1,2);put(o,0x55,1,1);put(o,0x5c,2,0);put(o,0x52,2,0);
        put(o,0x5a,2,0);put(o,0x58,2,0);put(o,0x60,2,1);put(o,0x56,2,0);
        put(o,0x6a,2,d->flags);
    } else if(type==8) {
        list10(o,0x6d);put(o,0x68,1,32);put(o,0x50,2,0);put(o,0x58,2,0);
        if(!first_frame(l,a,d->animation,&f)) return DOK_WORLD_RESOURCE;
        put(o,0x54,2,(uint32_t)f.parameter_a);put(o,0x56,2,(uint32_t)f.parameter_b);
        put(o,0x52,2,f.interval_bits);
    } else if(type==9) {
        list10(o,0xb0);put(o,0xa2,2,0);put(o,0xe6,1,0);
    } else if(type==10) put(o,0x83,2,4);
    else if(type==12) {put(o,0x52,2,0);put(o,0x50,2,18);}
    /* Virtual stream reader runs AFTER constructor. Never overwrite with defaults later. */
    for(i=0;i<d->field_count;++i) {
        const dok_level_field *v=&d->fields[i];size_t off=v->native_offset;
        unsigned width=v->kind==DOK_LEVEL_I32?4:(v->kind==DOK_LEVEL_CHAR?1:2);
        if(v->kind<=DOK_LEVEL_CHAR) {
            if(off>o->native_size || width>o->native_size-off) return DOK_WORLD_DEFINITION;
            put(o,off,width,(uint32_t)v->value.number);
        } else if(v->kind==DOK_LEVEL_TEXT) {
            size_t j;dok_level_span s=v->value.bytes;
            if(!span_valid(l,s) || s.size>39 || off>=o->native_size || s.size>=o->native_size-off)
                return DOK_WORLD_DEFINITION;
            for(j=0;j<s.size;++j) put(o,off+j,1,l->source[s.offset+j]);
            put(o,off+s.size,1,0);
        } else if(v->kind>DOK_LEVEL_TEXT) return DOK_WORLD_DEFINITION;
    }
    if(type==8) {
        uint32_t bits;int32_t damage;
        if(!dok_world_read_u(o,0x3c,4,&bits) || !dok_world_read_s(o,0x5c,2,&damage)) return DOK_WORLD_DEFINITION;
        put(o,0x3c,4,0u-bits);put(o,0x2a,4,(uint32_t)signed_word(-(int32_t)d->height_parameter));
        /* x86 SAR rounds negative odd damage toward -infinity. */
        put(o,0x5e,2,(uint32_t)(damage>=0?damage/2:-((-damage+1)/2)));
    }
    return DOK_WORLD_OK;
}
dok_world_status dok_world_build(dok_world *out,const dok_level *l,const dok_res_archive *a,
    dok_world_object *objects,size_t object_capacity,uint8_t *maps,size_t map_capacity,dok_world_error *error) {
    dok_world_error e={DOK_WORLD_OK,0,0,0,0};size_t i,p,x,z,width,cells;uint32_t total=0;
    if(!out || !l || !l->source || !l->objects || !l->maps || !objects || !maps) {
        e.status=DOK_WORLD_ARGUMENT;goto done;
    }
    width=l->settings[DOK_LEVEL_WIDTH];cells=width*width;
    if(!width || width>64 || l->settings[DOK_LEVEL_LENGTH]!=width ||
       l->settings[DOK_LEVEL_HEIGHT]!=1 || !l->settings[DOK_LEVEL_CELL_SIDE] ||
       l->settings[DOK_LEVEL_CELL_SIDE]>32767 || !l->object_count || l->object_count>256) {
        e.status=DOK_WORLD_PROFILE;goto done;
    }
    if(object_capacity<l->object_count || cells>map_capacity/4) {e.status=DOK_WORLD_CAPACITY;goto done;}
    out->level=l;out->objects=objects;out->maps=maps;out->cells=cells;out->player_index=0;out->pickup_99_total=0;
    for(i=0;i<l->object_count;++i) {
        e.object_index=(uint16_t)i;e.glyph=l->objects[i].glyph;
        if(l->glyph_index[e.glyph]!=i+1) {e.status=DOK_WORLD_DEFINITION;goto done;}
        e.status=initialize(l,a,&l->objects[i],&objects[i]);if(e.status!=DOK_WORLD_OK) goto done;
    }
    for(i=0;i<cells;++i) maps[3*cells+i]=0;
    for(p=0;p<3;++p) for(z=0;z<width;++z) for(x=0;x<width;++x) {
        size_t source_plane=p==0?1:(p==1?0:2);uint8_t glyph=l->maps[source_plane*cells+(width-1-z)*width+x];
        uint16_t index;
        if(glyph=='.') glyph=32;
        maps[p*cells+z*width+x]=glyph;index=l->glyph_index[glyph];
        if(!index || index>l->object_count) {
            e.status=DOK_WORLD_UNDEFINED_GLYPH;e.plane=(uint8_t)p;e.cell=(uint16_t)(z*width+x);e.glyph=glyph;goto done;
        }
    }
    /* Native order: scan runtime rows, then columns; last duplicate actor wins. */
    for(z=0;z<width;++z) for(x=0;x<width;++x) {
        size_t cell=z*width+x;uint16_t idx=l->glyph_index[maps[cells+cell]];
        dok_world_object *o=&objects[idx-1];uint32_t cls;int32_t key,value;
        dok_world_read_u(o,0x12,2,&cls);
        if(cls==3 && dok_world_read_s(o,0x54,2,&key) && key==99 && dok_world_read_s(o,0x56,2,&value))
            total=(total+(uint32_t)value)&65535u;
        idx=l->glyph_index[maps[cell]];o=&objects[idx-1];dok_world_read_u(o,0x12,2,&cls);
        if(cls==1 || cls==9) {
            int32_t y;int32_t side=l->settings[DOK_LEVEL_CELL_SIDE];
            if(!dok_world_read_s(o,0x19,2,&y)) {e.status=DOK_WORLD_DEFINITION;goto done;}
            y=signed_word(-y);
            put(o,0x26,4,(uint32_t)(side*(int32_t)x+side/2));
            put(o,0x2a,4,(uint32_t)y);put(o,0x2e,4,(uint32_t)(side*(int32_t)z+side/2));
            put(o,0x9e,2,(uint32_t)(y+(cls==1?26:0)));
            put(o,0xa0,2,(uint32_t)(y-(cls==1?26:0)));++o->placements;
            if(cls==1) out->player_index=idx;
        }
    }
    out->pickup_99_total=(int16_t)signed_word((int32_t)total);
done:
    if(error) *error=e;
    return e.status;
}
