#include "dok/movement.hpp"
#include "dok/items.hpp"
#include <iostream>
#include <stdexcept>

static std::uint32_t hash(const std::vector<std::uint8_t>& bytes){
    std::uint32_t h=2166136261u;for(auto b:bytes)h=(h^b)*16777619u;return h;
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Usage: dok_movement_probe RES LEVEL");
    auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
    const auto definition=dok::load_level(archive,argv[2]);
    dok::World base(definition);const auto hero=definition.objects.at(*base.player).glyph;
    for(auto& glyph:base.maps.at("mmap"))if(glyph==hero)glyph=' ';
    int op;std::int32_t x,z,nx,nz;
    while(std::cin>>op>>x>>z>>nx>>nz){
        auto pose=*base.objects.at(*base.player).pose;pose.x=x;pose.z=z;
        if(op==0){const auto result=dok::resolve_movement(base,pose,nx,nz);std::cout<<result.x<<','<<result.z<<'\n';}
        else if(op==1)std::cout<<dok::footprint_clear(base,x,z)<<'\n';
        else if(op==2){
            dok::World w(definition);w.maps=base.maps;
            auto& actor=w.objects.at(*w.player);actor.pose=pose;actor.pose->heading=static_cast<std::int16_t>(nz);
            const auto cell=(z/definition.cell_side)*definition.width+x/definition.cell_side;
            w.maps.at("mmap").at(cell)=hero;
            int strafe;std::cin>>strafe;dok::move_actor(w,actor.owner,static_cast<std::int16_t>(nx),strafe);
            std::uint32_t counters=2166136261u;
            for(unsigned glyph=0;glyph<256;++glyph){const auto index=definition.glyph_index[glyph];
                const auto value=index<0?0:w.objects[index].door_occupants;
                counters=(counters^std::uint8_t(value))*16777619u;counters=(counters^std::uint8_t(value>>8))*16777619u;}
            std::cout<<actor.pose->x<<','<<actor.pose->y<<','<<actor.pose->z<<','<<hash(w.maps.at("mmap"))<<','<<counters<<'\n';
        }else if(op==3||op==4){
            // Controlled open geometry isolates elapsed/input arithmetic from
            // the independently checked original-map collision functions.
            dok::World w(definition);for(auto& pair:w.maps)pair.second.assign(pair.second.size(),' ');
            auto& actor=w.objects.at(*w.player);actor.pose=pose;
            unsigned elapsed;int forward,strafe,turn,run,slow,aa,ab,direction,last_forward,last_strafe;
            std::cin>>elapsed>>forward>>strafe>>turn>>run>>slow>>aa>>ab>>direction>>last_forward>>last_strafe;
            actor.pose->heading=static_cast<std::int16_t>(nx);
            actor.motion->acceleration=aa;actor.motion->turn_acceleration=ab;actor.motion->turn_direction=direction;
            actor.motion->bob_direction=static_cast<std::int16_t>(nz);
            actor.motion->last_forward=static_cast<float>(last_forward);actor.motion->last_strafe=static_cast<float>(last_strafe);
            actor.motion->clock.start(0);
            dok::InputActions input;input.forward=static_cast<float>(forward);input.strafe=static_cast<float>(strafe);
            input.turn=static_cast<float>(turn);input.run=run;input.slow=slow;
            int count=1;if(op==4)std::cin>>count;
            for(int step=1;step<=count;++step)dok::update_player_motion(w,input,elapsed*static_cast<unsigned>(step));
            std::cout<<actor.pose->x<<','<<actor.pose->y<<','<<actor.pose->z<<','<<actor.pose->heading<<','
                     <<actor.motion->acceleration<<','<<actor.motion->turn_acceleration<<','<<actor.motion->turn_direction<<','<<actor.motion->bob_direction<<'\n';
        }else if(op==5||op==6){
            auto level=definition;for(auto& pair:level.maps)pair.second.assign(pair.second.size(),' ');
            level.maps["mmap"][0]=hero;
            dok::ObjectDefinition water;water.glyph='~';water.class_id=12;
            water.fields={{dok::FieldKind::word,0x66,nx,{}},{dok::FieldKind::word,0x68,nz,{}}};
            level.glyph_index['~']=int(level.objects.size());level.objects.push_back(water);
            level.maps["fcmap"].assign(level.maps["fcmap"].size(),'~');
            dok::World w(level);auto& actor=w.objects.at(*w.player);actor.pose=pose;
            int count;std::cin>>count;
            if(op==5){for(int step=0;step<count;++step)dok::player_surface(w);}
            else for(int frame=0;frame<count;++frame){
                const auto dt=(std::uint64_t(frame+1)*1000000000/count)-(std::uint64_t(frame)*1000000000/count);
                w.advance_events(dt,[&](auto,auto){dok::player_surface(w);});
            }
            std::cout<<actor.pose->x<<','<<actor.pose->z<<'\n';
        }else throw std::runtime_error("Unknown movement operation");
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
