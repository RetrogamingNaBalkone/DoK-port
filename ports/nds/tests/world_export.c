#include "world_export.h"
#include "dok/balance.h"
#include <stddef.h>
#include <string.h>

typedef struct Writer { dok_test_emit_fn emit; void *context; int ok; } Writer;
static void bytes(Writer *w,const char *p,uint32_t n)
{if(w->ok && !w->emit(w->context,p,n)) w->ok=0;}
static void text(Writer *w,const char *p)
{bytes(w,p,(uint32_t)strlen(p));}
static void unsigned_number(Writer *w,uint32_t n)
{
    char digits[10];uint32_t count=0,i;
    do {digits[count++]=(char)('0'+n%10u);n/=10u;} while(n!=0u);
    for(i=count;i>0u;--i) bytes(w,digits+i-1u,1u);
}
static void signed_number(Writer *w,int32_t n)
{if(n<0) {text(w,"-");unsigned_number(w,0u-(uint32_t)n);} else unsigned_number(w,(uint32_t)n);}
static void hex(Writer *w,const unsigned char *p,uint32_t count)
{
    static const char alphabet[]="0123456789abcdef";
    char block[64];uint32_t i,n,j;
    text(w,"\"");
    for(i=0;i<count;i+=n) {
        n=count-i;if(n>32u) n=32u;
        for(j=0;j<n;++j) {block[j*2u]=alphabet[p[i+j]>>4];block[j*2u+1u]=alphabet[p[i+j]&15u];}
        bytes(w,block,n*2u);
    }
    text(w,"\"");
}
static void span(Writer *w,dok_level_span s)
{text(w,"[");unsigned_number(w,s.offset);text(w,",");unsigned_number(w,s.length);text(w,"]");}
static void key(Writer *w,const char *name)
{text(w,"\"");text(w,name);text(w,"\":");}
static void reference(Writer *w,dok_object_ref r)
{text(w,"[");unsigned_number(w,r.world_epoch);text(w,",");unsigned_number(w,r.object_id);text(w,",");unsigned_number(w,r.generation);text(w,"]");}
static void level(Writer *w,const dok_level_definition *l,int include_source)
{
    uint32_t i,j,cells=(uint32_t)l->settings[DOK_LEVEL_WIDTH]*(uint32_t)l->settings[DOK_LEVEL_LENGTH];
    text(w,"{");key(w,"source_bytes");unsigned_number(w,l->source_bytes);
    text(w,",");key(w,"source_asset_id");unsigned_number(w,l->source_asset_id);
    text(w,",");key(w,"source_index");unsigned_number(w,l->source_index);
    text(w,",");key(w,"language");unsigned_number(w,l->language);
    text(w,",");key(w,"source_sha256");hex(w,l->source_sha256,32u);
    if(include_source) {text(w,",");key(w,"source_hex");hex(w,l->source,l->source_bytes);}
    text(w,",");key(w,"settings");text(w,"[");
    for(i=0;i<8u;++i) {if(i) text(w,",");signed_number(w,l->settings[i]);}
    text(w,"],");key(w,"palette");span(w,l->palette);text(w,",");key(w,"light");span(w,l->light);
    text(w,",");key(w,"ignored_step_ver");unsigned_number(w,l->ignored_step_ver);
    text(w,",");key(w,"truncated_messages");unsigned_number(w,l->truncated_messages);
    text(w,",");key(w,"object_count");unsigned_number(w,l->object_count);
    text(w,",");key(w,"glyph_index");text(w,"[");
    for(i=0;i<256u;++i) {if(i) text(w,",");unsigned_number(w,l->glyph_index[i]);}
    text(w,"],");key(w,"objects");text(w,"[");
    for(i=0;i<l->object_count;++i) {
        const dok_level_object *o=l->objects+i;
        if(i) {text(w,",");}
        text(w,"{");key(w,"class_id");unsigned_number(w,o->class_id);
        text(w,",");key(w,"flags");unsigned_number(w,o->flags);
        text(w,",");key(w,"width_parameter");signed_number(w,o->width_parameter);
        text(w,",");key(w,"height_parameter");signed_number(w,o->height_parameter);
        text(w,",");key(w,"sprite_flags");signed_number(w,o->sprite_flags);
        text(w,",");key(w,"glyph");unsigned_number(w,o->glyph);
        text(w,",");key(w,"marker");unsigned_number(w,o->marker);
        text(w,",");key(w,"sprite");span(w,o->sprite);
        text(w,",");key(w,"animation");span(w,o->animation);
        text(w,",");key(w,"sound");span(w,o->sound);
        text(w,",");key(w,"raw_line");span(w,o->raw_line);
        text(w,",");key(w,"raw_tail");span(w,o->raw_tail);
        text(w,",");key(w,"fields");text(w,"[");
        for(j=0;j<o->field_count;++j) {
            const dok_level_field *f=o->fields+j;
            if(j) {text(w,",");}
            text(w,"[");unsigned_number(w,f->native_offset);
            text(w,",");unsigned_number(w,f->kind);text(w,",");signed_number(w,f->value);
            text(w,",");unsigned_number(w,f->token.offset);text(w,",");unsigned_number(w,f->token.length);text(w,"]");
        }
        text(w,"]}");
    }
    text(w,"],");key(w,"maps");text(w,"[");
    for(i=0;i<3u;++i) {if(i) text(w,",");hex(w,l->maps[i],cells);}
    text(w,"]}");
}
static void world(Writer *w,const dok_initial_world *s)
{
    uint32_t i,cells=(uint32_t)s->definition->settings[DOK_LEVEL_WIDTH]*(uint32_t)s->definition->settings[DOK_LEVEL_LENGTH];
    text(w,"{");key(w,"epoch");unsigned_number(w,s->epoch);
    text(w,",");key(w,"hero");reference(w,s->hero);
    text(w,",");key(w,"object_count");unsigned_number(w,s->object_count);
    text(w,",");key(w,"actor_placements");unsigned_number(w,s->actor_placements);
    text(w,",");key(w,"placed_actors");unsigned_number(w,s->placed_actors);
    text(w,",");key(w,"category99_total");signed_number(w,s->category99_total);
    text(w,",");key(w,"glyph_index");text(w,"[");
    for(i=0;i<256u;++i) {if(i) text(w,",");unsigned_number(w,s->glyph_index[i]);}
    text(w,"],");key(w,"objects");text(w,"[");
    for(i=0;i<s->object_count;++i) {
        const dok_world_object_seed *o=s->objects+i;
        if(i) {text(w,",");}
        text(w,"{");key(w,"reference");reference(w,o->reference);
        text(w,",");key(w,"definition_index");unsigned_number(w,o->definition_index);
        text(w,",");key(w,"class_id");unsigned_number(w,o->class_id);
        text(w,",");key(w,"placements");unsigned_number(w,o->placements);
        text(w,",");key(w,"pose_known");unsigned_number(w,o->pose_known);
        text(w,",");key(w,"x");signed_number(w,o->x);text(w,",");key(w,"y");signed_number(w,o->y);
        text(w,",");key(w,"z");signed_number(w,o->z);text(w,",");key(w,"heading");signed_number(w,o->heading);
        text(w,",");key(w,"upper_y");signed_number(w,o->upper_y);text(w,",");key(w,"lower_y");signed_number(w,o->lower_y);
        text(w,"}");
    }
    text(w,"],");key(w,"maps");text(w,"[");
    for(i=0;i<4u;++i) {if(i) text(w,",");hex(w,s->maps[i],cells);}
    text(w,"]}");
}
static void binding(Writer *w,const dok_level_presentation *p,uint8_t glyph,uint16_t slot,uint32_t *count)
{
    dok_presentation_value value;
    if(dok_presentation_get(p,glyph,slot,&value)!=DOK_BALANCE_OK) {w->ok=0;return;}
    if((*count)++!=0u) text(w,",");
    text(w,"[");unsigned_number(w,glyph);text(w,",");unsigned_number(w,slot);
    text(w,",");unsigned_number(w,value.kind);text(w,",");unsigned_number(w,value.asset_id);
    text(w,",");hex(w,value.text,value.text_bytes);text(w,"]");
}
static void presentation(Writer *w,const dok_level_presentation *p)
{
    uint32_t i,j,count=0;
    text(w,"{");key(w,"language");unsigned_number(w,p->language);
    text(w,",");key(w,"resource_count");unsigned_number(w,p->resource_count);
    text(w,",");key(w,"resources");text(w,"[");
    for(i=0;i<p->resource_count;++i) {
        const dok_presentation_resource *r=p->resources+i;
        if(i) {text(w,",");}
        text(w,"[");unsigned_number(w,r->id);text(w,",");unsigned_number(w,r->kind);
        text(w,",");hex(w,r->name_cp866,16u);text(w,"]");
    }
    text(w,"],");key(w,"bindings");text(w,"[");
    binding(w,p,0u,DOK_PRESENT_PALETTE,&count);binding(w,p,0u,DOK_PRESENT_LIGHT,&count);
    for(i=0;i<p->effective->object_count;++i) {
        const dok_level_object *o=p->effective->objects+i;
        binding(w,p,o->glyph,DOK_PRESENT_SPRITE,&count);
        binding(w,p,o->glyph,DOK_PRESENT_ANIMATION,&count);
        binding(w,p,o->glyph,DOK_PRESENT_SOUND,&count);
        for(j=0;j<o->field_count;++j)
            if(o->fields[j].kind>=DOK_LEVEL_IMAGE)
                binding(w,p,o->glyph,o->fields[j].native_offset,&count);
    }
    text(w,"]}");
}
int dok_test_world_export(const dok_level_definition *base,
    const dok_level_definition *effective,const dok_initial_world *initial,
    const struct dok_level_presentation *view,dok_test_emit_fn emit,void *context)
{
    Writer w;
    if(base==NULL || effective==NULL || initial==NULL || view==NULL || emit==NULL ||
       initial->definition!=effective || view->effective!=effective || view->localized==NULL) return 0;
    w.emit=emit;w.context=context;w.ok=1;
    text(&w,"{");key(&w,"base");level(&w,base,1);
    text(&w,",");key(&w,"effective");level(&w,effective,0);
    text(&w,",");key(&w,"localized");level(&w,view->localized,view->language!=1u);
    text(&w,",");key(&w,"world");world(&w,initial);
    text(&w,",");key(&w,"presentation");presentation(&w,view);
    text(&w,"}\n");return w.ok;
}
