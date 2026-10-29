#include "dok/balance.h"
#include "dok/sha256.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"N05 balance line %d: %s\n",__LINE__,#x);return 0;}} while(0)
static dok_asset_workspace asset_workspace;
static dok_world_workspace workspace;
static dok_level_definition base,effective,localized,mutant,before_out,base_copy;
static dok_initial_world world,world_before;
static dok_level_presentation presentation,presentation_candidate,presentation_before;
static unsigned char ru_bytes[32768],local_bytes[32768],mutant_bytes[32768];
static uint32_t changed_total,retained_total,binding_total,rejected_total,resigned_total;

typedef struct Reader {FILE *file;const unsigned char *bytes;uint32_t size;} Reader;
static dok_asset_status read_at(void *context,uint32_t offset,unsigned char *out,uint32_t n,uint32_t *actual)
{
    Reader *r=context;
    if(offset>r->size || n>r->size-offset) return DOK_ASSET_IO_ERROR;
    if(r->file!=NULL) {
        if(offset>(uint32_t)LONG_MAX || fseek(r->file,(long)offset,SEEK_SET)!=0) return DOK_ASSET_IO_ERROR;
        *actual=(uint32_t)fread(out,1,n,r->file);
        return ferror(r->file)?DOK_ASSET_IO_ERROR:DOK_ASSET_OK;
    }
    memcpy(out,r->bytes+offset,n);*actual=n;return DOK_ASSET_OK;
}
static dok_asset_reader descriptor(Reader *r)
{dok_asset_reader d;d.context=r;d.read_at=read_at;d.file_bytes=r->size;return d;}
static uint32_t read32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t read16(const unsigned char *p)
{return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static void hash(const void *p,uint32_t n,unsigned char out[32])
{dok_sha256 h;dok_sha256_begin(&h);dok_sha256_update(&h,p,n);dok_sha256_end(&h,out);}
static void rehash_header(unsigned char *p)
{hash(p+160u,read32(p+16u)*128u,p+32u);hash(p,128u,p+128u);}
static uint32_t id(uint32_t language,uint32_t index,uint32_t representation)
{return (language<<30)|(index<<18)|(representation<<10);}
static int load(const dok_asset_session *s,uint32_t language,uint32_t index,unsigned char *storage,dok_level_definition *out)
{
    dok_asset_view v;
    CHECK(dok_assets_load(s,id(language,index,14u),storage,32768u,&v)==DOK_ASSET_OK);
    CHECK(dok_level_parse(&v,&workspace.level,out)==DOK_WORLD_OK);
    return 1;
}
static int profile_rejected(const dok_balance_profile *p,const unsigned char archive[32],const dok_level_definition *model)
{
    dok_balance_diff diff,before;
    dok_balance_status status;
    memset(&before_out,0xa5,sizeof(before_out));memset(&effective,0xa5,sizeof(effective));
    memset(&diff,0x5a,sizeof(diff));before=diff;
    status=dok_balance_apply(p,archive,model,&workspace.level,&effective,&diff);
    CHECK(status!=DOK_BALANCE_OK);CHECK(memcmp(&effective,&before_out,sizeof(effective))==0);
    CHECK(memcmp(&diff,&before,sizeof(diff))==0);++rejected_total;return 1;
}
static int apply_checks(const dok_asset_session *s,uint32_t index)
{
    static const unsigned char boyar[]={0x81,0x8e,0x9f,0x90,0x5f,0x88};
    static const unsigned char mouse[]={0x8c,0x9b,0x98,0x9c,0x5f,0x8b};
    dok_balance_profile profile=dok_balance_accepted_profile();dok_balance_diff diff;
    uint32_t i,j,boyars=0,mice=0,actual_changes=0;unsigned char digest[32],before_digest[32];
    CHECK(load(s,1u,index,ru_bytes,&base));base_copy=base;
    CHECK(dok_balance_model_digest(&base,before_digest)==DOK_BALANCE_OK);
    CHECK(dok_balance_apply(&profile,s->_source_sha256[0],&base,&workspace.level,&effective,&diff)==DOK_BALANCE_OK);
    CHECK(memcmp(&base,&base_copy,sizeof(base))==0);
    CHECK(effective.source==base.source);CHECK(effective.source_bytes==base.source_bytes);
    CHECK(dok_balance_model_digest(&effective,digest)==DOK_BALANCE_OK);
    if(index!=18u) {
        CHECK(memcmp(&base,&effective,sizeof(base))==0);CHECK(diff.count==0u);
        CHECK(memcmp(digest,before_digest,32u)==0);
    } else {
        CHECK(diff.count==39u);CHECK(diff.retained_assertions==35u);
        CHECK(diff.boyar_definitions==33u);CHECK(diff.mouse_definitions==30u);
        mutant=effective;
        for(i=0;i<base.object_count;++i) {
            const dok_level_object *a=base.objects+i,*b=effective.objects+i;
            const dok_level_field *health=dok_level_field_at(b,0xa4u),*speed=dok_level_field_at(b,0x38u),*wake=dok_level_field_at(b,0xbau);
            uint32_t is_boyar=a->class_id==9u && dok_level_span_equal(&base,a->animation,boyar,sizeof(boyar));
            uint32_t is_mouse=a->class_id==9u && dok_level_span_equal(&base,a->animation,mouse,sizeof(mouse));
            boyars+=is_boyar;mice+=is_mouse;
            if(is_boyar) CHECK(dok_level_field_at(b,0xaeu)->value==24);
            if(is_mouse) CHECK(dok_level_field_at(b,0xaeu)->value==8);
            if(a->glyph==0x81u || a->glyph==0x86u) {CHECK(health->value==42);CHECK(speed->value==20);CHECK(wake->value==9000);}
            if(a->glyph==0x8cu) CHECK(health->value==12);
            if(a->glyph==0xa0u) {CHECK(health->value==54);CHECK(speed->value==60);CHECK(wake->value==3400);CHECK(b->width_parameter==7);}
            for(j=0;j<a->field_count;++j) {
                if(a->fields[j].value!=b->fields[j].value) {
                    uint32_t k,matches=0;
                    ++actual_changes;
                    for(k=0;k<diff.count;++k)
                        if(diff.changes[k].glyph==a->glyph && diff.changes[k].native_offset==a->fields[j].native_offset) {
                            const dok_balance_change *c=diff.changes+k;
                            ++matches;CHECK(c->before_ru==a->fields[j].value);CHECK(c->value==b->fields[j].value);
                            if(c->native_offset==0xaeu) CHECK(c->reason==DOK_BALANCE_ENG_BOYAR_N && is_boyar);
                            else if(c->native_offset==0xbau) CHECK(c->reason==DOK_BALANCE_ENG_WAKE);
                            else if(c->native_offset==0xa4u) CHECK(c->reason==DOK_BALANCE_ENG_HEALTH);
                            else CHECK(c->native_offset==0x38u && c->reason==(a->glyph==0xa0u?DOK_BALANCE_HYBRID_BABA_SPEED:DOK_BALANCE_ENG_SPEED));
                        }
                    CHECK(matches==1u);mutant.objects[i].fields[j].value=a->fields[j].value;
                }
            }
        }
        CHECK(boyars==33u);CHECK(mice==30u);CHECK(actual_changes==39u);
        CHECK(memcmp(&mutant,&base,sizeof(base))==0); /* No map/common/weapon/span or extra-field edits. */
        changed_total+=diff.count;retained_total+=diff.retained_assertions;
    }
    return 1;
}
static int presentation_checks(const dok_asset_session *s,uint32_t index)
{
    dok_balance_profile profile=dok_balance_accepted_profile();dok_balance_diff diff;
    dok_presentation_value value,before_value;uint32_t language,i,j;
    CHECK(load(s,1u,index,ru_bytes,&base));
    CHECK(dok_balance_apply(&profile,s->_source_sha256[0],&base,&workspace.level,&effective,&diff)==DOK_BALANCE_OK);
    CHECK(dok_initial_world_build(&effective,index-17u,&workspace.world,&world)==DOK_WORLD_OK);world_before=world;base_copy=effective;
    for(language=1u;language<=2u;++language) {
        CHECK(load(s,language,index,local_bytes,&localized));
        CHECK(dok_presentation_bind(&effective,&localized,s,&presentation_candidate,&presentation)==DOK_BALANCE_OK);
        CHECK(presentation.language==language);CHECK(presentation.resource_count==146u);
        for(i=0;i<effective.object_count;++i) {
            const dok_level_object *o=effective.objects+i;
            for(j=0;j<3u;++j) {CHECK(dok_presentation_get(&presentation,o->glyph,(uint16_t)(0xfff0u+j),&value)==DOK_BALANCE_OK);++binding_total;}
            for(j=0;j<o->field_count;++j) {
                const dok_level_field *f=o->fields+j;
                if(f->kind>=DOK_LEVEL_IMAGE) {
                    CHECK(dok_presentation_get(&presentation,o->glyph,f->native_offset,&value)==DOK_BALANCE_OK);
                    CHECK(value.kind==f->kind);
                    if(f->kind==DOK_LEVEL_TEXT) {CHECK(value.asset_id==0u);CHECK(value.text_bytes<=39u);}
                    else CHECK((value.asset_id>>30)==language);
                    ++binding_total;
                } else {
                    memset(&value,0xa5,sizeof(value));before_value=value;
                    CHECK(dok_presentation_get(&presentation,o->glyph,f->native_offset,&value)==DOK_BALANCE_NOT_FOUND);
                    CHECK(memcmp(&value,&before_value,sizeof(value))==0);
                }
            }
        }
        CHECK(dok_presentation_get(&presentation,0u,DOK_PRESENT_PALETTE,&value)==DOK_BALANCE_OK);
        CHECK(value.kind==DOK_PRESENT_KIND_PALETTE && (value.asset_id>>30)==language);
        CHECK(dok_presentation_get(&presentation,0u,DOK_PRESENT_LIGHT,&value)==DOK_BALANCE_OK);
        CHECK(value.kind==DOK_PRESENT_KIND_LIGHT && (value.asset_id>>30)==language);
        CHECK(memcmp(&world,&world_before,sizeof(world))==0);CHECK(memcmp(&effective,&base_copy,sizeof(effective))==0);
        CHECK(dok_presentation_get(&presentation,0u,DOK_PRESENT_LIGHT,(dok_presentation_value *)(void *)&effective)==DOK_BALANCE_INVALID_ARGUMENT);
        CHECK(memcmp(&effective,&base_copy,sizeof(effective))==0);
        presentation_before=presentation;mutant=localized;mutant.objects[0].width_parameter++;
        CHECK(dok_presentation_bind(&effective,&mutant,s,&presentation_candidate,&presentation)!=DOK_BALANCE_OK);
        CHECK(memcmp(&presentation,&presentation_before,sizeof(presentation))==0);
    }
    return 1;
}
static int negative_checks(const dok_asset_session *s)
{
    static const uint32_t wrong_sizes[]={0u,1u,24383u,UINT32_MAX};
    dok_balance_profile p=dok_balance_accepted_profile(),bad;unsigned char archive[32];
    unsigned char digest[32],before_digest[32];
    dok_level_object swap;uint32_t i;dok_balance_diff diff,before_diff;
    CHECK(load(s,1u,18u,ru_bytes,&base));base_copy=base;
    CHECK(load(s,2u,18u,local_bytes,&localized));CHECK(profile_rejected(&p,s->_source_sha256[0],&localized));
    memcpy(archive,s->_source_sha256[0],32u);archive[0]^=1u;CHECK(profile_rejected(&p,archive,&base));
    bad=p;bad.identity^=1u;CHECK(profile_rejected(&bad,s->_source_sha256[0],&base));
    bad=p;bad.revision++;CHECK(profile_rejected(&bad,s->_source_sha256[0],&base));
    bad=p;bad.implementation=NULL;CHECK(profile_rejected(&bad,s->_source_sha256[0],&base));
    mutant=base;mutant.maps[0][0]^=1u;CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
    mutant=base;mutant.objects[0].width_parameter++;CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
    mutant=base;mutant.objects[0].sprite_flags++;CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
    mutant=base;mutant.settings[3]++;CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
    mutant=base;mutant.source_sha256[0]^=1u;CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
    mutant=base;mutant.objects[0].raw_tail.offset++;CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
    mutant=base;swap=mutant.objects[0];mutant.objects[0]=mutant.objects[1];mutant.objects[1]=swap;
    for(i=0;i<mutant.object_count;++i) mutant.glyph_index[mutant.objects[i].glyph]=(uint16_t)(i+1u);
    CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant)); /* Closed sample includes source order. */
    mutant=base;mutant.objects[base.glyph_index[0x81u]-1u].fields[0].value++;
    CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
    for(i=0;i<sizeof(wrong_sizes)/sizeof(wrong_sizes[0]);++i) {
        mutant=base;mutant.source_bytes=wrong_sizes[i];
        CHECK(profile_rejected(&p,s->_source_sha256[0],&mutant));
        memset(digest,0xa5,sizeof(digest));memcpy(before_digest,digest,sizeof(digest));
        CHECK(dok_balance_model_digest(&mutant,digest)==DOK_BALANCE_WRONG_SAMPLE);
        CHECK(memcmp(digest,before_digest,sizeof(digest))==0);
    }
    memset(&diff,0xa5,sizeof(diff));before_diff=diff;
    CHECK(dok_balance_apply(&p,s->_source_sha256[0],&base,&workspace.level,&base,&diff)==DOK_BALANCE_INVALID_ARGUMENT);
    CHECK(memcmp(&base,&base_copy,sizeof(base))==0);CHECK(memcmp(&diff,&before_diff,sizeof(diff))==0);
    return 1;
}
static int resigned_checks(const unsigned char *package,uint32_t size)
{
    unsigned char *copy=malloc(size),digest[32];uint32_t count,i,offset=0,length=0,token;
    dok_asset_session s;dok_asset_reader d;Reader r={0};dok_asset_view view;
    dok_balance_profile profile=dok_balance_accepted_profile();
    CHECK(copy!=NULL);memcpy(copy,package,size);count=read32(copy+16u);
    for(i=0;i<count;++i) {
        const unsigned char *row=copy+160u+i*128u;
        if(read32(row)==id(1u,18u,14u)) {offset=read32(row+8u);length=read32(row+12u);break;}
    }
    CHECK(offset!=0u && length<32768u);
    /* Change a genuine original scalar token while keeping a valid format and
     * resigning payload aliases, source provenance, catalog and header. */
    token=base.objects[base.glyph_index[0x81u]-1u].fields[0].token.offset;
    CHECK(copy[offset+token]=='6' && copy[offset+token+1u]=='0');copy[offset+token+1u]='1';
    hash(copy+offset,length,digest);
    for(i=0;i<count;++i) {
        unsigned char *row=copy+160u+i*128u;
        if(row[18]==1u && read16(row+20u)==18u) memcpy(row+72u,digest,32u);
        if(read32(row+8u)==offset && read32(row+12u)==length) memcpy(row+40u,digest,32u);
    }
    rehash_header(copy);r.bytes=copy;r.size=size;d=descriptor(&r);
    CHECK(dok_assets_open(&d,&asset_workspace,&s)==DOK_ASSET_OK);
    CHECK(dok_assets_load(&s,id(1u,18u,14u),mutant_bytes,sizeof(mutant_bytes),&view)==DOK_ASSET_OK);
    CHECK(dok_level_parse(&view,&workspace.level,&mutant)==DOK_WORLD_OK);
    CHECK(profile_rejected(&profile,s._source_sha256[0],&mutant));++resigned_total;
    /* Archive pins are another independent guard, not a generic N04 schema. */
    memcpy(copy,package,size);copy[64u]^=1u;rehash_header(copy);
    CHECK(dok_assets_open(&d,&asset_workspace,&s)==DOK_ASSET_OK);
    CHECK(dok_assets_load(&s,id(1u,18u,14u),mutant_bytes,sizeof(mutant_bytes),&view)==DOK_ASSET_OK);
    CHECK(dok_level_parse(&view,&workspace.level,&mutant)==DOK_WORLD_OK);
    CHECK(profile_rejected(&profile,s._source_sha256[0],&mutant));++resigned_total;
    free(copy);return 1;
}
int main(int argc,char **argv)
{
    Reader r={0};dok_asset_reader d;dok_asset_session s;FILE *file;long length;uint32_t index;
    unsigned char *package;int ok=1;
    if(argc!=2) {fprintf(stderr,"usage: test_balance PACKAGE\n");return 1;}
    file=fopen(argv[1],"rb");if(file==NULL) return 1;
    if(fseek(file,0,SEEK_END)!=0 || (length=ftell(file))<=0 || (unsigned long)length>UINT32_MAX) {fclose(file);return 1;}
    r.file=file;r.size=(uint32_t)length;d=descriptor(&r);
    if(dok_assets_open(&d,&asset_workspace,&s)!=DOK_ASSET_OK) {fclose(file);return 1;}
    for(index=18u;index<=20u && ok;++index) ok=apply_checks(&s,index) && presentation_checks(&s,index);
    if(ok) ok=negative_checks(&s);
    package=malloc(r.size);
    if(package==NULL || fseek(file,0,SEEK_SET)!=0 || fread(package,1,r.size,file)!=r.size) ok=0;
    if(ok) ok=resigned_checks(package,r.size);
    free(package);fclose(file);
    printf("{\"passed\":%s,\"checks\":%u,\"accepted_changes\":%u,\"retained_assertions\":%u,\"presentation_bindings\":%u,\"rejected_models\":%u,\"resigned_package_cases\":%u}\n",
        ok?"true":"false",(unsigned)checks,(unsigned)changed_total,(unsigned)retained_total,(unsigned)binding_total,(unsigned)rejected_total,(unsigned)resigned_total);
    return ok?0:1;
}
