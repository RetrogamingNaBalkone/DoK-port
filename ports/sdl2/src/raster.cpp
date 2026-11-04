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
                 std::int32_t du,std::int32_t dv,bool native_bank_wrap){
    auto u=std::uint32_t(u0),v=std::uint32_t(v0);
    // PKR 1010:0030..00af packs V fraction and the texture address into ESI,
    // U fraction and span counter into EAX/ECX. ADC's low-word carry is observable.
    std::uint32_t packed_u=u<<16;
    std::uint32_t packed_v=(v<<16)|static_cast<std::uint16_t>(sar32(v0,16)*image.width+sar32(u0,16));
    const std::uint32_t packed_step_v=(std::uint32_t(dv)<<16)|static_cast<std::uint16_t>(sar32(dv,16)*image.width+sar32(du,16));
    std::size_t destination=Vertical?std::size_t(begin)*target.width()+position:std::size_t(position)*target.width()+begin;
    const auto stride=Vertical?target.width():1;
    for(int pixel=begin;pixel<=end;++pixel){
        const auto tu=signed16(static_cast<std::uint16_t>(u>>16));
        const auto tv=signed16(static_cast<std::uint16_t>(v>>16));
        const auto offset=Vertical?std::int64_t(tv)*image.width+tu:std::int64_t(packed_v&65535);
        if(offset<0||offset>=static_cast<std::int64_t>(image.pixels.size()))
            throw std::out_of_range("Native sampler escaped texture payload");
        const auto mapped=colors[image.pixels[static_cast<std::size_t>(offset)]];
        if constexpr(Transparent){if(mapped)target.pixels[destination]=mapped;}
        else target.pixels[destination]=mapped;
        destination+=stride;
        if constexpr(!Vertical){
            // 1010:009d/00ba skips UV advancement when the destination WORD wraps.
            // Reference-only: normal software rendering fixes that visible VESA seam.
            if(native_bank_wrap&&(destination&65535)==0)continue;
            const std::uint32_t step_u=(std::uint32_t(du)<<16)|static_cast<std::uint16_t>(end-pixel+1);
            const std::uint64_t next_u=std::uint64_t(packed_u)+step_u;packed_u=std::uint32_t(next_u);
            const std::uint64_t next_v=std::uint64_t(packed_v)+packed_step_v+(next_u>>32);packed_v=std::uint32_t(next_v);
            if(next_v>>32)packed_v=(packed_v&0xffff0000u)|static_cast<std::uint16_t>(packed_v+image.width);
        }
        v+=std::uint32_t(dv);
    }
}
}
void rasterize(IndexedSurface& target,const std::vector<RasterVertex>& vertices,
               const Image& image,Bytes light,ClipRect clip,RasterAxis axis,std::int16_t light_near,RasterReferenceState* reference){
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
    // PKR 1120:0c03 / vertical counterpart only write DS:c146 after a
    // strictly greater coordinate. Retaining it reproduces the original bug.
    int low=0,high=reference?reference->maximum_vertex:0;
    auto high_major=major(vertices[0]);
    for(int i=0;i<count;++i){
        // Original word loop counters cannot traverse values outside this range.
        if(vertices[i].x < -32768||vertices[i].x>32767||vertices[i].y < -32768||vertices[i].y>32767)
            throw std::out_of_range("Raster vertex outside native word coordinate range");
        if(major(vertices[i])<major(vertices[low]))low=i;
        if(major(vertices[i])>high_major){high=i;high_major=major(vertices[i]);}
    }
    const auto start=major(vertices[low]);
    if(reference)reference->maximum_vertex=high;
    const auto limit=std::min(high_major,vertical?clip.right:clip.bottom);
    if(start>=limit)return;
    if(high<0||high>=count)throw std::out_of_range("Native LIGHT history index outside polygon");
    auto depth=q16(abs32(vertices[low].depth));
    const auto depth_span=sub32(major(vertices[high]),start);
    const auto step_depth=depth_span?divide32(q16(sub32(abs32(vertices[high].depth),abs32(vertices[low].depth))),depth_span):0;
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
                if(!vertical)span_pixels<false,false>(target,image,colors,position,begin,end,u,v,du,dv,reference!=nullptr);
                else if(image.flags&8)span_pixels<true,true>(target,image,colors,position,begin,end,u,v,du,dv,reference!=nullptr);
                else span_pixels<true,false>(target,image,colors,position,begin,end,u,v,du,dv,reference!=nullptr);
            }
        }
        advance(first);advance(second);depth=add32(depth,step_depth);
    }
}
} // namespace dok

namespace dok {
// OG 2b57:0b36/0d35/0f01/1301: horizontal Bresenham edges,
// half-step texture samples and exclusive right/bottom bounds.
void rasterize_old_gold(IndexedSurface& target,const std::vector<RasterVertex>& input,const Image& image,Bytes light,ClipRect clip,unsigned row,RasterAxis axis){
    if(input.size()<3||light.size!=4096||row>15)throw std::invalid_argument("Invalid OG raster input");
    auto vertices=input;
    // Original draw helpers use texture dimensions, not last-texel indices.
    if(axis==RasterAxis::vertical)for(auto& v:vertices){v.u=divide32(multiply32(v.u,image.width),std::max(1,int(image.width)-1));v.v=divide32(multiply32(v.v,image.height),std::max(1,int(image.height)-1));}
    const int count=int(vertices.size());int low=0,high=0;
    for(int i=1;i<count;++i){if(vertices[i].y<vertices[low].y)low=i;if(vertices[i].y>vertices[high].y)high=i;}
    if(vertices[low].y>=vertices[high].y)return;
    struct Side{int direction,index,remaining=0,x=0,dx=0,sign=0,error=0,remainder=0,denominator=0;std::int32_t u=0,v=0,du=0,dv=0;};
    auto prepare=[&](Side& e){
        int from=e.index;
        for(int guard=0;guard<count;++guard){
            if(from==high)return false;
            const int next=(from+e.direction+count)%count;const auto& a=vertices[from];const auto& b=vertices[next];
            const int dy=b.y-a.y;if(!dy){from=next;continue;}
            if(dy<0)throw std::invalid_argument("OG polygon is not monotone");
            e.index=next;e.remaining=e.denominator=dy;e.x=a.x;e.u=multiply32(a.u,65536);e.v=multiply32(a.v,65536);
            e.du=divide32(sub32(multiply32(b.u,65536),e.u),dy);e.dv=divide32(sub32(multiply32(b.v,65536),e.v),dy);
            const int delta=b.x-a.x;e.sign=delta<0?-1:1;e.error=delta<0?1-dy:0;e.dx=(std::abs(delta)/dy)*e.sign;e.remainder=std::abs(delta)%dy;return true;
        }return false;
    };
    auto advance=[&](Side& e){if(--e.remaining==0)return prepare(e);e.u=add32(e.u,e.du);e.v=add32(e.v,e.dv);e.x+=e.dx;e.error+=e.remainder;if(e.error>0){e.x+=e.sign;e.error-=e.denominator;}return true;};
    Side left{(image.flags&0x20)?1:-1,low},right{-left.direction,low};
    if(!prepare(left)||!prepare(right))return;
    clip.left=std::max(0,clip.left);clip.top=std::max(0,clip.top);clip.right=std::min(target.width()-1,clip.right);clip.bottom=std::min(target.height()-1,clip.bottom);
    for(int y=vertices[low].y;y<=clip.bottom;++y){
        if(y>=clip.top&&left.x<right.x&&right.x>clip.left&&left.x<=clip.right){
            const int n=right.x-left.x;const auto du=divide32(sub32(right.u,left.u),n),dv=divide32(sub32(right.v,left.v),n);
            auto u=add32(left.u,sar32(du,1)),v=add32(left.v,sar32(dv,1));const int begin=std::max(left.x,clip.left),end=std::min(right.x,clip.right+1);
            u=add32(u,multiply32(du,begin-left.x));v=add32(v,multiply32(dv,begin-left.x));
            // OG 2b57:1067..10e3 uses the same packed ADC sampler as Kremlin,
            // with destination offset / +1 in the low U word instead of a counter.
            std::uint32_t packed_u=(std::uint32_t(u)<<16)|static_cast<std::uint16_t>(y*target.width()+begin);
            std::uint32_t packed_v=(std::uint32_t(v)<<16)|static_cast<std::uint16_t>(sar32(v,16)*image.width+sar32(u,16));
            const std::uint32_t step_u=(std::uint32_t(du)<<16)|1;
            const std::uint32_t step_v=(std::uint32_t(dv)<<16)|static_cast<std::uint16_t>(sar32(dv,16)*image.width+sar32(du,16));
            for(int x=begin;x<end;++x){const auto offset=packed_v&65535;
                if(offset>=image.pixels.size())throw std::out_of_range("OG sampler escaped texture");
                const auto index=image.pixels[offset];if(!(image.flags&8)||index)target.pixels[std::size_t(y)*target.width()+x]=light.data[row*256+index];
                const auto next_u=std::uint64_t(packed_u)+step_u;packed_u=std::uint32_t(next_u);
                const auto next_v=std::uint64_t(packed_v)+step_v+(next_u>>32);packed_v=std::uint32_t(next_v);
                if(next_v>>32)packed_v=(packed_v&0xffff0000u)|static_cast<std::uint16_t>(packed_v+image.width);
            }
        }
        if(!advance(left)||!advance(right))break;
    }
}
}
