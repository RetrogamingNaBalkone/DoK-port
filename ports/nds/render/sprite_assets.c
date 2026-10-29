#include "dok/sprite_assets.h"
#include <string.h>
static dok_asset_id gpu(dok_asset_id id)
{ return (id&~UINT32_C(0x0003fc00))|((uint32_t)DOK_ASSET_GPU_INDEX8_POT<<10); }
const dok_asset_entry *dok_sprite_asset_image(const dok_sprite_asset_bank *b,dok_asset_id id)
{
    uint32_t i;if(!b || b->image_count>DOK_SPRITE_IMAGES) return NULL;
    id=gpu(id);
    for(i=0;i<b->image_count;++i) if(b->images[i].id==id) return &b->images[i];
    return NULL;
}
static dok_status add_image(const dok_asset_session *s,dok_sprite_asset_bank *b,dok_asset_id id)
{
    dok_asset_entry entry;
    if(dok_sprite_asset_image(b,id)) return DOK_STATUS_OK;
    if(b->image_count==DOK_SPRITE_IMAGES) return DOK_STATUS_LIMIT_REACHED;
    if(dok_assets_find(s,gpu(id),&entry)!=DOK_ASSET_OK || entry.representation!=DOK_ASSET_GPU_INDEX8_POT)
        return DOK_STATUS_INVALID_STATE;
    b->images[b->image_count++]=entry;return DOK_STATUS_OK;
}
dok_status dok_sprite_assets_load(const dok_level_presentation *p,dok_sprite_asset_bank *b,
    unsigned char *pool,uint32_t capacity)
{
    uint32_t i,j;
    if(!p || !p->effective || !p->session || !b || !pool) return DOK_STATUS_INVALID_ARGUMENT;
    memset(b,0,sizeof(*b));
    for(i=0;i<p->effective->object_count;++i) {
        const dok_level_object *o=&p->effective->objects[i];dok_presentation_value v;
        dok_asset_entry e;
        if(!(o->class_id==3u || o->class_id==6u || o->class_id==9u || o->class_id==10u)) continue;
        if(dok_presentation_get(p,o->glyph,DOK_PRESENT_SPRITE,&v)!=DOK_BALANCE_OK || !v.asset_id ||
           dok_assets_find(p->session,v.asset_id,&e)!=DOK_ASSET_OK || add_image(p->session,b,v.asset_id)!=DOK_STATUS_OK)
            return DOK_STATUS_INVALID_STATE;
        b->bindings[o->glyph].frame=(dok_sprite_frame){v.asset_id,0,1,1,e.width,e.height};
        /* Original shared cached image header: last Sprite assignment wins. */
        b->bindings[o->glyph].flags=(uint16_t)o->sprite_flags;
        for(j=0;j<p->effective->object_count;++j) {
            const dok_level_object *other=&p->effective->objects[j];
            if(dok_level_span_equal(p->effective,other->sprite,
                p->effective->source+o->sprite.offset,o->sprite.length))
                b->bindings[o->glyph].flags=(uint16_t)other->sprite_flags;
        }
    }
    /* Resource.kind is the N04 source kind (2=ANIMATION), not LEVEL kind5.
     * Bind all sequences for explicit death/projectile/effect state fixtures. */
    for(i=0;i<p->resource_count;++i) if(p->resources[i].kind==2u) {
        dok_asset_id id=p->resources[i].id;dok_asset_view view;dok_sprite_animation a;
        uint32_t offset=(b->metadata_bytes+3u)&~3u;
        if(b->animation_count==DOK_SPRITE_ANIMATIONS || offset>capacity)
            return DOK_STATUS_LIMIT_REACHED;
        if(dok_assets_load(p->session,id,pool+offset,capacity-offset,&view)!=DOK_ASSET_OK ||
           dok_sprite_animation_open(&view,&a)!=DOK_STATUS_OK) return DOK_STATUS_INVALID_STATE;
        for(j=0;j<a.count;++j) {
            dok_sprite_frame f;
            if(dok_sprite_animation_get(&a,(int32_t)j,&f)!=DOK_STATUS_OK ||
               add_image(p->session,b,f.image)!=DOK_STATUS_OK) return DOK_STATUS_INVALID_STATE;
        }
        b->animations[b->animation_count]=id;b->sequences[b->animation_count++]=a;
        b->metadata_bytes=offset+view.entry.bytes;
    }
    for(i=0;i<p->effective->object_count;++i) {
        const dok_level_object *o=&p->effective->objects[i];dok_presentation_value v;
        if(!(o->class_id==3u || o->class_id==6u || o->class_id==9u || o->class_id==10u))continue;
        /* PKR1068:03C1 selects frame0 for base flag0800; monster constructor
         *1068:370F..372F selects it unconditionally, even for base flags0008. */
        if(o->class_id==9u || (o->flags&0x0800u)) {
            if(dok_presentation_get(p,o->glyph,DOK_PRESENT_ANIMATION,&v)!=DOK_BALANCE_OK || !v.asset_id ||
               dok_sprite_asset_select(b,o->glyph,v.asset_id,0)!=DOK_STATUS_OK)
                return DOK_STATUS_INVALID_STATE;
        }
    }
    return DOK_STATUS_OK;
}
dok_status dok_sprite_asset_select(dok_sprite_asset_bank *b,uint8_t glyph,dok_asset_id animation,int32_t frame)
{
    uint32_t i;dok_sprite_frame f;
    if(!b) return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<b->animation_count;++i) if(b->animations[i]==animation) {
        if(dok_sprite_animation_get(&b->sequences[i],frame,&f)!=DOK_STATUS_OK) return DOK_STATUS_INVALID_STATE;
        b->bindings[glyph].frame=f;b->bindings[glyph].flags=8;return DOK_STATUS_OK;
    }
    return DOK_STATUS_INVALID_ARGUMENT;
}
