#include "runtime.h"
#include "dok/sprite_assets.h"
#include "dok/sha256.h"
#include "file_reader.h"
#include <stdio.h>
#include <string.h>

uint16_t n19_test_palette[256];
static uint32_t checks,language_calls,text_draws,animation_steps,changed_texts;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"N19 actual language line %d: %s\n",__LINE__,#x);return 0;}} while(0)
static dok_asset_workspace catalog_workspace;
static dok_render_workspace workspace;
static dok_render_world world;
static dok_level_definition original_ru;
static dok_sprite_asset_bank bank,bank_before,temporary_bank;
static NdsUiRuntime ui;
static unsigned char ru_bytes[32768],en_bytes[32768],metadata[65536],hud[DOK_UI_HUD_BYTES],expected[DOK_UI_HUD_BYTES],vram[DOK_UI_HUD_BYTES];
static unsigned char world_before[32],bank_metadata_before[32];

static void hash(const void *p,uint32_t bytes,unsigned char out[32])
{dok_sha256 h;dok_sha256_begin(&h);dok_sha256_update(&h,p,bytes);dok_sha256_end(&h,out);}
static int load(const dok_asset_session *s,uint32_t language,uint32_t index,unsigned char *bytes,dok_level_definition *out)
{
    dok_asset_view view;dok_asset_id id=language<<30|index<<18|(uint32_t)DOK_ASSET_LEVEL_CP866<<10;
    CHECK(dok_assets_load(s,id,bytes,32768,&view)==DOK_ASSET_OK);
    CHECK(dok_level_parse(&view,&workspace.world.level,out)==DOK_WORLD_OK);return 1;
}
static int unchanged(void)
{
    unsigned char after[32];
    hash(&world,sizeof(world),after);CHECK(memcmp(after,world_before,32)==0);
    CHECK(memcmp(&bank,&bank_before,sizeof(bank))==0);
    hash(metadata,bank.metadata_bytes,after);CHECK(memcmp(after,bank_metadata_before,32)==0);
    CHECK(memcmp(ui.language_before,ui.language_after,32)==0);
    CHECK(memcmp(ui.simulation_before,ui.simulation_after,32)==0);
    hash(&world.presentation,sizeof(world.presentation),after);CHECK(memcmp(after,ui.simulation_after,32)==0);
    return 1;
}
static int animation_resolves(uint32_t simulation_language)
{
    uint32_t i,j;
    for(i=0;i<world.effective.object_count;++i){
        const dok_level_object *o=&world.effective.objects[i];dok_presentation_value v;
        CHECK(dok_presentation_get(&world.presentation,o->glyph,DOK_PRESENT_ANIMATION,&v)==DOK_BALANCE_OK);
        if(!v.asset_id)continue;
        CHECK(v.asset_id>>30==simulation_language);
        for(j=0;j<bank.animation_count;++j)if(bank.animations[j]==v.asset_id)break;
        CHECK(j<bank.animation_count);
        temporary_bank=bank;
        CHECK(dok_sprite_asset_select(&temporary_bank,o->glyph,v.asset_id,0)==DOK_STATUS_OK);
        CHECK(temporary_bank.bindings[o->glyph].frame.image>>30==simulation_language);
        CHECK(dok_sprite_asset_image(&bank,temporary_bank.bindings[o->glyph].frame.image)!=NULL);
        dok_sprite_player player;uint32_t interval,cancel;
        memset(&player,0,sizeof(player));player.animation=bank.sequences[j];player.cursor=-1;player.remaining=2;
        CHECK(dok_sprite_player_step(&player,&interval,&cancel)==DOK_STATUS_OK&&cancel==0);
        CHECK(player.displayed.image==temporary_bank.bindings[o->glyph].frame.image);
        CHECK(dok_sprite_player_step(&player,&interval,&cancel)==DOK_STATUS_OK&&cancel==0);
        CHECK(player.displayed.image>>30==simulation_language);
        CHECK(dok_sprite_asset_image(&bank,player.displayed.image)!=NULL);animation_steps+=2;
    }
    return 1;
}
static int ui_texts(uint32_t language)
{
    const dok_level_definition *local=language==1?&world.effective:&world.base;
    dok_ui_values values={100,0,100,100,3,0,7,2};
    uint32_t i,j;
    for(i=0;i<local->object_count;++i){const dok_level_object *o=&local->objects[i];
        for(j=0;j<o->field_count;++j){const dok_level_field *f=&o->fields[j];dok_presentation_value v;
            if(f->kind!=DOK_LEVEL_TEXT)continue;
            uint32_t bytes=f->token.length>39?39:f->token.length;
            CHECK(dok_presentation_get(&ui.presentation,o->glyph,f->native_offset,&v)==DOK_BALANCE_OK);
            CHECK(v.kind==DOK_PRESENT_KIND_TEXT&&v.text==local->source+f->token.offset&&v.text_bytes==bytes);
            nds_ui_notice(&ui,1,o->glyph,f->native_offset);
            nds_ui_update(&ui,&world,&values,1234,hud,vram,77,55);
            dok_ui_hud(expected,ui.bank,language,&values,local->source+f->token.offset,bytes,0);
            CHECK(memcmp(hud,expected,sizeof(hud))==0&&memcmp(vram,expected,sizeof(vram))==0);
            CHECK(ui.message_bytes==bytes&&ui.fault==0&&ui.vram_verified>0);++text_draws;
            uint32_t other_index=(language==1?world.base.glyph_index[o->glyph]:world.effective.glyph_index[o->glyph]);
            const dok_level_definition *other=language==1?&world.base:&world.effective;
            CHECK(other_index>0);
            const dok_level_field *other_field=dok_level_field_at(&other->objects[other_index-1],f->native_offset);
            CHECK(other_field!=NULL);
            if(other_field->token.length!=f->token.length||memcmp(local->source+f->token.offset,other->source+other_field->token.offset,f->token.length)!=0)++changed_texts;
        }
    }
    CHECK(memcmp(ui.published_simulation,ui.simulation_before,32)==0);
    CHECK(memcmp(ui.published_simulation+32,ui.simulation_after,32)==0);
    return 1;
}
static int scenario(const dok_asset_session *s,uint32_t index,uint32_t simulation_language,const char *ui_path)
{
    dok_balance_profile profile=dok_balance_accepted_profile();dok_balance_diff diff;FILE *f;
    memset(&world,0,sizeof(world));memset(&ui,0,sizeof(ui));memset(metadata,0,sizeof(metadata));
    CHECK(load(s,1,index,ru_bytes,&original_ru));CHECK(load(s,2,index,en_bytes,&world.base));
    CHECK(dok_balance_apply(&profile,s->_source_sha256[0],&original_ru,&workspace.world.level,&world.effective,&diff)==DOK_BALANCE_OK);
    CHECK(dok_initial_world_build(&world.effective,index,&workspace.world.world,&world.initial)==DOK_WORLD_OK);
    world.initial.maps[DOK_WORLD_VISITED][37]=1;world.initial.objects[world.initial.hero.object_id-1].x+=29;
    const dok_level_definition *local=simulation_language==1?&world.effective:&world.base;
    CHECK(dok_presentation_bind(&world.effective,local,s,&workspace.presentation,&world.presentation)==DOK_BALANCE_OK);
    CHECK(dok_sprite_assets_load(&world.presentation,&bank,metadata,sizeof(metadata))==DOK_STATUS_OK);
    bank_before=bank;hash(metadata,bank.metadata_bytes,bank_metadata_before);hash(&world,sizeof(world),world_before);
    f=fopen(ui_path,"rb");CHECK(f!=NULL);CHECK(fseek(f,32,SEEK_SET)==0);
    CHECK(fread(ui.bank,1,sizeof(ui.bank),f)==sizeof(ui.bank));CHECK(fclose(f)==0);
    ui.level=index-17;ui.stage_count=1;ui.lives=3;
    for(uint32_t step=0;step<3;++step){uint32_t language=step==1?2:1;
        CHECK(nds_ui_language(&ui,&world,&workspace,language)==1);++language_calls;
        CHECK(ui.language==language&&ui.presentation.language==language);
        CHECK(ui.presentation.effective==&world.effective);
        CHECK(ui.presentation.localized==(language==1?&world.effective:&world.base));
        CHECK(unchanged());CHECK(animation_resolves(simulation_language));CHECK(ui_texts(language));CHECK(unchanged());
    }
    CHECK(ui.language_changes==2);return 1;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;int ok=1;
    if(argc!=3)return 1;
    if(nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK)return 1;
    if(dok_assets_open(&reader,&catalog_workspace,&session)!=DOK_ASSET_OK){nds_asset_file_close(&file);return 1;}
    for(uint32_t index=18;index<=20&&ok;++index)for(uint32_t language=1;language<=2&&ok;++language)ok=scenario(&session,index,language,argv[2]);
    nds_asset_file_close(&file);
    printf("{\"passed\":%s,\"checks\":%u,\"scenarios\":6,\"actual_language_calls\":%u,\"actual_text_draws\":%u,\"animation_player_steps\":%u,\"localized_text_differences\":%u}\n",
        ok?"true":"false",checks,language_calls,text_draws,animation_steps,changed_texts);
    return ok?0:1;
}
