#include "dok/geometry.hpp"
#include "dok/scene.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void check(bool value){if(!value)throw std::runtime_error("Raster vector failed");}
int main(){
    try{
        std::array<std::uint8_t,4096> lut{};
        for(unsigned i=0;i<lut.size();++i)lut[i]=static_cast<std::uint8_t>(i%256);
        dok::Bytes light{lut.data(),lut.size()};
        dok::IndexedSurface surface(8,8);
        dok::Image image{3,3,0,{1,2,3,4,5,6,7,8,9}};
        const std::vector<dok::RasterVertex> quad={{1,1,100,0,0},{5,1,100,2,0},{5,5,100,2,2},{1,5,100,0,2}};
        for(auto axis:{dok::RasterAxis::vertical,dok::RasterAxis::horizontal}){
            surface.clear(99);dok::rasterize(surface,quad,image,light,{0,0,7,7},axis);
            for(int y=0;y<8;++y)for(int x=0;x<8;++x){
                auto expected=x>=1&&x<=5&&y>=1&&y<=5?image.pixels[(y-1)/2*3+(x-1)/2]:99;
                check(surface.pixels[y*8+x]==expected);
            }
        }
        // Packed ESI low-word carry changes V at the seventh sample; separate
        // Q16 accumulators return texel 0 instead of original texel 8.
        dok::Image adc_image{8,2,0,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
        std::vector<dok::RasterVertex> adc_quad={{0,0,100,7,0},{7,0,100,0,1},{7,2,100,0,1},{0,2,100,7,0}};
        surface.clear();dok::rasterize(surface,adc_quad,adc_image,light,{0,0,7,7},dok::RasterAxis::horizontal);
        check(surface.pixels[7]==8);
        dok::IndexedSurface bank_surface(257,257);dok::Image bank_image{9,2,0,{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18}};
        std::vector<dok::RasterVertex> bank_quad={{0,254,100,0,0},{8,254,100,8,0},{8,256,100,8,1},{0,256,100,0,1}};
        dok::rasterize(bank_surface,bank_quad,bank_image,light,{0,0,256,256},dok::RasterAxis::horizontal);
        check(bank_surface.pixels[65535]==1&&bank_surface.pixels[65536]==2);
        dok::RasterReferenceState bank_reference;
        dok::rasterize(bank_surface,bank_quad,bank_image,light,{0,0,256,256},dok::RasterAxis::horizontal,1920,&bank_reference);
        check(bank_surface.pixels[65535]==1&&bank_surface.pixels[65536]==1);
        auto reversed=quad;std::reverse(reversed.begin(),reversed.end());image.flags=0x20;
        surface.clear(99);dok::rasterize(surface,reversed,image,light,{2,2,4,4},dok::RasterAxis::vertical);
        check(surface.pixels[2*8+2]==1&&surface.pixels[4*8+4]==5&&surface.pixels[1*8+1]==99);
        image.flags=8;lut[1]=0;surface.clear(99);
        dok::rasterize(surface,quad,image,light,{0,0,7,7},dok::RasterAxis::vertical);
        check(surface.pixels[1*8+1]==99&&surface.pixels[1*8+3]==2);
        dok::rasterize(surface,quad,image,light,{0,0,7,7},dok::RasterAxis::horizontal);
        check(surface.pixels[1*8+1]==0); // Horizontal ASM is always opaque.
        // OG tests raw transparency before LIGHT, and excludes bottom/right.
        surface.clear(99);dok::rasterize_old_gold(surface,quad,image,light,{0,0,7,7},0,dok::RasterAxis::vertical);
        check(surface.pixels[1*8+1]==0&&surface.pixels[1*8+2]==2);
        check(surface.pixels[5*8+1]==99&&surface.pixels[1*8+5]==99);
        // Original Kremlin DS history changes lighting when vertex0 is maximum.
        auto slopes=quad;slopes[1].depth=slopes[2].depth=5000;
        std::rotate(slopes.begin(),slopes.begin()+2,slopes.end());
        for(unsigned i=0;i<lut.size();++i)lut[i]=static_cast<std::uint8_t>(i/256+1);
        image.flags=0;surface.clear();dok::rasterize(surface,slopes,image,light,{0,0,7,7},dok::RasterAxis::vertical);
        const auto fixed=surface.pixels;dok::RasterReferenceState history{1};surface.clear();
        dok::rasterize(surface,slopes,image,light,{0,0,7,7},dok::RasterAxis::vertical,1920,&history);
        check(surface.pixels!=fixed&&history.maximum_vertex==1);
        history.maximum_vertex=0;surface.clear();dok::rasterize(surface,slopes,image,light,{0,0,7,7},dok::RasterAxis::vertical,1920,&history);check(surface.pixels==fixed);
        history.maximum_vertex=15;dok::rasterize(surface,quad,image,light,{0,0,7,7},dok::RasterAxis::vertical,1920,&history);check(history.maximum_vertex==1);
        check(dok::light_row(1920*65536,1920)==0);
        check(dok::light_row(2040*65536,1920)==1);
        check(dok::light_row(3720*65536,1920)==15);
        check(dok::light_row(0,1920)==0);
        dok::SoftwareCamera camera(dok::CameraParameters::kremlin_vga());
        camera.set_pose({15120,-300,22800,90});
        auto door=dok::vertical_plane(camera,{11760,0,22560,0},{11760,0,23040,0},640,96,128,1,120);
        check(door.has_value()&&door->sort_key==3240);
        check(door->vertices[0].x==145&&door->vertices[0].y==59);
        check(door->vertices[2].x==172&&door->vertices[2].y==95);
        auto reflected=dok::vertical_plane(camera,{11760,0,22080,0},{11760,0,22560,0},640,96,128,1,120,0,0,true);
        check(reflected.has_value()&&reflected->reverse_edges);
        check(reflected->vertices[0].x==145&&reflected->vertices[1].x==118);
        camera.set_pose({0,0,0,0});
        auto clipped=dok::vertical_plane(camera,{-20,100,-100,0},{100,100,100,0},100,11,11);
        check(clipped.has_value()&&clipped->vertices[0].depth==0&&clipped->vertices[0].u==5);
        check(!dok::billboard(camera,{0,0,0,0},100,100,10,10));
        auto near=*door,far=*door;near.sort_key=1;near.flags=3;far.sort_key=100;far.flags=1;
        std::vector<dok::RenderPolygon> list{near,far};dok::sort_and_cull(list,camera.clip());
        check(!(list[0].flags&1)&&(list[1].flags&1));
        const auto bounds=dok::original_cell_bounds({15120,-300,22800,90},480,64,64);
        check(bounds.min_x==24&&bounds.max_x==33&&bounds.min_z==41&&bounds.max_z==53);
        const auto asymmetric=dok::original_cell_bounds({20880,-300,3120,135},480,64,64);
        check(asymmetric.min_x==34&&asymmetric.max_x==44&&asymmetric.min_z==0&&asymmetric.max_z==8);
        // An unadmitted offscreen primitive can reorder equal keys in Shellsort.
        auto a=near,b=near,off=near;a.flags=b.flags=off.flags=1;a.sort_key=b.sort_key=10;off.sort_key=20;
        a.material=1;b.material=2;off.material=3;for(auto& v:off.vertices)v.x=-50;
        auto tail=off;tail.sort_key=0;tail.material=4;
        std::vector<dok::RenderPolygon> admitted{a,b},extra{a,b,off,tail};
        dok::sort_and_cull(admitted,camera.clip());dok::sort_and_cull(extra,camera.clip());
        check(admitted[0].material==1&&admitted[1].material==2);
        check(extra[0].material==3&&!(extra[0].flags&1)&&extra[1].material==2&&extra[2].material==1);
        camera.set_pose({0,-300,0,0});
        auto ground=dok::horizontal_plane(camera,{-240,0,-240,0},480,96,96);
        check(ground.has_value()&&ground->vertices[0].depth==240&&ground->vertices[2].depth==-8);
        check(ground->sort_key==116&&ground->axis==dok::RasterAxis::horizontal);
        auto roof=dok::horizontal_plane(camera,{-240,-951,-240,0},480,96,96,true);
        check(roof.has_value()&&roof->vertices[0].depth==-8&&roof->vertices[2].depth==240);
        std::cout<<"Affine H/V spans, clipping, LIGHT transparency and coarse visibility passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
