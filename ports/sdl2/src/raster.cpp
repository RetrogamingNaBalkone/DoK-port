#include "dok/raster.hpp"
#include "dok/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
using namespace original;
IndexedSurface::IndexedSurface(int width,int height):width_(width),height_(height){
    if(width<1||height<1||width>32767||height>32767)
        throw std::invalid_argument("Invalid indexed surface dimensions");
    pixels.resize(static_cast<std::size_t>(width)*height);
}
void IndexedSurface::clear(std::uint8_t index){std::fill(pixels.begin(),pixels.end(),index);}
unsigned light_row(std::int32_t depth_q16,std::int16_t near_distance)noexcept{
    const auto depth=signed16(static_cast<std::uint16_t>(sar32(depth_q16,16)));
    auto delta=depth<near_distance?0:signed16(static_cast<std::uint16_t>(std::int32_t(depth)-near_distance));
    return std::min<unsigned>(static_cast<std::uint16_t>(delta/120),15);
}
namespace {
std::int32_t q16(std::int32_t value){return multiply32(value,65536);}
std::int32_t abs32(std::int32_t value){return value<0?sub32(0,value):value;}
struct Edge {
    int index{},direction{},remaining{};
    std::int32_t coordinate{},u{},v{},step_coordinate{},step_u{},step_v{};
};
// Keep axis/transparency outside the pixel loop. Unsigned accumulators retain
// the original wrapping ADD; extracting the high word is the same SAR16.
template<bool Vertical,bool Transparent>
void span_pixels(IndexedSurface& target,const Image& image,const std::uint8_t* colors,
                 int position,int begin,int end,std::int32_t u0,std::int32_t v0,
                 std::int32_t du,std::int32_t dv){
    auto u=std::uint32_t(u0),v=std::uint32_t(v0);
    std::size_t destination=Vertical?std::size_t(begin)*target.width()+position:std::size_t(position)*target.width()+begin;
    const auto stride=Vertical?target.width():1;
    for(int pixel=begin;pixel<=end;++pixel){
        const auto tu=signed16(static_cast<std::uint16_t>(u>>16));
        const auto tv=signed16(static_cast<std::uint16_t>(v>>16));
        const auto offset=std::int64_t(tv)*image.width+tu;
        if(offset<0||offset>=static_cast<std::int64_t>(image.pixels.size()))
            throw std::out_of_range("Native sampler escaped texture payload");
        const auto mapped=colors[image.pixels[static_cast<std::size_t>(offset)]];
        if constexpr(Transparent){if(mapped)target.pixels[destination]=mapped;}
        else target.pixels[destination]=mapped;
        destination+=stride;
        if constexpr(!Vertical)u+=std::uint32_t(du);
        v+=std::uint32_t(dv);
    }
}
}
void rasterize(IndexedSurface& target,const std::vector<RasterVertex>& vertices,
               const Image& image,Bytes light,ClipRect clip,RasterAxis axis,std::int16_t light_near){
    if(vertices.size()<3||vertices.size()>16)throw std::invalid_argument("Native polygon needs 3..16 vertices");
    if(!image.width||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height)
        throw std::invalid_argument("Invalid raster texture");
    if(light.size!=4096||!light.data)throw std::invalid_argument("Raster needs sixteen LIGHT rows");
    clip.left=std::max(clip.left,0);clip.top=std::max(clip.top,0);
    clip.right=std::min(clip.right,target.width()-1);clip.bottom=std::min(clip.bottom,target.height()-1);
    if(clip.right<clip.left||clip.bottom<clip.top)return;
    const bool vertical=axis==RasterAxis::vertical;
    auto major=[&](const RasterVertex& p){return vertical?p.x:p.y;};
    auto minor=[&](const RasterVertex& p){return vertical?p.y:p.x;};
    const int count=static_cast<int>(vertices.size());
    int low=0,high=0;
    for(int i=0;i<count;++i){
        // Original word loop counters cannot traverse values outside this range.
        if(vertices[i].x < -32768||vertices[i].x>32767||vertices[i].y < -32768||vertices[i].y>32767)
            throw std::out_of_range("Raster vertex outside native word coordinate range");
        if(major(vertices[i])<major(vertices[low]))low=i;
        if(major(vertices[i])>major(vertices[high]))high=i;
    }
    const auto start=major(vertices[low]);
    const auto limit=std::min(major(vertices[high]),vertical?clip.right:clip.bottom);
    if(start>=limit)return;
    auto depth=q16(abs32(vertices[low].depth));
    const auto step_depth=divide32(q16(sub32(abs32(vertices[high].depth),abs32(vertices[low].depth))),
                                   sub32(major(vertices[high]),start));
    const int direction=((image.flags&0x20)?-1:1)*(vertical?1:-1);
    Edge first{low,direction},second{low,-direction};
    auto prepare=[&](Edge& edge){
        for(int guard=0;guard<count;++guard){
            const auto from=edge.index;
            edge.index=(edge.index+edge.direction+count)%count;
            const auto& a=vertices[from];const auto& b=vertices[edge.index];
            edge.remaining=major(b)-major(a);
            if(!edge.remaining)continue;
            if(edge.remaining<0)throw std::invalid_argument("Polygon is not monotone along raster axis");
            edge.coordinate=q16(minor(a));edge.u=q16(a.u);edge.v=q16(a.v);
            edge.step_coordinate=divide32(q16(sub32(minor(b),minor(a))),edge.remaining);
            edge.step_u=divide32(q16(sub32(b.u,a.u)),edge.remaining);
            edge.step_v=divide32(q16(sub32(b.v,a.v)),edge.remaining);
            return;
        }
        throw std::invalid_argument("Degenerate polygon edge chain");
    };
    auto advance=[](Edge& edge){
        --edge.remaining;
        edge.coordinate=add32(edge.coordinate,edge.step_coordinate);
        edge.u=add32(edge.u,edge.step_u);edge.v=add32(edge.v,edge.step_v);
    };
    for(auto position=start;position<=limit;++position){
        if(position<limit){if(!first.remaining)prepare(first);if(!second.remaining)prepare(second);}
        if(position>=(vertical?clip.left:clip.top)){
            auto begin=sar32(first.coordinate,16),end=sar32(second.coordinate,16);
            const auto min_clip=vertical?clip.top:clip.left,max_clip=vertical?clip.bottom:clip.right;
            if(begin<end&&end>=min_clip&&begin<=max_clip){
                const auto span=end-begin;
                // Vertical ASM keeps U fixed through a column.
                const auto du=vertical?0:divide32(sub32(second.u,first.u),span);
                const auto dv=divide32(sub32(second.v,first.v),span);
                auto u=first.u,v=first.v;
                if(begin<min_clip){
                    u=add32(u,multiply32(min_clip-begin,du));
                    v=add32(v,multiply32(min_clip-begin,dv));begin=min_clip;
                }
                end=std::min(end,max_clip);
                const auto row=light_row(depth,light_near);
                const auto* colors=light.data+row*256;
                if(!vertical)span_pixels<false,false>(target,image,colors,position,begin,end,u,v,du,dv);
                else if(image.flags&8)span_pixels<true,true>(target,image,colors,position,begin,end,u,v,du,dv);
                else span_pixels<true,false>(target,image,colors,position,begin,end,u,v,du,dv);
            }
        }
        advance(first);advance(second);depth=add32(depth,step_depth);
    }
}
} // namespace dok
