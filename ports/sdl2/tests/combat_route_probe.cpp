#include "dok/animations.hpp"
#include "dok/monsters.hpp"
#include "dok/weapons.hpp"
#include "dok/items.hpp"
#include "dok/movement.hpp"
#include "dok/interactions.hpp"
#include "dok/scene.hpp"
#include "dok/input_replay.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <queue>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Session {
    dok::World world;
    dok::Weapons weapons;
    dok::Projectiles shots;
    dok::Monsters monsters;
    dok::Animations animations;
    dok::SoftwareScene scene;
    dok::InputActions input;
    bool use=false,fire=false,ai=false;
    unsigned pickups=0,spawns=0,impacts=0;
    unsigned fps=60;
    std::uint64_t frame=0,elapsed=0;
    std::ostream* recording=nullptr;
    Session(const dok::Archive& a,unsigned rate):world(dok::load_level(a,"LEVEL4")),weapons(world,a),shots(world,a),monsters(world,a,shots),animations(world,a),scene(a,world.definition),fps(rate){}
    dok::WorldObject& actor(){return world.objects.at(*world.player);}
    dok::PlayerState& player(){return *actor().player_state;}
    int cell()const{const auto& p=*world.objects.at(*world.player).pose;return p.z/world.definition.cell_side*world.definition.width+p.x/world.definition.cell_side;}
    void tick(){
        const auto now=++frame*1000000000ULL/fps;
        world.advance_events(now-elapsed,[&](dok::OwnerId owner,dok::CallbackId cb){
            const auto event=static_cast<dok::GameEvent>(cb);
            if(event==dok::GameEvent::player_tick){
                if(recording){auto recorded=input;recorded.use=use;recorded.fire=fire;dok::InputReplay::write_sample(*recording,{world.simulation_ticks(),actor().pose->heading,recorded});}
                if(ai)monsters.consider(dok::original_cell_bounds(*actor().pose,world.definition.cell_side,world.definition.width,world.definition.length));
                animations.consider(dok::original_cell_bounds(*actor().pose,world.definition.cell_side,world.definition.width,world.definition.length));
                if(fire&&weapons.fire([&](auto glyph){const auto cost=shots.spawn(owner,glyph);if(cost)++spawns;return cost;})){}
                if(use){dok::player_use(world);use=false;}
                if(const auto picked=dok::player_pickup(world)){++pickups;if(world.object_for_owner(picked).weapon)weapons.picked_up(picked);}
                dok::player_sensors(world);
                auto action=input;action.slow|=dok::player_surface(world);
                dok::update_player_motion(world,action,static_cast<std::uint32_t>(world.simulation_ticks()),false);
            }else if(event==dok::GameEvent::projectile_tick){
                const auto slot=owner-dok::Projectiles::owner_base;
                const bool before=world.projectiles.at(slot)->impact,owned=world.projectiles.at(slot)->source==actor().owner;shots.event(owner);
                if(owned&&world.projectiles.at(slot)&&!before&&world.projectiles.at(slot)->impact)++impacts;
            }else if(event==dok::GameEvent::monster_tick)monsters.event(owner);
            else if(event==dok::GameEvent::weapon_tick||event==dok::GameEvent::weapon_switch)weapons.event(owner,event);
            else if(event==dok::GameEvent::base_animation)animations.event(owner);
            else dok::door_event(world,owner,event);
        });elapsed=now;
    }
    void wait(unsigned ms){const auto end=elapsed+std::uint64_t(ms)*1000000;while(elapsed<end)tick();}
    unsigned player_shots()const{unsigned count=0;for(const auto& shot:world.projectiles)if(shot&&shot->source==world.objects.at(*world.player).owner)++count;return count;}
    void heading(int value){dok::apply_mouse_yaw(world,float(actor().pose->heading-value));}
    // Explicit initial-condition setup for isolated source-cell tests only.
    // route() never calls this before its complete spawn -> W -> spawn walk.
    void setup_cell(int at){
        const auto& d=world.definition;auto& map=world.maps.at("mmap");const auto glyph=d.objects[actor().definition_index].glyph;
        check(passable(at),"Initial-condition cell is blocked");
        if(map[cell()]==glyph)map[cell()]=' ';
        actor().pose->x=(at%d.width)*d.cell_side+d.cell_side/2;actor().pose->z=(at/d.width)*d.cell_side+d.cell_side/2;
        map[at]=glyph;input={};
    }
    void approach_item(int at){
        const int width=world.definition.width;
        for(int next:{at+1,at-1,at+width,at-width}){
            if(next>=0&&next<int(world.visited.size())&&std::abs(next%width-at%width)+std::abs(next/width-at/width)==1&&passable(next)){
                setup_cell(next);walk(at);return;
            }
        }
        throw std::runtime_error("No adjacent item approach cell");
    }
    bool passable(int at)const{
        const auto& d=world.definition;const auto& o=world.objects.at(d.glyph_index[world.maps.at("tmap")[at]]);
        const auto& def=d.objects[o.definition_index];
        const auto actor_class=d.object(world.maps.at("mmap")[at])->class_id;
        return ((o.flags&0x10)||(def.class_id==7&&!(def.number(0x5c)&~world.objects.at(*world.player).player_state->keys())))&&(actor_class==0||actor_class==1);
    }
    std::vector<int> route(int destination)const{
        const auto& d=world.definition;std::vector<int> prev(d.width*d.length,-1);std::queue<int> queue;
        const int origin=cell();prev[origin]=origin;queue.push(origin);
        while(!queue.empty()&&prev[destination]<0){const int at=queue.front();queue.pop();
            for(int next:{at+1,at-1,at+d.width,at-d.width}){
                if(next<0||next>=int(prev.size())||std::abs(next%d.width-at%d.width)+std::abs(next/d.width-at/d.width)!=1||prev[next]>=0||!passable(next))continue;
                prev[next]=at;queue.push(next);
            }
        }
        if(prev[destination]<0)return {};
        std::vector<int> path;for(int at=destination;at!=origin;at=prev[at])path.push_back(at);std::reverse(path.begin(),path.end());return path;
    }
    void walk(int destination){
        const auto path=route(destination);check(destination==cell()||!path.empty(),"No walkable route to target");
        const auto& d=world.definition;
        for(const auto at:path){
            const int tx=(at%d.width)*d.cell_side+d.cell_side/2,tz=(at/d.width)*d.cell_side+d.cell_side/2;
            const auto& o=world.objects.at(d.glyph_index[world.maps.at("tmap")[at]]);
            const int dx=tx-actor().pose->x,dz=tz-actor().pose->z;
            heading(std::abs(dx)>std::abs(dz)?(dx>0?270:90):(dz>0?0:180));
            if(o.door&&!(o.flags&0x10)){use=true;wait(3000);check(o.flags&0x10,"Use did not open route door");}
            unsigned attempts=0;
            while(std::abs(actor().pose->x-tx)>2||std::abs(actor().pose->z-tz)>2){
                check(++attempts<fps*10,"Movement stalled on source map route");
                const int rx=tx-actor().pose->x,rz=tz-actor().pose->z;
                heading(std::abs(rx)>std::abs(rz)?(rx>0?270:90):(rz>0?0:180));
                input.forward=std::min(1.0f,float(std::max(std::abs(rx),std::abs(rz)))/45.0f);
                tick();
            }
            input={};wait(80);check(cell()==at,"Route lost target cell");
        }
    }
};
int placed(const dok::World& w,char glyph){const auto& m=w.maps.at("tmap");auto at=std::find(m.begin(),m.end(),glyph);check(at!=m.end(),"Missing placed item");return int(at-m.begin());}
void route(unsigned fps,const dok::Archive& a,const std::filesystem::path& recording={}){
    Session s(a,fps);s.ai=true;check(s.player().charge()==0&&!s.player().weapons[1],"Unexpected original starting equipment");
    std::ofstream record;
    if(!recording.empty()){record.open(recording);check(bool(record),"Cannot write replay");record<<"DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n"<<std::setprecision(9);s.recording=&record;}
    check(!s.weapons.fire([&](auto glyph){return s.shots.spawn(s.actor().owner,glyph);}),"Unarmed player fired");
    const int start=s.cell(),wand=placed(s.world,'W');
    // At spawn OG has no wand/capacity. The nearby '(' magic must be refused
    // at 0/0 exactly as the native current==maximum admission check requires.
    const int near_magic=47*s.world.definition.width+31;
    check(s.world.maps.at("tmap")[near_magic]=='(',"Missing original nearby magic");
    s.walk(near_magic);s.wait(100);
    check(s.world.maps.at("tmap")[near_magic]=='('&&s.player().charge()==0&&s.pickups==0,"Initial 0/0 magic was consumed");
    unsigned money=0;
    for(int attempt=0;attempt<2;++attempt){
        int target=-1;std::size_t length=999999;
        for(std::size_t at=0;at<s.world.maps.at("tmap").size();++at)if(s.world.maps.at("tmap")[at]=='$'){
            const auto path=s.route(static_cast<int>(at));if(!path.empty()&&path.size()<length){target=static_cast<int>(at);length=path.size();}
        }
        check(target>=0,"No natural money route");const auto score=s.player().score;s.walk(target);s.wait(100);
        check(s.world.maps.at("tmap")[target]==' '&&s.player().score==score+1,"Money on original reachable route not collected");++money;
    }
    s.walk(wand);s.wait(500);
    check(s.player().weapons[1]&&s.player().current_weapon==1&&s.player().charge()==150&&s.player().max_charge()==150,"Natural first wand acquisition failed");
    auto& gun=*s.world.object_for_owner(s.player().weapons[1]).weapon;
    check(gun.state==7,"Acquired wand did not become ready");
    dok::IndexedSurface overlay(320,170);s.weapons.overlay(overlay);check(std::any_of(overlay.pixels.begin(),overlay.pixels.end(),[](auto p){return p!=0;}),"Wand invisible in classic overlay");
    s.heading(180);s.fire=true;s.wait(1500);s.fire=false;s.wait(1500);
    check(s.spawns>0&&s.player().charge()==150-int(s.spawns)&&s.impacts>0,"Ordinary wand fire/contact/charge failed");
    check(s.player_shots()==0,"Ordinary shots did not finish effects");
    s.walk(near_magic);s.walk(start);s.wait(100);check(s.player().charge()>150-int(s.spawns),"Return route did not pick previously refused magic");
    check(s.world.maps.at("tmap")[near_magic]==' '&&s.player().score>=int(money),"Natural money/magic cells did not stay collected");
    check(s.player().health()>0,"Ordinary route killed the player");
    std::cout<<"OG route fps="<<fps<<" first_wand_cell="<<wand<<" pickups="<<s.pickups<<" money="<<s.player().score<<" shots="<<s.spawns<<" impacts="<<s.impacts<<" charge="<<s.player().charge()<<" ticks="<<s.world.simulation_ticks()<<'\n';
    s.recording=nullptr;record.close();
    // These continue the real inventory, but set a nearby starting position to
    // isolate the other placed wand and an original monster. They are not a
    // claim that the complete map was walked or its key progression solved.
    s.approach_item(placed(s.world,'V'));s.wait(1200);
    check(s.player().weapons[2]&&s.player().current_weapon==2&&s.player().max_charge()==300&&s.player().charge()==300,"Second placed wand acquisition/switch failed");
    check(s.world.object_for_owner(s.player().weapons[2]).weapon->state==7,"Second wand did not become ready");
    const auto before=s.spawns;s.heading(180);s.fire=true;s.wait(1200);s.fire=false;s.wait(2000);
    check(s.spawns>before&&s.player().charge()==300-int(s.spawns-before)&&!s.player_shots(),"Second wand fire/charge/effect failed");
    check(s.weapons.select(1),"Select first wand failed");s.wait(1200);
    check(s.player().current_weapon==1&&s.world.object_for_owner(s.player().weapons[1]).weapon->state==7,"Switch back to first wand failed");
    check(s.weapons.cycle(1),"Cycle to second wand failed");s.wait(1200);check(s.player().current_weapon==2,"Cycle did not select second wand");
    bool contact=false;
    const int width=s.world.definition.width;
    for(auto& monster:s.world.objects)if(monster.monster&&monster.pose){
        const int at=monster.pose->z/s.world.definition.cell_side*width+monster.pose->x/s.world.definition.cell_side;
        for(const int offset:{1,-1,width,-width}){
            const int near=at+offset,source=at+offset*2;
            if(near<0||source<0||near>=width*width||source>=width*width||std::abs(source%width-at%width)+std::abs(source/width-at/width)!=2||!s.passable(near)||!s.passable(source))continue;
            s.setup_cell(source);s.heading(offset==1?90:offset==-1?270:offset==width?180:0);
            const auto health=monster.monster->health;
            s.fire=true;for(unsigned f=0;f<fps*4&&monster.monster->health==health;++f)s.tick();s.fire=false;
            check(monster.monster->state!=0&&monster.monster->health<health,"Ordinary shots failed to damage original placed monster");
            s.wait(2500);check(!s.player_shots(),"Monster contact leaked projectile");
            std::cout<<"OG isolated source cells fps="<<fps<<" both_wands=PASS switch=PASS monster_owner="<<monster.owner<<" hp="<<health<<"->"<<monster.monster->health<<" player_projectiles="<<s.player_shots()<<'\n';
            contact=true;break;
        }
        if(contact)break;
    }
    check(contact,"No original monster contact scenario executed");
}
void native_matrix(const dok::Archive& a){
    Session s(a,60);auto& p=s.player();
    for(int category:{0,1,2,3,4,5,6,7,8,9,10,12,13,14,99,200})
        for(int current:{-1,0,1,50,100,32760})for(int value:{-32768,-100,-1,0,1,10,32767})for(int maximum:{0,100}){
            p.attributes.fill(static_cast<std::int16_t>(current));p.extra_attributes.fill(static_cast<std::int16_t>(current));
            p.attributes[2]=p.attributes[3]=static_cast<std::int16_t>(maximum);p.score=static_cast<std::int16_t>(current);s.world.map_revealed=false;
            const bool accepted=dok::change_player_stat(s.world,static_cast<std::int16_t>(category),static_cast<std::int16_t>(value));
            std::cout<<0<<','<<category<<','<<current<<','<<value<<','<<maximum<<','<<accepted;
            for(auto v:p.attributes)std::cout<<','<<v;for(auto v:p.extra_attributes)std::cout<<','<<v;
            std::cout<<','<<p.score<<','<<s.world.map_revealed<<'\n';
        }
    dok::OwnerId owner=0;for(const auto& object:s.world.objects)if(object.weapon){owner=object.owner;break;}
    check(owner!=0,"No OG weapon for native matrix");
    for(int state:{0,1,2,3,4,5,6,7})for(int frame:{0,1,2})for(int shift:{0,1,2,3}){
        Session w(a,60);auto& object=w.world.object_for_owner(owner);auto& gun=*object.weapon;
        gun.state=static_cast<std::int16_t>(state);gun.frame=static_cast<std::int16_t>(frame);gun.shift=static_cast<std::int16_t>(shift);object.flags=0x1234;
        check(w.world.events.schedule(owner,static_cast<dok::CallbackId>(dok::GameEvent::weapon_tick),416,0),"Matrix schedule failed");
        w.wait(50);const auto events=w.world.events.events(owner);
        std::cout<<1<<','<<state<<','<<frame<<','<<shift<<','<<gun.state<<','<<gun.frame<<','<<gun.shift<<','<<object.flags<<','<<!events.empty()<<','<<(events.empty()?0:events[0].interval)<<'\n';
    }
}
}
int main(int argc,char** argv){try{check(argc>=2&&argc<=4,"Usage: dok_combat_route_probe [--native-matrix | --record PATH] OLD_GOLD.RES");const auto a=dok::Archive::open(std::filesystem::u8path(argv[argc-1]));if(argc==3&&std::string(argv[1])=="--native-matrix")native_matrix(a);else if(argc==4&&std::string(argv[1])=="--record")route(60,a,std::filesystem::u8path(argv[2]));else for(unsigned fps:{30,60,144})route(fps,a);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
