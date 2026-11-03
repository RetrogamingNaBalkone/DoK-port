#include "dok/flow.hpp"
#include "dok/text.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool v){if(!v)throw std::runtime_error("Flow invariant failed");}
static dok::LevelDefinition fixture(){
    dok::LevelDefinition d;d.width=d.length=3;d.height=1;d.cell_side=480;d.cell_height=640;
    d.glyph_index.fill(-1);
    dok::ObjectDefinition blank;blank.glyph=' ';blank.class_id=0;
    dok::ObjectDefinition hero;hero.glyph='P';hero.class_id=1;hero.height_parameter=300;
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
    std::cout<<"Death/retry preservation, lives WORD boundaries, game over and finish precedence passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
