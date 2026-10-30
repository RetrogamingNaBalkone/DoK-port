#include "dok/hotcode.h"
#include "dok/render_scene.h"
#include "geometry_private.h"
#include <limits.h>
#include <string.h>
#if N21P_GEOMETRY_PROFILE
/* libnds nds/timers.h declares u32 cpuGetTiming(void). Keep the optional
 * profiler's SDK symbol out of this portable module's include dependency. */
extern uint32_t cpuGetTiming(void);
#endif
#include "render_trig.inc"

#define SCENE_TAG UINT32_C(0x4E303753)

DOK_NDS_HOT_CODE static int32_t wrap32(int64_t value)
{
    uint32_t low=(uint32_t)(uint64_t)value;
    return low<=INT32_MAX ? (int32_t)low:(int32_t)((int64_t)low-INT64_C(4294967296));
}
DOK_NDS_HOT_CODE static int32_t sar15(int32_t value)
{
    int64_t wide=value;
    return wide>=0 ? (int32_t)(wide/32768):-(int32_t)((-wide+32767)/32768);
}
DOK_NDS_HOT_CODE static int32_t word(int32_t value)
{
    uint32_t low=(uint32_t)value&65535u;
    return low<=32767u ? (int32_t)low:(int32_t)low-65536;
}
DOK_NDS_HOT_CODE int dok_render_trig(uint32_t heading,int32_t *sine,int32_t *cosine)
{
    if(heading>360u || !sine || !cosine) return 0;
    *sine=dok_render_sine[heading];*cosine=dok_render_cosine[heading];return 1;
}
DOK_NDS_HOT_CODE void dok_render_scene_init(dok_render_scene *scene)
{
    if(scene) { memset(scene,0,sizeof(*scene));scene->tag=SCENE_TAG; }
}
DOK_NDS_HOT_CODE static int same_vertex(const dok_nds_render_vertex *a,const dok_nds_render_vertex *b)
{
    return a->x==b->x && a->y==b->y && a->z==b->z &&
           a->u_q16==b->u_q16 && a->v_q16==b->v_q16;
}
DOK_NDS_HOT_CODE static int32_t component(const dok_nds_render_vertex *v,uint32_t index)
{
    return index==0u?v->x:index==1u?v->y:index==2u?v->z:index==3u?v->u_q16:v->v_q16;
}
DOK_NDS_HOT_CODE static int vertex_range(const dok_nds_render_vertex *v)
{
    return v->x>=-4096 && v->x<=4095 && v->y>=-4096 && v->y<=4095 &&
           v->z>=-4096 && v->z<=4095 &&
           v->u_q16>=-134217728 && v->u_q16<=134213632 &&
           v->v_q16>=-134217728 && v->v_q16<=134213632;
}
DOK_NDS_HOT_CODE int dok_render_merge(const dok_nds_render_quad *a,const dok_nds_render_quad *b,
    dok_nds_render_quad *out)
{
    uint32_t i,axis=3u;
    int32_t aw[5],bw[5],height[3],cross[3];
    dok_nds_render_quad result;
    if(!a || !b || !out || a->texture_slot!=b->texture_slot || a->flags!=b->flags ||
       a->color_rgb555!=b->color_rgb555 || a->reserved!=b->reserved ||
       !same_vertex(&a->vertex[1],&b->vertex[0]) ||
       !same_vertex(&a->vertex[2],&b->vertex[3])) return 0;
    for(i=0;i<4u;++i) if(!vertex_range(&a->vertex[i]) || !vertex_range(&b->vertex[i])) return 0;
    for(i=0;i<5u;++i) {
        aw[i]=component(&a->vertex[1],i)-component(&a->vertex[0],i);
        bw[i]=component(&b->vertex[1],i)-component(&b->vertex[0],i);
        if(component(&a->vertex[3],i)-component(&a->vertex[0],i)!=
           component(&b->vertex[3],i)-component(&b->vertex[0],i)) return 0;
        if(component(&a->vertex[2],i)-component(&a->vertex[1],i)!=
           component(&a->vertex[3],i)-component(&a->vertex[0],i) ||
           component(&b->vertex[2],i)-component(&b->vertex[1],i)!=
           component(&b->vertex[3],i)-component(&b->vertex[0],i)) return 0;
        if(i<3u && aw[i]!=0 && axis==3u) axis=i;
    }
    if(axis==3u || aw[axis]*bw[axis]<=0) return 0;
    for(i=0;i<5u;++i) if((int64_t)aw[i]*bw[axis]!=(int64_t)bw[i]*aw[axis]) return 0;
    for(i=0;i<3u;++i) height[i]=component(&a->vertex[3],i)-component(&a->vertex[0],i);
    cross[0]=aw[1]*height[2]-aw[2]*height[1];
    cross[1]=aw[2]*height[0]-aw[0]*height[2];
    cross[2]=aw[0]*height[1]-aw[1]*height[0];
    if(cross[0]==0 && cross[1]==0 && cross[2]==0) return 0;
    result=*a;result.vertex[1]=b->vertex[1];result.vertex[2]=b->vertex[2];
    *out=result;return 1;
}

DOK_NDS_HOT_CODE dok_render_scene_result dok_render_bounds(const dok_initial_world *world,
    const dok_nds_render_camera *camera,uint32_t heading,dok_render_gather_bounds *out)
{
    int32_t s,c,r,t,bx,bz,side;
    int32_t x[4],z[4];
    dok_render_gather_bounds result;
    uint32_t i;
    if(!world || !world->definition || !camera || !out ||
       !dok_render_trig(heading,&s,&c) || !dok_render_trig(360u-heading,&t,&r) ||
       s!=camera->sin_q15 || c!=camera->cos_q15) return DOK_RENDER_SCENE_ARGUMENT;
    side=world->definition->settings[DOK_LEVEL_CELL_SIDE];
    if(side<=0) return DOK_RENDER_SCENE_ARGUMENT;
    bx=wrap32((int64_t)camera->x+sar15(wrap32((int64_t)2*side*s)));
    bz=wrap32((int64_t)camera->z-sar15(wrap32((int64_t)2*side*c)));
    x[0]=wrap32((int64_t)bx+sar15(wrap32((int64_t)side*r)));
    z[0]=wrap32((int64_t)bz+sar15(wrap32(-(int64_t)side*t)));
    x[1]=wrap32((int64_t)bx+sar15(wrap32(-(int64_t)side*r)));
    z[1]=wrap32((int64_t)bz+sar15(wrap32((int64_t)side*t)));
    x[2]=wrap32((int64_t)bx+sar15(wrap32((int64_t)6*side*r+(int64_t)9*side*t)));
    z[2]=wrap32((int64_t)bz+sar15(wrap32((int64_t)9*side*r-(int64_t)6*side*t)));
    x[3]=wrap32((int64_t)bx+sar15(wrap32(-(int64_t)6*side*r+(int64_t)9*side*t)));
    z[3]=wrap32((int64_t)bz+sar15(wrap32((int64_t)9*side*r+(int64_t)6*side*t)));
    for(i=0;i<4u;++i) { x[i]=word(x[i]/side);z[i]=word(z[i]/side); }
    result.min_x=result.max_x=x[0];result.min_z=result.max_z=z[0];
    for(i=1;i<4u;++i) {
        if(x[i]<result.min_x) result.min_x=x[i];
        if(x[i]>result.max_x) result.max_x=x[i];
        if(z[i]<result.min_z) result.min_z=z[i];
        if(z[i]>result.max_z) result.max_z=z[i];
    }
    /* Conservative source gather: unlike the original overwritten-slot bug,
     * both min and max retain ALL four source control points. */
    if(result.min_x<0) result.min_x=0;
    if(result.min_z<0) result.min_z=0;
    if(result.max_x>=world->definition->settings[DOK_LEVEL_WIDTH])
        result.max_x=world->definition->settings[DOK_LEVEL_WIDTH]-1;
    if(result.max_z>=world->definition->settings[DOK_LEVEL_LENGTH])
        result.max_z=world->definition->settings[DOK_LEVEL_LENGTH]-1;
    *out=result;return DOK_RENDER_SCENE_OK;
}

typedef struct scene_camera_dot { int64_t lateral,depth; } scene_camera_dot;
DOK_NDS_HOT_CODE static int visible(const dok_geometry_quad *quad,const dok_nds_render_camera *camera,
    const scene_camera_dot *dot)
{
    uint32_t vertex,common_outside=31u;
    /* Two target-pixel guard band for Q12 matrix/raster quantization. It only
     * retains extra candidates; the GPU clips at the actual N06 viewport. */
    for(vertex=0;vertex<4u;++vertex) {
        const dok_geometry_vertex *v=&quad->vertices[vertex];
        int64_t depth=(int64_t)v->z*camera->cos_q15-(int64_t)v->x*camera->sin_q15-dot->depth;
        int64_t eye=depth+INT64_C(200)*32768;
        uint32_t outside=0;
        if(common_outside&1u)outside|=depth+32768<0?1u:0u;
        if(common_outside&6u) {
            int64_t lateral=(int64_t)v->x*camera->cos_q15+(int64_t)v->z*camera->sin_q15-dot->lateral;
            if(common_outside&2u)outside|=400*lateral+324*eye<0?2u:0u;
            if(common_outside&4u)outside|=326*eye-400*lateral<0?4u:0u;
        }
        if(common_outside&24u) {
            int64_t dy=((int64_t)v->y-camera->y)*32768;
            if(common_outside&8u)outside|=164*eye+400*dy<0?8u:0u;
            if(common_outside&16u)outside|=166*eye-400*dy<0?16u:0u;
        }
        common_outside&=outside;
        /* Intersection can only lose bits. Once no plane rejects all seen
         * vertices, later vertices cannot reject the quad either. */
        if(common_outside==0u) return 1;
    }
    return common_outside==0u;
}

DOK_NDS_HOT_CODE dok_render_scene_result dok_render_scene_prepare(dok_render_scene *scene,
    const dok_initial_world *world,const dok_geometry_binding bindings[256],
    const dok_nds_render_camera *camera,uint32_t heading,const dok_nds_render_frame **out)
{
#if GAMEPLAY_GEOMETRY_CACHE
    dok_geometry_cell scratch,cell;
#else
    dok_geometry_cell cell;
    dok_geometry_field54 prepared[DOK_LEVEL_MAX_OBJECTS];
#endif
    scene_camera_dot dot;
    dok_render_gather_bounds bounds;
    int32_t x,z,side;
    uint32_t quad_count=0,cell_count=0;
    uint32_t previous[5];
    int32_t previous_x[5],previous_origin_x[5],previous_origin_z[5];
    dok_render_scene_result status;
#if N21P_GEOMETRY_PROFILE
    uint32_t profile_stamp;
#endif
    if(!scene || scene->tag!=SCENE_TAG || !world || !world->definition ||
       !bindings || !camera || !out || !world->epoch) return DOK_RENDER_SCENE_ARGUMENT;
    if(scene->leased) return DOK_RENDER_SCENE_BUSY;
    if(scene->generation==UINT32_MAX) return DOK_RENDER_SCENE_LIMIT;
    status=dok_render_bounds(world,camera,heading,&bounds);
    if(status!=DOK_RENDER_SCENE_OK) return status;
#if !GAMEPLAY_GEOMETRY_CACHE
    dok_geometry_prepare_fields(world->definition,prepared);
#endif
    dot.lateral=(int64_t)camera->x*camera->cos_q15+(int64_t)camera->z*camera->sin_q15;
    dot.depth=(int64_t)camera->z*camera->cos_q15-(int64_t)camera->x*camera->sin_q15;
    memset(&scene->counts,0,sizeof(scene->counts));scene->counts.bounds=bounds;
#if N21P_GEOMETRY_PROFILE
    memset(scene->profile_ticks,0,sizeof(scene->profile_ticks));
#endif
    side=world->definition->settings[DOK_LEVEL_CELL_SIDE];
    for(z=bounds.min_z;z<=bounds.max_z;++z) for(x=bounds.min_x;x<=bounds.max_x;++x) {
        uint32_t i,start=quad_count;
        int32_t origin_x=((x*side+side/2+256)/512)*512;
        int32_t origin_z=((z*side+side/2+256)/512)*512;
        if(x==bounds.min_x) {
            uint32_t k;
            for(k=0;k<5u;++k) previous[k]=UINT32_MAX;
        }
        ++scene->counts.gathered_cells;
#if N21P_GEOMETRY_PROFILE
        profile_stamp=cpuGetTiming();
#endif
#if GAMEPLAY_GEOMETRY_CACHE
        if(dok_geometry_build_cell(world,bindings,(uint32_t)x,(uint32_t)z,&scratch,&cell)!=DOK_GEOMETRY_OK)
#else
        if(dok_geometry_build_cell_prepared(world,bindings,(uint32_t)x,(uint32_t)z,&cell,prepared)!=DOK_GEOMETRY_OK)
#endif
            return DOK_RENDER_SCENE_GEOMETRY;
#if N21P_GEOMETRY_PROFILE
        scene->profile_ticks[0]+=cpuGetTiming()-profile_stamp;
#endif
        scene->counts.source_quads+=cell.count;
        for(i=0;i<cell.count;++i) {
            const dok_geometry_quad *source=&cell.quads[i];
            dok_nds_render_quad *dest;
            uint32_t j;
#if N21P_GEOMETRY_PROFILE
            profile_stamp=cpuGetTiming();
#endif
            int source_visible=visible(source,camera,&dot);
#if N21P_GEOMETRY_PROFILE
            scene->profile_ticks[1]+=cpuGetTiming()-profile_stamp;
#endif
            if(!source_visible) { ++scene->counts.frustum_rejected;continue; }
            if(!source->material) return DOK_RENDER_SCENE_GEOMETRY;
            if(quad_count==DOK_RENDER_QUADS) return DOK_RENDER_SCENE_LIMIT;
            dest=&scene->quads[quad_count];memset(dest,0,sizeof(*dest));
            dest->texture_slot=(uint16_t)(source->material-1u);
            dest->source_surface_id=((uint32_t)z*64u+(uint32_t)x)*DOK_GEOMETRY_CELL_MAX+i+1u;
            dest->color_rgb555=32767u;
            for(j=0;j<4u;++j) {
                dest->vertex[j].x=source->vertices[j].x-origin_x;
                dest->vertex[j].y=source->vertices[j].y;
                dest->vertex[j].z=source->vertices[j].z-origin_z;
                dest->vertex[j].u_q16=source->vertices[j].u_q16;
                dest->vertex[j].v_q16=source->vertices[j].v_q16;
            }
            if(source->kind<5u && previous[source->kind]!=UINT32_MAX &&
               previous_x[source->kind]+1==x) {
                dok_nds_render_quad shifted=*dest;
                uint32_t kind=source->kind;
                for(j=0;j<4u;++j) {
                    shifted.vertex[j].x+=origin_x-previous_origin_x[kind];
                    shifted.vertex[j].z+=origin_z-previous_origin_z[kind];
                }
                ++scene->counts.merge_candidates;
#if N21P_GEOMETRY_PROFILE
                profile_stamp=cpuGetTiming();
#endif
                int merged=dok_render_merge(&scene->quads[previous[kind]],&shifted,
                     &scene->quads[previous[kind]]);
#if N21P_GEOMETRY_PROFILE
                scene->profile_ticks[2]+=cpuGetTiming()-profile_stamp;
#endif
                if(merged) {
                    dok_render_merge_record *record;
                    if(scene->counts.merged_quads==DOK_RENDER_QUADS) return DOK_RENDER_SCENE_LIMIT;
                    record=&scene->merges[scene->counts.merged_quads++];
                    record->retained_surface_id=scene->quads[previous[kind]].source_surface_id;
                    record->consumed_surface_id=dest->source_surface_id;
                    previous_x[kind]=x;continue;
                }
            }
            if(source->kind<5u) {
                uint32_t kind=source->kind;
                previous[kind]=quad_count;previous_x[kind]=x;
                previous_origin_x[kind]=origin_x;previous_origin_z[kind]=origin_z;
            }
            ++quad_count;
        }
        if(quad_count!=start) {
            dok_nds_render_cell *dest;
            if(cell_count==DOK_RENDER_CELLS) return DOK_RENDER_SCENE_LIMIT;
            dest=&scene->cells[cell_count++];dest->origin_x=origin_x;
            dest->origin_y=0;dest->origin_z=origin_z;
            dest->quads=scene->quads+start;dest->quad_count=quad_count-start;
        }
    }
    scene->counts.published_quads=quad_count;
    scene->frame.camera=*camera;scene->frame.cells=scene->cells;
    scene->frame.cell_count=cell_count;scene->frame.generation=++scene->generation;
    scene->leased=1;scene->world_epoch=world->epoch;*out=&scene->frame;
    return DOK_RENDER_SCENE_OK;
}
DOK_NDS_HOT_CODE dok_render_scene_result dok_render_scene_release(dok_render_scene *scene,uint32_t generation)
{
    if(!scene || scene->tag!=SCENE_TAG || !scene->leased || generation!=scene->generation)
        return DOK_RENDER_SCENE_ARGUMENT;
    scene->leased=0;return DOK_RENDER_SCENE_OK;
}
