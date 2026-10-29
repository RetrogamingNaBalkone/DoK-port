#include "dok/sprites.h"
#include <limits.h>
#include <string.h>

static uint16_t le16(const unsigned char *p)
{ return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8); }
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)le16(p)|(uint32_t)le16(p+2)<<16; }
static int32_t word(int64_t n)
{ uint32_t u=(uint32_t)(uint64_t)n&65535u;return u<32768u?(int32_t)u:(int32_t)u-65536; }
static int32_t wrap(int64_t n)
{ uint32_t u=(uint32_t)(uint64_t)n;return u<=INT32_MAX?(int32_t)u:-1-(int32_t)(UINT32_MAX-u); }
static int32_t sar(int32_t n,uint32_t bits)
{ int64_t d=INT64_C(1)<<bits;return n>=0?(int32_t)(n/d):-(int32_t)((-(int64_t)n+d-1)/d); }
static int32_t lattice(int32_t n)
{ return sar(n,9)*512; }

dok_status dok_sprite_animation_open(const dok_asset_view *view,dok_sprite_animation *out)
{
    dok_sprite_animation a;
    if(!view || !out || !view->data || view->entry.representation!=DOK_ASSET_ANIM_META ||
       view->entry.bytes<8u) return DOK_STATUS_INVALID_ARGUMENT;
    a=(dok_sprite_animation){view->data,view->entry.bytes,le16(view->data)};
    if(!a.count || a.count>DOK_SPRITE_FRAME_LIMIT || a.bytes!=8u+a.count*16u || le32(a.metadata+4))
        return DOK_STATUS_INVALID_STATE;
    *out=a;return DOK_STATUS_OK;
}
dok_status dok_sprite_animation_get(const dok_sprite_animation *a,int32_t index,dok_sprite_frame *out)
{
    const unsigned char *p;
    dok_sprite_frame f;
    if(!a || !out || !a->metadata || !a->count || a->count>DOK_SPRITE_FRAME_LIMIT ||
       a->bytes!=8u+a->count*16u) return DOK_STATUS_INVALID_ARGUMENT;
    if(index<0) index=0;
    if((uint32_t)index>=a->count) index=(int32_t)a->count-1;
    p=a->metadata+8u+(uint32_t)index*16u;
    f=(dok_sprite_frame){le32(p),(int16_t)word(le16(p+4)),(int16_t)word(le16(p+6)),
        (int16_t)word(le16(p+8)),le16(p+12),le16(p+14)};
    if(!f.image || !f.width || !f.height) return DOK_STATUS_INVALID_STATE;
    *out=f;return DOK_STATUS_OK;
}
dok_status dok_sprite_player_step(dok_sprite_player *p,uint32_t *interval,uint32_t *cancel)
{
    dok_sprite_player next;
    uint32_t index;
    if(!p || !interval || !cancel || p->revision==UINT32_MAX || p->cursor < -1 ||
       (p->cursor>=0 && (uint32_t)p->cursor>=p->animation.count))
        return DOK_STATUS_INVALID_ARGUMENT;
    if(p->remaining<=0) { *cancel=1;*interval=0;return DOK_STATUS_OK; }
    next=*p;
    index=(uint32_t)((int32_t)p->cursor+1);
    if(index>=p->animation.count) index=0;
    if(dok_sprite_animation_get(&p->animation,(int32_t)index,&next.displayed)!=DOK_STATUS_OK)
        return DOK_STATUS_INVALID_STATE;
    --next.remaining;next.cursor=(int16_t)index;++next.revision;
    *p=next;*cancel=0;*interval=(uint32_t)(int32_t)next.displayed.interval;
    return DOK_STATUS_OK;
}
dok_status dok_sprite_project(const dok_nds_render_camera *c,const dok_sprite_pose *p,
    dok_sprite_projection *out)
{
    dok_sprite_projection r={0};int32_t dx,dz,dy,factor,half;
    if(!c || !p || !out || c->sin_q15 < -32768 || c->sin_q15>32768 ||
       c->cos_q15 < -32768 || c->cos_q15>32768) return DOK_STATUS_INVALID_ARGUMENT;
    dx=wrap((int64_t)p->x-c->x);dz=wrap((int64_t)p->z-c->z);
    r.depth=sar(wrap((int64_t)dz*c->cos_q15-(int64_t)dx*c->sin_q15),15);
    if(r.depth<=0) { *out=r;return DOK_STATUS_OK; }
    r.lateral=sar(wrap((int64_t)dx*c->cos_q15+(int64_t)dz*c->sin_q15),15);
    factor=6553600/(r.depth+200);half=sar(p->width,1);
    r.left=wrap((int64_t)sar(wrap(((int64_t)r.lateral-half)*factor),15)+159);
    r.right=wrap((int64_t)sar(wrap(((int64_t)r.lateral+half)*factor),15)+159);
    dy=wrap((int64_t)p->y-c->y);
    r.top=wrap((int64_t)sar(wrap(((int64_t)dy-p->height)*factor),15)+79);
    r.bottom=wrap((int64_t)sar(wrap((int64_t)dy*factor),15)+79);
    r.sort_key=r.depth-sar(p->width,2);if(r.sort_key<0) r.sort_key=0;
    r.visible=(r.left<r.right?r.left:r.right)<=319 &&
        (r.left>r.right?r.left:r.right)>=0 && r.bottom>0 && r.top<159;
    if(!r.visible) r.sort_key=0;
    *out=r;return DOK_STATUS_OK;
}
static dok_status gather_one(const dok_sprite_snapshot *s,const dok_nds_render_camera *c,
    uint32_t plane,uint32_t cell,uint32_t x,uint32_t z,dok_sprite_list *list,
    const uint16_t *runtime_flags,const unsigned char *zero_height)
{
    const dok_initial_world *w=s->world;
    const dok_level_definition *d=w->definition;
    uint32_t glyph=w->maps[plane][cell],index=d->glyph_index[glyph],fc;
    const dok_level_object *o;const dok_sprite_binding *b;
    dok_sprite_pose p={0};dok_sprite_projection projection;
    if(!index) return DOK_STATUS_INVALID_STATE;
    o=&d->objects[index-1u];
    if(runtime_flags&&(runtime_flags[glyph]&0x400u))return DOK_STATUS_OK;
    /* PKR1098:42A9..42AE skips the entire remaining cell, including MMap,
     * for a solid TMap wall. An initial seed behind that wall is not drawn. */
    if(plane==DOK_WORLD_MMAP) {
        uint32_t tile=d->glyph_index[w->maps[DOK_WORLD_TMAP][cell]];
        if(!tile)return DOK_STATUS_INVALID_STATE;
        if(d->objects[tile-1u].class_id==2u)return DOK_STATUS_OK;
    }
    if(plane==DOK_WORLD_TMAP ? !(o->class_id==3u || o->class_id==6u || o->class_id==9u || o->class_id==10u)
                           : !(o->class_id==3u || o->class_id==9u)) return DOK_STATUS_OK;
    ++list->considered;b=&s->bindings[glyph];
    if(!b->frame.image || !b->frame.width || !b->frame.height) return DOK_STATUS_INVALID_STATE;
    fc=d->glyph_index[w->maps[DOK_WORLD_FCMAP][cell]];
    if(!fc) return DOK_STATUS_INVALID_STATE;
    p.x=(int32_t)x*d->settings[DOK_LEVEL_CELL_SIDE]+d->settings[DOK_LEVEL_CELL_SIDE]/2;
    p.z=(int32_t)z*d->settings[DOK_LEVEL_CELL_SIDE]+d->settings[DOK_LEVEL_CELL_SIDE]/2;
    if(plane==DOK_WORLD_MMAP && o->class_id==9u) {
        uint32_t seed=w->glyph_index[glyph];
        if(!seed || !w->objects[seed-1u].pose_known) return DOK_STATUS_INVALID_STATE;
        p.x=w->objects[seed-1u].x;p.z=w->objects[seed-1u].z;
    }
    p.y=word(-(int32_t)d->objects[fc-1u].height_parameter-o->height_parameter);
    if(zero_height&&(zero_height[glyph/8u]&(1u<<(glyph%8u))))p.y=word(-(int32_t)d->objects[fc-1u].height_parameter);
    p.width=(int16_t)word((int64_t)b->frame.width*o->width_parameter);
    p.height=(int16_t)word((int64_t)b->frame.height*o->width_parameter);
    if(p.width<=0 || p.height<=0) return DOK_STATUS_UNSUPPORTED;
    p.image=b->frame.image;p.glyph=(uint16_t)glyph;p.flags=o->flags;
    if(runtime_flags)p.flags=runtime_flags[glyph];
    p.texture_width=b->frame.width;p.texture_height=b->frame.height;
    p.source_id=UINT32_C(0x80000000)|(plane<<24)|cell;
    if(dok_sprite_project(c,&p,&projection)!=DOK_STATUS_OK) return DOK_STATUS_INVALID_STATE;
    if(!projection.visible) { ++list->rejected;return DOK_STATUS_OK; }
    if(list->count==DOK_SPRITE_LIMIT) return DOK_STATUS_LIMIT_REACHED;
    list->poses[list->count++]=p;return DOK_STATUS_OK;
}
dok_status dok_sprites_gather_live(const dok_sprite_snapshot *s,const uint16_t *runtime_flags,
    const unsigned char *zero_height,const dok_nds_render_camera *c,uint32_t heading,
    dok_sprite_list *list,const dok_sprite_list **out)
{
    dok_render_gather_bounds bounds;int32_t x,z;uint32_t plane,width;
    if(!s || !s->world || !s->world->definition || !s->bindings || !c || !list || !out ||
       !s->revision || s->world_epoch!=s->world->epoch || !s->world_epoch)
        return DOK_STATUS_INVALID_ARGUMENT;
    if(dok_render_bounds(s->world,c,heading,&bounds)!=DOK_RENDER_SCENE_OK)
        return DOK_STATUS_INVALID_ARGUMENT;
    list->count=list->considered=list->rejected=0;list->world_epoch=s->world_epoch;list->revision=s->revision;
    width=(uint32_t)s->world->definition->settings[DOK_LEVEL_WIDTH];
    for(z=bounds.min_z;z<=bounds.max_z;++z) for(x=bounds.min_x;x<=bounds.max_x;++x)
        for(plane=0;plane<2u;++plane) {
            dok_status r=gather_one(s,c,plane,(uint32_t)z*width+(uint32_t)x,(uint32_t)x,(uint32_t)z,list,runtime_flags,zero_height);
            if(r!=DOK_STATUS_OK) return r;
        }
    *out=list;return DOK_STATUS_OK;
}
dok_status dok_sprites_gather(const dok_sprite_snapshot *s,const dok_nds_render_camera *c,
    uint32_t heading,dok_sprite_list *list,const dok_sprite_list **out)
{return dok_sprites_gather_live(s,NULL,NULL,c,heading,list,out);}
static dok_render_scene_result billboard(const dok_nds_render_camera *c,const dok_sprite_pose *p,
    uint16_t slot,dok_nds_render_quad *q,dok_nds_render_cell *cell)
{
    int32_t half=p->width/2;uint32_t k;
    int32_t ox=lattice(p->x),oy=lattice(p->y),oz=lattice(p->z);
    memset(q,0,sizeof(*q));q->texture_slot=slot;q->color_rgb555=32767;
    q->source_surface_id=p->source_id;
    *cell=(dok_nds_render_cell){ox,oy,oz,q,1};
    for(k=0;k<4u;++k) {
        int32_t side=(k==0u || k==3u)?-half:half;
        int64_t vx=(int64_t)p->x+sar(wrap((int64_t)side*c->cos_q15),15)-ox;
        int64_t vz=(int64_t)p->z+sar(wrap((int64_t)side*c->sin_q15),15)-oz;
        int64_t vy=(int64_t)p->y-(k<2u?p->height:0)-oy;
        if(vx < -4096 || vx>4095 || vy < -4096 || vy>4095 || vz < -4096 || vz>4095)
            return DOK_RENDER_SCENE_LIMIT;
        q->vertex[k]=(dok_nds_render_vertex){(int32_t)vx,(int32_t)vy,(int32_t)vz,
            (k==1u || k==2u)?((int32_t)p->texture_width-1)*65536:0,
            k>=2u?((int32_t)p->texture_height-1)*65536:0};
    }
    return DOK_RENDER_SCENE_OK;
}
dok_render_scene_result dok_sprites_append(dok_render_scene *scene,const dok_sprite_list *list,
    const uint16_t *slots,uint32_t revision)
{
    uint32_t i,base;
    dok_nds_render_quad q;dok_nds_render_cell cell;
    if(!scene || !list || !slots || !scene->leased || list->world_epoch!=scene->world_epoch ||
       list->revision!=revision || !revision || list->count>DOK_SPRITE_LIMIT)
        return DOK_RENDER_SCENE_ARGUMENT;
    base=scene->counts.published_quads;
    if(base>DOK_RENDER_QUADS || scene->frame.cell_count>DOK_RENDER_CELLS ||
       list->count>DOK_RENDER_QUADS-base || list->count>DOK_RENDER_CELLS-scene->frame.cell_count)
        return DOK_RENDER_SCENE_LIMIT;
    for(i=0;i<list->count;++i) {
        dok_render_scene_result r=billboard(&scene->frame.camera,&list->poses[i],slots[i],&q,&cell);
        if(r!=DOK_RENDER_SCENE_OK) return r;
    }
    for(i=0;i<list->count;++i) {
        dok_nds_render_quad *dest=&scene->quads[base+i];
        (void)billboard(&scene->frame.camera,&list->poses[i],slots[i],dest,&cell);
        scene->cells[scene->frame.cell_count++]=cell;
    }
    scene->counts.published_quads+=list->count;return DOK_RENDER_SCENE_OK;
}
