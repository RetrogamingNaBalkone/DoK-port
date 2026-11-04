#include "dok/movement.hpp"
#include "dok/scene.hpp"
#include "dok/items.hpp"
#include "dok/interactions.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::invalid_argument("Usage: dok_walk_probe RES");
        const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
        unsigned rendered=0;
        const auto levels=archive.identify()==dok::DatasetId::old_gold?
            std::vector<const char*>{"LEVEL4"}:std::vector<const char*>{"LEVEL1","LEVEL2","LEVEL3"};
        for(const auto* name:levels){
            dok::Pose expected{};
            for(int fps:{30,60,144}){
                dok::World world(dok::load_level(archive,name));
                dok::SoftwareScene scene(archive,world.definition);
                const auto initial=*world.objects.at(*world.player).pose;
                std::uint64_t previous=0;
                // Three input phases with exact boundaries shared by all frame rates.
                for(unsigned phase=0;phase<3;++phase){
                    dok::InputActions actions;
                    actions.forward=phase==2?-1.0f:1.0f;
                    actions.strafe=phase==1?1.0f:0.0f;
                    actions.turn=phase==0?0.0f:(phase==1?0.5f:-1.0f);
                    for(int frame=1;frame<=fps*2;++frame){
                        const auto now=phase*2000000000ULL+static_cast<std::uint64_t>(frame)*1000000000ULL/fps;
                        world.advance_events(now-previous,[&](dok::OwnerId owner,dok::CallbackId callback){
                            if(callback!=static_cast<dok::CallbackId>(dok::GameEvent::player_tick)){
                                dok::door_event(world,owner,static_cast<dok::GameEvent>(callback));return;
                            }
                            dok::player_pickup(world);dok::player_sensors(world);
                            auto movement=actions;const auto slow=dok::player_surface(world);movement.slow=movement.slow||slow;
                            dok::update_player_motion(world,movement,static_cast<std::uint32_t>(world.simulation_ticks()));
                        });
                        previous=now;
                        const auto pose=*world.objects.at(*world.player).pose;
                        try{scene.render(world,pose);}catch(const std::exception& e){
                            std::cerr<<name<<" fps="<<fps<<" phase="<<phase<<" frame="<<frame<<" x="<<pose.x<<" z="<<pose.z<<" heading="<<pose.heading<<'\n';throw;
                        }
                        ++rendered;
                    }
                }
                const auto final=*world.objects.at(*world.player).pose;
                if(final.x==initial.x&&final.z==initial.z)throw std::runtime_error("Player did not move");
                if(fps==30)expected=final;
                else if(final.x!=expected.x||final.z!=expected.z||final.heading!=expected.heading)throw std::runtime_error("Frame-rate dependent motion");
                std::cout<<name<<" fps="<<fps<<" x="<<final.x<<" z="<<final.z<<" heading="<<final.heading<<'\n';
            }
        }
        std::cout<<"Rendered "<<rendered<<" moving frames; all replay endpoints match\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
