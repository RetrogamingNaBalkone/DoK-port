#include "dok/monsters.hpp"
#include "dok/weapons.hpp"
#include "dok/items.hpp"
#include "dok/movement.hpp"
#include "dok/interactions.hpp"
#include "dok/flow.hpp"
#include "dok/hud.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Usage: dok_old_gold_probe OLD_GOLD.RES");
    const auto a=dok::Archive::open(std::filesystem::u8path(argv[1]));
    dok::World w(dok::load_level(a,"LEVEL4"));dok::Weapons weapons(w,a);
    dok::Projectiles shots(w,a);dok::Monsters monsters(w,a,shots);dok::SoftwareScene scene(a,w.definition);
    dok::Hud hud(a);dok::GameFlow flow(w);
    auto& actor=w.objects.at(*w.player);auto& p=*actor.player_state;
    check(w.total_money==105,"Money total");
    dok::change_player_stat(w,1000,0);check(!w.level_complete,"Exit accepted without money");
    unsigned money=0,weapons_found=0;const auto start=*actor.pose;
    // Isolated pickup/exit integration: relocate to original cells, not a claimed playthrough.
    for(std::size_t i=0;i<w.maps.at("tmap").size();++i){
        const auto& d=*w.definition.object(w.maps.at("tmap")[i]);
        if((d.class_id==3&&d.number(0x54)==99)||d.class_id==10){
            actor.pose->x=int(i%w.definition.width)*w.definition.cell_side+w.definition.cell_side/2;
            actor.pose->z=int(i/w.definition.width)*w.definition.cell_side+w.definition.cell_side/2;
            const auto owner=dok::player_pickup(w);check(owner!=0,"Pickup rejected");
            if(d.class_id==10){weapons.picked_up(owner);++weapons_found;}else ++money;
        }
    }
    check(money==105&&p.score==105&&weapons_found==2,"Original pickups");
    check(p.max_charge()==300&&p.charge()==300,"Old Gold weapon capacity");
    actor.pose=start;
    for(auto glyph:{'1','2'}){
        check(shots.spawn(actor.owner,glyph)==1,"Old Gold shot cost");
    }
    for(const auto& shot:w.projectiles)if(shot)check(shot->pose.y==(start.y>>1)+(start.y>>2),"Old Gold launch height");
    unsigned ticks=0;
    for(int f=0;f<600;++f){
        w.advance_events(16666667,[&](dok::OwnerId owner,dok::CallbackId id){
            const auto e=static_cast<dok::GameEvent>(id);
            if(e==dok::GameEvent::projectile_tick)shots.event(owner);
            else if(e==dok::GameEvent::monster_tick)monsters.event(owner);
            else if(e==dok::GameEvent::weapon_tick||e==dok::GameEvent::weapon_switch)weapons.event(owner,e);
            else if(e==dok::GameEvent::player_tick){
                ++ticks;monsters.consider(dok::original_cell_bounds(*actor.pose,w.definition.cell_side,w.definition.width,w.definition.length));
                dok::InputActions input;input.forward=0.5f;input.strafe=0.3f;input.turn=0.4f;
                dok::update_player_motion(w,input,static_cast<std::uint32_t>(w.simulation_ticks()));
                weapons.fire([&](auto glyph){return shots.spawn(owner,glyph);});
            }else dok::door_event(w,owner,e);
        });
        auto images=monsters.images({0,0,63,63});auto projectiles=shots.images();images.insert(images.end(),projectiles.begin(),projectiles.end());
        auto frame=scene.render(w,*actor.pose,images);weapons.overlay(frame);hud.classic(frame,p);hud.overlay(frame,p);
    }
    check(ticks>200,"Simulation did not advance");
    check(actor.pose->x!=start.x||actor.pose->z!=start.z,"Player did not move");
    dok::change_player_stat(w,1000,0);check(flow.observe()==dok::SessionPhase::completed,"Money exit failed");
    std::cout<<"Old Gold: 105 money, 2 wands, shot profile, 600 rendered simulation frames, exit OK\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
