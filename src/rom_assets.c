#include "dok/rom_assets.h"

_Static_assert(sizeof(dok_runtime_definition)==32 && _Alignof(dok_runtime_definition)<=4,"ROM definition ABI");
_Static_assert(sizeof(dok_runtime_field)==8 && offsetof(dok_runtime_field,value)==4,"ROM field ABI");
_Static_assert(sizeof(dok_runtime_object)==36 && offsetof(dok_runtime_object,state)==16,"ROM object ABI");
_Static_assert(sizeof(dok_runtime_actor)==64 && offsetof(dok_runtime_actor,x)==4 && offsetof(dok_runtime_actor,heading)==36,"ROM actor ABI");
_Static_assert(sizeof(dok_runtime_player)==60 && offsetof(dok_runtime_player,weapons)==36 && offsetof(dok_runtime_player,auxiliary_da)==56,"ROM player ABI");
_Static_assert(offsetof(dok_runtime_definition,actor_index)==12 && offsetof(dok_runtime_definition,sprite)==20,"ROM definition offsets");
_Static_assert(offsetof(dok_runtime_actor,bound_9e)==56 && offsetof(dok_runtime_actor,glyph)==60,"ROM actor offsets");
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error ROM version1 requires a little-endian host/target
#endif
static uint16_t u16(const uint8_t *p) {return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static uint32_t u32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int16_t s16(const uint8_t *p) {uint16_t n=u16(p);return n<=32767?(int16_t)n:(int16_t)(-1-(int16_t)(65535u-n));}
static int zero(const uint8_t *p,size_t n) {while(n--) if(*p++) return 0;return 1;}
static int equal(const uint8_t *a,const uint8_t *b,size_t n) {while(n--) if(*a++!=*b++) return 0;return 1;}
static uint32_t data_start(const dok_rom_assets *a) {return a->view_offset+(uint32_t)a->level_count*a->language_count*DOK_ROM_VIEW_SIZE;}
static dok_rom_status range(const dok_rom_assets *a,uint32_t off,size_t count,size_t stride) {
    if(off&3u) return DOK_ROM_ALIGNMENT;
    if(off<data_start(a) || off>a->size || !stride || count>(a->size-off)/stride) return DOK_ROM_RANGE;
    return DOK_ROM_OK;
}
static unsigned name_size(const uint8_t *name) {unsigned n=0;while(n<16 && name[n]) ++n;return n;}
static int compare(const uint8_t *a,size_t an,const uint8_t *b,size_t bn) {
    size_t i,n=an<bn?an:bn;
    for(i=0;i<n;++i) if(a[i]!=b[i]) return a[i]<b[i]?-1:1;
    return an==bn?0:(an<bn?-1:1);
}
dok_rom_status dok_rom_find(uint16_t *out,const dok_rom_assets *a,const uint8_t *name,size_t size) {
    uint32_t low=0,high;
    if(!out || !a || !a->data || !name || !size || size>15) return DOK_ROM_ARGUMENT;
    high=a->resource_count;
    while(low<high) {
        uint32_t mid=low+(high-low)/2;const uint8_t *r=a->data+a->resource_offset+mid*32;
        int c=compare(name,size,r,name_size(r));
        if(!c) {*out=(uint16_t)(mid+1);return DOK_ROM_OK;}
        if(c<0) high=mid;else low=mid+1;
    }
    return DOK_ROM_NOT_FOUND;
}
dok_rom_status dok_rom_resource_at(dok_rom_resource *out,const dok_rom_assets *a,uint16_t id,unsigned language) {
    const uint8_t *r;uint32_t offset,size;dok_rom_resource result;dok_rom_status status;
    if(!out || !a || !a->data) return DOK_ROM_ARGUMENT;
    if(language>=a->language_count) return DOK_ROM_LANGUAGE_ERROR;
    if(!id || id>a->resource_count) return DOK_ROM_NOT_FOUND;
    r=a->data+a->resource_offset+(uint32_t)(id-1)*32;
    offset=u32(r+16+language*8);size=u32(r+20+language*8);
    if(!offset && !size) return DOK_ROM_NOT_FOUND;
    status=range(a,offset,size,1);if(status) return status;
    result.data=a->data+offset;result.size=size;result.id=id;*out=result;return DOK_ROM_OK;
}
static const uint8_t *view_record(const dok_rom_assets *a,unsigned level,unsigned language) {
    return a->data+a->view_offset+(level*a->language_count+language)*80;
}
dok_rom_status dok_rom_binding(uint16_t *out,const dok_rom_assets *a,unsigned level,unsigned language,uint16_t object,uint16_t slot) {
    const uint8_t *v,*records;uint32_t low=0,high;dok_rom_status status;
    if(!out || !a || !a->data) return DOK_ROM_ARGUMENT;
    if(level>=a->level_count) return DOK_ROM_NOT_FOUND;
    if(language>=a->language_count) return DOK_ROM_LANGUAGE_ERROR;
    v=view_record(a,level,language);high=u16(v+76);
    status=range(a,u32(v+72),high,8);if(status) return status;
    records=a->data+u32(v+72);
    while(low<high) {
        uint32_t mid=low+(high-low)/2;const uint8_t *r=records+mid*8;
        uint16_t ro=u16(r),rs=u16(r+2);
        if(ro==object && rs==slot) {*out=u16(r+4);return DOK_ROM_OK;}
        if(ro<object || (ro==object && rs<slot)) low=mid+1;else high=mid;
    }
    return DOK_ROM_NOT_FOUND;
}
static int binding_span(const dok_runtime_level *l,uint16_t object,uint16_t slot,dok_runtime_span *s,unsigned *kind) {
    const dok_runtime_definition *d;size_t i;
    if(object==DOK_ROM_GLOBAL_OBJECT) {
        *kind=255;
        if(slot==DOK_ROM_SLOT_PALETTE) {*s=l->palette;return 1;}
        if(slot==DOK_ROM_SLOT_LIGHT) {*s=l->light;return 1;}
        return 0;
    }
    if(object>=l->object_count) return 0;
    d=&l->definitions[object];
    if(slot==DOK_ROM_SLOT_SPRITE) {*s=d->sprite;*kind=DOK_LEVEL_IMAGE;return 1;}
    if(slot==DOK_ROM_SLOT_ANIMATION) {*s=d->animation;*kind=DOK_LEVEL_ANIMATION;return 1;}
    if(slot==DOK_ROM_SLOT_SOUND) {*s=d->sound;*kind=DOK_LEVEL_SOUND;return 1;}
    for(i=0;i<d->field_count;++i) {
        const dok_runtime_field *f=&l->fields[d->field_begin+i];
        if(f->native_offset==slot && f->kind>=DOK_LEVEL_IMAGE && f->kind<=DOK_LEVEL_SOUND) {
            *s=f->value.bytes;*kind=f->kind;return 1;
        }
    }
    return 0;
}
static dok_rom_status check_binding(const dok_rom_assets *a,unsigned level,unsigned language,
    const dok_runtime_level *l,uint16_t object,uint16_t slot,dok_runtime_span span) {
    uint16_t expected,actual;dok_rom_status status;
    if(!span.size) return DOK_ROM_OK;
    status=dok_rom_find(&expected,a,l->text+span.offset,span.size);if(status) return DOK_ROM_REFERENCE;
    status=dok_rom_binding(&actual,a,level,language,object,slot);
    if(status || actual!=expected) return DOK_ROM_REFERENCE;
    return DOK_ROM_OK;
}
dok_rom_status dok_rom_level(dok_runtime_level *out,const dok_rom_assets *a,unsigned level,unsigned language) {
    const uint8_t *v;dok_runtime_level l;dok_rom_status status;size_t i,j;uint32_t offsets[8];
    if(!out || !a || !a->data) return DOK_ROM_ARGUMENT;
    if(level>=a->level_count) return DOK_ROM_NOT_FOUND;
    if(language>=a->language_count) return DOK_ROM_LANGUAGE_ERROR;
    v=view_record(a,level,language);
    if(u16(v+30) || u16(v+78)) return DOK_ROM_FORMAT;
    for(i=0;i<8;++i) {l.settings[i]=u16(v+2*i);offsets[i]=u32(v+32+4*i);}
    l.object_count=u16(v+16);l.actor_count=u16(v+18);l.field_count=u16(v+20);l.text_size=u16(v+22);
    l.cells=u16(v+24);l.player_index=u16(v+26);l.pickup_99_total=s16(v+28);
#define R(offset,count,stride) do {status=range(a,offset,count,stride);if(status) return status;} while(0)
    R(offsets[0],256,2);R(offsets[1],l.object_count,32);R(offsets[2],l.field_count,8);R(offsets[3],l.text_size,1);
    R(offsets[4],l.cells,3);R(offsets[5],l.object_count,36);R(offsets[6],l.actor_count,64);R(offsets[7],1,60);
    R(u32(v+72),u16(v+76),8);
#undef R
    for(i=0;i<256;++i) l.glyph_index[i]=u16(a->data+offsets[0]+2*i);
    l.definitions=(const dok_runtime_definition *)(const void *)(a->data+offsets[1]);
    l.fields=(const dok_runtime_field *)(const void *)(a->data+offsets[2]);l.text=a->data+offsets[3];l.maps=a->data+offsets[4];
    l.initial_objects=(const dok_runtime_object *)(const void *)(a->data+offsets[5]);
    l.initial_actors=(const dok_runtime_actor *)(const void *)(a->data+offsets[6]);
    l.initial_player=(const dok_runtime_player *)(const void *)(a->data+offsets[7]);
    l.palette.offset=u16(v+64);l.palette.size=u16(v+66);l.light.offset=u16(v+68);l.light.size=u16(v+70);
    if(dok_runtime_validate_level(&l)!=DOK_RUNTIME_OK) return DOK_ROM_FORMAT;
    for(i=0;i<l.field_count;++i) if(l.fields[i].reserved) return DOK_ROM_FORMAT;
    for(i=0;i<l.object_count;++i) {
        const dok_runtime_object *o=&l.initial_objects[i];
        if(o->visual_resource || (o->known&~7u)) return DOK_ROM_FORMAT;
        if(l.definitions[i].class_id==9 && o->state.monster.course_counter_known>1) return DOK_ROM_FORMAT;
    }
    for(i=0;i<l.actor_count;++i) if((l.initial_actors[i].known&~15u) || !zero(l.initial_actors[i].reserved,2)) return DOK_ROM_FORMAT;
    if(l.initial_player->reserved) return DOK_ROM_FORMAT;
    for(i=0;i<9;++i) if(l.initial_player->weapons[i]>l.object_count) return DOK_ROM_REFERENCE;
    /* Bindings are strictly sorted, unique, and agree with descriptor names. */
    for(i=0;i<u16(v+76);++i) {
        const uint8_t *b=a->data+u32(v+72)+8*i,*name;dok_runtime_span span;unsigned kind;uint16_t id=u16(b+4);
        dok_rom_resource resource;
        if(b[7] || !binding_span(&l,u16(b),u16(b+2),&span,&kind) || !span.size || b[6]!=kind ||
           !id || id>a->resource_count) return DOK_ROM_REFERENCE;
        if(i) {
            const uint8_t *previous=b-8;
            if(u16(previous)>u16(b) || (u16(previous)==u16(b) && u16(previous+2)>=u16(b+2))) return DOK_ROM_REFERENCE;
        }
        name=a->data+a->resource_offset+(uint32_t)(id-1)*32;
        if(compare(name,name_size(name),l.text+span.offset,span.size)) return DOK_ROM_REFERENCE;
        status=dok_rom_resource_at(&resource,a,id,language);if(status) return DOK_ROM_REFERENCE;
    }
    status=check_binding(a,level,language,&l,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_PALETTE,l.palette);if(status) return status;
    status=check_binding(a,level,language,&l,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_LIGHT,l.light);if(status) return status;
    for(i=0;i<l.object_count;++i) {
        const dok_runtime_definition *d=&l.definitions[i];
#define B(slot,span) do {status=check_binding(a,level,language,&l,(uint16_t)i,slot,span);if(status) return status;} while(0)
        B(DOK_ROM_SLOT_SPRITE,d->sprite);B(DOK_ROM_SLOT_ANIMATION,d->animation);B(DOK_ROM_SLOT_SOUND,d->sound);
        for(j=0;j<d->field_count;++j) {
            const dok_runtime_field *f=&l.fields[d->field_begin+j];
            if(f->kind>=DOK_LEVEL_IMAGE && f->kind<=DOK_LEVEL_SOUND) {B(f->native_offset,f->value.bytes);}
        }
#undef B
    }
    *out=l;return DOK_ROM_OK;
}
static int common_profile(const dok_runtime_level *a,const dok_runtime_level *b) {
    size_t i,j;
    if(a->object_count!=b->object_count || a->actor_count!=b->actor_count || a->field_count!=b->field_count ||
       a->cells!=b->cells || a->player_index!=b->player_index || a->pickup_99_total!=b->pickup_99_total ||
       a->maps!=b->maps || a->initial_objects!=b->initial_objects || a->initial_actors!=b->initial_actors ||
       a->initial_player!=b->initial_player || !equal((const uint8_t *)a->settings,(const uint8_t *)b->settings,sizeof(a->settings)) ||
       !equal((const uint8_t *)a->glyph_index,(const uint8_t *)b->glyph_index,sizeof(a->glyph_index))) return 0;
    for(i=0;i<a->object_count;++i) {
        const dok_runtime_definition *x=&a->definitions[i],*y=&b->definitions[i];
        if(!equal((const uint8_t *)x,(const uint8_t *)y,20)) return 0;
        for(j=0;j<x->field_count;++j) {
            const dok_runtime_field *xf=&a->fields[x->field_begin+j],*yf=&b->fields[y->field_begin+j];
            if(xf->native_offset!=yf->native_offset || xf->kind!=yf->kind ||
               (xf->kind<=DOK_LEVEL_CHAR && xf->value.number!=yf->value.number)) return 0;
        }
    }
    return 1;
}
dok_rom_status dok_rom_open(dok_rom_assets *out,const void *data,size_t size) {
    const uint8_t *p=data;dok_rom_assets a;dok_rom_status status;size_t i;unsigned level;const uint16_t endian=1;
    static const uint8_t magic[8]={'D','O','K','G','B','A','1',0};
    if(!out || !data) return DOK_ROM_ARGUMENT;
    if((uintptr_t)data&3u) return DOK_ROM_ALIGNMENT;
    if(size<64 || size>UINT32_MAX) return DOK_ROM_RANGE;
    if(!equal(p,magic,8)) return DOK_ROM_FORMAT;
    if(u16(p+8)!=1 || u16(p+10)!=64 || u16(p+32)!=80 || u16(p+34)!=32) return DOK_ROM_VERSION_ERROR;
    if(*(const uint8_t *)(const void *)&endian!=1 || u32(p+36)!=1 || u32(p+40)!=1 || !zero(p+44,20)) return DOK_ROM_FORMAT;
    if(u32(p+12)!=size) return DOK_ROM_RANGE;
    a.data=p;a.size=size;a.level_count=u16(p+16);a.language_count=u16(p+18);a.resource_count=u32(p+20);
    a.resource_offset=u32(p+24);a.view_offset=u32(p+28);
    if(a.level_count!=3 || a.language_count!=2 || !a.resource_count || a.resource_count>65535) return DOK_ROM_FORMAT;
    if(a.resource_offset!=64 || a.view_offset!=64+a.resource_count*32 ||
       a.view_offset>size || (size-a.view_offset)/(80*2)<a.level_count) return DOK_ROM_RANGE;
    for(i=0;i<a.resource_count;++i) {
        const uint8_t *r=p+a.resource_offset+i*32;unsigned n=name_size(r),language;
        if(!n || n>=16 || !zero(r+n,16-n)) return DOK_ROM_FORMAT;
        if(i) {const uint8_t *prior=r-32;if(compare(prior,name_size(prior),r,n)>=0) return DOK_ROM_FORMAT;}
        for(language=0;language<2;++language) {
            uint32_t off=u32(r+16+language*8),length=u32(r+20+language*8);
            if(!off && !length) continue;
            status=range(&a,off,length,1);if(status) return status;
        }
    }
    for(level=0;level<3;++level) {
        dok_runtime_level ru,en;
        status=dok_rom_level(&ru,&a,level,0);if(status) return status;
        status=dok_rom_level(&en,&a,level,1);if(status) return status;
        if(!common_profile(&ru,&en)) return DOK_ROM_PROFILE;
    }
    *out=a;return DOK_ROM_OK;
}
dok_rom_status dok_rom_switch_language(dok_runtime *runtime,dok_runtime_level *view,const dok_rom_assets *a,unsigned language) {
    unsigned level;dok_rom_status status;dok_runtime_level selected;
    if(!runtime || !view || !a || runtime->level!=view) return DOK_ROM_ARGUMENT;
    if(language>=a->language_count) return DOK_ROM_LANGUAGE_ERROR;
    for(level=0;level<a->level_count;++level) {
        const uint8_t *v=view_record(a,level,0);
        if(view->maps==a->data+u32(v+48) && view->initial_objects==(const dok_runtime_object *)(const void *)(a->data+u32(v+52))) {
            status=dok_rom_level(&selected,a,level,language);if(status) return status;
            if(!common_profile(view,&selected)) return DOK_ROM_PROFILE;
            *view=selected;return DOK_ROM_OK;
        }
    }
    return DOK_ROM_NOT_FOUND;
}
