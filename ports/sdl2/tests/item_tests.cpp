#include "dok/items.hpp"
#include "dok/interactions.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static dok::LevelDefinition fixture(){
    dok::LevelDefinition d;d.dataset=dok::DatasetId::kremlin_pkr;
    d.width=d.length=4;d.height=1;d.cell_side=480;d.cell_height=960;d.glyph_index.fill(-1);
    dok::ObjectDefinition empty;empty.glyph=' ';empty.flags=0x10;
    dok::ObjectDefinition hero;hero.glyph='P';hero.class_id=1;
    hero.fields.push_back({dok::FieldKind::word,0x32,0,{}});
    for(int i=0;i<11;++i)hero.fields.push_back({dok::FieldKind::word,0x5e + i*2,i<4?100:0,{}});
    dok::ObjectDefinition item;item.glyph='@';item.class_id=3;item.flags=0x18;
    item.fields={{dok::FieldKind::word,0x54,0,{}},{dok::FieldKind::word,0x56,10,{}}};
    d.objects={empty,hero,item};d.glyph_index[' ']=0;d.glyph_index['P']=1;d.glyph_index['@']=2;
    for(auto name:{"tmap","mmap","fcmap"})d.maps[name].assign(16,' ');
    d.maps["mmap"][9]='P';d.maps["tmap"][9]='@';d.maps["tmap"][10]='@';return d;
}
static void scenarios(){
    dok::World w(fixture());auto& p=*w.objects[1].player_state;
    check(!dok::player_pickup(w),"Full health consumed pickup");
    dok::change_player_stat(w,0,-15);check(p.health()==85,"Damage");
    check(dok::player_pickup(w)==3&&p.health()==95,"Heal pickup");
    check(w.maps.at("tmap")[5]==' '&&w.maps.at("tmap")[6]=='@',"Pickup changed shared glyph");
    check(!dok::player_pickup(w),"Empty cell picked twice");
    p.attributes[0]=32760;p.attributes[2]=32767;dok::change_player_stat(w,0,20);
    check(p.health()==0,"ADD16 must wrap before clamp");
    check(dok::change_player_stat(w,0,-10)&&p.health()==0,"Damage at zero must be accepted");
    check(dok::change_player_stat(w,0,0),"Zero delta rejected");
    dok::change_player_stat(w,2,1);check(p.health()==-32768&&p.max_health()==-32768,"Maximum ADD16");
    dok::change_player_stat(w,10,1);dok::change_player_stat(w,10,4);check(p.keys()==5,"Key OR");
    dok::change_player_stat(w,99,32767);dok::change_player_stat(w,99,1);check(p.score==-32768,"Money wrap");
    dok::change_player_stat(w,200,0);check(w.map_revealed,"Map pickup");
    w.total_money=1;p.score=0;check(dok::change_player_stat(w,1000,1)&&!w.level_complete,"Premature exit");
    p.score=1;check(!dok::change_player_stat(w,1000,1)&&w.level_complete&&p.health()==-1,"Exit condition");
    bool rejected=false;try{dok::change_player_stat(w,11,1);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Unsupported native padding write accepted");
    auto weapon_level=fixture();auto& weapon=weapon_level.objects[2];weapon.class_id=10;
    weapon.fields[0].number=3;weapon.fields[1].number=50;weapon.fields.push_back({dok::FieldKind::word,0x81,1,{}});
    dok::World weapons(weapon_level);auto& inventory=*weapons.objects[1].player_state;
    dok::player_pickup(weapons);check(inventory.current_weapon==1&&inventory.pending_weapon==1&&inventory.weapons[1]==3,"First weapon registration");
    check(inventory.charge()==150&&inventory.max_charge()==150,"Weapon charge");
    weapons.objects[1].pose->x+=480;dok::player_pickup(weapons);
    check(inventory.max_charge()==200&&inventory.weapons[1]==3,"Duplicate weapon must replenish");
    auto key_level=fixture();key_level.objects[2].fields[0].number=10;key_level.objects[2].fields[1].number=4;
    dok::ObjectDefinition door;door.glyph='d';door.class_id=7;door.flags=0x40;
    door.fields={{dok::FieldKind::word,0x5c,4,{}},{dok::FieldKind::word,0x5e,0,{}},{dok::FieldKind::long_integer,0x66,0,{}}};
    key_level.objects.push_back(door);key_level.glyph_index['d']=3;key_level.maps["tmap"][5]='d';
    dok::World keys(key_level);
    check(dok::player_use(keys)==dok::UseResult::locked,"Lock without key");
    dok::player_pickup(keys);check(dok::player_use(keys)==dok::UseResult::scheduled,"Picked key did not unlock door");
    auto surface_level=fixture();surface_level.maps["tmap"].assign(16,' ');
    dok::ObjectDefinition surface;surface.glyph='c';surface.class_id=12;
    surface.fields={{dok::FieldKind::word,0x66,100,{}},{dok::FieldKind::word,0x68,1400,{}}};
    surface_level.objects.push_back(surface);surface_level.glyph_index['c']=3;surface_level.maps["fcmap"][9]='c';
    dok::World conveyor(surface_level);auto& moving=*conveyor.objects[1].pose;const auto start=moving;
    check(dok::player_surface(conveyor)&&moving.x==start.x+46&&moving.z==start.z,"Conveyor reference step");
    dok::change_player_stat(conveyor,102,-46);check(moving.z==start.z-46,"Z displacement");
    check(conveyor.maps.at("mmap")[5]=='P',"Conveyor lost occupancy");
    check(conveyor.maps.at("fcmap")[5]=='c',"Surface removed");
    surface_level.objects[3].fields[0].number=1000;surface_level.objects[3].fields[1].number=1;
    dok::World exit(surface_level);check(dok::player_surface(exit)&&exit.level_complete,"Exit surface condition");
}
static void real_maps(const char* path){
    const auto archive=dok::Archive::open(std::filesystem::u8path(path));unsigned count=0;
    for(auto name:{"LEVEL1","LEVEL2","LEVEL3"}){
        const auto definition=dok::load_level(archive,name);dok::World initial(definition);
        for(std::size_t cell=0;cell<initial.maps.at("tmap").size();++cell){
            const auto* object=definition.object(initial.maps.at("tmap")[cell]);
            if(object->class_id!=3&&object->class_id!=10)continue;
            dok::World world(definition);auto& actor=world.objects.at(*world.player);auto& p=*actor.player_state;
            actor.pose->x=static_cast<int>(cell%definition.width)*definition.cell_side+definition.cell_side/2;
            actor.pose->z=static_cast<int>(cell/definition.width)*definition.cell_side+definition.cell_side/2;
            p.attributes[0]=50;p.attributes[1]=0;
            check(dok::player_pickup(world)!=0,"Placed item rejected");
            check(world.visible_message()==object->resource(0x58).substr(0,38),"Pickup message differs from original LEVEL text");
            unsigned changed=0;
            for(std::size_t i=0;i<initial.maps.at("tmap").size();++i)changed+=initial.maps.at("tmap")[i]!=world.maps.at("tmap")[i];
            check(changed==1&&world.maps.at("tmap")[cell]==' ',"Placed pickup cell mutation");
            const auto category=object->number(0x54),value=object->number(0x56);
            if(category==0)check(p.health()==50+value,"Placed health value");
            else if(category==1)check(p.charge()==value,"Placed charge value");
            else if(category==10)check(p.keys()==value,"Placed key value");
            else if(category==200)check(world.map_revealed,"Placed map flag");
            else if(category==3)check(p.max_charge()==initial.objects.at(*initial.player).player_state->max_charge()+value,"Placed max charge");
            else throw std::runtime_error("Unexpected placed item category");
            ++count;
        }
    }
    std::cout<<count<<" placed pickups checked\n";
}
int main(int argc,char** argv){
    try{scenarios();for(int i=1;i<argc;++i)real_maps(argv[i]);std::cout<<"Stats, pickups and weapon registration passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
