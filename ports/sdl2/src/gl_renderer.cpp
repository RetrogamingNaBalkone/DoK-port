#include "dok/gl_api.hpp"
#include "dok/gl_renderer.hpp"
#include "dok/png.hpp"
#include "dok/presentation.hpp"
#include "dok/filters.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
namespace dok {
#include "xbr_shader.inc"
#define GL_FUNCTIONS(X) \
 X(GetString) X(GetIntegerv) X(CreateShader) X(ShaderSource) X(CompileShader) X(GetShaderiv) X(GetShaderInfoLog) X(DeleteShader) \
 X(CreateProgram) X(AttachShader) X(BindAttribLocation) X(LinkProgram) X(GetProgramiv) X(GetProgramInfoLog) X(DeleteProgram) \
 X(UseProgram) X(GetUniformLocation) X(Uniform1i) X(Uniform2f) X(GenBuffers) X(BindBuffer) X(BufferData) X(DeleteBuffers) \
 X(EnableVertexAttribArray) X(DisableVertexAttribArray) X(VertexAttribPointer) X(GenTextures) X(BindTexture) X(TexParameteri) X(TexImage2D) X(TexSubImage2D) \
 X(DeleteTextures) X(ActiveTexture) X(PixelStorei) X(DrawArrays) X(Viewport) X(ClearColor) X(Clear) \
 X(Enable) X(Disable) X(BlendFunc) X(ReadPixels) X(CopyTexSubImage2D)
#define FRAME_FUNCTIONS(X) \
 X(GenFramebuffers) X(BindFramebuffer) X(FramebufferTexture2D) X(CheckFramebufferStatus) X(DeleteFramebuffers) \
 X(GenRenderbuffers) X(BindRenderbuffer) X(RenderbufferStorage) X(FramebufferRenderbuffer) X(DeleteRenderbuffers)
struct GlRenderer::Impl {
    SDL_Window* window;SDL_GLContext context=nullptr;int width=1,height=1,virtual_width=320,virtual_height=160;
    int frame_width=0,frame_height=0;GLuint framebuffer=0,frame_texture=0,frame_depth=0;bool frame_finished=false;
    SDL_Rect scene_rect{};GLuint post_texture=0;int post_width=0,post_height=0;
    GLuint program=0,vbo=0,light=0;
    GLuint xbr_program=0,xbr_framebuffer=0,xbr_texture=0;
    int xbr_width=0,xbr_height=0;bool xbr_failed=false;
    GLint xbr_size=-1;
    struct UiTexture {GLuint id=0;std::vector<std::uint32_t> pixels;};
    std::unordered_map<std::uint64_t,UiTexture> ui_textures;
    std::array<std::uint32_t,256> palette_values{};
    std::vector<std::uint8_t> light_values;
    std::vector<std::uint8_t> light_rgba;
    bool palette_valid=false;
    bool light_uploaded=false;
    int texture_filter=0;bool derivatives=true;
    struct MipTexture {GLuint id;std::uint64_t epoch;std::size_t bytes;};
    std::unordered_map<std::uint64_t,MipTexture> mip_textures;
    std::size_t mip_bytes=0;
    std::unordered_map<std::uint64_t,GLuint> textures;
    // Validate bytes, not pointer identity: callers may mutate or recycle Image.
    struct ImageHash {unsigned width=0,height=0;std::vector<std::uint8_t> pixels;std::uint64_t hash=0,epoch=0;};
    std::unordered_map<const void*,ImageHash> image_hashes;
    std::uint64_t image_epoch=0;
    bool immutable_images=false;
    GLint mode_location=-1,filter_location=-1,size_location=-1,batch_location=-1,output_scale_location=-1;
    std::vector<std::uint8_t> rgba_bytes;
    std::vector<float> polygon_data;
    struct Command {GLuint texture;GLint first;GLsizei count;unsigned width,height;int mode;int filter;bool blend;};
    bool deferred=false;
    std::vector<float> world_vertices;
    std::vector<Command> commands;
    struct Batch {GLint first=0;GLsizei count=0;std::array<GLuint,7> textures{};unsigned used=0;int filter=0;bool blend=false;};
    std::vector<Batch> batches;
    std::vector<float> triangle_vertices;
#define DECLARE(name) decltype(&gl##name) name=nullptr;
    GL_FUNCTIONS(DECLARE)
    FRAME_FUNCTIONS(DECLARE)
#undef DECLARE
    GLuint shader(GLenum kind,const char* body){
        int profile=0;SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,&profile);
        const char* prefix=profile==SDL_GL_CONTEXT_PROFILE_ES?
            (kind==GL_VERTEX_SHADER?"#version 100\nprecision highp float;\n#define VARYING varying mediump\n":"#version 100\nprecision mediump float;\n#define VARYING varying mediump\n"):
            "#version 110\n#define VARYING varying\n";GLuint id=CreateShader(kind);
        const char* extension=profile==SDL_GL_CONTEXT_PROFILE_ES&&kind==GL_FRAGMENT_SHADER?(derivatives?"#extension GL_OES_standard_derivatives : enable\n":"#define NO_MIP_DERIVATIVES\n"):"";
        // Extensions must precede precision declarations in GLES.
        std::string header=prefix;if(profile==SDL_GL_CONTEXT_PROFILE_ES&&kind==GL_FRAGMENT_SHADER)header=std::string("#version 100\n")+extension+"#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n#define VARYING varying mediump\n";
        const char* sources[]={header.c_str(),body};ShaderSource(id,2,sources,nullptr);CompileShader(id);
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
    bool xbr(GLuint source,int w,int h){
        if(xbr_failed||SDL_GetHintBoolean("DOK_DISABLE_GPU_XBR",SDL_FALSE)||!GenFramebuffers||!BindFramebuffer||!FramebufferTexture2D||!CheckFramebufferStatus||!DeleteFramebuffers)return false;
        GLint limit=0;GetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);if(w>limit/3||h>limit/3)return false;
        if(!xbr_program){
            GLuint vertex=0,fragment=0;
            try{
                vertex=shader(GL_VERTEX_SHADER,"attribute vec4 position;attribute vec2 uv;\n#ifdef GL_ES\nvarying highp vec2 texcoord;\n#else\nvarying vec2 texcoord;\n#endif\nvoid main(){gl_Position=position;texcoord=uv;}");
                fragment=shader(GL_FRAGMENT_SHADER,xbr_fragment);
                xbr_program=CreateProgram();AttachShader(xbr_program,vertex);AttachShader(xbr_program,fragment);
                BindAttribLocation(xbr_program,0,"position");BindAttribLocation(xbr_program,1,"uv");LinkProgram(xbr_program);
                GLint okay=0;GetProgramiv(xbr_program,GL_LINK_STATUS,&okay);if(!okay)throw std::runtime_error("xBR shader link failed");
                UseProgram(xbr_program);Uniform1i(GetUniformLocation(xbr_program,"picture"),0);xbr_size=GetUniformLocation(xbr_program,"pictureSize");
            }catch(...){if(vertex)DeleteShader(vertex);if(fragment)DeleteShader(fragment);if(xbr_program)DeleteProgram(xbr_program);xbr_program=0;xbr_failed=true;return false;}
            DeleteShader(vertex);DeleteShader(fragment);
        }
        GLint previous=0;GetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);
        if(!xbr_framebuffer)GenFramebuffers(1,&xbr_framebuffer);
        BindFramebuffer(GL_FRAMEBUFFER,xbr_framebuffer);
        if(xbr_width!=w||xbr_height!=h){
            if(!xbr_texture)GenTextures(1,&xbr_texture);texture(xbr_texture,0,w*3,h*3,GL_RGBA,nullptr);
            FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,xbr_texture,0);
            if(CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){BindFramebuffer(GL_FRAMEBUFFER,previous);xbr_failed=true;return false;}
            xbr_width=w;xbr_height=h;
        }
        Viewport(0,0,w*3,h*3);Disable(GL_DEPTH_TEST);Disable(GL_BLEND);
        UseProgram(xbr_program);Uniform2f(xbr_size,float(w),float(h));ActiveTexture(GL_TEXTURE0);BindTexture(GL_TEXTURE_2D,source);
        TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        upload({-1,-1,0,1,0,0,0,1,-1,0,1,1,0,0,1,1,0,1,1,1,0,-1,1,0,1,0,1,0});DrawArrays(GL_TRIANGLE_FAN,0,4);
        BindFramebuffer(GL_FRAMEBUFFER,previous);return true;
    }
    ~Impl(){if(context){SDL_GL_MakeCurrent(window,context);
        if(DeleteTextures){for(auto t:mip_textures)DeleteTextures(1,&t.second.id);for(auto t:textures)DeleteTextures(1,&t.second);for(const auto& t:ui_textures)DeleteTextures(1,&t.second.id);DeleteTextures(1,&light);if(post_texture)DeleteTextures(1,&post_texture);}
        if(DeleteFramebuffers&&framebuffer)DeleteFramebuffers(1,&framebuffer);
        if(DeleteFramebuffers&&xbr_framebuffer)DeleteFramebuffers(1,&xbr_framebuffer);
        if(DeleteTextures&&xbr_texture)DeleteTextures(1,&xbr_texture);
        if(DeleteProgram&&xbr_program)DeleteProgram(xbr_program);
        if(DeleteRenderbuffers&&frame_depth)DeleteRenderbuffers(1,&frame_depth);
        if(DeleteTextures&&frame_texture)DeleteTextures(1,&frame_texture);
        if(DeleteBuffers&&vbo)DeleteBuffers(1,&vbo);if(DeleteProgram&&program)DeleteProgram(program);SDL_GL_DeleteContext(context);}}
};
GlRenderer::GlRenderer(SDL_Window* window):impl_(std::make_unique<Impl>()){
    auto& g=*impl_;g.window=window;g.context=SDL_GL_CreateContext(window);if(!g.context)throw std::runtime_error(SDL_GetError());
#define LOAD(name) g.name=reinterpret_cast<decltype(g.name)>(SDL_GL_GetProcAddress("gl" #name));if(!g.name)throw std::runtime_error("Missing gl" #name);
    GL_FUNCTIONS(LOAD)
#undef LOAD
#define LOAD_FRAME(name) g.name=reinterpret_cast<decltype(g.name)>(SDL_GL_GetProcAddress("gl" #name));if(!g.name)g.name=reinterpret_cast<decltype(g.name)>(SDL_GL_GetProcAddress("gl" #name "EXT"));
    FRAME_FUNCTIONS(LOAD_FRAME)
#undef LOAD_FRAME
    int profile=0;SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,&profile);g.derivatives=(profile!=SDL_GL_CONTEXT_PROFILE_ES||SDL_GL_ExtensionSupported("GL_OES_standard_derivatives"))&&!SDL_GetHintBoolean("DOK_DISABLE_GL_DERIVATIVES",SDL_FALSE);
#ifdef DOK_GLES2
    if(profile!=SDL_GL_CONTEXT_PROFILE_ES)throw std::runtime_error("GLES2 build requires an ES context");
#endif
    SDL_Log("OGDoK graphics: %s; %s; %s; SDL video: %s",
        reinterpret_cast<const char*>(g.GetString(GL_VENDOR)),reinterpret_cast<const char*>(g.GetString(GL_RENDERER)),
        reinterpret_cast<const char*>(g.GetString(GL_VERSION)),SDL_GetCurrentVideoDriver());
    const auto vertex=g.shader(GL_VERTEX_SHADER,R"(
attribute vec4 position; attribute vec2 uv; attribute float depth; attribute vec4 material;
VARYING vec2 texcoord; VARYING float shade; VARYING vec4 mat;
void main(){gl_Position=position;texcoord=uv;shade=depth/4096.0;mat=material;}
)");
    const auto fragment=g.shader(GL_FRAGMENT_SHADER,R"(
uniform sampler2D picture; uniform sampler2D lighting;
uniform sampler2D picture1; uniform sampler2D picture2; uniform sampler2D picture3;
uniform sampler2D picture4; uniform sampler2D picture5; uniform sampler2D picture6;
uniform int mode; uniform int filtering; uniform int batched; uniform vec2 pictureSize; uniform vec2 outputScale;
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
// RGBA atlas contains independently reduced, premultiplied LIGHT colors.
vec4 mipRow(vec2 uv,vec2 size,float level,float row){
 vec2 dim=size;float offset=0.0,tile=0.0;
 for(int k=0;k<16;++k){
  if(float(k)<level){offset+=dim.x;}
  tile+=dim.x;
  if(dim.x<=1.0&&dim.y<=1.0)break;
  dim=max(floor(dim*0.5),vec2(1.0));
 }
 dim=max(floor(size/exp2(level)),vec2(1.0));
 vec2 p=clamp(uv*dim,vec2(0.5),dim-0.5);
 vec2 origin=vec2(mod(row,4.0)*tile+offset,floor(row/4.0)*size.y);
 return samplePicture((origin+p)/vec2(tile*4.0,size.y*4.0));
}
vec4 mipLight(vec2 uv,vec2 size,float level,float row){
 float lower=floor(row);
 return mix(mipRow(uv,size,level,lower),mipRow(uv,size,level,min(lower+1.0,15.0)),fract(row));
}
vec4 mipColor(vec2 uv,vec2 size,float row,vec2 dx,vec2 dy){
 // Principal axes of J*transpose(J), in source texels per screen pixel.
 float a=dx.x*dx.x+dy.x*dy.x,b=dx.x*dx.y+dy.x*dy.y,c=dx.y*dx.y+dy.y*dy.y;
 float major2=max(0.0,0.5*(a+c+sqrt(max(0.0,(a-c)*(a-c)+4.0*b*b))));
 float major=sqrt(major2),minor=major>0.0001?abs(dx.x*dy.y-dx.y*dy.x)/major:0.0;
 vec2 direction=abs(b)>0.00001?normalize(vec2(b,major2-a)):(a>=c?vec2(1.0,0.0):vec2(0.0,1.0));
 float taps=1.0;
 if(filtering>=4)taps=clamp(ceil(major/max(minor,1.0)),1.0,exp2(float(filtering-3)));
 float last=0.0;vec2 dim=size;
 for(int k=0;k<16;++k){if(dim.x<=1.0&&dim.y<=1.0)break;dim=max(floor(dim*0.5),vec2(1.0));last+=1.0;}
 float lod=clamp(log2(max(1.0,filtering>=4?max(minor,major/taps):major)),0.0,last);
 if(filtering==2)lod=floor(lod+0.5);
 float lower=floor(lod),upper=min(lower+1.0,last);vec4 color=vec4(0.0);
 for(int k=0;k<8;++k){if(float(k)>=taps)break;
  vec2 point=uv+direction*(major*((float(k)+0.5)/taps-0.5))/size;
  color+=mix(mipLight(point,size,lower,row),mipLight(point,size,upper,row),fract(lod));
 }
 color/=taps;if(color.a>0.0)color.rgb/=color.a;return color;
}
float luma(vec3 rgb){return dot(rgb,vec3(0.299,0.587,0.114));}
vec4 frameColor(vec2 uv){return texture2D(picture,uv);}
void main(){
 vec2 sourceSize=batched==1?floor(mat.xy+0.5):pictureSize;
 vec2 dx=vec2(0.0),dy=vec2(0.0);
#ifndef NO_MIP_DERIVATIVES
 // Evaluate derivatives before any divergent branch/discard.
 dx=dFdx(texcoord*sourceSize);dy=dFdy(texcoord*sourceSize);
#endif
 if(mode==3){
  // Contrast-adaptive directional FXAA, two/four taps along a local edge.
  vec2 step=1.0/pictureSize;
  vec3 nw=frameColor(texcoord+vec2(-1.0,1.0)*step).rgb,ne=frameColor(texcoord+step).rgb;
  vec3 sw=frameColor(texcoord-step).rgb,se=frameColor(texcoord+vec2(1.0,-1.0)*step).rgb;
  vec3 center=frameColor(texcoord).rgb;
  float a=luma(nw),b=luma(ne),c=luma(sw),d=luma(se),m=luma(center);
  float lo=min(m,min(min(a,b),min(c,d))),hi=max(m,max(max(a,b),max(c,d)));
  if(hi-lo<max(0.0312,hi*0.125)){gl_FragColor=vec4(center,1.0);return;}
  vec2 direction=vec2(-((a+b)-(c+d)),(a+c)-(b+d));
  float reduce=max((a+b+c+d)*0.03125,0.0078125);
  direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-8.0),vec2(8.0))*step;
  vec3 narrow=0.5*(frameColor(texcoord-direction/6.0).rgb+frameColor(texcoord+direction/6.0).rgb);
  vec3 wide=0.5*narrow+0.25*(frameColor(texcoord-direction*0.5).rgb+frameColor(texcoord+direction*0.5).rgb);
  float value=luma(wide);gl_FragColor=vec4(value<lo||value>hi?narrow:wide,1.0);return;
 }
 if(mode==2){
  vec2 uv=texcoord;
  if(filtering==2){
   vec2 p=uv*pictureSize-0.5,scale=max(floor(outputScale),vec2(1.0));
   uv=(floor(p)+clamp((fract(p)-0.5)*scale+0.5,0.0,1.0)+0.5)/pictureSize;
  }
  vec4 color=texture2D(picture,uv);if(color.a==0.0)discard;gl_FragColor=color;return;
 }
 float row=clamp((shade*4096.0-1920.0)/120.0,0.0,15.0);
 // Interpolate actual palette colors, never palette indices. Keep native
 // LIGHT colors at each anchor, while removing discrete distance bands.
 vec4 color;
 if(filtering>=2)color=mipColor(texcoord,sourceSize,row,dx,dy);
 else if(filtering==0)color=texelColor(texcoord,row);
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
    g.output_scale_location=g.GetUniformLocation(g.program,"outputScale");
    g.batch_location=g.GetUniformLocation(g.program,"batched");
    g.GenBuffers(1,&g.vbo);g.GenTextures(1,&g.light);g.PixelStorei(GL_UNPACK_ALIGNMENT,1);
    SDL_GL_SetSwapInterval(1);
}
GlRenderer::~GlRenderer()=default;
void GlRenderer::frame_size(int width,int height){
    auto& g=*impl_;SDL_GL_MakeCurrent(g.window,g.context);
#define REQUIRE_FRAME(name) if(!g.name)throw std::runtime_error("Fullscreen internal resolution requires gl" #name);
    FRAME_FUNCTIONS(REQUIRE_FRAME)
#undef REQUIRE_FRAME
    if(width<=0||height<=0)throw std::invalid_argument("Invalid fullscreen frame size");
    GLint limit=0;g.GetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    if(width>limit||height>limit)throw std::runtime_error("Fullscreen resolution exceeds GL texture limit");
    g.GenFramebuffers(1,&g.framebuffer);g.BindFramebuffer(GL_FRAMEBUFFER,g.framebuffer);
    g.GenTextures(1,&g.frame_texture);g.texture(g.frame_texture,0,width,height,GL_RGBA,nullptr);
    g.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g.frame_texture,0);
    g.GenRenderbuffers(1,&g.frame_depth);g.BindRenderbuffer(GL_RENDERBUFFER,g.frame_depth);
    g.RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,width,height);
    g.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,g.frame_depth);
    const bool complete=g.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    g.BindFramebuffer(GL_FRAMEBUFFER,0);
    if(!complete)throw std::runtime_error("Cannot allocate fullscreen frame buffer");
    g.frame_width=width;g.frame_height=height;
}
void GlRenderer::finish_frame(){
    flush_world();auto& g=*impl_;if(!g.framebuffer||g.frame_finished)return;
    g.frame_finished=true;g.BindFramebuffer(GL_FRAMEBUFFER,0);
    SDL_GL_GetDrawableSize(g.window,&g.width,&g.height);
    g.Viewport(0,0,g.width,g.height);g.Disable(GL_DEPTH_TEST);g.Disable(GL_BLEND);
    g.ClearColor(0,0,0,1);g.Clear(GL_COLOR_BUFFER_BIT);
    const auto r=fit_frame(g.width,g.height,g.frame_width,g.frame_height);
    g.Viewport(r.x,g.height-r.y-r.h,r.w,r.h);
    g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,g.frame_texture);
    g.UseProgram(g.program);g.Uniform1i(g.filter_location,0);
    g.Uniform2f(g.size_location,float(g.frame_width),float(g.frame_height));
    g.draw({-1,-1,0,1,0,0,0,1,-1,0,1,1,0,0,1,1,0,1,1,1,0,-1,1,0,1,0,1,0},2);
}
void GlRenderer::begin(){auto& g=*impl_;SDL_GL_MakeCurrent(g.window,g.context);SDL_GL_GetDrawableSize(g.window,&g.width,&g.height);g.height=std::max(1,g.height);
    g.frame_finished=false;if(g.BindFramebuffer)g.BindFramebuffer(GL_FRAMEBUFFER,g.framebuffer);
    if(g.framebuffer){g.width=g.frame_width;g.height=g.frame_height;}
    g.commands.clear();g.world_vertices.clear();g.deferred=false;g.immutable_images=false;
    if(g.ui_textures.size()>16){for(const auto& t:g.ui_textures)g.DeleteTextures(1,&t.second.id);g.ui_textures.clear();}
    // Bound CPU snapshots when a persistent Display visits many levels.
    if(g.image_hashes.size()>1024)g.image_hashes.clear();
    g.virtual_width=std::clamp(int(192.0*g.width/g.height+0.5),160,1200);g.virtual_height=160;g.scene_rect={0,0,g.width,g.height};
    g.Viewport(0,0,g.width,g.height);g.Disable(GL_DEPTH_TEST);g.Disable(GL_CULL_FACE);g.Disable(GL_BLEND);g.ClearColor(0,0,0,1);g.Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
int GlRenderer::view_width()const{return impl_->virtual_width;}
void GlRenderer::set_linear_filter(bool enabled){set_texture_filter(enabled?1:0);}
bool GlRenderer::mip_filters_supported()const{return impl_->derivatives;}
void GlRenderer::set_texture_filter(int mode){if(mode<0||mode>6)throw std::invalid_argument("Invalid GL filter");flush_world();impl_->texture_filter=mode>=2&&!impl_->derivatives?1:mode;}
void GlRenderer::scene_view(SDL_Rect r,int width,int height){
    flush_world();auto& g=*impl_;
    if(r.w<=0||r.h<=0||width<=0||height<=0)throw std::invalid_argument("Invalid GL scene viewport");
    g.scene_rect=r;g.virtual_width=width;g.virtual_height=height;
    g.Viewport(r.x,g.height-r.y-r.h,r.w,r.h);
}
void GlRenderer::finish_scene(bool fxaa){
    flush_world();auto& g=*impl_;if(!fxaa)return;
    const auto r=g.scene_rect;
    if(!g.post_texture)g.GenTextures(1,&g.post_texture);
    if(g.post_width!=r.w||g.post_height!=r.h){
        g.texture(g.post_texture,0,r.w,r.h,GL_RGBA,nullptr);g.post_width=r.w;g.post_height=r.h;
    }else{g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,g.post_texture);}
    g.CopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,r.x,g.height-r.y-r.h,r.w,r.h);
    g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    g.Disable(GL_DEPTH_TEST);g.Disable(GL_BLEND);g.UseProgram(g.program);
    g.Uniform2f(g.size_location,float(r.w),float(r.h));
    g.draw({-1,-1,0,1,0,0,0,1,-1,0,1,1,0,0,1,1,0,1,1,1,0,-1,1,0,1,0,1,0},3);
}
void GlRenderer::world(const std::array<std::uint32_t,256>& palette,bool defer,bool immutable_images){
    flush_world();auto& g=*impl_;
    // Retire cold level/animation materials under pressure, retaining the last
    // frame's working set so a large visible scene cannot trigger cache thrash.
    if(g.mip_bytes>64*1024*1024)for(auto it=g.mip_textures.begin();it!=g.mip_textures.end();){
        if(it->second.epoch<g.image_epoch){g.DeleteTextures(1,&it->second.id);g.mip_bytes-=it->second.bytes;it=g.mip_textures.erase(it);}else ++it;
    }
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
        if(!batch||batch->filter!=c.filter||batch->blend!=c.blend||slot==7){
            g.batches.push_back({static_cast<GLint>(g.triangle_vertices.size()/11),0,{},0,c.filter,c.blend});
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
        g.Uniform1i(g.filter_location,b.filter);
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
        const float x=(2.0f*(v[0]+0.5f)/g.virtual_width-1)*w,y=(1-2.0f*(v[1]+0.5f)/g.virtual_height)*w;
        left&=x<-w;right&=x>w;above&=y>w;below&=y<-w;
    }
    // Keep the first-polygon LIGHT contract even if that polygon is offscreen.
    if(!g.light_uploaded){
        if(g.light_values.size()!=light.size||!std::equal(g.light_values.begin(),g.light_values.end(),light.data)){
            // Exact composition of the two nearest 8-bit lookup tables. Keep
            // interpolation/alpha in the shader in precisely the same order.
            for(const auto& t:g.mip_textures)g.DeleteTextures(1,&t.second.id);g.mip_textures.clear();g.mip_bytes=0;
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
    if(g.texture_filter>=2){
        const bool transparent=(image.flags&8)&&axis==RasterAxis::vertical;
        // Separate opaque and transparent uses of the same index bytes.
        const auto key=hash^(transparent?0x9e3779b97f4a7c15ULL:0ULL);
        const auto mip=g.mip_textures.find(key);
        if(mip==g.mip_textures.end()){
            const auto atlas=make_mip_atlas(image,g.palette_values,light,transparent);
            GLint limit=0;g.GetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
            if(atlas.width>limit||atlas.height>limit)throw std::runtime_error("Lit mip atlas exceeds GL texture limit");
            g.GenTextures(1,&id);g.rgba(id,0,atlas.width,atlas.height,atlas.pixels.data());
            g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            const auto bytes=atlas.pixels.size()*sizeof(std::uint32_t);g.mip_bytes+=bytes;
            g.mip_textures.emplace(key,Impl::MipTexture{id,g.image_epoch,bytes});
        }else{id=mip->second.id;mip->second.epoch=g.image_epoch;if(!g.deferred){g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,id);}}
    }
    const bool blend=g.texture_filter!=0&&(image.flags&8)&&axis==RasterAxis::vertical;
    auto& data=g.polygon_data;data.clear();data.reserve(vertices.size()*7);
    for(const auto& p:vertices){
        const auto v=p.precise.value_or(std::array<float,5>{float(p.x),float(p.y),float(p.depth),float(p.u),float(p.v)});
        const float w=std::max(1.0f,v[2]+200.0f);
        data.insert(data.end(),{(2.0f*(v[0]+0.5f)/g.virtual_width-1)*w,(1-2.0f*(v[1]+0.5f)/g.virtual_height)*w,w-2.0f,w,
            (v[3]+0.5f)/image.width,(v[4]+0.5f)/image.height,p.shade_depth.value_or(std::max(0.0f,v[2]))});}
    const int mode=(axis==RasterAxis::vertical&&(image.flags&8))?1:0;
    if(g.deferred){
        g.commands.push_back({id,static_cast<GLint>(g.world_vertices.size()/7),static_cast<GLsizei>(data.size()/7),image.width,image.height,mode,g.texture_filter,blend});
        g.world_vertices.insert(g.world_vertices.end(),data.begin(),data.end());
    }else{
        g.Uniform1i(g.filter_location,g.texture_filter);
        g.Uniform2f(g.size_location,float(image.width),float(image.height));
        if(blend){g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}else g.Disable(GL_BLEND);
        g.draw(data,mode);
    }
}
void GlRenderer::image(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect r,bool screen_pixels,int filter){
    if(filter==3&&r.w<=width&&r.h<=height)filter=0;
    flush_world();
    auto& g=*impl_;
    const auto key=(std::uint64_t(width)<<32)|unsigned(height);
    auto& texture=g.ui_textures[key];const auto count=std::size_t(width)*height;
    if(!texture.id){g.GenTextures(1,&texture.id);g.rgba(texture.id,0,width,height,pixels.data());texture.pixels.assign(pixels.begin(),pixels.begin()+count);}
    else if(std::memcmp(texture.pixels.data(),pixels.data(),count*sizeof(std::uint32_t))!=0){
        g.rgba(texture.id,0,width,height,pixels.data(),true);std::copy_n(pixels.data(),count,texture.pixels.data());
    }else{g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,texture.id);}
    if(filter==3){
        if(g.xbr(texture.id,width,height)){g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,g.xbr_texture);width*=3;height*=3;filter=0;}
        else{
            const auto scaled=xbr3(pixels,width,height);const int sw=width*3,sh=height*3;
            GLint limit=0;g.GetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
            if(SDL_GetHintBoolean("DOK_XBR_CPU_TILES",SDL_FALSE))limit=std::min(limit,32);
            if(sw<=limit&&sh<=limit){image(scaled,sw,sh,r,screen_pixels,0);return;}
            // CPU fallback must also work when the 3x texture exceeds an old
            // GL2 device limit. Sample visible output pixels into bounded tiles.
            const double sx=screen_pixels?1.0:double(g.width)/320,sy=screen_pixels?1.0:double(g.height)/240;
            const double left=r.x*sx,top=r.y*sy,rw=r.w*sx,rh=r.h*sy;
            const int x0=std::clamp(int(std::ceil(left-.5)),0,g.width),x1=std::clamp(int(std::ceil(left+rw-.5)),0,g.width);
            const int y0=std::clamp(int(std::ceil(top-.5)),0,g.height),y1=std::clamp(int(std::ceil(top+rh-.5)),0,g.height);
            const int step=std::max(1,std::min(int(limit),512));
            for(int y=y0;y<y1;y+=step)for(int x=x0;x<x1;x+=step){
                const int tw=std::min(step,x1-x),th=std::min(step,y1-y);std::vector<std::uint32_t> tile(std::size_t(tw)*th);
                for(int ty=0;ty<th;++ty){const int iy=std::clamp(int((y+ty+.5-top)*sh/rh),0,sh-1);
                    for(int tx=0;tx<tw;++tx){const int ix=std::clamp(int((x+tx+.5-left)*sw/rw),0,sw-1);tile[std::size_t(ty)*tw+tx]=scaled[std::size_t(iy)*sw+ix];}}
                image(tile,tw,th,{x,y,tw,th},true,0);
            }return;
        }
    }
    g.Viewport(0,0,g.width,g.height);
    g.UseProgram(g.program);g.Uniform1i(g.filter_location,filter);
    g.Uniform2f(g.size_location,float(width),float(height));
    g.Uniform2f(g.output_scale_location,float(r.w)/width,float(r.h)/height);
    g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter?GL_LINEAR:GL_NEAREST);
    g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter?GL_LINEAR:GL_NEAREST);
    g.Disable(GL_DEPTH_TEST);g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const float rw=screen_pixels?float(g.width):320.f,rh=screen_pixels?float(g.height):240.f;
    const float l=2.f*r.x/rw-1,rr=2.f*(r.x+r.w)/rw-1,t=1-2.f*r.y/rh,b=1-2.f*(r.y+r.h)/rh;
    g.draw({l,t,0,1,0,0,0,rr,t,0,1,1,0,0,rr,b,0,1,1,1,0,l,b,0,1,0,1,0},2);
}
bool GlRenderer::xbr_gpu_ready()const{return impl_->xbr_program&&impl_->xbr_framebuffer&&!impl_->xbr_failed&&!SDL_GetHintBoolean("DOK_DISABLE_GPU_XBR",SDL_FALSE);}
void GlRenderer::present(){finish_frame();flush_world();SDL_GL_SwapWindow(impl_->window);}
std::vector<std::uint32_t> GlRenderer::read_pixels(){finish_frame();flush_world();auto& g=*impl_;std::vector<std::uint8_t> bytes(std::size_t(g.width)*g.height*4);g.ReadPixels(0,0,g.width,g.height,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());std::vector<std::uint32_t> pixels(std::size_t(g.width)*g.height);
    for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x){const auto* p=&bytes[(std::size_t(g.height-1-y)*g.width+x)*4];pixels[std::size_t(y)*g.width+x]=0xff000000u|(p[0]<<16)|(p[1]<<8)|p[2];}return pixels;
}
void GlRenderer::screenshot(const std::string& path){finish_frame();flush_world();auto& g=*impl_;std::vector<std::uint8_t> bytes(std::size_t(g.width)*g.height*4);
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
