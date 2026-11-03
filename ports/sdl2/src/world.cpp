#include "dok/world.hpp"
#include "dok/arithmetic.hpp"
#include <stdexcept>

namespace dok {
void World::sound(OwnerId owner,AudioAction action,unsigned binding){
    if(!audio)return;
    if(action==AudioAction::stop_all){audio({action,0,{}});return;}
    std::string resource;
    if(action!=AudioAction::stop){
        const auto& d=definition.objects[object_for_owner(owner).definition_index];
        if(binding==0x4c)resource=d.sound;
        else for(const auto& field:d.fields)if(field.offset==static_cast<int>(binding)&&field.kind==FieldKind::sound){resource=field.bytes;break;}
        if(resource.empty())return;
    }
    audio({action,sound_id(owner,binding),resource});
}
void World::notify(const std::string& text){
    message=text.substr(0,std::min<std::size_t>(38,text.find('\0')));
    // Native HUD keeps a message for measured_FPS*4 redraws. Use simulation
    // time so refresh rate and pauses cannot shorten or extend it.
    message_until=simulated_ticks_+(4*GameClock::pit_hz+118)/119;
}
void World::notify(const ObjectDefinition& object,int offset){
    for(const auto& field:object.fields)if(field.offset==offset&&field.kind==FieldKind::text){notify(field.bytes);return;}
}
std::string World::visible_message()const{return simulated_ticks_<message_until?message:std::string{};}
World::World(LevelDefinition level):definition(std::move(level)) {
    const auto& d=definition;
    if(d.width!=d.length||d.height!=1)throw std::runtime_error("Original world initialization requires square one-layer map");
    visited.resize(static_cast<std::size_t>(d.width*d.length),0);
    projectile_occupancy.assign(visited.size(),-1);
    for(const auto& pair:d.maps){
        auto& dst=maps[pair.first];dst.resize(pair.second.size());
        for(int row=0;row<d.length;++row)for(int col=0;col<d.width;++col){
            auto glyph=pair.second[static_cast<std::size_t>((d.length-1-row)*d.width+col)];
            dst[static_cast<std::size_t>(row*d.width+col)]=glyph=='.'?' ':glyph;
        }
    }
    for(std::size_t i=0;i<d.objects.size();++i){
        WorldObject state;
        state.definition_index=i;
        state.owner=static_cast<OwnerId>(i+1);
        state.flags=d.objects[i].flags;
        if(d.objects[i].class_id==7)state.door.emplace();
        if(d.objects[i].class_id==10)state.weapon.emplace();
        if(d.objects[i].class_id==9){state.monster.emplace();state.monster->health=static_cast<std::int16_t>(d.objects[i].number(0xa4));}
        if(d.objects[i].class_id==1||d.objects[i].class_id==5||d.objects[i].class_id==9)state.motion.emplace();
        if(!events.create(state.owner))throw std::runtime_error("Cannot create object event owner");
        if(d.objects[i].class_id==1){
            state.player_state.emplace();
            for(const auto& field:d.objects[i].fields)
                if(field.offset>=0x76&&field.offset<=0x7a&&field.offset%2==0)
                    state.player_state->extra_attributes[(field.offset-0x76)/2]=static_cast<std::int16_t>(field.number);
            for(unsigned word=0;word<11;++word)
                state.player_state->attributes[word]=static_cast<std::int16_t>(d.objects[i].number(0x5e + word*2));
            if(!events.schedule(state.owner,static_cast<CallbackId>(GameEvent::player_tick),416,0))
                throw std::runtime_error("Cannot schedule player event");
        }
        objects.push_back(std::move(state));
    }
    for(auto glyph:maps.at("tmap")){
        const auto* obj=d.object(glyph);
        if(!obj)throw std::runtime_error("Unresolved world glyph");
        if(obj->class_id==3&&obj->number(0x54)==99)
            total_money=original::signed16(static_cast<std::uint16_t>(std::uint16_t(total_money)+std::uint16_t(obj->number(0x56))));
    }
    for(int row=0;row<d.length;++row)for(int col=0;col<d.width;++col){
        auto glyph=maps.at("mmap")[static_cast<std::size_t>(row*d.width+col)];
        const auto* obj=d.object(glyph);
        if(!obj)throw std::runtime_error("Unresolved world actor glyph");
        if(obj->class_id!=1&&obj->class_id!=9)continue;
        auto index=static_cast<std::size_t>(d.glyph_index[glyph]);auto& state=objects[index];
        state.pose=Pose{d.cell_side*col+d.cell_side/2,original::negate16(obj->height_parameter),d.cell_side*row+d.cell_side/2,static_cast<std::int16_t>(obj->number(0x32))};
        ++state.placements;if(obj->class_id==1)player=index;
    }
    if(!player)throw std::runtime_error("No placed player in level");
}

WorldObject& World::object_for_owner(OwnerId owner){
    if(!owner||owner>objects.size())throw std::out_of_range("Unknown world owner");
    return objects[owner-1];
}
const WorldObject& World::object_for_owner(OwnerId owner)const{
    if(!owner||owner>objects.size())throw std::out_of_range("Unknown world owner");
    return objects[owner-1];
}
void World::advance_events(std::uint64_t nanoseconds,const Events::Invoke& handler){
    if(advancing_)throw std::logic_error("Recursive world advancement");
    if(!handler)throw std::invalid_argument("Missing gameplay event handler");
    advancing_=true;
    clock_.advance_nanoseconds(nanoseconds);
    try{
        // One sweep per native tick, independent of presentation. This removes
        // DOS frame-latency drift without replacing every event by a 60 Hz tick.
        // Elapsed time is never dropped. Future optimizations must preserve ring
        // order, compaction and the unsigned deadline rollover behaviour.
        while(simulated_ticks_<clock_.ticks()){
            ++simulated_ticks_;
            if(events.active_owners())events.dispatch(
                [this]{return static_cast<std::uint32_t>(simulated_ticks_);},handler);
        }
    }catch(...){advancing_=false;throw;}
    advancing_=false;
}
}
