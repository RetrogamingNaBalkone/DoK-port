#include "door_render_cases.h"
#include <limits.h>
#include <string.h>
#define CHECK(r,c) do { ++(r)->checks;if(!(c)){++(r)->failures; \
    if(!(r)->first_failed_line){(r)->first_failed_line=__LINE__;}return;} } while(0)
static int32_t rounded(int64_t n,int32_t d)
{ return n>=0?(int32_t)((n+d/2)/d):-(int32_t)((-n+d/2)/d); }
static void fixture(dok_door_render_test_workspace *w,uint32_t mirror,uint32_t mode)
{
    uint32_t i;
    memset(w,0,sizeof(*w));w->image=(dok_geometry_image){3,96,128,(uint16_t)(mode|8u)};
    w->source.kind=DOK_GEOMETRY_DOOR;w->source.material=3;w->source.image_flags=w->image.flags;
    w->source.orientation=90;w->source.source_glyph='D';
    w->source.vertices[0]=(dok_geometry_vertex){240,-640,0,0,0};
    w->source.vertices[1]=(dok_geometry_vertex){240,-640,480,95*65536,0};
    w->source.vertices[2]=(dok_geometry_vertex){240,0,480,95*65536,127*65536};
    w->source.vertices[3]=(dok_geometry_vertex){240,0,0,0,127*65536};
    if(mirror)for(i=0;i<4u;++i)w->source.vertices[i].u_q16=95*65536-w->source.vertices[i].u_q16;
    memset(&w->output,0xa5,sizeof(w->output));w->sentinel=w->output;
}
static void phases(dok_door_render_test_workspace *w,dok_door_render_test_report *r)
{
    uint32_t mirror,mode,p,i;
    for(mirror=0;mirror<2u;++mirror)for(mode=0;mode<2u;++mode)for(p=0;p<30u;++p) {
        int32_t shift=(mode?128:96)/30*(int32_t)p;
        fixture(w,mirror,mode?0x100u:0x40u);++r->phase_cases;
        if(p==29u) {
            CHECK(r,dok_door_geometry_build(&w->source,&w->image,(int16_t)p,30,&w->output)==DOK_DOOR_GEOMETRY_HIDDEN);
            CHECK(r,memcmp(&w->output,&w->sentinel,sizeof(w->output))==0);continue;
        }
        CHECK(r,dok_door_geometry_build(&w->source,&w->image,(int16_t)p,30,&w->output)==DOK_DOOR_GEOMETRY_KEEP);
        CHECK(r,w->output.material==w->source.material && w->output.image_flags==w->source.image_flags &&
            w->output.orientation==w->source.orientation && w->output.source_glyph=='D');
        if(!p)CHECK(r,memcmp(&w->output,&w->source,sizeof(w->source))==0);
        for(i=0;i<4u;++i) {
            int32_t u=w->source.vertices[i].u_q16/65536,v=w->source.vertices[i].v_q16/65536;
            int32_t expectedz=w->source.vertices[i].z,expectedy=w->source.vertices[i].y;
            if(!mode) {
                if(u<shift) {u=shift;expectedz=mirror?480-rounded((int64_t)480*shift,95):rounded((int64_t)480*shift,95);}
                u-=shift;
            } else {
                if(v<shift) {v=shift;expectedy=-640+rounded((int64_t)640*shift,127);}
                v-=shift;
            }
            CHECK(r,w->output.vertices[i].x==240 && w->output.vertices[i].z==expectedz && w->output.vertices[i].y==expectedy);
            CHECK(r,w->output.vertices[i].u_q16==u*65536 && w->output.vertices[i].v_q16==v*65536);
            ++r->sample_cases;
        }
        CHECK(r,dok_door_geometry_build(&w->source,&w->image,(int16_t)p,30,&w->source)==DOK_DOOR_GEOMETRY_KEEP);
        CHECK(r,memcmp(&w->source,&w->output,sizeof(w->source))==0);
    }
}
static void guards(dok_door_render_test_workspace *w,dok_door_render_test_report *r)
{
    uint32_t i;
    static const int16_t phase[]={-1,0},extent[]={30,0};
    for(i=0;i<2u;++i) {
        fixture(w,0,0x40);++r->guard_cases;
        CHECK(r,dok_door_geometry_build(&w->source,&w->image,phase[i],extent[i],&w->output)==DOK_DOOR_GEOMETRY_ARGUMENT);
        CHECK(r,memcmp(&w->output,&w->sentinel,sizeof(w->output))==0);
    }
    fixture(w,0,0x80);++r->guard_cases;
    CHECK(r,dok_door_geometry_build(&w->source,&w->image,1,30,&w->output)==DOK_DOOR_GEOMETRY_UNSUPPORTED);
    CHECK(r,memcmp(&w->output,&w->sentinel,sizeof(w->output))==0);
    fixture(w,0,0x40);w->source.vertices[0].u_q16=1;++r->guard_cases;
    CHECK(r,dok_door_geometry_build(&w->source,&w->image,1,30,&w->output)==DOK_DOOR_GEOMETRY_UNSUPPORTED);
    CHECK(r,memcmp(&w->output,&w->sentinel,sizeof(w->output))==0);
    fixture(w,0,0x40);++r->guard_cases;
    CHECK(r,dok_door_geometry_build(&w->source,&w->image,0,1,&w->output)==DOK_DOOR_GEOMETRY_KEEP);
    CHECK(r,memcmp(&w->output,&w->source,sizeof(w->source))==0);
    fixture(w,0,0x40);w->image.width=1;w->source.vertices[1].u_q16=w->source.vertices[2].u_q16=0;
    CHECK(r,dok_door_geometry_build(&w->source,&w->image,1,30,&w->output)==DOK_DOOR_GEOMETRY_KEEP);
    CHECK(r,memcmp(&w->output,&w->source,sizeof(w->source))==0);
    fixture(w,0,0x40);w->source.vertices[0].z=w->source.vertices[3].z=INT32_MIN;
    w->source.vertices[1].z=w->source.vertices[2].z=INT32_MAX;
    CHECK(r,dok_door_geometry_build(&w->source,&w->image,13,30,&w->output)==DOK_DOOR_GEOMETRY_KEEP);
    CHECK(r,w->output.vertices[0].z>INT32_MIN && w->output.vertices[0].z<INT32_MAX);
}
static void resampled_extent(dok_door_render_test_workspace *w,dok_door_render_test_report *r)
{
    uint32_t mode,mirror,i;
    static const int16_t phase[]={7,8,29,INT16_MAX};
    /* Ready-open Use resamples C2 without lowering the existing phase. The
     * private native image getter clamps image frame, not door.phase. */
    for(mode=0;mode<2u;++mode)for(mirror=0;mirror<2u;++mirror)for(i=0;i<4u;++i) {
        fixture(w,mirror,mode?0x100u:0x40u);++r->phase_cases;
        CHECK(r,dok_door_geometry_build(&w->source,&w->image,phase[i],8,&w->output)==DOK_DOOR_GEOMETRY_HIDDEN);
        CHECK(r,memcmp(&w->output,&w->sentinel,sizeof(w->output))==0);
    }
}
int dok_door_render_run_cases(dok_door_render_test_workspace *w,dok_door_render_test_report *r)
{
    if(!w || !r)return 1;
    memset(r,0,sizeof(*r));phases(w,r);if(!r->failures)guards(w,r);
    if(!r->failures)resampled_extent(w,r);
    return r->failures?1:0;
}
