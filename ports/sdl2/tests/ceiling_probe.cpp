#include "dok/scene.hpp"
#include "dok/gpu_geometry.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Usage: ceiling_probe PKR.RES PKE.RES");
    std::cout<<"[";bool comma=false;
    for(int argument=1;argument<3;++argument){
        const auto archive=dok::Archive::open(std::filesystem::u8path(argv[argument]));
        dok::World world(dok::load_level(archive,"LEVEL1"));dok::SoftwareScene scene(archive,world.definition);
        const auto start=*world.objects.at(*world.player).pose;const auto& level=world.definition;
        std::vector<std::array<int,2>> cells{{start.x/level.cell_side,start.z/level.cell_side}};
        const auto bounds=dok::original_cell_bounds(start,level.cell_side,level.width,level.length);
        int nearest=100000;std::array<int,2> near_cell{};
        for(int z=bounds.min_z;z<=bounds.max_z;++z)for(int x=bounds.min_x;x<=bounds.max_x;++x){
            const auto index=z*level.width+x;
            if(level.object(world.maps.at("fcmap")[index])->number(0x54)!=400||level.object(world.maps.at("tmap")[index])->class_id==2)continue;
            const int distance=std::abs(x-cells[0][0])+std::abs(z-cells[0][1]);
            if(distance<nearest){nearest=distance;near_cell={x,z};}
        }
        check(nearest<100000,"No low ceiling near LEVEL1 start");cells.push_back(near_cell);cells.push_back({57,52});cells.push_back({20,33});
        for(const auto cell:cells){
            auto pose=start;pose.x=cell[0]*level.cell_side+level.cell_side/2;pose.z=cell[1]*level.cell_side+level.cell_side/2;pose.heading=90;
            const auto& surface=*level.object(world.maps.at("fcmap")[cell[1]*level.width+cell[0]]);
            const int inset=surface.number(0x54),base=-surface.height_parameter;
            const int roof_y=base-(level.cell_height+1-inset-surface.height_parameter);
            check(roof_y==inset-level.cell_height-1,"Ceiling position depends on floor height");
            const auto top=archive.image(surface.resource(0x56));
            const dok::GpuCamera camera(pose,320);
            const auto roof=camera.horizontal({cell[0]*level.cell_side,roof_y,cell[1]*level.cell_side,0},level.cell_side,top.width,top.height,true);
            check(roof.has_value(),"Expected own-cell ceiling");
            unsigned seen=0;scene.inspect_geometry([&](const auto& polygons){
                for(const auto& p:polygons)for(const auto& v:p.secondary)if(v.precise){
                    for(const auto& expected:roof->vertices){const auto& a=*v.precise;const auto& b=*expected.precise;
                        if(std::abs(a[0]-b[0])<0.001&&std::abs(a[1]-b[1])<0.001&&std::abs(a[2]-b[2])<0.001){++seen;break;}}
                }
            });
            scene.render(world,pose,{},[](const auto&,const auto&,auto,auto){});
            check(seen>=roof->vertices.size(),"Scene ceiling disagrees with world-space native roof formula");
            dok::SoftwareCamera native(dok::CameraParameters::kremlin_vga());native.set_pose(pose);
            unsigned projected=0;
            for(const auto& vertex:roof->vertices){const auto& v=*vertex.precise;if(v[2]<200)continue;
                // Heading90: camera lateral = world dz, depth = -world dx.
                const auto expected=native.project({int((v[0]-159.5)*(v[2]+200)/200),roof_y-pose.y,int(v[2])});
                check(std::abs((expected.y+0.5)-v[1])<2,"GPU/native roof vertical projection mismatch");++projected;
            }
            check(projected>0,"No roof projection anchors");
            if(comma)std::cout<<",";comma=true;
            std::cout<<"{\"dataset\":\""<<dok::dataset_name(level.dataset)<<"\",\"cell\":["<<cell[0]<<","<<cell[1]<<"],\"cell_height\":"<<level.cell_height<<",\"inset\":"<<inset<<",\"floor_y\":"<<base<<",\"roof_y\":"<<roof_y<<",\"eye_y\":"<<pose.y<<",\"clearance\":"<<base-roof_y<<",\"eye_to_roof\":"<<pose.y-roof_y<<",\"scene_vertices\":"<<seen<<",\"projection_anchors\":"<<projected<<"}";
        }
    }
    std::cout<<"]\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
