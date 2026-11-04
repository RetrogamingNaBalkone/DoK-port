#include "dok/animations.hpp"
#include "dok/gl_renderer.hpp"
#include <SDL_opengl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
 SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
 auto* window=SDL_CreateWindow("Animation scene regression",0,0,640,384,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(window,"GL window");
 unsigned levels=0,clips=0,changes=0;
 {dok::GlRenderer gl(window);auto read=reinterpret_cast<decltype(&glReadPixels)>(SDL_GL_GetProcAddress("glReadPixels"));
 for(int arg=1;arg<argc;++arg){auto archive=dok::Archive::open(argv[arg]);
 for(auto name:{"LEVEL1","LEVEL2","LEVEL3","LEVEL4"})if(archive.find(name)){
  dok::World world(dok::load_level(archive,name));dok::Animations animations(world,archive);dok::SoftwareScene scene(archive,world.definition);
  auto getter=[&](auto owner){return animations.image(owner);};const auto pose=*world.objects.at(*world.player).pose;
  for(auto mode:{std::pair<int,int>{320,200},{640,400},{640,480},{800,600},{1024,768}}){scene.software_resolution(mode.first,mode.second);const auto before=world.simulation_ticks();scene.render(world,pose,{}, {},320,{},getter);check(before==world.simulation_ticks(),"Render advanced clock");}
  scene.software_resolution(320,200);
  for(auto& o:world.objects)if(o.animation){++clips;const auto& d=world.definition.objects[o.definition_index];const auto frames=archive.animation(d.animation);
   auto different=std::find_if(frames.begin()+1,frames.end(),[&](const auto& f){return f.image.pixels!=frames[0].image.pixels;});
   check(different!=frames.end(),"Expected changing original clip");
   // Controlled room uses original definitions/resources, not a claimed DOS/full-map frame.
   auto fixture=world.definition;dok::World room(fixture);dok::Animations playback(room,archive);dok::SoftwareScene cached(archive,fixture);
   std::fill(room.maps.at("tmap").begin(),room.maps.at("tmap").end(),' ');std::fill(room.maps.at("mmap").begin(),room.maps.at("mmap").end(),' ');
   auto floor=room.maps.at("fcmap")[0];for(auto g:room.maps.at("fcmap"))if(fixture.object(g)->class_id==0){floor=g;break;}
   std::fill(room.maps.at("fcmap").begin(),room.maps.at("fcmap").end(),floor);
   const int x=fixture.width/2,z=fixture.length/2,side=fixture.cell_side;room.maps.at("tmap")[z*fixture.width+x]=d.glyph;
   dok::Pose view{x*side+side/2,-side/2,(z-2)*side+side/2,0};
   auto image_get=[&](auto owner){return playback.image(owner);};auto& state=*room.object_for_owner(o.owner).animation;
   auto sw=[&]{return cached.render(room,view,{}, {},320,{},image_get).pixels;};
   auto gpu=[&]{gl.begin();gl.world(cached.palette());cached.render(room,view,{},[&](const auto& v,const auto& im,auto light,auto axis){gl.polygon(v,im,light,axis);},320,{},image_get);gl.flush_world();std::vector<unsigned char> out(640*384*4);read(0,0,640,384,GL_RGBA,GL_UNSIGNED_BYTE,out.data());return out;};
   std::cout<<"Checking "<<dok::dataset_name(fixture.dataset)<<' '<<name<<" glyph "<<unsigned(d.glyph)<<std::endl;
   const auto sw0=sw(),gl0=gpu();state.frame=static_cast<std::int16_t>(different-frames.begin());
   const auto sw1=sw(),gl1=gpu();check(sw1!=sw0,"Software base animation stayed static");check(gl1!=gl0,"GL base animation stayed static");++changes;
   check(sw()==sw1&&gpu()==gl1,"Repeated redraw changes animation");
   dok::SoftwareScene fresh(archive,fixture);check(fresh.render(room,view,{}, {},320,{},image_get).pixels==sw1,"Cached scene differs from fresh");
   state.frame=0;check(sw()==sw0&&gpu()==gl0,"Animation cache failed phase reversal");
   check(state.remaining==0&&room.simulation_ticks()==0,"Rendering activated simulation");
   // Explicit immutable replacement with changed dimensions/properties catches
   // caches that refresh texels while retaining the first frame's geometry.
   auto resized=frames[0].image;const auto old_width=resized.width;resized.width=std::max(1,int(resized.width)/2);resized.flags^=8;
   std::vector<std::uint8_t> texels;for(unsigned y=0;y<resized.height;++y)texels.insert(texels.end(),resized.pixels.begin()+y*old_width,resized.pixels.begin()+y*old_width+resized.width);resized.pixels=std::move(texels);
   auto resized_get=[&](auto owner)->const dok::Image*{return owner==o.owner?&resized:playback.image(owner);};
   const auto resized_frame=cached.render(room,view,{}, {},320,{},resized_get).pixels;
   dok::SoftwareScene resized_fresh(archive,fixture);check(resized_fresh.render(room,view,{}, {},320,{},resized_get).pixels==resized_frame,"Dimension/property replacement kept stale geometry");
   auto resized_gpu=[&](dok::SoftwareScene& target){gl.begin();gl.world(target.palette());target.render(room,view,{},[&](const auto& v,const auto& im,auto light,auto axis){gl.polygon(v,im,light,axis);},320,{},resized_get);gl.flush_world();std::vector<unsigned char> out(640*384*4);read(0,0,640,384,GL_RGBA,GL_UNSIGNED_BYTE,out.data());return out;};
   check(resized_gpu(cached)==resized_gpu(resized_fresh),"GL dimension/property replacement kept stale cache");
   check(sw()==sw0&&gpu()==gl0,"Replacement frame cache did not restore original");
   std::cout<<dok::dataset_name(fixture.dataset)<<' '<<name<<" glyph "<<unsigned(d.glyph)<<" class "<<d.class_id<<": software/GL changing frame PASS\n";
  }++levels;
 }}}
 SDL_DestroyWindow(window);SDL_Quit();check(levels==7&&clips==20&&changes==clips,"Incomplete scene matrix");std::cout<<levels<<" levels x5 modes, "<<clips<<" changing clips software/GL PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
