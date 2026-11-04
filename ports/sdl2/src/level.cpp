#include "dok/level.hpp"
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace dok {
namespace {
using K=FieldKind;
struct Spec {K kind;int offset;};
const std::vector<Spec> surface={{K::word,0x54},{K::image,0x56},{K::image,0x5a},{K::image,0x5e},{K::image,0x62}};
const std::vector<Spec> motion={{K::long_integer,0x38},{K::word,0x32},{K::word,0x34}};
std::vector<Spec> schema(unsigned cls,DatasetId dataset) {
    switch(cls) {
        case 0:return surface;
        case 1:{auto s=motion;for(int at=0x5e;at<=0x72;at+=2)s.push_back({K::word,at});
            s.insert(s.end(),{{K::sound,0xa2},{K::sound,0xa6},{K::word,0x76},{K::word,0x78},{K::word,0x7a}});return s;}
        case 2:case 4:case 6:return {};
        case 3:return {{K::word,0x54},{K::word,0x56},{K::text,0x58}};
        case 5:return motion;
        case 7:return {{K::word,0x5c},{K::word,0x5e},{K::long_integer,0x66}};
        case 8:
            if(dataset==DatasetId::old_gold)return {{K::long_integer,0x3c},{K::word,0x58},{K::word,0x5a}};
            return {{K::long_integer,0x3c},{K::word,0x5a},{K::animation,0x77},{K::word,0x5c},{K::animation,0x7b}};
        case 9:{auto s=motion;s.insert(s.end(),{{K::animation,0xe8},{K::animation,0xec},{K::sound,0xf0},{K::sound,0xf4},
            {K::word,0xa4},{K::long_integer,0xba},{K::character,0xe7},{K::word,0xa6},{K::word,0xa8},{K::word,0xae},{K::text,0xbe}});return s;}
        case 10:return {{K::word,0x81},{K::word,0x54},{K::word,0x56},{K::text,0x58}};
        case 12:{auto s=surface;s.insert(s.end(),{{K::word,0x66},{K::word,0x68}});return s;}
        case 13:{auto s=surface;s.insert(s.end(),{{K::character,0x66},{K::long_integer,0x67}});return s;}
        default:throw std::runtime_error("Unsupported LEVEL class");
    }
}
bool space(char c){return c==' '||c=='\t';}
struct Scanner {
    std::string_view line;std::size_t at{};
    std::string token(){while(at<line.size()&&space(line[at]))++at;auto first=at;while(at<line.size()&&!space(line[at]))++at;if(first==at)throw std::runtime_error("Missing LEVEL field");return std::string(line.substr(first,at-first));}
    std::int32_t integer(std::int64_t low=-32768,std::int64_t high=32767){auto t=token();std::int64_t value{};const char* first=t.data();
        // Borland's signed extractor accepts '+', unlike from_chars. Do not
        // accept a second sign after removing it (e.g. '+-1').
        if(t[0]=='+'){++first;if(first==t.data()+t.size()||*first<'0'||*first>'9')throw std::runtime_error("Invalid LEVEL integer: "+t);}
        auto r=std::from_chars(first,t.data()+t.size(),value);if(r.ec!=std::errc{}||r.ptr!=t.data()+t.size()||value<low||value>high)throw std::runtime_error("Invalid LEVEL integer: "+t);return static_cast<std::int32_t>(value);}
    std::uint8_t character(){while(at<line.size()&&space(line[at]))++at;if(at==line.size())throw std::runtime_error("Missing LEVEL character");return static_cast<std::uint8_t>(line[at++]);}
    std::string text(){auto s=std::string(line.substr(at,39));at=line.size();return s;}
    bool done(){while(at<line.size()&&space(line[at]))++at;return at==line.size();}
};
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
bool starts(const std::string& s,const std::string& prefix){return s.compare(0,prefix.size(),prefix)==0;}
std::string value(const std::string& line){auto at=line.find('=');if(at==std::string::npos)throw std::runtime_error("Missing LEVEL equals");++at;while(at<line.size()&&line[at]==' ')++at;return line.substr(at);}
}
std::int32_t ObjectDefinition::number(int offset)const{for(const auto& f:fields)if(f.offset==offset)return f.number;throw std::runtime_error("Undefined object number");}
std::string ObjectDefinition::resource(int offset)const{for(const auto& f:fields)if(f.offset==offset)return f.bytes;throw std::runtime_error("Undefined object resource");}
const ObjectDefinition* LevelDefinition::object(std::uint8_t glyph)const{auto i=glyph_index[glyph];return i<0?nullptr:&objects[static_cast<std::size_t>(i)];}
LevelDefinition load_level(const Archive& a,const std::string& name){
    LevelDefinition out;out.dataset=a.identify();out.name=name;out.glyph_index.fill(-1);
    if(out.dataset==DatasetId::unknown)throw std::runtime_error("Unknown LEVEL dialect");
    auto bytes=a.get(name);std::string text(reinterpret_cast<const char*>(bytes.data),bytes.size);
    if(text.find('\0')!=std::string::npos)throw std::runtime_error("NUL in LEVEL");
    std::string section,sprite,animation,sound;std::uint16_t sprite_flags=0;
    std::map<std::string,int*> settings={{"level width",&out.width},{"level height",&out.height},{"level length",&out.length},{"cell side",&out.cell_side},{"cell height",&out.cell_height},{"floor color",&out.floor_color},{"ceiling color",&out.ceiling_color},{"min dis",&out.min_distance}};
    std::set<std::string> seen;unsigned line_number=0;
    for(std::size_t at=0;at<text.size();){
        auto end=text.find_first_of("\r\n",at);if(end==std::string::npos)end=text.size();
        auto line=text.substr(at,end-at);at=end;
        if(at<text.size()&&text[at++]=='\r'&&at<text.size()&&text[at]=='\n')++at;
        ++line_number;if(line.empty()||line[0]=='#')continue;
        try{
            if(line[0]=='['){auto close=line.find(']');if(close==std::string::npos)throw std::runtime_error("Unclosed section");section=lower(line.substr(1,close-1));
                if(section!="game ini"&&section!="palette"&&section!="light"&&section!="game object"&&section!="tmap"&&section!="mmap"&&section!="fcmap")throw std::runtime_error("Unknown LEVEL section");
                continue;}
            if(section=="game ini"){
                if(!out.maps.empty())throw std::runtime_error("Settings changed after map data");
                auto key=lower(line);bool found=false;
                if(starts(key,"step ver")){++out.ignored_step_ver;continue;}
                for(const auto& setting:settings)if(starts(key,setting.first)){auto v=value(line);Scanner scan{v};*setting.second=scan.integer(0,32767);if(!scan.done())throw std::runtime_error("Extra setting data");seen.insert(setting.first);found=true;break;}
                if(!found)throw std::runtime_error("Unknown LEVEL setting");
            }else if(section=="palette")out.palette=value(line);
            else if(section=="light")out.light=value(line);
            else if(section=="tmap"||section=="mmap"||section=="fcmap"){
                if(out.width<1||out.width>64||out.length<1||out.length>64||out.height!=1||line.size()!=static_cast<std::size_t>(out.width))throw std::runtime_error("Invalid map dimensions/row");
                auto& map=out.maps[section];if(map.size()+line.size()>static_cast<std::size_t>(out.width*out.length))throw std::runtime_error("Too many map rows");map.insert(map.end(),line.begin(),line.end());
            }else if(section=="game object"){
                auto key=lower(line);
                if(starts(key,"sprite")){auto v=value(line);Scanner scan{v};sprite=scan.token();sprite_flags=static_cast<std::uint16_t>(scan.integer(0,65535));if(!scan.done())throw std::runtime_error("Extra Sprite data");continue;}
                if(starts(key,"flic")){animation=value(line);continue;}
                if(starts(key,"sound")){sound=value(line);continue;}
                Scanner scan{line};ObjectDefinition obj;obj.glyph=scan.character();if(obj.glyph=='_')obj.glyph=' ';
                obj.class_id=static_cast<std::uint16_t>(scan.integer(0,65535));obj.flags=static_cast<std::uint16_t>(scan.integer(0,65535));
                obj.width_parameter=static_cast<std::int16_t>(scan.integer());obj.height_parameter=static_cast<std::int16_t>(scan.integer());obj.marker=static_cast<std::uint8_t>(scan.integer());
                obj.sprite=sprite;obj.sprite_flags=sprite_flags;obj.animation=animation;obj.sound=sound;
                for(const auto& spec:schema(obj.class_id,out.dataset)){
                    Field f;f.kind=spec.kind;f.offset=spec.offset;
                    switch(spec.kind){
                        case K::word:f.number=scan.integer();break;
                        case K::long_integer:f.number=scan.integer(std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max());break;
                        case K::character:f.number=scan.character();break;
                        case K::text:f.bytes=scan.text();break;
                        default:f.bytes=scan.token();break;
                    }
                    obj.fields.push_back(std::move(f));
                }
                if(!scan.done()) {
                    // OG 1ae1:0699 consumes only W66,W68 after its surface reader.
                    // LEVEL4 line 254 has an unused message after those words.
                    if(out.dataset==DatasetId::old_gold && obj.class_id==12) {
                        obj.ignored_tail=std::string(scan.line.substr(scan.at));
                    } else throw std::runtime_error("Extra object fields");
                }
                if(out.glyph_index[obj.glyph]>=0)throw std::runtime_error("Duplicate object glyph");
                out.glyph_index[obj.glyph]=static_cast<int>(out.objects.size());out.objects.push_back(std::move(obj));
            }else throw std::runtime_error("Unknown LEVEL section");
        }catch(const std::exception& e){throw std::runtime_error(name+":"+std::to_string(line_number)+": "+e.what());}
    }
    if(seen.size()!=8||out.width<1||out.length<1||out.cell_side<1||out.cell_height<1||out.palette.empty()||out.floor_color>255||out.ceiling_color>255)throw std::runtime_error("Incomplete LEVEL settings");
    for(auto key:{"tmap","mmap","fcmap"})if(out.maps[key].size()!=static_cast<std::size_t>(out.width*out.length))throw std::runtime_error("Incomplete map");
    std::set<std::pair<K,std::string>> checked;
    auto ref=[&](const std::string& s,K kind){
        if(s.empty()||!checked.insert({kind,s}).second)return;
        if(!a.find(s))throw std::runtime_error("Unresolved LEVEL resource");
        if(kind==K::image)a.image(s);else if(kind==K::animation)a.animation(s);else if(kind==K::sound)a.sound(s);
    };
    a.palette(out.palette);if(!out.light.empty())a.light(out.light);
    for(const auto& o:out.objects){ref(o.sprite,K::image);ref(o.animation,K::animation);ref(o.sound,K::sound);for(const auto& f:o.fields)if(f.kind==K::image||f.kind==K::animation||f.kind==K::sound)ref(f.bytes,f.kind);}
    for(const auto& map:out.maps)for(auto glyph:map.second) {
        auto resolved=static_cast<std::uint8_t>(glyph=='.'?' ':glyph);
        if(!out.object(resolved))throw std::runtime_error("Undefined map glyph in "+map.first);
    }
    return out;
}
}
