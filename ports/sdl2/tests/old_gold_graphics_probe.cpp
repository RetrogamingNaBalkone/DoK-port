#include "dok/monsters.hpp"
#include "dok/movement.hpp"
#include "dok/hud.hpp"
#include "dok/png.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("RES output.png");
    const auto a=dok::Archive::open(std::filesystem::u8path(argv[1]));
    dok::World w(dok::load_level(a,"LEVEL4"));const auto& d=w.definition;
    dok::Hud hud(a);auto p=*w.objects.at(*w.player).player_state;
    const auto panel=a.image("\x8f\x80\x8d\x85\x8b\x9c");
    const auto life=a.image("\x8f\x86\x88\x87\x8d\x9c"),magic=a.image("\x8f\x8c\x80\x83\x88\x9f"),money=a.image("\x8f\x84\x85\x8d\x9c\x83\x88");
    for(int score:{0,1,21,42,63,84,105}){
        p.score=score;p.weapons.fill(0);p.attributes[10]=0;
        auto expected=dok::IndexedSurface(320,200);
        for(unsigned y=0;y<panel.height;++y)for(unsigned x=0;x<panel.width;++x)expected.pixels[(171+y)*320+x]=panel.pixels[y*panel.width+x];
        const auto bar=[&](const dok::Image& icon,int x,int current,int maximum){
            const int count=current?std::max(1,current*5/maximum):0;
            for(int n=0;n<count;++n)for(unsigned y=0;y<icon.height;++y)for(unsigned sx=0;sx<icon.width;++sx){
                const auto c=icon.pixels[y*icon.width+sx];if(c)expected.pixels[(183+y)*320+x+n*(icon.width+2)+sx]=c;
            }
        };
        bar(life,4,p.health(),p.max_health());bar(magic,70,p.charge(),p.max_charge());bar(money,136,score,105);
        const auto actual=hud.classic(dok::IndexedSurface(320,170),p);
        check(actual.pixels==expected.pixels,"OG native icon count/coordinates differ");
        if(score==105){const auto palette=a.palette(d.palette);std::vector<std::uint32_t> rgba;
            for(auto c:actual.pixels)rgba.push_back(palette[c]);dok::save_png(std::filesystem::u8path(argv[2]),320,200,rgba);}
    }
    // A large billboard centered in an open tile must stop at its wall neighbour,
    // retaining the source UV interval rather than squeezing the whole sprite.
    dok::World arena(d);std::fill(arena.maps.at("tmap").begin(),arena.maps.at("tmap").end(),' ');
    auto wall=std::find_if(d.objects.begin(),d.objects.end(),[](const auto& o){return o.class_id==2;});
    check(wall!=d.objects.end(),"No wall");arena.maps.at("tmap")[10*d.width+11]=wall->glyph;
    const dok::Pose center{10*d.cell_side+d.cell_side/2,0,10*d.cell_side+d.cell_side/2,0};
    const auto span=dok::billboard_wall_span(arena,center,2*d.cell_side,0);
    check(span[0]==0&&span[1]==0.75,"Billboard enters adjacent wall");
    check(dok::billboard_wall_span(arena,center,2*d.cell_side,90)==std::array<double,2>{0,1},"Unrelated wall clips sprite");
    dok::Projectiles shots(w,a);dok::Monsters monsters(w,a,shots);std::vector<dok::Pose> starts(w.objects.size());
    unsigned actors=0,moved=0;std::uint64_t checks=0;
    for(auto& o:w.objects)if(o.monster&&o.pose){starts[o.owner-1]=*o.pose;monsters.wake(o.owner);++actors;}
    for(int frame=0;frame<1800;++frame){
        w.advance_events(16666667,[&](auto owner,auto id){
            if(id==static_cast<dok::CallbackId>(dok::GameEvent::monster_tick))monsters.event(owner);
            else if(id==static_cast<dok::CallbackId>(dok::GameEvent::projectile_tick))shots.event(owner);
        });
        for(const auto& o:w.objects)if(o.monster&&o.pose){
            const int x=o.pose->x/d.cell_side,z=o.pose->z/d.cell_side;
            check(x>=0&&x<d.width&&z>=0&&z<d.length,"Monster left map");
            check(d.object(w.maps.at("tmap")[z*d.width+x])->class_id!=2,"Monster center entered wall");++checks;
        }
    }
    for(const auto& o:w.objects)if(o.monster&&o.pose&&(o.pose->x!=starts[o.owner-1].x||o.pose->z!=starts[o.owner-1].z))++moved;
    check(moved>0,"All monsters stuck");
    std::cout<<"PASS 7 native HUD states; billboard wall clipping; "<<actors<<" original monsters, "<<moved<<" moved; "<<checks<<" wall occupancy checks over30s\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
