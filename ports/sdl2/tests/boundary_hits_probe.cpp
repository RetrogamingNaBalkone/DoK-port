#include "dok/projectiles.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
enum Barrier {none,solid,flag8,flag16,door_closed,door_flag8,door_open,door_open_flag8,side_wall,corner_wall,corner_pass};
static unsigned scenario(const dok::Archive& archive,const char* level,bool enabled,Barrier barrier,
                         int direction=1,bool dead=false,bool friendly=false,bool wake=false,
                         bool transpose=false,int separation=2){
    dok::World w(dok::load_level(archive,level));auto& d=w.definition;const int side=d.cell_side;
    for(auto name:{"tmap","mmap"})std::fill(w.maps.at(name).begin(),w.maps.at(name).end(),' ');
    auto& hero=w.objects.at(*w.player);
    auto monster=std::find_if(w.objects.begin(),w.objects.end(),[](const auto& o){return bool(o.monster);});
    check(monster!=w.objects.end(),"Original monster missing");
    const bool corner=barrier==corner_wall||barrier==corner_pass;
    const int mx=11*side-direction,mz=corner?12*side+1:12*side+side/2;
    monster->pose=dok::Pose{mx,-300,mz,0};monster->monster->health=dead?0:100;monster->monster->state=wake?0:1;
    w.maps.at("mmap")[(mz/side)*d.width+mx/side]=d.objects[monster->definition_index].glyph;
    hero.pose=dok::Pose{mx+direction*separation,-300,10*side+side/2,0};
    dok::WorldObject* obstacle=nullptr;
    const bool door=barrier>=door_closed&&barrier<=door_open_flag8;
    if(barrier!=none){
        auto it=std::find_if(w.objects.begin(),w.objects.end(),[&](const auto& o){return d.objects[o.definition_index].class_id==(door?7:2);});
        check(it!=w.objects.end(),"Original obstacle missing");obstacle=&*it;
        obstacle->flags=barrier==flag8||barrier==door_flag8||barrier==door_open_flag8||barrier==corner_pass?8:barrier==flag16?16:0;
        if(door)obstacle->door->state=barrier==door_open||barrier==door_open_flag8?1:2;
        const auto glyph=d.objects[obstacle->definition_index].glyph;
        if(barrier==side_wall)w.maps.at("tmap")[(mz/side)*d.width+mx/side]=glyph;
        else if(corner){
            // Diagonally adjacent target: both side cells around the corner.
            w.maps.at("tmap")[11*d.width+10]=glyph;
            w.maps.at("tmap")[12*d.width+11]=glyph;
        }else w.maps.at("tmap")[11*d.width+hero.pose->x/side]=glyph;
    }
    if(transpose){
        for(auto name:{"tmap","mmap"}){
            const auto previous=w.maps.at(name);
            for(int z=0;z<d.length;++z)for(int x=0;x<d.width;++x)w.maps.at(name)[z*d.width+x]=previous[x*d.width+z];
        }
        std::swap(hero.pose->x,hero.pose->z);hero.pose->heading=270;
        std::swap(monster->pose->x,monster->pose->z);
    }
    dok::Projectiles shots(w,archive);check(!shots.boundary_hits,"Default must remain native");shots.boundary_hits=enabled;
    unsigned hits=0;
    if(!wake)shots.monster_hit=[&](dok::OwnerId owner,std::int16_t){check(owner==monster->owner,"Wrong target");++hits;return true;};
    const auto source=friendly?monster->owner:hero.owner;
    const auto saved=monster->pose;if(friendly)monster->pose=hero.pose;
    check(shots.spawn(source,'1')>0,"Spawn failed");if(friendly)monster->pose=saved;
    for(auto& p:w.projectiles)if(p)p->speed=-120;
    w.advance_events(1500000000ULL,[&](dok::OwnerId owner,dok::CallbackId id){
        if(id==static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))shots.event(owner);
    });
    if(wake){check(monster->monster->state==1&&monster->monster->health==100,"Boundary impact changed first wake semantics");return 1;}
    return hits;
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("Usage: dok_boundary_hits_probe ROOT");
        unsigned cases=0;const auto root=std::filesystem::u8path(argv[1]);
        for(const auto* path:{"originals/pkr/RUS/PKR.RES","originals/pkr/ENG/PKE.RES","originals/Old_Gold_3D/OLD_GOLD.RES"}){
            const auto a=dok::Archive::open(root/path);const bool og=a.identify()==dok::DatasetId::old_gold;
            const auto level=og?"LEVEL4":"LEVEL3";
            for(bool transpose:{false,true}){
            for(int direction:{-1,1})for(bool enabled:{false,true}){
                check(scenario(a,level,enabled,none,direction,false,false,false,transpose)==unsigned(enabled),"Boundary toggle mismatch");++cases;
            }
            for(auto barrier:{solid,flag8,flag16,door_closed,door_flag8,door_open,door_open_flag8,side_wall,corner_wall,corner_pass}){
                const bool passes=barrier==flag8||barrier==flag16||barrier==corner_pass||
                    (og?(barrier==door_open||barrier==door_open_flag8):(barrier==door_flag8||barrier==door_open_flag8));
                check(scenario(a,level,true,barrier,1,false,false,false,transpose)==unsigned(passes),"Wall/door/corner penetration changed");++cases;
                check(scenario(a,level,false,barrier,1,false,false,false,transpose)==0,"Native adjacent cell must still miss");++cases;
            }
            check(scenario(a,level,true,none,1,true,false,false,transpose)==0,"Dead monster received expanded hit");++cases;
            check(scenario(a,level,true,none,1,false,true,false,transpose)==0,"Expanded hit bypassed friendly-class filter");++cases;
            scenario(a,level,true,none,1,false,false,true,transpose);++cases;
            const int radius=dok::load_level(a,level).min_distance;
            for(int separation:{radius-1,radius,radius+1}){
                check(scenario(a,level,true,none,1,false,false,false,transpose,separation)==unsigned(separation<=radius),"Footprint radius boundary mismatch");++cases;
            }
            }
        }
        std::cout<<cases<<" optional boundary/wall/door/corner/wake scenarios PASS\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
