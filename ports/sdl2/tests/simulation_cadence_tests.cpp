#include "dok/monsters.hpp"
#include "dok/weapons.hpp"
#include "dok/items.hpp"
#include "dok/movement.hpp"
#include "dok/interactions.hpp"
#include "dok/flow.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
static std::uint64_t digest(const dok::World& w){
 std::uint64_t h=1469598103934665603ULL;auto add=[&](std::uint64_t n){h=(h^n)*1099511628211ULL;};
 add(w.simulation_ticks());add(w.level_complete);add(w.total_money);
 for(const auto& map:w.maps)for(auto b:map.second)add(static_cast<unsigned char>(b));
 for(const auto& o:w.objects){add(o.owner);add(o.flags);if(o.pose){add(o.pose->x);add(o.pose->y);add(o.pose->z);add(o.pose->heading);}
  if(o.player_state){for(auto v:o.player_state->attributes)add(v);add(o.player_state->score);add(o.player_state->lives);}
  if(o.monster){add(o.monster->health);add(o.monster->state);add(o.monster->display_frame);}
  for(auto e:w.events.events(o.owner)){add(e.deadline);add(e.interval);add(e.callback);add(e.parameter);}
 }
 for(const auto& p:w.projectiles)if(p){add(p->pose.x);add(p->pose.y);add(p->pose.z);add(p->frame);add(p->impact);}
 return h;
}
static std::vector<std::uint64_t> replay(const dok::Archive& a,const char* level,unsigned fps){
 dok::World w(dok::load_level(a,level));dok::Weapons weapons(w,a);dok::Projectiles shots(w,a);dok::Monsters monsters(w,a,shots);dok::GameFlow flow(w);
 auto& hero=w.objects.at(*w.player);std::vector<std::uint64_t> trace;unsigned calls=0;
 auto handler=[&](dok::OwnerId owner,dok::CallbackId id){++calls;auto e=static_cast<dok::GameEvent>(id);
  if(e==dok::GameEvent::projectile_tick)shots.event(owner);
  else if(e==dok::GameEvent::monster_tick)monsters.event(owner);
  else if(e==dok::GameEvent::weapon_tick||e==dok::GameEvent::weapon_switch)weapons.event(owner,e);
  else if(e==dok::GameEvent::player_tick){
   monsters.consider(dok::original_cell_bounds(*hero.pose,w.definition.cell_side,w.definition.width,w.definition.length));
   dok::InputActions input;input.forward=0.5f;input.strafe=0.3f;input.turn=w.simulation_ticks()<15000?0.4f:-0.2f;
   dok::update_player_motion(w,input,static_cast<std::uint32_t>(w.simulation_ticks()));
   const auto pickup=dok::player_pickup(w);if(pickup)weapons.picked_up(pickup);
   weapons.fire([&](auto glyph){return shots.spawn(owner,glyph);});
  }else dok::door_event(w,owner,e);
  trace.push_back(w.simulation_ticks());trace.push_back((std::uint64_t(owner)<<16)|id);trace.push_back(digest(w));
 };
 std::uint64_t previous=0;for(unsigned f=1;f<=fps*3;++f){auto now=std::uint64_t(f)*1000000000ULL/fps;
  w.advance_events(now-previous,handler);previous=now;
  const auto before=digest(w);const auto old_calls=calls;w.advance_events(0,handler);
  if(before!=digest(w)||calls!=old_calls)throw std::runtime_error("Paused elapsed advanced gameplay");
 }
 if(calls==0)throw std::runtime_error("No gameplay callbacks");trace.push_back(digest(w));return trace;
}
int main(int argc,char** argv){try{unsigned levels=0;for(int i=1;i<argc;++i){auto a=dok::Archive::open(argv[i]);
 for(const auto* level:{"LEVEL1","LEVEL2","LEVEL3","LEVEL4"})if(a.find(level)){
  auto baseline=replay(a,level,30);for(auto fps:{60u,144u})if(replay(a,level,fps)!=baseline)throw std::runtime_error(std::string("Cadence differs: ")+level);
  std::cout<<dok::dataset_name(a.identify())<<' '<<level<<": "<<(baseline.size()-1)/3<<" identical gameplay callbacks at30/60/144; pause PASS\n";++levels;
 }}if(levels!=7)throw std::runtime_error("Expected seven levels");}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
