#include "dok/cheat.hpp"
#include "dok/items.hpp"
#include "dok/projectiles.hpp"
#include "dok/weapons.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Case {
    dok::World world;
    dok::Weapons weapons;
    dok::Projectiles shots;
    unsigned fired=0;
    bool held=false;
    explicit Case(const dok::Archive& a,const std::string& level):world(dok::load_level(a,level)),weapons(world,a),shots(world,a){}
    dok::PlayerState& player(){return *world.objects.at(*world.player).player_state;}
    dok::WeaponState* gun(){const auto owner=player().weapons.at(player().current_weapon);return owner?&*world.object_for_owner(owner).weapon:nullptr;}
    void code(){dok::CheatSequence sequence;bool matched=false;for(char key:std::string("IDKFA"))if(sequence.push(key)){weapons.grant_all();matched=true;}check(matched,"Code did not match");}
    void frame(unsigned fps){world.advance_events(1000000000ULL/fps,[&](dok::OwnerId owner,dok::CallbackId cb){
        const auto event=static_cast<dok::GameEvent>(cb);
        if(event==dok::GameEvent::player_tick){if(held&&weapons.fire([&](auto glyph){const auto cost=shots.spawn(owner,glyph);if(cost)++fired;return cost;})){};}
        else if(event==dok::GameEvent::weapon_tick||event==dok::GameEvent::weapon_switch)weapons.event(owner,event);
        else if(event==dok::GameEvent::projectile_tick)shots.event(owner);
    });}
    void wait(unsigned fps,unsigned ms){for(unsigned n=0;n<(fps*ms+999)/1000;++n)frame(fps);}
    void natural_pickup(unsigned slot){
        const auto& d=world.definition;auto& actor=world.objects.at(*world.player);auto& map=world.maps.at("mmap");
        const int old=actor.pose->z/d.cell_side*d.width+actor.pose->x/d.cell_side;
        const auto glyph=d.objects[actor.definition_index].glyph;if(map[old]==glyph)map[old]=' ';
        for(std::size_t at=0;at<world.maps.at("tmap").size();++at){
            const auto& item=*d.object(world.maps.at("tmap")[at]);if(item.class_id!=10||item.number(0x81)!=int(slot))continue;
            actor.pose->x=int(at%d.width)*d.cell_side+d.cell_side/2;actor.pose->z=int(at/d.width)*d.cell_side+d.cell_side/2;map[at]=glyph;
            const auto owner=dok::player_pickup(world);check(owner!=0,"Natural source-cell pickup failed");weapons.picked_up(owner);return;
        }
        throw std::runtime_error("No placed weapon for pickup");
    }
};
void cycling_without_other(const dok::Archive& a){
    const auto level=a.identify()==dok::DatasetId::old_gold?"LEVEL4":"LEVEL1";
    Case c(a,level);c.code();c.wait(60,1000);
    auto& p=c.player();const auto slot=p.current_weapon;const auto owner=p.weapons.at(slot);
    p.weapons.fill(0);p.weapons.at(slot)=owner;
    check(!c.weapons.cycle(1)&&!c.weapons.cycle(-1)&&p.current_weapon==slot,"Cycling one owned weapon must do nothing");
    p.weapons.fill(0);
    check(!c.weapons.cycle(1)&&!c.weapons.cycle(-1)&&p.current_weapon==slot,"Cycling without weapons must do nothing");
}
void repeat_switch_while_hidden(const dok::Archive& a){
    const auto level=a.identify()==dok::DatasetId::old_gold?"LEVEL4":"LEVEL1";
    Case c(a,level);c.code();c.wait(60,1000);
    const auto first=c.player().current_weapon;check(c.weapons.cycle(1),"First cycle request");
    bool hidden=false;
    for(int i=0;i<1000&&c.player().current_weapon==first;++i){
        c.frame(1000);
        if(c.player().current_weapon==first&&c.gun()->state==4){hidden=true;break;}
    }
    check(hidden,"Need hidden current gun awaiting player switch callback");
    check(c.weapons.cycle(1),"Repeated cycle during hidden handoff");
    c.wait(60,1000);
    check(c.player().current_weapon!=first&&c.gun()->state==7,"Repeated cycle froze weapon hide/show state machine");
    const auto fired=c.fired;c.held=true;c.wait(60,1000);
    check(c.fired>fired,"Fire blocked after repeated switch");
}
void refill(const dok::Archive& a,const std::string& level){
    Case c(a,level);c.code();
    const auto maximum=c.player().max_charge(),health=c.player().health();
    const auto selected=c.player().current_weapon;
    check(maximum>0&&c.player().charge()==maximum,"First IDKFA must fill charge");
    for(const auto remaining:{std::int16_t(0),std::int16_t(maximum/2),maximum}){
        c.player().attributes[1]=remaining;c.code();
        check(c.player().charge()==maximum&&c.player().max_charge()==maximum,"IDKFA refill/capacity regression");
        check(c.player().health()==health&&c.player().current_weapon==selected,"IDKFA refill changed health/equipped weapon");
    }
}
void scenario(const dok::Archive& a,const std::string& level,unsigned fps,int timing){
    Case c(a,level);
    if(timing==0)c.code();
    else if(timing==1){c.natural_pickup(1);c.code();} // During first show.
    else if(timing==2){c.natural_pickup(1);c.wait(fps,1000);c.code();}
    else{c.code();c.wait(fps,1000);}
    c.held=true;
    if(timing==3){c.weapons.select(2);c.code();} // Hide/switch pending.
    if(timing==4){c.weapons.select(2);c.wait(fps,100);c.code();} // Second slow show.
    for(unsigned frame=0;frame<fps*6;++frame){
        if(timing==5&&c.gun()&&c.gun()->state==2)c.code(); // Every fire animation frame.
        if(timing==6&&c.gun()&&c.gun()->state==3)c.code(); // Every recovery frame.
        if(frame==fps*2)c.weapons.select(2);
        if(frame==fps*4)c.weapons.select(1);
        c.frame(fps);
    }
    check(c.fired>2&&c.player().charge()>0,"IDKFA fire pipeline stopped or exhausted unexpectedly");
    const auto fired=c.fired;c.wait(fps,2000);check(c.fired>fired,"Held fire stopped after IDKFA/switch");
    const auto maximum=c.player().max_charge();c.code();
    check(c.player().max_charge()==maximum&&c.player().charge()==maximum,"Repeated IDKFA must refill without increasing capacity");
    c.wait(fps,1500);check(c.fired>fired+1,"Repeated IDKFA stopped sustained fire");
    std::cout<<"IDKFA "<<level<<" fps="<<fps<<" timing="<<timing<<" fired="<<c.fired<<" charge="<<c.player().charge()<<" state="<<c.gun()->state<<" events="<<c.world.events.events(c.player().weapons.at(c.player().current_weapon)).size()<<'\n';
}
}
int main(int argc,char** argv){try{
    check(argc>=2,"Usage: dok_idkfa_tests RES...");unsigned count=0;
    for(int arg=1;arg<argc;++arg){const auto archive=dok::Archive::open(std::filesystem::u8path(argv[arg]));cycling_without_other(archive);repeat_switch_while_hidden(archive);
        for(const std::string level:{"LEVEL1","LEVEL2","LEVEL3","LEVEL4"})if(archive.find(level)){refill(archive,level);for(unsigned fps:{30,60,144})for(int timing=0;timing<7;++timing){scenario(archive,level,fps,timing);++count;}}}
    std::cout<<count<<" IDKFA timing/pickup/switch/sustained real-projectile scenarios passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
