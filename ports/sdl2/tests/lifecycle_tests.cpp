#include "dok/animations.hpp"
#include "dok/monsters.hpp"
#include "dok/weapons.hpp"
#include "dok/arithmetic.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
static void check(bool b){if(!b)throw std::runtime_error("Lifecycle assertion failed");}
static void initial(const dok::World& w){
 check(w.simulation_ticks()==0&&!w.map_revealed&&!w.level_complete&&w.message.empty()&&w.message_until==0);
 for(auto b:w.visited)check(!b);for(auto b:w.projectile_occupancy)check(b==-1);for(const auto& p:w.projectiles)check(!p);
 for(std::size_t i=0;i<w.objects.size();++i){const auto& o=w.objects[i];const auto& d=w.definition.objects[i];
  check(o.owner==i+1&&o.definition_index==i&&o.flags==d.flags&&o.door_occupants==0);
  check(!(d.flags&0x400)); // No native retired dynamic object in the seven LEVELs.
  check(bool(o.motion)==(d.class_id==1||d.class_id==5||d.class_id==9));
  check(bool(o.player_state)==(d.class_id==1));check(bool(o.monster)==(d.class_id==9));
  check(bool(o.weapon)==(d.class_id==10));check(bool(o.door)==(d.class_id==7));
  check(bool(o.switch_state)==(d.class_id==6));check(bool(o.surface)==(d.class_id==12));
  if(o.switch_state)check(!o.switch_state->native_54&&!o.switch_state->native_56&&!o.switch_state->native_58&&!o.switch_state->interval);
  if(o.surface)check(o.surface->phase==18&&!o.surface->interval);
  if(o.player_state){const auto& p=*o.player_state;check(p.lives==3&&!p.score&&!p.current_weapon&&!p.pending_weapon&&!p.native_da);
   for(auto v:p.weapons)check(!v);for(int n=0;n<11;++n)check(p.attributes[n]==d.number(0x5e + n*2));
   for(int n=0;n<3;++n)check(p.extra_attributes[n]==d.number(0x76+n*2));
  }
  if(o.monster){const auto& m=*o.monster;check(m.health==d.number(0xa4)&&!m.state&&m.next_frame==-1&&!m.display_frame&&!m.display_clip&&!m.course_counter&&!m.shot_count&&!m.corpse&&!m.burst_clock.running());}
  if(o.weapon)check(o.weapon->state==4&&!o.weapon->shift&&!o.weapon->frame&&!o.weapon->acquired);
  if(o.door)check(!o.door->phase&&o.door->extent==1&&!o.door->interval&&o.door->state==2&&o.door->ready);
  if(o.motion){const auto& m=*o.motion;check(!m.clock.running()&&!m.clock.read(123)&&!m.acceleration&&!m.turn_acceleration&&!m.turn_direction&&!m.last_forward&&!m.last_strafe&&!m.mouse_remainder);}
  const auto ev=w.events.events(o.owner);check(ev.size()==(d.class_id==1?1u:0u));
  if(!ev.empty())check(ev[0].deadline==416&&ev[0].interval==416&&ev[0].parameter==0&&ev[0].callback==1);
 }
}
int main(int argc,char** argv){try{unsigned levels=0,animations=0,reuses=0;
 for(int arg=1;arg<argc;++arg){auto a=dok::Archive::open(argv[arg]);for(auto name:{"LEVEL1","LEVEL2","LEVEL3","LEVEL4"})if(a.find(name)){
  auto definition=dok::load_level(a,name);dok::World w(definition);initial(w);dok::Animations player(w,a);
  // Class5 is valid but absent from supplied LEVELs; preserve an unplaced actor.
  auto synthetic=definition;auto actor=synthetic.objects.at(*w.player);actor.class_id=5;
  unsigned free_glyph=0;while(synthetic.glyph_index[free_glyph]>=0)++free_glyph;
  actor.glyph=static_cast<std::uint8_t>(free_glyph);synthetic.glyph_index[free_glyph]=static_cast<int>(synthetic.objects.size());synthetic.objects.push_back(actor);
  dok::World with_actor(synthetic);initial(with_actor);check(with_actor.objects.back().motion&&!with_actor.objects.back().pose);
  for(auto& o:w.objects)if(o.animation){++animations;const auto owner=o.owner;auto& s=*o.animation;
   const auto frames=a.animation(definition.objects[o.definition_index].animation);
   check(s.frame==0&&s.remaining==0&&s.interval==dok::original::signed16(frames[0].interval));
   check(s.parameter_a==dok::original::signed16(frames[0].parameter_a)&&s.parameter_b==dok::original::signed16(frames[0].parameter_b));
   const auto count=w.events.events(owner).size();check(player.activate(owner)&&!player.activate(owner));check(w.events.events(owner).size()==count+1);
   for(unsigned n=0;n<200;++n){w.events.dispatch([]{return 0xffffffffu;},[&](auto id,auto cb){if(id==owner&&cb==static_cast<dok::CallbackId>(dok::GameEvent::base_animation))player.event(id);});}
   check(s.remaining==0&&s.frame==200%frames.size());
   // The original getter can run between exhaustion and cancellation.
   check(player.activate(owner)&&w.events.events(owner).size()==2&&s.remaining==200);
   s.remaining=0;check(player.activate(owner)&&w.events.events(owner).size()==3);
   s.remaining=0;check(!player.activate(owner)&&s.remaining==200&&w.events.events(owner).size()==3);
   s.remaining=0;
   w.events.dispatch([]{return 0xffffffffu;},[&](auto id,auto cb){if(id==owner&&cb==static_cast<dok::CallbackId>(dok::GameEvent::base_animation))player.event(id);});
   check(w.events.events(owner).empty());check(player.image(owner)->pixels==frames[s.frame].image.pixels);
   check(player.activate(owner));
   // Rebuilding immutable bindings must not reset an active saved world.
   dok::Animations rebound(w,a);check(s.remaining==200&&rebound.image(owner)->pixels==player.image(owner)->pixels);
   w.events.destroy(owner);
  }
  dok::World slots(definition);dok::Projectiles shots(slots,a);dok::Weapons weapons(slots,a);
  {dok::Monsters monsters(slots,a,shots);check(bool(shots.monster_hit));}check(!shots.monster_hit);
  const auto source=slots.objects.at(*slots.player).owner;
  // Exhaust all unreserved slots; failed spawn cannot alter live slots/events.
  const auto template_it=std::find_if(definition.objects.begin(),definition.objects.end(),[](const auto& d){return d.class_id==8;});
  if(template_it!=definition.objects.end()){
   const auto available=static_cast<unsigned>(std::count(definition.glyph_index.begin(),definition.glyph_index.end(),-1));
   for(unsigned n=0;n<available;++n)shots.spawn(source,template_it->glyph);
   check(shots.active()==available);check(shots.spawn(source,template_it->glyph)==0&&shots.active()==available);
   for(auto& p:slots.projectiles)if(p){p->impact=true;p->frame=32766;}
   for(unsigned sweep=0;sweep<256&&shots.active();++sweep)slots.events.dispatch([]{return 0xffffffffu;},[&](auto id,auto cb){if(cb==static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))shots.event(id);});
   check(!shots.active());for(auto v:slots.projectile_occupancy)check(v==-1);
  }
  for(const auto& d:definition.objects)if(d.class_id==8){
   unsigned reused=256;
   for(int iteration=0;iteration<2;++iteration){shots.spawn(source,d.glyph);unsigned found=256;for(unsigned i=0;i<256;++i)if(slots.projectiles[i]){found=i;break;}
    check(found<256);if(iteration)check(found==reused);reused=found;
    auto& p=*slots.projectiles[found];check(!p.impact&&!p.effect_frame&&!p.occupied_cell&&p.source==source&&p.frame==0);
    p.impact=true;p.actor_impact=false;p.frame=32766;
    slots.events.dispatch([]{return 0xffffffffu;},[&](auto id,auto cb){if(cb==static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))shots.event(id);});
    check(!slots.projectiles[found]&&slots.events.events(dok::Projectiles::owner_base+found).empty());++reuses;
   }
  }
  w.notify("test");w.map_revealed=true;w.level_complete=true;w.visited[0]=1;
  dok::World fresh(definition);initial(fresh);dok::Animations fresh_animation(fresh,a);
  for(const auto& o:fresh.objects)if(o.animation)check(o.animation->frame==0&&!o.animation->remaining);
  ++levels;std::cout<<dok::dataset_name(a.identify())<<' '<<name<<" lifecycle/reset PASS\n";
 }}check(levels==7&&animations>0&&reuses>0);std::cout<<levels<<" worlds, "<<animations<<" base animations, "<<reuses<<" projectile reuse cases\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
