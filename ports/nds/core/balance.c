#include "dok/balance.h"
#include "dok/sha256.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint32_t accepted_implementation[2]={DOK_BALANCE_ID,DOK_BALANCE_REVISION};
static const char *const archive_hash[2]={
    "af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816",
    "865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458"};
static const char *const source_hash[2][3]={
    {"21872af6fa49bf8c0c8fe89dfe2ca5dd040912b3f141c306258c9a4f6cb9e856",
     "f8bb9c934f2cf96e3164fbce6f0c2e3f0c666f9527642584d646899b6084fc99",
     "15e99cb968333c6998c7e9fc56e7de57de56c7013b98e5d7ea8f52f25478a66a"},
    {"ff9354074b91d020ab26892c803b984fd6367875efd190202927840afb39c089",
     "65869d72be30ccc30cb673a8a66ec1b3ebe12681a7beb5a6aa365b07fc0ced91",
     "f70efbcdf0801d59ccad30017b8953af7ecd80df9a87b6ef378a6d7bbfd5e6e7"}};
static const uint32_t source_size[2][3]={
    {24384u,24280u,27997u},{24239u,24150u,27909u}};
/* Independently computed by the original-data oracle, not C object layout. */
static const char *const model_hash[2][3]={
    {"f1253dd0121027cc4a1e4491d73fb6c2f80df817436bb5a50f29fc597cb2ff59",
     "562980ac2dd3df9fdeb692aeb33b9bd552b2ce232d24a27154bd697d9cce0500",
     "ea8e6e48887f68798171e1e55217dd1683ad78b0513e44359bf0f6acf20bdb8f"},
    {"5b021b9b05033fc644420352b2fad961e2dbf49be70114d425d815367358eabc",
     "d1683569724ddb825ddaed9a3f672c16d5f8139c4914bfe5fb79365edf164982",
     "102fa33cfe3104b75f9d284665bde733e93a0c013b40dca7f4424d37d081c7f2"}};
static const char effective1_hash[]=
    "6ced33e56473c43ab64284f0af28e506c06aa8005873b982c643246c0e1fda2b";
static const unsigned char boyar_name[]={0x81,0x8e,0x9f,0x90,0x5f,0x88};
static const unsigned char mouse_name[]={0x8c,0x9b,0x98,0x9c,0x5f,0x8b};
static const unsigned char baba_name[]={0x81,0x80,0x81,0x80,0x5f,0x80};

static uint32_t disjoint(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y) return 0u;
    return x+an<=y || y+bn<=x;
}
static uint32_t hex_digit(char c)
{
    return c>='0' && c<='9'?(uint32_t)(c-'0'):(uint32_t)(c-'a'+10);
}
static uint32_t hash_matches(const unsigned char hash[32],const char *hex)
{
    uint32_t i;
    for(i=0u;i<32u;++i)
        if(hash[i]!=(unsigned char)((hex_digit(hex[2u*i])<<4)|hex_digit(hex[2u*i+1u]))) return 0u;
    return 1u;
}
static void hash_u8(dok_sha256 *h,uint32_t value)
{
    unsigned char b=(unsigned char)value;dok_sha256_update(h,&b,1u);
}
static void hash_u16(dok_sha256 *h,uint32_t value)
{
    unsigned char b[2]={(unsigned char)value,(unsigned char)(value>>8)};
    dok_sha256_update(h,b,2u);
}
static void hash_u32(dok_sha256 *h,uint32_t value)
{
    unsigned char b[4]={(unsigned char)value,(unsigned char)(value>>8),
        (unsigned char)(value>>16),(unsigned char)(value>>24)};
    dok_sha256_update(h,b,4u);
}
static uint32_t valid_span(const dok_level_definition *l,dok_level_span s)
{
    return s.offset<=l->source_bytes && s.length<=l->source_bytes-s.offset;
}
static uint32_t known_source_size(const dok_level_definition *l)
{
    return l->language>=1u && l->language<=2u &&
        l->source_index>=18u && l->source_index<=20u &&
        l->source_bytes==source_size[(uint32_t)l->language-1u][(uint32_t)l->source_index-18u];
}
static uint32_t hash_span(dok_sha256 *h,const dok_level_definition *l,dok_level_span s)
{
    if(!valid_span(l,s)) return 0u;
    hash_u32(h,s.offset);hash_u32(h,s.length);
    dok_sha256_update(h,l->source+s.offset,s.length);return 1u;
}
dok_balance_status dok_balance_model_digest(const dok_level_definition *l,unsigned char out[32])
{
    static const unsigned char tag[]="DOKNDS-N05-MODEL1";
    dok_sha256 h;unsigned char result[32];uint32_t i,j;
    if(l==NULL || out==NULL || l->source==NULL || l->object_count>DOK_LEVEL_MAX_OBJECTS)
        return DOK_BALANCE_INVALID_ARGUMENT;
    if(!known_source_size(l)) return DOK_BALANCE_WRONG_SAMPLE;
    if(!disjoint(l,sizeof(*l),out,32u) || !disjoint(l->source,l->source_bytes,out,32u))
        return DOK_BALANCE_INVALID_ARGUMENT;
    dok_sha256_begin(&h);dok_sha256_update(&h,tag,sizeof(tag));
    hash_u32(&h,l->source_bytes);hash_u32(&h,l->source_asset_id);
    hash_u16(&h,l->source_index);hash_u8(&h,l->language);hash_u8(&h,l->reserved);
    dok_sha256_update(&h,l->source_sha256,32u);
    for(i=0u;i<8u;++i) hash_u16(&h,(uint16_t)l->settings[i]);
    hash_u32(&h,l->ignored_step_ver);hash_u32(&h,l->truncated_messages);
    if(!hash_span(&h,l,l->palette) || !hash_span(&h,l,l->light)) return DOK_BALANCE_BAD_MODEL;
    hash_u16(&h,l->object_count);
    for(i=0u;i<256u;++i) hash_u16(&h,l->glyph_index[i]);
    for(i=0u;i<l->object_count;++i) {
        const dok_level_object *o=&l->objects[i];
        if(o->field_count>DOK_LEVEL_MAX_FIELDS) return DOK_BALANCE_BAD_MODEL;
        hash_u16(&h,o->class_id);hash_u16(&h,o->flags);
        hash_u16(&h,(uint16_t)o->width_parameter);hash_u16(&h,(uint16_t)o->height_parameter);
        hash_u16(&h,(uint16_t)o->sprite_flags);hash_u8(&h,o->glyph);hash_u8(&h,o->marker);
        hash_u16(&h,o->field_count);hash_u16(&h,o->reserved);
        if(!hash_span(&h,l,o->sprite) || !hash_span(&h,l,o->animation) ||
           !hash_span(&h,l,o->sound) || !hash_span(&h,l,o->raw_line) ||
           !hash_span(&h,l,o->raw_tail)) return DOK_BALANCE_BAD_MODEL;
        for(j=0u;j<o->field_count;++j) {
            const dok_level_field *f=&o->fields[j];
            hash_u16(&h,f->native_offset);hash_u8(&h,f->kind);hash_u8(&h,f->reserved);
            hash_u32(&h,(uint32_t)f->value);
            if(!hash_span(&h,l,f->token)) return DOK_BALANCE_BAD_MODEL;
        }
    }
    dok_sha256_update(&h,l->maps,sizeof(l->maps));dok_sha256_end(&h,result);
    memcpy(out,result,32u);return DOK_BALANCE_OK;
}
static dok_balance_status validate_sample(const dok_level_definition *l,uint32_t effective)
{
    unsigned char hash[32];uint32_t language,level;dok_balance_status status;
    if(l==NULL || l->source==NULL || !known_source_size(l)) return DOK_BALANCE_WRONG_SAMPLE;
    language=(uint32_t)l->language-1u;level=(uint32_t)l->source_index-18u;
    if(l->source_asset_id!=((uint32_t)l->language<<30 | (uint32_t)l->source_index<<18 |
        (uint32_t)DOK_ASSET_LEVEL_CP866<<10)) return DOK_BALANCE_WRONG_SAMPLE;
    dok_sha256 hash_state;dok_sha256_begin(&hash_state);
    dok_sha256_update(&hash_state,l->source,l->source_bytes);dok_sha256_end(&hash_state,hash);
    if(!hash_matches(hash,source_hash[language][level]) || memcmp(hash,l->source_sha256,32u)!=0)
        return DOK_BALANCE_WRONG_SAMPLE;
    status=dok_balance_model_digest(l,hash);if(status!=DOK_BALANCE_OK) return status;
    if(effective && language==0u && level==0u) {
        if(!hash_matches(hash,effective1_hash)) return DOK_BALANCE_BAD_MODEL;
    } else if(!hash_matches(hash,model_hash[language][level])) return DOK_BALANCE_BAD_MODEL;
    return DOK_BALANCE_OK;
}
dok_balance_profile dok_balance_accepted_profile(void)
{
    dok_balance_profile p={DOK_BALANCE_ID,DOK_BALANCE_REVISION,accepted_implementation};return p;
}
static dok_level_object *object_at(dok_level_definition *l,uint8_t glyph)
{
    uint16_t index=l->glyph_index[glyph];
    return index!=0u && index<=l->object_count?&l->objects[index-1u]:NULL;
}
static uint32_t scalar_is(const dok_level_object *o,uint16_t offset,int32_t value)
{
    const dok_level_field *f;
    if(o==NULL) return 0u;
    f=dok_level_field_at(o,offset);
    return f!=NULL && f->kind<=DOK_LEVEL_CHARACTER && f->value==value;
}
static uint32_t change(dok_level_definition *l,dok_balance_diff *diff,uint8_t glyph,
    uint16_t offset,int32_t before,int32_t value,uint8_t reason)
{
    dok_level_object *o=object_at(l,glyph);uint32_t i;
    if(o==NULL || !scalar_is(o,offset,before) || diff->count>=DOK_BALANCE_CHANGE_COUNT) return 0u;
    for(i=0u;i<o->field_count;++i) if(o->fields[i].native_offset==offset) {
        dok_balance_change *c=&diff->changes[diff->count];
        c->native_offset=offset;c->glyph=glyph;c->reason=reason;c->before_ru=before;c->value=value;
        o->fields[i].value=value;++diff->count;return 1u;
    }
    return 0u;
}
dok_balance_status dok_balance_apply(const dok_balance_profile *profile,
    const unsigned char archive[32],const dok_level_definition *base,
    dok_level_definition *candidate,dok_level_definition *out,dok_balance_diff *diff)
{
    dok_balance_diff result;dok_balance_status status;uint32_t i;
    if(profile==NULL || archive==NULL || base==NULL || candidate==NULL || out==NULL || diff==NULL)
        return DOK_BALANCE_INVALID_ARGUMENT;
    if(!disjoint(base,sizeof(*base),candidate,sizeof(*candidate)) ||
       !disjoint(base,sizeof(*base),out,sizeof(*out)) ||
       !disjoint(candidate,sizeof(*candidate),out,sizeof(*out)) ||
       !disjoint(diff,sizeof(*diff),base,sizeof(*base)) ||
       !disjoint(diff,sizeof(*diff),candidate,sizeof(*candidate)) ||
       !disjoint(diff,sizeof(*diff),out,sizeof(*out)) ||
       !disjoint(profile,sizeof(*profile),candidate,sizeof(*candidate)) ||
       !disjoint(profile,sizeof(*profile),out,sizeof(*out)) ||
       !disjoint(profile,sizeof(*profile),diff,sizeof(*diff)) ||
       !disjoint(archive,32u,candidate,sizeof(*candidate)) ||
       !disjoint(archive,32u,out,sizeof(*out)) ||
       !disjoint(archive,32u,diff,sizeof(*diff)) ||
       !disjoint(base->source,base->source_bytes,candidate,sizeof(*candidate)) ||
       !disjoint(base->source,base->source_bytes,out,sizeof(*out)) ||
       !disjoint(base->source,base->source_bytes,diff,sizeof(*diff))) return DOK_BALANCE_INVALID_ARGUMENT;
    if(profile->identity!=DOK_BALANCE_ID || profile->revision!=DOK_BALANCE_REVISION ||
       profile->implementation!=accepted_implementation) return DOK_BALANCE_BAD_PROFILE;
    if(base->language!=1u || !hash_matches(archive,archive_hash[0])) return DOK_BALANCE_WRONG_SAMPLE;
    status=validate_sample(base,0u);if(status!=DOK_BALANCE_OK) return status;
    memset(&result,0,sizeof(result));*candidate=*base;
    if(base->source_index==18u) {
        for(i=0u;i<candidate->object_count;++i) {
            dok_level_object *o=&candidate->objects[i];
            if(o->class_id!=9u) continue;
            if(dok_level_span_equal(candidate,o->animation,boyar_name,sizeof(boyar_name))) {
                ++result.boyar_definitions;
                if(!scalar_is(o,0xAEu,10)) return DOK_BALANCE_BAD_MODEL;
            }
            if(dok_level_span_equal(candidate,o->animation,mouse_name,sizeof(mouse_name))) {
                ++result.mouse_definitions;
                if(!scalar_is(o,0xAEu,8)) return DOK_BALANCE_BAD_MODEL;
                ++result.retained_assertions;
            }
        }
        if(result.boyar_definitions!=33u || result.mouse_definitions!=30u)
            return DOK_BALANCE_BAD_MODEL;
        for(i=0u;i<2u;++i) {
            uint8_t glyph=(uint8_t)(i==0u?0x81u:0x86u);
            if(!scalar_is(object_at(candidate,glyph),0xA4u,42)) return DOK_BALANCE_BAD_MODEL;
            ++result.retained_assertions;
            if(!change(candidate,&result,glyph,0x38u,60,20,DOK_BALANCE_ENG_SPEED) ||
               !change(candidate,&result,glyph,0xBAu,20000,9000,DOK_BALANCE_ENG_WAKE))
                return DOK_BALANCE_BAD_MODEL;
        }
        if(!change(candidate,&result,0x8Cu,0xA4u,42,12,DOK_BALANCE_ENG_HEALTH)) return DOK_BALANCE_BAD_MODEL;
        for(i=0u;i<candidate->object_count;++i) {
            dok_level_object *o=&candidate->objects[i];
            if(o->class_id==9u && dok_level_span_equal(candidate,o->animation,boyar_name,sizeof(boyar_name)) &&
               !change(candidate,&result,o->glyph,0xAEu,10,24,DOK_BALANCE_ENG_BOYAR_N)) return DOK_BALANCE_BAD_MODEL;
        }
        {
            dok_level_object *baba=object_at(candidate,0xA0u);
            if(baba==NULL || baba->class_id!=9u ||
               !dok_level_span_equal(candidate,baba->animation,baba_name,sizeof(baba_name)) ||
               baba->width_parameter!=7 || !scalar_is(baba,0xA4u,54) || !scalar_is(baba,0xBAu,3400))
                return DOK_BALANCE_BAD_MODEL;
            result.retained_assertions+=3u;
        }
        if(!change(candidate,&result,0xA0u,0x38u,80,60,DOK_BALANCE_HYBRID_BABA_SPEED) ||
           result.count!=39u || result.retained_assertions!=35u) return DOK_BALANCE_BAD_MODEL;
        status=validate_sample(candidate,1u);if(status!=DOK_BALANCE_OK) return status;
    }
    *out=*candidate;*diff=result;return DOK_BALANCE_OK;
}

static uint16_t primary_representation(uint8_t kind)
{
    static const uint16_t reps[9]={0u,2u,3u,11u,7u,12u,14u,16u,8u};
    return kind<=8u?reps[kind]:0u;
}
static uint8_t source_kind(uint8_t presentation_kind)
{
    switch(presentation_kind) {
    case DOK_PRESENT_KIND_IMAGE:return 1u;
    case DOK_PRESENT_KIND_ANIMATION:return 2u;
    case DOK_PRESENT_KIND_SOUND:return 8u;
    case DOK_PRESENT_KIND_PALETTE:return 4u;
    case DOK_PRESENT_KIND_LIGHT:return 5u;
    default:return 0u;
    }
}
static dok_balance_status resolve(const dok_level_presentation *p,dok_level_span span,
    uint8_t kind,dok_presentation_value *out)
{
    dok_presentation_value result;uint32_t i;const dok_level_definition *l=p->localized;
    if(!valid_span(l,span)) return DOK_BALANCE_BAD_MODEL;
    memset(&result,0,sizeof(result));result.kind=kind;
    if(kind==DOK_PRESENT_KIND_TEXT) {
        result.text=l->source+span.offset;result.text_bytes=span.length>39u?39u:span.length;
        *out=result;return DOK_BALANCE_OK;
    }
    if(span.length==0u) {*out=result;return DOK_BALANCE_OK;}
    if(span.length>15u) return DOK_BALANCE_RESOURCE_ERROR;
    for(i=0u;i<p->resource_count;++i) {
        const dok_presentation_resource *r=&p->resources[i];
        if(r->kind==source_kind(kind) && r->name_cp866[span.length]==0u &&
           memcmp(r->name_cp866,l->source+span.offset,span.length)==0) {
            result.asset_id=r->id;*out=result;return DOK_BALANCE_OK;
        }
    }
    return DOK_BALANCE_RESOURCE_ERROR;
}
dok_balance_status dok_presentation_get(const dok_level_presentation *p,uint8_t glyph,
    uint16_t slot,dok_presentation_value *out)
{
    const dok_level_definition *l;const dok_level_object *o;const dok_level_field *f;uint16_t index;
    if(p==NULL || out==NULL || p->localized==NULL || p->effective==NULL || p->session==NULL ||
       p->tag!=0x4E355052u || p->resource_count>DOK_PRESENTATION_MAX_RESOURCES)
        return DOK_BALANCE_INVALID_ARGUMENT;
    l=p->localized;
    if(!disjoint(p,sizeof(*p),out,sizeof(*out)) || !disjoint(l,sizeof(*l),out,sizeof(*out)) ||
       !disjoint(p->effective,sizeof(*p->effective),out,sizeof(*out)) ||
       !disjoint(p->effective->source,p->effective->source_bytes,out,sizeof(*out)) ||
       !disjoint(p->session,sizeof(*p->session),out,sizeof(*out)) ||
       !disjoint(l->source,l->source_bytes,out,sizeof(*out))) return DOK_BALANCE_INVALID_ARGUMENT;
    if(slot==DOK_PRESENT_PALETTE) return resolve(p,l->palette,DOK_PRESENT_KIND_PALETTE,out);
    if(slot==DOK_PRESENT_LIGHT) return resolve(p,l->light,DOK_PRESENT_KIND_LIGHT,out);
    index=l->glyph_index[glyph];if(index==0u || index>l->object_count) return DOK_BALANCE_NOT_FOUND;
    o=&l->objects[index-1u];
    if(slot==DOK_PRESENT_SPRITE) return resolve(p,o->sprite,DOK_PRESENT_KIND_IMAGE,out);
    if(slot==DOK_PRESENT_ANIMATION) return resolve(p,o->animation,DOK_PRESENT_KIND_ANIMATION,out);
    if(slot==DOK_PRESENT_SOUND) return resolve(p,o->sound,DOK_PRESENT_KIND_SOUND,out);
    f=dok_level_field_at(o,slot);
    if(f==NULL || f->kind<DOK_LEVEL_IMAGE || f->kind>DOK_LEVEL_TEXT) return DOK_BALANCE_NOT_FOUND;
    return resolve(p,f->token,f->kind,out);
}
dok_balance_status dok_presentation_bind(const dok_level_definition *effective,
    const dok_level_definition *localized,const dok_asset_session *session,
    dok_level_presentation *candidate,dok_level_presentation *out)
{
    dok_asset_catalog catalog;dok_balance_status status;uint32_t i,j,count;
    if(effective==NULL || localized==NULL || session==NULL || candidate==NULL || out==NULL)
        return DOK_BALANCE_INVALID_ARGUMENT;
    if(!disjoint(candidate,sizeof(*candidate),out,sizeof(*out)) ||
       !disjoint(candidate,sizeof(*candidate),effective,sizeof(*effective)) ||
       !disjoint(candidate,sizeof(*candidate),localized,sizeof(*localized)) ||
       !disjoint(out,sizeof(*out),effective,sizeof(*effective)) ||
       !disjoint(out,sizeof(*out),localized,sizeof(*localized)) ||
       !disjoint(candidate,sizeof(*candidate),localized->source,localized->source_bytes) ||
       !disjoint(out,sizeof(*out),localized->source,localized->source_bytes) ||
       !disjoint(candidate,sizeof(*candidate),effective->source,effective->source_bytes) ||
       !disjoint(out,sizeof(*out),effective->source,effective->source_bytes) ||
       !disjoint(candidate,sizeof(*candidate),session,sizeof(*session)) ||
       !disjoint(out,sizeof(*out),session,sizeof(*session))) return DOK_BALANCE_INVALID_ARGUMENT;
    if(effective->language!=1u || localized->source_index!=effective->source_index)
        return DOK_BALANCE_WRONG_SAMPLE;
    status=validate_sample(effective,1u);if(status!=DOK_BALANCE_OK) return status;
    status=validate_sample(localized,0u);if(status!=DOK_BALANCE_OK) return status;
    if(dok_assets_catalog(session,&catalog)!=DOK_ASSET_OK ||
       !hash_matches(session->_source_sha256[0],archive_hash[0]) ||
       !hash_matches(session->_source_sha256[1],archive_hash[1])) return DOK_BALANCE_WRONG_SAMPLE;
    memset(candidate,0,sizeof(*candidate));candidate->effective=effective;
    candidate->localized=localized;candidate->session=session;candidate->language=localized->language;
    count=dok_assets_count(session);
    for(i=0u;i<count;++i) {
        dok_asset_entry entry,primary;uint16_t representation;dok_asset_id id;
        if(dok_assets_entry_at(session,i,&entry)!=DOK_ASSET_OK) return DOK_BALANCE_RESOURCE_ERROR;
        if(entry.representation!=DOK_ASSET_RAW_SOURCE || entry.language!=localized->language) continue;
        if(candidate->resource_count>=DOK_PRESENTATION_MAX_RESOURCES) return DOK_BALANCE_RESOURCE_ERROR;
        representation=primary_representation(entry.source_kind);
        id=(uint32_t)entry.language<<30 | (uint32_t)entry.source_index<<18 | (uint32_t)representation<<10;
        if(representation==0u || dok_assets_find(session,id,&primary)!=DOK_ASSET_OK ||
           primary.source_kind!=entry.source_kind || primary.language!=entry.language ||
           memcmp(primary.name_cp866,entry.name_cp866,16u)!=0) return DOK_BALANCE_RESOURCE_ERROR;
        candidate->resources[candidate->resource_count].id=id;
        candidate->resources[candidate->resource_count].kind=entry.source_kind;
        memcpy(candidate->resources[candidate->resource_count].name_cp866,entry.name_cp866,16u);
        ++candidate->resource_count;
    }
    if(candidate->resource_count!=DOK_PRESENTATION_MAX_RESOURCES ||
       effective->object_count!=localized->object_count) return DOK_BALANCE_RESOURCE_ERROR;
    candidate->tag=0x4E355052u;
    {
        dok_presentation_value value;
        if(dok_presentation_get(candidate,0u,DOK_PRESENT_PALETTE,&value)!=DOK_BALANCE_OK ||
           dok_presentation_get(candidate,0u,DOK_PRESENT_LIGHT,&value)!=DOK_BALANCE_OK)
            return DOK_BALANCE_RESOURCE_ERROR;
    }
    for(i=0u;i<effective->object_count;++i) {
        const dok_level_object *a=&effective->objects[i],*b;
        uint16_t index=localized->glyph_index[a->glyph];dok_presentation_value value;
        if(index==0u || index>localized->object_count) return DOK_BALANCE_BAD_MODEL;
        b=&localized->objects[index-1u];
        if(a->class_id!=b->class_id || a->field_count!=b->field_count) return DOK_BALANCE_BAD_MODEL;
        if(dok_presentation_get(candidate,a->glyph,DOK_PRESENT_SPRITE,&value)!=DOK_BALANCE_OK ||
           dok_presentation_get(candidate,a->glyph,DOK_PRESENT_ANIMATION,&value)!=DOK_BALANCE_OK ||
           dok_presentation_get(candidate,a->glyph,DOK_PRESENT_SOUND,&value)!=DOK_BALANCE_OK)
            return DOK_BALANCE_RESOURCE_ERROR;
        for(j=0u;j<a->field_count;++j) {
            const dok_level_field *f=&a->fields[j],*g=dok_level_field_at(b,f->native_offset);
            if(g==NULL || f->kind!=g->kind) return DOK_BALANCE_BAD_MODEL;
            if(f->kind>=DOK_LEVEL_IMAGE &&
               dok_presentation_get(candidate,a->glyph,f->native_offset,&value)!=DOK_BALANCE_OK)
                return DOK_BALANCE_RESOURCE_ERROR;
        }
    }
    *out=*candidate;return DOK_BALANCE_OK;
}
