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
 X(Enable) X(Disable) X(BlendFunc) X(DepthFunc) X(Scissor) X(ReadPixels) X(CopyTexSubImage2D)
#define FRAME_FUNCTIONS(X) \
 X(GenFramebuffers) X(BindFramebuffer) X(FramebufferTexture2D) X(CheckFramebufferStatus) X(DeleteFramebuffers) \
 X(GenRenderbuffers) X(BindRenderbuffer) X(RenderbufferStorage) X(FramebufferRenderbuffer) X(DeleteRenderbuffers)
struct GlRenderer::Impl {
    SDL_Window* window;SDL_GLContext context=nullptr;int width=1,height=1,virtual_width=320,virtual_height=160;
    int frame_width=0,frame_height=0;GLuint framebuffer=0,frame_texture=0,frame_depth=0;
    bool frame_offscreen=false,native_frame_format=false;
    SDL_Rect scene_rect{};GLuint post_texture=0;int post_width=0,post_height=0;
    struct Program {
        GLuint id=0;
        GLint masked=-1,filter=-1,size=-1,batch=-1,output_scale=-1;
    };
    Program nearest,nearest_opaque,filtered,ui,fxaa;
    GLuint vbo=0,light=0;
    const Program& world_program(int filter,bool opaque)const{
        return filter==0?(opaque?nearest_opaque:nearest):filtered;
    }
    GLuint xbr_program=0,xbr_framebuffer=0,xbr_texture=0;
    int xbr_width=0,xbr_height=0;bool xbr_failed=false;
    GLint xbr_size=-1;
    struct UiTexture {GLuint id=0;std::vector<std::uint32_t> pixels;SDL_Rect bounds{};bool opaque=false;std::uint64_t revision=0;};
    std::unordered_map<std::uint64_t,UiTexture> ui_textures;
    std::array<std::uint32_t,256> palette_values{};
    std::vector<std::uint8_t> light_values;
    std::vector<std::uint8_t> light_rgba;
    bool palette_valid=false,opaque_palette=false;
    bool light_uploaded=false;
    int texture_filter=0;bool derivatives=true;
    struct MipTexture {GLuint id;std::uint64_t epoch;std::size_t bytes;};
    std::unordered_map<std::uint64_t,MipTexture> mip_textures;
    std::size_t mip_bytes=0;
    std::unordered_map<std::uint64_t,GLuint> textures;
    std::size_t preloaded_bytes=0;
    // Validate bytes, not pointer identity: callers may mutate or recycle Image.
    struct ImageHash {
        unsigned width=0,height=0;std::vector<std::uint8_t> pixels;
        std::uint64_t hash=0,epoch=0;
        std::optional<SDL_Rect> ink;
    };
    std::unordered_map<const void*,ImageHash> image_hashes;
    std::uint64_t image_epoch=0;
    bool immutable_images=false;
    std::vector<std::uint8_t> rgba_bytes;
    std::vector<float> polygon_data;
    struct Command {GLuint texture;GLint first;GLsizei count;unsigned width,height;int mode;bool blend;bool opaque;SDL_Rect scissor;};
    bool deferred=false,reverse_depth_order=false;
    std::vector<float> world_vertices;
    std::vector<Command> commands;
    struct Batch {GLint first=0;GLsizei count=0;std::array<GLuint,7> textures{};unsigned used=0;bool blend=false;bool opaque=false;SDL_Rect scissor{};};
    std::vector<Batch> batches;
    std::vector<float> triangle_vertices;
#define DECLARE(name) decltype(&gl##name) name=nullptr;
    GL_FUNCTIONS(DECLARE)
    FRAME_FUNCTIONS(DECLARE)
#undef DECLARE
    GLuint shader(GLenum kind,const char* body,const char* defines=""){
        int profile=0;SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,&profile);
        const char* prefix=profile==SDL_GL_CONTEXT_PROFILE_ES?
            (kind==GL_VERTEX_SHADER?"#version 100\nprecision highp float;\n#define VARYING varying mediump\n":"#version 100\nprecision mediump float;\n#define VARYING varying mediump\n"):
            "#version 110\n#define VARYING varying\n";GLuint id=CreateShader(kind);
        const char* extension=profile==SDL_GL_CONTEXT_PROFILE_ES&&kind==GL_FRAGMENT_SHADER?(derivatives?"#extension GL_OES_standard_derivatives : enable\n":"#define NO_MIP_DERIVATIVES\n"):"";
        // Extensions must precede precision declarations in GLES.
        std::string header=prefix;if(profile==SDL_GL_CONTEXT_PROFILE_ES&&kind==GL_FRAGMENT_SHADER)header=std::string("#version 100\n")+extension+"#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n#define VARYING varying mediump\n";
        const char* sources[]={header.c_str(),defines,body};ShaderSource(id,3,sources,nullptr);CompileShader(id);
        GLint okay=0;GetShaderiv(id,GL_COMPILE_STATUS,&okay);
        if(!okay){char message[2048]{};GetShaderInfoLog(id,sizeof(message),nullptr,message);DeleteShader(id);throw std::runtime_error(message);}return id;
    }
    void texture(GLuint id,int unit,int w,int h,GLenum format,const void* data){
        ActiveTexture(GL_TEXTURE0+unit);BindTexture(GL_TEXTURE_2D,id);
        TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        TexImage2D(GL_TEXTURE_2D,0,format,w,h,0,format,GL_UNSIGNED_BYTE,data);
    }
    static std::uint64_t image_hash(const Image& image){
        std::uint64_t hash=1469598103934665603ULL;
        for(auto byte:image.pixels)hash=(hash^byte)*1099511628211ULL;
        return hash^((std::uint64_t(image.width)<<32)|image.height);
    }
    GLuint indexed_texture(const Image& image,std::uint64_t hash){
        const auto found=textures.find(hash);
        if(found!=textures.end())return found->second;
        GLuint id=0;
        GenTextures(1,&id);
        texture(id,0,image.width,image.height,GL_LUMINANCE,image.pixels.data());
        textures.emplace(hash,id);
        return id;
    }
    static SDL_Rect indexed_ink(const Image& image){
        int left=image.width,right=0,top=image.height,bottom=0;
        for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){
            if(!image.pixels[std::size_t(y)*image.width+x])continue;
            left=std::min(left,int(x));right=std::max(right,int(x)+1);
            top=std::min(top,int(y));bottom=int(y)+1;
        }
        return left<right?SDL_Rect{left,top,right-left,bottom-top}:SDL_Rect{};
    }
    SDL_Rect mask_scissor(const std::vector<float>& data,const Image& image,SDL_Rect ink,SDL_Rect bounds)const{
        // Keep the original triangles and interpolation. Intersect their
        // edges in homogeneous coordinates with the largest transparent
        // margin's UV plane, then restrict fragment work with a scissor.
        const int w=image.width,h=image.height;
        const int margins[]{ink.x,w-ink.x-ink.w,ink.y,h-ink.y-ink.h};
        int side=0;
        for(int i=1;i<4;++i)
            if(double(margins[i])/(i<2?w:h)>double(margins[side])/(side<2?w:h))side=i;
        const int extent=side<2?w:h;
        const int margin=margins[side]-2-extent/512;
        if(margin*8<extent)return {};
        const bool lower=side%2==0;
        const double limit=lower?double(margin)/extent:1.0-double(margin)/extent;
        const int uv=side<2?4:5;
        double xmin=1,xmax=-1,ymin=1,ymax=-1;
        auto include=[&](double x,double y,double clip_w){
            x=std::clamp(x/clip_w,-1.0,1.0);y=std::clamp(y/clip_w,-1.0,1.0);
            xmin=std::min(xmin,x);xmax=std::max(xmax,x);
            ymin=std::min(ymin,y);ymax=std::max(ymax,y);
        };
        auto inside=[&](const float* v){return lower?v[uv]>=limit:v[uv]<=limit;};
        auto edge=[&](const float* a,const float* b){
            if(inside(a)==inside(b))return;
            const double t=(limit-a[uv])/(double(b[uv])-a[uv]);
            include(a[0]+t*(double(b[0])-a[0]),a[1]+t*(double(b[1])-a[1]),a[3]+t*(double(b[3])-a[3]));
        };
        for(std::size_t i=0;i<data.size();i+=7){
            const auto* p=data.data()+i;
            if(inside(p))include(p[0],p[1],p[3]);
            edge(p,data.data()+(i+7)%data.size());
            if(i>7&&i+7<data.size())edge(data.data(),p);
        }
        if(xmax<xmin||ymax<ymin)return {};
        const auto clipped=pixel_bounds(xmin,xmax,ymin,ymax);
        const auto saved=std::int64_t(bounds.w)*bounds.h-std::int64_t(clipped.w)*clipped.h;
        return saved>=std::int64_t(scene_rect.w)*scene_rect.h/12?clipped:SDL_Rect{};
    }
    SDL_Rect pixel_bounds(double xmin,double xmax,double ymin,double ymax)const{
        const auto r=scene_rect;
        const int x0=std::max(0,int(std::floor((xmin+1)*.5*r.w))-1);
        const int x1=std::min(r.w,int(std::ceil((xmax+1)*.5*r.w))+1);
        const int y0=std::max(0,int(std::floor((ymin+1)*.5*r.h))-1);
        const int y1=std::min(r.h,int(std::ceil((ymax+1)*.5*r.h))+1);
        return {r.x+x0,height-r.y-r.h+y0,x1-x0,y1-y0};
    }
    void rgba(GLuint id,int unit,int width,int height,const std::uint32_t* data,const SDL_Rect* changed=nullptr){
        const SDL_Rect area=changed?*changed:SDL_Rect{0,0,width,height};
        rgba_bytes.resize(std::size_t(area.w)*area.h*4);
        auto* dst=rgba_bytes.data();
        for(int y=0;y<area.h;++y){
            const auto* src=data+std::size_t(area.y+y)*width+area.x;
            for(int x=0;x<area.w;++x){
                const auto color=src[x];
                dst[0]=color>>16;dst[1]=color>>8;dst[2]=color;dst[3]=color>>24;
                dst+=4;
            }
        }
        if(changed){
            ActiveTexture(GL_TEXTURE0+unit);BindTexture(GL_TEXTURE_2D,id);
            TexSubImage2D(GL_TEXTURE_2D,0,area.x,area.y,area.w,area.h,GL_RGBA,GL_UNSIGNED_BYTE,rgba_bytes.data());
        }else texture(id,unit,width,height,GL_RGBA,rgba_bytes.data());
    }
    static SDL_Rect changed_area(const std::uint32_t* before,const std::uint32_t* after,int width,int height){
        int left=width,right=0,top=height,bottom=0;
        for(int y=0;y<height;++y,before+=width,after+=width){
            if(std::memcmp(before,after,std::size_t(width)*sizeof(*before))==0)continue;
            top=std::min(top,y);bottom=y+1;
            for(int x=0;x<left;++x)if(before[x]!=after[x]){left=x;break;}
            for(int x=width;x>right;--x)if(before[x-1]!=after[x-1]){right=x;break;}
        }
        return {left,top,right-left,bottom-top};
    }
    static SDL_Rect alpha_bounds(const std::uint32_t* data,int width,int height){
        if((data[0]>>24)&&(data[width-1]>>24)
           &&(data[std::size_t(height-1)*width]>>24)&&(data[std::size_t(height)*width-1]>>24))
            return {0,0,width,height};
        int left=width,top=height,right=0,bottom=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            if(!(data[std::size_t(y)*width+x]>>24))continue;
            left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=y+1;
        }
        if(left>=right)return {};
        // Leave a margin for interpolation precision at texture boundaries.
        const int pad_x=2+width/512,pad_y=2+height/512;
        left=std::max(0,left-pad_x);top=std::max(0,top-pad_y);
        return {left,top,std::min(width,right+pad_x)-left,std::min(height,bottom+pad_y)-top};
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
    void draw_quad(const std::vector<float>& vertices){
        upload(vertices);
        DrawArrays(GL_TRIANGLE_FAN,0,static_cast<GLsizei>(vertices.size()/7));
    }
    void build_program(Program& program,GLuint vertex,const char* source,const char* defines,bool world){
        const auto fragment=shader(GL_FRAGMENT_SHADER,source,defines);
        program.id=CreateProgram();AttachShader(program.id,vertex);AttachShader(program.id,fragment);
        BindAttribLocation(program.id,0,"position");BindAttribLocation(program.id,1,"uv");
        BindAttribLocation(program.id,2,"depth");BindAttribLocation(program.id,3,"material");
        LinkProgram(program.id);DeleteShader(fragment);
        GLint okay=0;GetProgramiv(program.id,GL_LINK_STATUS,&okay);
        if(!okay){char message[2048]{};GetProgramInfoLog(program.id,sizeof(message),nullptr,message);throw std::runtime_error(message);}
        UseProgram(program.id);Uniform1i(GetUniformLocation(program.id,"picture"),0);
        if(world){
            Uniform1i(GetUniformLocation(program.id,"lighting"),7);
            for(int i=1;i<7;++i)Uniform1i(GetUniformLocation(program.id,("picture"+std::to_string(i)).c_str()),i);
            program.masked=GetUniformLocation(program.id,"masked");
            program.batch=GetUniformLocation(program.id,"batched");
        }
        program.filter=GetUniformLocation(program.id,"filtering");
        program.size=GetUniformLocation(program.id,"pictureSize");
        program.output_scale=GetUniformLocation(program.id,"outputScale");
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
        if(DeleteBuffers&&vbo)DeleteBuffers(1,&vbo);
        if(DeleteProgram)for(const auto* program:{&nearest,&nearest_opaque,&filtered,&ui,&fxaa})if(program->id)DeleteProgram(program->id);
        SDL_GL_DeleteContext(context);}}
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
    const auto fragment=R"(
uniform sampler2D picture;
uniform vec2 pictureSize;
VARYING vec2 texcoord;
#ifdef WORLD
uniform sampler2D lighting;
uniform sampler2D picture1; uniform sampler2D picture2; uniform sampler2D picture3;
uniform sampler2D picture4; uniform sampler2D picture5; uniform sampler2D picture6;
uniform int masked; uniform int filtering; uniform int batched;
VARYING float shade; VARYING vec4 mat;
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
#ifndef OPAQUE
 if((batched==1?mat.w:float(masked))>0.5 && index<0.5)return vec4(0.0);
#endif
 float lower=floor(row);
 return mix(litColor(index,lower),litColor(index,min(lower+1.0,15.0)),fract(row));
}
#ifdef FILTERED
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
#endif
void main(){
#ifdef FILTERED
 vec2 sourceSize=batched==1?floor(mat.xy+0.5):pictureSize;
 vec2 dx=vec2(0.0),dy=vec2(0.0);
#ifndef NO_MIP_DERIVATIVES
 // Evaluate derivatives before any divergent branch/discard.
 dx=dFdx(texcoord*sourceSize);dy=dFdy(texcoord*sourceSize);
#endif
#endif
 float row=clamp((shade*4096.0-1920.0)/120.0,0.0,15.0);
 // Interpolate actual palette colors while retaining native LIGHT anchors.
 vec4 color;
#ifdef FILTERED
 if(filtering>=2)color=mipColor(texcoord,sourceSize,row,dx,dy);
 else{
  vec2 p=texcoord*sourceSize-0.5,base=(floor(p)+0.5)/sourceSize,f=fract(p),step=1.0/sourceSize;
  color=mix(mix(texelColor(base,row),texelColor(base+vec2(step.x,0.0),row),f.x),
            mix(texelColor(base+vec2(0.0,step.y),row),texelColor(base+step,row),f.x),f.y);
  if(color.a>0.0)color.rgb/=color.a;
 }
#else
 color=texelColor(texcoord,row);
#endif
#ifndef OPAQUE
 if(color.a<=0.0)discard;
#endif
 gl_FragColor=color;
}
#elif defined(FXAA)
float luma(vec3 rgb){return dot(rgb,vec3(0.299,0.587,0.114));}
vec4 frameColor(vec2 uv){return texture2D(picture,uv);}
void main(){
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
  float value=luma(wide);gl_FragColor=vec4(value<lo||value>hi?narrow:wide,1.0);
}
#else
uniform int filtering; uniform vec2 outputScale;
void main(){
  vec2 uv=texcoord;
  if(filtering==2){
   vec2 p=uv*pictureSize-0.5,scale=max(floor(outputScale),vec2(1.0));
   uv=(floor(p)+clamp((fract(p)-0.5)*scale+0.5,0.0,1.0)+0.5)/pictureSize;
  }
  gl_FragColor=texture2D(picture,uv);
}
#endif
)";
    try{
        g.build_program(g.nearest,vertex,fragment,"#define WORLD\n",true);
        g.build_program(g.nearest_opaque,vertex,fragment,"#define WORLD\n#define OPAQUE\n",true);
        g.build_program(g.filtered,vertex,fragment,"#define WORLD\n#define FILTERED\n",true);
        g.build_program(g.ui,vertex,fragment,"",false);
        g.build_program(g.fxaa,vertex,fragment,"#define FXAA\n",false);
    }catch(...){g.DeleteShader(vertex);throw;}
    g.DeleteShader(vertex);
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
    g.BindFramebuffer(GL_FRAMEBUFFER,0);
    GLint red=0,green=0,blue=0,depth=0;
    g.GetIntegerv(GL_RED_BITS,&red);g.GetIntegerv(GL_GREEN_BITS,&green);g.GetIntegerv(GL_BLUE_BITS,&blue);
    g.GetIntegerv(GL_DEPTH_BITS,&depth);
    // Use direct output when the drawable has full color and at least the
    // offscreen target's depth precision.
    g.native_frame_format=red==8&&green==8&&blue==8&&depth>=16;
    if(!g.framebuffer)g.GenFramebuffers(1,&g.framebuffer);
    g.BindFramebuffer(GL_FRAMEBUFFER,g.framebuffer);
    if(!g.frame_texture)g.GenTextures(1,&g.frame_texture);
    g.texture(g.frame_texture,0,width,height,GL_RGBA,nullptr);
    g.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g.frame_texture,0);
    if(!g.frame_depth)g.GenRenderbuffers(1,&g.frame_depth);
    g.BindRenderbuffer(GL_RENDERBUFFER,g.frame_depth);
    g.RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,width,height);
    g.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,g.frame_depth);
    const bool complete=g.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    g.BindFramebuffer(GL_FRAMEBUFFER,0);
    if(!complete)throw std::runtime_error("Cannot allocate fullscreen frame buffer");
    g.frame_width=width;g.frame_height=height;
}
void GlRenderer::finish_frame(){
    flush_world();auto& g=*impl_;if(!g.frame_offscreen)return;
    g.frame_offscreen=false;g.BindFramebuffer(GL_FRAMEBUFFER,0);
    SDL_GL_GetDrawableSize(g.window,&g.width,&g.height);
    const auto r=fit_frame(g.width,g.height,g.frame_width,g.frame_height);
    g.Disable(GL_DEPTH_TEST);g.Disable(GL_BLEND);
    if(r.w!=g.width||r.h!=g.height){g.ClearColor(0,0,0,1);g.Clear(GL_COLOR_BUFFER_BIT);}
    g.Viewport(r.x,g.height-r.y-r.h,r.w,r.h);
    g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,g.frame_texture);
    g.UseProgram(g.ui.id);g.Uniform1i(g.ui.filter,0);
    g.draw_quad({-1,-1,0,1,0,0,0,1,-1,0,1,1,0,0,1,1,0,1,1,1,0,-1,1,0,1,0,1,0});
}
void GlRenderer::begin(){auto& g=*impl_;if(SDL_GL_GetCurrentContext()!=g.context)SDL_GL_MakeCurrent(g.window,g.context);
    SDL_GL_GetDrawableSize(g.window,&g.width,&g.height);g.height=std::max(1,g.height);
    g.frame_offscreen=g.framebuffer&&(!g.native_frame_format||g.width!=g.frame_width||g.height!=g.frame_height);
    if(g.BindFramebuffer)g.BindFramebuffer(GL_FRAMEBUFFER,g.frame_offscreen?g.framebuffer:0);
    if(g.frame_offscreen){g.width=g.frame_width;g.height=g.frame_height;}
    g.commands.clear();g.world_vertices.clear();g.deferred=false;g.immutable_images=false;g.reverse_depth_order=true;
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
    g.Disable(GL_DEPTH_TEST);g.Disable(GL_BLEND);g.UseProgram(g.fxaa.id);
    g.Uniform2f(g.fxaa.size,float(r.w),float(r.h));
    g.draw_quad({-1,-1,0,1,0,0,0,1,-1,0,1,1,0,0,1,1,0,1,1,1,0,-1,1,0,1,0,1,0});
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
    if(!g.palette_valid||g.palette_values!=palette){
        g.palette_values=palette;g.palette_valid=true;g.light_values.clear();
        g.opaque_palette=std::all_of(palette.begin(),palette.end(),[](auto color){return (color>>24)==255;});
    }
    g.Disable(GL_BLEND);g.Enable(GL_DEPTH_TEST);g.light_uploaded=false;g.deferred=defer;
}
void GlRenderer::flush_world(){
    auto& g=*impl_;g.deferred=false;g.immutable_images=false;if(g.commands.empty())return;
    g.batches.clear();g.triangle_vertices.clear();
    // Scene submits far to near. For an unblended pass over an empty depth
    // buffer, start with nearby surfaces so depth can reject hidden fragments.
    // Reversing LESS into LEQUAL preserves the first submission on depth ties.
    const bool reverse=g.texture_filter==0&&g.reverse_depth_order;
    for(std::size_t i=0;i<g.commands.size();++i){
        const auto& c=g.commands[reverse?g.commands.size()-1-i:i];
        auto* batch=g.batches.empty()?nullptr:&g.batches.back();
        unsigned slot=0;
        if(batch)while(slot<batch->used&&batch->textures[slot]!=c.texture)++slot;
        if(!batch||batch->blend!=c.blend||batch->opaque!=c.opaque||slot==7||c.scissor.w||batch->scissor.w){
            g.batches.push_back({static_cast<GLint>(g.triangle_vertices.size()/11),0,{},0,c.blend,c.opaque,c.scissor});
            batch=&g.batches.back();slot=0;
        }
        if(slot==batch->used)batch->textures[batch->used++]=c.texture;
        // Keep each polygon's fan triangles and texture coordinates.
        for(int i=1;i+1<c.count;++i)for(int index:{0,i,i+1}){
            const auto first=g.world_vertices.begin()+std::size_t(c.first+index)*7;
            g.triangle_vertices.insert(g.triangle_vertices.end(),first,first+7);
            g.triangle_vertices.insert(g.triangle_vertices.end(),{float(c.width),float(c.height),float(slot),float(c.mode)});
            ++batch->count;
        }
    }
    // set_texture_filter flushes queued polygons before changing the filter.
    g.upload(g.triangle_vertices,true);
    if(reverse)g.DepthFunc(GL_LEQUAL);
    GLuint active_program=0;
    for(const auto& b:g.batches){
        const auto& program=g.world_program(g.texture_filter,b.opaque);
        if(active_program!=program.id){
            g.UseProgram(program.id);g.Uniform1i(program.batch,1);
            g.Uniform1i(program.filter,g.texture_filter);active_program=program.id;
        }
        for(unsigned slot=0;slot<b.used;++slot){g.ActiveTexture(GL_TEXTURE0+slot);g.BindTexture(GL_TEXTURE_2D,b.textures[slot]);}
        if(b.blend){g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}else g.Disable(GL_BLEND);
        if(b.scissor.w){
            g.Enable(GL_SCISSOR_TEST);
            g.Scissor(b.scissor.x,b.scissor.y,b.scissor.w,b.scissor.h);
        }
        g.DrawArrays(GL_TRIANGLES,b.first,b.count);
        if(b.scissor.w)g.Disable(GL_SCISSOR_TEST);
    }
    if(reverse)g.DepthFunc(GL_LESS);
    g.reverse_depth_order=false;
    g.commands.clear();g.world_vertices.clear();
}
void GlRenderer::preload_indexed(const Image& image){
    auto& g=*impl_;
    if(SDL_GL_GetCurrentContext()!=g.context&&SDL_GL_MakeCurrent(g.window,g.context)!=0)
        throw std::runtime_error(SDL_GetError());
    constexpr std::size_t budget=8*1024*1024;
    if(image.pixels.size()>budget-g.preloaded_bytes)return;
    const auto hash=g.image_hash(image);
    if(g.textures.find(hash)!=g.textures.end())return;
    g.indexed_texture(image,hash);
    g.preloaded_bytes+=image.pixels.size();
}
void GlRenderer::polygon(const std::vector<RasterVertex>& vertices,const Image& image,Bytes light,RasterAxis axis){
    auto& g=*impl_;if(vertices.size()<3)return;
    // All clip W are positive (max(1, depth+200)). A polygon whose vertices
    // lie strictly outside the same XY clip plane cannot produce a fragment.
    // Evaluate exactly the float expressions uploaded below, including subpixel poses.
    const int mode=(axis==RasterAxis::vertical&&(image.flags&8))?1:0;
    const bool prepare_scissor=mode&&g.texture_filter==0&&g.deferred;
    bool left=true,right=true,above=true,below=true;
    float xmin=1,xmax=-1,ymin=1,ymax=-1;
    for(const auto& p:vertices){
        const auto v=p.precise.value_or(std::array<float,5>{float(p.x),float(p.y),float(p.depth),float(p.u),float(p.v)});
        const float w=std::max(1.0f,v[2]+200.0f);
        // A polygon at the clear depth must still fail LESS on either a
        // 16-bit or a higher precision target.
        if(w>65536.0f)g.reverse_depth_order=false;
        const float x=(2.0f*(v[0]+0.5f)/g.virtual_width-1)*w,y=(1-2.0f*(v[1]+0.5f)/g.virtual_height)*w;
        left&=x<-w;right&=x>w;above&=y>w;below&=y<-w;
        if(prepare_scissor){
            const float nx=x/w,ny=y/w;
            xmin=std::min(xmin,nx);xmax=std::max(xmax,nx);
            ymin=std::min(ymin,ny);ymax=std::max(ymax,ny);
        }
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
        cached.hash=g.image_hash(image);
        cached.width=image.width;cached.height=image.height;cached.pixels=image.pixels;
        cached.ink.reset();
    }
    cached.epoch=g.image_epoch;
    const auto hash=cached.hash;
    SDL_Rect bounds{};
    if(prepare_scissor){
        bounds=g.pixel_bounds(std::clamp(xmin,-1.f,1.f),std::clamp(xmax,-1.f,1.f),
                              std::clamp(ymin,-1.f,1.f),std::clamp(ymax,-1.f,1.f));
    }
    const bool large_mask=std::int64_t(bounds.w)*bounds.h>std::int64_t(g.scene_rect.w)*g.scene_rect.h/8;
    if(large_mask){
        if(!cached.ink)cached.ink=g.indexed_ink(image);
        if(!cached.ink->w)return;
    }
    GLuint id;
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
    }else{
        id=g.indexed_texture(image,hash);
        if(!g.deferred){g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,id);}
    }
    const bool blend=g.texture_filter!=0&&(image.flags&8)&&axis==RasterAxis::vertical;
    auto& data=g.polygon_data;data.clear();data.reserve(vertices.size()*7);
    for(const auto& p:vertices){
        const auto v=p.precise.value_or(std::array<float,5>{float(p.x),float(p.y),float(p.depth),float(p.u),float(p.v)});
        const float w=std::max(1.0f,v[2]+200.0f);
        data.insert(data.end(),{(2.0f*(v[0]+0.5f)/g.virtual_width-1)*w,(1-2.0f*(v[1]+0.5f)/g.virtual_height)*w,w-2.0f,w,
            (v[3]+0.5f)/image.width,(v[4]+0.5f)/image.height,p.shade_depth.value_or(std::max(0.0f,v[2]))});}
    // No discard is needed for fully opaque materials, allowing early
    // depth rejection on the common wall/floor/ceiling path.
    const bool opaque=g.texture_filter==0&&g.opaque_palette&&mode==0;
    if(g.deferred){
        SDL_Rect scissor{};
        if(large_mask)scissor=g.mask_scissor(data,image,*cached.ink,bounds);
        g.commands.push_back({id,static_cast<GLint>(g.world_vertices.size()/7),static_cast<GLsizei>(data.size()/7),image.width,image.height,mode,blend,opaque,scissor});
        g.world_vertices.insert(g.world_vertices.end(),data.begin(),data.end());
    }else{
        g.reverse_depth_order=false;
        const auto& program=g.world_program(g.texture_filter,opaque);
        g.UseProgram(program.id);g.Uniform1i(program.batch,0);g.Uniform1i(program.masked,mode);
        g.Uniform1i(program.filter,g.texture_filter);
        g.Uniform2f(program.size,float(image.width),float(image.height));
        if(blend){g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}else g.Disable(GL_BLEND);
        g.draw_quad(data);
    }
}
void GlRenderer::image(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect r,bool screen_pixels,int filter,
                       std::uint64_t revision){
    if(width<=0||height<=0||pixels.size()<std::size_t(width)*height)
        throw std::invalid_argument("Invalid GL image size");
    if(filter==3&&r.w<=width&&r.h<=height)filter=0;
    flush_world();
    auto& g=*impl_;
    const auto key=(std::uint64_t(width)<<32)|unsigned(height);
    auto& texture=g.ui_textures[key];const auto count=std::size_t(width)*height;
    if(!texture.id){
        g.GenTextures(1,&texture.id);g.rgba(texture.id,0,width,height,pixels.data());
        texture.pixels.assign(pixels.begin(),pixels.begin()+count);
        texture.bounds=g.alpha_bounds(pixels.data(),width,height);
        texture.opaque=std::all_of(pixels.begin(),pixels.begin()+count,[](auto c){return (c>>24)==255;});
    }
    else if((!revision||texture.revision!=revision)&&std::memcmp(texture.pixels.data(),pixels.data(),count*sizeof(std::uint32_t))!=0){
        const auto area=g.changed_area(texture.pixels.data(),pixels.data(),width,height);
        g.rgba(texture.id,0,width,height,pixels.data(),&area);std::copy_n(pixels.data(),count,texture.pixels.data());
        texture.bounds=g.alpha_bounds(pixels.data(),width,height);
        texture.opaque=std::all_of(pixels.begin(),pixels.begin()+count,[](auto c){return (c>>24)==255;});
    }else{g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,texture.id);}
    texture.revision=revision;
    const auto bounds=texture.bounds;
    if(bounds.w==0)return;
    const bool crop=filter==0&&r.w>0&&r.h>0&&(bounds.w!=width||bounds.h!=height);
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
    g.UseProgram(g.ui.id);g.Uniform1i(g.ui.filter,filter);
    g.Uniform2f(g.ui.size,float(width),float(height));
    g.Uniform2f(g.ui.output_scale,float(r.w)/width,float(r.h)/height);
    g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter?GL_LINEAR:GL_NEAREST);
    g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter?GL_LINEAR:GL_NEAREST);
    g.Disable(GL_DEPTH_TEST);
    if(texture.opaque)g.Disable(GL_BLEND);
    else{g.Enable(GL_BLEND);g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    const float rw=screen_pixels?float(g.width):320.f,rh=screen_pixels?float(g.height):240.f;
    const float l=2.f*r.x/rw-1,rr=2.f*(r.x+r.w)/rw-1,t=1-2.f*r.y/rh,b=1-2.f*(r.y+r.h)/rh;
    if(crop){
        // Keep the original quad and sampling coordinates. Scissor only the
        // fully transparent margins of nearest-sampled overlays.
        const double sx=double(g.width)/rw,sy=double(g.height)/rh;
        const auto x=[&](int p){return (r.x+double(r.w)*p/width)*sx;};
        const auto y=[&](int p){return (r.y+double(r.h)*p/height)*sy;};
        const int x0=std::clamp(int(std::floor(x(bounds.x))),0,g.width);
        const int x1=std::clamp(int(std::ceil(x(bounds.x+bounds.w))),x0,g.width);
        const int y0=std::clamp(int(std::floor(y(bounds.y))),0,g.height);
        const int y1=std::clamp(int(std::ceil(y(bounds.y+bounds.h))),y0,g.height);
        g.Enable(GL_SCISSOR_TEST);g.Scissor(x0,g.height-y1,x1-x0,y1-y0);
    }
    g.draw_quad({l,t,0,1,0,0,0,rr,t,0,1,1,0,0,rr,b,0,1,1,1,0,l,b,0,1,0,1,0});
    if(crop)g.Disable(GL_SCISSOR_TEST);
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
