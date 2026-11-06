#include "dok/scene.hpp"
#include "dok/movement.hpp"
#include "dok/items.hpp"
#include "dok/clock.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#define private public
#include "dok/monsters.hpp"
#undef private
static std::uint32_t hash(const std::vector<std::uint8_t>& b){std::uint32_t h=2166136261u;for(auto c:b)h=(h^c)*16777619u;return h;}
static std::string hx(const std::string& s){const char* d="0123456789abcdef";std::string out;for(unsigned char c:s){out+=d[c>>4];out+=d[c&15];}return out;}
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Usage: dok_monster_ai_probe RES LEVEL");auto a=dok::Archive::open(std::filesystem::u8path(argv[1]));const auto d=dok::load_level(a,argv[2]);
 int op,glyph,board,state,frame,course,shots,burst,motion,heading,dx,dz,hp,busy;
 while(std::cin>>op>>glyph>>board>>state>>frame>>course>>shots>>burst>>motion>>heading>>dx>>dz>>hp>>busy){
  dok::World w(d);dok::Projectiles p(w,a);dok::Monsters m(w,a,p);const auto index=d.glyph_index.at(glyph);if(index<0||!w.objects[index].monster)throw std::runtime_error("Not monster");
  const auto original=w.objects[index].pose;const auto original_hero=w.objects.at(*w.player).pose;for(auto& o:w.objects)if(o.monster)o.pose.reset();auto& actor=w.objects[index];auto& hero=w.objects.at(*w.player);auto& ms=*actor.monster;
  const int side=d.cell_side;const int x=20*side+side/2,z=20*side+side/2;
  if(board<7){for(auto& pair:w.maps)std::fill(pair.second.begin(),pair.second.end(),' ');auto wall=std::find_if(d.objects.begin(),d.objects.end(),[](auto& def){return def.class_id==2;});if(wall==d.objects.end())throw std::runtime_error("No wall");
   for(int r=0;r<d.length;++r)for(int c=0;c<d.width;++c)if(!r||!c||r==d.length-1||c==d.width-1)w.maps.at("tmap")[r*d.width+c]=wall->glyph;
   if(board==1)w.maps.at("tmap")[22*d.width+20]=wall->glyph;
   if(board==3)w.maps.at("tmap")[21*d.width+20]=wall->glyph;
   if(board==4){auto other=std::find_if(d.objects.begin(),d.objects.end(),[&](auto& def){return def.class_id==9&&def.glyph!=glyph;});if(other!=d.objects.end())w.maps.at("mmap")[21*d.width+20]=other->glyph;}
   if(board==5){auto shot=std::find_if(d.objects.begin(),d.objects.end(),[](auto& def){return def.class_id==8;});if(shot!=d.objects.end())w.maps.at("mmap")[21*d.width+20]=shot->glyph;}
   if(board==6)for(int r=19;r<=23;++r)w.maps.at("tmap")[r*d.width+21]=wall->glyph;
  }else {for(auto& c:w.maps.at("mmap"))if(c==d.objects[*w.player].glyph||c==glyph)c=' ';}
  actor.pose=dok::Pose{x,-d.objects[index].height_parameter,z,static_cast<std::int16_t>(heading)};
  if(board>=7&&original)actor.pose=original;actor.pose->heading=static_cast<std::int16_t>(heading);
  hero.pose=dok::Pose{actor.pose->x+dx*(op==5?1:side),-d.objects[*w.player].height_parameter,actor.pose->z+dz*(op==5?1:side),180};if(board==8)hero.pose=original_hero;hero.player_state->attributes[0]=hero.player_state->attributes[2]=30000;
  auto cell=[&](dok::Pose pose){return std::clamp(pose.z/side,0,d.length-1)*d.width+std::clamp(pose.x/side,0,d.width-1);};
  w.maps.at("mmap")[cell(*actor.pose)]=glyph;w.maps.at("mmap")[cell(*hero.pose)]=d.objects[*w.player].glyph;
  std::uint32_t now=100000;w.restore_time({now,0},now);ms.state=state;ms.next_frame=frame;ms.course_counter=course;ms.shot_count=shots;ms.health=hp;ms.burst_clock.restore({now-static_cast<std::uint32_t>(burst),0,true});actor.motion->clock.restore({now-static_cast<std::uint32_t>(motion),0,true});
  dok::MonoVoice voice;std::map<std::string,dok::Sound> sounds;std::string trace;const auto& def=d.objects[index];
  const auto token=[&](dok::SoundId id)->unsigned {if(!id)return 0;if((id>>16)==actor.owner)return static_cast<unsigned>(id&65535);if((id>>16)==hero.owner)return 0x10000u+static_cast<unsigned>(id&65535);return 999;};
  const auto get=[&](const std::string& key)->const dok::Sound&{auto found=sounds.find(key);if(found==sounds.end())found=sounds.emplace(key,a.sound(key)).first;return found->second;};
  const auto role_name=[&](unsigned role){return role==0x4c?def.sound:role==0xf0?def.resource(0xf0):role==0xf4?def.resource(0xf4):role==0x100a6?d.objects[*w.player].resource(0xa6):def.sound;};
  const unsigned initial=busy==1?0xf0:busy==2?999:busy==3?0x4c:busy==4?0xf4:busy==5?0x100a6:0;
  if(initial){const auto key=role_name(initial);if(!key.empty())voice.play(initial==999?0xdeadbeef:dok::sound_id(initial==0x100a6?hero.owner:actor.owner,initial&65535),get(key),initial==999,d.dataset==dok::DatasetId::old_gold);}
  w.audio=[&](const dok::AudioCommand& command){const auto before=voice.snapshot();if(command.action==dok::AudioAction::stop_all)voice.stop();else if(command.action==dok::AudioAction::stop)voice.stop(command.id);else if(!command.resource.empty()){if(command.action==dok::AudioAction::priority_once&&voice.active()!=command.id)voice.stop();if(command.action!=dok::AudioAction::priority_once||voice.active()!=command.id)voice.play(command.id,get(command.resource),command.action==dok::AudioAction::loop,d.dataset==dok::DatasetId::old_gold);}const auto after=voice.snapshot();
    trace+=std::to_string(int(command.action))+","+std::to_string(token(command.id))+","+hx(command.resource)+","+std::to_string(token(before.id))+","+std::to_string(token(after.id))+","+std::to_string(after.loop)+","+std::to_string(before.loop)+";";};
  w.events.clear();w.events.schedule(actor.owner,static_cast<dok::CallbackId>(dok::GameEvent::monster_tick),416,now-416);for(int round=0;round<1;++round){w.restore_time({now,0},now);if(op==6)m.wake(actor.owner);else w.events.dispatch([&]{return now;},[&](auto owner,auto){m.event(owner);});
  std::cout<<actor.pose->x<<','<<actor.pose->y<<','<<actor.pose->z<<','<<actor.pose->heading<<','<<ms.state<<','<<ms.next_frame<<','<<ms.display_clip<<','<<ms.display_frame<<','<<ms.course_counter<<','<<ms.shot_count<<','<<actor.motion->acceleration<<','<<hero.player_state->health()<<','<<hash(w.maps.at("mmap"))<<','<<p.active()<<',';
  const auto events=w.events.events(actor.owner);std::cout<<(events.empty()?0:events[0].interval)<<','<<ms.burst_clock.read(now)<<'|';const auto end=voice.snapshot();std::cout<<token(end.id)<<','<<end.loop<<'|'<<trace<<'\n';now+=events.empty()?416:events[0].interval;}
 }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
