#include "dok/interactions.hpp"
#include "dok/projectiles.hpp"
#include "dok/movement.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static dok::WorldObject& object(dok::World& w,unsigned char glyph){return w.objects.at(w.definition.glyph_index[glyph]);}
static void advance(dok::World& w,std::uint64_t ns){
    w.advance_events(ns,[&](dok::OwnerId owner,dok::CallbackId id){
        const auto event=static_cast<dok::GameEvent>(id);
        if(event==dok::GameEvent::door_open||event==dok::GameEvent::door_close||event==dok::GameEvent::door_auto)
            dok::door_event(w,owner,event);
    });
}
static void door(const dok::Archive& archive){
    dok::World w(dok::load_level(archive,"LEVEL3"));auto& hero=w.objects.at(*w.player);
    auto& gate=object(w,'k');const auto& def=w.definition.objects[gate.definition_index];
    check(def.number(0x5c)==8&&def.number(0x5e)==0&&def.number(0x66)==0,"LEVEL3 gate contract changed");
    const auto& sensor=*w.definition.object('f');
    check(sensor.number(0x66)=='k'&&sensor.number(0x67)==0x800,"LEVEL3 sensor changed");
    unsigned sensors=0;const int side=w.definition.cell_side;
    for(int z=0;z<64;++z)for(int x=0;x<64;++x)if(w.definition.maps.at("fcmap")[z*64+x]=='f'){
        check(z==12&&x>=57&&x<=62,"Unexpected sensor location");++sensors;
    }
    check(sensors==6,"Sensor strip length");
    hero.pose->x=57*side+side/2;hero.pose->z=(63-12)*side+side/2;
    dok::player_sensors(w);advance(w,2000000000ULL);
    check(gate.door->state==1&&gate.door->ready,"Sensor did not open gate");
    advance(w,60000000000ULL);
    check(gate.door->state==1&&w.events.events(gate.owner).empty(),"Gate acquired an auto-close timer");
    hero.pose->x+=side;dok::player_sensors(w);
    check(w.events.events(gate.owner).empty(),"Adjacent same sensor retriggered");
    hero.pose->z+=side;dok::player_sensors(w);hero.pose->z-=side;dok::player_sensors(w);
    // Enter before the closing animation finishes, using collision-aware moves.
    for(int step=0;step<12;++step){
        dok::move_actor_to(w,hero.owner,hero.pose->x-side/4,hero.pose->z);
        advance(w,41600000ULL);
    }
    check(hero.pose->x<56*side&&gate.door_occupants==0,"Could not enter key pocket during closing");
    advance(w,2000000000ULL);check(gate.door->state==2,"Re-entry did not close gate");
    // First encountered key is the iron key Y, not the copper key X.
    const auto& key=*w.definition.object(w.definition.maps.at("tmap")[11*64+52]);
    check(key.class_id==3&&key.number(0x54)==10&&key.number(0x56)==2,"Iron key location");
    hero.player_state->attributes[10]=2;
    check(dok::use_door(w,gate.owner,hero.owner)==dok::UseResult::locked,"Iron key unexpectedly opens sensor gate");
    auto& exit=object(w,'a');
    check(w.definition.maps.at("tmap")[23*64+31]=='a'&&w.definition.maps.at("tmap")[24*64+31]=='a',"Room exit location");
    check(dok::use_door(w,exit.owner,hero.owner)==dok::UseResult::scheduled,"Iron key does not open room exit");
    advance(w,2000000000ULL);check(exit.door->state==1,"Room exit did not open");
    // Topology after defeating actors: test actual collision clearance, including
    // static decorations. Definition rows run north-to-south; runtime Z is flipped.
    std::fill(w.maps.at("mmap").begin(),w.maps.at("mmap").end(),' ');
    std::vector<int> route{(63-11)*64+52};std::vector<bool> seen(4096);
    seen[route[0]]=true;
    for(std::size_t i=0;i<route.size();++i){
        const int x=route[i]%64,z=route[i]/64;
        for(const auto delta:{std::pair<int,int>{1,0},{-1,0},{0,1},{0,-1}}){
            const int nx=x+delta.first,nz=z+delta.second;
            if(nx<0||nz<0||nx>=64||nz>=64)continue;
            const int at=nz*64+nx;
            if(!seen[at]&&dok::footprint_clear(w,nx*side+side/2,nz*side+side/2)){
                seen[at]=true;route.push_back(at);
            }
        }
    }
    check(!seen[(63-23)*64+30],"Key pocket unexpectedly connects to iron-key exit with k closed");
    // The later copper-key pocket is a different door, with original AUTO data.
    auto& copper=object(w,'u');const auto& copper_def=w.definition.objects[copper.definition_index];
    check(copper_def.number(0x5c)==0&&copper_def.number(0x5e)==1&&copper_def.number(0x66)==40000,"Copper pocket AUTO contract");
    hero.player_state->attributes[10]=0;
    check(dok::use_door(w,copper.owner,hero.owner)==dok::UseResult::scheduled,"Copper pocket requires a key");
    advance(w,12000000000ULL);check(copper.door->state==2,"Copper AUTO did not close");
    check(dok::use_door(w,copper.owner,hero.owner)==dok::UseResult::scheduled,"Copper pocket cannot reopen from inside");
}
static void boundary(const dok::Archive& archive,const char* level){
    const auto definition=dok::load_level(archive,level);const int side=definition.cell_side;
    for(int direction:{-1,1})for(bool adjacent:{false,true}){
        dok::World w(definition);
        for(auto name:{"tmap","mmap"})std::fill(w.maps.at(name).begin(),w.maps.at(name).end(),' ');
        auto& hero=w.objects.at(*w.player);
        auto target=std::find_if(w.objects.begin(),w.objects.end(),[](const auto& o){return bool(o.monster);});
        check(target!=w.objects.end(),"No monster in original level");
        const int edge=11*side,monster_x=edge+direction;
        target->pose=dok::Pose{monster_x,-300,12*side+side/2,0};
        w.maps.at("mmap")[12*definition.width+monster_x/side]=definition.objects[target->definition_index].glyph;
        hero.pose=dok::Pose{adjacent?edge-direction:monster_x,-300,10*side+side/2,0};
        dok::Projectiles shots(w,archive);unsigned hits=0;
        shots.monster_hit=[&](dok::OwnerId owner,std::int16_t){check(owner==target->owner,"Wrong monster");++hits;return true;};
        check(shots.spawn(hero.owner,'1')>0,"Spawn failed");
        w.advance_events(500000000ULL,[&](dok::OwnerId owner,dok::CallbackId id){
            if(id==static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))shots.event(owner);
        });
        check(hits==(adjacent?0u:1u),"Cell-boundary contact behavior changed");
    }
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("Usage: dok_rc1_review_probe ROOT");
        const auto root=std::filesystem::u8path(argv[1]);
        for(const auto* path:{"originals/pkr/RUS/PKR.RES","originals/pkr/ENG/PKE.RES"}){
            const auto archive=dok::Archive::open(root/path);door(archive);boundary(archive,"LEVEL3");
        }
        boundary(dok::Archive::open(root/"originals/Old_Gold_3D/OLD_GOLD.RES"),"LEVEL4");
        std::cout<<"PASS: 2 original LEVEL3 door/sensor/closed-pocket scenarios; 12 projectile boundary scenarios (same cell hits, adjacent cell misses)\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
