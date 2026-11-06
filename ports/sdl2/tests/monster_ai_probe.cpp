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
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Usage: dok_monster_ai_probe RES LEVEL");auto a=dok::Archive::open(std::filesystem::u8path(argv[1]));const auto d=dok::load_level(a,argv[2]);
 int op,glyph,board,state,frame,course,shots,burst,motion,heading,dx,dz,hp;
 std::string input;while(std::getline(std::cin,input)){
  if(input.empty())continue;std::istringstream row(input);if(!(row>>op>>glyph>>board>>state>>frame>>course>>shots>>burst>>motion>>heading>>dx>>dz>>hp))throw std::runtime_error("Incomplete AI vector");
  int rounds=1,release=-1;if(op==4||op==7){if(!(row>>rounds)||rounds<1||rounds>1000)throw std::runtime_error("Invalid sequence count");if(op==7&&!(row>>release))throw std::runtime_error("Missing release step");}std::string extra;if(row>>extra)throw std::runtime_error("Trailing vector input");
  dok::World w(d);dok::Projectiles p(w,a);dok::Monsters m(w,a,p);const auto index=d.glyph_index.at(glyph);if(index<0||!w.objects[index].monster)throw std::runtime_error("Not monster");
  const auto original=w.objects[index].pose;const auto original_hero=w.objects.at(*w.player).pose;for(auto& o:w.objects)if(o.monster)o.pose.reset();auto& actor=w.objects[index];auto& hero=w.objects.at(*w.player);auto& ms=*actor.monster;
  const int side=d.cell_side;const int x=20*side+side/2,z=20*side+side/2;
  if(board<7||board==9){for(auto& pair:w.maps)std::fill(pair.second.begin(),pair.second.end(),' ');auto wall=std::find_if(d.objects.begin(),d.objects.end(),[](auto& def){return def.class_id==2;});if(wall==d.objects.end())throw std::runtime_error("No wall");
   for(int r=0;r<d.length;++r)for(int c=0;c<d.width;++c)if(board!=9&&(!r||!c||r==d.length-1||c==d.width-1))w.maps.at("tmap")[r*d.width+c]=wall->glyph;
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
  if(op==5){ms.burst_clock.restore({0,static_cast<std::uint32_t>(burst),false});m.consider({0,0,d.width-1,d.length-1});std::cout<<ms.state<<','<<ms.next_frame<<','<<ms.course_counter<<','<<ms.shot_count<<','<<w.events.events(actor.owner).size()<<','<<ms.burst_clock.read(now)<<'\n';continue;}
  if(op==3){w.maps.at("mmap")[cell(*actor.pose)]=' ';auto result=dok::resolve_movement(w,*actor.pose,actor.pose->x+motion,actor.pose->z+motion);std::cout<<result.x<<','<<result.z<<','<<dok::footprint_clear(w,result.x,result.z)<<','<<dok::footprint_clear(w,actor.pose->x,actor.pose->z)<<'\n';continue;}
  if(op==1){std::cout<<m.sight(actor)<<'\n';continue;}if(op==2){std::cout<<dok::original_direction(dx,dz)<<'\n';continue;}
  w.events.clear();w.events.schedule(actor.owner,static_cast<dok::CallbackId>(dok::GameEvent::monster_tick),416,now-416);for(int round=0;round<rounds;++round){if(round==release){auto terrain=w.maps.at("tmap")[cell(*actor.pose)];w.objects.at(d.glyph_index[terrain]).flags|=0x10;}w.restore_time({now,0},now);w.events.dispatch([&]{return now;},[&](auto owner,auto){m.event(owner);});
  std::cout<<actor.pose->x<<','<<actor.pose->y<<','<<actor.pose->z<<','<<actor.pose->heading<<','<<ms.state<<','<<ms.next_frame<<','<<ms.display_clip<<','<<ms.display_frame<<','<<ms.course_counter<<','<<ms.shot_count<<','<<actor.motion->acceleration<<','<<hero.player_state->health()<<','<<hash(w.maps.at("mmap"))<<','<<p.active()<<',';
  const auto events=w.events.events(actor.owner);std::cout<<(events.empty()?0:events[0].interval)<<','<<ms.burst_clock.read(now)<<'\n';now+=events.empty()?416:events[0].interval;}
 }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
