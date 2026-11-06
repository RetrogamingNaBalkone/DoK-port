#include "dok/filters.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace dok {
namespace {
using Color=std::array<float,4>;
Color unpack(std::uint32_t p){const float a=(p>>24)/255.f;return {float((p>>16)&255)*a,float((p>>8)&255)*a,float(p&255)*a,a*255};}
std::uint32_t pack(const Color& c,bool straight=false){const float scale=straight&&c[3]>0?255/c[3]:1;auto byte=[](float f){return std::uint32_t(std::clamp(std::lround(f),0l,255l));};return byte(c[3])<<24|byte(c[0]*scale)<<16|byte(c[1]*scale)<<8|byte(c[2]*scale);}
Color mix(Color a,const Color& b,float t){for(int i=0;i<4;++i)a[i]+=(b[i]-a[i])*t;return a;}
}
MipAtlas make_mip_atlas(const Image& image,const std::array<std::uint32_t,256>& palette,Bytes light,bool transparent){
    if(!image.width||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height||light.size!=4096)throw std::invalid_argument("Invalid mipmap source");
    MipAtlas atlas;int w=image.width,h=image.height;
    for(;;){atlas.tile_width+=w;if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);}
    atlas.width=4*atlas.tile_width;atlas.height=4*image.height;atlas.pixels.resize(std::size_t(atlas.width)*atlas.height);
    for(int row=0;row<16;++row){
        std::vector<Color> level(image.pixels.size());
        for(std::size_t i=0;i<level.size();++i){const auto index=image.pixels[i];level[i]=transparent&&index==0?Color{}:unpack(palette[light.data[row*256+index]]);}
        w=image.width;h=image.height;int offset=0;
        for(;;){
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)atlas.pixels[std::size_t((row/4)*image.height+y)*atlas.width+(row%4)*atlas.tile_width+offset+x]=pack(level[std::size_t(y)*w+x]);
            offset+=w;if(w==1&&h==1)break;
            const int nw=std::max(1,w/2),nh=std::max(1,h/2);std::vector<Color> next(std::size_t(nw)*nh);
            // Area resampling includes the last row/column of odd-sized images.
            for(int y=0;y<nh;++y)for(int x=0;x<nw;++x){
                const double x0=double(x)*w/nw,x1=double(x+1)*w/nw,y0=double(y)*h/nh,y1=double(y+1)*h/nh;
                Color sum{};double weight=0;
                for(int sy=int(y0);sy<int(std::ceil(y1));++sy)for(int sx=int(x0);sx<int(std::ceil(x1));++sx){const double a=(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)));weight+=a;for(int c=0;c<4;++c)sum[c]+=float(a)*level[std::size_t(sy)*w+sx][c];}
                for(auto& c:sum)c/=float(weight);next[std::size_t(y)*nw+x]=sum;
            }
            level=std::move(next);w=nw;h=nh;
        }
    }return atlas;
}
// CPU adaptation of Hyllian's xBR-lv2, CORNER_C / smooth tips, scale 3.
// Copyright (C) 2011-2016 Hyllian. MIT license: third_party/xbr-LICENSE.txt.
std::vector<std::uint32_t> xbr3(const std::vector<std::uint32_t>& pixels,int w,int h){
    if(w<=0||h<=0||pixels.size()!=std::size_t(w)*h)throw std::invalid_argument("Invalid xBR source");
    std::vector<std::uint32_t> out(std::size_t(w)*h*9);
    auto df=[](float a,float b){return std::abs(a-b);};auto eq=[&](float a,float b){return df(a,b)<=15;};
    auto luma=[](const Color& c){return (14.352f*c[0]+28.176f*c[1]+5.472f*c[2])/255;};
    auto ramp=[](float value,float center,float delta){return std::clamp((value+delta-center)/(2*delta),0.f,1.f);};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const auto E=unpack(pixels[std::size_t(y)*w+x]);const float e=luma(E);
        struct Edge{Color target;bool inner=false,edge=false,left=false,up=false;};std::array<Edge,4> edges;
        for(int r=0;r<4;++r){
            auto at=[&](int dx,int dy){for(int k=0;k<r;++k){const int old=dx;dx=dy;dy=-old;}return unpack(pixels[std::size_t(std::clamp(y+dy,0,h-1))*w+std::clamp(x+dx,0,w-1)]);};
            const auto F=at(1,0),H=at(0,1);const float f=luma(F),hh=luma(H),b=luma(at(0,-1)),c=luma(at(1,-1)),d=luma(at(-1,0)),g=luma(at(-1,1)),i=luma(at(1,1)),f4=luma(at(2,0)),i4=luma(at(2,1)),h5=luma(at(0,2)),i5=luma(at(1,2));
            const bool initial=e!=f&&e!=hh;
            const bool corner=(!eq(f,b)&&!eq(f,c))||(!eq(hh,d)&&!eq(hh,g))||(eq(e,i)&&((!eq(f,f4)&&!eq(f,i4))||(!eq(hh,h5)&&!eq(hh,i5))))||eq(e,g)||eq(e,c);
            const float wd1=df(e,c)+df(e,g)+df(i,h5)+df(i,f4)+4*df(hh,f);
            const float wd2=df(hh,d)+df(hh,i5)+df(f,i4)+df(f,b)+4*df(e,i);
            auto& edge=edges[r];edge.target=df(e,f)<=df(e,hh)?F:H;edge.inner=initial&&wd1<=wd2;edge.edge=initial&&corner&&wd1+.1f<=wd2;
            edge.left=edge.edge&&2*df(f,g)<=df(hh,c)&&e!=g&&d!=g;
            edge.up=edge.edge&&2*df(hh,c)<=df(f,g)&&e!=c&&b!=c;
        }
        for(int sy=0;sy<3;++sy)for(int sx=0;sx<3;++sx){
            std::array<float,4> blend{};
            for(int r=0;r<4;++r){float fx=(sx+.5f)/3,fy=(sy+.5f)/3;for(int k=0;k<r;++k){const float old=fx;fx=1-fy;fy=old;}const auto& a=edges[r];
                blend[r]=std::max({a.inner?ramp(fx+fy,1.75f,1.f/3):0,a.edge?ramp(fx+fy,1.5f,1.f/3):0,a.left?ramp(fy+.5f*fx,1,1.f/6):0,a.up?ramp(fy+2*fx,2,1.f/3):0});
            }
            const auto a=mix(mix(E,edges[0].target,blend[0]),edges[2].target,blend[2]);
            const auto b=mix(mix(E,edges[1].target,blend[1]),edges[3].target,blend[3]);
            auto distance=[&](const Color& c){return df(E[0],c[0])+df(E[1],c[1])+df(E[2],c[2])+df(E[3],c[3]);};
            out[std::size_t(y*3+sy)*w*3+x*3+sx]=pack(distance(a)<=distance(b)?b:a,true);
        }
    }
    return out;
}
}
