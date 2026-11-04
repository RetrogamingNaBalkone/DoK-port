#include "dok/animations.hpp"
#include "dok/flow.hpp"
#include "dok/monsters.hpp"
#include "dok/weapons.hpp"
#include "dok/items.hpp"
#include "dok/movement.hpp"
#include "dok/interactions.hpp"
#include "dok/input_replay.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>

namespace {
struct Route {
    dok::World world;
    dok::Weapons weapons;
    dok::Projectiles shots;
    dok::Monsters monsters;
    dok::Animations animations;
    dok::GameFlow flow;
    dok::InputActions input;
    std::ostream* recording=nullptr;
    std::uint64_t frame=0,elapsed=0;
    unsigned pickups=0,spawns=0,used=0;
    bool resupplying=false;
    std::set<int> sensors;
    Route(const dok::Archive& a,const std::string& level):world(dok::load_level(a,level)),weapons(world,a),shots(world,a),monsters(world,a,shots),animations(world,a),flow(world){}
    dok::WorldObject& actor(){return world.objects.at(*world.player);}
    dok::PlayerState& player(){return *actor().player_state;}
    const dok::PlayerState& player()const{return *world.objects.at(*world.player).player_state;}
    int cell(dok::Pose p)const{return p.z/world.definition.cell_side*world.definition.width+p.x/world.definition.cell_side;}
    int cell()const{return cell(*world.objects.at(*world.player).pose);}
    const dok::ObjectDefinition& terrain(int at)const{return *world.definition.object(world.maps.at("tmap")[at]);}
    const dok::ObjectDefinition& floor(int at)const{return *world.definition.object(world.maps.at("fcmap")[at]);}
    void aim(int x,int z){
        const auto p=*actor().pose;const double angle=std::atan2(double(p.x-x),double(z-p.z))*180.0/3.14159265358979323846;
        const int heading=(int(std::lround(angle))+360)%360;
        dok::apply_mouse_yaw(world,float(p.heading-heading));
    }
    void tick(){
        if(flow.phase()!=dok::SessionPhase::playing)return;
        const auto now=++frame*1000000000ULL/60;
        world.advance_events(now-elapsed,[&](dok::OwnerId owner,dok::CallbackId id){
            const auto event=static_cast<dok::GameEvent>(id);
            if(event==dok::GameEvent::player_tick){
                if(player().health()<=0||world.level_complete)return;
                if(recording)dok::InputReplay::write_sample(*recording,{world.simulation_ticks(),actor().pose->heading,input});
                if(input.select_weapon>=0)weapons.select(input.select_weapon);
                if(input.next_weapon)weapons.cycle(1);
                const auto bounds=dok::original_cell_bounds(*actor().pose,world.definition.cell_side,world.definition.width,world.definition.length);
                monsters.consider(bounds);animations.consider(bounds);
                if(input.fire)weapons.fire([&](auto glyph){auto cost=shots.spawn(owner,glyph);if(cost)++spawns;return cost;});
                if(input.use){if(dok::player_use(world)==dok::UseResult::scheduled)++used;input.use=false;}
                if(auto pickup=dok::player_pickup(world)){++pickups;if(world.object_for_owner(pickup).weapon)weapons.picked_up(pickup);}
                dok::player_sensors(world);if(floor(cell()).class_id==13)sensors.insert(cell());
                auto movement=input;movement.slow|=dok::player_surface(world);
                dok::update_player_motion(world,movement,static_cast<std::uint32_t>(world.simulation_ticks()),false);
            }else if(event==dok::GameEvent::projectile_tick)shots.event(owner);
            else if(event==dok::GameEvent::monster_tick)monsters.event(owner);
            else if(event==dok::GameEvent::weapon_tick||event==dok::GameEvent::weapon_switch)weapons.event(owner,event);
            else if(event==dok::GameEvent::base_animation)animations.event(owner);
            else dok::door_event(world,owner,event);
        });elapsed=now;flow.observe();
        if(flow.phase()==dok::SessionPhase::retry||flow.phase()==dok::SessionPhase::game_over)
            throw std::runtime_error("Route player died at "+std::to_string(cell()%world.definition.width)+","+std::to_string(cell()/world.definition.width)+" after "+std::to_string(frame)+" frames");
    }
    bool passable(int at)const{
        const auto& d=world.definition;const auto& def=terrain(at);const auto& object=world.objects.at(d.glyph_index[def.glyph]);
        const auto& occupant=world.objects.at(d.glyph_index[world.maps.at("mmap")[at]]);
        if(occupant.monster&&occupant.monster->health>0&&player().charge()==0)return false;
        if(object.flags&0x10)return true;
        return def.class_id==7&&(!def.number(0x5c)||(def.number(0x5c)&player().keys()));
    }
    int cost(int at)const{
        int value=10;const auto& f=floor(at);
        if(f.class_id==12&&f.number(0x66)==0&&f.number(0x68)<0)value+=150;
        const auto& item=terrain(at);
        if(item.class_id==3&&item.number(0x54)==0&&item.number(0x56)<0)value+=2000;
        const auto& m=*world.definition.object(world.maps.at("mmap")[at]);
        if(m.class_id==9&&world.objects.at(world.definition.glyph_index[m.glyph]).monster->health>0)value+=200;
        if(terrain(at).class_id==7)value+=5;
        return value;
    }
    struct Paths {std::vector<int> distance,previous;};
    Paths paths()const{
        const auto& d=world.definition;Paths out{std::vector<int>(d.width*d.length,100000000),std::vector<int>(d.width*d.length,-1)};
        using Entry=std::pair<int,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
        out.distance[cell()]=0;out.previous[cell()]=cell();queue.push({0,cell()});
        while(!queue.empty()){
            const auto [distance,at]=queue.top();queue.pop();if(distance!=out.distance[at])continue;
            for(int next:{at-1,at+1,at-d.width,at+d.width}){
                if(next<0||next>=int(out.distance.size())||std::abs(next%d.width-at%d.width)+std::abs(next/d.width-at/d.width)!=1||!passable(next))continue;
                const auto& current_floor=floor(at);
                if(current_floor.class_id==12&&world.definition.dataset==dok::DatasetId::old_gold){
                    const int axis=current_floor.number(0x66),drift=current_floor.number(0x68);
                    const int direction=axis==102?next/d.width-at/d.width:(axis==100||axis==101?next%d.width-at%d.width:0);
                    if(direction*drift<0&&std::abs(drift)>d.objects[world.objects.at(*world.player).definition_index].number(0x38)*3/2)continue;
                }
                const int candidate=distance+cost(next);
                if(candidate<out.distance[next]){out.distance[next]=candidate;out.previous[next]=at;queue.push({candidate,next});}
            }
        }
        return out;
    }
    bool sight(dok::Pose target)const{
        const auto from=*world.objects.at(*world.player).pose;const auto& d=world.definition;
        const int steps=std::max(std::abs(target.x-from.x),std::abs(target.z-from.z))/30+1;
        for(int i=1;i<steps;++i){const auto x=from.x+(std::int64_t(target.x-from.x)*i)/steps,z=from.z+(std::int64_t(target.z-from.z)*i)/steps;
            const int at=int(z/d.cell_side)*d.width+int(x/d.cell_side);const auto& o=world.objects.at(d.glyph_index[world.maps.at("tmap")[at]]);
            if(!(o.flags&0x18))return false;
        }return true;
    }
    dok::WorldObject* threat(){
        if(!player().weapons.at(player().current_weapon)||!player().charge())return nullptr;
        dok::WorldObject* result=nullptr;std::int64_t best=std::int64_t(world.definition.cell_side)*4;
        for(auto& o:world.objects)if(o.monster&&o.pose&&o.monster->health>0){
            const auto p=*actor().pose,q=*o.pose;const auto distance=std::max(std::abs(std::int64_t(q.x)-p.x),std::abs(std::int64_t(q.z)-p.z));
            if(distance<best&&sight(q)){result=&o;best=distance;}
        }return result;
    }
    void fight(){
        auto* enemy=threat();if(!enemy)return;
        const auto distance=std::max(std::abs(enemy->pose->x-actor().pose->x),std::abs(enemy->pose->z-actor().pose->z));
        if(resupplying&&distance>world.definition.cell_side*3/2)return;
        unsigned guard=0;const auto owner=enemy->owner;
        const auto before_health=enemy->monster->health,before_charge=player().charge();
        input={};input.fire=true;input.strafe=1;input.run=true;
        auto previous=*actor().pose;unsigned stopped=0;
        while(enemy->monster->health>0&&player().charge()>0&&flow.phase()==dok::SessionPhase::playing){
            if(++guard>60*30)throw std::runtime_error("Combat stalled against owner "+std::to_string(owner));
            aim(enemy->pose->x,enemy->pose->z);
            const auto distance=std::max(std::abs(enemy->pose->x-actor().pose->x),std::abs(enemy->pose->z-actor().pose->z));
            input.forward=distance<world.definition.cell_side*2?-0.5f:0;
            const auto safe=[&](float strafe,float forward){
                const double angle=actor().pose->heading*3.14159265358979323846/180.0;
                const int stride=world.definition.cell_side/2;
                const int x=actor().pose->x+int((-std::sin(angle)*forward+std::cos(angle)*strafe)*stride);
                const int z=actor().pose->z+int((std::cos(angle)*forward+std::sin(angle)*strafe)*stride);
                const auto& d=world.definition;if(x<0||z<0||x>=d.width*d.cell_side||z>=d.length*d.cell_side)return false;
                const int at=z/d.cell_side*d.width+x/d.cell_side;const auto& item=terrain(at);const auto& surface=floor(at);
                return passable(at)&&!(item.class_id==3&&item.number(0x54)==0&&item.number(0x56)<0)&&!(surface.class_id==12&&surface.number(0x66)==0&&surface.number(0x68)<0);
            };
            if(!safe(input.strafe,input.forward)){input.strafe=-input.strafe;if(!safe(input.strafe,input.forward)){input.strafe=0;if(!safe(0,input.forward))input.forward=0;}}
            tick();
            if(previous.x==actor().pose->x&&previous.z==actor().pose->z){if(++stopped>6){input.strafe=-input.strafe;stopped=0;}}
            else stopped=0;
            previous=*actor().pose;
            if(!sight(*enemy->pose))break;
        }
        input={};
        std::cout<<"FIGHT owner="<<owner<<" hp="<<before_health<<"->"<<enemy->monster->health<<" ammo="<<before_charge<<"->"<<player().charge()<<" frames="<<guard<<" hero="<<player().health()<<'\n';
    }
    void walk(int destination,const Paths& graph){
        if(destination<0||graph.previous[destination]<0)throw std::runtime_error("No reachable progression target");
        std::vector<int> path;for(int at=destination;at!=cell();at=graph.previous[at])path.push_back(at);std::reverse(path.begin(),path.end());
        const auto& d=world.definition;
        const auto& destination_definition=terrain(destination);
        resupplying=destination_definition.class_id==3&&(destination_definition.number(0x54)==0||destination_definition.number(0x54)==1);
        for(const int at:path){
            if(flow.phase()!=dok::SessionPhase::playing)return;
            const int tx=(at%d.width)*d.cell_side+d.cell_side/2,tz=(at/d.width)*d.cell_side+d.cell_side/2;
            const auto before_combat=cell();fight();if(cell()!=before_combat)return;aim(tx,tz);
            auto& object=world.objects.at(d.glyph_index[world.maps.at("tmap")[at]]);
            if(object.door&&!(object.flags&0x10)){
                input={};
                for(unsigned guard=0;!(object.flags&0x10)&&guard<60*8;++guard){const auto before=cell();fight();if(cell()!=before)return;aim(tx,tz);input.use=object.door->ready;tick();}
                if(!(object.flags&0x10))throw std::runtime_error("Door failed to open at "+std::to_string(at%d.width)+","+std::to_string(at/d.width));
            }
            unsigned guard=0,still=0;auto old=*actor().pose;
            while(std::abs(actor().pose->x-tx)>1||std::abs(actor().pose->z-tz)>1){
                if(flow.phase()!=dok::SessionPhase::playing)return;
                if(++guard>60*20)throw std::runtime_error("Movement stalled at "+std::to_string(cell()%d.width)+","+std::to_string(cell()/d.width)+" pose="+std::to_string(actor().pose->x)+","+std::to_string(actor().pose->z)+" target="+std::to_string(tx)+","+std::to_string(tz)+" toward "+std::to_string(at%d.width)+","+std::to_string(at/d.width));
                if(threat()){const auto before=cell();fight();if(cell()!=before)return;old=*actor().pose;}
                if(object.door&&!(object.flags&0x10)){
                    aim(tx,tz);input={};input.use=object.door->ready;tick();continue;
                }
                const int rx=tx-actor().pose->x,rz=tz-actor().pose->z;
                if(std::abs(rx)>std::abs(rz))aim(actor().pose->x+(rx>0?100:-100),actor().pose->z);
                else aim(actor().pose->x,actor().pose->z+(rz>0?100:-100));
                const auto speed=d.objects[actor().definition_index].number(0x38);
                input={};input.forward=std::min(1.0f,float(std::max(4,std::max(std::abs(rx),std::abs(rz))))/float(speed*2));input.run=true;
                tick();if(actor().pose->x==old.x&&actor().pose->z==old.z)++still;else still=0;
                old=*actor().pose;
                if(still>120){std::cerr<<"BLOCK hp="<<player().health()<<" ammo="<<player().charge()<<" targetglyph="<<int(world.maps.at("mmap")[at])<<"\n";still=0;}
            }
            input={};for(int i=0;i<8&&flow.phase()==dok::SessionPhase::playing;++i)tick();
            // Replan often: sensor-controlled doors and enemy occupancy can change.
            if(pickups&&floor(cell()).class_id==13)return;
            if(!resupplying&&(player().health()<70||(player().current_weapon&&player().charge()<55)))return;
        }
    }
    void complete(){
        for(unsigned iteration=0;iteration<2000&&flow.phase()==dok::SessionPhase::playing;++iteration){
            resupplying=player().health()<70||(player().current_weapon&&player().charge()<55);
            fight();const auto graph=paths();int target=-1,best=100000000,priority=99;
            for(int at=0;at<int(graph.distance.size());++at){
                if(at==cell()||graph.previous[at]<0)continue;
                const auto& object=terrain(at);int rank=99;
                if(object.class_id==10&&!player().weapons.at(object.number(0x81)))rank=player().current_weapon?3:0;
                if(object.class_id==3){
                    const int category=object.number(0x54),value=object.number(0x56);
                    if(category==10&&(value&~player().keys()))rank=1;
                    if(category==99&&world.definition.dataset==dok::DatasetId::old_gold)rank=4;
                    if(category==0&&value>0&&player().health()<85)rank=graph.distance[at]<400?0:3;
                    if(category==2&&value>0)rank=3;
                    if(category==1&&value>0&&player().charge()<std::max(55,int(player().max_charge())/2)&&player().max_charge()>0)rank=graph.distance[at]<500||player().charge()<20?0:3;
                }
                const auto& surface=floor(at);
                if(surface.class_id==13){
                    const auto control=static_cast<unsigned>(surface.number(0x66))&255u;
                    const auto index=world.definition.glyph_index[control];
                    const bool needs_open=index>=0&&world.objects[index].door&&!(world.objects[index].flags&0x10);
                    if(!sensors.count(at)||needs_open)rank=std::min(rank,2);
                }
                if(surface.class_id==12&&surface.number(0x66)==1000&&player().score==world.total_money)rank=0;
                if(rank<priority||(rank==priority&&graph.distance[at]<best)){target=at;best=graph.distance[at];priority=rank;}
            }
            if(target<0||priority==99)throw std::runtime_error("No progression target: keys="+std::to_string(player().keys())+" score="+std::to_string(player().score)+" hp="+std::to_string(player().health()));
            const auto& object=terrain(target);std::cout<<"TARGET "<<iteration<<" at="<<target%world.definition.width<<','<<target/world.definition.width<<" cls="<<object.class_id<<" rank="<<priority<<" keys="<<player().keys()<<" hp="<<player().health()<<" charge="<<player().charge()<<" score="<<player().score<<" frames="<<frame<<'\n'<<std::flush;
            walk(target,graph);
        }
        if(flow.phase()!=dok::SessionPhase::completed)throw std::runtime_error("Route did not reach a normal finish");
        std::cout<<"COMPLETED level="<<world.definition.name<<" frames="<<frame<<" ticks="<<world.simulation_ticks()<<" hp="<<player().health()<<" keys="<<player().keys()<<" money="<<player().score<<" pickups="<<pickups<<" shots="<<spawns<<" used="<<used<<'\n';
    }
};
}
int main(int argc,char** argv){try{
    if(argc<3||argc>4)throw std::runtime_error("Usage: dok_walkthrough_probe RES LEVEL [REPLAY]");
    const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));Route route(archive,argv[2]);std::ofstream file;
    if(argc==4){file.open(std::filesystem::u8path(argv[3]));if(!file)throw std::runtime_error("Cannot write replay");route.recording=&file;file<<"DOK_INPUT_REPLAY 1 "<<(archive.identify()==dok::DatasetId::old_gold?"old_gold":archive.identify()==dok::DatasetId::kremlin_pkr?"kremlin_pkr":"kremlin_pke")<<' '<<argv[2]<<" 0\n"<<std::setprecision(9);}
    route.complete();
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
