#include "dok/flow.hpp"
#include "dok/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
namespace dok {
GameFlow::GameFlow(World& world):world_(world),spawn_(*world.objects.at(*world.player).pose){
}
SessionPhase GameFlow::observe(){
    if(phase_!=SessionPhase::playing)return phase_;
    auto& player=*world_.objects.at(*world_.player).player_state;
    if(world_.level_complete||player.health()<0)phase_=SessionPhase::completed;
    else if(player.health()==0){
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
    auto& map=world_.maps.at("mmap");
    // Do not erase another actor if a malformed/overlapping occupancy replaced us.
    if(map.at(cell(*actor.pose))==glyph)map.at(cell(*actor.pose))=' ';
    actor.pose=spawn_;map.at(cell(spawn_))=glyph;
    actor.player_state->attributes[0]=actor.player_state->max_health();
    phase_=SessionPhase::playing;
    return true;
}
}
