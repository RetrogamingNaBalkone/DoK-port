#include "dok/movement.hpp"
#include "dok/camera.hpp"
#include "dok/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dok {
using namespace original;
namespace {
std::int16_t word(std::int32_t value){return signed16(static_cast<std::uint16_t>(value));}
std::size_t cell(const World& world,std::int32_t x,std::int32_t z){
    const auto& d=world.definition;
    auto col=std::clamp<int>(word(divide32(x,d.cell_side)),0,d.width-1);
    auto row=std::clamp<int>(word(divide32(z,d.cell_side)),0,d.length-1);
    return static_cast<std::size_t>(row)*d.width+col;
}
WorldObject& object(World& world,std::uint8_t glyph){return world.objects.at(world.definition.glyph_index[glyph]);}
int heading(int value){value%=360;return value<0?value+360:value;}
void require_profile(const World& world){
    if(world.definition.dataset==DatasetId::unknown)
        throw std::invalid_argument("Movement profile has not been verified for this dataset");
}
}
bool footprint_clear(const World& world,std::int32_t x,std::int32_t z){
    const auto r=world.definition.min_distance;
    for(auto dx:{-r,r})for(auto dz:{-r,r}){
        const auto slot=world.projectile_occupancy[cell(world,add32(x,dx),add32(z,dz))];
        if(slot>=0&&world.projectiles[slot]&&!(world.projectiles[slot]->flags&0x10))return false;
    }
    for(const char* name:{"mmap","tmap"})for(auto dx:{-r,r})for(auto dz:{-r,r}){
        const auto glyph=world.maps.at(name)[cell(world,add32(x,dx),add32(z,dz))];
        const auto index=world.definition.glyph_index[glyph];
        if(index<0||!(world.objects.at(index).flags&0x10))return false;
    }
    return true;
}
Pose resolve_movement(const World& world,Pose from,std::int32_t nx,std::int32_t nz){
    const auto side=world.definition.cell_side,r=world.definition.min_distance;
    const auto ox=from.x,oz=from.z;
    const auto dx=std::int64_t(nx)-ox,dz=std::int64_t(nz)-oz;
    const auto center=[&](std::int32_t value){return add32(multiply32(divide32(value,side),side),side>>1);};
    auto cx=center(std::min(ox,nx));
    if(cx<std::min(ox,nx)||cx>std::max(ox,nx))cx=std::min(ox,nx);
    // Original Z branch unconditionally replaces its tested midpoint.
    auto cz=center(std::min(oz,nz));
    const auto edge=[&](std::int32_t old,bool negative){
        const auto origin=sub32(old,old%side);return add32(origin,negative?r:side-r-1);
    };
    auto rx=nx,rz=nz;
    if(std::abs(dx)<=std::abs(dz)){
        if(!footprint_clear(world,ox,nz)){
            if(footprint_clear(world,cx,nz)){from.x=cx;from.z=nz;return from;}
            rz=edge(oz,dz<0);
        }
        if(!footprint_clear(world,nx,rz))rx=edge(ox,dx<0);
    }else{
        if(!footprint_clear(world,nx,oz)){
            if(footprint_clear(world,nx,cz)){from.x=nx;from.z=cz;return from;}
            rx=edge(ox,dx<0);
        }
        if(!footprint_clear(world,rx,nz))rz=edge(oz,dz<0);
    }
    from.x=rx;from.z=rz;return from;
}
static void move_actor_to_impl(World& world,OwnerId id,std::int32_t nx,std::int32_t nz,bool current){
    require_profile(world);
    auto& actor=world.object_for_owner(id);
    if(!actor.pose)throw std::invalid_argument("Moving unplaced actor");
    auto& pose=*actor.pose;const auto& d=world.definition;
    auto& mmap=world.maps.at("mmap");const auto& tmap=world.maps.at("tmap");
    const auto glyph=d.objects[actor.definition_index].glyph;
        auto old_cell=cell(world,pose.x,pose.z);
        if(mmap[old_cell]!=glyph&&mmap[old_cell]!=' ')return; // Do not erase another live actor.
        auto& old_door=object(world,tmap[old_cell]);
        if(d.objects[old_door.definition_index].class_id==7&&old_door.door_occupants)
            old_door.door_occupants=word(old_door.door_occupants-1);
        mmap[old_cell]=' ';
        const bool origin_clear=footprint_clear(world,pose.x,pose.z);
        auto candidate=resolve_movement(world,pose,nx,nz);
        const bool x_axis=nz==pose.z&&nx!=pose.x,z_axis=nx==pose.x&&nz!=pose.z;
        if(current&&((x_axis&&candidate.z!=pose.z)||(z_axis&&candidate.x!=pose.x))){
            // Native OG can move a pure current sideways to a cell midpoint.
            // With a fixed416 tick this makes a real bank pocket unreachable.
            // Sweep the requested axis to its FIRST solid edge instead. Each
            // chunk is shorter than one tile: a blocked cell's interval on an
            // axis is at least one tile, so clear chunk endpoints cannot skip it.
            const auto delta=x_axis?std::int64_t(nx)-pose.x:std::int64_t(nz)-pose.z;
            const auto direction=delta<0?-1:1;
            const auto distance=std::abs(delta);
            const auto point=[&](std::int64_t step){auto p=pose;
                if(x_axis)p.x=static_cast<std::int32_t>(std::int64_t(p.x)+direction*step);
                else p.z=static_cast<std::int32_t>(std::int64_t(p.z)+direction*step);
                return p;};
            const auto clear=[&](std::int64_t step){const auto p=point(step);const auto r=d.min_distance;
                return p.x>=r&&p.z>=r&&p.x<std::int64_t(d.width)*d.cell_side-r&&
                    p.z<std::int64_t(d.length)*d.cell_side-r&&footprint_clear(world,p.x,p.z);};
            std::int64_t reached=0;
            while(reached<distance){
                const auto end=std::min(distance,reached+std::max(1,d.cell_side-1));
                if(clear(end)){reached=end;continue;}
                auto low=reached,high=end;
                while(low+1<high){const auto middle=low+(high-low)/2;if(clear(middle))low=middle;else high=middle;}
                reached=low;break;
            }
            candidate=point(reached);
        }
        // Keep valid actors out of obstacles and every actor inside the map.
        // Original maps can place an actor on solid terrain (PKR LEVEL1 glyph158).
        // An already blocked origin must retain the native resolver's alignment;
        // refusing it changes later course trials and can prevent native escape.
        const auto r=d.min_distance;
        if(candidate.x>=r&&candidate.z>=r&&candidate.x<std::int64_t(d.width)*d.cell_side-r&&
           candidate.z<std::int64_t(d.length)*d.cell_side-r&&(!origin_clear||footprint_clear(world,candidate.x,candidate.z)))pose=candidate;
        const auto new_cell=cell(world,pose.x,pose.z);auto& new_door=object(world,tmap[new_cell]);
        if(d.objects[new_door.definition_index].class_id==7)new_door.door_occupants=word(new_door.door_occupants+1);
        mmap[new_cell]=glyph;
}
void move_actor_to(World& world,OwnerId id,std::int32_t nx,std::int32_t nz){
    move_actor_to_impl(world,id,nx,nz,false);
}
void move_actor_current(World& world,OwnerId id,std::int16_t distance,bool z_axis){
    const auto& actor=world.object_for_owner(id);
    if(!actor.pose)throw std::invalid_argument("Moving unplaced actor with current");
    const auto p=*actor.pose;
    move_actor_to_impl(world,id,z_axis?p.x:add32(p.x,distance),z_axis?add32(p.z,distance):p.z,true);
}
void move_actor(World& world,OwnerId id,std::int16_t distance,bool strafe){
    require_profile(world);
    const auto& actor=world.object_for_owner(id);
    if(!actor.pose)throw std::invalid_argument("Moving unplaced actor");
    const auto step=sar32(distance,2);
    for(int part=0;part<3;++part){
        const auto pose=*actor.pose;const auto angle=heading(pose.heading+(strafe?90:0));
        const auto nx=add32(pose.x,sar32(multiply32(original_sine(angle),step),15));
        const auto nz=sub32(pose.z,sar32(multiply32(original_cosine(angle),step),15));
        move_actor_to(world,id,nx,nz);
    }
}
void apply_mouse_yaw(World& world,float degrees){
    if(!std::isfinite(degrees))return;
    auto& actor=world.objects.at(*world.player);auto& motion=*actor.motion;
    motion.mouse_remainder+=std::clamp(degrees,-3600.0f,3600.0f);
    const auto whole=static_cast<int>(motion.mouse_remainder);motion.mouse_remainder-=whole;
    actor.pose->heading=static_cast<std::int16_t>(heading(actor.pose->heading-whole));
}
void update_player_motion(World& world,const InputActions& input,std::uint32_t now,bool inertia){
    update_actor_motion(world,world.objects.at(*world.player).owner,input,now,inertia);
}
void update_actor_motion(World& world,OwnerId owner,const InputActions& input,std::uint32_t now,bool inertia){
    require_profile(world);
    auto& actor=world.object_for_owner(owner);auto& motion=*actor.motion;auto& pose=*actor.pose;
    const auto& definition=world.definition.objects[actor.definition_index];
    const bool old_gold=world.definition.dataset==DatasetId::old_gold;
    const auto old_x=pose.x,old_z=pose.z;
    motion.clock.stop(now);const auto elapsed=motion.clock.read(now);
    const bool moving=input.forward!=0||input.strafe!=0,turning=input.turn!=0;
    if(moving){motion.last_forward=input.forward;motion.last_strafe=input.strafe;}
    motion.acceleration=std::clamp(add32(motion.acceleration,moving?signed32(elapsed):sub32(0,signed32(elapsed))),0,10000);
    if(!inertia){motion.acceleration=moving?10000:0;if(!moving)motion.last_forward=motion.last_strafe=0;}
    motion.turn_acceleration=std::min(add32(motion.turn_acceleration,signed32(elapsed)),10000);
    auto scale=static_cast<std::uint32_t>(elapsed<<8)/416;
    if(turning&&input.forward!=0){
        if(inertia)motion.acceleration=motion.turn_acceleration=std::max(motion.acceleration,motion.turn_acceleration);
        scale>>=1;
    }
    scale=std::clamp<std::uint32_t>(scale,256,old_gold?1536:768);
    if(input.run)scale<<=1;if(input.slow)scale>>=1;
    auto turn_step=word(sar32(divide32(multiply32(multiply32(definition.number(0x34),scale),motion.turn_acceleration),10000),8));
    if(!old_gold)turn_step=std::min<std::int16_t>(turn_step,18);
    if(!turning)motion.turn_acceleration=0;
    else{
        const int direction=input.turn>0?1:0;
        if(motion.turn_direction!=direction){turn_step=1;motion.turn_direction=static_cast<std::int16_t>(direction);}
        const auto delta=static_cast<int>(turn_step*std::clamp(std::abs(input.turn),0.0f,1.0f));
        pose.heading=static_cast<std::int16_t>(heading(pose.heading+(direction?-delta:delta)));
    }
    auto step=word(divide32(multiply32(sar32(multiply32(definition.number(0x38),scale),8),motion.acceleration),10000));
    if(step<1){step=0;motion.acceleration=0;}
    if(step){
        auto forward=moving?input.forward:motion.last_forward;
        auto strafe=moving?input.strafe:motion.last_strafe;
        if(forward)move_actor(world,actor.owner,word(static_cast<int>(-step*std::clamp(forward,-1.0f,1.0f))),false);
        if(strafe)move_actor(world,actor.owner,word(static_cast<int>(step*std::clamp(strafe,-1.0f,1.0f))),true);
    }
    motion.clock.start(now);
    if(old_gold&&(pose.x!=old_x||pose.z!=old_z)){
        // OG 1ae1:0908 only bobs after actual displacement; walls do not bob.
        // World prepare 21b8:0d90 fixes player bounds at spawn Y +/-26.
        const auto rate=definition.class_id==1?4:0;
        const auto amplitude=definition.class_id==1?26:0;
        const auto baseline=negate16(definition.height_parameter);
        const auto upper=word(baseline+amplitude),lower=word(baseline-amplitude);
        const auto delta=sar32(multiply32(rate,scale),8);
        if(motion.bob_direction==0){
            pose.y=sub32(pose.y,delta);
            if(pose.y<lower){motion.bob_direction=1;pose.y=lower;}
        }else{
            pose.y=add32(pose.y,delta);
            if(pose.y>upper){motion.bob_direction=0;pose.y=upper;}
        }
    }
}
} // namespace dok
