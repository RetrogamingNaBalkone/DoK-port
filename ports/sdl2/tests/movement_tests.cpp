#include "dok/movement.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool value){if(!value)throw std::runtime_error("Movement scenario failed");}
static dok::LevelDefinition fixture(){
    dok::LevelDefinition level;level.dataset=dok::DatasetId::kremlin_pkr;
    level.width=level.length=8;level.height=1;level.cell_side=480;level.cell_height=960;level.min_distance=238;
    level.glyph_index.fill(-1);
    dok::ObjectDefinition empty;empty.glyph=' ';empty.class_id=0;empty.flags=0x10;
    dok::ObjectDefinition wall;wall.glyph='#';wall.class_id=2;wall.flags=2;
    dok::ObjectDefinition hero;hero.glyph='P';hero.class_id=1;hero.flags=4;hero.height_parameter=300;
    for(int i=0;i<11;++i)hero.fields.push_back({dok::FieldKind::word,0x5e + i*2,100,{}});
    hero.fields.push_back({dok::FieldKind::word,0x32,0,{}});
    hero.fields.push_back({dok::FieldKind::word,0x34,5,{}});
    hero.fields.push_back({dok::FieldKind::long_integer,0x38,120,{}});
    level.objects={empty,wall,hero};level.glyph_index[' ']=0;level.glyph_index['#']=1;level.glyph_index['P']=2;
    level.maps["tmap"].assign(64,' ');level.maps["mmap"].assign(64,' ');level.maps["fcmap"].assign(64,' ');
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(!x||!y||x==7||y==7)level.maps["tmap"][y*8+x]='#';
    level.maps["mmap"][4*8+3]='P';return level;
}
int main(){
    try{
        dok::World world(fixture());auto& actor=world.objects.at(*world.player);const auto owner=actor.owner;
        auto initial=*actor.pose;check(initial.x==1680&&initial.z==1680);
        dok::move_actor(world,owner,-120,false);check(actor.pose->z==initial.z+90&&actor.pose->x==initial.x);
        dok::move_actor(world,owner,120,true);check(actor.pose->x==initial.x+90);
        for(int i=0;i<100;++i)dok::move_actor(world,owner,120,true);
        check(actor.pose->x==6*480+241); // Solid neighbour: original cell edge.
        const auto x=actor.pose->x,z=actor.pose->z;dok::move_actor(world,owner,-120,false);
        check(actor.pose->x==x&&actor.pose->z==z+90); // Slide along the wall.
        auto glyph=world.definition.objects[actor.definition_index].glyph;
        check(world.maps.at("mmap")[(actor.pose->z/480)*8+actor.pose->x/480]==glyph);
        std::vector<dok::Pose> finals;
        for(auto fps:{30,60,144}){
            dok::World replay(fixture());dok::InputActions actions;actions.forward=1;actions.strafe=1;actions.turn=0.5f;
            std::uint64_t previous=0;
            for(std::uint64_t frame=1;frame<=static_cast<unsigned>(fps)*2;++frame){
                auto elapsed=frame*1000000000ULL/fps;
                replay.advance_events(elapsed-previous,[&](dok::OwnerId,dok::CallbackId){dok::update_player_motion(replay,actions,static_cast<std::uint32_t>(replay.simulation_ticks()));});
                previous=elapsed;
            }
            finals.push_back(*replay.objects.at(*replay.player).pose);
        }
        for(const auto& p:finals)check(p.x==finals[0].x&&p.z==finals[0].z&&p.heading==finals[0].heading);
        check(finals[0].heading!=0&&finals[0].x!=initial.x&&finals[0].z!=initial.z);
        dok::World mouse(fixture());dok::apply_mouse_yaw(mouse,0.4f);dok::apply_mouse_yaw(mouse,0.7f);
        check(mouse.objects.at(*mouse.player).pose->heading==359);
        std::cout<<"Quarter steps, walls, sliding, occupancy and combined 30/60/144 FPS replay passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
