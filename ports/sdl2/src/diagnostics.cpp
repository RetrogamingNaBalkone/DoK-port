#include "dok/level.hpp"
#include "dok/world.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace dok {
static std::string hex(const std::string& bytes) {
    std::ostringstream s;s<<std::hex<<std::setfill('0');for(unsigned char b:bytes)s<<std::setw(2)<<unsigned(b);return s.str();
}
static std::string hex(const std::vector<std::uint8_t>& b){return hex(std::string(b.begin(),b.end()));}
static std::uint32_t hash(const std::vector<std::uint8_t>& bytes){std::uint32_t h=2166136261u;for(auto b:bytes)h=(h^b)*16777619u;return h;}
static void image_json(const Image& im){std::cout<<"["<<im.width<<','<<im.height<<','<<im.flags<<','<<hash(im.pixels)<<']';}
void diagnostic_json(const Archive& a) {
    std::cout<<"{\"levels\":[";bool first=true;
    for(const auto& r:a.entries())if(r.name.compare(0,5,"LEVEL")==0){
        auto l=load_level(a,r.name);if(!first)std::cout<<',';first=false;
        std::cout<<"{\"name\":\""<<l.name<<"\",\"settings\":["<<l.width<<','<<l.height<<','<<l.length<<','<<l.cell_side<<','<<l.cell_height<<','<<l.floor_color<<','<<l.ceiling_color<<','<<l.min_distance<<"],\"palette\":\""<<hex(l.palette)<<"\",\"light\":\""<<hex(l.light)<<"\",\"ignored_step_ver\":"<<l.ignored_step_ver<<",\"objects\":[";
        bool first_object=true;
        for(const auto& o:l.objects){
            if(!first_object)std::cout<<',';first_object=false;
            std::cout<<"{\"glyph\":"<<unsigned(o.glyph)<<",\"class\":"<<o.class_id<<",\"flags\":"<<o.flags<<",\"width\":"<<o.width_parameter<<",\"height\":"<<o.height_parameter<<",\"marker\":"<<unsigned(o.marker)<<",\"sprite_flags\":"<<o.sprite_flags<<",\"sprite\":\""<<hex(o.sprite)<<"\",\"animation\":\""<<hex(o.animation)<<"\",\"sound\":\""<<hex(o.sound)<<"\",\"fields\":[";
            bool first_field=true;
            for(const auto& f:o.fields){if(!first_field)std::cout<<',';first_field=false;std::cout<<'['<<int(f.kind)<<','<<f.offset<<',';if(f.kind<=FieldKind::character)std::cout<<f.number;else std::cout<<'"'<<hex(f.bytes)<<'"';std::cout<<']';}
            std::cout<<"]}";
        }
        std::cout<<"],\"maps\":[\""<<hex(l.maps.at("tmap"))<<"\",\""<<hex(l.maps.at("mmap"))<<"\",\""<<hex(l.maps.at("fcmap"))<<"\"]";
        World world(l);
        std::cout<<",\"world\":{\"money\":"<<world.total_money<<",\"actors\":[";
        bool first_actor=true;
        for(const auto& o:world.objects)if(o.pose){if(!first_actor)std::cout<<',';first_actor=false;const auto& def=l.objects[o.definition_index];const auto& p=*o.pose;
            std::cout<<"{\"glyph\":"<<unsigned(def.glyph)<<",\"class_id\":"<<def.class_id<<",\"placements\":"<<o.placements<<",\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"z\":"<<p.z<<",\"heading\":"<<p.heading<<'}';}
        std::cout<<"],\"maps\":[\""<<hex(world.maps.at("tmap"))<<"\",\""<<hex(world.maps.at("mmap"))<<"\",\""<<hex(world.maps.at("fcmap"))<<"\"],\"players\":[";
        bool first_player=true;
        for(const auto& o:world.objects)if(o.player_state){
            if(!first_player)std::cout<<',';first_player=false;
            const auto& p=*o.player_state;
            std::cout<<"{\"owner\":"<<o.owner<<",\"glyph\":"<<unsigned(l.objects[o.definition_index].glyph)<<",\"attributes\":[";
            for(unsigned i=0;i<p.attributes.size();++i){if(i)std::cout<<',';std::cout<<p.attributes[i];}
            std::cout<<"],\"weapons\":[";
            for(unsigned i=0;i<p.weapons.size();++i){if(i)std::cout<<',';std::cout<<p.weapons[i];}
            std::cout<<"],\"lives\":"<<p.lives<<",\"score\":"<<p.score<<",\"current\":"<<p.current_weapon<<",\"pending\":"<<p.pending_weapon<<",\"events\":[";
            bool first_event=true;for(const auto& e:world.events.events(o.owner)){
                if(!first_event)std::cout<<',';first_event=false;
                std::cout<<'['<<e.deadline<<','<<e.interval<<','<<e.callback<<','<<e.parameter<<']';}
            std::cout<<"]}";
        }
        std::cout<<"]}}";
    }
    std::cout<<"],\"assets\":[";first=true;
    for(const auto& r:a.entries()){
        auto b=a.get(r.name);std::string kind;std::vector<AnimationFrame> frames;
        if(r.name=="FONT8"||r.name=="FONT16"||r.name.compare(0,5,"LIGHT")==0||b.size==768){
            if(!first)std::cout<<',';first=false;
            kind=b.size==768?"palette":r.name.compare(0,5,"LIGHT")==0?"light":"font";
            std::cout<<"{\"name\":\""<<hex(r.name)<<"\",\"kind\":\""<<kind<<"\",\"data\":[";
            if(kind=="palette"){
                auto p=a.palette(r.name);for(unsigned i=0;i<p.size();++i){if(i)std::cout<<',';std::cout<<p[i];}
            }else{auto raw=kind=="font"?a.font(r.name):a.light(r.name);std::vector<std::uint8_t> bytes(raw.data,raw.data+raw.size);std::cout<<raw.size<<','<<hash(bytes);}
            std::cout<<"]}";continue;
        }
        bool is_image=b.size>=18 && b.u16(0)>0 && b.u16(2)>0 && std::size_t(b.u16(0))*b.u16(2)==b.size-18 && b.u32(10)==b.size-18;
        bool is_sound=b.size>=36 && b.data[2]=='S'&&b.data[3]=='V'&&b.data[4]=='M';
        if(!is_image&&!is_sound){try{frames=a.animation(r.name);}catch(const std::exception&){}if(frames.empty())continue;}
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"name\":\""<<hex(r.name)<<"\",\"kind\":\""<<(is_image?"image":is_sound?"sound":"animation")<<"\",\"data\":";
        if(is_image)image_json(a.image(r.name));
        else if(is_sound){auto s=a.sound(r.name);std::cout<<'['<<s.frequency<<','<<s.parameter_c<<','<<s.parameter_d<<','<<s.samples.size()<<','<<hash(s.samples)<<']';}
        else{std::cout<<'[';bool ff=true;for(const auto& f:frames){if(!ff)std::cout<<',';ff=false;std::cout<<'['<<f.interval<<','<<f.parameter_a<<','<<f.parameter_b<<',';image_json(f.image);std::cout<<']';}std::cout<<']';}
        std::cout<<'}';
    }
    std::cout<<"]}\n";
}
}
