#include "dok/balance.h"
#include "dok/sha256.h"
#include "file_reader.h"
#include <stdio.h>
#include <string.h>

static uint32_t checks,bindings,texts,resources,numeric_rejections,alias_rejections,switches;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"N19 presentation line %d: %s\n",__LINE__,#x);return 0;}} while(0)
static dok_asset_workspace asset_workspace;
static dok_world_workspace workspace;
static dok_level_definition base,effective,english,mutant,effective_before;
static dok_initial_world world,world_before;
static dok_level_presentation presentation,candidate,preserved;
static dok_presentation_resource expected_resources[DOK_PRESENTATION_MAX_RESOURCES];
static uint32_t expected_count;
static unsigned char ru_bytes[32768],en_bytes[32768],mutant_bytes[32768];

static void hash(const void *p,uint32_t n,unsigned char out[32])
{dok_sha256 h;dok_sha256_begin(&h);dok_sha256_update(&h,p,n);dok_sha256_end(&h,out);}
static dok_asset_id id(uint32_t language,uint32_t index,uint32_t rep)
{return language<<30|index<<18|rep<<10;}
static int load(const dok_asset_session *s,uint32_t language,uint32_t index,unsigned char *bytes,dok_level_definition *out)
{
    dok_asset_view view;
    CHECK(dok_assets_load(s,id(language,index,DOK_ASSET_LEVEL_CP866),bytes,32768,&view)==DOK_ASSET_OK);
    CHECK(dok_level_parse(&view,&workspace.level,out)==DOK_WORLD_OK);return 1;
}
static int expected_catalog(const dok_asset_session *s,uint32_t language)
{
    static const uint16_t primary[]={0,2,3,11,7,12,14,16,8};
    dok_asset_entry raw,actual;uint32_t i;
    expected_count=0;memset(expected_resources,0,sizeof(expected_resources));
    for(i=0;i<dok_assets_count(s);++i){
        CHECK(dok_assets_entry_at(s,i,&raw)==DOK_ASSET_OK);
        if(raw.language!=language||raw.representation!=DOK_ASSET_RAW_SOURCE)continue;
        CHECK(expected_count<DOK_PRESENTATION_MAX_RESOURCES&&raw.source_kind>=1&&raw.source_kind<=8);
        dok_presentation_resource *r=&expected_resources[expected_count++];
        r->id=id(language,raw.source_index,primary[raw.source_kind]);r->kind=raw.source_kind;
        memcpy(r->name_cp866,raw.name_cp866,16);
        CHECK(dok_assets_find(s,r->id,&actual)==DOK_ASSET_OK);
        CHECK(actual.language==language&&actual.source_kind==r->kind&&memcmp(actual.name_cp866,r->name_cp866,16)==0);
    }
    CHECK(expected_count==DOK_PRESENTATION_MAX_RESOURCES);return 1;
}
static int compare_value(const dok_level_definition *local,uint8_t glyph,uint16_t slot,uint8_t kind,dok_level_span span)
{
    static const unsigned char resource_kinds[]={0,0,0,1,2,8,0,4,5};
    dok_presentation_value value;dok_asset_id expected_id=0;uint32_t i;
    CHECK(span.offset<=local->source_bytes&&span.length<=local->source_bytes-span.offset);
    CHECK(dok_presentation_get(&presentation,glyph,slot,&value)==DOK_BALANCE_OK);
    CHECK(value.kind==kind);CHECK(value.reserved[0]==0&&value.reserved[1]==0&&value.reserved[2]==0);
    if(kind==DOK_PRESENT_KIND_TEXT){
        uint32_t bytes=span.length>39?39:span.length;
        CHECK(value.asset_id==0&&value.text_bytes==bytes);
        CHECK(value.text==local->source+span.offset);
        CHECK(memcmp(value.text,local->source+span.offset,bytes)==0);++texts;
    }else{
        if(span.length){
            CHECK(span.length<=15&&kind<=8);
            for(i=0;i<expected_count;++i){const dok_presentation_resource *r=&expected_resources[i];
                if(r->kind==resource_kinds[kind]&&r->name_cp866[span.length]==0&&
                   memcmp(r->name_cp866,local->source+span.offset,span.length)==0){CHECK(expected_id==0);expected_id=r->id;}
            }
            CHECK(expected_id!=0);
        }
        CHECK(value.asset_id==expected_id&&value.text==NULL&&value.text_bytes==0);++resources;
    }
    ++bindings;return 1;
}
static int compare_all(const dok_level_definition *local)
{
    uint32_t i,j;
    CHECK(presentation.effective==&effective&&presentation.localized==local);
    CHECK(presentation.language==local->language&&presentation.resource_count==expected_count);
    CHECK(memcmp(presentation.resources,expected_resources,sizeof(expected_resources))==0);
    CHECK(compare_value(local,0,DOK_PRESENT_PALETTE,DOK_PRESENT_KIND_PALETTE,local->palette));
    CHECK(compare_value(local,0,DOK_PRESENT_LIGHT,DOK_PRESENT_KIND_LIGHT,local->light));
    for(i=0;i<local->object_count;++i){const dok_level_object *o=&local->objects[i];
        CHECK(compare_value(local,o->glyph,DOK_PRESENT_SPRITE,DOK_PRESENT_KIND_IMAGE,o->sprite));
        CHECK(compare_value(local,o->glyph,DOK_PRESENT_ANIMATION,DOK_PRESENT_KIND_ANIMATION,o->animation));
        CHECK(compare_value(local,o->glyph,DOK_PRESENT_SOUND,DOK_PRESENT_KIND_SOUND,o->sound));
        for(j=0;j<o->field_count;++j){const dok_level_field *f=&o->fields[j];
            if(f->kind>=DOK_LEVEL_IMAGE)CHECK(compare_value(local,o->glyph,f->native_offset,f->kind,f->token));
            else{dok_presentation_value value,before;memset(&value,0xa5,sizeof(value));before=value;
                CHECK(dok_presentation_get(&presentation,o->glyph,f->native_offset,&value)==DOK_BALANCE_NOT_FOUND);
                CHECK(memcmp(&value,&before,sizeof(value))==0);++numeric_rejections;
            }
        }
    }
    return 1;
}
static int rejected_alias(const dok_asset_session *s)
{
    preserved=presentation;
    CHECK(dok_presentation_bind(&mutant,&mutant,s,&candidate,&presentation)!=DOK_BALANCE_OK);
    CHECK(memcmp(&presentation,&preserved,sizeof(presentation))==0);++alias_rejections;return 1;
}
static int level(const dok_asset_session *s,uint32_t index)
{
    dok_balance_profile profile=dok_balance_accepted_profile();dok_balance_diff diff;
    uint32_t step,i,j;unsigned char source_hash_before[32],source_hash_after[32];
    CHECK(load(s,1,index,ru_bytes,&base));CHECK(load(s,2,index,en_bytes,&english));
    CHECK(dok_balance_apply(&profile,s->_source_sha256[0],&base,&workspace.level,&effective,&diff)==DOK_BALANCE_OK);
    CHECK(dok_initial_world_build(&effective,100u+index,&workspace.world,&world)==DOK_WORLD_OK);
    /* A progressed world: language rebinding must preserve these changes too. */
    world.maps[DOK_WORLD_VISITED][37]=1;world.maps[DOK_WORLD_TMAP][41]=' ';
    CHECK(world.hero.object_id>0&&world.hero.object_id<=world.object_count);
    world.objects[world.hero.object_id-1].x+=17;
    world_before=world;effective_before=effective;
    hash(effective.source,effective.source_bytes,source_hash_before);
    for(step=0;step<3;++step){
        const dok_level_definition *local=step==1?&english:&effective;
        CHECK(expected_catalog(s,local->language));
        CHECK(dok_presentation_bind(&effective,local,s,&candidate,&presentation)==DOK_BALANCE_OK);
        CHECK(compare_all(local));++switches;
        CHECK(memcmp(&world,&world_before,sizeof(world))==0);
        CHECK(memcmp(&effective,&effective_before,sizeof(effective))==0);
        hash(effective.source,effective.source_bytes,source_hash_after);
        CHECK(memcmp(source_hash_before,source_hash_after,32)==0);
    }
    /* The pre-N19 nonalias original RU presentation still works. */
    CHECK(dok_presentation_bind(&effective,&base,s,&candidate,&presentation)==DOK_BALANCE_OK);
    CHECK(compare_all(&base));
    mutant=effective;mutant.objects[0].width_parameter++;CHECK(rejected_alias(s));
    mutant=effective;mutant.maps[0][0]^=1;CHECK(rejected_alias(s));
    mutant=effective;mutant.source_sha256[0]^=1;CHECK(rejected_alias(s));
    mutant=effective;memcpy(mutant_bytes,effective.source,effective.source_bytes);mutant.source=mutant_bytes;
    mutant_bytes[0]^=1;CHECK(rejected_alias(s));
    /* Re-signing a modified source is still rejected by original sample pins. */
    hash(mutant_bytes,mutant.source_bytes,mutant.source_sha256);CHECK(rejected_alias(s));
    mutant=effective;
    for(i=0;i<mutant.object_count;++i){uint32_t changed=0;
        for(j=0;j<mutant.objects[i].field_count;++j)if(mutant.objects[i].fields[j].kind<DOK_LEVEL_IMAGE){
            mutant.objects[i].fields[j].value^=1;changed=1;break;
        }
        if(changed)break;
    }
    CHECK(i<mutant.object_count);CHECK(rejected_alias(s));
    if(index==18){
        /* Only exact pointer alias gets effective-profile validation. A separate
         * balanced LEVEL1 copy cannot masquerade as an original local sample. */
        mutant=effective;preserved=presentation;
        CHECK(dok_presentation_bind(&effective,&mutant,s,&candidate,&presentation)!=DOK_BALANCE_OK);
        CHECK(memcmp(&presentation,&preserved,sizeof(presentation))==0);++alias_rejections;
    }
    preserved=presentation;
    CHECK(dok_presentation_bind(&effective,&effective,s,&presentation,&presentation)==DOK_BALANCE_INVALID_ARGUMENT);
    CHECK(memcmp(&presentation,&preserved,sizeof(presentation))==0);++alias_rejections;
    CHECK(memcmp(&effective,&effective_before,sizeof(effective))==0);
    CHECK(memcmp(&world,&world_before,sizeof(world))==0);return 1;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t i;int ok=1;
    if(argc!=2)return 1;
    if(nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK)return 1;
    if(dok_assets_open(&reader,&asset_workspace,&session)!=DOK_ASSET_OK){nds_asset_file_close(&file);return 1;}
    for(i=18;i<=20&&ok;++i)ok=level(&session,i);
    nds_asset_file_close(&file);
    printf("{\"passed\":%s,\"checks\":%u,\"levels\":3,\"language_binds\":%u,\"bindings\":%u,\"texts\":%u,\"resources\":%u,\"numeric_rejections\":%u,\"alias_rejections\":%u}\n",
        ok?"true":"false",checks,switches,bindings,texts,resources,numeric_rejections,alias_rejections);
    return ok?0:1;
}
