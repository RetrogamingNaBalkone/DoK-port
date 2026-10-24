#include "dok/level.h"

typedef struct { const uint8_t *s; size_t pos, end; } cursor;
typedef struct { uint8_t kind; uint16_t offset; } spec;
#define W(o) {DOK_LEVEL_I16,o}
#define L(o) {DOK_LEVEL_I32,o}
#define C(o) {DOK_LEVEL_CHAR,o}
#define I(o) {DOK_LEVEL_IMAGE,o}
#define A(o) {DOK_LEVEL_ANIMATION,o}
#define S(o) {DOK_LEVEL_SOUND,o}
#define T(o) {DOK_LEVEL_TEXT,o}
#define SURFACE W(0x54),I(0x56),I(0x5a),I(0x5e),I(0x62)
#define MOTION L(0x38),W(0x32),W(0x34)
static const spec surface[] = {SURFACE};
static const spec player[] = {MOTION,W(0x5e),W(0x60),W(0x62),W(0x64),W(0x66),
    W(0x68),W(0x6a),W(0x6c),W(0x6e),W(0x70),W(0x72),S(0xa2),S(0xa6),
    W(0x76),W(0x78),W(0x7a)};
static const spec pickup[] = {W(0x54),W(0x56),T(0x58)};
static const spec actor[] = {MOTION};
static const spec door[] = {W(0x5c),W(0x5e),L(0x66)};
static const spec projectile[] = {L(0x3c),W(0x5a),A(0x77),W(0x5c),A(0x7b)};
static const spec monster[] = {MOTION,A(0xe8),A(0xec),S(0xf0),S(0xf4),
    W(0xa4),L(0xba),C(0xe7),W(0xa6),W(0xa8),W(0xae),T(0xbe)};
static const spec weapon[] = {W(0x81),W(0x54),W(0x56),T(0x58)};
static const spec effect[] = {SURFACE,W(0x66),W(0x68)};
static const spec trigger[] = {SURFACE,C(0x66),L(0x67)};
static int space(uint8_t c) { return c == 32 || (c >= 9 && c <= 13); }
static void skip(cursor *c) { while(c->pos<c->end && space(c->s[c->pos])) ++c->pos; }
static int prefix(cursor c, const char *s) {
    size_t i=0;
    while(s[i]) {
        uint8_t a,b=(uint8_t)s[i];
        if(c.pos+i>=c.end) return 0;
        a=c.s[c.pos+i];
        if(a>='a' && a<='z') a=(uint8_t)(a-32);
        if(b>='a' && b<='z') b=(uint8_t)(b-32);
        if(a!=b) return 0;
        ++i;
    }
    return 1;
}
static int number(cursor *c, int32_t *out) {
    uint32_t n=0,limit; size_t start; int negative=0;
    skip(c);
    if(c->pos<c->end && (c->s[c->pos]=='-' || c->s[c->pos]=='+'))
        negative=c->s[c->pos++]=='-';
    start=c->pos; limit=negative ? UINT32_C(2147483648) : INT32_MAX;
    while(c->pos<c->end && c->s[c->pos]>='0' && c->s[c->pos]<='9') {
        uint32_t digit=(uint32_t)(c->s[c->pos++]-'0');
        if(n>(limit-digit)/10) return 0;
        n=n*10+digit;
    }
    if(start==c->pos) return 0;
    *out=negative ? (n==UINT32_C(2147483648) ? INT32_MIN : -(int32_t)n) : (int32_t)n;
    return 1;
}
static dok_level_span span(size_t start,size_t end) {
    dok_level_span r; r.offset=(uint32_t)start; r.size=(uint32_t)(end-start); return r;
}
static dok_level_span token(cursor *c) {
    size_t start; skip(c); start=c->pos;
    while(c->pos<c->end && !space(c->s[c->pos])) ++c->pos;
    return span(start,c->pos);
}
static int assignment(cursor *c) {
    while(c->pos<c->end && c->s[c->pos]!='=') ++c->pos;
    if(c->pos==c->end) return 0;
    ++c->pos;
    while(c->pos<c->end && c->s[c->pos]==32) ++c->pos;
    return 1;
}
static const spec *schema(unsigned type, size_t *count) {
    *count=0;
#define SELECT(n,array) case n: *count=sizeof(array)/sizeof(array[0]); return array
    switch(type) {
    SELECT(0,surface); SELECT(1,player); SELECT(3,pickup); SELECT(5,actor);
    SELECT(7,door); SELECT(8,projectile); SELECT(9,monster); SELECT(10,weapon);
    SELECT(12,effect); SELECT(13,trigger);
    default: return 0;
    }
}
static dok_level_status object_read(dok_level *level,cursor c,dok_level_object *o) {
    int32_t n[5]; size_t i,count; const spec *fields;
    skip(&c); if(c.pos==c.end) return DOK_LEVEL_SYNTAX;
    o->glyph=c.s[c.pos++]; if(o->glyph=='_') o->glyph=' ';
    for(i=0;i<5;++i) if(!number(&c,&n[i])) return DOK_LEVEL_SYNTAX;
    if(n[0]<0 || n[0]>13 || n[0]==11 || n[1]<0 || n[1]>65535 ||
       n[2]<-32768 || n[2]>32767 || n[3]<-32768 || n[3]>32767 ||
       n[4]<-32768 || n[4]>32767) return DOK_LEVEL_RANGE;
    if(level->glyph_index[o->glyph]) return DOK_LEVEL_DUPLICATE;
    o->class_id=(uint16_t)n[0]; o->flags=(uint16_t)n[1];
    o->width_parameter=(int16_t)n[2]; o->height_parameter=(int16_t)n[3];
    o->marker=(uint8_t)n[4]; o->raw_tail=span(c.pos,c.end);
    fields=schema(o->class_id,&count); o->field_count=(uint8_t)count;
    for(i=0;i<count;++i) {
        dok_level_field *f=&o->fields[i];
        f->kind=fields[i].kind; f->native_offset=fields[i].offset;
        if(f->kind==DOK_LEVEL_I16 || f->kind==DOK_LEVEL_I32) {
            if(!number(&c,&f->value.number)) return DOK_LEVEL_SYNTAX;
            if(f->kind==DOK_LEVEL_I16 && (f->value.number < -32768 || f->value.number>32767))
                return DOK_LEVEL_RANGE;
        } else if(f->kind==DOK_LEVEL_CHAR) {
            skip(&c); if(c.pos==c.end) return DOK_LEVEL_SYNTAX;
            f->value.number=c.s[c.pos++];
        } else if(f->kind==DOK_LEVEL_TEXT) {
            size_t end=c.end;
            if(end-c.pos>39) { end=c.pos+39; ++level->truncated_messages; }
            f->value.bytes=span(c.pos,end); c.pos=c.end;
        } else {
            f->value.bytes=token(&c);
            if(!f->value.bytes.size || f->value.bytes.size>15) return DOK_LEVEL_SYNTAX;
        }
    }
    skip(&c);
    return c.pos==c.end ? DOK_LEVEL_OK : DOK_LEVEL_SYNTAX;
}

dok_level_status dok_level_parse(dok_level *out,const void *source,size_t size,
    dok_level_object *objects,size_t capacity,uint8_t *maps,size_t map_capacity,
    dok_level_error *error) {
    static const char *sections[]={"Game Ini","Palette","Light","Game Object","TMap","MMap","FCMap"};
    static const char *keys[]={"Level Width","Level Height","Level Length","Cell Side",
        "Cell Height","Floor Color","Ceiling Color","Min Dis"};
    dok_level_span sprite={0,0},animation={0,0},sound={0,0};
    uint16_t sprite_flags=0; unsigned seen=0,settings=0,section=7;
    size_t pos=0,i,rows[3]={0,0,0}; uint32_t line=0;
    dok_level_status status=DOK_LEVEL_OK;
    if(error) {error->line=0;error->status=DOK_LEVEL_OK;}
    if(!out || !source || !objects || !maps || size>UINT32_MAX) {
        status=DOK_LEVEL_ARGUMENT; goto done;
    }
    out->source=source; out->source_size=size; out->objects=objects; out->maps=maps;
    out->object_count=0; out->ignored_step_ver=0; out->truncated_messages=0;
    out->palette=span(0,0); out->light=span(0,0);
    for(i=0;i<256;++i) out->glyph_index[i]=0;
    for(i=0;i<DOK_LEVEL_SETTING_COUNT;++i) out->settings[i]=0;
    while(pos<size) {
        cursor c; size_t start=pos; int32_t value;
        ++line;
        while(pos<size && out->source[pos]!=10 && out->source[pos]!=13) {
            if(out->source[pos]==0) {status=DOK_LEVEL_SYNTAX;goto done;}
            ++pos;
        }
        c.s=source;c.pos=start;c.end=pos;
        if(pos<size) {uint8_t eol=out->source[pos++];
            if(eol==13 && pos<size && out->source[pos]==10) ++pos;}
        if(c.pos==c.end || c.s[c.pos]=='#') continue;
        if(c.end-c.pos>178) {status=DOK_LEVEL_RANGE;goto done;}
        if(c.s[c.pos]=='[') {
            ++c.pos;
            if(c.end<=c.pos || c.s[c.end-1]!=']') {status=DOK_LEVEL_SYNTAX;goto done;}
            --c.end;
            for(i=0;i<7 && !prefix(c,sections[i]);++i) {}
            if(i==7) {status=DOK_LEVEL_SYNTAX;goto done;}
            section=(unsigned)i; seen|=1u<<section; continue;
        }
        if(section==0) {
            for(i=0;i<8 && !prefix(c,keys[i]);++i) {}
            if(i==8 && prefix(c,"Step Ver")) {++out->ignored_step_ver;continue;}
            if(i==8 || !assignment(&c) || !number(&c,&value)) {status=DOK_LEVEL_SYNTAX;goto done;}
            skip(&c);
            if(c.pos!=c.end || value<0 || value>65535) {status=DOK_LEVEL_RANGE;goto done;}
            if(i<=DOK_LEVEL_LENGTH && (rows[0] || rows[1] || rows[2]) &&
               out->settings[i]!=(uint16_t)value) {status=DOK_LEVEL_RANGE;goto done;}
            out->settings[i]=(uint16_t)value; settings|=1u<<i;
        } else if(section==1 || section==2) {
            dok_level_span name;
            if(!prefix(c,section==1?"Palette":"Light") || !assignment(&c)) {status=DOK_LEVEL_SYNTAX;goto done;}
            name=span(c.pos,c.end);
            if(!name.size || name.size>15) {status=DOK_LEVEL_RANGE;goto done;}
            if(section==1) out->palette=name; else out->light=name;
        } else if(section==3) {
            if(prefix(c,"Sprite")) {
                if(!assignment(&c)) {status=DOK_LEVEL_SYNTAX;goto done;}
                sprite=token(&c);
                if(!sprite.size || sprite.size>15 || !number(&c,&value) || value<0 || value>32767) {
                    status=DOK_LEVEL_SYNTAX;goto done;}
                sprite_flags=(uint16_t)value;skip(&c);
                if(c.pos!=c.end) {status=DOK_LEVEL_SYNTAX;goto done;}
            } else if(prefix(c,"Flic") || prefix(c,"Sound")) {
                int is_sound=prefix(c,"Sound"); dok_level_span name;
                if(!assignment(&c)) {status=DOK_LEVEL_SYNTAX;goto done;}
                name=span(c.pos,c.end);
                if(name.size>15 || (!is_sound && !name.size)) {status=DOK_LEVEL_RANGE;goto done;}
                if(is_sound) sound=name; else animation=name;
            } else {
                dok_level_object *o;
                if(out->object_count>=capacity || out->object_count>=256) {status=DOK_LEVEL_CAPACITY;goto done;}
                o=&objects[out->object_count];
                o->sprite=sprite;o->animation=animation;o->sound=sound;
                o->sprite_flags=sprite_flags;o->line=line;
                status=object_read(out,c,o);if(status!=DOK_LEVEL_OK) goto done;
                out->glyph_index[o->glyph]=(uint16_t)(++out->object_count);
            }
        } else if(section>=4 && section<=6) {
            size_t width=out->settings[DOK_LEVEL_WIDTH], length=out->settings[DOK_LEVEL_LENGTH];
            size_t plane=section-4, cells=width*length;
            if(settings!=255 || !width || width>64 || !length || length>64 ||
               out->settings[DOK_LEVEL_HEIGHT]!=1) {status=DOK_LEVEL_RANGE;goto done;}
            if(cells>map_capacity/3) {status=DOK_LEVEL_CAPACITY;goto done;}
            if(c.end-c.pos!=width || rows[plane]>=length) {status=DOK_LEVEL_SYNTAX;goto done;}
            for(i=0;i<width;++i) maps[plane*cells+rows[plane]*width+i]=c.s[c.pos+i];
            ++rows[plane];
        } else {status=DOK_LEVEL_SYNTAX;goto done;}
    }
    if(settings==255 && (!out->settings[DOK_LEVEL_WIDTH] || out->settings[DOK_LEVEL_WIDTH]>64 ||
       !out->settings[DOK_LEVEL_LENGTH] || out->settings[DOK_LEVEL_LENGTH]>64 ||
       out->settings[DOK_LEVEL_HEIGHT]!=1)) {status=DOK_LEVEL_RANGE;goto done;}
    if(seen!=127 || settings!=255 || !out->palette.size || !out->light.size ||
       !out->object_count || rows[0]!=out->settings[DOK_LEVEL_LENGTH] ||
       rows[1]!=rows[0] || rows[2]!=rows[0]) status=DOK_LEVEL_INCOMPLETE;
done:
    if(error) {error->line=line;error->status=status;}
    return status;
}
