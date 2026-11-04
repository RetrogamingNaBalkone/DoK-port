#include "dok/automap.hpp"
#include <algorithm>
namespace dok {
IndexedSurface native_automap(const World& world){
    const auto& d=world.definition;const bool og=d.dataset==DatasetId::old_gold;
    const int step=og?2:3,ox=og?0:64,oy=og?0:7;
    IndexedSurface out(320,200);
    auto object=[&](const char* plane,int x,int z)->const ObjectDefinition*{
        if(x<0||z<0||x>=d.width||z>=d.length)return nullptr;
        return d.object(world.maps.at(plane)[z*d.width+x]);};
    auto cls=[&](const char* plane,int x,int z){const auto* o=object(plane,x,z);return o?int(o->class_id):-1;};
    auto marker=[&](const ObjectDefinition* o){if(!o)return std::uint8_t(0);auto c=o->marker;
        const auto& state=world.objects.at(d.glyph_index[o->glyph]);if(state.door&&state.door->state==1)c=~c;return c;};
    auto rect=[&](int x,int y,int x2,int y2,std::uint8_t color){for(int z=std::max(0,y);z<=std::min(199,y2);++z)for(int a=std::max(0,x);a<=std::min(319,x2);++a)out.pixels[z*320+a]=color;};
    const auto* empty=d.object(' ');const auto* solid=d.object(219);
    rect(0,0,og?d.width*step:319,og?d.length*step:199,marker(d.object(og?'M':' ')));
    int hx=-1,hz=-1,heading=0;std::uint8_t hero_color=0;
    if(world.player){const auto& hero=world.objects.at(*world.player);hx=hero.pose->x/d.cell_side;hz=hero.pose->z/d.cell_side;heading=hero.pose->heading;hero_color=d.objects[hero.definition_index].marker;}
    auto seen=[&](int x,int z){return x>=0&&z>=0&&x<d.width&&z<d.length&&(world.visited[z*d.width+x]||(x==hx&&z==hz));};
    for(int z=0;z<d.length;++z)for(int x=0;x<d.width;++x)if(seen(x,z)){
        const auto c=marker(object("fcmap",x,z));if(c!=marker(empty)&&c!=marker(solid))rect(ox+(d.width-x-1)*step,oy+z*step,ox+(d.width-x)*step,oy+(z+1)*step,c);
    }
    for(int z=0;z<d.length;++z)for(int x=0;x<d.width;++x)if(seen(x,z)){
        auto* o=object("mmap",x,z);if(!o||o->class_id==0)o=object("tmap",x,z);if(!o)continue;
        const int right=ox+(d.width-x)*step,left=right-step,top=oy+z*step,bottom=top+step,c=o->class_id;const auto color=marker(o);
        if(c==2){
            if(seen(x-1,z)&&cls("tmap",x-1,z)!=2)rect(right,top,right,bottom,color);
            if(seen(x,z-1)&&cls("tmap",x,z-1)!=2)rect(left,top,right,top,color);
            if(seen(x+1,z)&&cls("tmap",x+1,z)!=2)rect(left,top,left,bottom,color);
            if(seen(x,z+1)&&cls("tmap",x,z+1)!=2)rect(left,bottom,right,bottom,color);
        }else if(c==4||c==7){
            auto free=[&](int a,int b){const int n=cls("tmap",a,b);return n!=2&&n!=4&&n!=7;};
            if(free(x-1,z)&&free(x+1,z))rect(right-step/2,top+1,right-step/2,bottom,color);
            if(free(x,z-1)&&free(x,z+1))rect(left,top+step/2,right-1,top+step/2,color);
        }else if(c==1){if(og)rect(right-1,top+1,right-1,top+1,color);}
        else if(c==8||c==12||(world.map_revealed&&(c==3||c==6||c==10||c==13||c==9))){
            const auto& state=world.objects.at(d.glyph_index[o->glyph]);
            if(c!=9||og||!state.monster||!state.monster->corpse)rect(left+1,top+1,right-1,bottom-1,color);
        }
    }
    if(!og&&hx>=0){
        int dx=0,dz=1;if(heading>=20&&heading<65){dx=1;dz=1;}else if(heading>=65&&heading<110){dx=1;dz=0;}else if(heading>=110&&heading<155){dx=1;dz=-1;}else if(heading>=155&&heading<200){dx=0;dz=-1;}else if(heading>=200&&heading<245){dx=-1;dz=-1;}else if(heading>=245&&heading<290){dx=-1;dz=0;}else if(heading>=290&&heading<=340){dx=-1;dz=1;}
        const int x=ox+(d.width-hx-1)*step+2,y=oy+hz*step+2;rect(x-dx,y-dz,x-dx,y-dz,0);rect(x,y,x,y,0);rect(x+dx,y+dz,x+dx,y+dz,hero_color);
    }
    return out;
}
}
