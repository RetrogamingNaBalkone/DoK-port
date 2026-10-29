#include "dok/world.h"
#include "dok/sha256.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct cursor {
    const unsigned char *bytes;
    uint32_t at, end;
} cursor;
typedef struct field_schema { uint16_t offset; uint8_t kind; } field_schema;
#define W(o) {o,DOK_LEVEL_WORD}
#define L(o) {o,DOK_LEVEL_LONG}
#define C(o) {o,DOK_LEVEL_CHARACTER}
#define I(o) {o,DOK_LEVEL_IMAGE}
#define A(o) {o,DOK_LEVEL_ANIMATION}
#define S(o) {o,DOK_LEVEL_SOUND}
#define T(o) {o,DOK_LEVEL_TEXT}
static const field_schema class0[]={W(0x54),I(0x56),I(0x5a),I(0x5e),I(0x62)};
static const field_schema class1[]={L(0x38),W(0x32),W(0x34),
 W(0x5e),W(0x60),W(0x62),W(0x64),W(0x66),W(0x68),W(0x6a),W(0x6c),
 W(0x6e),W(0x70),W(0x72),S(0xa2),S(0xa6),W(0x76),W(0x78),W(0x7a)};
static const field_schema class3[]={W(0x54),W(0x56),T(0x58)};
static const field_schema class5[]={L(0x38),W(0x32),W(0x34)};
static const field_schema class7[]={W(0x5c),W(0x5e),L(0x66)};
static const field_schema class8[]={L(0x3c),W(0x5a),A(0x77),W(0x5c),A(0x7b)};
static const field_schema class9[]={L(0x38),W(0x32),W(0x34),A(0xe8),A(0xec),
 S(0xf0),S(0xf4),W(0xa4),L(0xba),C(0xe7),W(0xa6),W(0xa8),W(0xae),T(0xbe)};
static const field_schema class10[]={W(0x81),W(0x54),W(0x56),T(0x58)};
static const field_schema class12[]={W(0x54),I(0x56),I(0x5a),I(0x5e),I(0x62),W(0x66),W(0x68)};
static const field_schema class13[]={W(0x54),I(0x56),I(0x5a),I(0x5e),I(0x62),C(0x66),L(0x67)};
#undef W
#undef L
#undef C
#undef I
#undef A
#undef S
#undef T

static uint32_t separated(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y) return 0;
    return x+an<=y || y+bn<=x;
}
static uint32_t white(unsigned char c)
{ return c==' ' || (c>=9 && c<=13); }
static void skip(cursor *c)
{ while(c->at<c->end && white(c->bytes[c->at])) ++c->at; }
static dok_level_span token(cursor *c)
{
    dok_level_span s;
    skip(c);s.offset=c->at;
    while(c->at<c->end && !white(c->bytes[c->at])) ++c->at;
    s.length=c->at-s.offset;return s;
}
static uint32_t number(cursor *c,int32_t low,int32_t high,int32_t *out,
                       dok_level_span *raw)
{
    dok_level_span s=token(c);
    uint32_t i=0,n=0,limit;
    uint32_t negative=0;
    int32_t value;
    if(!s.length) return 0;
    if(c->bytes[s.offset]=='-' || c->bytes[s.offset]=='+') {
        negative=c->bytes[s.offset]=='-';++i;
    }
    if(i==s.length || (negative && low>=0)) return 0;
    limit=negative ? (uint32_t)(-(low+1))+1u : (uint32_t)high;
    for(;i<s.length;++i) {
        unsigned char d=c->bytes[s.offset+i];
        if(d<'0' || d>'9' || (uint32_t)(d-'0')>limit ||
           n>(limit-(uint32_t)(d-'0'))/10u) return 0;
        n=n*10u+(uint32_t)(d-'0');
    }
    value=negative ? (n==UINT32_C(2147483648) ? INT32_MIN : -(int32_t)n) : (int32_t)n;
    if(value<low || value>high) return 0;
    *out=value;if(raw) *raw=s;return 1;
}
static unsigned char lower(unsigned char c)
{ return c>='A' && c<='Z' ? (unsigned char)(c+('a'-'A')) : c; }
static uint32_t prefix(const unsigned char *b,uint32_t n,const char *key)
{
    uint32_t i;
    for(i=0;key[i];++i) if(i>=n || lower(b[i])!=lower((unsigned char)key[i])) return 0;
    return 1;
}
static uint32_t resource_span(dok_level_span s)
{ return s.length<=15u; }
static uint16_t read16(const unsigned char *b)
{ return (uint16_t)((uint16_t)b[0] | (uint16_t)((uint16_t)b[1]<<8)); }
static uint32_t read32(const unsigned char *b)
{ return (uint32_t)read16(b) | ((uint32_t)read16(b+2)<<16); }
static int16_t signed16(uint16_t x)
{ return x<=INT16_MAX ? (int16_t)x : (int16_t)((int32_t)x-65536); }

static uint32_t schema(uint16_t cls,const field_schema **out,uint16_t *count)
{
#define CASE(k) case k:*out=class##k;*count=(uint16_t)(sizeof(class##k)/sizeof(class##k[0]));return 1
    switch(cls) {
    CASE(0);CASE(1);CASE(3);CASE(5);CASE(7);CASE(8);CASE(9);CASE(10);CASE(12);CASE(13);
    case 2:case 4:case 6:*out=NULL;*count=0;return 1;
    default:return 0;
    }
#undef CASE
}
static dok_world_status object_line(dok_level_definition *level,cursor c,
    dok_level_span line,dok_level_span sprite,int16_t sprite_flags,
    dok_level_span animation,dok_level_span sound)
{
    dok_level_object *o;
    const field_schema *fields;
    int32_t value;
    uint16_t i;
    if(level->object_count==DOK_LEVEL_MAX_OBJECTS) return DOK_WORLD_LIMIT;
    o=&level->objects[level->object_count];
    skip(&c);if(c.at==c.end) return DOK_WORLD_MALFORMED;
    o->glyph=c.bytes[c.at++];if(o->glyph=='_') o->glyph=' ';
    if(level->glyph_index[o->glyph]) return DOK_WORLD_DUPLICATE;
    if(!number(&c,0,65535,&value,NULL)) return DOK_WORLD_MALFORMED;
    o->class_id=(uint16_t)value;
    if(!schema(o->class_id,&fields,&o->field_count)) return DOK_WORLD_UNSUPPORTED;
    if(!number(&c,0,65535,&value,NULL)) return DOK_WORLD_MALFORMED;
    o->flags=(uint16_t)value;
    if(!number(&c,INT16_MIN,INT16_MAX,&value,NULL)) return DOK_WORLD_MALFORMED;
    o->width_parameter=(int16_t)value;
    if(!number(&c,INT16_MIN,INT16_MAX,&value,NULL)) return DOK_WORLD_MALFORMED;
    o->height_parameter=(int16_t)value;
    if(!number(&c,INT16_MIN,INT16_MAX,&value,NULL)) return DOK_WORLD_MALFORMED;
    o->marker=(uint8_t)(uint16_t)value;
    o->sprite=sprite;o->sprite_flags=sprite_flags;o->animation=animation;o->sound=sound;
    o->raw_line=line;o->raw_tail.offset=c.at;o->raw_tail.length=c.end-c.at;
    for(i=0;i<o->field_count;++i) {
        dok_level_field *f=&o->fields[i];
        f->native_offset=fields[i].offset;f->kind=fields[i].kind;
        if(f->kind==DOK_LEVEL_WORD || f->kind==DOK_LEVEL_LONG) {
            int32_t low=f->kind==DOK_LEVEL_WORD ? INT16_MIN : INT32_MIN;
            int32_t high=f->kind==DOK_LEVEL_WORD ? INT16_MAX : INT32_MAX;
            if(!number(&c,low,high,&f->value,&f->token)) return DOK_WORLD_MALFORMED;
        } else if(f->kind==DOK_LEVEL_CHARACTER) {
            skip(&c);if(c.at==c.end) return DOK_WORLD_MALFORMED;
            f->token.offset=c.at;f->token.length=1;f->value=c.bytes[c.at++];
        } else if(f->kind==DOK_LEVEL_TEXT) {
            f->token.offset=c.at;f->token.length=c.end-c.at;c.at=c.end;
            if(f->token.length>39u) ++level->truncated_messages;
        } else {
            f->token=token(&c);
            if(!f->token.length || !resource_span(f->token)) return DOK_WORLD_MALFORMED;
        }
    }
    skip(&c);if(c.at!=c.end) return DOK_WORLD_MALFORMED;
    level->glyph_index[o->glyph]=(uint16_t)(level->object_count+1u);
    ++level->object_count;return DOK_WORLD_OK;
}

dok_world_status dok_level_parse(const dok_asset_view *view,
    dok_level_definition *candidate,dok_level_definition *out)
{
    static const char *const sections[]={"Game Ini","Palette","Light","Game Object","TMap","MMap","FCMap"};
    static const char *const keys[]={"Level Width","Level Height","Level Length","Cell Side","Cell Height","Floor Color","Ceiling Color","Min Dis"};
    dok_level_span sprite={0,0},animation={0,0},sound={0,0};
    uint32_t at=0,section=7,section_mask=0,settings_mask=0,map_rows[3]={0,0,0},i;
    uint32_t palette_seen=0,light_seen=0;
    int16_t sprite_flags=0;
    dok_sha256 hash;
    if(!view || !candidate || !out || !view->data || !view->entry.bytes ||
       view->entry.representation!=DOK_ASSET_LEVEL_CP866 ||
       (view->entry.language!=1u && view->entry.language!=2u) ||
       !separated(candidate,sizeof(*candidate),out,sizeof(*out)) ||
       !separated(view,sizeof(*view),candidate,sizeof(*candidate)) ||
       !separated(view,sizeof(*view),out,sizeof(*out)) ||
       !separated(view->data,view->entry.bytes,candidate,sizeof(*candidate)) ||
       !separated(view->data,view->entry.bytes,out,sizeof(*out))) return DOK_WORLD_INVALID_ARGUMENT;
    if(view->entry.bytes>65536u) return DOK_WORLD_LIMIT;
    for(i=0;i<view->entry.bytes;++i) if(!view->data[i]) return DOK_WORLD_MALFORMED;
    memset(candidate,0,sizeof(*candidate));candidate->source=view->data;
    candidate->source_bytes=view->entry.bytes;candidate->source_asset_id=view->entry.id;
    candidate->source_index=view->entry.source_index;candidate->language=view->entry.language;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,view->data,view->entry.bytes);
    dok_sha256_end(&hash,candidate->source_sha256);
    while(at<view->entry.bytes) {
        dok_level_span line;
        cursor c;
        uint32_t end,eq;
        line.offset=at;
        while(at<view->entry.bytes && view->data[at]!='\r' && view->data[at]!='\n') ++at;
        line.length=at-line.offset;end=at;
        if(at<view->entry.bytes) {
            unsigned char newline=view->data[at++];
            if(newline=='\r' && at<view->entry.bytes && view->data[at]=='\n') ++at;
        }
        if(line.length>DOK_LEVEL_MAX_LINE) return DOK_WORLD_LIMIT;
        if(!line.length || view->data[line.offset]=='#') continue;
        c.bytes=view->data;c.at=line.offset;c.end=end;
        if(view->data[line.offset]=='[') {
            uint32_t close=line.offset+1u;
            while(close<end && view->data[close]!=']') ++close;
            if(close==end) return DOK_WORLD_MALFORMED;
            for(i=0;i<7u;++i) if(prefix(view->data+line.offset+1u,close-line.offset-1u,sections[i])) break;
            if(i==7u) return DOK_WORLD_UNSUPPORTED;
            if(section_mask&(1u<<i)) return DOK_WORLD_DUPLICATE;
            section=i;section_mask|=1u<<i;continue;
        }
        if(section>=7u) return DOK_WORLD_MALFORMED;
        if(section>=4u) {
            uint32_t plane=section-4u,width=(uint16_t)candidate->settings[DOK_LEVEL_WIDTH];
            uint32_t length=(uint16_t)candidate->settings[DOK_LEVEL_LENGTH];
            if(settings_mask!=255u || !width || width>64u || !length || length>64u ||
               candidate->settings[DOK_LEVEL_HEIGHT]!=1) return DOK_WORLD_UNSUPPORTED;
            if(line.length!=width || map_rows[plane]>=length) return DOK_WORLD_MALFORMED;
            memcpy(candidate->maps[plane]+map_rows[plane]*width,view->data+line.offset,width);
            ++map_rows[plane];continue;
        }
        for(eq=line.offset;eq<end && view->data[eq]!='=';++eq) { }
        if(section==3u && !(prefix(view->data+line.offset,line.length,"Sprite") ||
                            prefix(view->data+line.offset,line.length,"Flic") ||
                            prefix(view->data+line.offset,line.length,"Sound"))) {
            dok_world_status status=object_line(candidate,c,line,sprite,sprite_flags,animation,sound);
            if(status!=DOK_WORLD_OK) return status;
            continue;
        }
        if(eq==end) return DOK_WORLD_MALFORMED;
        c.at=eq+1u;while(c.at<end && view->data[c.at]==' ') ++c.at;
        if(section==0u) {
            int32_t value;
            for(i=0;i<8u;++i) if(prefix(view->data+line.offset,eq-line.offset,keys[i])) break;
            if(i==8u) {
                if(prefix(view->data+line.offset,eq-line.offset,"Step Ver")) { ++candidate->ignored_step_ver;continue; }
                return DOK_WORLD_UNSUPPORTED;
            }
            if(section_mask&0x70u) return DOK_WORLD_UNSUPPORTED;
            if(settings_mask&(1u<<i)) return DOK_WORLD_DUPLICATE;
            if(!number(&c,INT16_MIN,INT16_MAX,&value,NULL)) return DOK_WORLD_MALFORMED;
            skip(&c);if(c.at!=end) return DOK_WORLD_MALFORMED;
            candidate->settings[i]=(int16_t)value;settings_mask|=1u<<i;
        } else if(section==1u || section==2u) {
            dok_level_span value={c.at,end-c.at};
            if(!resource_span(value) || !value.length) return DOK_WORLD_MALFORMED;
            if(section==1u) {
                if(!prefix(view->data+line.offset,eq-line.offset,"Palette")) return DOK_WORLD_UNSUPPORTED;
                if(palette_seen++) return DOK_WORLD_DUPLICATE;
                candidate->palette=value;
            } else {
                if(!prefix(view->data+line.offset,eq-line.offset,"Light")) return DOK_WORLD_UNSUPPORTED;
                if(light_seen++) return DOK_WORLD_DUPLICATE;
                candidate->light=value;
            }
        } else if(prefix(view->data+line.offset,eq-line.offset,"Sprite")) {
            int32_t value;
            sprite=token(&c);
            if(!resource_span(sprite) || !sprite.length || !number(&c,INT16_MIN,INT16_MAX,&value,NULL)) return DOK_WORLD_MALFORMED;
            sprite_flags=(int16_t)value;skip(&c);if(c.at!=end) return DOK_WORLD_MALFORMED;
        } else if(prefix(view->data+line.offset,eq-line.offset,"Flic")) {
            animation.offset=c.at;animation.length=end-c.at;
            if(!resource_span(animation)) return DOK_WORLD_MALFORMED;
        } else {
            sound.offset=c.at;sound.length=end-c.at;
            if(!resource_span(sound)) return DOK_WORLD_MALFORMED;
        }
    }
    if(section_mask!=127u || settings_mask!=255u || !palette_seen || !light_seen ||
       !candidate->object_count) return DOK_WORLD_MALFORMED;
    if(candidate->settings[DOK_LEVEL_WIDTH]<1 || candidate->settings[DOK_LEVEL_WIDTH]>64 ||
       candidate->settings[DOK_LEVEL_LENGTH]<1 || candidate->settings[DOK_LEVEL_LENGTH]>64 ||
       candidate->settings[DOK_LEVEL_HEIGHT]!=1 || candidate->settings[DOK_LEVEL_CELL_SIDE]<1 ||
       candidate->settings[DOK_LEVEL_CELL_HEIGHT]<1) return DOK_WORLD_UNSUPPORTED;
    for(i=0;i<3u;++i) if(map_rows[i]!=(uint16_t)candidate->settings[DOK_LEVEL_LENGTH]) return DOK_WORLD_MALFORMED;
    memcpy(out,candidate,sizeof(*out));return DOK_WORLD_OK;
}

const dok_level_field *dok_level_field_at(const dok_level_object *object,uint16_t offset)
{
    uint16_t i;
    if(!object || object->field_count>DOK_LEVEL_MAX_FIELDS) return NULL;
    for(i=0;i<object->field_count;++i) if(object->fields[i].native_offset==offset) return &object->fields[i];
    return NULL;
}
uint32_t dok_level_span_equal(const dok_level_definition *level,dok_level_span s,
                            const unsigned char *bytes,uint32_t count)
{
    return level && level->source && (bytes || !count) && s.length==count &&
        s.offset<=level->source_bytes && s.length<=level->source_bytes-s.offset &&
        (!count || memcmp(level->source+s.offset,bytes,count)==0);
}
dok_world_status dok_level_validate_maps(const dok_level_definition *level,const dok_asset_view *maps)
{
    uint32_t cells,plane;
    static const uint8_t order[3]={DOK_LEVEL_MMAP,DOK_LEVEL_TMAP,DOK_LEVEL_FCMAP};
    if(!level || !maps || !maps->data || maps->entry.representation!=DOK_ASSET_LEVEL_MAPS) return DOK_WORLD_INVALID_ARGUMENT;
    if(level->settings[0]<1 || level->settings[0]>64 || level->settings[2]<1 || level->settings[2]>64) return DOK_WORLD_UNSUPPORTED;
    cells=(uint32_t)level->settings[0]*(uint32_t)level->settings[2];
    if(maps->entry.bytes!=20u+3u*cells || maps->entry.language!=level->language || maps->entry.source_index!=level->source_index ||
       read16(maps->data)!=(uint16_t)level->settings[0] || read16(maps->data+2)!=(uint16_t)level->settings[2] ||
       read16(maps->data+4)!=1u || read16(maps->data+6)!=3u || read32(maps->data+8)!=(uint16_t)level->settings[3] ||
       read32(maps->data+12)!=(uint16_t)level->settings[4] || read32(maps->data+16)!=0u) return DOK_WORLD_MAP_MISMATCH;
    for(plane=0;plane<3u;++plane) if(memcmp(level->maps[order[plane]],maps->data+20u+plane*cells,cells)) return DOK_WORLD_MAP_MISMATCH;
    return DOK_WORLD_OK;
}

dok_world_status dok_initial_world_build(const dok_level_definition *level,dok_world_epoch epoch,
                                        dok_initial_world *candidate,dok_initial_world *out)
{
    uint32_t width,length,cells,plane,row,column,i;
    uint16_t total=0;
    static const uint8_t order[3]={DOK_LEVEL_MMAP,DOK_LEVEL_TMAP,DOK_LEVEL_FCMAP};
    if(!level || !candidate || !out || !epoch || !separated(candidate,sizeof(*candidate),out,sizeof(*out)) ||
       !separated(level,sizeof(*level),candidate,sizeof(*candidate)) ||
       !separated(level,sizeof(*level),out,sizeof(*out))) return DOK_WORLD_INVALID_ARGUMENT;
    if(level->language!=1u || level->settings[0]<1 || level->settings[0]>64 || level->settings[2]!=level->settings[0] ||
       level->settings[1]!=1 || level->settings[3]<1 || level->object_count<1u || level->object_count>256u) return DOK_WORLD_UNSUPPORTED;
    width=(uint16_t)level->settings[0];length=(uint16_t)level->settings[2];cells=width*length;
    memset(candidate,0,sizeof(*candidate));candidate->definition=level;candidate->epoch=epoch;
    candidate->object_count=level->object_count;memcpy(candidate->glyph_index,level->glyph_index,sizeof(candidate->glyph_index));
    for(i=0;i<level->object_count;++i) {
        const dok_level_object *definition=&level->objects[i];
        dok_world_object_seed *seed=&candidate->objects[i];
        if(level->glyph_index[definition->glyph]!=i+1u) return DOK_WORLD_MALFORMED;
        seed->reference.world_epoch=epoch;seed->reference.object_id=i+1u;seed->reference.generation=1;
        seed->definition_index=(uint16_t)i;seed->class_id=definition->class_id;
    }
    for(plane=0;plane<3u;++plane) for(row=0;row<length;++row) for(column=0;column<width;++column) {
        unsigned char glyph=level->maps[order[plane]][(length-1u-row)*width+column];
        uint16_t index;
        if(glyph=='.') glyph=' ';
        index=level->glyph_index[glyph];
        if(!index || index>level->object_count) return DOK_WORLD_MISSING_GLYPH;
        candidate->maps[plane][row*width+column]=glyph;
    }
    for(i=0;i<cells;++i) {
        uint16_t index=candidate->glyph_index[candidate->maps[DOK_WORLD_TMAP][i]];
        const dok_level_object *object=&level->objects[index-1u];
        if(object->class_id==3u) {
            const dok_level_field *kind=dok_level_field_at(object,0x54),*amount=dok_level_field_at(object,0x56);
            if(!kind || !amount) return DOK_WORLD_MALFORMED;
            if(kind->value==99) total=(uint16_t)(total+(uint16_t)amount->value);
        }
        index=candidate->glyph_index[candidate->maps[DOK_WORLD_MMAP][i]];
        object=&level->objects[index-1u];
        if(object->class_id==1u || object->class_id==9u) {
            const dok_level_field *heading=dok_level_field_at(object,0x32);
            dok_world_object_seed *seed=&candidate->objects[index-1u];
            int16_t y=signed16((uint16_t)(0u-(uint16_t)object->height_parameter));
            if(!heading || heading->kind!=DOK_LEVEL_WORD) return DOK_WORLD_MALFORMED;
            if(!seed->placements) ++candidate->placed_actors;
            ++seed->placements;++candidate->actor_placements;seed->pose_known=1;
            seed->x=(int32_t)level->settings[3]*(int32_t)(i%width)+level->settings[3]/2;
            seed->y=y;seed->z=(int32_t)level->settings[3]*(int32_t)(i/width)+level->settings[3]/2;
            seed->heading=(int16_t)heading->value;
            seed->upper_y=object->class_id==1u ? signed16((uint16_t)((uint16_t)y+26u)) : y;
            seed->lower_y=object->class_id==1u ? signed16((uint16_t)((uint16_t)y-26u)) : y;
            if(object->class_id==1u) candidate->hero=seed->reference;
        }
    }
    candidate->category99_total=signed16(total);memcpy(out,candidate,sizeof(*out));return DOK_WORLD_OK;
}
