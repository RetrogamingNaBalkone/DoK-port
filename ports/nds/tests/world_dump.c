#include "world_export.h"
#include "dok/balance.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static dok_asset_workspace asset_workspace;
static dok_world_workspace world_workspace;
static dok_level_definition base,effective,localized;
static dok_initial_world initial;
static dok_level_presentation presentation,presentation_candidate;
static unsigned char ru_bytes[32768],localized_bytes[32768],map_bytes[16384];

static dok_asset_status read_at(void *context,uint32_t offset,unsigned char *out,
    uint32_t requested,uint32_t *actual)
{
    FILE *file=context;
    if(offset>(uint32_t)LONG_MAX || fseek(file,(long)offset,SEEK_SET)!=0) return DOK_ASSET_IO_ERROR;
    *actual=(uint32_t)fread(out,1,requested,file);
    return ferror(file)?DOK_ASSET_IO_ERROR:DOK_ASSET_OK;
}
static int emit(void *context,const char *p,uint32_t n)
{return fwrite(p,1,n,(FILE *)context)==n;}
static int fail(const char *operation,unsigned int status)
{fprintf(stderr,"N05 world_dump: %s failed (%u)\n",operation,status);return 1;}
int main(int argc,char **argv)
{
    dok_asset_session session;dok_asset_reader reader;dok_asset_view view,map_view;
    dok_asset_status asset_status;dok_world_status world_status;dok_balance_status balance_status;
    dok_balance_profile profile=dok_balance_accepted_profile();dok_balance_diff diff;
    FILE *file;long file_bytes;unsigned long number,language,epoch=1;uint32_t index,id;
    if(argc<4 || argc>5) return fail("usage: PACKAGE LEVEL(1..3) LANGUAGE(1..2) [EPOCH]",1u);
    number=strtoul(argv[2],NULL,10);language=strtoul(argv[3],NULL,10);
    if(argc==5) epoch=strtoul(argv[4],NULL,10);
    if(number<1 || number>3 || language<1 || language>2 || epoch==0 || epoch>UINT32_MAX) return fail("arguments",1u);
    file=fopen(argv[1],"rb");if(file==NULL) return fail("fopen",1u);
    if(fseek(file,0,SEEK_END)!=0 || (file_bytes=ftell(file))<0 || (unsigned long)file_bytes>UINT32_MAX) {fclose(file);return fail("file size",1u);}
    reader.context=file;reader.read_at=read_at;reader.file_bytes=(uint32_t)file_bytes;
    asset_status=dok_assets_open(&reader,&asset_workspace,&session);
    if(asset_status!=DOK_ASSET_OK) {fclose(file);return fail("assets_open",asset_status);}
    index=(uint32_t)number+17u;id=(1u<<30)|(index<<18)|(14u<<10);
    asset_status=dok_assets_load(&session,id,ru_bytes,sizeof(ru_bytes),&view);
    if(asset_status!=DOK_ASSET_OK) {fclose(file);return fail("load RU LEVEL",asset_status);}
    world_status=dok_level_parse(&view,&world_workspace.level,&base);
    if(world_status!=DOK_WORLD_OK) {fclose(file);return fail("parse RU LEVEL",world_status);}
    balance_status=dok_balance_apply(&profile,session._source_sha256[0],&base,&world_workspace.level,&effective,&diff);
    if(balance_status!=DOK_BALANCE_OK) {fclose(file);return fail("apply profile",balance_status);}
    id=(1u<<30)|(index<<18)|(13u<<10);
    asset_status=dok_assets_load(&session,id,map_bytes,sizeof(map_bytes),&map_view);
    if(asset_status!=DOK_ASSET_OK) {fclose(file);return fail("load RU maps",asset_status);}
    world_status=dok_level_validate_maps(&base,&map_view);
    if(world_status!=DOK_WORLD_OK) {fclose(file);return fail("validate RU maps",world_status);}
    world_status=dok_initial_world_build(&effective,(uint32_t)epoch,&world_workspace.world,&initial);
    if(world_status!=DOK_WORLD_OK) {fclose(file);return fail("build initial world",world_status);}
    id=((uint32_t)language<<30)|(index<<18)|(14u<<10);
    asset_status=dok_assets_load(&session,id,localized_bytes,sizeof(localized_bytes),&view);
    if(asset_status!=DOK_ASSET_OK) {fclose(file);return fail("load localized LEVEL",asset_status);}
    world_status=dok_level_parse(&view,&world_workspace.level,&localized);
    if(world_status!=DOK_WORLD_OK) {fclose(file);return fail("parse localized LEVEL",world_status);}
    balance_status=dok_presentation_bind(&effective,&localized,&session,&presentation_candidate,&presentation);
    if(balance_status!=DOK_BALANCE_OK) {fclose(file);return fail("bind presentation",balance_status);}
    if(!dok_test_world_export(&base,&effective,&initial,&presentation,emit,stdout)) {fclose(file);return fail("export",1u);}
    if(fclose(file)!=0) return fail("fclose",1u);
    return 0;
}
