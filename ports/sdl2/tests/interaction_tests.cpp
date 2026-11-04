#include "dok/interactions.hpp"
#include "dok/scene.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static dok::LevelDefinition fixture(){
    dok::LevelDefinition d;d.dataset=dok::DatasetId::kremlin_pkr;
    d.width=d.length=4;d.height=1;d.cell_side=480;d.cell_height=960;d.min_distance=238;
    d.glyph_index.fill(-1);
    dok::ObjectDefinition empty;empty.glyph=' ';empty.flags=0x10;
    dok::ObjectDefinition hero;hero.glyph='P';hero.class_id=1;hero.height_parameter=300;
    hero.fields.push_back({dok::FieldKind::word,0x32,0,{}});
    for(int i=0;i<11;++i)hero.fields.push_back({dok::FieldKind::word,0x5e + i*2,0,{}});
    dok::ObjectDefinition door;door.glyph='d';door.class_id=7;door.flags=0x40;
    door.fields={{dok::FieldKind::word,0x5c,3,{}},{dok::FieldKind::word,0x5e,0,{}},{dok::FieldKind::long_integer,0x66,40000,{}}};
    d.objects={empty,hero,door};d.glyph_index[' ']=0;d.glyph_index['P']=1;d.glyph_index['d']=2;
    for(auto name:{"tmap","mmap","fcmap"})d.maps[name].assign(16,' ');
    d.maps["mmap"][9]='P';d.maps["tmap"][5]='d';return d;
}
static void advance(dok::World& w,std::uint64_t ns){
    w.advance_events(ns,[&](dok::OwnerId owner,dok::CallbackId id){
        if(id!=static_cast<dok::CallbackId>(dok::GameEvent::player_tick))dok::door_event(w,owner,static_cast<dok::GameEvent>(id));
    });
}
static void scenarios(){
    dok::World w(fixture());auto& door=w.objects[2];auto& state=*door.door;
    check(dok::player_use(w)==dok::UseResult::locked,"Missing key must lock door");
    w.objects[1].player_state->attributes[10]=2;
    check(dok::player_use(w)==dok::UseResult::scheduled,"Any matching key must unlock");
    check(w.objects[1].player_state->keys()==2,"Use consumed key");
    advance(w,250000000);check(state.phase==7&&!(door.flags&0x10),"Door passability before threshold");
    advance(w,50000000);check(state.phase==8&&(door.flags&0x10),"Door must pass after quarter threshold");
    check(dok::player_use(w)==dok::UseResult::ignored,"Moving door accepted Use");
    advance(w,1000000000);check(state.state==1&&state.ready&&state.phase==29,"Door did not finish opening");
    door.door_occupants=1;
    check(dok::player_use(w)==dok::UseResult::scheduled,"Manual close scheduling");
    advance(w,2000000000);check(state.phase==29,"Occupied door closed");
    door.door_occupants=0;advance(w,2000000000);
    check(state.state==2&&state.phase==0&&door.flags==0x40,"Door flags not restored");
    auto automatic=fixture();automatic.objects[2].fields[1].number=1;
    dok::World a(automatic);a.objects[1].player_state->attributes[10]=1;
    check(dok::player_use(a)==dok::UseResult::scheduled,"AUTO open");
    advance(a,5000000000ULL);check(a.objects[2].door->state==1&&a.objects[2].door->phase==29,"AUTO skipped second delay");
    advance(a,5000000000ULL);check(a.objects[2].door->state==2,"AUTO failed to close");
    auto sensor_level=fixture();
    dok::ObjectDefinition sensor;sensor.class_id=13;sensor.glyph='f';
    sensor.fields={{dok::FieldKind::character,0x66,'d',{}},{dok::FieldKind::long_integer,0x67,0x800,{}}};
    sensor_level.glyph_index['f']=3;sensor_level.objects.push_back(sensor);
    sensor_level.maps["fcmap"][9]='f';sensor_level.maps["fcmap"][10]='f';
    dok::World sensors(sensor_level);
    dok::player_sensors(sensors);check(sensors.events.events(3).size()==1,"Sensor must bypass keys");
    sensors.objects[1].pose->x+=480;dok::player_sensors(sensors);
    check(sensors.events.events(3).size()==1,"Same glyph cell retriggered sensor");
    sensors.objects[1].pose->x+=480;dok::player_sensors(sensors);
    sensors.objects[1].pose->x-=480;dok::player_sensors(sensors);
    check(sensors.events.events(3).size()==2,"Re-entering sensor must trigger again");
    dok::Image image;image.width=96;image.height=3;image.flags=0x48;image.pixels.assign(288,42);
    dok::DoorState crop;crop.extent=30;crop.phase=29;
    auto out=dok::door_image(image,crop);
    check(out.pixels[86]==0&&out.pixels[87]==42&&out.pixels[96]==0,"Native divide-before-multiply crop");
    image.width=3;image.height=96;image.flags=0x108;out=dok::door_image(image,crop);
    check(out.pixels[260]==0&&out.pixels[261]==42,"Vertical crop");
    for(auto profile:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke}){
    std::vector<std::uint64_t> reference;
    for(unsigned fps:{30,60,144}){
        auto level=automatic;level.dataset=profile;
        dok::World replay(level);replay.objects[1].player_state->attributes[10]=1;dok::player_use(replay);
        std::vector<std::uint64_t> trace;std::uint64_t previous=0;
        for(unsigned f=1;f<=fps*10;++f){
            const auto now=std::uint64_t(f)*1000000000ULL/fps;
            replay.advance_events(now-previous,[&](dok::OwnerId owner,dok::CallbackId id){
                if(id==static_cast<dok::CallbackId>(dok::GameEvent::player_tick))return;
                dok::door_event(replay,owner,static_cast<dok::GameEvent>(id));
                trace.push_back((replay.simulation_ticks()<<16)|(replay.objects[2].door->phase<<8)|replay.objects[2].flags);
            });previous=now;
        }
        if(reference.empty())reference=trace;else check(trace==reference,"Door cadence depends on FPS");
    }
    }
    auto og_level=fixture();og_level.dataset=dok::DatasetId::old_gold;
    dok::World og(og_level);auto& og_door=og.objects[2];og.objects[1].player_state->attributes[10]=1;
    check(dok::player_use(og)==dok::UseResult::scheduled,"OG door use");
    advance(og,300000000);check(og_door.door->phase==8&&!(og_door.flags&0x10),"OG door passed before completed OPEN");
    advance(og,1000000000);check(og_door.door->state==1&&(og_door.flags&0x10),"OG door did not pass at OPEN completion");
    check(dok::player_use(og)==dok::UseResult::scheduled,"OG close use");
    advance(og,50000000);check(!(og_door.flags&0x10),"OG closing door retained passability");
    advance(og,1500000000);check(og_door.flags==0x48,"OG must restore per-step saved flags, including bit8");
    check(!og.visible_message().empty(),"Native door notification missing");
}
static void real_maps(const char* path){
    auto archive=dok::Archive::open(std::filesystem::u8path(path));unsigned doors=0,frames=0,sensors=0;
    const auto levels=archive.identify()==dok::DatasetId::old_gold?
        std::vector<const char*>{"LEVEL4"}:std::vector<const char*>{"LEVEL1","LEVEL2","LEVEL3"};
    for(auto name:levels){
        dok::World w(dok::load_level(archive,name));dok::SoftwareScene scene(archive,w.definition);
        const auto owner=w.objects.at(*w.player).owner;w.objects.at(*w.player).player_state->attributes[10]=-1;
        for(auto& object:w.objects)if(object.door){check(dok::use_door(w,object.owner,owner)==dok::UseResult::scheduled,"Actual door scheduling");++doors;}
        for(unsigned i=0;i<40;++i){
            advance(w,33333333);auto pose=*w.objects.at(*w.player).pose;
            for(int h:{0,45,90,135,180,225,270,315}){pose.heading=static_cast<std::int16_t>(h);scene.render(w,pose);++frames;}
        }
        for(auto& object:w.objects)if(object.door)check(object.door->state==1,"Actual door not open");
        for(std::size_t cell=0;cell<w.maps.at("fcmap").size();++cell){
            const auto* surface=w.definition.object(w.maps.at("fcmap")[cell]);
            if(surface->class_id!=13)continue;
            dok::World entry(dok::load_level(archive,name));
            auto& actor=entry.objects.at(*entry.player);
            actor.pose->x=static_cast<int>(cell%entry.definition.width)*entry.definition.cell_side+entry.definition.cell_side/2;
            actor.pose->z=static_cast<int>(cell/entry.definition.width)*entry.definition.cell_side+entry.definition.cell_side/2;
            actor.player_state->attributes[10]=0;
            dok::player_sensors(entry);advance(entry,1500000000);
            const auto glyph=static_cast<std::uint8_t>(surface->number(0x66));
            check(entry.objects.at(entry.definition.glyph_index[glyph]).door->state==1,"Actual sensor did not open target");++sensors;
        }
    }
    std::cout<<doors<<" actual doors; "<<frames<<" animated scene frames; "<<sensors<<" sensor cells\n";
}
// One callback per fixture, paired with the independent original-instruction
// oracle. Nonzero intervals exclude the port's explicit FPS adaptation.
static void native_matrix(){
    for(auto profile:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke})
    for(auto event:{dok::GameEvent::door_open,dok::GameEvent::door_close,dok::GameEvent::door_auto})
    for(int state:{1,2})for(int phase:{0,7,8,28,29})for(int occupants:{0,1})for(bool ready:{false,true})
    for(int flags:{0x42,0x5a}){
        auto level=fixture();level.dataset=profile;dok::World w(level);
        auto& object=w.objects[2];auto& door=*object.door;
        door.phase=phase;door.extent=30;door.interval=7;door.state=state;door.ready=ready;
        door.saved_flags=0x2442;object.flags=flags;object.door_occupants=occupants;
        w.events.schedule(object.owner,static_cast<dok::CallbackId>(event),1,0);
        w.events.dispatch([]{return 2u;},[&](auto owner,auto callback){dok::door_event(w,owner,static_cast<dok::GameEvent>(callback));});
        std::cout<<static_cast<int>(profile)<<','<<static_cast<int>(event)<<','<<state<<','<<phase<<','<<occupants<<','<<ready<<','<<flags<<','
                 <<door.phase<<','<<int(door.state)<<','<<door.ready<<','<<object.flags<<','<<door.saved_flags<<'\n';
    }
}
int main(int argc,char** argv){
    try{if(argc==2&&std::string(argv[1])=="--native-matrix"){native_matrix();return 0;}
        scenarios();for(int i=1;i<argc;++i)real_maps(argv[i]);std::cout<<"Door keys, thresholds, occupancy, AUTO, crop and FPS checks passed\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
