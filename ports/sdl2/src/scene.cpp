#include "dok/scene.hpp"
#include "dok/arithmetic.hpp"
#include "dok/interactions.hpp"
#include "dok/gpu_geometry.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
using namespace original;
namespace {
std::int16_t word(std::int32_t n){return signed16(static_cast<std::uint16_t>(n));}
bool joins(unsigned cls){return cls==2||cls==4||cls==7;}
}
CellBounds original_cell_bounds(Pose pose,int side,int map_width,int map_length){
    if(side<=0||map_width<=0||map_length<=0)throw std::invalid_argument("Invalid scene grid");
    const auto s=original_sine(pose.heading),c=original_cosine(pose.heading);
    const auto rs=original_sine(360-pose.heading),rc=original_cosine(360-pose.heading);
    const auto x=add32(pose.x,sar32(multiply32(side*2,s),15));
    const auto z=sub32(pose.z,sar32(multiply32(side*2,c),15));
    std::array<std::int32_t,4> xs={{add32(x,sar32(multiply32(side,rc),15)),
        add32(x,sar32(multiply32(-side,rc),15)),
        add32(x,sar32(add32(multiply32(side*6,rc),multiply32(side*9,rs)),15)),
        add32(x,sar32(add32(multiply32(side*-6,rc),multiply32(side*9,rs)),15))}};
    std::array<std::int32_t,4> zs={{add32(z,sar32(multiply32(-side,rs),15)),
        add32(z,sar32(multiply32(side,rs),15)),
        add32(z,sar32(sub32(multiply32(side*9,rc),multiply32(side*6,rs)),15)),
        add32(z,sar32(sub32(multiply32(side*9,rc),multiply32(side*-6,rs)),15))}};
    for(auto& n:xs)n=word(divide32(n,side));for(auto& n:zs)n=word(divide32(n,side));
    return {std::max(0,*std::min_element(xs.begin(),xs.end())),std::max(0,*std::min_element(zs.begin(),zs.end())),
        std::min(map_width-1,*std::max_element(xs.begin(),xs.end())),std::min(map_length-1,*std::max_element(zs.begin(),zs.end()))};
}
CellBounds view_cell_bounds(Pose pose,int side,int width,int length,int view_width){
    auto b=original_cell_bounds(pose,side,width,length);const int margin=std::max(0,(view_width-320+31)/32);
    b.min_x=std::max(0,b.min_x-margin);b.min_z=std::max(0,b.min_z-margin);
    b.max_x=std::min(width-1,b.max_x+margin);b.max_z=std::min(length-1,b.max_z+margin);return b;
}
SoftwareScene::SoftwareScene(const Archive& archive,const LevelDefinition& level){
    if(level.dataset==DatasetId::unknown)
        throw std::invalid_argument("Scene camera currently verified for Kremlin only");
    if(archive.identify()!=level.dataset)throw std::invalid_argument("Scene archive/profile mismatch");
    dataset_=level.dataset;level_name_=level.name;
    auto load=[&](const std::string& name){
        auto found=names_.find(name);if(found!=names_.end())return found->second;
        auto index=images_.size();images_.push_back(archive.image(name));names_[name]=index;return index;
    };
    for(const auto& object:level.objects){
        auto index=load(object.sprite);images_[index].flags|=object.sprite_flags;
        if((object.flags&0x800)||object.class_id==9){
            auto frames=archive.animation(object.animation);
            if(frames.empty())throw std::runtime_error("Empty initial object animation");
            index=images_.size();images_.push_back(std::move(frames[0].image));images_.back().flags=8;
        }
        if(object.class_id==7){
            Image copy=images_[index];
            index=images_.size();images_.push_back(copy);
            door_sources_.emplace(object_images_.size(),std::move(copy));
        }
        object_images_.push_back(index);
        for(const auto& field:object.fields)if(field.kind==FieldKind::image)load(field.bytes);
    }
    auto light=archive.light(level.dataset==DatasetId::old_gold?"\x91\x82\x85\x92_\x97":level.light);light_.assign(light.data,light.data+light.size);
    palette_=archive.palette(level.palette);
}
std::size_t SoftwareScene::resource(const std::string& name)const{return names_.at(name);}
const IndexedSurface& SoftwareScene::render(const World& world,Pose pose,const std::vector<PositionedImage>& dynamic,const Draw& gpu,int view_width,std::array<double,4> camera_offset){
    const auto& level=world.definition;const auto side=level.cell_side;
    if(dataset_!=level.dataset||level_name_!=level.name||object_images_.size()!=level.objects.size())
        throw std::invalid_argument("Scene/world mismatch");
    std::vector<bool> replaced(world.objects.size(),false);
    for(const auto& instance:dynamic)if(instance.replaces&&instance.replaces<=world.objects.size())replaced[instance.replaces-1]=true;
    for(const auto& entry:door_sources_)
        images_[object_images_[entry.first]]=door_image(entry.second,*world.objects.at(entry.first).door);
    auto parameters=CameraParameters::kremlin_vga();if(gpu)parameters.width=static_cast<std::int16_t>(view_width);
    SoftwareCamera camera(parameters);camera.set_pose(pose);
    const auto& hero=world.objects.at(*world.player);
    const GpuCamera precise_camera(pose,view_width,hero.motion?hero.motion->mouse_remainder:0,camera_offset);
    const auto vertical_plane=[&](const SoftwareCamera& c,Pose a,Pose b,std::int16_t h,int tw,int th,
        std::uint8_t flags=1,std::int16_t bias=0,int v0=0,int v1=0,bool reflection=false){
        return gpu?precise_camera.vertical(a,b,h,tw,th,flags,v0,v1,reflection):
            dok::vertical_plane(c,a,b,h,tw,th,flags,bias,v0,v1,reflection);
    };
    const auto billboard=[&](const SoftwareCamera& c,Pose p,std::int16_t w,std::int16_t h,int tw,int th,std::uint8_t flags=1){
        return gpu?precise_camera.sprite(p,w,h,tw,th,flags):dok::billboard(c,p,w,h,tw,th,flags);
    };
    const auto horizontal_plane=[&](const SoftwareCamera& c,Pose p,std::int16_t side,int tw,int th,bool ceiling=false){
        return gpu?precise_camera.horizontal(p,side,tw,th,ceiling):dok::horizontal_plane(c,p,side,tw,th,ceiling);
    };
    std::vector<RenderPolygon> polygons;
    const auto bounds=view_cell_bounds(pose,side,level.width,level.length,gpu?view_width:320);
    auto at=[&](const char* map,int x,int z)->const ObjectDefinition&{
        x=std::clamp(x,0,level.width-1);z=std::clamp(z,0,level.length-1);
        return *level.object(world.maps.at(map)[static_cast<std::size_t>(z)*level.width+x]);
    };
    auto picture=[&](const ObjectDefinition& object){return object_images_.at(static_cast<std::size_t>(level.glyph_index[object.glyph]));};
    auto add=[&](std::optional<RenderPolygon> polygon,std::size_t material){
        if(polygon){polygon->material=material;polygons.push_back(std::move(*polygon));}
    };
    auto edge=[&](int x,int y,int z,int width,int height,int orientation,std::size_t material,
                   std::uint8_t flags,int v0=0,int v1=0){
        Pose a{x,y,z,0},b=a;
        if(orientation==0)b.x+=width;
        else if(orientation==90){a.z+=width;}
        else if(orientation==180){a.x+=width;a.z+=width;b.z+=width;}
        else {a.x+=width;b.x+=width;b.z+=width;}
        const auto& image=images_[material];
        auto polygon=vertical_plane(camera,a,b,word(height),image.width,image.height,flags,0,v0,v1);
        if(polygon)polygon->updates_image_winding=false; // Edge draw 10b0:0bf5 does not change image bit20.
        add(std::move(polygon),material);
    };
    for(int z=bounds.min_z;z<=bounds.max_z;++z)for(int x=bounds.min_x;x<=bounds.max_x;++x){
        const auto& object=at("tmap",x,z);const auto& surface=at("fcmap",x,z);
        const int px=x*side,pz=z*side,base=negate16(surface.height_parameter),inset=surface.number(0x54);
        const auto material=picture(object);const auto& image=images_[material];
        const int width=word(image.width*object.width_parameter),height=word(image.height*object.width_parameter);
        const std::array<int,4> dx={{0,-1,0,1}},dz={{-1,0,1,0}},orientation={{0,90,180,270}};
        if(inset>=0){
            auto ceiling_side=resource(surface.resource(0x5e));
            const auto& texture=images_[ceiling_side];
            for(int i=0;i<4;++i)if(at("fcmap",x+dx[i],z+dz[i]).number(0x54)<inset)
                // BP-52 is column X, BP-56 is row Z (34c8 / outer loop).
                // 35f9..360d builds the step vector in ordinary X/Y/Z order.
                edge(px,inset-level.cell_height,pz,side,inset,orientation[i],ceiling_side,
                     static_cast<std::uint8_t>(surface.flags),0,word(divide32(multiply32(texture.height,inset),level.cell_height)));
        }
        if(object.class_id==2){
            for(int i=0;i<4;++i)if(at("tmap",x+dx[i],z+dz[i]).class_id!=2){
                const auto neighbour=at("fcmap",x+dx[i],z+dz[i]).number(0x54);
                edge(px,base,pz,width,word(height-neighbour+1),orientation[i],material,
                     static_cast<std::uint8_t>(object.flags),word(divide32(multiply32(image.height,neighbour),level.cell_height)));
            }
        }else{
            auto floor_material=surface.class_id==12?picture(surface):resource(surface.resource(0x5a));
            const auto& floor=images_[floor_material];
            auto ground=horizontal_plane(camera,{px,base,pz,0},word(side),floor.width,floor.height);
            if(ground&&inset>=0){
                auto top_material=resource(surface.resource(0x56));const auto& top=images_[top_material];
                auto roof=horizontal_plane(camera,{px,sub32(base,word(level.cell_height+1-inset-surface.height_parameter)),pz,0},
                    word(side),top.width,top.height,true);
                if(roof){ground->secondary=std::move(roof->vertices);ground->secondary_material=top_material;}
            }
            add(std::move(ground),floor_material);
            if((object.class_id==3||object.class_id==9||object.class_id==10||object.class_id==6)&&!replaced[level.glyph_index[object.glyph]])
                add(billboard(camera,{px+side/2,base,pz+side/2,0},word(width),word(height),image.width,image.height,
                              static_cast<std::uint8_t>(object.flags)),material);
            else if(object.class_id==4||object.class_id==7){
                if(!joins(at("tmap",x,z-1).class_id)&&!joins(at("tmap",x,z+1).class_id))
                    add(vertical_plane(camera,{px,base,pz+width/2,0},{px+width,base,pz+width/2,0},word(height),
                        image.width,image.height,static_cast<std::uint8_t>(world.objects[level.glyph_index[object.glyph]].flags),word(width>>2),0,0,at("tmap",x-1,z).class_id==2),material);
                if(!joins(at("tmap",x-1,z).class_id)&&!joins(at("tmap",x+1,z).class_id))
                    add(vertical_plane(camera,{px+width/2,base,pz,0},{px+width/2,base,pz+width,0},word(height),
                        image.width,image.height,static_cast<std::uint8_t>(world.objects[level.glyph_index[object.glyph]].flags),word(width>>2),0,0,at("tmap",x,z-1).class_id==2),material);
            }
        }
        const auto& actor=at("mmap",x,z);
        if((actor.class_id==9||actor.class_id==3)&&!replaced[level.glyph_index[actor.glyph]]){
            auto id=picture(actor);const auto& sprite=images_[id];
            auto position=Pose{px+side/2,negate16(actor.height_parameter),pz+side/2,0};
            if(actor.class_id==9){const auto& state=world.objects[level.glyph_index[actor.glyph]];if(state.pose)position=*state.pose;}
            add(billboard(camera,position,word(sprite.width*actor.width_parameter),word(sprite.height*actor.width_parameter),
                sprite.width,sprite.height,static_cast<std::uint8_t>(actor.flags)),id);
        }
    }
    const auto static_images=images_.size();
    struct RestoreImages {std::vector<Image>& images;std::size_t count;~RestoreImages(){images.resize(count);}} restore{images_,static_images};
    for(const auto& instance:dynamic){
        if(!instance.image)continue;
        const auto& image=*instance.image;
        auto polygon=billboard(camera,instance.pose,word(image.width*instance.scale),word(image.height*instance.scale),
            image.width,image.height,static_cast<std::uint8_t>(instance.flags));
        if(polygon){
            const auto material=images_.size();images_.push_back(image);images_.back().flags|=8;
            add(std::move(polygon),material);
        }
    }
    polygon_count_=polygons.size();
    // Native column occlusion is quantized and unsuitable for subpixel GPU geometry.
    if(!gpu)sort_and_cull(polygons,camera.clip());
    else std::stable_sort(polygons.begin(),polygons.end(),[](const auto& a,const auto& b){return a.sort_key>b.sort_key;});
    surface_.clear();
    const Bytes light{light_.data(),light_.size()};
    for(const auto& polygon:polygons)if(polygon.flags&1){
        auto& image=images_[polygon.material];
        if(polygon.axis==RasterAxis::vertical&&polygon.updates_image_winding)
            image.flags=static_cast<std::uint16_t>((image.flags&~0x20)|(polygon.reverse_edges?0x20:0));
        if(gpu)gpu(polygon.vertices,image,light,polygon.axis);else rasterize(surface_,polygon.vertices,image,light,camera.clip(),polygon.axis);
        if(!polygon.secondary.empty()){
            if(gpu)gpu(polygon.secondary,images_[polygon.secondary_material],light,RasterAxis::horizontal);
            else rasterize(surface_,polygon.secondary,images_[polygon.secondary_material],light,camera.clip(),RasterAxis::horizontal);
        }
    }
    return surface_;
}
} // namespace dok
