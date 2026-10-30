#include "dok/render_materials.h"
#include <string.h>
#ifndef N23_COMPACT_SOURCE
#define N23_COMPACT_SOURCE 0
#endif

static uint32_t align32(uint32_t value) { return (value+31u)&~31u; }

static uint16_t shared_flags(const dok_level_definition *level,
                            const dok_asset_entry *entry)
{
    uint16_t flags=(uint16_t)entry->param0;
    uint32_t length=0, i;
    while(length<16u && entry->name_cp866[length]) ++length;
    /* PKR1048:07B2 assigns, it does not OR. Cached images are shared. */
    for(i=0;i<level->object_count;++i) {
        const dok_level_object *object=&level->objects[i];
        if(dok_level_span_equal(level,object->sprite,entry->name_cp866,length))
            flags=(uint16_t)object->sprite_flags;
    }
    return flags;
}

static dok_status image_binding(const dok_asset_session *session,
    dok_render_world *world, unsigned char *pool, uint32_t capacity,
    const dok_level_object *object, uint16_t field, uint32_t vertical,
    dok_geometry_image *out)
{
    dok_presentation_value value;
    dok_asset_entry entry;
    dok_asset_view view;
    dok_asset_id gpu_id;
    uint32_t i, transparent, offset;
    uint16_t flags;
    if(dok_presentation_get(&world->presentation,object->glyph,field,&value)!=DOK_BALANCE_OK)
        return DOK_STATUS_INVALID_STATE;
    if(value.asset_id==0u) { memset(out,0,sizeof(*out)); return DOK_STATUS_OK; }
    if(dok_assets_find(session,value.asset_id,&entry)!=DOK_ASSET_OK ||
       entry.representation!=DOK_ASSET_IMAGE_INDEX8) return DOK_STATUS_UNSUPPORTED;
    flags=shared_flags(&world->effective,&entry);
    if(field==DOK_PRESENT_SPRITE && object->class_id==7u)
        flags=(uint16_t)object->sprite_flags; /* Private copied image header. */
    transparent=vertical && (flags&8u) ? 1u:0u;
    gpu_id=(value.asset_id&~UINT32_C(0x0003FC00)) |
           (uint32_t)DOK_ASSET_GPU_INDEX8_POT<<10;
    for(i=0;i<world->material_count;++i)
        if(world->materials[i].entry.id==gpu_id &&
           world->materials[i].color0_transparent==transparent) break;
    if(i==world->material_count) {
        dok_render_material *material;
        if(i==DOK_RENDER_MATERIALS) return DOK_STATUS_LIMIT_REACHED;
        offset=align32(world->asset_bytes);
        if(offset>capacity || dok_assets_load(session,gpu_id,pool+offset,
             capacity-offset,&view)!=DOK_ASSET_OK) return DOK_STATUS_NO_SPACE;
        if(view.entry.bytes>capacity-offset) return DOK_STATUS_NO_SPACE;
        material=&world->materials[i];
        material->entry=view.entry;material->pixels=view.data;
        material->color0_transparent=transparent;
        world->asset_bytes=offset+view.entry.bytes;++world->material_count;
    }
    out->material=(uint16_t)(i+1u);out->width=entry.width;
    out->height=entry.height;out->flags=flags;
    return DOK_STATUS_OK;
}

dok_status dok_render_world_load(const dok_asset_session *session,
    uint32_t level, dok_world_epoch epoch, dok_render_world *world,
    dok_render_workspace *scratch, unsigned char *asset_pool,
    uint32_t pool_capacity)
{
    dok_asset_view view;
    dok_balance_profile profile=dok_balance_accepted_profile();
    unsigned char used_t[256]={0},used_fc[256]={0};
    uint32_t i,cells,index;
    dok_asset_id id;
    if(!session || !world || !scratch || !asset_pool ||
       !epoch || level<1u || level>3u || pool_capacity<DOK_RENDER_SOURCE_BYTES)
        return DOK_STATUS_INVALID_ARGUMENT;
    memset(world,0,sizeof(*world));
    index=17u+level;
    id=UINT32_C(1)<<30 | index<<18 | (uint32_t)DOK_ASSET_LEVEL_CP866<<10;
    if(dok_assets_load(session,id,asset_pool,DOK_RENDER_SOURCE_BYTES,&view)!=DOK_ASSET_OK ||
       dok_level_parse(&view,&scratch->world.level,&world->base)!=DOK_WORLD_OK ||
       dok_balance_apply(&profile,session->_source_sha256[0],&world->base,
         &scratch->world.level,&world->effective,&world->diff)!=DOK_BALANCE_OK ||
       dok_initial_world_build(&world->effective,epoch,&scratch->world.world,&world->initial)!=DOK_WORLD_OK)
        return DOK_STATUS_INVALID_STATE;
    /* The parser/world and presentation scratch lifetimes are disjoint. */
    if(dok_presentation_bind(&world->effective,&world->base,session,
        &scratch->presentation,&world->presentation)!=DOK_BALANCE_OK) return DOK_STATUS_INVALID_STATE;
#if N23_COMPACT_SOURCE
    /* All token spans borrow precisely source_bytes, not the unused capacity
     * of the 64KiB loading destination. Keep that complete lease, aligned for
     * following GPU payloads; loading's original capacity guard is unchanged. */
    world->asset_bytes=align32(world->base.source_bytes);
#else
    world->asset_bytes=DOK_RENDER_SOURCE_BYTES;
#endif
    cells=(uint32_t)world->effective.settings[DOK_LEVEL_WIDTH]*
          (uint32_t)world->effective.settings[DOK_LEVEL_LENGTH];
    for(i=0;i<cells;++i) {
        used_t[world->initial.maps[DOK_WORLD_TMAP][i]]=1;
        used_fc[world->initial.maps[DOK_WORLD_FCMAP][i]]=1;
    }
    for(i=0;i<world->effective.object_count;++i) {
        const dok_level_object *object=&world->effective.objects[i];
        dok_geometry_binding *binding=&world->bindings[object->glyph];
        dok_status status;
        if(used_t[object->glyph] && (object->class_id==2u ||
           object->class_id==4u || object->class_id==7u)) {
            if(object->flags&0x0800u) return DOK_STATUS_UNSUPPORTED;
            status=image_binding(session,world,asset_pool,pool_capacity,object,
                DOK_PRESENT_SPRITE,1,&binding->images[DOK_GEOMETRY_IMAGE]);
            if(status!=DOK_STATUS_OK) return status;
        }
        if(used_fc[object->glyph]) {
            const uint16_t fields[3]={object->class_id==12u ? DOK_PRESENT_SPRITE:0x5Au,0x56u,0x5Eu};
            uint32_t slot;
            if(object->class_id!=0u && object->class_id!=12u && object->class_id!=13u)
                return DOK_STATUS_UNSUPPORTED;
            for(slot=0;slot<3u;++slot) {
                status=image_binding(session,world,asset_pool,pool_capacity,object,
                    fields[slot],slot==2u,&binding->images[slot+1u]);
                if(status!=DOK_STATUS_OK) return status;
            }
        }
    }
    return DOK_STATUS_OK;
}
