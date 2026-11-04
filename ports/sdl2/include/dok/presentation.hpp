#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
namespace dok {
struct PresentationRect { int x=0,y=0,w=0,h=0; };
inline PresentationRect fit_frame(int width,int height,int frame_width,int frame_height){
    int w=width,h=int(std::int64_t(width)*frame_height/frame_width);
    if(h>height){h=height;w=int(std::int64_t(height)*frame_width/frame_height);}
    return {(width-w)/2,(height-h)/2,std::max(1,w),std::max(1,h)};
}
inline std::string aspect_label(int width,int height){
    const int divisor=std::gcd(width,height);
    return std::to_string(width/divisor)+":"+std::to_string(height/divisor);
}
struct PresentationLayout {
    PresentationRect frame,scene,panel,weapon;
    int view_width=320,view_height=160;
    bool classic=false,reserved_panel=false;
};
inline PresentationLayout presentation_layout(int width,int height,bool classic,bool automatic,bool old_gold){
    PresentationLayout out;
    out.frame={0,0,width,height};
    if(std::int64_t(width)*3<std::int64_t(height)*4)out.frame=fit_frame(width,height,4,3);
    const auto frame=out.frame;
    const bool wide=std::int64_t(frame.w)*3>std::int64_t(frame.h)*4+3;
    out.classic=classic||(automatic&&!wide);
    out.reserved_panel=out.classic&&!wide;
    out.view_height=out.reserved_panel?(old_gold?170:160):200;
    out.view_width=std::max(1,int(std::lround(240.0*frame.w/frame.h)));
    out.scene=frame;
    if(out.reserved_panel)out.scene.h=frame.h*out.view_height/200;
    const int panel_width=std::min(frame.w,int(std::lround(frame.h*4.0/3.0)));
    const int panel_height=frame.h*(old_gold?30:40)/200;
    out.panel={frame.x+(frame.w-panel_width)/2,frame.y+frame.h-panel_height,panel_width,panel_height};
    // A central DOS-sized weapon layer shares the world's pixel scale.
    const int weapon_height=frame.h*out.view_height/200;
    out.weapon={frame.x+(frame.w-panel_width)/2,out.scene.y+out.scene.h-weapon_height,panel_width,weapon_height};
    return out;
}
} // namespace dok
