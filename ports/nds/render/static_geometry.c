#include "static_geometry.h"
#include "dok/hotcode.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
typedef struct PackedQuad {
    int16_t vertex[4][5];
    uint16_t image_flags,object_flags;
    uint8_t material,orientation,kind,glyph;
} PackedQuad;
_Static_assert(sizeof(PackedQuad)==48u,"fixed game geometry record");
typedef struct LevelGeometry {
    uint16_t patterns,quads;
    uint16_t offset[257];
    uint8_t cell[4096];
    PackedQuad quad[512];
} LevelGeometry;
static const LevelGeometry *geometry;
static const dok_initial_world *bound_world;
static const dok_geometry_binding *bound_bindings;
void dok_static_geometry_reset(void)
{geometry=NULL;bound_world=NULL;bound_bindings=NULL;}
static int pack(const dok_geometry_quad *q,uint32_t x,uint32_t z,PackedQuad *out)
{
    if(!q->material||q->material>255u||q->orientation>270u||q->orientation%90u||q->kind>4u)return 0;
    for(uint32_t v=0;v<4u;++v){
        const dok_geometry_vertex *p=&q->vertices[v];
        int64_t values[5]={(int64_t)p->x-x*480u,p->y,(int64_t)p->z-z*480u,
            p->u_q16/65536,p->v_q16/65536};
        if(p->u_q16%65536||p->v_q16%65536)return 0;
        for(uint32_t k=0;k<5u;++k){
            if(values[k]<INT16_MIN||values[k]>INT16_MAX)return 0;
            out->vertex[v][k]=(int16_t)values[k];
        }
    }
    out->image_flags=q->image_flags;out->object_flags=q->object_flags;
    out->material=(uint8_t)q->material;out->orientation=(uint8_t)(q->orientation/90u);
    out->kind=q->kind;out->glyph=q->source_glyph;return 1;
}
int dok_static_geometry_load(const dok_initial_world *world,const dok_geometry_binding bindings[256],
    void *backing,uint32_t capacity,uint32_t *used)
{
    dok_geometry_cell candidate,cell;PackedQuad packed[DOK_GEOMETRY_CELL_MAX];
    LevelGeometry *g=backing;uint32_t pattern;
    dok_static_geometry_reset();
    if(!world||!world->definition||!bindings||!backing||!used||
       ((uintptr_t)backing&1u)||capacity<offsetof(LevelGeometry,quad)||
       world->definition->settings[0]!=64||world->definition->settings[2]!=64||
       world->definition->settings[3]!=480)return 0;
    g->patterns=g->quads=0;g->offset[0]=0;
    for(uint32_t z=0;z<64u;++z)for(uint32_t x=0;x<64u;++x){
        if(dok_geometry_build_cell(world,bindings,x,z,&candidate,&cell)!=DOK_GEOMETRY_OK)return 0;
        for(uint32_t q=0;q<cell.count;++q)if(!pack(&cell.quads[q],x,z,&packed[q]))return 0;
        for(pattern=0;pattern<g->patterns;++pattern){
            uint32_t start=g->offset[pattern],count=g->offset[pattern+1u]-start;
            if(count==cell.count&&!memcmp(g->quad+start,packed,count*sizeof(PackedQuad)))break;
        }
        if(pattern==g->patterns){
            uint32_t next=g->quads+cell.count;
            if(pattern==256u||next>512u||offsetof(LevelGeometry,quad)+next*sizeof(PackedQuad)>capacity)return 0;
            memcpy(g->quad+g->quads,packed,cell.count*sizeof(PackedQuad));
            g->quads=(uint16_t)next;g->offset[++g->patterns]=(uint16_t)next;
        }
        g->cell[z*64u+x]=(uint8_t)pattern;
    }
    *used=(uint32_t)offsetof(LevelGeometry,quad)+g->quads*(uint32_t)sizeof(PackedQuad);
    bound_world=world;bound_bindings=bindings;geometry=g;return 1;
}
DOK_NDS_HOT_CODE int dok_static_geometry_read(const dok_initial_world *world,
    const dok_geometry_binding bindings[256],uint32_t x,uint32_t z,dok_geometry_cell *out)
{
    const LevelGeometry *g=geometry;
    if(!g||world!=bound_world||bindings!=bound_bindings||!out||x>=64u||z>=64u)return 0;
    uint32_t pattern=g->cell[z*64u+x],start=g->offset[pattern],count=g->offset[pattern+1u]-start;
    int32_t xx=(int32_t)x*480,zz=(int32_t)z*480;
    out->count=(uint16_t)count;out->reserved=0;
    for(uint32_t i=0;i<count;++i){
        const PackedQuad *p=&g->quad[start+i];dok_geometry_quad *q=&out->quads[i];
        q->material=p->material;q->image_flags=p->image_flags;q->object_flags=p->object_flags;
        q->orientation=(uint16_t)(p->orientation*90u);q->kind=p->kind;
        q->cell_x=(uint8_t)x;q->cell_z=(uint8_t)z;q->source_glyph=p->glyph;
        for(uint32_t v=0;v<4u;++v){
            q->vertices[v].x=xx+p->vertex[v][0];q->vertices[v].y=p->vertex[v][1];
            q->vertices[v].z=zz+p->vertex[v][2];
            q->vertices[v].u_q16=(int32_t)p->vertex[v][3]*65536;
            q->vertices[v].v_q16=(int32_t)p->vertex[v][4]*65536;
        }
    }
    return 1;
}
DOK_NDS_HOT_CODE int dok_static_geometry_shapes(const dok_initial_world *world,
    const dok_geometry_binding bindings[256],uint32_t x,uint32_t z,
    dok_static_map_shape shapes[DOK_GEOMETRY_CELL_MAX],uint32_t *count)
{
    const LevelGeometry *g=geometry;
    if(!g||world!=bound_world||bindings!=bound_bindings||!shapes||!count||x>=64u||z>=64u)return 0;
    uint32_t pattern=g->cell[z*64u+x],start=g->offset[pattern],end=g->offset[pattern+1u],n=0;
    int32_t xx=(int32_t)x*480,zz=(int32_t)z*480;
    for(uint32_t i=start;i<end;++i){
        const PackedQuad *p=&g->quad[i];
        /* The native constructor emits the floor record only; paired ceiling
         * geometry has never had an independent disclosure record. */
        if(p->kind==DOK_GEOMETRY_CEILING_QUAD)continue;
        dok_static_map_shape *s=&shapes[n++];
        s->ax=xx+p->vertex[0][0];s->bx=xx+p->vertex[1][0];
        s->az=zz+p->vertex[p->kind==DOK_GEOMETRY_FLOOR_QUAD?2u:0u][2];
        s->bz=zz+p->vertex[p->kind==DOK_GEOMETRY_FLOOR_QUAD?0u:1u][2];
        s->flags=p->object_flags;s->glyph=p->glyph;s->kind=p->kind;
    }
    *count=n;return 1;
}
