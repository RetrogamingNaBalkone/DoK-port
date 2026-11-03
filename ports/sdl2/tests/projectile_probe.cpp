#include "dok/projectiles.hpp"
#include "dok/weapons.hpp"
#include "dok/items.hpp"
#include "dok/movement.hpp"
#include "dok/scene.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static void advance(dok::World& w,dok::Projectiles& shots,std::uint64_t ns){
    w.advance_events(ns,[&](dok::OwnerId owner,dok::CallbackId id){
        if(id==static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))shots.event(owner);
    });
}
static unsigned first(const dok::World& w){for(unsigned i=0;i<256;++i)if(w.projectiles[i])return i;throw std::runtime_error("No projectile");}
static void arena(dok::World& w,int wall_row=20){
    auto& d=w.definition;std::uint8_t wall=0;
    for(const auto& object:d.objects)if(object.class_id==2){wall=object.glyph;break;}
    check(wall!=0,"No wall in source level");
    std::fill(w.maps.at("tmap").begin(),w.maps.at("tmap").end(),' ');
    std::fill(w.maps.at("mmap").begin(),w.maps.at("mmap").end(),' ');
    for(int z=0;z<d.length;++z)for(int x=0;x<d.width;++x)
        if(!x||!z||x==d.width-1||z==d.length-1||z==wall_row)w.maps.at("tmap")[z*d.width+x]=wall;
    auto& actor=w.objects.at(*w.player);actor.pose=dok::Pose{10*d.cell_side+d.cell_side/2,-123,10*d.cell_side+d.cell_side/2,0};
    w.maps.at("mmap")[10*d.width+10]=d.objects[actor.definition_index].glyph;
}
int main(int argc,char** argv){
    try{
        if(argc<2)throw std::invalid_argument("Usage: dok_projectile_probe RES...");
        unsigned cases=0;
        for(int arg=1;arg<argc;++arg){const auto archive=dok::Archive::open(std::filesystem::u8path(argv[arg]));
            for(auto name:{"LEVEL1","LEVEL2","LEVEL3"}){
                auto definition=dok::load_level(archive,name);
                for(auto glyph:{'1','2'}){
                    std::vector<std::uint64_t> reference;
                    for(unsigned fps:{30,60,144}){
                        dok::World w(definition);arena(w);dok::Projectiles shots(w,archive);dok::SoftwareScene scene(archive,w.definition);
                        const auto source=w.objects.at(*w.player).owner;
                        const auto starting_health=w.objects.at(*w.player).player_state->health();
                        const auto cost=shots.spawn(source,glyph);check(cost==(glyph=='1'?14:4),"Wrong spawn cost");
                        const auto slot=first(w);check(w.projectiles[slot]->pose.y==-250,"Projectile copied player height");
                        std::vector<std::uint64_t> trace;std::uint64_t previous=0;bool impact_seen=false,effect_seen=false,visible=false;
                        for(unsigned f=1;f<=fps*3;++f){
                            const auto now=std::uint64_t(f)*1000000000ULL/fps;
                            w.advance_events(now-previous,[&](dok::OwnerId owner,dok::CallbackId id){
                                if(id!=static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))return;
                                shots.event(owner);
                                if(w.projectiles[slot]){const auto& p=*w.projectiles[slot];
                                    trace.push_back((w.simulation_ticks()<<24)|(std::uint64_t(p.pose.z&65535)<<8)|(p.frame<<2)|p.impact);
                                    if(p.impact){impact_seen=true;effect_seen|=p.effect_frame;
                                        if(!p.effect_frame)check(shots.images()[0].image->pixels==archive.animation(definition.object(glyph)->animation)[0].image.pixels,"Contact lost flight image");
                                    }
                                    else if(p.occupied_cell)check(!dok::footprint_clear(w,p.pose.x,p.pose.z),"Projectile occupancy did not block actor footprint");
                                }else trace.push_back(w.simulation_ticks()<<24);
                            });previous=now;
                            const auto pose=*w.objects.at(*w.player).pose;
                            if(!visible&&shots.active()){
                                const auto baseline=scene.render(w,pose).pixels;
                                visible=scene.render(w,pose,shots.images()).pixels!=baseline;
                            }else scene.render(w,pose,shots.images());
                        }
                        check(impact_seen&&effect_seen&&!shots.active(),"Impact lifecycle did not finish");
                        check(visible,"Dynamic projectile/effect never changed scene pixels");
                        check(w.objects.at(*w.player).player_state->health()==starting_health,"Projectile hit its source class");
                        check(std::all_of(w.projectile_occupancy.begin(),w.projectile_occupancy.end(),[](auto v){return v<0;}),"Projectile leaked occupancy");
                        check(w.events.events(dok::Projectiles::owner_base+slot).empty(),"Projectile leaked event");
                        check(shots.spawn(source,glyph)==cost&&first(w)==slot,"Dynamic slot was not reused");
                        if(reference.empty())reference=trace;else check(reference==trace,"Projectile trace depends on display FPS");
                        ++cases;
                    }
                }
                dok::World w(definition);arena(w,14);dok::Projectiles shots(w,archive);
                auto& player=w.objects.at(*w.player);dok::WorldObject* monster=nullptr;
                for(auto& object:w.objects)if(object.monster){monster=&object;break;}
                check(monster!=nullptr,"No monster");
                monster->pose=dok::Pose{player.pose->x,-300,12*definition.cell_side+definition.cell_side/2,180};
                w.maps.at("mmap")[12*definition.width+10]=definition.objects[monster->definition_index].glyph;
                const auto health=monster->monster->health;
                shots.spawn(player.owner,'1');advance(w,shots,60000000);
                check(monster->monster->state==1&&monster->monster->health==health,"First contact must wake without damage");
                shots.spawn(player.owner,'1');advance(w,shots,60000000);
                check(monster->monster->state==3&&monster->monster->health==std::max(0,health-14),"Active monster damage");
                player.player_state->attributes[0]=100;
                shots.spawn(monster->owner,'1');advance(w,shots,60000000);
                check(player.player_state->health()==86,"Enemy-owned projectile did not hit player");
                ++cases;
                dok::World integrated(definition);arena(integrated);dok::Weapons weapons(integrated,archive);dok::Projectiles fired(integrated,archive);
                auto& hero=integrated.objects.at(*integrated.player);
                for(const auto& item:definition.objects)if(item.class_id==10&&item.number(0x81)==1)
                    integrated.maps.at("tmap")[10*definition.width+10]=item.glyph;
                const auto picked=dok::player_pickup(integrated);check(picked!=0,"Integration pickup");weapons.picked_up(picked);
                auto tick=[&](std::uint64_t ns){integrated.advance_events(ns,[&](dok::OwnerId owner,dok::CallbackId id){
                    const auto event=static_cast<dok::GameEvent>(id);
                    if(event==dok::GameEvent::projectile_tick)fired.event(owner);
                    else if(event!=dok::GameEvent::player_tick)weapons.event(owner,event);
                });};
                tick(350000000);const auto charge=hero.player_state->charge();
                check(weapons.fire([&](auto glyph){return fired.spawn(hero.owner,glyph);}),"Actual weapon did not spawn projectile");
                check(fired.active()==1&&hero.player_state->charge()==charge-14,"Actual weapon charge/spawn integration");
                tick(3000000000ULL);check(!fired.active(),"Integrated shot did not finish");++cases;
                dok::World pool(definition);arena(pool);dok::Projectiles pooled(pool,archive);
                const auto source=pool.objects.at(*pool.player).owner;
                const auto expected=std::count_if(definition.glyph_index.begin(),definition.glyph_index.end(),[](auto i){return i<0;});
                unsigned allocated=0;while(allocated<256&&pooled.spawn(source,'1'))++allocated;
                check(allocated==expected&&pooled.active()==allocated&&!pooled.spawn(source,'1'),"Projectile pool capacity/overflow");++cases;
            }
        }
        std::cout<<cases<<" projectile scenarios: lifecycle, damage, immunity, rendering, slot reuse and FPS passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
