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
 X(EnableVertexAttribArray) X(VertexAttribPointer) X(GenTextures) X(BindTexture) X(TexParameteri) X(TexImage2D) \
 X(DeleteTextures) X(ActiveTexture) X(PixelStorei) X(DrawArrays) X(Viewport) X(ClearColor) X(Clear) \
 X(Enable) X(Disable) X(BlendFunc) X(ReadPixels)
struct GlRenderer::Impl {
    SDL_Window* window;SDL_GLContext context=nullptr;int width=1,height=1,virtual_width=320;
    GLuint program=0,vbo=0,palette=0,light=0,ui=0;
    bool light_uploaded=false;
    bool linear_filter=false;
    std::unordered_map<std::uint64_t,GLuint> textures;
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
    void rgba(GLuint id,int unit,int w,int h,const std::uint32_t* data){
        std::vector<std::uint8_t> bytes(std::size_t(w)*h*4);
        for(std::size_t i=0;i<bytes.size()/4;++i){const auto c=data[i];bytes[i*4]=c>>16;bytes[i*4+1]=c>>8;bytes[i*4+2]=c;bytes[i*4+3]=c>>24;}
        texture(id,unit,w,h,GL_RGBA,bytes.data());
    }
    void draw(const std::vector<float>& vertices,int mode){
        UseProgram(program);Uniform1i(GetUniformLocation(program,"mode"),mode);
        BindBuffer(GL_ARRAY_BUFFER,vbo);BufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STREAM_DRAW);
        for(GLuint a=0;a<3;++a)EnableVertexAttribArray(a);
        VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,7*sizeof(float),nullptr);
        VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,7*sizeof(float),reinterpret_cast<const void*>(4*sizeof(float)));
        VertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,7*sizeof(float),reinterpret_cast<const void*>(6*sizeof(float)));
        DrawArrays(GL_TRIANGLE_FAN,0,static_cast<GLsizei>(vertices.size()/7));
    }
    ~Impl(){if(context){SDL_GL_MakeCurrent(window,context);
        if(DeleteTextures){for(auto t:textures)DeleteTextures(1,&t.second);GLuint ids[]={palette,light,ui};DeleteTextures(3,ids);}
        if(DeleteBuffers&&vbo)DeleteBuffers(1,&vbo);if(DeleteProgram&&program)DeleteProgram(program);SDL_GL_DeleteContext(context);}}
};
GlRenderer::GlRenderer(SDL_Window* window):impl_(std::make_unique<Impl>()){
    auto& g=*impl_;g.window=window;g.context=SDL_GL_CreateContext(window);if(!g.context)throw std::runtime_error(SDL_GetError());
#define LOAD(name) g.name=reinterpret_cast<decltype(g.name)>(SDL_GL_GetProcAddress("gl" #name));if(!g.name)throw std::runtime_error("Missing gl" #name);
    GL_FUNCTIONS(LOAD)
#undef LOAD
    const auto vertex=g.shader(GL_VERTEX_SHADER,R"(
attribute vec4 position; attribute vec2 uv; attribute float depth;
VARYING vec2 texcoord; VARYING float shade;
void main(){gl_Position=position;texcoord=uv;shade=depth/4096.0;}
)");
    const auto fragment=g.shader(GL_FRAGMENT_SHADER,R"(
uniform sampler2D picture; uniform sampler2D colors; uniform sampler2D lighting;
uniform int mode; uniform int filtering; uniform vec2 pictureSize;
VARYING vec2 texcoord; VARYING float shade;
vec4 litColor(float index,float row){
 float mapped=floor(texture2D(lighting,vec2((index+0.5)/256.0,(row+0.5)/16.0)).r*255.0+0.5);
 return texture2D(colors,vec2((mapped+0.5)/256.0,0.5));
}
vec4 texelColor(vec2 uv,float row){
 float index=floor(texture2D(picture,uv).r*255.0+0.5);
 if(mode==1 && index<0.5)return vec4(0.0);
 float lower=floor(row);
 return mix(litColor(index,lower),litColor(index,min(lower+1.0,15.0)),fract(row));
}
void main(){
 if(mode==2){gl_FragColor=texture2D(picture,texcoord);return;}
 float row=clamp((shade*4096.0-1920.0)/120.0,0.0,15.0);
 // Interpolate actual palette colors, never palette indices. Keep native
 // LIGHT colors at each anchor, while removing discrete distance bands.
 vec4 color;
 if(filtering==0)color=texelColor(texcoord,row);
 else{
  vec2 p=texcoord*pictureSize-0.5,base=(floor(p)+0.5)/pictureSize,f=fract(p),step=1.0/pictureSize;
  color=mix(mix(texelColor(base,row),texelColor(base+vec2(step.x,0.0),row),f.x),
            mix(texelColor(base+vec2(0.0,step.y),row),texelColor(base+step,row),f.x),f.y);
  if(color.a>0.0)color.rgb/=color.a;
 }
 if(color.a<=0.0)discard;
 gl_FragColor=color;
}
)");
    g.program=g.CreateProgram();g.AttachShader(g.program,vertex);g.AttachShader(g.program,fragment);
    g.BindAttribLocation(g.program,0,"position");g.BindAttribLocation(g.program,1,"uv");g.BindAttribLocation(g.program,2,"depth");g.LinkProgram(g.program);
    g.DeleteShader(vertex);g.DeleteShader(fragment);GLint okay=0;g.GetProgramiv(g.program,GL_LINK_STATUS,&okay);
    if(!okay){char message[2048]{};g.GetProgramInfoLog(g.program,sizeof(message),nullptr,message);throw std::runtime_error(message);}
    g.UseProgram(g.program);g.Uniform1i(g.GetUniformLocation(g.program,"picture"),0);g.Uniform1i(g.GetUniformLocation(g.program,"colors"),1);g.Uniform1i(g.GetUniformLocation(g.program,"lighting"),2);
    g.GenBuffers(1,&g.vbo);g.GenTextures(1,&g.palette);g.GenTextures(1,&g.light);g.GenTextures(1,&g.ui);g.PixelStorei(GL_UNPACK_ALIGNMENT,1);
    SDL_GL_SetSwapInterval(1);
}
GlRenderer::~GlRenderer()=default;
void GlRenderer::begin(){auto& g=*impl_;SDL_GL_MakeCurrent(g.window,g.context);SDL_GL_GetDrawableSize(g.window,&g.width,&g.height);g.height=std::max(1,g.height);
    g.virtual_width=std::clamp(int(192.0*g.width/g.height+0.5),160,1200);
    g.Viewport(0,0,g.width,g.height);g.Disable(GL_DEPTH_TEST);g.Disable(GL_CULL_FACE);g.Disable(GL_BLEND);g.ClearColor(0,0,0,1);g.Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
int GlRenderer::view_width()const{return impl_->virtual_width;}
void GlRenderer::set_linear_filter(bool enabled){impl_->linear_filter=enabled;}
void GlRenderer::world(const std::array<std::uint32_t,256>& palette){auto& g=*impl_;g.rgba(g.palette,1,256,1,palette.data());g.Disable(GL_BLEND);g.Enable(GL_DEPTH_TEST);g.light_uploaded=false;}
void GlRenderer::polygon(const std::vector<RasterVertex>& vertices,const Image& image,Bytes light,RasterAxis axis){
    auto& g=*impl_;if(vertices.size()<3)return;
    std::uint64_t hash=1469598103934665603ULL;for(auto b:image.pixels)hash=(hash^b)*1099511628211ULL;
    hash^=(std::uint64_t(image.width)<<32)|image.height;
    auto found=g.textures.find(hash);GLuint id;
    if(found==g.textures.end()){g.GenTextures(1,&id);g.texture(id,0,image.width,image.height,GL_LUMINANCE,image.pixels.data());g.textures.emplace(hash,id);}
    else{id=found->second;g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,id);}
    if(!g.light_uploaded){g.texture(g.light,2,256,16,GL_LUMINANCE,light.data);g.light_uploaded=true;}
    g.Uniform1i(g.GetUniformLocation(g.program,"filtering"),g.linear_filter?1:0);
    g.Uniform2f(g.GetUniformLocation(g.program,"pictureSize"),float(image.width),float(image.height));
    if(g.linear_filter&&(image.flags&8)&&axis==RasterAxis::vertical){g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    else g.Disable(GL_BLEND);
    std::vector<float> data;data.reserve(vertices.size()*7);
    for(const auto& p:vertices){
        const auto v=p.precise.value_or(std::array<float,5>{float(p.x),float(p.y),float(p.depth),float(p.u),float(p.v)});
        const float w=std::max(1.0f,v[2]+200.0f);
        data.insert(data.end(),{(2.0f*(v[0]+0.5f)/g.virtual_width-1)*w,(1-2.0f*(v[1]+0.5f)/160)*w,w-2.0f,w,
            (v[3]+0.5f)/image.width,(v[4]+0.5f)/image.height,std::max(0.0f,v[2])});}
    g.draw(data,(axis==RasterAxis::vertical&&(image.flags&8))?1:0);
}
void GlRenderer::image(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect r,bool screen_pixels){
    auto& g=*impl_;g.rgba(g.ui,0,width,height,pixels.data());g.Disable(GL_DEPTH_TEST);g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const float rw=screen_pixels?float(g.width):320.f,rh=screen_pixels?float(g.height):240.f;
    const float l=2.f*r.x/rw-1,rr=2.f*(r.x+r.w)/rw-1,t=1-2.f*r.y/rh,b=1-2.f*(r.y+r.h)/rh;
    g.draw({l,t,0,1,0,0,0,rr,t,0,1,1,0,0,rr,b,0,1,1,1,0,l,b,0,1,0,1,0},2);
}
void GlRenderer::present(){SDL_GL_SwapWindow(impl_->window);}
void GlRenderer::screenshot(const std::string& path){auto& g=*impl_;std::vector<std::uint8_t> bytes(std::size_t(g.width)*g.height*4);
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
