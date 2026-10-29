#include "dok/render_scene.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint32_t checks,grid_points,positive_merges,negative_cases;
static uint32_t scene_provenance_records;
static dok_level_definition level;
static dok_initial_world world;
static dok_geometry_binding bindings[256];
static dok_render_scene scene;
#define CHECK(condition) do {checks++;if(!(condition)){fprintf(stderr,"render_scene:%d\n",__LINE__);return 0;}}while(0)

static int32_t component(const dok_nds_render_vertex *v,uint32_t axis)
{
    switch(axis) {case 0:return v->x;case 1:return v->y;case 2:return v->z;
      case 3:return v->u_q16;default:return v->v_q16;}
}

static dok_nds_render_vertex point(uint32_t plane,int32_t s,int32_t t)
{
    dok_nds_render_vertex v;
    v.x=plane==0u?-200+s:plane==1u?-200+s:-200;
    v.y=plane==0u?-100+t:plane==1u?-100:-100+s;
    v.z=plane==0u?200:plane==1u?200+t:200+t;
    v.u_q16=-5*65536+4096*s+1024*t;
    v.v_q16= 3*65536-2048*s+8192*t;
    return v;
}

static dok_nds_render_quad rectangle(uint32_t plane,int32_t start,int32_t width,int32_t height,uint32_t id)
{
    dok_nds_render_quad q;memset(&q,0,sizeof(q));
    q.texture_slot=3u;q.color_rgb555=0x7fffu;q.source_surface_id=id;
    q.vertex[0]=point(plane,start,0);q.vertex[1]=point(plane,start+width,0);
    q.vertex[2]=point(plane,start+width,height);q.vertex[3]=point(plane,start,height);
    return q;
}

/* Independent bilinear interpolation from all four corners. An affine quad
 * has exact integer values at this test's world-grid samples. No floats. */
static int64_t interpolate(const dok_nds_render_quad *q,uint32_t axis,int32_t s,int32_t t,int32_t w,int32_t h)
{
    int64_t a=component(&q->vertex[0],axis),b=component(&q->vertex[1],axis);
    int64_t c=component(&q->vertex[2],axis),d=component(&q->vertex[3],axis);
    return a*(w-s)*(h-t)+b*s*(h-t)+c*s*t+d*(w-s)*t;
}

static int interiors(uint32_t plane,const dok_nds_render_quad *merged,
    const dok_nds_render_quad *pieces,const int32_t *widths,uint32_t count,int32_t height)
{
    int32_t width=0,s,t;uint32_t i,axis;
    for(i=0u;i<count;i++)width+=widths[i];
    for(s=1;s<width;s+=7)for(t=1;t<height;t+=11) {
        int32_t start=0;uint32_t containing=0u;
        dok_nds_render_vertex expected=point(plane,s,t);
        for(i=0u;i<count;i++) {
            if(s>=start && s<start+widths[i]) {
                ++containing;
                for(axis=0u;axis<5u;axis++) {
                    int64_t a=interpolate(merged,axis,s,t,width,height);
                    int64_t b=interpolate(&pieces[i],axis,s-start,t,widths[i],height);
                    int64_t da=(int64_t)width*height,db=(int64_t)widths[i]*height;
                    CHECK(a%da==0 && b%db==0);
                    CHECK(a/da==b/db && a/da==component(&expected,axis));
                }
            }
            start+=widths[i];
        }
        CHECK(containing==1u); /* Exact half-open union; no gaps/overlap. */
        ++grid_points;
    }
    return 1;
}

static int positive(void)
{
    uint32_t plane;const int32_t widths[3]={60,120,180};
    for(plane=0u;plane<3u;plane++) {
        dok_nds_render_quad pieces[3],merged,expected,two;
        pieces[0]=rectangle(plane,0,60,80,101u);
        pieces[1]=rectangle(plane,60,120,80,202u);
        pieces[2]=rectangle(plane,180,180,80,303u);
        CHECK(dok_render_merge(&pieces[0],&pieces[1],&merged)==1);positive_merges++;
        expected=rectangle(plane,0,180,80,101u);
        CHECK(memcmp(&merged,&expected,sizeof(merged))==0);
        CHECK(interiors(plane,&merged,pieces,widths,2u,80));
        two=merged;
        CHECK(dok_render_merge(&merged,&pieces[2],&merged)==1);positive_merges++;
        expected=rectangle(plane,0,360,80,101u);
        CHECK(memcmp(&merged,&expected,sizeof(merged))==0);
        CHECK(merged.source_surface_id==pieces[0].source_surface_id);
        CHECK(interiors(plane,&merged,pieces,widths,3u,80));
        CHECK(dok_render_merge(&pieces[0],&pieces[1],&pieces[0])==1);positive_merges++;
        CHECK(memcmp(&pieces[0],&two,sizeof(two))==0); /* out aliases a. */
    }
    return 1;
}

static int reject(const dok_nds_render_quad *a,const dok_nds_render_quad *b)
{
    dok_nds_render_quad out,sentinel;
    memset(&out,0xa5,sizeof(out));sentinel=out;
    CHECK(dok_render_merge(a,b,&out)==0);
    CHECK(memcmp(&out,&sentinel,sizeof(out))==0);negative_cases++;
    return 1;
}

static int negatives(void)
{
    dok_nds_render_quad a=rectangle(0u,0,60,80,1u),b=rectangle(0u,60,120,80,2u),bad;
    uint32_t i;
    CHECK(reject(NULL,&b));CHECK(reject(&a,NULL));
    CHECK(dok_render_merge(&a,&b,NULL)==0);
    bad=b;bad.texture_slot++;CHECK(reject(&a,&bad));
    bad=b;bad.flags=1u;CHECK(reject(&a,&bad));
    bad=b;bad.reserved=1u;CHECK(reject(&a,&bad));
    bad=b;bad.color_rgb555^=1u;CHECK(reject(&a,&bad));
    bad=b;for(i=0u;i<4u;i++)bad.vertex[i].u_q16-=60*4096;CHECK(reject(&a,&bad)); /* UV reset */
    bad=b;bad.vertex[1].u_q16++;bad.vertex[2].u_q16++;CHECK(reject(&a,&bad)); /* slope seam */
    bad=b;bad.vertex[2].x+=1;CHECK(reject(&a,&bad)); /* non-parallelogram */
    bad=b;bad.vertex[1].y+=5;bad.vertex[2].y+=5;CHECK(reject(&a,&bad)); /* non-collinear widths */
    bad=b;bad.vertex[2].y+=1;CHECK(reject(&a,&bad)); /* unequal heights */
    bad=rectangle(0u,60,-30,80,2u);CHECK(reject(&a,&bad)); /* overlapping/backward */
    bad=rectangle(0u,61,120,80,2u);CHECK(reject(&a,&bad)); /* gap */
    bad=rectangle(0u,60,120,0,2u);CHECK(reject(&a,&bad));
    {
        dok_nds_render_quad zero=rectangle(0u,0,0,80,1u),next=rectangle(0u,0,120,80,2u);
        CHECK(reject(&zero,&next));
        zero=rectangle(0u,0,60,0,1u);next=rectangle(0u,60,120,0,2u);CHECK(reject(&zero,&next));
        zero=rectangle(0u,0,60,80,1u);next=rectangle(0u,60,120,80,2u);
        for(i=0u;i<4u;i++){zero.vertex[i].y=0;next.vertex[i].y=0;}CHECK(reject(&zero,&next)); /* zero area */
    }
    /* Preserve shared edges and valid affine shape; rejection must be range. */
    for(i=0u;i<5u;i++) {
        static const int32_t low[5]={-4097,-4097,-4097,-134217729,-134217729};
        static const int32_t high[5]={4096,4096,4096,134213633,134213633};
        uint32_t edge;
        for(edge=0u;edge<2u;edge++) {
            uint32_t plane=i==0u?2u:i==1u?1u:0u;
            dok_nds_render_quad aa=rectangle(plane,0,60,80,1u),bb=rectangle(plane,60,120,80,2u);uint32_t v;
            for(v=0u;v<4u;v++) {
                int32_t value=edge?high[i]:low[i];
                switch(i) {
                case 0:aa.vertex[v].x=value;bb.vertex[v].x=value;break;
                case 1:aa.vertex[v].y=value;bb.vertex[v].y=value;break;
                case 2:aa.vertex[v].z=value;bb.vertex[v].z=value;break;
                case 3:aa.vertex[v].u_q16=value;bb.vertex[v].u_q16=value;break;
                default:aa.vertex[v].v_q16=value;bb.vertex[v].v_q16=value;break;
                }
            }
            CHECK(reject(&aa,&bb));
        }
    }
    {
        dok_nds_render_quad aa=a,bb=b,out;uint32_t v;
        for(v=0u;v<4u;v++) {
            aa.vertex[v].z=bb.vertex[v].z=-4096;
            aa.vertex[v].u_q16=bb.vertex[v].u_q16=-134217728;
            aa.vertex[v].v_q16=bb.vertex[v].v_q16=134213632;
        }
        CHECK(dok_render_merge(&aa,&bb,&out)==1);positive_merges++;
        for(v=0u;v<4u;v++)aa.vertex[v].z=bb.vertex[v].z=4095;
        CHECK(dok_render_merge(&aa,&bb,&out)==1);positive_merges++;
        aa=a;bb=b;bb.flags=1u;out=aa;
        CHECK(dok_render_merge(&aa,&bb,&aa)==0);
        CHECK(memcmp(&aa,&out,sizeof(aa))==0);negative_cases++; /* Aliased failure. */
    }
    return 1;
}

static int scene_provenance(void)
{
    const dok_nds_render_frame *frame;
    dok_nds_render_camera camera;
    uint32_t i,j,seen[3][2]={{0u}},quads=0u;
    memset(&level,0,sizeof(level));memset(&world,0,sizeof(world));memset(bindings,0,sizeof(bindings));
    level.language=1u;level.object_count=1u;level.glyph_index[1]=1u;
    level.settings[0]=3;level.settings[1]=1;level.settings[2]=3;level.settings[3]=480;level.settings[4]=960;
    level.objects[0].glyph=1u;level.objects[0].class_id=0u;level.objects[0].field_count=1u;
    level.objects[0].fields[0].native_offset=0x54u;level.objects[0].fields[0].kind=DOK_LEVEL_WORD;
    level.objects[0].fields[0].value=10;
    memset(world.maps,1,sizeof(world.maps));world.definition=&level;world.epoch=7u;
    for(i=1u;i<4u;i++) {bindings[1].images[i].material=(uint16_t)i;bindings[1].images[i].width=1u;bindings[1].images[i].height=1u;}
    camera.x=720;camera.y=-300;camera.z=-240;
    CHECK(dok_render_trig(0u,&camera.sin_q15,&camera.cos_q15));dok_render_scene_init(&scene);
    CHECK(dok_render_scene_prepare(&scene,&world,bindings,&camera,0u,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(scene.counts.source_quads==18u && scene.counts.frustum_rejected==6u);
    CHECK(scene.counts.merged_quads==8u && scene.counts.published_quads==4u);
    CHECK(scene.counts.source_quads-scene.counts.frustum_rejected==scene.counts.published_quads+scene.counts.merged_quads);
    for(i=0u;i<scene.counts.merged_quads;i++) {
        const dok_render_merge_record *r=&scene.merges[i];
        uint32_t retained=r->retained_surface_id-1u,consumed=r->consumed_surface_id-1u;
        uint32_t row=(retained/12u)/64u,kind=retained%12u;
        CHECK(row<3u && kind<2u && (retained/12u)%64u==0u);
        CHECK(consumed%12u==kind && (consumed/12u)/64u==row);
        CHECK((consumed/12u)%64u==1u || (consumed/12u)%64u==2u);
        seen[row][kind]|=1u<<((consumed/12u)%64u);scene_provenance_records++;
    }
    CHECK(seen[0][0]==6u && seen[1][0]==6u && seen[2][0]==6u && seen[2][1]==6u);
    CHECK(seen[0][1]==0u && seen[1][1]==0u);
    for(i=0u;i<frame->cell_count;i++)for(j=0u;j<frame->cells[i].quad_count;j++) {
        const dok_nds_render_quad *q=&frame->cells[i].quads[j];uint32_t kind=(q->source_surface_id-1u)%12u;
        CHECK(kind<2u && (q->source_surface_id-1u)/12u%64u==0u);
        CHECK(q->vertex[0].x+frame->cells[i].origin_x==0);
        CHECK(q->vertex[1].x+frame->cells[i].origin_x==1440);
        CHECK(q->vertex[2].x+frame->cells[i].origin_x==1440);
        CHECK(q->vertex[3].x+frame->cells[i].origin_x==0);
        CHECK(q->vertex[0].u_q16==0 && q->vertex[1].u_q16==0 && q->vertex[2].u_q16==0 && q->vertex[3].u_q16==0);
        quads++;
    }
    CHECK(quads==4u);
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    return 1;
}

int main(void)
{
    if(!positive() || !negatives() || !scene_provenance())return 1;
    printf("{\"passed\":true,\"checks\":%" PRIu32 ",\"grid_points\":%" PRIu32 ",\"positive_merges\":%" PRIu32 ",\"negative_cases\":%" PRIu32 ",\"positive_scene_provenance_records\":%" PRIu32 "}\n",
      checks,grid_points,positive_merges,negative_cases,scene_provenance_records);
    return 0;
}
