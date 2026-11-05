#include "dok/presentation.hpp"
#include "dok/gpu_geometry.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    for(bool og:{false,true}){
        const auto native=dok::presentation_layout(800,600,true,false,og);
        check(native.reserved_panel&&native.view_width==320&&native.view_height==(og?170:160),"DOS viewport");
        const auto modern=dok::presentation_layout(800,600,false,false,og);
        check(!modern.reserved_panel&&modern.view_width==320&&modern.view_height==200,"4:3 Vert+");
        const double original_v=2*std::atan(native.view_height/400.0);
        check(2*std::atan(modern.view_height/400.0)>original_v,"Vertical FOV must grow");
        for(auto size:{std::pair<int,int>{960,600},{960,540},{1000,600},{1600,600},{1001,601}}){
            const auto wide=dok::presentation_layout(size.first,size.second,true,false,og);
            check(wide.scene.w==size.first&&wide.scene.h==size.second-size.second*(200-native.view_height)/200&&wide.reserved_panel,"Classic must reserve full-width bottom band");
            check(wide.view_width>320&&wide.view_height==native.view_height,"Classic Hor+ without Vert+");
            check(wide.scene.y+wide.scene.h==wide.panel.y,"World must end above classic panel");
            const int panel_width=int(std::lround(size.second*4.0/3.0));
            check(wide.panel.w==panel_width&&wide.panel.x==(size.first-panel_width)/2,"Panel proportions and centering");
            check(std::abs(double(wide.weapon.w)/size.second-double(native.weapon.w)/600)<0.002,"Weapon scale must not depend on width");
            check(dok::presentation_layout(size.first,size.second,false,true,og).classic==false,"Auto wide HUD");
            // Project an actual world sprite: changing viewport dimensions only
            // shifts its center, and never changes its projected size.
            const auto a=dok::GpuCamera({},320,0,{},native.view_height).sprite({0,0,1000,0},100,100,8,8,1);
            const auto b=dok::GpuCamera({},wide.view_width,0,{},wide.view_height).sprite({0,0,1000,0},100,100,8,8,1);
            check(a&&b,"Projection fixture");
            const auto extent=[](const auto& p,int axis){return (*p.vertices[1].precise)[axis]-(*p.vertices[0].precise)[axis];};
            check(std::abs(extent(*a,0)-extent(*b,0))<0.0001,"Projection scale changed");
        }
    }
    const auto narrow=dok::presentation_layout(600,600,false,true,false);
    for(bool classic:{false,true})for(bool automatic:{false,true}){
        const auto software=dok::presentation_layout(1920,1080,classic,automatic,false,true);
        check(software.frame.x==240&&software.frame.w==1440&&software.frame.h==1080&&software.view_width==320,"Software must always use a centered 4:3 frame");
        check(software.classic==(classic||automatic),"Software Auto must select classic in its effective 4:3 frame");
    }
    check(narrow.frame.h==450&&narrow.frame.y==75&&narrow.classic,"Narrow frame fit");
    const auto fit=dok::fit_frame(1920,1080,800,600);
    check(fit.x==240&&fit.y==0&&fit.w==1440&&fit.h==1080,"Fullscreen aspect independent of desktop");
    check(dok::aspect_label(1280,768)=="5:3"&&dok::aspect_label(1280,800)=="16:10","Exact aspect ratio");
    std::cout<<"DOS baseline, Hor+, Vert+, panel/weapon scale and fullscreen fit passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
