#include "dok/flow.hpp"
#include "dok/text.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
static void verify(bool v,const char* expression,int line){if(!v)throw std::runtime_error("Flow invariant at line "+std::to_string(line)+": "+expression);}
#define check(v) verify((v),#v,__LINE__)
static dok::LevelDefinition fixture(){
    dok::LevelDefinition d;d.dataset=dok::DatasetId::kremlin_pkr;d.width=d.length=3;d.height=1;d.cell_side=480;d.cell_height=640;d.min_distance=100;
    d.glyph_index.fill(-1);
    dok::ObjectDefinition blank;blank.glyph=' ';blank.class_id=0;blank.flags=16;
    dok::ObjectDefinition hero;hero.glyph='P';hero.class_id=1;hero.height_parameter=300;hero.flags=4;
    for(int i=0;i<11;++i)hero.fields.push_back({dok::FieldKind::word,0x5e + i*2,100,{}});
    hero.fields.push_back({dok::FieldKind::word,0x32,90,{}});
    d.objects={blank,hero};d.glyph_index[' ']=0;d.glyph_index['P']=1;
    d.maps["tmap"]=std::vector<std::uint8_t>(9,' ');d.maps["fcmap"]=d.maps["tmap"];
    d.maps["mmap"]=d.maps["tmap"];d.maps["mmap"][4]='P';return d;
}
int main(){try{
    const std::string source="ONE TWO THREE\r\n\r\nFOUR\n$IGNORED";
    const auto pages=dok::paginate_story({reinterpret_cast<const std::uint8_t*>(source.data()),source.size()},7,2);
    check(pages.size()==2&&pages[0][0]=="ONE TWO"&&pages[0][1]=="THREE"&&pages[1][0].empty()&&pages[1][1]=="FOUR");
    dok::World notification(fixture());notification.notify(std::string(45,'A'));
    check(notification.visible_message()==std::string(38,'A'));
    notification.advance_events(3999000000ULL,[](auto,auto){});check(!notification.visible_message().empty());
    notification.advance_events(2000000ULL,[](auto,auto){});check(notification.visible_message().empty());
    notification.notify("B");check(notification.visible_message()=="B");
    for(const auto dataset:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke}){
        auto d=fixture();d.dataset=dataset;
        d.objects[1].fields.push_back({dok::FieldKind::sound,0xa6,0,"death"});
        dok::World w(d);dok::GameFlow flow(w);std::vector<dok::AudioCommand> commands;
        w.audio=[&](const auto& command){commands.push_back(command);};
        w.objects.at(*w.player).player_state->attributes[0]=0;
        check(flow.observe()==dok::SessionPhase::retry);
        const bool og=dataset==dok::DatasetId::old_gold;
        check(commands.size()==(og?2u:1u));
        if(og)check(commands.front().action==dok::AudioAction::stop_all);
        check(commands.back().action==dok::AudioAction::once&&commands.back().resource=="death");
        const auto count=commands.size();flow.observe();check(commands.size()==count);
    }
    for(int lives:{3,1,0,-32768}){
        dok::World w(fixture());dok::GameFlow flow(w);auto& actor=w.objects.at(*w.player);auto& p=*actor.player_state;
        const auto spawn=*actor.pose;const auto owner=actor.owner;
        w.maps["mmap"][4]=' ';w.maps["mmap"][0]='P';actor.pose->x=240;actor.pose->z=240;actor.pose->heading=270;
        p.lives=static_cast<std::int16_t>(lives);p.attributes[0]=0;p.attributes[1]=17;p.attributes[10]=5;p.score=7;
        p.weapons[1]=owner;p.current_weapon=1;w.visited[7]=1;w.map_revealed=true;
        w.maps["tmap"][6]='P';actor.motion->acceleration=42;
        const auto before=w.events.events(owner)[0];
        const auto phase=flow.observe();const auto left=p.lives;
        check(left==(lives==-32768?32767:lives-1));check(flow.observe()==phase&&p.lives==left);
        if(lives==1){check(phase==dok::SessionPhase::game_over&&!flow.retry());continue;}
        check(phase==dok::SessionPhase::retry&&flow.retry()&&!flow.retry());
        check(actor.pose->x==spawn.x&&actor.pose->z==spawn.z&&actor.pose->y==spawn.y&&actor.pose->heading==spawn.heading);
        check(w.maps["mmap"][0]==' '&&w.maps["mmap"][4]=='P'&&w.maps["tmap"][6]=='P');
        check(p.health()==p.max_health()&&p.charge()==17&&p.keys()==5&&p.score==7&&p.current_weapon==1&&p.weapons[1]==owner);
        check(w.visited[7]&&w.map_revealed&&actor.motion->acceleration==42);
        check(w.events.events(owner)[0].deadline==before.deadline&&w.simulation_ticks()==0);
    }
    for(bool sentinel:{false,true}){dok::World w(fixture());dok::GameFlow flow(w);auto& p=*w.objects.at(*w.player).player_state;
        if(sentinel)p.attributes[0]=-1;else{w.level_complete=true;p.attributes[0]=0;}
        check(flow.observe()==dok::SessionPhase::completed&&p.lives==3&&!flow.retry());}
    {
        auto d=fixture();dok::ObjectDefinition enemy;enemy.glyph='E';enemy.class_id=9;enemy.flags=4;enemy.fields.push_back({dok::FieldKind::word,0xa4,100,{}});
        d.glyph_index['E']=int(d.objects.size());d.objects.push_back(enemy);
        dok::World w(d);dok::GameFlow flow(w);auto& actor=w.objects.at(*w.player);
        actor.pose->x=actor.pose->z=240;w.maps["mmap"][0]='P';w.maps["mmap"][4]='E';actor.player_state->attributes[0]=0;
        check(flow.observe()==dok::SessionPhase::retry&&flow.retry());
        check(w.maps["mmap"][4]=='E'&&w.maps["mmap"][3]=='P'&&w.maps["mmap"][0]==' ');
        check(actor.pose->x==240&&actor.pose->z==720&&actor.player_state->health()==100);
    }
    {
        auto d=fixture();dok::ObjectDefinition door;door.glyph='D';door.class_id=7;door.flags=16;
        d.glyph_index['D']=int(d.objects.size());d.objects.push_back(door);d.maps["tmap"][0]='D';
        dok::World w(d);dok::GameFlow flow(w);auto& actor=w.objects.at(*w.player);auto& gate=w.objects.back();
        actor.pose->x=actor.pose->z=240;w.maps["mmap"][4]=' ';w.maps["mmap"][0]='P';w.maps["tmap"][0]='D';gate.door_occupants=1;actor.player_state->attributes[0]=0;
        check(flow.observe()==dok::SessionPhase::retry&&flow.retry()&&gate.door_occupants==0);
        check(!flow.retry()&&gate.door_occupants==0);
        dok::World spawn(d);spawn.maps["tmap"][4]='D';dok::GameFlow spawn_flow(spawn);auto& p=spawn.objects.at(*spawn.player);
        p.pose->x=p.pose->z=240;spawn.maps["mmap"][4]=' ';spawn.maps["mmap"][0]='P';p.player_state->attributes[0]=0;
        check(spawn_flow.observe()==dok::SessionPhase::retry&&spawn_flow.retry()&&spawn.objects.back().door_occupants==1);
    }
    {
        auto d=fixture();dok::ObjectDefinition wall;wall.glyph='#';wall.class_id=2;wall.flags=2;
        d.glyph_index['#']=int(d.objects.size());d.objects.push_back(wall);std::fill(d.maps["tmap"].begin(),d.maps["tmap"].end(),'#');
        dok::World w(d);dok::GameFlow flow(w);auto& actor=w.objects.at(*w.player);actor.player_state->attributes[0]=0;
        const auto pose=*actor.pose;const auto maps=w.maps;
        check(flow.observe()==dok::SessionPhase::retry&&!flow.retry());
        check(w.maps==maps&&actor.pose->x==pose.x&&actor.pose->z==pose.z&&actor.player_state->health()==0);
    }
    std::cout<<"Death/retry preservation, lives WORD boundaries, game over and finish precedence passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
