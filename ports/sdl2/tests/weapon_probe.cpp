#include "dok/weapons.hpp"
#include "dok/items.hpp"
#include "dok/scene.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static void advance(dok::World& world,dok::Weapons& weapons,std::uint64_t ns){
    world.advance_events(ns,[&](dok::OwnerId owner,dok::CallbackId id){
        if(id!=static_cast<dok::CallbackId>(dok::GameEvent::player_tick))weapons.event(owner,static_cast<dok::GameEvent>(id));
    });
}
static dok::OwnerId pickup(dok::World& world,dok::Weapons& weapons,int slot){
    for(std::size_t i=0;i<world.maps.at("tmap").size();++i){
        const auto* object=world.definition.object(world.maps.at("tmap")[i]);
        if(object->class_id!=10||object->number(0x81)!=slot)continue;
        auto& pose=*world.objects.at(*world.player).pose;
        pose.x=static_cast<int>(i%world.definition.width)*world.definition.cell_side+world.definition.cell_side/2;
        pose.z=static_cast<int>(i/world.definition.width)*world.definition.cell_side+world.definition.cell_side/2;
        auto owner=dok::player_pickup(world);weapons.picked_up(owner);return owner;
    }
    throw std::runtime_error("Weapon pickup not found");
}
int main(int argc,char** argv){
    try{
        if(argc<2)throw std::invalid_argument("Usage: dok_weapon_probe [--output DIR] RES...");
        int first=1;std::filesystem::path output;
        if(std::string(argv[1])=="--output"){
            if(argc<4)throw std::invalid_argument("Missing output directory or RES");
            output=std::filesystem::u8path(argv[2]);first=3;std::filesystem::create_directories(output);
        }
        unsigned cases=0;
        for(int a=first;a<argc;++a){const auto archive=dok::Archive::open(std::filesystem::u8path(argv[a]));
            for(auto level:{"LEVEL1","LEVEL2","LEVEL3"}){
                std::vector<std::uint64_t> reference;
                for(unsigned fps:{30,60,144}){
                    dok::World w(dok::load_level(archive,level));dok::Weapons weapons(w,archive);
                    const auto gun=pickup(w,weapons,1);auto& g=*w.object_for_owner(gun).weapon;
                    auto& p=*w.objects.at(*w.player).player_state;
                    check(g.state==1&&g.shift==2,"Initial show state");
                    check(!weapons.fire([](auto){return 14;}),"Hidden weapon fired");
                    advance(w,weapons,350000000);check(g.state==7&&g.shift==0,"Weapon not ready after show");
                    dok::IndexedSurface overlay(320,160);weapons.overlay(overlay);
                    check(std::any_of(overlay.pixels.begin(),overlay.pixels.end(),[](auto c){return c!=0;}),"Weapon overlay empty");
                    auto save=[&](int slot){
                        if(output.empty()||fps!=30)return;
                        dok::SoftwareScene scene(archive,w.definition);
                        auto image=scene.render(w,*w.objects.at(*w.player).pose);weapons.overlay(image);
                        const auto name=std::to_string(a-first)+"-"+level+"-"+std::to_string(slot)+".idx";
                        std::ofstream file(output/name,std::ios::binary);
                        file.write(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size());
                        if(!file)throw std::runtime_error("Cannot write weapon preview");
                    };
                    save(1);
                    p.attributes[1]=1;unsigned spawns=0;
                    check(weapons.fire([&](auto glyph){check(glyph=='1',"Wrong projectile template");++spawns;return 14;}),"One charge must allow firing");
                    check(p.charge()==0&&spawns==1&&g.state==2,"Fire accounting");
                    check(!weapons.fire([](auto){return 14;}),"Repeated not-ready fire");
                    std::vector<std::uint64_t> trace;std::uint64_t previous=0;
                    for(unsigned f=1;f<=fps*2;++f){
                        const auto now=std::uint64_t(f)*1000000000ULL/fps;
                        w.advance_events(now-previous,[&](dok::OwnerId owner,dok::CallbackId id){
                            if(id==static_cast<dok::CallbackId>(dok::GameEvent::player_tick))return;
                            weapons.event(owner,static_cast<dok::GameEvent>(id));
                            trace.push_back((w.simulation_ticks()<<16)|(g.state<<8)|g.frame);
                        });previous=now;
                    }
                    check(g.state==7&&g.frame==0,"Gun animation failed to reset");
                    check(std::any_of(trace.begin(),trace.end(),[](auto v){return (v&0xffff)==0x0608;}),"Gun did not reach ninth frame before recovery");
                    check(std::any_of(trace.begin(),trace.end(),[](auto v){return (v&0xffff)==0x0300;}),"Gun skipped early-ready state3");
                    if(reference.empty())reference=trace;else check(reference==trace,"Weapon trace depends on FPS");
                    p.attributes[1]=50;check(!weapons.fire([](auto){return 0;}),"Failed spawn reported success");
                    check(p.charge()==50&&g.state==2,"Failed spawn must consume readiness only");
                    advance(w,weapons,2000000000);
                    const auto laser=pickup(w,weapons,2);
                    check(g.state==0&&p.current_weapon==1&&p.pending_weapon==2,"Pickup did not hide current weapon");
                    advance(w,weapons,1000000000);
                    check(p.current_weapon==2&&w.object_for_owner(laser).weapon->state==7&&g.state==4,"Laser switch did not finish");
                    save(2);
                    p.attributes[1]=4;check(weapons.fire([](auto glyph){check(glyph=='2',"Wrong laser template");return 4;}),"Laser fire contract");
                    advance(w,weapons,500000000);
                    check(w.object_for_owner(laser).weapon->state==7&&p.charge()==0,"Laser three-frame recovery");
                    check(weapons.cycle(-1),"Controller previous selection");advance(w,weapons,1000000000);
                    check(p.current_weapon==1&&g.state==7,"Return to gun failed");++cases;
                }
            }
        }
        std::cout<<cases<<" weapon scenarios: pickup/show/fire contract/cooldown/switch/overlay and FPS passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
