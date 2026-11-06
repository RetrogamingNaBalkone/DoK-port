#include "dok/monsters.hpp"
#include "dok/arithmetic.hpp"
#include "dok/movement.hpp"
#include "dok/items.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
using namespace original;
namespace {
#include "ai_angles.inc"
std::int16_t word(std::int32_t n){return signed16(static_cast<std::uint16_t>(n));}
void step(Pose& pose,int distance){
    pose.x=add32(pose.x,sar32(multiply32(original_sine(pose.heading),distance),15));
    pose.z=sub32(pose.z,sar32(multiply32(original_cosine(pose.heading),distance),15));
}
int toward(Pose from,Pose to){return (360-original_direction(sub32(to.z,from.z),sub32(to.x,from.x)))%360;}
}
int original_direction(std::int32_t z,std::int32_t x){
    const auto az=std::abs(std::int64_t(z)),ax=std::abs(std::int64_t(x));
    if(ax<az){const auto a=angles.at(ax*100/az);
        if(z>0&&x>=0)return a;if(z<0&&x>=0)return 180-a;
        if(z>0&&x<0)return 360-a;if(z<0&&x<0)return a+180;
    }else if(ax){const auto a=angles.at(az*100/ax);
        if(x>0&&z>=0)return 90-a;if(x<0&&z>=0)return a+270;
        if(x>0&&z<0)return a+90;if(x<0&&z<0)return 270-a;
    }
    return 0;
}
Monsters::Monsters(World& w,const Archive& archive,Projectiles& shots):world_(w),projectiles_(shots){
    if(archive.identify()!=w.definition.dataset)
        throw std::invalid_argument("Monster archive/world profile mismatch");
    for(const auto& object:w.objects)if(object.monster){
        const auto& d=w.definition.objects[object.definition_index];
        std::array<std::vector<AnimationFrame>,3> frames{{archive.animation(d.animation),archive.animation(d.resource(0xe8)),archive.animation(d.resource(0xec))}};
        for(auto& clip:frames){if(clip.empty())throw std::runtime_error("Empty monster animation");for(auto& f:clip)f.image.flags=8;}
        clips_.emplace(object.owner,std::move(frames));
    }
    projectiles_.monster_hit=[this](auto owner,auto amount){return damage(owner,amount);};
}
Monsters::~Monsters(){projectiles_.monster_hit={};}
std::size_t Monsters::cell(Pose p)const{
    const auto& d=world_.definition;
    return std::clamp<int>(word(divide32(p.z,d.cell_side)),0,d.length-1)*d.width+
        std::clamp<int>(word(divide32(p.x,d.cell_side)),0,d.width-1);
}
Monsters::Target Monsters::lookup(Pose& p,int distance,bool terrain)const{
    step(p,distance);const auto at=cell(p);
    if(!terrain&&world_.maps.at("mmap")[at]==' '){
        const auto slot=world_.projectile_occupancy[at];
        if(slot>=0&&world_.projectiles[slot])return {Projectiles::owner_base+static_cast<unsigned>(slot),8,world_.projectiles[slot]->flags};
    }
    const auto glyph=world_.maps.at(terrain?"tmap":"mmap")[at];
    const auto& o=world_.objects.at(world_.definition.glyph_index[glyph]);
    return {o.owner,world_.definition.objects[o.definition_index].class_id,o.flags};
}
bool Monsters::sight(WorldObject& actor)const{
    auto p=*actor.pose;const auto player=world_.objects.at(*world_.player).owner;
    for(int i=0;i<5000;++i){
        const auto m=lookup(p,-100,false);
        if(m.owner==actor.owner)continue;
        if(m.owner==player)return true;
        if(!(m.flags&0x18))return false;
        if(!(lookup(p,-100,true).flags&0x18))return false;
    }
    return false;
}
void Monsters::wake(OwnerId owner){
    auto& o=world_.object_for_owner(owner);auto& m=*o.monster;
    if(m.corpse||!o.pose)return;
    world_.sound(owner,world_.definition.dataset==DatasetId::old_gold?AudioAction::priority_once:AudioAction::once);
    m.state=1;m.next_frame=-1;m.shot_count=0;
    m.course_counter=word(world_.definition.objects[o.definition_index].number(0xae));
    m.burst_clock.start(static_cast<std::uint32_t>(world_.simulation_ticks()));
    if(!world_.events.schedule(owner,static_cast<CallbackId>(GameEvent::monster_tick),416,
        static_cast<std::uint32_t>(world_.simulation_ticks())))throw std::runtime_error("Cannot schedule monster");
}
void Monsters::consider(CellBounds bounds){
    const auto hero=*world_.objects.at(*world_.player).pose;const auto side=world_.definition.cell_side;
    for(auto& o:world_.objects)if(o.monster&&o.pose&&o.monster->state==0){
        const auto x=o.pose->x/side,z=o.pose->z/side;
        if(x<bounds.min_x||x>bounds.max_x||z<bounds.min_z||z>bounds.max_z)continue;
        const auto distance=std::max(std::abs(std::int64_t(hero.x)-o.pose->x),std::abs(std::int64_t(hero.z)-o.pose->z));
        if(distance<world_.definition.objects[o.definition_index].number(0xba))wake(o.owner);
    }
}
bool Monsters::damage(OwnerId owner,std::int16_t amount){
    auto& o=world_.object_for_owner(owner);auto& m=*o.monster;
    if(!m.health)return false;
    if(!m.state)wake(owner);
    else{m.health=std::max<std::int16_t>(0,word(m.health-amount));m.state=3;
        if(!m.health)world_.notify(world_.definition.objects[o.definition_index],0xbe);}
    return true;
}
void Monsters::display(WorldObject& o,int clip,int frame){
    auto& m=*o.monster;m.display_clip=static_cast<std::int16_t>(clip);m.display_frame=static_cast<std::int16_t>(frame);
    // Treat resource durations as unsigned WORDs. Native sign extension makes
    // long death frames wrap the deadline near UINT32_MAX early in a level.
    world_.events.set_first_interval(clips_.at(o.owner)[clip].at(frame).interval);
}
void Monsters::event(OwnerId owner){
    auto& o=world_.object_for_owner(owner);auto& m=*o.monster;const auto& d=world_.definition.objects[o.definition_index];
    const auto& clips=clips_.at(owner);const auto now=static_cast<std::uint32_t>(world_.simulation_ticks());
    if(m.health<=0){
        const auto release_passage=[&]{
            if(m.passage_released)return;
            const auto at=cell(*o.pose);
            // A dead actor leaves door occupancy exactly once, even if its
            // transparent animation tail continues after another actor enters.
            if(world_.maps.at("mmap")[at]==d.glyph){
                auto& terrain=world_.objects.at(world_.definition.glyph_index[world_.maps.at("tmap")[at]]);
                if(terrain.door&&terrain.door_occupants>0)--terrain.door_occupants;
            }
            o.flags|=0x10;m.passage_released=true;
        };
        if(m.state!=4){m.state=4;m.next_frame=0;world_.sound(owner,AudioAction::stop_all);world_.sound(owner,AudioAction::once,0xf4);}
        if(m.next_frame>=static_cast<int>(clips[2].size())){
            const auto at=cell(*o.pose);auto& mmap=world_.maps.at("mmap");auto& tmap=world_.maps.at("tmap");
            release_passage();
            if(mmap[at]==d.glyph)mmap[at]=' ';
            if(world_.definition.object(tmap[at])->class_id==0)tmap[at]=d.glyph;
            else if(mmap[at]==' ')mmap[at]=d.glyph; // Never erase an actor that entered the released cell.
            m.corpse=true;o.pose->y=0;world_.sound(owner,AudioAction::stop,0xf4);world_.events.cancel_current();return;
        }
        display(o,2,m.next_frame++);
        // Port fix: an entirely transparent final tail must not keep an
        // invisible solid obstacle alive for the native frame duration.
        // Keep the event and audio duration; visible death frames still block.
        if(std::all_of(clips[2].begin()+m.display_frame,clips[2].end(),[](const auto& frame){
            return std::none_of(frame.image.pixels.begin(),frame.image.pixels.end(),[](auto p){return p!=0;});
        }))release_passage();
        // OG 1ae1:38ca halves the signed object height (+19) on every death
        // frame. Pose stores its negative, so preserve SAR's rounding here.
        if(world_.definition.dataset==DatasetId::old_gold)
            o.pose->y=negate16(word(sar32(negate16(word(o.pose->y)),1)));
        return;
    }
    auto front_pose=*o.pose;auto front=lookup(front_pose,-world_.definition.cell_side,false);
    if(front.cls==0)front=lookup(front_pose,-world_.definition.cell_side,true);
    if(front.cls==1){
        world_.sound(owner,world_.definition.dataset==DatasetId::old_gold?AudioAction::priority_once:AudioAction::once,0xf0);
        if(m.state==1){m.next_frame=-1;m.state=2;}
        if(++m.next_frame>=static_cast<int>(clips[1].size())){
            damage_player(world_,-5);
            m.next_frame=0;
        }
        display(o,1,m.next_frame);
        if(m.state==3){
            // OG 1ae1:34ee only resumes attack; Kremlin substitutes hurt frame0.
            if(world_.definition.dataset!=DatasetId::old_gold)display(o,2,0);
            m.state=2;
        }
        return;
    }
    m.state=1;
    const auto at=cell(*o.pose);auto& occupied=world_.maps.at("mmap")[at];
    const auto saved=occupied;occupied=' ';
    const auto old_heading=o.pose->heading;
    o.pose->heading=static_cast<std::int16_t>(toward(*o.pose,*world_.objects.at(*world_.player).pose));
    if(m.burst_clock.read(now)/10000>static_cast<std::uint32_t>(d.number(0xa8))){
        if(m.shot_count<d.number(0xa6)&&front.cls!=8&&front.cls!=9&&sight(o)){
            m.shot_count=word(m.shot_count+1);m.course_counter=word(d.number(0xae));
            projectiles_.spawn(owner,static_cast<std::uint8_t>(d.number(0xe7)));
            world_.sound(owner,world_.definition.dataset==DatasetId::old_gold?AudioAction::priority_once:AudioAction::once,0xf0);
        }else{m.burst_clock.stop(now);m.burst_clock.start(now);m.shot_count=0;}
    }
    if(m.course_counter==d.number(0xae)){
        m.course_counter=0;auto trial=*o.pose;
        for(int i=0;i<10;++i){auto candidate=trial;step(candidate,-(world_.definition.cell_side>>1));
            trial=resolve_movement(world_,trial,candidate.x,candidate.z);if(cell(trial)!=at)break;
        }
        o.pose->heading=static_cast<std::int16_t>(toward(*o.pose,trial));
    }else{m.course_counter=word(m.course_counter+1);o.pose->heading=old_heading;}
    occupied=saved;
    if(++m.next_frame>=static_cast<int>(clips[0].size()))m.next_frame=0;
    display(o,0,m.next_frame);
    InputActions forward;forward.forward=1;update_actor_motion(world_,owner,forward,now);
}
std::vector<PositionedImage> Monsters::images(CellBounds bounds)const{
    std::vector<PositionedImage> result;const auto side=world_.definition.cell_side;
    for(const auto& o:world_.objects)if(o.monster&&o.pose){
        const auto x=o.pose->x/side,z=o.pose->z/side;
        if(x<bounds.min_x||x>bounds.max_x||z<bounds.min_z||z>bounds.max_z)continue;
        const auto& m=*o.monster;const auto& image=clips_.at(o.owner)[m.display_clip].at(m.display_frame).image;
        result.push_back({*o.pose,&image,world_.definition.objects[o.definition_index].width_parameter,o.flags,o.owner});
    }
    return result;
}
}
