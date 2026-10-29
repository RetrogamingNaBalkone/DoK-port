#include "dok/light_cache.h"
#include <limits.h>
#include <string.h>
void dok_light_cache_reset(dok_light_cache *c){if(c)memset(c,0,sizeof(*c));}
static int pack(const dok_nds_render_quad *q,const dok_nds_render_cell *cell,dok_light_cache_quad *p)
{
    uint32_t i;int32_t origin[3]={cell->origin_x,cell->origin_y,cell->origin_z};
    if(q->flags || q->color_rgb555>32767u || q->reserved>15u || (q->texture_slot>=128u && q->texture_slot!=UINT16_MAX))return 0;
    memset(p,0,sizeof(*p));
    p->attributes=(q->texture_slot==UINT16_MAX?128u:q->texture_slot)|((uint32_t)q->reserved<<8)|((uint32_t)q->color_rgb555<<12);
    p->source_id=q->source_surface_id;
    for(i=0;i<3u;++i){if(origin[i]<INT16_MIN || origin[i]>INT16_MAX)return 0;p->origin[i]=(int16_t)origin[i];}
    for(i=0;i<4u;++i) {
        const dok_nds_render_vertex *v=&q->vertex[i];
        if(v->x<INT16_MIN || v->x>INT16_MAX || v->y<INT16_MIN || v->y>INT16_MAX || v->z<INT16_MIN || v->z>INT16_MAX)return 0;
        p->xyz[i][0]=(int16_t)v->x;p->xyz[i][1]=(int16_t)v->y;p->xyz[i][2]=(int16_t)v->z;
        p->uv[i][0]=v->u_q16;p->uv[i][1]=v->v_q16;
    }
    return 1;
}
static void unpack(const dok_light_cache_quad *p,dok_nds_render_quad *q)
{
    uint32_t i,slot=p->attributes&255u;memset(q,0,sizeof(*q));
    q->texture_slot=(uint16_t)(slot==128u?UINT16_MAX:slot);q->reserved=(uint16_t)((p->attributes>>8)&15u);
    q->color_rgb555=(uint16_t)((p->attributes>>12)&32767u);q->source_surface_id=p->source_id;
    for(i=0;i<4u;++i)q->vertex[i]=(dok_nds_render_vertex){p->xyz[i][0],p->xyz[i][1],p->xyz[i][2],p->uv[i][0],p->uv[i][1]};
}
static int flatten(const dok_render_scene *s,dok_light_cache_quad *dest,uint32_t capacity,int compare)
{
    uint32_t i,j,index=0;dok_light_cache_quad value;
    if(s->counts.published_quads>capacity || s->frame.cell_count>DOK_RENDER_CELLS)return 0;
    for(i=0;i<s->frame.cell_count;++i) {
        const dok_nds_render_cell *cell=&s->cells[i];
        if(cell->quad_count>capacity-index || cell->quads!=s->quads+index)return 0;
        for(j=0;j<cell->quad_count;++j,++index) {
            if(!pack(&cell->quads[j],cell,&value))return 0;
            if(compare){if(memcmp(&value,&dest[index],sizeof(value)))return 0;}
            else dest[index]=value;
        }
    }
    return index==s->counts.published_quads;
}
static void restore(const dok_light_cache *c,dok_render_scene *s)
{
    uint32_t i,count=0;
    for(i=0;i<c->output_count;++i) {
        const dok_light_cache_quad *p=&c->output[i];unpack(p,&s->quads[i]);
        if(count && s->cells[count-1u].origin_x==p->origin[0] && s->cells[count-1u].origin_y==p->origin[1] && s->cells[count-1u].origin_z==p->origin[2])
            ++s->cells[count-1u].quad_count;
        else s->cells[count++]=(dok_nds_render_cell){p->origin[0],p->origin[1],p->origin[2],&s->quads[i],1};
    }
    s->counts.published_quads=c->output_count;s->frame.cell_count=count;
}
dok_render_scene_result dok_light_cache_apply(dok_light_cache *c,dok_render_scene *s,int16_t near_depth,
    dok_light_workspace *workspace,dok_light_counts *out)
{
    int supported;dok_light_counts result;dok_render_scene_result status;
    if(!c || !s || !workspace || !out || !s->leased || c->queries==UINT32_MAX)return DOK_RENDER_SCENE_ARGUMENT;
    ++c->queries;c->last_hit=0;
    if(c->valid && c->epoch==s->world_epoch && c->near_depth==near_depth && c->input_count==s->counts.published_quads &&
       !memcmp(&c->camera,&s->frame.camera,sizeof(c->camera)) && flatten(s,c->input,DOK_LIGHT_CACHE_INPUTS,1)) {
        restore(c,s);*out=c->counts;++c->hits;c->last_hit=1;return DOK_RENDER_SCENE_OK;
    }
    ++c->misses;c->valid=0;supported=flatten(s,c->input,DOK_LIGHT_CACHE_INPUTS,0);
    if(supported){c->input_count=s->counts.published_quads;c->camera=s->frame.camera;c->epoch=s->world_epoch;c->near_depth=near_depth;}
    status=dok_light_scene_apply(s,near_depth,workspace,&result);if(status!=DOK_RENDER_SCENE_OK)return status;
    if(supported && flatten(s,c->output,DOK_RENDER_QUADS,0)) {
        c->output_count=s->counts.published_quads;c->counts=result;c->valid=1;++c->stores;
    } else ++c->unsupported;
    *out=result;return DOK_RENDER_SCENE_OK;
}
