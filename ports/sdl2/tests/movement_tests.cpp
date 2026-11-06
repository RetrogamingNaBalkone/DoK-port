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
static dok::World bank_fixture(bool transpose=false){
    auto level=fixture();level.dataset=dok::DatasetId::old_gold;dok::World w(level);
    for(auto& glyph:w.maps.at("mmap"))glyph=' ';
    auto& pose=*w.objects.at(*w.player).pose;
    pose.x=transpose?2108:1680;pose.z=transpose?1680:2108;
    for(auto cell:{std::pair<int,int>{2,3},{4,3},{3,2}}){
        const auto x=transpose?cell.second:cell.first,z=transpose?cell.first:cell.second;
        w.maps.at("tmap")[z*8+x]='#';
    }
    w.maps.at("mmap")[(pose.z/480)*8+pose.x/480]='P';return w;
}
static void current_boundaries(){
    for(bool z_axis:{false,true})for(int delta:{-100,100}){
        auto w=bank_fixture(z_axis);auto& actor=w.objects.at(*w.player);const auto before=*actor.pose;
        dok::move_actor_current(w,actor.owner,static_cast<std::int16_t>(delta),z_axis);
        const auto clipped=delta>0?1681:1678;
        check(actor.pose->x==(z_axis?before.x:clipped)&&actor.pose->z==(z_axis?clipped:before.z));
        auto open=fixture();open.dataset=dok::DatasetId::old_gold;dok::World free(open);
        auto& p=free.objects.at(*free.player);const auto origin=*p.pose;
        dok::move_actor_current(free,p.owner,static_cast<std::int16_t>(delta),z_axis);
        check(p.pose->x==origin.x+(z_axis?0:delta)&&p.pose->z==origin.z+(z_axis?delta:0));
    }
    // Ordinary category100 remains native, including its perpendicular snap;
    // only the surface caller opts into the fixed-cadence current adaptation.
    auto native=bank_fixture();const auto start=*native.objects.at(*native.player).pose;
    dok::change_player_stat(native,100,100);check(native.objects.at(*native.player).pose->z!=start.z);
    auto level=fixture();level.dataset=dok::DatasetId::old_gold;
    dok::ObjectDefinition door;door.glyph='d';door.class_id=7;door.flags=0x10;
    level.glyph_index['d']=int(level.objects.size());level.objects.push_back(door);
    dok::World door_world(level);auto& actor=door_world.objects.at(*door_world.player);
    auto& door_state=door_world.objects.back();door_state.door_occupants=1;
    for(auto& glyph:door_world.maps.at("mmap"))glyph=' ';
    actor.pose->x=1870;actor.pose->z=1680;
    door_world.maps.at("mmap")[3*8+3]='P';door_world.maps.at("tmap")[3*8+3]='d';
    dok::move_actor_current(door_world,actor.owner,100,false);check(door_state.door_occupants==0);
    dok::move_actor_current(door_world,actor.owner,-100,false);check(door_state.door_occupants==1);
    check(door_world.maps.at("mmap")[3*8+3]=='P');
}
static void original_current_bank(const char* path){
    const auto a=dok::Archive::open(std::filesystem::u8path(path));
    for(unsigned fps:{30,60,144}){
        dok::World w(dok::load_level(a,"LEVEL4"));auto& actor=w.objects.at(*w.player);
        const auto hero=w.definition.objects[actor.definition_index].glyph;
        for(auto& glyph:w.maps.at("mmap"))if(glyph==hero)glyph=' ';
        // Explicit local regression fixture, not a claimed spawn-to-exit route.
        actor.pose->x=14616;actor.pose->z=19599;actor.pose->heading=0;
        w.maps.at("mmap")[58*64+43]=hero;
        dok::InputActions input;input.forward=-1;input.run=true;
        unsigned ticks=0;std::uint64_t previous=0;
        for(unsigned frame=1;frame<=fps&&ticks<3;++frame){
            const auto now=std::uint64_t(frame)*1000000000ULL/fps;
            w.advance_events(now-previous,[&](auto,auto){
                if(ticks>=3)return;
                dok::player_pickup(w);dok::player_surface(w);
                dok::update_player_motion(w,input,static_cast<std::uint32_t>(w.simulation_ticks()),false);++ticks;
            });previous=now;
        }
        check(ticks==3&&actor.player_state->score==1&&w.maps.at("tmap")[57*64+43]==' ');
        std::cout<<"OG current bank coin43,57: fps="<<fps<<" ticks="<<ticks<<" pose="<<actor.pose->x<<','<<actor.pose->z<<" score="<<actor.player_state->score<<'\n';
    }
}
static void movement_host_guards(){
    unsigned cases=0;
    for(auto profile:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke}){
        // Open border makes bounds protection independent of wall resolution.
        for(bool invalid_origin:{false,true})for(auto target:{std::pair<int,int>{-1000,1680},{5000,1680},{1680,-1000},{1680,5000},{2100,1680},{1680,2100},{2400,2400},{1680,1680}}){
            auto level=fixture();level.dataset=profile;level.maps["tmap"].assign(64,' ');
            if(invalid_origin)level.maps["tmap"][3*8+3]='#';
            else level.maps["tmap"][3*8+4]='#';
            dok::World w(level);auto& actor=w.objects.at(*w.player);
            dok::move_actor_to(w,actor.owner,target.first,target.second);
            const auto p=*actor.pose;check(p.x>=238&&p.z>=238&&p.x<3840-238&&p.z<3840-238);
            const auto at=(p.z/480)*8+p.x/480;check(w.maps.at("mmap")[at]=='P');
            w.maps.at("mmap")[at]=' ';
            if(!invalid_origin)check(dok::footprint_clear(w,p.x,p.z));
            ++cases;
        }
        auto level=fixture();level.dataset=profile;dok::World w(level);auto& actor=w.objects.at(*w.player);
        const auto p=*actor.pose;const auto at=(p.z/480)*8+p.x/480;w.maps.at("mmap")[at]='#';
        dok::move_actor_to(w,actor.owner,p.x+60,p.z);check(actor.pose->x==p.x&&actor.pose->z==p.z&&w.maps.at("mmap")[at]=='#');++cases;
    }
    std::cout<<cases<<" host bounds/valid-origin footprint/foreign-occupancy guard cases passed\n";
}
int main(int argc,char** argv){
    try{
        movement_host_guards();
        current_boundaries();
        // Presentation-only bobbing toggle also cancels GL's residual Y offset.
        for(auto profile:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke}){
            auto level=fixture();level.dataset=profile;dok::World world(level);
            auto& actor=world.objects.at(*world.player);
            for(int y:{-326,-300,-274}){
                actor.pose->y=y;const auto before=*actor.pose;
                for(double fraction:{0.0,0.5,1.0}){
                    const std::array<double,4> offset{{12*fraction,4*fraction,-8*fraction,2*fraction}};
                    const auto native=dok::player_view(world,true,offset),stable=dok::player_view(world,false,offset);
                    check(native.pose.y==y&&native.offset==offset);
                    check(stable.pose.y==(profile==dok::DatasetId::old_gold?-300:y));
                    check(stable.offset[1]==(profile==dok::DatasetId::old_gold?0:offset[1]));
                    check(stable.pose.x==before.x&&stable.pose.z==before.z&&stable.pose.heading==before.heading);
                    for(int i:{0,2,3})check(stable.offset[i]==offset[i]);
                    check(actor.pose->y==before.y);
                    const auto preview=dok::player_view(world,false);
                    check(preview.pose.y==stable.pose.y&&preview.offset==std::array<double,4>{});
                }
            }
        }
        if(argc==2)original_current_bank(argv[1]);
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
