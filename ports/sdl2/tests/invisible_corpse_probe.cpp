#include "dok/monsters.hpp"
#include "dok/movement.hpp"
#include "dok/interactions.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

static void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
static std::size_t cell(const dok::World& w,const dok::Pose& p){return std::size_t(p.z/w.definition.cell_side)*w.definition.width+p.x/w.definition.cell_side;}
static bool blank(const dok::Image& image){return std::none_of(image.pixels.begin(),image.pixels.end(),[](auto p){return p!=0;});}
struct Run{
    dok::World world;dok::Projectiles shots;dok::Monsters monsters;unsigned fps,stops=0;std::uint64_t frame=0,elapsed=0;bool walk=false;
    Run(const dok::LevelDefinition& d,const dok::Archive& a,unsigned rate):world(d),shots(world,a),monsters(world,a,shots),fps(rate){
        world.audio=[&](const auto& command){if(command.action==dok::AudioAction::stop)++stops;};
    }
    void advance(){const auto ns=++frame*1000000000ULL/fps;world.advance_events(ns-elapsed,[&](auto owner,auto id){
        if(id==static_cast<dok::CallbackId>(dok::GameEvent::monster_tick))monsters.event(owner);
        else if(id==static_cast<dok::CallbackId>(dok::GameEvent::player_tick)&&walk){dok::InputActions input;input.forward=1;dok::update_player_motion(world,input,world.simulation_ticks(),false);}
        else if(id!=static_cast<dok::CallbackId>(dok::GameEvent::player_tick))dok::door_event(world,owner,static_cast<dok::GameEvent>(id));
    });elapsed=ns;}
    void place(dok::WorldObject& actor,std::size_t at){auto& map=world.maps.at("mmap");const auto glyph=world.definition.objects[actor.definition_index].glyph;
        if(actor.pose&&map[cell(world,*actor.pose)]==glyph)map[cell(world,*actor.pose)]=' ';
        actor.pose->x=int(at%world.definition.width)*world.definition.cell_side+world.definition.cell_side/2;
        actor.pose->z=int(at/world.definition.width)*world.definition.cell_side+world.definition.cell_side/2;map[at]=glyph;
    }
};
static std::pair<std::size_t,int> neighbor(Run& run,std::size_t at){
    auto& w=run.world;const auto width=w.definition.width,side=w.definition.cell_side;
    for(auto delta:std::vector<std::pair<int,int>>{{-width,0},{width,180},{-1,270},{1,90}}){
        const auto index=std::ptrdiff_t(at)+delta.first;if(index<0||index>=std::ptrdiff_t(w.maps.at("mmap").size()))continue;
        const int x=int(index%width)*side+side/2,z=int(index/width)*side+side/2;
        if(w.maps.at("mmap")[index]==' '&&dok::footprint_clear(w,x,z))return {std::size_t(index),delta.second};
    }
    return {std::size_t(-1),0};
}
static void scenario(const dok::Archive& archive,const dok::LevelDefinition& definition,std::size_t enemy_index,unsigned fps,bool door){
    Run run(definition,archive,fps);auto& w=run.world;auto& enemy=w.objects[enemy_index];auto& hero=w.objects.at(*w.player);
    const auto clip=archive.animation(definition.objects[enemy_index].resource(0xec));
    check(blank(clip.back().image)&&clip.back().interval==34464,"Expected original transparent long tail");
    auto at=cell(w,*enemy.pose);auto adjacent=neighbor(run,at);dok::WorldObject* doorway=nullptr;
    if(door){
        bool found=false;for(std::size_t i=0;i<w.maps.at("tmap").size();++i){auto& object=w.objects[definition.glyph_index[w.maps.at("tmap")[i]]];
            if(!object.door||w.maps.at("mmap")[i]!=' ')continue;
            const auto next=neighbor(run,i);if(next.first==std::size_t(-1))continue;
            doorway=&object;doorway->flags|=0x10;doorway->door->state=1;doorway->door->phase=doorway->door->extent-1;
            at=i;adjacent=next;found=true;break;
        }check(found,"No original door fixture");
        run.place(enemy,adjacent.first);const int x=int(at%definition.width)*definition.cell_side+definition.cell_side/2,z=int(at/definition.width)*definition.cell_side+definition.cell_side/2;
        dok::move_actor_to(w,enemy.owner,x,z);check(cell(w,*enemy.pose)==at&&doorway->door_occupants==1,"Live monster did not enter/count original door");
    }
    check(adjacent.first!=std::size_t(-1),"No passable neighboring original cell");run.place(hero,adjacent.first);hero.pose->heading=adjacent.second;
    const auto target=*enemy.pose;run.monsters.wake(enemy.owner);run.monsters.damage(enemy.owner,enemy.monster->health);
    while(enemy.monster->state!=4)run.advance();
    check(!blank(clip[enemy.monster->display_frame].image),"Expected visible first death frame");
    check(!(enemy.flags&0x10)&&!enemy.monster->passage_released,"Visible body released too early");
    dok::move_actor_to(w,hero.owner,target.x,target.z);check(cell(w,*hero.pose)!=at,"Visible death body did not block movement");
    if(doorway)check(doorway->door_occupants==1,"Visible corpse released door too early");
    while(!blank(clip[enemy.monster->display_frame].image))run.advance();
    check((enemy.flags&0x10)&&enemy.monster->passage_released&&!enemy.monster->corpse,"Transparent tail did not release passage before finalization");
    const auto events=w.events.events(enemy.owner);check(events.size()==1&&events.front().deadline>w.simulation_ticks()+30000,"Transparent tail duration was skipped");
    check(run.stops==0,"Death audio stopped early");if(doorway)check(doorway->door_occupants==0,"Transparent corpse retained door occupancy");
    // Real input, movement clock, quarter steps, collision and map insertion.
    run.walk=true;for(unsigned i=0;i<fps*2&&cell(w,*hero.pose)!=at;++i)run.advance();run.walk=false;
    check(cell(w,*hero.pose)==at,"Ordinary input could not enter transparent corpse cell");
    const auto hero_glyph=definition.objects[hero.definition_index].glyph;
    check(w.maps.at("mmap")[at]==hero_glyph,"Movement did not insert hero");
    if(doorway)check(doorway->door_occupants==1,"Hero entry was not counted after corpse release");
    while(!enemy.monster->corpse)run.advance();
    check(w.maps.at("mmap")[at]==hero_glyph,"Corpse finalization erased entering hero");
    check(w.events.events(enemy.owner).empty()&&run.stops==1,"Death event/audio did not finish normally");
    if(doorway){
        check(doorway->door_occupants==1,"Corpse finalization decremented hero occupancy");
        const int x=int(adjacent.first%definition.width)*definition.cell_side+definition.cell_side/2,z=int(adjacent.first/definition.width)*definition.cell_side+definition.cell_side/2;
        dok::move_actor_to(w,hero.owner,x,z);check(doorway->door_occupants==0,"Hero exit leaked door occupancy");
    }
    std::cout<<dok::dataset_name(definition.dataset)<<" glyph="<<unsigned(definition.objects[enemy_index].glyph)<<" fps="<<fps<<" terrain="<<(door?"door":"original")<<" passage/audio/maps/occupancy PASS\n";
}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: dok_invisible_corpse_probe PKR.RES PKE.RES");unsigned scenarios=0;
    for(int a=1;a<argc;++a){const auto archive=dok::Archive::open(std::filesystem::u8path(argv[a]));const auto definition=dok::load_level(archive,"LEVEL1");dok::World initial(definition);
        std::set<std::string> names;for(std::size_t i=0;i<definition.objects.size();++i){const auto& d=definition.objects[i];if(!initial.objects[i].monster||!initial.objects[i].pose)continue;
            const auto clip=archive.animation(d.resource(0xec));if(!blank(clip.back().image)||clip.back().interval!=34464||names.count(d.resource(0xec)))continue;
            Run probe(definition,archive,60);const auto at=cell(probe.world,*probe.world.objects[i].pose);
            if(definition.object(probe.world.maps.at("tmap")[at])->class_id!=0||neighbor(probe,at).first==std::size_t(-1))continue;
            names.insert(d.resource(0xec));for(unsigned fps:{30,60,144})for(bool door:{false,true}){scenario(archive,definition,i,fps,door);++scenarios;}
        }check(names.size()==2,"Expected boyar and bat transparent tails in LEVEL1");
    }std::cout<<scenarios<<" original LEVEL1 transparent corpse scenarios passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
