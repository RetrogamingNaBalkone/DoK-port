#include "dok/geometry.hpp"
#include "dok/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
using namespace original;
std::uint8_t old_gold_light_row(std::int32_t x,std::int32_t depth,int bias){
    auto abs_word=[](std::int32_t n){const auto w=signed16(static_cast<std::uint16_t>(n));return w<0?signed16(static_cast<std::uint16_t>(-int(w))):w;};
    const auto sum=signed16(static_cast<std::uint16_t>(abs_word(x)+abs_word(depth)-bias));
    return static_cast<std::uint8_t>(std::min(15,std::max(0,int(sum))/154));
}
namespace {
std::optional<RenderPolygon> project_vertical(const SoftwareCamera& camera,
    CameraPoint a,CameraPoint b,std::int16_t height,int texture_width,int texture_height,
    std::uint8_t flags,std::int16_t sort_bias,int v_start,int v_end,bool reflection=false){
    if(texture_width<1||texture_height<1)throw std::invalid_argument("Empty plane texture");
    if(a.depth<=0&&b.depth<=0)return std::nullopt;
    const auto og_light=old_gold_light_row(a.lateral,a.depth,1008);
    std::int32_t u0=0,u1=texture_width-1;
    if(a.depth<0){
        const auto divisor=sub32(b.depth,a.depth);
        a.lateral=add32(divide32(multiply32(sub32(a.lateral,b.lateral),b.depth),divisor),b.lateral);
        u0=divide32(multiply32(u1,sub32(0,a.depth)),divisor);a.depth=0;
    }else if(b.depth<0){
        const auto divisor=sub32(a.depth,b.depth);
        b.lateral=add32(divide32(multiply32(sub32(b.lateral,a.lateral),a.depth),divisor),a.lateral);
        u1=divide32(multiply32(u1,a.depth),divisor);b.depth=0;
    }
    auto bottom_a=camera.project(a),bottom_b=camera.project(b);
    a.vertical=sub32(a.vertical,height);b.vertical=sub32(b.vertical,height);
    auto top_a=camera.project(a),top_b=camera.project(b);
    const auto clip=camera.clip();
    if(top_a.x==top_b.x||std::min(top_a.x,top_b.x)>clip.right||std::max(top_a.x,top_b.x)<clip.left)return std::nullopt;
    // Center constructor BP+1c swaps projected endpoints, but its depth slots
    // and U range remain in the original order (10b0:03ee onward).
    if(reflection){std::swap(top_a,top_b);std::swap(bottom_a,bottom_b);}
    if(!v_end)v_end=texture_height-1; // Native zero means the full texture height.
    RenderPolygon out;
    out.old_gold_light=og_light;
    out.flags=flags|1;out.reverse_edges=top_b.x<top_a.x;
    out.sort_key=std::max(0,sub32(sar32(add32(a.depth,b.depth),1),sort_bias));
    out.vertices={{top_a.x,top_a.y,a.depth,u0,v_start},{top_b.x,top_b.y,b.depth,u1,v_start},
                  {bottom_b.x,bottom_b.y,b.depth,u1,v_end},{bottom_a.x,bottom_a.y,a.depth,u0,v_end}};
    return out;
}
}
std::optional<RenderPolygon> vertical_plane(const SoftwareCamera& camera,Pose a,Pose b,
    std::int16_t height,int texture_width,int texture_height,std::uint8_t flags,
    std::int16_t sort_bias,int v_start,int v_end,bool reflection){
    if(a.y!=b.y)throw std::invalid_argument("Vertical plane endpoints require equal base Y");
    return project_vertical(camera,camera.transform(a.x,a.y,a.z),camera.transform(b.x,b.y,b.z),
                            height,texture_width,texture_height,flags,sort_bias,v_start,v_end,reflection);
}
std::optional<RenderPolygon> billboard(const SoftwareCamera& camera,Pose origin,
    std::int16_t width,std::int16_t height,int texture_width,int texture_height,std::uint8_t flags){
    auto a=camera.transform(origin.x,origin.y,origin.z),b=a;
    if(a.depth<=0)return std::nullopt;
    a.lateral=sub32(a.lateral,sar32(width,1));b.lateral=add32(b.lateral,sar32(width,1));
    auto result=project_vertical(camera,a,b,height,texture_width,texture_height,flags,
                                  static_cast<std::int16_t>(sar32(width,2)),0,0);
    if(result){
        result->old_gold_light=old_gold_light_row(sub32(origin.x,camera.pose().x),a.depth,2184);
        result->updates_image_winding=false;
        auto clip=camera.clip();
        if(result->vertices[0].x>=result->vertices[1].x||
           result->vertices[2].y<=clip.top||result->vertices[0].y>=clip.bottom)return std::nullopt;
    }
    return result;
}
void sort_and_cull(std::vector<RenderPolygon>& polygons,ClipRect clip){
    // Native halving-gap sort, including its behaviour with equal keys.
    const auto count=static_cast<int>(polygons.size());
    for(int gap=count/2;gap>0;gap/=2)
        for(int i=gap;i<count;++i)
            for(int j=i-gap;j>=0&&polygons[j].sort_key<polygons[j+gap].sort_key;j-=gap)
                std::swap(polygons[j],polygons[j+gap]);
    const auto left=std::max(0,clip.left),right=std::min(1299,clip.right);
    if(left>right){for(auto& p:polygons){p.flags&=~1;p.sort_key=0;}return;}
    std::vector<bool> open(static_cast<std::size_t>(right-left+1),true);
    for(auto it=polygons.rbegin();it!=polygons.rend();++it){
        auto& p=*it;
        int a=32767,b=-32768;
        for(const auto& v:p.vertices){a=std::min(a,v.x);b=std::max(b,v.x);}
        a=std::max(a,left);b=std::min(b,right);
        p.flags&=~1;p.sort_key=0;
        for(int x=a;x<=b;++x)if(open[static_cast<std::size_t>(x-left)]){
            p.flags|=1;
            if(p.flags&2)open[static_cast<std::size_t>(x-left)]=false;
            else break;
        }
    }
}
std::optional<RenderPolygon> horizontal_plane(const SoftwareCamera& camera,Pose origin,
    std::int16_t side,int texture_width,int texture_height,bool ceiling){
    if(texture_width<1||texture_height<1)throw std::invalid_argument("Empty horizontal texture");
    std::array<CameraPoint,4> corners={{camera.transform(origin.x,origin.y,add32(origin.z,side)),
        camera.transform(add32(origin.x,side),origin.y,add32(origin.z,side)),
        camera.transform(add32(origin.x,side),origin.y,origin.z),camera.transform(origin.x,origin.y,origin.z)}};
    if(std::all_of(corners.begin(),corners.end(),[](auto p){return p.depth<=0;}))return std::nullopt;
    for(auto& p:corners)if(p.depth<0)p.depth=sar32(p.depth,5); // SAR20 of rotation numerator.
    RenderPolygon result;result.axis=RasterAxis::horizontal;result.flags=0x19;
    result.old_gold_light=old_gold_light_row(corners[0].lateral,corners[0].depth,1008);
    result.sort_key=std::max(0,sar32(add32(corners[0].depth,corners[2].depth),1));
    const std::array<int,4> u={{0,texture_width-1,texture_width-1,0}},v={{0,0,texture_height-1,texture_height-1}};
    for(int i=0;i<4;++i){const auto& p=corners[ceiling?3-i:i];auto screen=camera.project(p);
        result.vertices.push_back({screen.x,screen.y,p.depth,u[i],v[i]});}
    return result;
}
} // namespace dok
