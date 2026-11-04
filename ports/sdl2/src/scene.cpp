#include "dok/scene.hpp"
#include "dok/arithmetic.hpp"
#include "dok/interactions.hpp"
#include "dok/gpu_geometry.hpp"
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace dok {
using namespace original;
namespace {
std::int16_t word(std::int32_t n){return signed16(static_cast<std::uint16_t>(n));}
bool joins(unsigned cls){return cls==2||cls==4||cls==7;}
}
Image flowing_surface_image(const Image& source,std::uint64_t ticks){
    Image result=source;
    const unsigned phase=(ticks/334)%18;
    const auto flags=source.flags;
    if(!(flags&0x3c00))return result;
    // Match the original pair priority and divide-before-multiply crop.
    const bool horizontal=(flags&0x0c00)!=0;
    const int width=static_cast<int>(source.width),height=static_cast<int>(source.height);
    const int extent=horizontal?width:height;
    const int shift=(extent/18)*phase;
    const int direction=(flags&0x0400)?-1:(flags&0x0800)?1:(flags&0x1000)?-1:1;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const int sx=horizontal?(x+direction*shift+width)%width:x;
        const int sy=horizontal?y:(y+direction*shift+height)%height;
        result.pixels[std::size_t(y)*source.width+x]=source.pixels[std::size_t(sy)*source.width+sx];
    }
    return result;
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
    // PKR 1098:3335/3343 compares the already reduced minimum, not near corner 1.
    // Keeping that asymmetric native bound preserves equal-depth polygon ordering.
    return {std::max(0,*std::min_element(xs.begin(),xs.end())),std::max(0,*std::min_element(zs.begin(),zs.end())),
        std::min(map_width-1,std::max({xs[0],xs[2],xs[3]})),std::min(map_length-1,std::max({zs[0],zs[2],zs[3]}))};
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
        if(object.class_id==12&&level.dataset!=DatasetId::old_gold&&(images_[index].flags&0x3c00)){
            Image copy=images_[index];
            index=images_.size();images_.push_back(copy);
            flowing_sources_.emplace(object_images_.size(),std::move(copy));
        }
        object_images_.push_back(index);bound_images_.push_back(nullptr);
        for(const auto& field:object.fields)if(field.kind==FieldKind::image)load(field.bytes);
    }
    auto light=archive.light(level.dataset==DatasetId::old_gold?"\x91\x82\x85\x92_\x97":level.light);light_.assign(light.data,light.data+light.size);
    palette_=archive.palette(level.palette);
    software_resolution(320,200);
}
std::size_t SoftwareScene::resource(const std::string& name)const{return names_.at(name);}
void SoftwareScene::software_resolution(int width,int height){
    if(!((width==320&&height==200)||(width==640&&(height==400||height==480))||(width==800&&height==600)||(width==1024&&height==768)))
        throw std::invalid_argument("Not an original software video mode");
    // PKR 1008:0b8b..0b9f: K = trunc(4096 * horizontal scale).
    // 1008:0cf0..0d18: viewport height = screen height - trunc(panel height * vertical scale).
    // OG 203d:00cb and measured LEVEL4 RAM: 320x171 -> clip320x170.
    const float panel_height=dataset_==DatasetId::old_gold?29.0f:39.0f;
    const int height_argument=int(float(height)-panel_height*(float(height)/200.0f));
    software_camera_={static_cast<std::int16_t>(width),static_cast<std::int16_t>(height_argument),8,
        static_cast<std::int16_t>(4096.0f*(float(width)/320.0f)),200};
    const int scene_height=(height_argument/2)*2;
    if(surface_.width()!=width||surface_.height()!=scene_height)surface_=IndexedSurface(width,scene_height);
    frame_height_=height;presentation_view_width_=320;
}
void SoftwareScene::software_presentation(int width,int height){
    if(width<1||width>32766||height<2||height>614||height%2)throw std::invalid_argument("Invalid presentation viewport");
    presentation_view_width_=int(std::lround(width*160.0/height));
    software_camera_={static_cast<std::int16_t>(width),static_cast<std::int16_t>(height+1),8,
        static_cast<std::int16_t>(4096.0*height/160.0),200};
    if(surface_.width()!=width||surface_.height()!=height)surface_=IndexedSurface(width,height);
}
const IndexedSurface& SoftwareScene::render(const World& world,Pose pose,const std::vector<PositionedImage>& dynamic,const Draw& gpu,int view_width,std::array<double,4> camera_offset,const ImageGetter& getter,int gpu_height){
    const auto& level=world.definition;const auto side=level.cell_side;
    if(dataset_!=level.dataset||level_name_!=level.name||object_images_.size()!=level.objects.size())
        throw std::invalid_argument("Scene/world mismatch");
    std::vector<bool> replaced(world.objects.size(),false);
    for(const auto& instance:dynamic)if(instance.replaces&&instance.replaces<=world.objects.size())replaced[instance.replaces-1]=true;
    for(const auto& entry:door_sources_){
        const auto& state=*world.objects.at(entry.first).door;
        const auto key=std::make_pair(int(state.phase),int(state.extent));
        auto& image=images_[object_images_[entry.first]];
        const auto found=door_phases_.find(entry.first);
        if(found==door_phases_.end()||found->second!=key){
            image=door_image(entry.second,state);
            door_phases_[entry.first]=key;
        }
        // Raster winding is mutable and was reset by the old per-frame copy.
        image.flags=entry.second.flags;
    }
    const unsigned flowing_phase=(world.simulation_ticks()/334)%18;
    for(const auto& entry:flowing_sources_){
        auto& image=images_[object_images_[entry.first]];
        if(flowing_phase_!=flowing_phase)
            image=flowing_surface_image(entry.second,world.simulation_ticks());
        image.flags=entry.second.flags;
    }
    flowing_phase_=flowing_phase;
    // Update all base materials, including animated walls, before dimensions
    // enter the polygon constructors. Copy keeps raster winding private to Scene.
    if(getter)for(std::size_t i=0;i<world.objects.size();++i)
        if(const auto* image=getter(world.objects[i].owner)){
            auto& material=images_[object_images_[i]];
            if(bound_images_[i]!=image){material=*image;bound_images_[i]=image;}
            material.flags=image->flags;
        }
    auto parameters=gpu?CameraParameters::kremlin_vga():software_camera_;if(gpu)parameters.width=static_cast<std::int16_t>(view_width);
    if(discovering_)parameters={320,static_cast<std::int16_t>(dataset_==DatasetId::old_gold?171:161),8,4096,200};
    const double focal=discovering_?200.0:presentation_focal(base_fov_);
    if(base_fov_&&!discovering_)parameters.focal_q12=static_cast<std::int16_t>(parameters.focal_q12*focal/200.0);
    SoftwareCamera camera(parameters);camera.set_pose(pose);
    const auto& hero=world.objects.at(*world.player);
    const GpuCamera precise_camera(pose,view_width,hero.motion?hero.motion->mouse_remainder:0,camera_offset,gpu_height,focal);
    const auto vertical_plane=[&](const SoftwareCamera& c,Pose a,Pose b,std::int16_t h,int tw,int th,
        std::uint8_t flags=1,std::int16_t bias=0,int v0=0,int v1=0,bool reflection=false){
        auto result=gpu?precise_camera.vertical(a,b,h,tw,th,flags,v0,v1,reflection):
            dok::vertical_plane(c,a,b,h,tw,th,flags,bias,v0,v1,reflection);
        if(gpu&&dataset_==DatasetId::old_gold&&result){const auto p=c.transform(a.x,a.y,a.z);result->old_gold_light=old_gold_light_row(p.lateral,p.depth,1008);}return result;
    };
    const auto billboard=[&](const SoftwareCamera& c,Pose p,std::int16_t w,std::int16_t h,int tw,int th,std::uint8_t flags=1){
        auto result=gpu?precise_camera.sprite(p,w,h,tw,th,flags):dok::billboard(c,p,w,h,tw,th,flags);
        if(gpu&&dataset_==DatasetId::old_gold&&result){const auto v=c.transform(p.x,p.y,p.z);result->old_gold_light=old_gold_light_row(sub32(p.x,pose.x),v.depth,2184);}return result;
    };
    const auto horizontal_plane=[&](const SoftwareCamera& c,Pose p,std::int16_t side,int tw,int th,bool ceiling=false){
        auto result=gpu?precise_camera.horizontal(p,side,tw,th,ceiling):dok::horizontal_plane(c,p,side,tw,th,ceiling);
        if(dataset_==DatasetId::old_gold&&result){
            auto a=c.transform(p.x,p.y,add32(p.z,side)),b=c.transform(add32(p.x,side),p.y,p.z);
            if(a.depth<0)a.depth=sar32(a.depth,5);if(b.depth<0)b.depth=sar32(b.depth,5);
            result->old_gold_light=old_gold_light_row(a.lateral,a.depth,1008);
            // OG 24cf:1964..19a0 / 1f8f..1fcb adds camera-relative Y/4,
            // then ABS, unlike Kremlin's max(0, average depth).
            const auto key=add32(sar32(add32(a.depth,b.depth),1),sar32(sub32(p.y,pose.y),2));
            result->sort_key=key<0?sub32(0,key):key;
        }return result;
    };
    std::vector<RenderPolygon> polygons;
    const auto bounds=discovering_?original_cell_bounds(pose,side,level.width,level.length):
        view_cell_bounds(pose,side,level.width,level.length,int(std::ceil((gpu?view_width:presentation_view_width_)*200.0/focal)));
    const auto& tmap=world.maps.at("tmap");const auto& fcmap=world.maps.at("fcmap");const auto& mmap=world.maps.at("mmap");
    auto at=[&](const char* map,int x,int z)->const ObjectDefinition&{
        x=std::clamp(x,0,level.width-1);z=std::clamp(z,0,level.length-1);
        const auto& cells=map[0]=='t'?tmap:map[0]=='f'?fcmap:mmap;
        return *level.object(cells[static_cast<std::size_t>(z)*level.width+x]);
    };
    auto picture=[&](const ObjectDefinition& object){return object_images_.at(static_cast<std::size_t>(level.glyph_index[object.glyph]));};
    int map_cell=-1;
    auto add=[&](std::optional<RenderPolygon> polygon,std::size_t material){
        if(polygon){polygon->material=material;polygon->map_cell=map_cell;polygons.push_back(std::move(*polygon));}
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
        map_cell=z*level.width+x;
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
    std::vector<const Image*> borrowed;
    struct RestoreImages {std::vector<Image>& images;std::size_t count;~RestoreImages(){images.resize(count);}} restore{images_,static_images};
    for(const auto& instance:dynamic){
        if(!instance.image)continue;
        const auto& image=*instance.image;
        auto polygon=billboard(camera,instance.pose,word(image.width*instance.scale),word(image.height*instance.scale),
            image.width,image.height,static_cast<std::uint8_t>(instance.flags));
        if(polygon){
            const int x=std::clamp(instance.pose.x/side,0,level.width-1),z=std::clamp(instance.pose.z/side,0,level.length-1);
            polygon->map_cell=z*level.width+x;
            const auto material=images_.size();
            // GPU sprites do not mutate winding. Reuse already-transparent
            // immutable animation frames; retain the copy fallback for other flags.
            if(gpu&&(image.flags&8)){images_.push_back({image.width,image.height,image.flags,{}});borrowed.push_back(&image);}
            else{images_.push_back(image);images_.back().flags|=8;borrowed.push_back(nullptr);}
            add(std::move(polygon),material);
        }
    }
    if(dataset_==DatasetId::old_gold)for(auto& p:polygons){
        const float shade=1920.0f+120.0f*p.old_gold_light;
        for(auto& v:p.vertices)v.shade_depth=shade;
        for(auto& v:p.secondary)v.shade_depth=shade;
    }
    if(geometry_observer_&&!discovering_)geometry_observer_(polygons);
    if(!discovering_)polygon_count_=polygons.size();
    // Native column occlusion is quantized and unsuitable for subpixel GPU geometry.
    if(!gpu)sort_and_cull(polygons,camera.clip());
    else std::stable_sort(polygons.begin(),polygons.end(),[](const auto& a,const auto& b){return a.sort_key>b.sort_key;});
    if(discovering_){
        discovered_.clear();
        for(const auto& polygon:polygons)if((polygon.flags&1)&&polygon.map_cell>=0)
            discovered_.push_back(static_cast<std::size_t>(polygon.map_cell));
        std::sort(discovered_.begin(),discovered_.end());
        discovered_.erase(std::unique(discovered_.begin(),discovered_.end()),discovered_.end());
        return surface_;
    }
    if(!gpu||surface_dirty_)surface_.clear();
    surface_dirty_=!gpu;
    const Bytes light{light_.data(),light_.size()};
    for(const auto& polygon:polygons)if(polygon.flags&1){
        if(gpu&&polygon.material>=static_images&&borrowed[polygon.material-static_images]){
            gpu(polygon.vertices,*borrowed[polygon.material-static_images],light,polygon.axis);continue;
        }
        auto& image=images_[polygon.material];
        if(polygon.axis==RasterAxis::vertical&&polygon.updates_image_winding)
            image.flags=static_cast<std::uint16_t>((image.flags&~0x20)|(polygon.reverse_edges?0x20:0));
        if(gpu)gpu(polygon.vertices,image,light,polygon.axis);else if(dataset_==DatasetId::old_gold)rasterize_old_gold(surface_,polygon.vertices,image,light,camera.clip(),polygon.old_gold_light,polygon.axis);else rasterize(surface_,polygon.vertices,image,light,camera.clip(),polygon.axis,1920,raster_reference_?&*raster_reference_:nullptr);
        if(!polygon.secondary.empty()){
            if(gpu)gpu(polygon.secondary,images_[polygon.secondary_material],light,RasterAxis::horizontal);
            else if(dataset_==DatasetId::old_gold)rasterize_old_gold(surface_,polygon.secondary,images_[polygon.secondary_material],light,camera.clip(),polygon.old_gold_light,RasterAxis::horizontal);
            else rasterize(surface_,polygon.secondary,images_[polygon.secondary_material],light,camera.clip(),RasterAxis::horizontal,1920,raster_reference_?&*raster_reference_:nullptr);
        }
    }
    return surface_;
}
std::vector<std::size_t> SoftwareScene::discovered_cells(const World& world,Pose pose,
    const std::vector<PositionedImage>& dynamic,const ImageGetter& getter){
    if(discovering_)throw std::logic_error("Recursive map discovery");
    discovering_=true;
    struct Reset {bool& value;~Reset(){value=false;}} reset{discovering_};
    render(world,pose,dynamic,{},320,{},getter);
    return discovered_;
}
} // namespace dok
