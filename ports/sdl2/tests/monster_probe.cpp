#include "dok/monsters.hpp"
#include "dok/arithmetic.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
struct Run {
    dok::World world;dok::Projectiles shots;dok::Monsters monsters;std::uint64_t elapsed=0;bool activate=false;
    Run(dok::LevelDefinition d,const dok::Archive& a):world(std::move(d)),shots(world,a),monsters(world,a,shots){}
    void advance(std::uint64_t ns){world.advance_events(ns,[&](auto owner,auto id){
        if(activate&&id==static_cast<dok::CallbackId>(dok::GameEvent::player_tick))
            monsters.consider(dok::original_cell_bounds(*world.objects.at(*world.player).pose,
                world.definition.cell_side,world.definition.width,world.definition.length));
        if(id==static_cast<dok::CallbackId>(dok::GameEvent::monster_tick))monsters.event(owner);
        else if(id==static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))shots.event(owner);
    });elapsed+=ns;}
    void until(std::uint64_t ticks){const auto ns=(ticks*119000000000ULL+1193181)/1193182;check(ns>=elapsed,"Backward test time");check(ns-elapsed<60000000000ULL,"Unexpected event deadline over 60 seconds away");advance(ns-elapsed);}
    std::uint64_t next(dok::OwnerId owner){for(const auto& e:world.events.events(owner))if(e.callback==static_cast<dok::CallbackId>(dok::GameEvent::monster_tick))return e.deadline;throw std::runtime_error("Missing monster event");}
};
static void arena(Run& r,std::size_t index,int separation){
    auto& w=r.world;const auto& d=w.definition;
    std::uint8_t wall=0;for(const auto& o:d.objects)if(o.class_id==2){wall=o.glyph;break;}
    for(auto map:{"mmap","tmap"})std::fill(w.maps.at(map).begin(),w.maps.at(map).end(),' ');
    for(int z=0;z<d.length;++z)for(int x=0;x<d.width;++x)if(!x||!z||x==d.width-1||z==d.length-1)w.maps.at("tmap")[z*d.width+x]=wall;
    for(auto& o:w.objects)if(o.monster)o.pose.reset();
    auto& enemy=w.objects[index];enemy.pose=dok::Pose{10*d.cell_side+d.cell_side/2,-d.objects[index].height_parameter,10*d.cell_side+d.cell_side/2,0};
    auto& hero=w.objects.at(*w.player);hero.pose=dok::Pose{enemy.pose->x,-300,enemy.pose->z+separation*d.cell_side,180};
    hero.player_state->attributes[0]=hero.player_state->attributes[2]=30000;
    w.maps.at("mmap")[10*d.width+10]=d.objects[index].glyph;
    w.maps.at("mmap")[(10+separation)*d.width+10]=d.objects[hero.definition_index].glyph;
}
static void death(Run& r,std::size_t index,bool terrain){
    auto& enemy=r.world.objects[index];const auto& d=r.world.definition;const auto owner=enemy.owner;
    std::uint8_t obstacle=' ';
    if(terrain){for(const auto& o:d.objects)if(o.class_id==3){obstacle=o.glyph;break;}r.world.maps.at("tmap")[10*d.width+10]=obstacle;}
    r.monsters.damage(owner,enemy.monster->health);
    const auto message=r.world.visible_message();
    check(message==d.objects[index].resource(0xbe),"Death message delayed until animation end");
    const auto message_until=r.world.message_until;
    const auto frames=r.world.definition.objects[index].resource(0xec);
    const auto initial_y=enemy.pose->y;
    r.until(r.next(owner));
    check(enemy.monster->state==4&&enemy.monster->display_clip==2&&enemy.monster->display_frame==0&&!enemy.monster->corpse,"Death first frame");
    const auto expected_y=d.dataset==dok::DatasetId::old_gold?
        dok::original::negate16(static_cast<std::int16_t>(dok::original::sar32(dok::original::negate16(static_cast<std::int16_t>(initial_y)),1))):initial_y;
    check(enemy.pose->y==expected_y,"Profile-specific death height");
    unsigned guard=0;
    while(!enemy.monster->corpse){check(++guard<100,"Death callback loop");r.until(r.next(owner));}
    check(r.world.message_until==message_until,"Corpse repeated death message");
    check(enemy.flags&0x10,"Corpse must allow movement");check(!(enemy.flags&0x400),"Corpse must not be reclaimable");
    check(r.world.events.events(owner).empty(),"Corpse leaked event");
    check(!r.monsters.damage(owner,1),"Corpse absorbed projectile");
    const auto cell=10*d.width+10;
    if(terrain)check(r.world.maps.at("tmap")[cell]==obstacle&&r.world.maps.at("mmap")[cell]==d.objects[index].glyph,"Corpse overwrote occupied TMap");
    else check(r.world.maps.at("tmap")[cell]==d.objects[index].glyph&&r.world.maps.at("mmap")[cell]==' ',"Corpse not placed into empty TMap");
}
static void native_matrix(const dok::Archive& archive){
    auto definition=dok::load_level(archive,archive.identify()==dok::DatasetId::old_gold?"LEVEL4":"LEVEL1");
    for(int health:{-32768,-1,0,1,7,300,32767})for(int state:{0,1,2,3,4})
        for(int amount:{-32768,-5,0,1,7,32767}){
            Run run(definition,archive);auto found=std::find_if(run.world.objects.begin(),run.world.objects.end(),[](const auto& o){return o.monster&&o.pose;});
            check(found!=run.world.objects.end(),"No original monster");auto& m=*found->monster;
            const auto course=static_cast<std::int16_t>(definition.objects[found->definition_index].number(0xae));
            m.health=static_cast<std::int16_t>(health);m.state=static_cast<std::int16_t>(state);
            m.next_frame=9;m.shot_count=5;m.course_counter=2;
            const bool accepted=run.monsters.damage(found->owner,static_cast<std::int16_t>(amount));
            std::cout<<int(archive.identify())<<','<<health<<','<<state<<','<<amount<<','<<course<<','
                <<m.health<<','<<m.state<<','<<m.next_frame<<','<<m.shot_count<<','<<m.course_counter<<','<<accepted<<'\n';
        }
}
static void death_matrix(const dok::Archive& archive){
    auto definition=dok::load_level(archive,archive.identify()==dok::DatasetId::old_gold?"LEVEL4":"LEVEL1");
    for(int height:{-32768,-301,-1,0,1,300,301,32767})for(int state:{1,4})for(int phase:{0,1}){
        Run run(definition,archive);auto found=std::find_if(run.world.objects.begin(),run.world.objects.end(),[](const auto& o){return o.monster&&o.pose;});
        check(found!=run.world.objects.end(),"No original monster");const auto owner=found->owner;
        run.monsters.wake(owner);auto& m=*found->monster;m.health=0;m.state=static_cast<std::int16_t>(state);m.next_frame=static_cast<std::int16_t>(phase);
        found->pose->y=dok::original::negate16(static_cast<std::int16_t>(height));
        const auto count=archive.animation(definition.objects[found->definition_index].resource(0xec)).size();
        check(count>1,"Death oracle needs2 frames");run.until(run.next(owner));
        std::cout<<int(archive.identify())<<','<<height<<','<<state<<','<<phase<<','<<count<<','<<m.next_frame<<','<<m.state<<','
            <<dok::original::negate16(static_cast<std::int16_t>(found->pose->y))<<'\n';
    }
}
int main(int argc,char** argv){
    try{
        if(argc<2)throw std::invalid_argument("Usage: dok_monster_probe RES...");
        if(std::string(argv[1])=="--native-matrix"){
            for(int arg=2;arg<argc;++arg)native_matrix(dok::Archive::open(std::filesystem::u8path(argv[arg])));
            return 0;
        }
        if(std::string(argv[1])=="--death-matrix"){
            for(int arg=2;arg<argc;++arg)death_matrix(dok::Archive::open(std::filesystem::u8path(argv[arg])));
            return 0;
        }
        check(dok::original_direction(1,0)==0&&dok::original_direction(0,1)==90&&dok::original_direction(-1,0)==180&&dok::original_direction(0,-1)==270,"Direction axes");
        check(dok::original_direction(100,100)==45&&dok::original_direction(0,0)==0,"Direction diagonals/origin");
        unsigned groups=0,replays=0;
        for(int arg=1;arg<argc;++arg){const auto archive=dok::Archive::open(std::filesystem::u8path(argv[arg]));
            const auto levels=archive.identify()==dok::DatasetId::old_gold?std::vector<std::string>{"LEVEL4"}:
                std::vector<std::string>{"LEVEL1","LEVEL2","LEVEL3"};
            for(const auto& level:levels){
                const auto definition=dok::load_level(archive,level);dok::World initial(definition);std::set<std::string> signatures;bool replayed=false;
                for(std::size_t index=0;index<definition.objects.size();++index){const auto& d=definition.objects[index];
                    if(d.class_id!=9||!initial.objects[index].pose)continue;
                    auto key=d.animation+d.resource(0xe8)+d.resource(0xec);
                    for(int f:{0x38,0xa4,0xa6,0xa8,0xae,0xba,0xe7})key+=':'+std::to_string(d.number(f));
                    if(!signatures.insert(key).second)continue;
                    std::cerr<<argv[arg]<<' '<<level<<" monster "<<index<<" melee\n";
                    Run melee(definition,archive);arena(melee,index,1);auto& enemy=melee.world.objects[index];const auto owner=enemy.owner;
                    auto& hero=melee.world.objects.at(*melee.world.player);const auto original=*hero.pose;
                    hero.pose->x=enemy.pose->x+d.number(0xba);hero.pose->z=enemy.pose->z;
                    const dok::CellBounds bounds{0,0,definition.width-1,definition.length-1};
                    melee.monsters.consider(bounds);check(enemy.monster->state==0,"Activation threshold must be strict");
                    --hero.pose->x;melee.monsters.consider(bounds);check(enemy.monster->state==1,"Inside threshold did not wake");*hero.pose=original;
                    const auto attack=archive.animation(d.resource(0xe8));std::uint64_t duration=416;
                    for(const auto& frame:attack)duration+=frame.interval;
                    melee.until(duration-1);check(hero.player_state->health()==30000,"Early melee damage");
                    melee.until(duration);check(hero.player_state->health()==29995,"Melee must deal5 after full attack cycle");
                    const auto hp=enemy.monster->health;melee.monsters.damage(owner,1);check(enemy.monster->health==hp-1,"Monster damage");
                    melee.until(melee.next(owner));check(enemy.monster->state==2&&
                        enemy.monster->display_clip==(definition.dataset==dok::DatasetId::old_gold?1:2),"Profile-specific contact hurt frame");
                    death(melee,index,false);
                    Run blocked(definition,archive);arena(blocked,index,1);blocked.monsters.wake(blocked.world.objects[index].owner);death(blocked,index,true);
                    Run moving(definition,archive);arena(moving,index,6);auto& actor=moving.world.objects[index];moving.monsters.wake(actor.owner);
                    const auto start=*actor.pose;const auto delay=static_cast<std::uint32_t>(d.number(0xa8));
                    actor.monster->burst_clock.stop(0);actor.monster->burst_clock.start(0-delay*10000);
                    moving.until(416);check(!moving.shots.active(),"Burst fired at non-strict threshold");
                    actor.monster->burst_clock.stop(416);actor.monster->burst_clock.start(416-(delay+1)*10000);
                    moving.until(moving.next(actor.owner));
                    check((moving.shots.active()!=0)==(d.number(0xa6)>0),"Burst limit/sight did not control real spawn");
                    moving.advance(2000000000ULL);check(actor.pose->x!=start.x||actor.pose->z!=start.z,"Active monster did not move");
                    if(!replayed){std::vector<std::uint64_t> reference;
                        for(bool gpu:{false,true})for(unsigned fps:{30,60,144}){Run run(definition,archive);arena(run,index,2);auto& actor2=run.world.objects[index];run.activate=true;
                            dok::SoftwareScene scene(archive,definition);std::vector<std::uint64_t> trace;
                            for(unsigned f=1;f<=fps*2;++f){const auto ns=std::uint64_t(f)*1000000000ULL/fps;run.advance(ns-run.elapsed);
                                auto dynamic=run.monsters.images(bounds);const auto shots=run.shots.images();dynamic.insert(dynamic.end(),shots.begin(),shots.end());
                                const dok::SoftwareScene::Draw draw=gpu?dok::SoftwareScene::Draw([](const auto&,const auto&,dok::Bytes,dok::RasterAxis){}):dok::SoftwareScene::Draw{};
                                scene.render(run.world,*run.world.objects.at(*run.world.player).pose,dynamic,draw,gpu?342:320);
                            }
                            trace={static_cast<std::uint64_t>(actor2.pose->x),static_cast<std::uint64_t>(actor2.pose->z),static_cast<std::uint64_t>(actor2.pose->heading),static_cast<std::uint64_t>(actor2.monster->display_frame)};
                            check(actor2.monster->state!=0,"Replay did not activate AI");
                            if(reference.empty())reference=trace;else check(reference==trace,"AI depends on renderer or display FPS");++replays;
                        }replayed=true;
                    }
                    ++groups;
                }
            }
        }
        std::cout<<groups<<" monster parameter groups: wake/melee/hurt/death/corpse/ranged/movement; "<<replays<<" rendered FPS replays passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
