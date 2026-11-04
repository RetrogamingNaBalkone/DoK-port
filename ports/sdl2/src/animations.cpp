#include "dok/animations.hpp"
#include "dok/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
Animations::Animations(World& world,const Archive& archive):world_(world){
    if(archive.identify()!=world.definition.dataset)throw std::invalid_argument("Animation profile mismatch");
    for(auto& object:world.objects){
        const auto& d=world.definition.objects[object.definition_index];
        // These classes have dedicated virtual getters and playback state.
        if(!(d.flags&0x800)||d.class_id==8||d.class_id==9||d.class_id==10)continue;
        auto frames=archive.animation(d.animation);
        if(frames.empty())throw std::runtime_error("Empty base animation");
        for(auto& f:frames)f.image.flags=8;
        clips_.emplace(object.owner,std::move(frames));
        if(!object.animation){object.animation.emplace();select(object,0);}
    }
}
void Animations::select(WorldObject& object,std::size_t frame){
    const auto& f=clips_.at(object.owner).at(frame);auto& state=*object.animation;
    state.frame=static_cast<std::int16_t>(frame);
    state.parameter_a=original::signed16(f.parameter_a);state.parameter_b=original::signed16(f.parameter_b);
    state.interval=original::signed16(f.interval);
}
bool Animations::activate(OwnerId owner){
    if(!clips_.count(owner))return false;
    auto& state=*world_.object_for_owner(owner).animation;
    if(state.remaining!=0)return false;
    // Native getter sets the budget before schedule and ignores queue-full.
    // In particular, an exhausted but not yet cancelled callback may coexist
    // with the newly scheduled one. Preserve that ordering, without a fatal error.
    state.remaining=200;
    return world_.events.schedule(owner,static_cast<CallbackId>(GameEvent::base_animation),state.interval,
        static_cast<std::uint32_t>(world_.simulation_ticks()));
}
void Animations::consider(CellBounds bounds){
    const auto& d=world_.definition;
    bounds.min_x=std::max(0,bounds.min_x);bounds.min_z=std::max(0,bounds.min_z);
    bounds.max_x=std::min(d.width-1,bounds.max_x);bounds.max_z=std::min(d.length-1,bounds.max_z);
    for(int z=bounds.min_z;z<=bounds.max_z;++z)for(int x=bounds.min_x;x<=bounds.max_x;++x){
        const auto cell=static_cast<std::size_t>(z*d.width+x);
        const auto& terrain=world_.objects.at(d.glyph_index[world_.maps.at("tmap")[cell]]);
        const auto tc=d.objects[terrain.definition_index].class_id;
        // Original gather skips the remaining cell behind a solid wall.
        // Wall textures also use the base getter (OG glyph H is animated).
        activate(terrain.owner);
        if(tc==2)continue;
        const auto& actor=world_.objects.at(d.glyph_index[world_.maps.at("mmap")[cell]]);
        const auto ac=d.objects[actor.definition_index].class_id;
        if(ac==3||ac==9)activate(actor.owner);
    }
}
void Animations::event(OwnerId owner){
    if(!clips_.count(owner))throw std::invalid_argument("Not a base animation owner");
    auto& object=world_.object_for_owner(owner);auto& state=*object.animation;
    if(state.remaining<=0){world_.events.cancel_current();return;}
    --state.remaining;
    select(object,(static_cast<std::size_t>(state.frame)+1)%clips_.at(owner).size());
    world_.events.set_first_interval(state.interval);
}
const Image* Animations::image(OwnerId owner)const{
    const auto it=clips_.find(owner);if(it==clips_.end())return nullptr;
    return &it->second.at(world_.object_for_owner(owner).animation->frame).image;
}
}
