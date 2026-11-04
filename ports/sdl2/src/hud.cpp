#include "dok/hud.hpp"
#include <algorithm>
#include <stdexcept>
namespace dok {
namespace {
void pixel(IndexedSurface& s,int x,int y,std::uint8_t c){
    if(x>=0&&y>=0&&x<s.width()&&y<s.height())s.pixels[y*s.width()+x]=c;
}
void image(IndexedSurface& s,const Image& a,int x,int y,int w,int h,bool transparent=true){
    if(w<=0||h<=0)return;
    for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx){
        const auto sy=h>1?(std::uint64_t(dy)*((std::uint64_t(a.height-1)<<16)/(h-1))>>16):0;
        const auto sx=w>1?(std::uint64_t(dx)*((std::uint64_t(a.width-1)<<16)/(w-1))>>16):0;
        const auto c=a.pixels[sy*a.width+sx];
        if(c||!transparent)pixel(s,x+dx,y+dy,c);
    }
}
void bar(IndexedSurface& s,const Image& icon,int y,int value,int maximum){
    if(value<=0||maximum<=0)return;
    const auto count=std::clamp(value*10/maximum,1,10);
    const int w=8*icon.width/icon.height,step=13;
    const int x=11+(w<step?(130-9*step-w)/2:0);
    for(int i=0;i<count;++i)image(s,icon,x+i*step,y,w,8);
}
}
Hud::Hud(const Archive& a){
    old_gold_=a.identify()==DatasetId::old_gold;
    if(old_gold_)money_=a.image("\x8f\x84\x85\x8d\x9c\x83\x88");
    panel_=a.image("\x8f\x80\x8d\x85\x8b\x9c");
    health_=a.image("\x8f\x86\x88\x87\x8d\x9c");charge_=a.image("\x8f\x8c\x80\x83\x88\x9f");
    keys_={a.image("\x8f\x8a\x8b\x9e\x97\x8c"),a.image("\x8f\x8a\x8b\x9e\x97\x86"),a.image("\x8f\x8a\x8b\x9e\x97\x87")};
    weapons_={a.image("\x8f\x8e\x90\x93\x86\x88\x85\x31"),a.image("\x8f\x8e\x90\x93\x86\x88\x85\x32")};
    const auto f=a.font("FONT8");std::copy(f.data,f.data+f.size,font_.begin());
}
void Hud::text(IndexedSurface& s,int x,int y,const std::string& value)const{
    for(unsigned char c:value){
        for(int row=0;row<8;++row)for(int bit=0;bit<8;++bit)if(font_[c*8+row]&(0x80>>bit)){
            pixel(s,x+bit+1,y+row+1,0);pixel(s,x+bit,y+row,2);
        }
        x+=8;
    }
}
IndexedSurface Hud::classic(const IndexedSurface& scene,const PlayerState& p,const std::string& message)const{
    if(scene.width()!=320||scene.height()!=(old_gold_?170:160))throw std::invalid_argument("Classic HUD requires native VGA scene dimensions");
    IndexedSurface out(320,200);std::copy(scene.pixels.begin(),scene.pixels.end(),out.pixels.begin());
    image(out,panel_,0,old_gold_?171:161,320,old_gold_?29:39,false);
    text(out,4,old_gold_?173:164,message.substr(0,38));
    if(old_gold_){
        const auto icons=[&](const Image& icon,int x,int value,int maximum){
            const int count=value>0&&maximum>0?std::clamp(value*5/maximum,1,5):0;
            for(int i=0;i<count;++i)image(out,icon,x+i*(icon.width+2),180,icon.width,icon.height);
        };
        icons(health_,4,p.health(),p.max_health());icons(charge_,70,p.charge(),p.max_charge());
        image(out,money_,136,180,16,15);text(out,155,184,std::to_string(p.score)+"/105");
        for(int i=0;i<2;++i)if(p.weapons[i+1])image(out,weapons_[i],228+20*i,180,weapons_[i].width,weapons_[i].height);
        for(int i=0;i<3;++i)if(p.keys()&(1<<i))image(out,keys_[i],288+11*i,180,keys_[i].width,keys_[i].height);
        return out;
    }
    bar(out,health_,177,p.health(),p.max_health());bar(out,charge_,188,p.charge(),p.max_charge());
    for(int i=0;i<3;++i)if(p.keys()&(1<<i))image(out,keys_[i],205+17*i,178,13,15);
    for(int i=0;i<2;++i)if(p.weapons[i+1])image(out,weapons_[i],i?263:150,177,41,18);
    return out;
}
IndexedSurface Hud::classic_mode(const IndexedSurface& scene,int frame_height,const PlayerState& p,const std::string& message)const{
    if(scene.width()==320&&frame_height==200)return classic(scene,p,message);
    IndexedSurface out(scene.width(),frame_height);
    std::copy(scene.pixels.begin(),scene.pixels.end(),out.pixels.begin());
    // Keep the accepted classic layout; only its panel is scaled, never the rendered world.
    const int panel_height=old_gold_?29:39;
    const auto panel=classic(IndexedSurface(320,old_gold_?170:160),p,message);
    const int start=frame_height-int(float(panel_height)*(float(frame_height)/200));
    std::vector<int> columns(out.width());
    for(int x=0;x<out.width();++x)columns[x]=x*320/out.width();
    int previous=-1;
    for(int y=start;y<frame_height;++y){
        const int row=200-panel_height+(y-start)*panel_height/(frame_height-start);
        auto* dst=out.pixels.data()+y*out.width();
        if(row==previous)std::copy_n(dst-out.width(),out.width(),dst);
        else for(int x=0;x<out.width();++x)dst[x]=panel.pixels[row*320+columns[x]];
        previous=row;
    }
    return out;
}
std::vector<HudPanel> Hud::overlay(IndexedSurface& s,const PlayerState& p,const std::string& message)const{
    if(s.width()<160||s.height()<160)throw std::invalid_argument("Overlay HUD requires at least160x160");
    const int key_height=p.keys()?18:0;
    const int height=42+key_height+(old_gold_?18:0),top=s.height()-height-4;
    std::vector<HudPanel> panels{{4,top,116,height}};
    image(s,health_,10,top+6,15,10);text(s,30,top+8,std::to_string(std::max(0,int(p.health()))));
    image(s,charge_,10,top+22,10,11);text(s,30,top+24,std::to_string(std::max(0,int(p.charge()))));
    for(int i=0;i<3;++i)if(p.keys()&(1<<i))image(s,keys_[i],10+18*i,top+40,13,15);
    for(int i=0;i<2;++i)if(p.weapons[i+1]){
        const int x=78,y=top+6+18*i;
        image(s,weapons_[i],x,y,34,14);
        if(p.current_weapon==i+1)for(int dx=0;dx<34;++dx)pixel(s,x+dx,y+15,2);
    }
    if(old_gold_){image(s,money_,10,top+40+key_height,16,15);text(s,30,top+43+key_height,std::to_string(p.score)+"/105");}
    const int columns=std::min(38,(s.width()-16)/8);
    const auto message_text=message.substr(0,38);
    const int rows=(static_cast<int>(message_text.size())+columns-1)/columns;
    if(rows){
        const int y=top-rows*10-12;
        panels.push_back({4,y,std::min(columns,static_cast<int>(message_text.size()))*8+8,rows*10+8});
        for(int row=0;row<rows;++row)text(s,8,y+4+row*10,message_text.substr(row*columns,columns));
    }
    return panels;
}
}
