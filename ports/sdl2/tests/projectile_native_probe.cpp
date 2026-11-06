#include "dok/projectiles.hpp"
#include "dok/monsters.hpp"
#include "dok/arithmetic.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace dok;
static std::uint64_t image_hash(const Image& image){std::uint64_t h=14695981039346656037ULL;for(auto b:image.pixels){h^=b;h*=1099511628211ULL;}return h;}
int main(int argc,char** argv){try{
 if(argc<3)throw std::runtime_error("RES LEVEL [--describe]");const auto archive=Archive::open(std::filesystem::u8path(argv[1]));const auto definition=load_level(archive,argv[2]);
 const bool trace=argc>3&&std::string(argv[3])=="--trace";unsigned case_index=0;
 if(argc>3&&!trace){for(const auto& d:definition.objects){std::cout<<"D,"<<int(d.glyph)<<","<<d.class_id<<","<<d.flags<<","<<d.width_parameter<<","<<d.height_parameter<<"\n";if(d.class_id!=8)continue;for(int k=0;k<3;++k){const auto frames=archive.animation(k==0||definition.dataset==DatasetId::old_gold?d.animation:d.resource(k==1?0x77:0x7b));for(std::size_t i=0;i<frames.size();++i){const auto& f=frames[i];std::cout<<"A,"<<int(d.glyph)<<","<<k<<","<<i<<","<<f.interval<<","<<f.parameter_a<<","<<f.parameter_b<<","<<f.image.width<<","<<f.image.height<<","<<image_hash(f.image)<<"\n";}}}return 0;}
 // op 0: contact/free flight; op 1: effect playback; op 2: allocation.
 int op,glyph,target,source_class,flags,hp,state,heading,speed,damage,elapsed,frame,plane,height,impact_actor,steps;
 while(std::cin>>op>>glyph>>target>>source_class>>flags>>hp>>state>>heading>>speed>>damage>>elapsed>>frame>>plane>>height>>impact_actor>>steps){
  const auto trace_case=case_index++;
  World w(definition);Projectiles shots(w,archive);Monsters monsters(w,archive,shots);w.events.clear();
  const auto index=definition.glyph_index.at(glyph);const auto& d=definition.objects.at(index);const bool og=definition.dataset==DatasetId::old_gold;
  auto target_index=definition.glyph_index.at(target);auto& actor=w.objects.at(target_index);actor.flags=static_cast<std::uint16_t>(flags);w.definition.objects[target_index].height_parameter=static_cast<std::int16_t>(height);
  if(actor.door)actor.door->state=static_cast<std::uint8_t>(state);
  if(actor.player_state){actor.player_state->attributes[0]=hp;actor.player_state->attributes[2]=100;}
  if(actor.monster){actor.monster->health=hp;actor.monster->state=state;actor.pose=Pose{10000,-height,10000,0};}
  OwnerId source=0;for(const auto& o:w.objects)if(w.definition.objects[o.definition_index].class_id==source_class){source=o.owner;break;}if(!source)throw std::runtime_error("Source class missing");
  for(auto& pair:w.maps)std::fill(pair.second.begin(),pair.second.end(),' ');std::fill(w.maps.at(plane?"tmap":"mmap").begin(),w.maps.at(plane?"tmap":"mmap").end(),static_cast<std::uint8_t>(target));
  unsigned slot=0;while(definition.glyph_index[slot]>=0)++slot;
  const auto owner=Projectiles::owner_base+slot;
  ProjectileState p;p.definition_index=index;p.source=source;p.pose={10000,-250,10000,static_cast<std::int16_t>(heading)};p.speed=speed;p.damage=static_cast<std::int16_t>(damage);p.damage_floor=original::signed16(static_cast<std::uint16_t>(original::sar32(damage,1)));p.flags=d.flags|0x1000;p.frame=frame;p.impact=op==1;p.actor_impact=impact_actor!=0;p.effect_frame=op==1;p.clock.start(0);w.projectiles[slot]=p;
  w.events.create(owner);Events::Snapshot queue=w.events.snapshot();for(auto& o:queue.owners)if(o.id==owner)o.slots={{0,416,static_cast<CallbackId>(GameEvent::projectile_tick),0}};queue.ring={owner};queue.cursor=owner;w.events.restore(queue);
  std::uint32_t now=elapsed;int sound_once=0,sound_stop=0;w.audio=[&](const AudioCommand& a){sound_once+=a.action==AudioAction::once;sound_stop+=a.action==AudioAction::stop;if(trace){std::cerr<<"T,"<<trace_case<<","<<static_cast<int>(a.action)<<","<<a.id<<",";const char* hex="0123456789abcdef";for(unsigned char c:a.resource)std::cerr<<hex[c>>4]<<hex[c&15];std::cerr<<"\n";}};
  if(op==2){w.projectiles={};for(unsigned n=0;n<256;++n)if(definition.glyph_index[n]<0){ProjectileState occupied;occupied.definition_index=index;w.projectiles[n]=occupied;}if(frame>=0&&frame<256)w.projectiles[frame].reset();auto& src=w.object_for_owner(source);src.pose=Pose{10000,-250,10000,0};const auto cost=shots.spawn(source,glyph);int got=-1;for(unsigned n=0;n<256;++n)if(w.projectiles[n]&&w.projectiles[n]->source==source){got=n;break;}std::cout<<got<<","<<cost<<","<<shots.active()<<"\n";continue;}
  for(int step=0;step<steps;++step){w.restore_time({now,0},now);auto qs=w.events.snapshot();for(auto& o:qs.owners)if(o.id==owner&&!o.slots.empty())o.slots.front().deadline=now;w.events.restore(qs);w.events.dispatch([&]{return now;},[&](auto id,auto cb){if(id==owner&&cb==static_cast<CallbackId>(GameEvent::projectile_tick))shots.event(id);});now+=elapsed;}
  if(op==3){if(w.projectiles[slot])throw std::runtime_error("Reuse fixture did not complete effect");auto& src=w.object_for_owner(source);src.pose=Pose{10000,-250,10000,0};const auto cost=shots.spawn(source,glyph);int got=-1;for(unsigned n=0;n<256;++n)if(w.projectiles[n]){got=n;break;}std::cout<<got<<","<<cost<<","<<shots.active()<<"\n";continue;}
  const int health=actor.player_state?actor.player_state->health():actor.monster?actor.monster->health:0,monster_state=actor.monster?actor.monster->state:0;
  const auto events=w.events.events(owner);const int active=bool(w.projectiles[slot]);std::uint64_t pixels_hash=0;std::cout<<active;
  if(active){const auto& r=*w.projectiles[slot];const auto images=shots.images();const auto expected_frames=archive.animation(!r.effect_frame||og?d.animation:d.resource(r.actor_impact?0x77:0x7b));int displayed=-1;for(std::size_t i=0;i<expected_frames.size();++i)if(expected_frames[i].image.width==images.front().image->width&&expected_frames[i].image.height==images.front().image->height&&expected_frames[i].image.pixels==images.front().image->pixels){displayed=static_cast<int>(i);break;}if(displayed<0)throw std::runtime_error("Displayed image is not in source clip");pixels_hash=image_hash(*images.front().image);std::cout<<","<<r.pose.x<<","<<r.pose.y<<","<<r.pose.z<<","<<r.damage<<","<<r.frame<<","<<r.impact<<","<<r.effect_frame<<","<<r.actor_impact<<","<<bool(r.occupied_cell);}
  else std::cout<<",0,0,0,0,0,0,0,0,0";
  std::cout<<","<<health<<","<<monster_state<<","<<events.size()<<","<<(events.empty()?0:events.front().interval)<<","<<sound_once<<","<<sound_stop<<","<<pixels_hash<<"\n";
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
