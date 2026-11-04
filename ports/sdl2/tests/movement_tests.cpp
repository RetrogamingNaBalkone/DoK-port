#include "dok/movement.hpp"
#include "dok/items.hpp"
#include "dok/view_motion.hpp"
#include <algorithm>
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
        // OG applies class12 WORD values directly; Kremlin alone divides by
        // reference FPS and slows motion. In particular -1 must hurt in OG.
        for(auto profile:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke})
        for(int category:{0,100,102})for(int value:{-100,-1,1,100}){
            auto level=fixture();level.dataset=profile;
            dok::ObjectDefinition surface;surface.glyph='~';surface.class_id=12;
            surface.fields={{dok::FieldKind::word,0x66,category,{}},{dok::FieldKind::word,0x68,value,{}}};
            level.glyph_index['~']=int(level.objects.size());level.objects.push_back(surface);
            level.maps["fcmap"].assign(64,'~');dok::World w(level);
            auto& actor=w.objects.at(*w.player);const auto before=*actor.pose;
            actor.player_state->attributes[0]=50;
            const bool og=profile==dok::DatasetId::old_gold;
            check(dok::player_surface(w)==!og);
            const int delta=og?value:value/30;
            if(category==0)check(actor.player_state->health()==std::clamp(50+delta,0,100));
            else{check(actor.pose->x==before.x+(category==100?delta:0));check(actor.pose->z==before.z+(category==102?delta:0));}
        }
        for(auto profile:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke})for(bool inertia:{false,true}){
            std::vector<dok::Pose> replay_poses;
            for(int fps:{30,60,144}){
                auto level=fixture();level.dataset=profile;dok::World replay(level);std::uint64_t previous=0;
                for(std::uint64_t frame=1;frame<=unsigned(fps);++frame){const auto elapsed=frame*1000000000ULL/fps;
                    replay.advance_events(elapsed-previous,[&](auto,auto){
                        const auto tick=std::uint32_t(replay.simulation_ticks());dok::InputActions a;
                        if(tick<2500){a.forward=0.5f;a.strafe=0.5f;}else if(tick<5000)a.forward=-1;
                        auto& actor=replay.objects.at(*replay.player);const auto before=*actor.pose;
                        dok::update_player_motion(replay,a,tick,inertia);
                        if(!inertia&&!a.forward&&!a.strafe)check(before.x==actor.pose->x&&before.z==actor.pose->z);
                        if(!inertia&&(a.forward||a.strafe))check(actor.motion->acceleration==10000);
                    });previous=elapsed;
                }
                replay_poses.push_back(*replay.objects.at(*replay.player).pose);
            }
            for(const auto& pose:replay_poses)check(pose.x==replay_poses[0].x&&pose.z==replay_poses[0].z);
        }
        // Whole-tick interpolation must include forced current motion, even with no input.
        for(bool inertia:{false,true})for(int category:{100,102})for(int force:{-1400,1400})for(int fps:{30,60,144}){
            auto level=fixture();dok::ObjectDefinition water;water.glyph='~';water.class_id=12;
            water.fields={{dok::FieldKind::word,0x66,category,{}},{dok::FieldKind::word,0x68,force,{}}};
            level.glyph_index['~']=int(level.objects.size());level.objects.push_back(water);
            level.maps["fcmap"].assign(64,'~');dok::World current(level);dok::ViewMotion view;
            auto& pose=*current.objects.at(*current.player).pose;
            const auto origin=pose;const int axis=category==100?0:2;const double delta=force/30;
            std::uint64_t previous=0;double previous_visual=axis==0?origin.x:origin.z;
            for(std::uint64_t frame=1;frame<=unsigned(fps)/2;++frame){
                const auto elapsed=frame*1000000000ULL/fps;
                current.advance_events(elapsed-previous,[&](auto,auto){
                    const double tick=current.simulation_ticks();
                    const auto before=pose;const auto old_offset=view.offset(tick);
                    view.step(pose,tick,[&]{
                        dok::InputActions input;input.slow=dok::player_surface(current);
                        dok::update_player_motion(current,input,std::uint32_t(tick),inertia);
                    });
                    const auto offset=view.offset(tick);
                    // No immediate camera jump on a simulation tick.
                    check(std::abs((axis==0?pose.x:pose.z)+offset[axis]-(axis==0?before.x:before.z)-old_offset[axis])<1e-8);
                });
                previous=elapsed;const double now=current.presentation_ticks();
                const double visual=(axis==0?pose.x:pose.z)+view.offset(now)[axis];
                const double expected=(axis==0?origin.x:origin.z)+delta*std::max(0.0,now-416)/416;
                check(std::abs(visual-expected)<1e-8);
                check((visual-previous_visual)*force>=0);previous_visual=visual;
            }
        }
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
