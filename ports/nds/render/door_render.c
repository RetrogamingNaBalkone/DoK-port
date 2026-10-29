#include "dok/door_render.h"
#include <string.h>

static int64_t nearest(int64_t numerator,int32_t denominator)
{
    return numerator>=0 ? (numerator+denominator/2)/denominator
        : -((-numerator+denominator/2)/denominator);
}
static dok_geometry_vertex intersection(const dok_geometry_vertex *a,
    const dok_geometry_vertex *b,int32_t numerator,int32_t denominator)
{
    dok_geometry_vertex v;
    v.x=(int32_t)((int64_t)a->x+nearest(((int64_t)b->x-a->x)*numerator,denominator));
    v.y=(int32_t)((int64_t)a->y+nearest(((int64_t)b->y-a->y)*numerator,denominator));
    v.z=(int32_t)((int64_t)a->z+nearest(((int64_t)b->z-a->z)*numerator,denominator));
    v.u_q16=(int32_t)((int64_t)a->u_q16+nearest(((int64_t)b->u_q16-a->u_q16)*numerator,denominator));
    v.v_q16=(int32_t)((int64_t)a->v_q16+nearest(((int64_t)b->v_q16-a->v_q16)*numerator,denominator));
    return v;
}

dok_door_geometry_result dok_door_geometry_build(const dok_geometry_quad *source,
    const dok_geometry_image *image,int16_t phase,int16_t extent,
    dok_geometry_quad *out)
{
    dok_geometry_quad result;
    int32_t width,height,shift,maxu,maxv;
    uint32_t mode,i;
    if(!source || !image || !out || source->kind!=DOK_GEOMETRY_DOOR ||
       !image->width || !image->height || image->width>32767u || image->height>32767u ||
       extent<1 || phase<0) return DOK_DOOR_GEOMETRY_ARGUMENT;
    if(extent>1 && phase>=extent-1) return DOK_DOOR_GEOMETRY_HIDDEN;
    if(!phase) { *out=*source;return DOK_DOOR_GEOMETRY_KEEP; }
    width=image->width;height=image->height;maxu=(width-1)*65536;maxv=(height-1)*65536;
    mode=image->flags&UINT32_C(0xFFC0);
    if(mode!=0x40u && mode!=0x100u) return DOK_DOOR_GEOMETRY_UNSUPPORTED;
    if(source->vertices[0].u_q16!=source->vertices[3].u_q16 ||
       source->vertices[1].u_q16!=source->vertices[2].u_q16 ||
       !((source->vertices[0].u_q16==0 && source->vertices[1].u_q16==maxu) ||
         (source->vertices[0].u_q16==maxu && source->vertices[1].u_q16==0)) ||
       source->vertices[0].v_q16!=0 || source->vertices[1].v_q16!=0 ||
       source->vertices[2].v_q16!=maxv || source->vertices[3].v_q16!=maxv)
        return DOK_DOOR_GEOMETRY_UNSUPPORTED;
    shift=(mode==0x40u ? width:height)/extent*phase;
    result=*source;
    if(shift!=0) {
        if(mode==0x40u) {
            if(width<2 || shift>=width) return DOK_DOOR_GEOMETRY_UNSUPPORTED;
            if(source->vertices[0].u_q16==0) {
                result.vertices[0]=intersection(&source->vertices[0],&source->vertices[1],shift,width-1);
                result.vertices[3]=intersection(&source->vertices[3],&source->vertices[2],shift,width-1);
            } else {
                result.vertices[1]=intersection(&source->vertices[1],&source->vertices[0],shift,width-1);
                result.vertices[2]=intersection(&source->vertices[2],&source->vertices[3],shift,width-1);
            }
            for(i=0;i<4u;++i) result.vertices[i].u_q16-=shift*65536;
        } else {
            if(height<2 || shift>=height) return DOK_DOOR_GEOMETRY_UNSUPPORTED;
            result.vertices[0]=intersection(&source->vertices[0],&source->vertices[3],shift,height-1);
            result.vertices[1]=intersection(&source->vertices[1],&source->vertices[2],shift,height-1);
            for(i=0;i<4u;++i) result.vertices[i].v_q16-=shift*65536;
        }
    }
    *out=result;return DOK_DOOR_GEOMETRY_KEEP;
}

typedef struct source_cache {
    uint32_t cell_id,valid;
    dok_geometry_cell cell,candidate;
} source_cache;
static const dok_geometry_quad *source_quad(source_cache *cache,uint32_t id,
    const dok_initial_world *world,const dok_geometry_binding bindings[256])
{
    uint32_t cell,ordinal,x,z;
    if(id==0u) return NULL;
    --id;cell=id/DOK_GEOMETRY_CELL_MAX;ordinal=id%DOK_GEOMETRY_CELL_MAX;
    x=cell%64u;z=cell/64u;
    if(!cache->valid || cache->cell_id!=cell) {
        if(dok_geometry_build_cell(world,bindings,x,z,&cache->candidate,&cache->cell)!=DOK_GEOMETRY_OK)
            return NULL;
        cache->cell_id=cell;cache->valid=1;
    }
    return ordinal<cache->cell.count ? &cache->cell.quads[ordinal]:NULL;
}

/* Only TMap classes4/7 can emit DOK_GEOMETRY_DOOR (geometry.c). Most
 * published surfaces are floor/wall/ceiling: their cell need not be built
 * again merely to discover that they have no movable door. source IDs are
 * produced by the base builder immediately above, with a fixed stride64. */
static uint32_t source_door_class(uint32_t id,const dok_initial_world *world)
{
    uint32_t cell=(id-1u)/DOK_GEOMETRY_CELL_MAX,x=cell%64u,z=cell/64u;
    uint32_t width=(uint32_t)world->definition->settings[DOK_LEVEL_WIDTH];
    uint32_t glyph=world->maps[DOK_WORLD_TMAP][z*width+x];
    uint32_t index=world->definition->glyph_index[glyph];
    return world->definition->objects[index-1u].class_id;
}
static int changed(const dok_geometry_quad *q,const dok_initial_world *world,
    const dok_geometry_binding bindings[256],const dok_interaction_phase_view *phases)
{
    uint32_t index=world->definition->glyph_index[q->source_glyph];
    dok_geometry_quad cropped;
    dok_door_geometry_result result;
    if(q->kind!=DOK_GEOMETRY_DOOR || index==0u ||
       world->definition->objects[index-1u].class_id!=7u) return 0;
    result=dok_door_geometry_build(q,&bindings[q->source_glyph].images[DOK_GEOMETRY_IMAGE],
        phases->phase[q->source_glyph],phases->extent[q->source_glyph],&cropped);
    return result!=DOK_DOOR_GEOMETRY_KEEP || memcmp(q,&cropped,sizeof(cropped))!=0;
}

dok_render_scene_result dok_door_scene_prepare(dok_render_scene *scene,
    const dok_initial_world *world,const dok_geometry_binding bindings[256],
    const dok_nds_render_camera *camera,uint32_t heading,
    const dok_interaction_phase_view *phases,dok_door_render_counts *counts,
    const dok_nds_render_frame **out)
{
    const dok_nds_render_frame *base=NULL;
    dok_door_render_counts result={0};
    source_cache cache={0};
    uint32_t i,j,quads=0,cells=0;
    dok_render_scene_result status;
    if(!out || !counts || !world || !world->definition ||
       (phases && (phases->world_epoch!=world->epoch || !phases->revision)))
        return DOK_RENDER_SCENE_ARGUMENT;
    status=dok_render_scene_prepare(scene,world,bindings,camera,heading,&base);
    if(status!=DOK_RENDER_SCENE_OK) return status;
    result.world_epoch=world->epoch;result.revision=phases ? phases->revision:0;
    if(!phases) { *counts=result;*out=base;return DOK_RENDER_SCENE_OK; }
    for(i=0;i<scene->counts.merged_quads;++i) {
        const dok_render_merge_record *m=&scene->merges[i];
        const dok_geometry_quad *q;
        if(source_door_class(m->retained_surface_id,world)==7u) {
            q=source_quad(&cache,m->retained_surface_id,world,bindings);
            if(!q || changed(q,world,bindings,phases)) goto failed;
        }
        if(source_door_class(m->consumed_surface_id,world)==7u) {
            q=source_quad(&cache,m->consumed_surface_id,world,bindings);
            if(!q || changed(q,world,bindings,phases)) goto failed;
        }
    }
    for(i=0;i<base->cell_count;++i) {
        dok_nds_render_cell old=scene->cells[i];
        uint32_t start=quads;
        for(j=0;j<old.quad_count;++j) {
            dok_nds_render_quad dest=old.quads[j];
            uint32_t source_class=source_door_class(dest.source_surface_id,world);
            if(source_class==4u || source_class==7u) {
                const dok_geometry_quad *q=source_quad(&cache,dest.source_surface_id,world,bindings);
                if(!q) goto failed;
                if(q->kind!=DOK_GEOMETRY_DOOR) {
                    scene->quads[quads++]=dest;continue;
                }
                uint32_t index=world->definition->glyph_index[q->source_glyph];
                ++result.doors;
                if(index!=0u && world->definition->objects[index-1u].class_id==7u) {
                    dok_geometry_quad cropped;
                    uint32_t k;
                    dok_door_geometry_result r=dok_door_geometry_build(q,
                        &bindings[q->source_glyph].images[DOK_GEOMETRY_IMAGE],
                        phases->phase[q->source_glyph],phases->extent[q->source_glyph],&cropped);
                    if(r==DOK_DOOR_GEOMETRY_HIDDEN) { ++result.hidden;continue; }
                    if(r!=DOK_DOOR_GEOMETRY_KEEP) goto failed;
                    if(memcmp(q,&cropped,sizeof(cropped))) {
                        ++result.cropped;
                        for(k=0;k<4u;++k) {
                            dest.vertex[k].x=cropped.vertices[k].x-old.origin_x;
                            dest.vertex[k].y=cropped.vertices[k].y-old.origin_y;
                            dest.vertex[k].z=cropped.vertices[k].z-old.origin_z;
                            dest.vertex[k].u_q16=cropped.vertices[k].u_q16;
                            dest.vertex[k].v_q16=cropped.vertices[k].v_q16;
                        }
                    }
                }
            }
            scene->quads[quads++]=dest;
        }
        if(quads!=start) {
            old.quads=scene->quads+start;old.quad_count=quads-start;
            scene->cells[cells++]=old;
        }
    }
    scene->counts.published_quads=quads;scene->frame.cell_count=cells;
    *counts=result;*out=&scene->frame;return DOK_RENDER_SCENE_OK;
failed:
    (void)dok_render_scene_release(scene,scene->generation);
    return DOK_RENDER_SCENE_GEOMETRY;
}
