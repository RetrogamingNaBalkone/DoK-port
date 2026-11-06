#include "dok/items.hpp"
#include "dok/interactions.hpp"
#include "dok/weapons.hpp"
#include "dok/flow.hpp"
#include <iostream>
static std::string hx(const std::string&s){const char*d="0123456789abcdef";std::string o;for(unsigned char c:s){o+=d[c>>4];o+=d[c&15];}return o;}
int main(int argc,char**argv){try{if(argc!=3)return 2;auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));auto d=dok::load_level(archive,argv[2]);int mode,glyph,x,y,z;
 while(std::cin>>mode>>glyph>>x>>y>>z){dok::World w(d);auto&actor=w.objects.at(*w.player);auto&player=*actor.player_state;player.attributes[1]=player.attributes[3]=100;auto&object=w.objects.at(d.glyph_index.at(glyph));std::string trace;
  w.audio=[&](const dok::AudioCommand&c){trace+=std::to_string(int(c.action))+","+std::to_string(c.id)+","+hx(c.resource)+","+std::to_string(player.charge())+";";};
  if(mode==0){auto&door=*object.door;door.state=x;door.phase=y;door.extent=30;door.interval=7;door.ready=true;door.saved_flags=0x2442;object.door_occupants=0;w.events.schedule(object.owner,1,7,0);if(z==4){player.attributes[10]=0;dok::use_door(w,object.owner,actor.owner);}else dok::door_event(w,object.owner,z==1?dok::GameEvent::door_open:z==2?dok::GameEvent::door_close:dok::GameEvent::door_auto);}
  else if(mode==1){const auto p=*actor.pose;const int at=(p.z/d.cell_side)*d.width+p.x/d.cell_side;w.maps.at("tmap")[at]=glyph;player.attributes[0]=player.attributes[1]=x;player.attributes[2]=player.attributes[3]=100;dok::player_pickup(w);}
  else if(mode==2){dok::Weapons weapons(w,archive);player.weapons[0]=object.owner;player.current_weapon=0;player.attributes[1]=x;object.weapon->state=y;weapons.fire([&](auto){return static_cast<std::int16_t>(z);});}
  else if(mode==3){player.attributes[0]=x;player.lives=y;dok::GameFlow flow(w);flow.observe();}
  else if(mode==4){player.attributes[0]=x;dok::change_player_stat(w,0,static_cast<std::int16_t>(y));}
  std::cout<<player.charge()<<"|"<<trace<<"\n";
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
