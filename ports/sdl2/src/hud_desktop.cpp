#include "dok/hud.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dok {
namespace {
struct Painter {
    HudFrame frame;
    const std::array<std::uint32_t,256>& palette;
    const std::array<std::uint8_t,2048>& font;
    void pixel(int x,int y,std::uint32_t color) {
        if(x<0||y<0||x>=frame.width||y>=frame.height)return;
        auto& dst=frame.pixels[y*frame.width+x];
        const unsigned a=color>>24,da=dst>>24,out=a+(da*(255-a)+127)/255;
        if(!out)return;
        std::uint32_t value=out<<24;
        for(int shift:{0,8,16}){
            const unsigned premul=((color>>shift)&255)*a+(((dst>>shift)&255)*da*(255-a)+127)/255;
            value|=std::min(255u,(premul+out/2)/out)<<shift;
        }
        dst=value;
    }
    void rect(int x,int y,int w,int h,std::uint32_t color) {
        for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx)pixel(x+dx,y+dy,color);
    }
    void shadow(int x,int y,int w,int h) {
        for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx){
            const int edge=std::min({dx,dy,w-1-dx,h-1-dy,10});
            pixel(x+dx,y+dy,std::uint32_t(edge*13)<<24);
        }
    }
    void image(const Image& source,int x,int y,int w,int h,unsigned alpha=255,double dim=0) {
        for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx){
            const auto index=source.pixels[(dy*source.height/h)*source.width+dx*source.width/w];
            if(!index)continue;
            auto color=palette[index]&0xffffff;
            if(dim){const unsigned r=(color>>16)&255,g=(color>>8)&255,b=color&255;
                const unsigned gray=(r*3+g*6+b)/10;
                const auto muted=[&](unsigned channel){return unsigned(std::lround(channel*(1-0.5*dim)+gray*0.35*dim));};
                color=(muted(r)<<16)|(muted(g)<<8)|muted(b);
            }
            pixel(x+dx,y+dy,(alpha<<24)|color);
        }
    }
    void text(int x,int y,const std::string& text,int scale,std::uint32_t color) {
        // Draw all shadows first so adjacent foreground glyphs remain intact.
        for(int pass=0;pass<2;++pass){int pen=x;
            for(unsigned char c:text){
                for(int row=0;row<8;++row)for(int bit=0;bit<8;++bit)
                    if(font[c*8+row]&(0x80>>bit))rect(pen+bit*scale+(pass?0:1),y+row*scale+(pass?0:1),scale,scale,pass?color:0xd0000000);
                pen+=8*scale;
            }
        }
    }
};
std::vector<std::string> wrap(std::string value,int columns) {
    std::vector<std::string> lines;
    while(!value.empty()){
        std::size_t count=std::min<std::size_t>(columns,value.size());
        if(count<value.size()){const auto space=value.rfind(' ',count);if(space!=std::string::npos&&space>0)count=space;}
        lines.push_back(value.substr(0,count));value.erase(0,count);
        while(!value.empty()&&value.front()==' ')value.erase(0,1);
    }
    return lines;
}
}

const HudFrame& Hud::desktop(int width,const PlayerState& p,const std::array<std::uint32_t,256>& palette,
                      const std::string& message,double seconds) {
    if(width<320)throw std::invalid_argument("Desktop HUD requires width >=320");
    const int hp=std::max(0,int(p.health())),ammo=std::max(0,int(p.charge()));
    int active=p.current_weapon>=1&&p.current_weapon<=2&&p.weapons[p.current_weapon]?p.current_weapon:0;
    if(!active)for(int i=1;i<=2;++i)if(p.weapons[i]){active=i;break;}
    if(previous_health_>=0&&hp!=previous_health_){health_delta_=hp-previous_health_;health_changed_=seconds;}
    if(previous_weapon_>=0&&active!=previous_weapon_){old_weapon_=previous_weapon_;weapon_changed_=seconds;}
    previous_health_=hp;previous_weapon_=active;
    const double health_age=seconds-health_changed_;
    const double t=std::clamp((seconds-weapon_changed_)/0.18,0.0,1.0);
    const bool animated=health_age<1.0||t<1.0;
    if(desktop_valid_&&desktop_frame_.width==width&&desktop_player_.attributes==p.attributes&&
       desktop_player_.weapons==p.weapons&&desktop_player_.current_weapon==p.current_weapon&&
       desktop_player_.score==p.score&&desktop_message_==message&&desktop_palette_==palette&&
       desktop_animated_==animated&&(!animated||desktop_seconds_==seconds))return desktop_frame_;
    Painter out{{width,100,std::vector<std::uint32_t>(std::size_t(width)*100)},palette,font_};
    constexpr std::uint32_t bone=0xffe7dfc9,amber=0xffe3bd7c;
    const auto health_text=std::to_string(hp),ammo_text=std::to_string(ammo);
    // Never reserve five digits for ordinary values; do expand for real extremes.
    const int health_right=64+std::max(3,int(health_text.size()))*24;
    out.shadow(8,30,health_right,60);
    out.image(health_,18,42,38,26);
    out.text(64,42,health_text,3,hp<=p.max_health()/4?0xffffa18a:bone);
    const double ratio=p.max_health()>0?std::clamp(double(hp)/p.max_health(),0.0,1.0):0;
    const double trail=p.max_health()>0?std::clamp(double(hp-health_delta_)/p.max_health(),0.0,1.0):ratio;
    for(int i=0;i<10;++i){
        std::uint32_t color=0x704d3430;
        if((i+0.5)/10<=ratio)color=0xffc4493e;
        else if(health_delta_<0&&health_age<0.45&&(i+0.5)/10<=trail)color=0xffdcaa77;
        out.rect(64+i*7,74,5,3,color);
    }
    if(health_age>=0&&health_age<1.0){
        const auto alpha=unsigned(255*std::min(1.0,(1.0-health_age)*2));
        out.text(64,22,(health_delta_>0?"+":"")+std::to_string(health_delta_),1,
                 (alpha<<24)|(health_delta_>0?0xb9db8c:0xffa18a));
    }

    const int deck_x=health_right+18;
    const double ease=t*t*(3-2*t);
    auto card=[&](int slot,bool front){
        if(slot<1||slot>2||!p.weapons[slot])return;
        double back=front?0:1;
        if(t<1&&old_weapon_>=1&&old_weapon_<=2&&p.weapons[old_weapon_])
            back=slot==active?1-ease:(slot==old_weapon_?ease:back);
        const int x=deck_x+int(std::lround(14*back)),y=36-int(std::lround(12*back));
        const unsigned alpha=unsigned(255-55*back);
        // Original panel art is itself the card; no replacement icon artwork.
        out.image(weapons_[slot-1],x,y,85,39,alpha,back);
        if(back>0){
            const auto edge=(unsigned(180*back)<<24)|0xb49a66;
            out.rect(x+2,y,80,1,edge);out.rect(x+84,y+2,1,35,edge);
        }
        if(front)out.rect(x+3,y+40,78,2,0xffb49a66);
    };
    if(active){card(active==1?2:1,false);card(active,true);}

    const int ammo_x=deck_x+(active?105:8);
    out.shadow(ammo_x-8,30,40+std::max(3,int(ammo_text.size()))*24,60);
    out.image(charge_,ammo_x,44,17,18);
    out.text(ammo_x+25,42,ammo_text,3,amber);
    for(int i=0;i<3;++i)if(p.keys()&(1<<i))out.image(keys_[i],ammo_x+25+i*19,68,13,15);
    int right=ammo_x+25+std::max(3,int(ammo_text.size()))*24;
    if(old_gold_){
        const auto money_text=std::to_string(p.score)+"/105";
        out.image(money_,right+16,46,20,19);out.text(right+42,48,money_text,2,amber);
        right+=42+int(money_text.size())*16;
    }
    if(!message.empty()){
        int x=right+26,y=42;
        int scale=3; // Same FONT8 scale as health and charge numerals.
        int columns=std::min(24,(width-x-24)/(8*scale));
        // Safe fallback for small desktop windows; portable layout is separate work.
        if(columns<18){scale=2;columns=std::min(24,(width-x-24)/(8*scale));}
        if(columns<12){x=18;y=2;scale=1;columns=std::max(1,std::min(38,(width-36)/8));}
        const auto lines=wrap(message.substr(0,38),columns);
        if(scale>1)y=std::max(6,std::min(42,80-(int(lines.size())*(8*scale+3)-3)));
        int longest=0;for(const auto& line:lines)longest=std::max(longest,int(line.size()));
        const int box_width=longest*8*scale+20,box_height=int(lines.size())*(8*scale+3)+12;
        for(int dy=0;dy<box_height;++dy)for(int dx=0;dx<box_width;++dx){
            const int fade=std::min(24,box_width-1-dx);
            out.pixel(x-10+dx,y-6+dy,std::uint32_t(160*fade/24)<<24);
        }
        out.rect(x-10,y-3,2,box_height-6,0xb0b49a66);
        for(std::size_t row=0;row<lines.size();++row)out.text(x,y+int(row)*(8*scale+3),lines[row],scale,bone);
    }
    desktop_frame_=std::move(out.frame);desktop_player_=p;desktop_palette_=palette;desktop_message_=message;
    desktop_valid_=true;desktop_animated_=animated;desktop_seconds_=seconds;
    return desktop_frame_;
}
}
