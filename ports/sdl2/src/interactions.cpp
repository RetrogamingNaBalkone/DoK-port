#include "dok/interactions.hpp"
#include "dok/arithmetic.hpp"
#include "dok/camera.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
namespace {
#include "door_messages.inc"
const char* message(DatasetId profile,unsigned index){
    return (profile==DatasetId::old_gold?door_old_gold:profile==DatasetId::kremlin_pkr?door_ru:door_en)[index];
}
void profile(const World& w){
    if(w.definition.dataset==DatasetId::unknown)
        throw std::invalid_argument("Unknown interaction profile");
}
bool schedule(World& w,OwnerId owner,GameEvent event,std::int32_t delay){
    return w.events.schedule(owner,static_cast<CallbackId>(event),delay,static_cast<std::uint32_t>(w.simulation_ticks()));
}
// DOS interval zero means next rendered frame. Fixed 30-phase reference:
// 334 PIT ticks per animation step, independent of display refresh rate.
std::int32_t period(const DoorState& door){return door.interval?door.interval:334;}
}
UseResult use_door(World& world,OwnerId target,OwnerId source){
    profile(world);
    auto& object=world.object_for_owner(target);
    if(!object.door||!object.door->ready)return UseResult::ignored;
    auto& door=*object.door;door.extent=30;
    const auto& definition=world.definition.objects[object.definition_index];
    const auto& actor=world.object_for_owner(source);
    const auto keys=world.definition.objects[actor.definition_index].class_id==13?0xffff:
        (actor.player_state?actor.player_state->keys():0);
    const auto mask=static_cast<std::uint16_t>(definition.number(0x5c));
    if(mask&&!(mask&keys)){
        world.sound(target,AudioAction::once);
        world.notify(message(world.definition.dataset,mask==1?2:mask==2?3:mask==4?4:mask==8?5:6));
        return UseResult::locked;
    }
    if(door.state==2){
        bool ok=schedule(world,target,GameEvent::door_open,416);
        if(definition.number(0x5e)==1)ok=schedule(world,target,GameEvent::door_auto,definition.number(0x66))&&ok;
        return ok?UseResult::scheduled:UseResult::queue_full;
    }
    if(definition.number(0x5e)==0)
        return schedule(world,target,GameEvent::door_close,416)?UseResult::scheduled:UseResult::queue_full;
    return UseResult::ignored;
}
UseResult player_use(World& world){
    profile(world);
    const auto& player=world.objects.at(*world.player);const auto& p=*player.pose;
    using namespace original;
    const auto side=world.definition.cell_side;
    const auto x=add32(p.x,sar32(multiply32(original_sine(p.heading),-side),15));
    const auto z=sub32(p.z,sar32(multiply32(original_cosine(p.heading),-side),15));
    const auto col=std::clamp<int>(signed16(static_cast<std::uint16_t>(divide32(x,side))),0,world.definition.width-1);
    const auto row=std::clamp<int>(signed16(static_cast<std::uint16_t>(divide32(z,side))),0,world.definition.length-1);
    const auto glyph=world.maps.at("tmap")[row*world.definition.width+col];
    return use_door(world,world.objects.at(world.definition.glyph_index[glyph]).owner,player.owner);
}
void player_sensors(World& world){
    profile(world);
    auto& player=world.objects.at(*world.player);const auto& p=*player.pose;
    const auto& d=world.definition;
    const auto x=std::clamp(p.x/d.cell_side,0,d.width-1),z=std::clamp(p.z/d.cell_side,0,d.length-1);
    auto& surface=world.objects.at(d.glyph_index[world.maps.at("fcmap")[z*d.width+x]]);
    auto& previous=player.player_state->native_da; // Portable owner ID replaces native far pointer.
    const auto& definition=d.objects[surface.definition_index];
    if(definition.class_id==13&&previous!=surface.owner){
        const auto bits=static_cast<std::uint32_t>(definition.number(0x67));
        if(bits&0x800){
            const auto glyph=static_cast<std::uint8_t>(definition.number(0x66));
            use_door(world,world.objects.at(d.glyph_index[glyph]).owner,surface.owner);
        }
    }
    previous=surface.owner;
}
void door_event(World& world,OwnerId owner,GameEvent event){
    profile(world);
    auto& object=world.object_for_owner(owner);
    if(!object.door)throw std::logic_error("Door callback on non-door");
    auto& door=*object.door;
    const auto& definition=world.definition.objects[object.definition_index];
    const bool old_gold=world.definition.dataset==DatasetId::old_gold;
    switch(event){
    case GameEvent::door_open:
        if(door.state==2&&door.phase<door.extent-1){
            world.sound(owner,AudioAction::loop);
            if(old_gold)door.saved_flags=object.flags;
            object.flags|=8;if(old_gold)object.flags&=~2;
            door.ready=false;world.events.set_first_interval(period(door));
            ++door.phase;if(!old_gold&&door.phase>door.extent/4)object.flags|=0x10;
        }else{
            door.state=1;object.flags|=0x10;door.ready=true;world.events.cancel_current();
            world.sound(owner,AudioAction::stop);
            world.notify(message(world.definition.dataset,0));
        }
        break;
    case GameEvent::door_close:
        if(object.door_occupants>0)break;
        if(door.state==1&&door.phase>0){
            if(old_gold)object.flags&=~0x10;
            world.sound(owner,AudioAction::loop);
            --door.phase;door.ready=false;world.events.set_first_interval(period(door));
        }else{
            door.state=2;if(!old_gold)door.phase=0;
            object.flags=door.saved_flags;door.ready=true;world.events.cancel_current();
            world.sound(owner,AudioAction::stop);
            world.notify(message(world.definition.dataset,1));
        }
        break;
    case GameEvent::door_auto:
        if(object.door_occupants<1&&door.state==1&&door.ready){
            // Like the original, a full queue can lose this scheduling request.
            schedule(world,owner,GameEvent::door_close,definition.number(0x66));
            world.events.cancel_current();
        }
        break;
    default:throw std::invalid_argument("Not a door callback");
    }
}
Image door_image(const Image& source,const DoorState& state){
    if(state.extent<1||state.phase<0||state.phase>=state.extent)throw std::invalid_argument("Invalid door crop state");
    Image result=source;
    if(!state.phase)return result;
    const auto modes=source.flags&0x3fc0;
    if(modes!=0x40&&modes!=0x100)throw std::invalid_argument("Unsupported door crop mode");
    const unsigned dx=modes==0x40?(source.width/state.extent)*state.phase:0;
    const unsigned dy=modes==0x100?(source.height/state.extent)*state.phase:0;
    for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x)
        result.pixels[y*source.width+x]=x<dx||y<dy?0:source.pixels[(y-dy)*source.width+x-dx];
    return result;
}
}
