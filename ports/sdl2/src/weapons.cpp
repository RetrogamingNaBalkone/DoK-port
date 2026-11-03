#include "dok/weapons.hpp"
#include "dok/items.hpp"
#include "dok/arithmetic.hpp"
#include <stdexcept>

namespace dok {
Weapons::Weapons(World& world,const Archive& archive):world_(world){
    if(archive.identify()!=world.definition.dataset)
        throw std::invalid_argument("Weapon profile currently verified for Kremlin only");
    for(const auto& object:world.objects)if(object.weapon){
        auto frames=archive.animation(world.definition.objects[object.definition_index].animation);
        if(frames.empty())throw std::runtime_error("Empty weapon animation");
        animations_.emplace(object.owner,std::move(frames));
    }
}
const AnimationFrame& Weapons::frame(OwnerId owner)const{
    return animations_.at(owner).at(world_.object_for_owner(owner).weapon->frame);
}
void Weapons::show(OwnerId owner){
    auto& weapon=*world_.object_for_owner(owner).weapon;
    if(weapon.state!=4)return;
    weapon.frame=0;weapon.state=1;weapon.shift=2;
    if(!world_.events.schedule(owner,static_cast<CallbackId>(GameEvent::weapon_tick),
        original::signed16(frame(owner).interval),static_cast<std::uint32_t>(world_.simulation_ticks())))
        throw std::runtime_error("Cannot schedule weapon animation");
}
void Weapons::hide_current(){
    auto& player=*world_.objects.at(*world_.player).player_state;
    auto& weapon=*world_.object_for_owner(player.weapons.at(player.current_weapon)).weapon;
    weapon.state=0;weapon.shift=0;weapon.frame=0;
    if(!world_.events.schedule(world_.objects.at(*world_.player).owner,static_cast<CallbackId>(GameEvent::weapon_switch),416,
        static_cast<std::uint32_t>(world_.simulation_ticks())))throw std::runtime_error("Cannot schedule weapon switch");
}
void Weapons::picked_up(OwnerId owner){
    auto& acquired=world_.object_for_owner(owner).weapon->acquired;
    if(acquired)return;
    acquired=true;
    auto& player=*world_.objects.at(*world_.player).player_state;
    const auto current=player.weapons.at(player.current_weapon);
    if(current==owner){show(owner);return;}
    if(current&&(*world_.object_for_owner(current).weapon).state&4)hide_current();
}
bool Weapons::select(int slot){
    auto& player=*world_.objects.at(*world_.player).player_state;
    player.pending_weapon=player.current_weapon;
    if(slot<0||slot>=static_cast<int>(player.weapons.size())||!player.weapons[slot]||slot==player.current_weapon)return false;
    player.pending_weapon=static_cast<std::int16_t>(slot);
    const auto current=player.weapons.at(player.current_weapon);
    if(!current){player.current_weapon=player.pending_weapon;show(player.weapons[slot]);}
    else if(world_.object_for_owner(current).weapon->state&4)hide_current();
    return true;
}
void Weapons::grant_all(){
    auto& player=*world_.objects.at(*world_.player).player_state;
    player.attributes[10]|=7;
    for(auto& object:world_.objects)if(object.weapon){
        const auto& definition=world_.definition.objects[object.definition_index];
        const int slot=definition.number(0x81);
        if(slot<=0||slot>=static_cast<int>(player.weapons.size())||player.weapons[slot])continue;
        // Give the same capacity/charge benefit as a first ordinary weapon pickup.
        change_player_stat(world_,static_cast<std::int16_t>(definition.number(0x54)),
                           static_cast<std::int16_t>(definition.number(0x56)));
        player.weapons[slot]=object.owner;object.weapon->acquired=true;
    }
    if(!player.weapons.at(player.current_weapon)){
        for(int slot=1;slot<static_cast<int>(player.weapons.size());++slot)if(player.weapons[slot]){
            player.current_weapon=player.pending_weapon=static_cast<std::int16_t>(slot);
            show(player.weapons[slot]);break;
        }
    }
    // CP866: Ах ты хитрый думер!
    world_.notify("\x80\xe5 \xe2\xeb \xe5\xa8\xe2\xe0\xeb\xa9 \xa4\xe3\xac\xa5\xe0!");
}
bool Weapons::cycle(int direction){
    const auto& player=*world_.objects.at(*world_.player).player_state;
    for(int offset=1;offset<9;++offset){
        const int slot=(player.current_weapon+(direction<0?-offset:offset)+9)%9;
        if(player.weapons[slot])return select(slot);
    }
    return false;
}
void Weapons::event(OwnerId owner,GameEvent event){
    if(event==GameEvent::weapon_switch){
        auto& player=*world_.object_for_owner(owner).player_state;
        if(world_.object_for_owner(player.weapons.at(player.current_weapon)).weapon->state==4){
            player.current_weapon=player.pending_weapon;show(player.weapons.at(player.current_weapon));world_.events.cancel_current();
        }
        return;
    }
    if(event!=GameEvent::weapon_tick)throw std::invalid_argument("Not a weapon callback");
    auto& object=world_.object_for_owner(owner);auto& weapon=*object.weapon;
    if(weapon.state==1){if(--weapon.shift==0)weapon.state|=4;return;}
    if(weapon.state==0){if(++weapon.shift==3){weapon.state|=4;world_.events.cancel_current();}return;}
    const auto last=static_cast<int>(animations_.at(owner).size())-1;
    if(weapon.state==2){if(weapon.frame>=last)weapon.state|=4;else ++weapon.frame;}
    else{
        weapon.state=3;
        if(!weapon.frame)weapon.state|=4;
        else if(world_.definition.dataset==DatasetId::old_gold)--weapon.frame;
        else if(weapon.frame>=last)weapon.frame=0;
        else ++weapon.frame;
    }
    object.flags|=8;
    world_.events.set_first_interval(original::signed16(frame(owner).interval));
}
bool Weapons::fire(const std::function<std::int16_t(std::uint8_t)>& spawn){
    auto& player=*world_.objects.at(*world_.player).player_state;
    const auto owner=player.weapons.at(player.current_weapon);
    if(!owner||player.charge()==0||!spawn)return false;
    auto& weapon=*world_.object_for_owner(owner).weapon;
    if(weapon.state!=7&&weapon.state!=3)return false;
    weapon.state=2;
    const auto cost=spawn(static_cast<std::uint8_t>('0'+world_.definition.objects[world_.object_for_owner(owner).definition_index].number(0x81)));
    change_player_stat(world_,1,original::negate16(cost));
    if(cost)world_.sound(owner,AudioAction::once);
    return cost!=0;
}
void Weapons::overlay(IndexedSurface& target)const{
    const auto& player=*world_.objects.at(*world_.player).player_state;
    const auto owner=player.weapons.at(player.current_weapon);if(!owner)return;
    const auto& weapon=*world_.object_for_owner(owner).weapon;if(weapon.state==4)return;
    const auto& image=frame(owner).image;
    // Verified VGA scale=8, surface scale=1. Inclusive native viewport bounds.
    const int left=(target.width()-1-static_cast<int>(image.width))/2;
    const int top=target.height()-1-((static_cast<int>(image.height)-2)>>weapon.shift);
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){
        const int dx=left+x,dy=top+y;const auto color=image.pixels[y*image.width+x];
        if(color&&dx>=0&&dy>=0&&dx<target.width()&&dy<target.height())target.pixels[dy*target.width()+dx]=color;
    }
}
}
