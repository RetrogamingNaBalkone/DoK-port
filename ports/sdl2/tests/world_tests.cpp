#include "dok/world.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool value){if(!value)throw std::runtime_error("World lifecycle vector failed");}
// Minimal synthetic valid world: real assets and seven-level comparisons are
// checked separately by check_assets.py. No renderer/gameplay equivalence claim.
static dok::LevelDefinition fixture(){
    dok::LevelDefinition level;
    level.width=level.length=2;level.height=1;
    level.cell_side=480;level.cell_height=640;
    level.glyph_index.fill(-1);
    dok::ObjectDefinition empty;empty.glyph=' ';empty.class_id=2;
    dok::ObjectDefinition hero;hero.glyph='P';hero.class_id=1;hero.height_parameter=300;
    for(int i=0;i<11;++i)hero.fields.push_back({dok::FieldKind::word,0x5e + i*2,100+i,{}});
    hero.fields.push_back({dok::FieldKind::word,0x32,90,{}});
    level.objects={empty,hero};level.glyph_index[' ']=0;level.glyph_index['P']=1;
    level.maps["tmap"]={' ',' ',' ',' '};level.maps["fcmap"]=level.maps["tmap"];
    level.maps["mmap"]={'P',' ',' ',' '};
    return level;
}
int main(){
    try{
        const auto definition=fixture();
        std::vector<std::uint64_t> reference;
        for(auto fps:{30u,60u,144u}){
            dok::World world(definition);
            const auto owner=world.objects.at(*world.player).owner;
            const auto& player=*world.object_for_owner(owner).player_state;
            check(player.lives==3&&player.score==0&&player.current_weapon==0&&player.pending_weapon==0);
            check(player.health()==100&&player.charge()==101&&player.max_health()==102&&player.keys()==110);
            for(auto weapon:player.weapons)check(weapon==0);
            check(world.events.active_owners()==1);
            check(world.events.events(owner)[0].deadline==416);
            check(world.objects[*world.player].pose->z==720);
            std::vector<std::uint64_t> trace;
            std::uint64_t previous=0;
            for(std::uint64_t frame=1;frame<=fps*3u;++frame){
                const auto elapsed=frame*1000000000ULL/fps;
                world.advance_events(elapsed-previous,[&](dok::OwnerId id,dok::CallbackId event){
                    check(id==owner&&event==static_cast<dok::CallbackId>(dok::GameEvent::player_tick));
                    trace.push_back(world.simulation_ticks());
                    ++world.object_for_owner(id).player_state->score;
                });
                previous=elapsed;
            }
            check(world.simulation_ticks()==30080);
            check(trace.size()==72&&trace.front()==416&&trace.back()==29952);
            if(reference.empty())reference=trace;else check(reference==trace);
            check(world.object_for_owner(owner).player_state->score==72);
            // A paused frame provides no elapsed time and does not dispatch.
            world.advance_events(0,[&](dok::OwnerId,dok::CallbackId){check(false);});
            check(world.events.destroy(owner));
            world.advance_events(1000000000,[&](dok::OwnerId,dok::CallbackId){check(false);});
            dok::World restarted(definition);
            check(restarted.simulation_ticks()==0);
            check(restarted.object_for_owner(owner).player_state->score==0);
            check(restarted.events.events(owner)[0].deadline==416);
        }
        std::cout<<"Player initialization and identical event traces at 30/60/144 FPS passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
