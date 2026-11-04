#include "dok/flow.hpp"
#include "dok/arithmetic.hpp"
#include "dok/movement.hpp"
#include <algorithm>
#include <queue>
#include <cstdlib>
#include <stdexcept>
namespace dok {
GameFlow::GameFlow(World& world):world_(world),spawn_(*world.objects.at(*world.player).pose){
}
SessionPhase GameFlow::observe(){
    if(phase_!=SessionPhase::playing)return phase_;
    auto& player=*world_.objects.at(*world_.player).player_state;
    if(world_.level_complete||player.health()<0)phase_=SessionPhase::completed;
    else if(player.health()==0){
        // OG 1ae1:15c0 calls global stop 18a6:0280 before once 18a6:01f7.
        if(world_.definition.dataset==DatasetId::old_gold)
            world_.sound(world_.objects.at(*world_.player).owner,AudioAction::stop_all);
        world_.sound(world_.objects.at(*world_.player).owner,AudioAction::once,0xa6);
        player.lives=original::signed16(static_cast<std::uint16_t>(player.lives-1));
        phase_=player.lives==0?SessionPhase::game_over:SessionPhase::retry;
    }
    return phase_;
}
bool GameFlow::retry(){
    if(phase_!=SessionPhase::retry)return false;
    auto& actor=world_.objects.at(*world_.player);
    const auto& d=world_.definition;const auto glyph=d.objects[actor.definition_index].glyph;
    const auto cell=[&](Pose p){return std::clamp(p.z/d.cell_side,0,d.length-1)*d.width+
        std::clamp(p.x/d.cell_side,0,d.width-1);};
    auto& map=world_.maps.at("mmap");const auto old_cell=cell(*actor.pose);
    const bool owned=map.at(old_cell)==glyph;
    // Validate the complete destination before changing pose, HP or occupancy.
    // Native unconditionally overwrites the spawn MMap byte; preserve a live
    // actor and use the nearest free cell instead of creating an overlap.
    if(owned)map.at(old_cell)=' ';
    auto destination=spawn_;
    if(!footprint_clear(world_,destination.x,destination.z)){
        std::vector<bool> seen(d.width*d.length,false);std::queue<int> queue;
        const auto origin=cell(spawn_);seen[origin]=true;queue.push(origin);bool found=false;
        while(!queue.empty()&&!found){const int at=queue.front();queue.pop();
            for(const int next:{at-1,at+1,at-d.width,at+d.width}){
                if(next<0||next>=int(seen.size())||seen[next]||std::abs(next%d.width-at%d.width)+std::abs(next/d.width-at/d.width)!=1)continue;
                seen[next]=true;const int x=(next%d.width)*d.cell_side+d.cell_side/2,z=(next/d.width)*d.cell_side+d.cell_side/2;
                if(!footprint_clear(world_,x,z)){
                    const auto terrain=world_.maps.at("tmap")[next];
                    if(world_.objects.at(d.glyph_index[terrain]).flags&0x10)queue.push(next);
                    continue;
                }
                destination.x=x;destination.z=z;found=true;break;
            }
        }
        if(!found){if(owned)map.at(old_cell)=glyph;return false;}
    }
    const auto new_cell=cell(destination);
    if(owned){
        auto& old_terrain=world_.objects.at(d.glyph_index[world_.maps.at("tmap")[old_cell]]);
        if(old_terrain.door&&old_terrain.door_occupants>0)--old_terrain.door_occupants;
    }
    actor.pose=destination;map.at(new_cell)=glyph;
    auto& new_terrain=world_.objects.at(d.glyph_index[world_.maps.at("tmap")[new_cell]]);
    if(new_terrain.door)++new_terrain.door_occupants;
    actor.player_state->attributes[0]=actor.player_state->max_health();
    phase_=SessionPhase::playing;
    return true;
}
}
