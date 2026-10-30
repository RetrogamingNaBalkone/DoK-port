#include "dok/render_materials.h"
#include "../../platform/assets/file_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
extern dok_status baseline_dok_render_world_load(const dok_asset_session *,uint32_t,dok_world_epoch,dok_render_world *,dok_render_workspace *,unsigned char *,uint32_t);
static dok_render_world a,b,na,nb;
static dok_render_workspace scratch;
static dok_asset_workspace io;
static unsigned char pa[DOK_NDS_ASSETS_BYTES],pb[DOK_NDS_ASSETS_BYTES];
static uint64_t compared;static uint32_t checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void same(const void *x,const void *y,size_t n){CHECK(!memcmp(x,y,n));compared+=n;}
static void span(const dok_level_definition *d,dok_level_span s)
{CHECK(s.offset<=d->source_bytes&&s.length<=d->source_bytes-s.offset);}
static void model(const dok_level_definition *d)
{
    span(d,d->palette);span(d,d->light);
    for(uint32_t i=0;i<d->object_count;++i){const dok_level_object *o=&d->objects[i];
        span(d,o->sprite);span(d,o->animation);span(d,o->sound);span(d,o->raw_line);span(d,o->raw_tail);
        for(uint32_t j=0;j<o->field_count;++j)span(d,o->fields[j].token);}
}
static void normalize(dok_render_world *w)
{
    w->base.source=NULL;w->effective.source=NULL;w->initial.definition=NULL;
    w->presentation.effective=NULL;w->presentation.localized=NULL;w->presentation.session=NULL;
    for(uint32_t i=0;i<w->material_count;++i)w->materials[i].pixels=NULL;
    w->asset_bytes=0;
}
static void compare(void)
{
    CHECK(a.base.source_bytes==b.base.source_bytes&&a.effective.source_bytes==b.effective.source_bytes);
    same(a.base.source,b.base.source,a.base.source_bytes);same(a.effective.source,b.effective.source,a.effective.source_bytes);
    model(&a.base);model(&a.effective);model(&b.base);model(&b.effective);
    CHECK(a.initial.definition==&a.effective&&b.initial.definition==&b.effective);
    CHECK(a.presentation.effective==&a.effective&&b.presentation.effective==&b.effective);
    CHECK(a.presentation.localized==&a.base&&b.presentation.localized==&b.base);
    CHECK(a.material_count==b.material_count);
    for(uint32_t i=0;i<a.material_count;++i){
        const dok_render_material *x=&a.materials[i],*y=&b.materials[i];
        same(&x->entry,&y->entry,sizeof(x->entry));same(x->pixels,y->pixels,x->entry.bytes);
        CHECK(y->pixels>=pb+((b.effective.source_bytes+31u)&~31u));
        CHECK((uintptr_t)y->pixels+x->entry.bytes<=(uintptr_t)pb+b.asset_bytes);
    }
    na=a;nb=b;normalize(&na);normalize(&nb);same(&na,&nb,sizeof(na));
}
static void localize(dok_render_world *w,unsigned char *p,const dok_asset_session *session,uint32_t level)
{
    uint32_t off=(w->asset_bytes+31u)&~31u;dok_asset_view v;
    dok_asset_id id=UINT32_C(2)<<30|(17u+level)<<18|(uint32_t)DOK_ASSET_LEVEL_CP866<<10;
    CHECK(dok_assets_load(session,id,p+off,DOK_RENDER_SOURCE_BYTES,&v)==DOK_ASSET_OK);
    CHECK(dok_level_parse(&v,&scratch.world.level,&w->base)==DOK_WORLD_OK);
    CHECK(dok_presentation_bind(&w->effective,&w->base,session,&scratch.presentation,&w->presentation)==DOK_BALANCE_OK);
    w->asset_bytes=off+v.entry.bytes;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    if(argc!=2||nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK||dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 1;
    printf("{\"passed\":true,\"levels\":[");
    for(uint32_t level=1;level<=3u;++level){
        memset(pa,0xa5,sizeof(pa));memset(pb,0x5a,sizeof(pb));
        CHECK(baseline_dok_render_world_load(&session,level,level,&a,&scratch,pa,sizeof(pa))==DOK_STATUS_OK);
        CHECK(dok_render_world_load(&session,level,level,&b,&scratch,pb,sizeof(pb))==DOK_STATUS_OK);
        compare();uint32_t freed=a.asset_bytes-b.asset_bytes;
        CHECK(freed==65536u-((b.effective.source_bytes+31u)&~31u));
        uint32_t ru=b.asset_bytes;localize(&a,pa,&session,level);localize(&b,pb,&session,level);compare();
        CHECK(a.asset_bytes-b.asset_bytes==freed);
        printf("%s{\"level\":%" PRIu32 ",\"source_bytes\":%" PRIu32 ",\"freed_bytes\":%" PRIu32 ",\"ru_asset_bytes\":%" PRIu32 ",\"localized_asset_bytes\":%" PRIu32 "}",level>1u?",":"",level,b.effective.source_bytes,freed,ru,b.asset_bytes);
    }
    printf("],\"checks\":%" PRIu32 ",\"compared_bytes\":%" PRIu64 "}\n",checks,compared);nds_asset_file_close(&file);return 0;
}
