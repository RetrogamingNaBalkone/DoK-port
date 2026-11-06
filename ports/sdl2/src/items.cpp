#include "dok/items.hpp"
#include "dok/arithmetic.hpp"
#include "dok/movement.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
namespace {
void profile(const World& w){
    if(w.definition.dataset==DatasetId::unknown)
        throw std::invalid_argument("Item profile currently verified for Kremlin only");
}
std::int16_t add(std::int16_t a,std::int16_t b){
    return original::signed16(static_cast<std::uint16_t>(std::uint16_t(a)+std::uint16_t(b)));
}
}
bool change_player_stat(World& world,std::int16_t category,std::int16_t value){
    profile(world);
    auto& p=*world.objects.at(*world.player).player_state;
    if(category==0||category==1){
        auto& current=p.attributes[category];const auto maximum=p.attributes[category+2];
        if(value>0&&current==maximum)return false;
        current=add(current,value);
        if(current<0)current=0;else if(current>maximum)current=maximum;
    }else if(category==2||category==3){
        auto& maximum=p.attributes[category];maximum=add(maximum,value);p.attributes[category-2]=maximum;
    }else if(category==10){
        p.attributes[10]=original::signed16(p.keys()|static_cast<std::uint16_t>(value));
    }else if(category>=100&&category<=102){
        auto& actor=world.objects.at(*world.player);const auto pose=*actor.pose;
        const auto x=category==102?pose.x:original::add32(pose.x,value);
        const auto z=category==102?original::add32(pose.z,value):pose.z;
        move_actor_to(world,actor.owner,x,z);
    }else if(category==99){p.score=add(p.score,value);
    }else if(category==200){world.map_revealed=true;
    }else if(category==1000){
        if(p.score==world.total_money){p.attributes[0]=-1;world.level_complete=true;return false;}
        // Native fallback writes outside the player's allocation when the
        // exit condition fails. Do not reproduce that memory corruption.
        return true;
    }else if(category>=4&&category<=9){p.attributes[category]=add(p.attributes[category],value);
    }else if(category>=12&&category<=14){p.extra_attributes[category-12]=add(p.extra_attributes[category-12],value);
    }else throw std::invalid_argument("Unsupported player stat category");
    return true;
}
void damage_player(World& world,std::int16_t value){
    auto& actor=world.objects.at(*world.player);
    const auto health=actor.player_state->health();
    if(!health)return;
    const bool old_gold=world.definition.dataset==DatasetId::old_gold;
    // OG 1ae1:182b bypasses its same-identity gate at every fifth HP.
    // Both profiles request audio before applying the signed stat delta.
    if(old_gold&&health%5==0){
        world.sound(actor.owner,AudioAction::stop_all);
        world.sound(actor.owner,AudioAction::once,0xa6);
    }else world.sound(actor.owner,old_gold?AudioAction::priority_once:AudioAction::once,0xa6);
    change_player_stat(world,0,value);
}
OwnerId player_pickup(World& world){
    profile(world);
    auto& actor=world.objects.at(*world.player);const auto& pose=*actor.pose;
    const auto& d=world.definition;
    const auto x=std::clamp(pose.x/d.cell_side,0,d.width-1),z=std::clamp(pose.z/d.cell_side,0,d.length-1);
    auto& glyph=world.maps.at("tmap")[z*d.width+x];
    const auto& object=world.objects.at(d.glyph_index[glyph]);const auto& definition=d.objects[object.definition_index];
    if(definition.class_id!=3&&definition.class_id!=10)return 0;
    const auto category=static_cast<std::int16_t>(definition.number(0x54));
    const bool accepted=change_player_stat(world,category,static_cast<std::int16_t>(definition.number(0x56)));
    if(definition.class_id==3&&!accepted)return 0;
    world.notify(definition,0x58);
    if(world.definition.dataset==DatasetId::old_gold)world.sound(object.owner,AudioAction::priority_once);
    else {world.sound(object.owner,AudioAction::stop_all);world.sound(object.owner,AudioAction::once);}
    if(category!=1000)glyph=' ';
    if(definition.class_id==10){
        auto& p=*actor.player_state;const auto slot=definition.number(0x81);
        if(slot<0||slot>=static_cast<int>(p.weapons.size()))throw std::runtime_error("Invalid weapon slot");
        p.pending_weapon=static_cast<std::int16_t>(slot);
        if(!p.weapons[slot]){
            p.weapons[slot]=object.owner;
            if(!p.weapons.at(p.current_weapon))p.current_weapon=p.pending_weapon;
            // An occupied current slot needs the recovered hide/show state
            // machine before committing pending_weapon (next SP11 step).
        }
    }
    return object.owner;
}
bool player_surface(World& world){
    profile(world);
    const auto& pose=*world.objects.at(*world.player).pose;const auto& d=world.definition;
    const auto x=std::clamp(pose.x/d.cell_side,0,d.width-1),z=std::clamp(pose.z/d.cell_side,0,d.length-1);
    const auto& surface=*d.object(world.maps.at("fcmap")[z*d.width+x]);
    if(surface.class_id!=12)return false;
    // OG 1ae1:1e16 applies +68 directly and does not request slow movement.
    // Kremlin divides it by measured FPS and slows movement on class12.
    // The port stabilizes that Kremlin divisor at the accepted 30 FPS profile.
    const bool old_gold=world.definition.dataset==DatasetId::old_gold;
    const auto category=static_cast<std::int16_t>(surface.number(0x66));
    const auto value=static_cast<std::int16_t>(old_gold?surface.number(0x68):surface.number(0x68)/30);
    if(old_gold&&category>=100&&category<=102){
        const auto owner=world.objects.at(*world.player).owner;
        move_actor_current(world,owner,value,category==102);
    }else change_player_stat(world,category,value);
    return !old_gold; // Original class12 never removes its FCMap cell.
}
}
