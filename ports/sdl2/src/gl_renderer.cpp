#define GL_GLEXT_PROTOTYPES
#include <SDL_opengl.h>
#include "dok/gl_renderer.hpp"
#include "dok/png.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
namespace dok {
#define GL_FUNCTIONS(X) \
 X(CreateShader) X(ShaderSource) X(CompileShader) X(GetShaderiv) X(GetShaderInfoLog) X(DeleteShader) \
 X(CreateProgram) X(AttachShader) X(BindAttribLocation) X(LinkProgram) X(GetProgramiv) X(GetProgramInfoLog) X(DeleteProgram) \
 X(UseProgram) X(GetUniformLocation) X(Uniform1i) X(Uniform2f) X(GenBuffers) X(BindBuffer) X(BufferData) X(DeleteBuffers) \
 X(EnableVertexAttribArray) X(DisableVertexAttribArray) X(VertexAttribPointer) X(GenTextures) X(BindTexture) X(TexParameteri) X(TexImage2D) X(TexSubImage2D) \
 X(DeleteTextures) X(ActiveTexture) X(PixelStorei) X(DrawArrays) X(Viewport) X(ClearColor) X(Clear) \
 X(Enable) X(Disable) X(BlendFunc) X(ReadPixels)
struct GlRenderer::Impl {
    SDL_Window* window;SDL_GLContext context=nullptr;int width=1,height=1,virtual_width=320;
    GLuint program=0,vbo=0,light=0;
    struct UiTexture {GLuint id=0;std::vector<std::uint32_t> pixels;};
    std::unordered_map<std::uint64_t,UiTexture> ui_textures;
    std::array<std::uint32_t,256> palette_values{};
    std::vector<std::uint8_t> light_values;
    std::vector<std::uint8_t> light_rgba;
    bool palette_valid=false;
    bool light_uploaded=false;
    bool linear_filter=false;
    std::unordered_map<std::uint64_t,GLuint> textures;
    // Validate bytes, not pointer identity: callers may mutate or recycle Image.
    struct ImageHash {unsigned width=0,height=0;std::vector<std::uint8_t> pixels;std::uint64_t hash=0,epoch=0;};
    std::unordered_map<const void*,ImageHash> image_hashes;
    std::uint64_t image_epoch=0;
    bool immutable_images=false;
    GLint mode_location=-1,filter_location=-1,size_location=-1,batch_location=-1;
    std::vector<std::uint8_t> rgba_bytes;
    std::vector<float> polygon_data;
    struct Command {GLuint texture;GLint first;GLsizei count;unsigned width,height;int mode;bool linear,blend;};
    bool deferred=false;
    std::vector<float> world_vertices;
    std::vector<Command> commands;
    struct Batch {GLint first=0;GLsizei count=0;std::array<GLuint,7> textures{};unsigned used=0;bool linear=false,blend=false;};
    std::vector<Batch> batches;
    std::vector<float> triangle_vertices;
#define DECLARE(name) decltype(&gl##name) name=nullptr;
    GL_FUNCTIONS(DECLARE)
#undef DECLARE
    GLuint shader(GLenum kind,const char* body){
        int profile=0;SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,&profile);
        const char* prefix=profile==SDL_GL_CONTEXT_PROFILE_ES?
            (kind==GL_VERTEX_SHADER?"#version 100\nprecision highp float;\n#define VARYING varying mediump\n":"#version 100\nprecision mediump float;\n#define VARYING varying mediump\n"):
            "#version 110\n#define VARYING varying\n";GLuint id=CreateShader(kind);
        const char* sources[]={prefix,body};ShaderSource(id,2,sources,nullptr);CompileShader(id);
        GLint okay=0;GetShaderiv(id,GL_COMPILE_STATUS,&okay);
        if(!okay){char message[2048]{};GetShaderInfoLog(id,sizeof(message),nullptr,message);DeleteShader(id);throw std::runtime_error(message);}return id;
    }
    void texture(GLuint id,int unit,int w,int h,GLenum format,const void* data){
        ActiveTexture(GL_TEXTURE0+unit);BindTexture(GL_TEXTURE_2D,id);
        TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        TexImage2D(GL_TEXTURE_2D,0,format,w,h,0,format,GL_UNSIGNED_BYTE,data);
    }
    void rgba(GLuint id,int unit,int w,int h,const std::uint32_t* data,bool update=false){
        auto& bytes=rgba_bytes;bytes.resize(std::size_t(w)*h*4);
        for(std::size_t i=0;i<bytes.size()/4;++i){const auto c=data[i];bytes[i*4]=c>>16;bytes[i*4+1]=c>>8;bytes[i*4+2]=c;bytes[i*4+3]=c>>24;}
        if(update){ActiveTexture(GL_TEXTURE0+unit);BindTexture(GL_TEXTURE_2D,id);TexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());}
        else texture(id,unit,w,h,GL_RGBA,bytes.data());
    }
    void upload(const std::vector<float>& vertices,bool materials=false){
        BindBuffer(GL_ARRAY_BUFFER,vbo);BufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STREAM_DRAW);
        for(GLuint a=0;a<3;++a)EnableVertexAttribArray(a);
        const auto stride=(materials?11:7)*sizeof(float);
        VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,stride,nullptr);
        VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<const void*>(4*sizeof(float)));
        VertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<const void*>(6*sizeof(float)));
        if(materials){EnableVertexAttribArray(3);VertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<const void*>(7*sizeof(float)));}
        else DisableVertexAttribArray(3);
    }
    void draw(const std::vector<float>& vertices,int mode){
        UseProgram(program);Uniform1i(mode_location,mode);Uniform1i(batch_location,0);upload(vertices);
        DrawArrays(GL_TRIANGLE_FAN,0,static_cast<GLsizei>(vertices.size()/7));
    }
    ~Impl(){if(context){SDL_GL_MakeCurrent(window,context);
        if(DeleteTextures){for(auto t:textures)DeleteTextures(1,&t.second);for(const auto& t:ui_textures)DeleteTextures(1,&t.second.id);DeleteTextures(1,&light);}
        if(DeleteBuffers&&vbo)DeleteBuffers(1,&vbo);if(DeleteProgram&&program)DeleteProgram(program);SDL_GL_DeleteContext(context);}}
};
GlRenderer::GlRenderer(SDL_Window* window):impl_(std::make_unique<Impl>()){
    auto& g=*impl_;g.window=window;g.context=SDL_GL_CreateContext(window);if(!g.context)throw std::runtime_error(SDL_GetError());
#define LOAD(name) g.name=reinterpret_cast<decltype(g.name)>(SDL_GL_GetProcAddress("gl" #name));if(!g.name)throw std::runtime_error("Missing gl" #name);
    GL_FUNCTIONS(LOAD)
#undef LOAD
    const auto vertex=g.shader(GL_VERTEX_SHADER,R"(
attribute vec4 position; attribute vec2 uv; attribute float depth; attribute vec4 material;
VARYING vec2 texcoord; VARYING float shade; VARYING vec4 mat;
void main(){gl_Position=position;texcoord=uv;shade=depth/4096.0;mat=material;}
)");
    const auto fragment=g.shader(GL_FRAGMENT_SHADER,R"(
uniform sampler2D picture; uniform sampler2D lighting;
uniform sampler2D picture1; uniform sampler2D picture2; uniform sampler2D picture3;
uniform sampler2D picture4; uniform sampler2D picture5; uniform sampler2D picture6;
uniform int mode; uniform int filtering; uniform int batched; uniform vec2 pictureSize;
VARYING vec2 texcoord; VARYING float shade; VARYING vec4 mat;
vec4 samplePicture(vec2 uv){
 float slot=batched==1?mat.z:0.0;
 if(slot<0.5)return texture2D(picture,uv);
 if(slot<1.5)return texture2D(picture1,uv);
 if(slot<2.5)return texture2D(picture2,uv);
 if(slot<3.5)return texture2D(picture3,uv);
 if(slot<4.5)return texture2D(picture4,uv);
 if(slot<5.5)return texture2D(picture5,uv);
 return texture2D(picture6,uv);
}
vec4 litColor(float index,float row){
 return texture2D(lighting,vec2((index+0.5)/256.0,(row+0.5)/16.0));
}
vec4 texelColor(vec2 uv,float row){
 float index=floor(samplePicture(uv).r*255.0+0.5);
 if((batched==1?mat.w:float(mode))>0.5 && index<0.5)return vec4(0.0);
 float lower=floor(row);
 return mix(litColor(index,lower),litColor(index,min(lower+1.0,15.0)),fract(row));
}
void main(){
 if(mode==2){vec4 color=texture2D(picture,texcoord);if(color.a==0.0)discard;gl_FragColor=color;return;}
 float row=clamp((shade*4096.0-1920.0)/120.0,0.0,15.0);
 // Interpolate actual palette colors, never palette indices. Keep native
 // LIGHT colors at each anchor, while removing discrete distance bands.
 vec4 color;
 if(filtering==0)color=texelColor(texcoord,row);
 else{
  vec2 size=batched==1?floor(mat.xy+0.5):pictureSize;
  vec2 p=texcoord*size-0.5,base=(floor(p)+0.5)/size,f=fract(p),step=1.0/size;
  color=mix(mix(texelColor(base,row),texelColor(base+vec2(step.x,0.0),row),f.x),
            mix(texelColor(base+vec2(0.0,step.y),row),texelColor(base+step,row),f.x),f.y);
  if(color.a>0.0)color.rgb/=color.a;
 }
 if(color.a<=0.0)discard;
 gl_FragColor=color;
}
)");
    g.program=g.CreateProgram();g.AttachShader(g.program,vertex);g.AttachShader(g.program,fragment);
    g.BindAttribLocation(g.program,0,"position");g.BindAttribLocation(g.program,1,"uv");g.BindAttribLocation(g.program,2,"depth");g.BindAttribLocation(g.program,3,"material");g.LinkProgram(g.program);
    g.DeleteShader(vertex);g.DeleteShader(fragment);GLint okay=0;g.GetProgramiv(g.program,GL_LINK_STATUS,&okay);
    if(!okay){char message[2048]{};g.GetProgramInfoLog(g.program,sizeof(message),nullptr,message);throw std::runtime_error(message);}
    g.UseProgram(g.program);g.Uniform1i(g.GetUniformLocation(g.program,"picture"),0);g.Uniform1i(g.GetUniformLocation(g.program,"lighting"),7);
    for(int i=1;i<7;++i)g.Uniform1i(g.GetUniformLocation(g.program,("picture"+std::to_string(i)).c_str()),i);
    g.mode_location=g.GetUniformLocation(g.program,"mode");
    g.filter_location=g.GetUniformLocation(g.program,"filtering");
    g.size_location=g.GetUniformLocation(g.program,"pictureSize");
    g.batch_location=g.GetUniformLocation(g.program,"batched");
    g.GenBuffers(1,&g.vbo);g.GenTextures(1,&g.light);g.PixelStorei(GL_UNPACK_ALIGNMENT,1);
    SDL_GL_SetSwapInterval(1);
}
GlRenderer::~GlRenderer()=default;
void GlRenderer::begin(){auto& g=*impl_;SDL_GL_MakeCurrent(g.window,g.context);SDL_GL_GetDrawableSize(g.window,&g.width,&g.height);g.height=std::max(1,g.height);
    g.commands.clear();g.world_vertices.clear();g.deferred=false;g.immutable_images=false;
    if(g.ui_textures.size()>16){for(const auto& t:g.ui_textures)g.DeleteTextures(1,&t.second.id);g.ui_textures.clear();}
    // Bound CPU snapshots when a persistent Display visits many levels.
    if(g.image_hashes.size()>1024)g.image_hashes.clear();
    g.virtual_width=std::clamp(int(192.0*g.width/g.height+0.5),160,1200);
    g.Viewport(0,0,g.width,g.height);g.Disable(GL_DEPTH_TEST);g.Disable(GL_CULL_FACE);g.Disable(GL_BLEND);g.ClearColor(0,0,0,1);g.Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
int GlRenderer::view_width()const{return impl_->virtual_width;}
void GlRenderer::set_linear_filter(bool enabled){impl_->linear_filter=enabled;}
void GlRenderer::world(const std::array<std::uint32_t,256>& palette,bool defer,bool immutable_images){
    flush_world();auto& g=*impl_;
    if(++g.image_epoch==0){g.image_hashes.clear();++g.image_epoch;}
    g.immutable_images=immutable_images;
    if(!g.palette_valid||g.palette_values!=palette){g.palette_values=palette;g.palette_valid=true;g.light_values.clear();}
    g.Disable(GL_BLEND);g.Enable(GL_DEPTH_TEST);g.light_uploaded=false;g.deferred=defer;
}
void GlRenderer::flush_world(){
    auto& g=*impl_;g.deferred=false;g.immutable_images=false;if(g.commands.empty())return;
    g.batches.clear();g.triangle_vertices.clear();
    for(const auto& c:g.commands){
        auto* batch=g.batches.empty()?nullptr:&g.batches.back();
        unsigned slot=0;
        if(batch)while(slot<batch->used&&batch->textures[slot]!=c.texture)++slot;
        if(!batch||batch->linear!=c.linear||batch->blend!=c.blend||slot==7){
            g.batches.push_back({static_cast<GLint>(g.triangle_vertices.size()/11),0,{},0,c.linear,c.blend});
            batch=&g.batches.back();slot=0;
        }
        if(slot==batch->used)batch->textures[batch->used++]=c.texture;
        // Same fan triangles and order; no material sort or atlas UV remapping.
        for(int i=1;i+1<c.count;++i)for(int index:{0,i,i+1}){
            const auto first=g.world_vertices.begin()+std::size_t(c.first+index)*7;
            g.triangle_vertices.insert(g.triangle_vertices.end(),first,first+7);
            g.triangle_vertices.insert(g.triangle_vertices.end(),{float(c.width),float(c.height),float(slot),float(c.mode)});
            ++batch->count;
        }
    }
    g.UseProgram(g.program);g.Uniform1i(g.mode_location,0);g.Uniform1i(g.batch_location,1);g.upload(g.triangle_vertices,true);
    for(const auto& b:g.batches){
        for(unsigned slot=0;slot<b.used;++slot){g.ActiveTexture(GL_TEXTURE0+slot);g.BindTexture(GL_TEXTURE_2D,b.textures[slot]);}
        g.Uniform1i(g.filter_location,b.linear?1:0);
        if(b.blend){g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}else g.Disable(GL_BLEND);
        g.DrawArrays(GL_TRIANGLES,b.first,b.count);
    }
    g.commands.clear();g.world_vertices.clear();
}
void GlRenderer::polygon(const std::vector<RasterVertex>& vertices,const Image& image,Bytes light,RasterAxis axis){
    auto& g=*impl_;if(vertices.size()<3)return;
    // All clip W are positive (max(1, depth+200)). A polygon whose vertices
    // lie strictly outside the same XY clip plane cannot produce a fragment.
    // Evaluate exactly the float expressions uploaded below, including subpixel poses.
    bool left=true,right=true,above=true,below=true;
    for(const auto& p:vertices){
        const auto v=p.precise.value_or(std::array<float,5>{float(p.x),float(p.y),float(p.depth),float(p.u),float(p.v)});
        const float w=std::max(1.0f,v[2]+200.0f);
        const float x=(2.0f*(v[0]+0.5f)/g.virtual_width-1)*w,y=(1-2.0f*(v[1]+0.5f)/160)*w;
        left&=x<-w;right&=x>w;above&=y>w;below&=y<-w;
    }
    // Keep the first-polygon LIGHT contract even if that polygon is offscreen.
    if(!g.light_uploaded){
        if(g.light_values.size()!=light.size||!std::equal(g.light_values.begin(),g.light_values.end(),light.data)){
            // Exact composition of the two nearest 8-bit lookup tables. Keep
            // interpolation/alpha in the shader in precisely the same order.
            g.light_rgba.resize(light.size*4);
            for(std::size_t i=0;i<light.size;++i){
                const auto c=g.palette_values[light.data[i]];
                g.light_rgba[i*4]=c>>16;g.light_rgba[i*4+1]=c>>8;g.light_rgba[i*4+2]=c;g.light_rgba[i*4+3]=c>>24;
            }
            g.texture(g.light,7,256,16,GL_RGBA,g.light_rgba.data());g.light_values.assign(light.data,light.data+light.size);
        }else{g.ActiveTexture(GL_TEXTURE7);g.BindTexture(GL_TEXTURE_2D,g.light);}
        g.light_uploaded=true;
    }
    if(left||right||above||below)return;
    auto& cached=g.image_hashes[image.pixels.data()];
    if(cached.width!=image.width||cached.height!=image.height||
       ((!g.immutable_images||cached.epoch!=g.image_epoch)&&(cached.pixels.size()!=image.pixels.size()||
        std::memcmp(cached.pixels.data(),image.pixels.data(),image.pixels.size())!=0))){
        std::uint64_t hash=1469598103934665603ULL;for(auto b:image.pixels)hash=(hash^b)*1099511628211ULL;
        cached.hash=hash^((std::uint64_t(image.width)<<32)|image.height);
        cached.width=image.width;cached.height=image.height;cached.pixels=image.pixels;
    }
    cached.epoch=g.image_epoch;
    const auto hash=cached.hash;
    auto found=g.textures.find(hash);GLuint id;
    if(found==g.textures.end()){g.GenTextures(1,&id);g.texture(id,0,image.width,image.height,GL_LUMINANCE,image.pixels.data());g.textures.emplace(hash,id);}
    else{id=found->second;if(!g.deferred){g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,id);}}
    const bool blend=g.linear_filter&&(image.flags&8)&&axis==RasterAxis::vertical;
    auto& data=g.polygon_data;data.clear();data.reserve(vertices.size()*7);
    for(const auto& p:vertices){
        const auto v=p.precise.value_or(std::array<float,5>{float(p.x),float(p.y),float(p.depth),float(p.u),float(p.v)});
        const float w=std::max(1.0f,v[2]+200.0f);
        data.insert(data.end(),{(2.0f*(v[0]+0.5f)/g.virtual_width-1)*w,(1-2.0f*(v[1]+0.5f)/160)*w,w-2.0f,w,
            (v[3]+0.5f)/image.width,(v[4]+0.5f)/image.height,std::max(0.0f,v[2])});}
    const int mode=(axis==RasterAxis::vertical&&(image.flags&8))?1:0;
    if(g.deferred){
        g.commands.push_back({id,static_cast<GLint>(g.world_vertices.size()/7),static_cast<GLsizei>(data.size()/7),image.width,image.height,mode,g.linear_filter,blend});
        g.world_vertices.insert(g.world_vertices.end(),data.begin(),data.end());
    }else{
        g.Uniform1i(g.filter_location,g.linear_filter?1:0);
        g.Uniform2f(g.size_location,float(image.width),float(image.height));
        if(blend){g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}else g.Disable(GL_BLEND);
        g.draw(data,mode);
    }
}
void GlRenderer::image(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect r,bool screen_pixels){
    flush_world();
    auto& g=*impl_;
    const auto key=(std::uint64_t(width)<<32)|unsigned(height);
    auto& texture=g.ui_textures[key];const auto count=std::size_t(width)*height;
    if(!texture.id){g.GenTextures(1,&texture.id);g.rgba(texture.id,0,width,height,pixels.data());texture.pixels.assign(pixels.begin(),pixels.begin()+count);}
    else if(std::memcmp(texture.pixels.data(),pixels.data(),count*sizeof(std::uint32_t))!=0){
        g.rgba(texture.id,0,width,height,pixels.data(),true);std::copy_n(pixels.data(),count,texture.pixels.data());
    }else{g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,texture.id);}
    g.Disable(GL_DEPTH_TEST);g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const float rw=screen_pixels?float(g.width):320.f,rh=screen_pixels?float(g.height):240.f;
    const float l=2.f*r.x/rw-1,rr=2.f*(r.x+r.w)/rw-1,t=1-2.f*r.y/rh,b=1-2.f*(r.y+r.h)/rh;
    g.draw({l,t,0,1,0,0,0,rr,t,0,1,1,0,0,rr,b,0,1,1,1,0,l,b,0,1,0,1,0},2);
}
void GlRenderer::present(){flush_world();SDL_GL_SwapWindow(impl_->window);}
void GlRenderer::screenshot(const std::string& path){flush_world();auto& g=*impl_;std::vector<std::uint8_t> bytes(std::size_t(g.width)*g.height*4);
    g.ReadPixels(0,0,g.width,g.height,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());
    if(std::filesystem::u8path(path).extension()==".png"){
        std::vector<std::uint32_t> pixels(std::size_t(g.width)*g.height);
        for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x){const auto* p=&bytes[(std::size_t(g.height-1-y)*g.width+x)*4];
            pixels[std::size_t(y)*g.width+x]=0xff000000u|(p[0]<<16)|(p[1]<<8)|p[2];}
        save_png(std::filesystem::u8path(path),g.width,g.height,pixels);return;
    }
    SDL_Surface* s=SDL_CreateRGBSurfaceWithFormat(0,g.width,g.height,32,SDL_PIXELFORMAT_ARGB8888);if(!s)throw std::runtime_error(SDL_GetError());
    for(int y=0;y<g.height;++y){auto* dst=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+y*s->pitch);
        for(int x=0;x<g.width;++x){const auto* p=&bytes[(std::size_t(g.height-1-y)*g.width+x)*4];dst[x]=0xff000000u|(p[0]<<16)|(p[1]<<8)|p[2];}}
    const int result=SDL_SaveBMP(s,path.c_str());SDL_FreeSurface(s);if(result)throw std::runtime_error(SDL_GetError());
}
}
