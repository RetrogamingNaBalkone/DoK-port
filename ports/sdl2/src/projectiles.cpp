#include "dok/projectiles.hpp"
#include "dok/arithmetic.hpp"
#include "dok/camera.hpp"
#include "dok/items.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
using namespace original;
namespace {
std::int16_t word(std::int32_t v){return signed16(static_cast<std::uint16_t>(v));}
void step(Pose& pose,std::int32_t distance){
    pose.x=add32(pose.x,sar32(multiply32(original_sine(pose.heading),distance),15));
    pose.z=sub32(pose.z,sar32(multiply32(original_cosine(pose.heading),distance),15));
}
}
Projectiles::Projectiles(World& world,const Archive& archive):world_(world){
    if(archive.identify()!=world.definition.dataset)
        throw std::invalid_argument("Projectile profile currently verified for Kremlin only");
    for(std::size_t i=0;i<world.objects.size();++i){const auto& d=world.definition.objects[i];
        if(d.class_id!=8)continue;
        const bool og=world.definition.dataset==DatasetId::old_gold;
        Clips clips{archive.animation(d.animation),og?archive.animation(d.animation):archive.animation(d.resource(0x77)),og?archive.animation(d.animation):archive.animation(d.resource(0x7b))};
        if(clips.flight.empty()||clips.hit.empty()||clips.miss.empty())throw std::runtime_error("Empty projectile animation");
        clips_.emplace(i,std::move(clips));
    }
}
std::size_t Projectiles::cell(Pose p)const{
    const auto& d=world_.definition;
    return std::clamp<int>(word(divide32(p.z,d.cell_side)),0,d.length-1)*d.width+
        std::clamp<int>(word(divide32(p.x,d.cell_side)),0,d.width-1);
}
const AnimationFrame& Projectiles::frame(const ProjectileState& p)const{
    const auto& c=clips_.at(p.definition_index);
    return (p.effect_frame?(p.actor_impact?c.hit:c.miss):c.flight).at(p.frame);
}
std::int16_t Projectiles::spawn(OwnerId source,std::uint8_t glyph){
    const auto index=world_.definition.glyph_index[glyph];
    if(index<0||!clips_.count(index))throw std::invalid_argument("Not a projectile template");
    const auto& actor=world_.object_for_owner(source);
    if(!actor.pose)return 0;
    for(unsigned slot=0;slot<256;++slot){
        // Static definitions are reserved. Native flags0400 marks a completed
        // dynamic projectile; this port releases its optional slot immediately
        // after cancelling the callback, so the same lowest slot is reusable.
        if(world_.definition.glyph_index[slot]>=0||world_.projectiles[slot])continue;
        const auto& d=world_.definition.objects[index];ProjectileState p;
        const bool og=world_.definition.dataset==DatasetId::old_gold;
        p.pose=*actor.pose;p.pose.y=og?add32(sar32(actor.pose->y,1),sar32(actor.pose->y,2)):negate16(d.height_parameter);
        p.definition_index=index;p.source=source;p.damage=word(d.number(og?0x5a:0x5c));
        p.damage_floor=word(sar32(p.damage,1));p.speed=sub32(0,d.number(0x3c));p.flags=d.flags|0x1000;
        world_.events.create(owner_base+slot); // Existing empty owner is reused after cancellation.
        if(!world_.events.schedule(owner_base+slot,static_cast<CallbackId>(GameEvent::projectile_tick),416,
            static_cast<std::uint32_t>(world_.simulation_ticks())))return 0;
        const auto cost=og?1:p.damage;world_.projectiles[slot]=std::move(p);return cost;
    }
    return 0;
}
void Projectiles::unoccupy(unsigned slot){
    auto& p=*world_.projectiles.at(slot);
    if(p.occupied_cell&&world_.projectile_occupancy[*p.occupied_cell]==static_cast<int>(slot))
        world_.projectile_occupancy[*p.occupied_cell]=-1;
    p.occupied_cell.reset();
}
void Projectiles::occupy(unsigned slot){
    auto& p=*world_.projectiles.at(slot);const auto at=cell(p.pose);
    if(world_.maps.at("mmap")[at]==' '&&world_.projectile_occupancy[at]<0){
        world_.projectile_occupancy[at]=static_cast<std::int16_t>(slot);p.occupied_cell=at;
    }
}
bool Projectiles::hit(WorldObject& target,const ProjectileState& projectile){
    const auto cls=world_.definition.objects[target.definition_index].class_id;
    const auto& source=world_.object_for_owner(projectile.source);
    if(cls==world_.definition.objects[source.definition_index].class_id)return false;
    if(cls==1){if(target.player_state->health()!=0)change_player_stat(world_,0,negate16(projectile.damage));return true;}
    if(cls==9){
        if(monster_hit)return monster_hit(target.owner,projectile.damage);
        auto& m=*target.monster;if(!m.health)return false;
        if(!m.state)m.state=1; // First impact wakes without damage; AI scheduling follows in SP12.
        else{m.health=std::max<std::int16_t>(0,word(m.health-projectile.damage));m.state=3;}
        return true;
    }
    if(cls==7)return !(target.flags&8);
    return !(target.flags&0x18);
}
void Projectiles::event(OwnerId owner){
    if(owner<owner_base||owner>=owner_base+256||!world_.projectiles[owner-owner_base])
        throw std::invalid_argument("Invalid projectile callback");
    const auto slot=owner-owner_base;auto& p=*world_.projectiles[slot];
    const auto& clips=clips_.at(p.definition_index);
    if(p.impact){
        p.effect_frame=true;
        const auto& effect=p.actor_impact?clips.hit:clips.miss;
        ++p.frame;
        if(p.frame>=static_cast<int>(effect.size())){
            if(world_.audio)world_.audio({AudioAction::stop,sound_id(owner),{}});
            unoccupy(slot);world_.events.cancel_current();world_.projectiles[slot].reset();return;
        }
        world_.events.set_first_interval(signed16(effect[p.frame].interval));return;
    }
    unoccupy(slot);
    const auto now=static_cast<std::uint32_t>(world_.simulation_ticks());p.clock.stop(now);
    const bool og=world_.definition.dataset==DatasetId::old_gold;
    const auto scale=std::clamp<std::uint32_t>((p.clock.read(now)<<8)/416,256,og?1536:2048);
    auto distance=sar32(multiply32(word(p.speed),scale),8);
    const auto quarter=sar32(-world_.definition.cell_side,2);
    int count=divide32(distance,quarter)+1;
    if(og){count=1;while(distance<quarter){distance=sar32(distance,1);++count;}}
    bool contact=false;
    for(int part=0;part<count;++part){
        step(p.pose,og?distance:(part?quarter:distance%quarter));
        const auto at=cell(p.pose);
        auto* target=&world_.objects.at(world_.definition.glyph_index[world_.maps.at("mmap")[at]]);
        const auto cls=world_.definition.objects[target->definition_index].class_id;
        if(cls==0||cls==8)target=&world_.objects.at(world_.definition.glyph_index[world_.maps.at("tmap")[at]]);
        const bool outside=p.pose.x<0||p.pose.z<0||p.pose.x>=world_.definition.width*world_.definition.cell_side||
            p.pose.z>=world_.definition.length*world_.definition.cell_side;
        if(outside||hit(*target,p)){
            contact=true;const auto target_class=world_.definition.objects[target->definition_index].class_id;
            p.actor_impact=!outside&&(target_class==1||target_class==9);
            world_.events.set_first_interval(signed16(clips.flight.at(p.frame).interval));
            step(p.pose,og?-distance:-quarter);
            const auto target_height=world_.definition.objects[target->definition_index].height_parameter;
            if(!outside&&target_height>0){
                const auto image_height=clips.flight.at(p.frame).image.height;
                const auto scale_word=world_.definition.objects[p.definition_index].width_parameter;
                p.pose.y=-(target_height-sar32(word(image_height*scale_word),1));
            }
            p.frame=og?0:1;break;
        }
    }
    // Native get-next loops past end to frame0, even on the contact tick.
    if(!og&&++p.frame>=static_cast<int>(clips.flight.size()))p.frame=0;
    p.damage=word(sar32(word(p.damage+p.damage_floor),1));p.clock.start(now);
    occupy(slot);
    if(contact){
        if(world_.audio)world_.audio({AudioAction::once,sound_id(owner),world_.definition.objects[p.definition_index].sound});
        // Retain flight image on this tick, then start effect at the next frame.
        p.impact=true;
    }
}
std::size_t Projectiles::active()const{
    return std::count_if(world_.projectiles.begin(),world_.projectiles.end(),[](const auto& p){return p.has_value();});
}
std::vector<PositionedImage> Projectiles::images()const{
    std::vector<PositionedImage> result;
    for(const auto& p:world_.projectiles)if(p){
        const auto& d=world_.definition.objects[p->definition_index];
        result.push_back({p->pose,&frame(*p).image,d.width_parameter,p->flags});
    }
    return result;
}
}
