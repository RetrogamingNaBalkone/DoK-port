#include "dok/camera.hpp"
#include "dok/gpu_geometry.hpp"
#include "dok/view_motion.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool value){if(!value)throw std::runtime_error("Camera reference mismatch");}
int main(){
    try{
        dok::SoftwareCamera camera(dok::CameraParameters::kremlin_vga());
        const auto clip=camera.clip();
        check(clip.left==0&&clip.top==0&&clip.right==319&&clip.bottom==159);
        check(camera.center_x()==159&&camera.center_y()==79);
        // Independent DOS render records, four original door panels.
        for(int language=0;language<2;++language){
            camera.set_pose({15120,-300,22800+480*language,90});
            for(int panel=0;panel<2;++panel){
                auto a=camera.transform(11760,-640,22560-panel*480);
                auto b=camera.transform(11760,0,23040-panel*480);
                check(a.depth==3360&&b.depth==3360);
                auto top=camera.project(a),bottom=camera.project(b);
                check(top.factor==1840&&bottom.factor==1840);
                check(top.x==145-27*(language+panel));
                check(bottom.x==172-27*(language+panel));
                check(top.y==59&&bottom.y==95);
            }
        }
        camera.set_pose({0,0,0,0});
        auto axes=camera.transform(300,-10,1000);
        check(axes.lateral==300&&axes.depth==1000&&axes.vertical==-10);
        // Low32 multiply followed by SAR, not an exact 64-bit rotation.
        auto wrapped=camera.transform(131072,0,0);
        check(wrapped.lateral==0);
        bool failed=false;try{camera.project({0,0,-200});}catch(const std::domain_error&){failed=true;}
        check(failed);
        dok::SoftwareCamera inset({320,161,7,4096,200});
        check(inset.clip().left==20&&inset.clip().right==299&&inset.clip().top==10&&inset.clip().bottom==149);
        check(inset.center_x()==159&&inset.center_y()==79);
        // Sub-degree mouse yaw must move vertices continuously, without VGA snapping.
        float previous=0;
        for(int step=0;step<=10;++step){
            dok::GpuCamera gpu({0,-300,0,0},320,step*0.01f);
            const auto wall=gpu.vertical({-240,0,2000,0},{240,0,2000,0},640,96,128,1);
            check(wall&&wall->vertices[0].precise.has_value());
            const float x=wall->vertices[0].precise->at(0);
            if(step)check(x<previous&&previous-x<0.1f);
            previous=x;
        }
        check(dok::presentation_focal(0)==200.0);
        check(std::abs(dok::presentation_focal(90)-160.0)<1e-10);
        float previous_extent=1000;
        for(int fov:{45,60,75,90,110}){
            dok::GpuCamera zoom({0,-300,0,0},320,0,{},160,dok::presentation_focal(fov));
            const auto wall=zoom.vertical({-240,0,2000,0},{240,0,2000,0},640,96,128,1);
            const auto& top=*wall->vertices[0].precise;const auto& bottom=*wall->vertices[3].precise;
            const float extent=159.5f-top[0];check(extent<previous_extent);previous_extent=extent;
            // Zoom acts equally on X and Y; source texel/weapon ratios cannot stretch.
            check(std::abs((bottom[1]-top[1])/extent-640.0/240.0)<0.0001);
            check(top[2]==2000&&bottom[2]==2000);
        }
        bool bad_fov=false;try{dok::presentation_focal(44);}catch(const std::invalid_argument&){bad_fov=true;}check(bad_fov);
        dok::ViewMotion motion;
        motion.record({0,-300,0,359},{100,-300,200,1},416);
        check(motion.offset(416)[0]==-100&&motion.offset(624)[0]==-50);
        check(motion.offset(624)[2]==-100&&motion.offset(624)[3]==-1);
        check(motion.offset(832)[0]==0&&motion.offset(1000)[0]==0);
        // Constant tick displacement must produce constant per-frame travel at all rates.
        for(int fps:{30,60,144}){
            double previous_position=-100,previous_time=0;
            for(int frame=0;frame<fps;++frame){
                const double time=frame*10000.0/fps;
                const int tick=int(time/416);
                motion.record({tick*100-100,0,0,0},{tick*100,0,0,0},tick*416);
                const double position=tick*100+motion.offset(time)[0];
                if(frame)check(std::abs((position-previous_position)/(time-previous_time)-100.0/416)<1e-9);
                previous_position=position;previous_time=time;
            }
        }
        motion.reset();check(motion.offset(0)==std::array<double,4>{});
        dok::GpuCamera shifted({100,-300,0,0},320,0,{-99.5,0,0,0});
        const auto shifted_wall=shifted.vertical({-240,0,2000,0},{240,0,2000,0},640,96,128,1);
        check(shifted_wall&&std::abs(shifted_wall->vertices[0].precise->at(0)-(159.5-240.5*200/2200))<0.0001);
        dok::GpuCamera gpu({0,-300,0,0},320);
        const auto floor=gpu.horizontal({-240,0,-240,0},480,96,96,false);
        check(floor&&floor->vertices.size()>=3);
        for(const auto& vertex:floor->vertices){const auto& v=*vertex.precise;
            check(std::isfinite(v[0])&&std::isfinite(v[1])&&v[2]>=0&&v[3]>=0&&v[3]<=95&&v[4]>=0&&v[4]<=95);}
        std::cout<<"VGA viewport and four DOS door records matched; full renderer pending\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
