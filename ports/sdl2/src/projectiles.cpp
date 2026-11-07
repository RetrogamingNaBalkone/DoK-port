#include "dok/projectiles.hpp"
#include "dok/arithmetic.hpp"
#include "dok/camera.hpp"
#include "dok/items.hpp"
#include <algorithm>
#include <cmath>
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
    // OG +50 is also the impact cursor: contact sets it to 1 while the
    // renderer retains flight image0 until the next animation callback.
    const auto frame_index=world_.definition.dataset==DatasetId::old_gold&&p.impact&&!p.effect_frame?0:p.frame;
    return (p.effect_frame?(p.actor_impact?c.hit:c.miss):c.flight).at(frame_index);
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
    if(cls==1){damage_player(world_,negate16(projectile.damage));return true;}
    if(cls==9){
        if(monster_hit)return monster_hit(target.owner,projectile.damage);
        auto& m=*target.monster;if(!m.health)return false;
        if(!m.state)m.state=1; // First impact wakes without damage; AI scheduling follows in SP12.
        else{m.health=std::max<std::int16_t>(0,word(m.health-projectile.damage));m.state=3;}
        return true;
    }
    return blocks_terrain(target);
}
bool Projectiles::blocks_terrain(const WorldObject& target)const{
    const auto cls=world_.definition.objects[target.definition_index].class_id;
    if(cls==7)return world_.definition.dataset==DatasetId::old_gold?
        target.door->state==2:!(target.flags&8);
    return !(target.flags&0x18);
}
bool Projectiles::clear_to_monster(Pose from,Pose to)const{
    const auto& d=world_.definition;const int side=d.cell_side;
    const auto clear=[&](int x,int z){
        if(x<0||z<0||x>=d.width||z>=d.length)return false;
        for(const auto* map:{"mmap","tmap"}){
            const auto glyph=world_.maps.at(map)[z*d.width+x];
            const auto& o=world_.objects.at(d.glyph_index[glyph]);
            const auto cls=d.objects[o.definition_index].class_id;
            if(cls!=1&&cls!=9&&cls!=8&&blocks_terrain(o))return false;
        }
        return true;
    };
    int x=from.x/side,z=from.z/side;const int end_x=to.x/side,end_z=to.z/side;
    const std::int64_t dx=std::int64_t(to.x)-from.x,dz=std::int64_t(to.z)-from.z;
    const int sx=dx>0?1:-1,sz=dz>0?1:-1;
    const auto ax=std::abs(dx),az=std::abs(dz);
    std::int64_t nx=dx>0?(x+1)*side-from.x:from.x-x*side;
    std::int64_t nz=dz>0?(z+1)*side-from.z:from.z-z*side;
    // Supercover traversal: touching a solid corner must not expose its far side.
    for(;;){
        if(!clear(x,z))return false;
        if(x==end_x&&z==end_z)return true;
        if(x==end_x){z+=sz;nz+=side;}
        else if(z==end_z){x+=sx;nx+=side;}
        else if(nx*az<nz*ax){x+=sx;nx+=side;}
        else if(nx*az>nz*ax){z+=sz;nz+=side;}
        else{
            if(!clear(x+sx,z)||!clear(x,z+sz))return false;
            x+=sx;z+=sz;nx+=side;nz+=side;
        }
    }
}
WorldObject* Projectiles::nearby_hit(const ProjectileState& p,Pose from){
    const auto& d=world_.definition;const int radius=d.min_distance;
    if(radius<=0)return nullptr;
    const double dx=double(p.pose.x)-from.x,dz=double(p.pose.z)-from.z,length=dx*dx+dz*dz;
    const int x0=std::max(0,(std::min(from.x,p.pose.x)-radius)/d.cell_side);
    const int x1=std::min(d.width-1,(std::max(from.x,p.pose.x)+radius)/d.cell_side);
    const int z0=std::max(0,(std::min(from.z,p.pose.z)-radius)/d.cell_side);
    const int z1=std::min(d.length-1,(std::max(from.z,p.pose.z)+radius)/d.cell_side);
    std::array<bool,256> seen{};
    std::vector<std::pair<double,WorldObject*>> candidates;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){
        const auto glyph=world_.maps.at("mmap")[z*d.width+x];if(seen[glyph])continue;seen[glyph]=true;
        auto& o=world_.objects.at(d.glyph_index[glyph]);
        if(!o.monster||!o.pose||o.monster->health<=0)continue;
        const double mx=double(o.pose->x)-from.x,mz=double(o.pose->z)-from.z;
        const double projection=length?(mx*dx+mz*dz)/length:0;
        const double closest=std::clamp(projection,0.0,1.0);
        const double ex=mx-closest*dx,ez=mz-closest*dz;
        if(ex*ex+ez*ez>double(radius)*radius)continue;
        const double perpendicular=std::max(0.0,mx*mx+mz*mz-projection*projection*length);
        const double entry=length?std::clamp(projection-std::sqrt(std::max(0.0,double(radius)*radius-perpendicular)/length),0.0,1.0):0;
        candidates.push_back({entry,&o});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
        return a.first!=b.first?a.first<b.first:a.second->owner<b.second->owner;
    });
    for(const auto& candidate:candidates){
        // Use integer sample endpoints for occlusion, independent of floating
        // point rounding at a circle entry. A native terrain impact has priority.
        if(clear_to_monster(from,p.pose)&&clear_to_monster(p.pose,*candidate.second->pose)&&hit(*candidate.second,p))return candidate.second;
    }
    return nullptr;
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
        const auto previous=p.pose;
        step(p.pose,og?distance:(part?quarter:distance%quarter));
        const auto at=cell(p.pose);
        auto* target=&world_.objects.at(world_.definition.glyph_index[world_.maps.at("mmap")[at]]);
        const auto cls=world_.definition.objects[target->definition_index].class_id;
        if(cls==0||cls==8)target=&world_.objects.at(world_.definition.glyph_index[world_.maps.at("tmap")[at]]);
        const bool outside=p.pose.x<0||p.pose.z<0||p.pose.x>=world_.definition.width*world_.definition.cell_side||
            p.pose.z>=world_.definition.length*world_.definition.cell_side;
        bool struck=outside||hit(*target,p);
        if(!struck&&boundary_hits)if(auto* monster=nearby_hit(p,previous)){target=monster;struck=true;}
        if(struck){
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
            // Original OG +50 becomes 1 at contact; get-next preincrements it,
            // so its first effect image is frame2, not frame1.
            p.frame=1;break;
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
