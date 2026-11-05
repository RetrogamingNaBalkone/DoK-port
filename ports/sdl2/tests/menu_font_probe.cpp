#include "dok/frontend.hpp"
#include "dok/png.hpp"
#include "dok/display.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void key(dok::Frontend& menu,SDL_Keycode code){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=code;e.key.keysym.scancode=SDL_GetScancodeFromKey(code);menu.handle(e,640,480);menu.render(640,480);}
void shot(dok::Frontend& menu,int width,int height,const std::filesystem::path& path){
    const auto& frame=menu.render(width,height);std::vector<std::uint32_t> pixels(width*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[y*width+x]=frame.pixels[(y*frame.height/height)*frame.width+x*frame.width/width];
    dok::save_png(path,width,height,pixels);
}
}
int main(int argc,char** argv){
    SDL_SetMainReady();if((argc!=3&&argc!=4)||SDL_Init(SDL_INIT_EVENTS|(argc==4?SDL_INIT_VIDEO:0))!=0)return 2;
    try{
        const std::filesystem::path root=std::filesystem::u8path(argv[1]),out=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(out);
        check(dok::cp866(u8"ЕЁеё")==std::string("\x85\xf0\xa5\xf1",4),"Yo UTF8 mapping");
        const char* paths[]={"originals/pkr/RUS/PKR.RES","originals/pkr/ENG/PKE.RES","originals/Old_Gold_3D/OLD_GOLD.RES"};
        unsigned frames=0,readbacks=0;
        for(int dataset=-1;dataset<3;++dataset){
            dok::AppSettings settings;settings.locale=dok::LocaleId::ru;dok::Frontend menu(settings);
            std::optional<dok::Archive> archive;if(dataset>=0){archive=dok::Archive::open(root/paths[dataset]);menu.datasets({{&*archive,"source.RES"}});
                const auto original=archive->font("FONT16");std::array<std::uint8_t,4096> patched{};std::copy_n(original.data,4096,patched.begin());dok::menu_yo16(patched);
                for(int c=0;c<256;++c)if(c!=0xf0&&c!=0xf1)check(std::equal(original.data+c*16,original.data+(c+1)*16,patched.begin()+c*16),"Unexpected font mutation");
                for(int y=5;y<=12;++y)check(patched[0xf1*16+y]==original.data[0xa5*16+y],"Lowercase yo body differs from e");
                check(patched[0xf1*16+2]==0x6c&&patched[0xf1*16+3]==0x6c&&!patched[0xf1*16+4],"Lowercase yo diaeresis");
                for(int y=1;y<=12;++y)check(patched[0xf0*16+y+2]==original.data[0x85*16+y],"Uppercase Yo body differs from E");
                const auto small=archive->font("FONT8");
                // Contact sheet: Е Ё е ё; source FONT8, source FONT16, menu FONT16.
                const int w=256,h=192;std::vector<std::uint32_t> pixels(w*h,0xff101010);
                const int chars[]={0x85,0xf0,0xa5,0xf1};
                for(int band=0;band<3;++band)for(int i=0;i<4;++i){const int rows=band?16:8;const auto* font=band==0?small.data:band==1?original.data:patched.data();
                    for(int y=0;y<rows;++y)for(int x=0;x<8;++x)if(font[chars[i]*rows+y]&(128>>x))for(int dy=0;dy<3;++dy)for(int dx=0;dx<3;++dx)pixels[(band*64+y*3+dy)*w+i*64+12+x*3+dx]=0xffeeeeee;
                }
                dok::save_png(out/(std::to_string(dataset)+"-source-vs-menu.png"),w,h,pixels);
                for(int input=0;input<3;++input){
                    menu.main_menu();key(menu,SDLK_RETURN);
                    check(menu.screen()==dok::MenuScreen::title,"Splash route");
                    key(menu,SDLK_DOWN);check(menu.focused_row()==0,"Splash must have no invisible menu selection");
                    if(input==0)key(menu,SDLK_RETURN);
                    else{SDL_Event next{};
                        if(input==1){next.type=SDL_MOUSEBUTTONDOWN;next.button.button=SDL_BUTTON_LEFT;next.button.x=5;next.button.y=5;}
                        else{next.type=SDL_CONTROLLERBUTTONDOWN;next.cbutton.button=SDL_CONTROLLER_BUTTON_A;}
                        menu.handle(next,640,480);menu.render(640,480);
                    }
                    check(menu.screen()==(dataset==2?dok::MenuScreen::levels:dok::MenuScreen::readme),"Splash proceeds by keyboard, mouse and pad");
                    if(dataset!=2){
                        key(menu,SDLK_DOWN);check(menu.focused_row()==0,"README must have no invisible menu selection");
                        for(int page=0;menu.screen()==dok::MenuScreen::readme&&page<100;++page)key(menu,SDLK_RETURN);
                        check(menu.screen()==dok::MenuScreen::levels,"README pages must reach level selection");
                    }
                    menu.main_menu();key(menu,SDLK_RETURN);key(menu,SDLK_ESCAPE);
                    check(menu.screen()==dok::MenuScreen::main,"Splash Escape returns to parent");
                }
            }
            for(auto size:{std::array<int,2>{320,240},{640,480},{1280,720},{1920,1080}}){
                const auto prefix=std::to_string(dataset)+"-"+std::to_string(size[0]);
                menu.main_menu();menu.notice(u8"Е Ё е ё   Вперёд   Поворот влево","");shot(menu,size[0],size[1],out/(prefix+"-comparison.png"));++frames;
                menu.main_menu();key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_DOWN);key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);
                check(menu.screen()==dok::MenuScreen::bindings,"Bindings route");shot(menu,size[0],size[1],out/(prefix+"-bindings.png"));++frames;
            }
            if(dataset>=0)for(int lang=0;lang<2;++lang)for(auto size:{std::array<int,2>{320,200},{640,400},{640,480},{800,600},{1024,768},{1280,720},{1920,1080},{2560,1440},{1920,1440}}){
                settings.locale=lang?dok::LocaleId::en:dok::LocaleId::ru;
                const auto prefix=std::string(lang?"layout-en-":"layout-")+std::to_string(dataset)+"-"+std::to_string(size[0])+"x"+std::to_string(size[1]);
                auto save=[&](const char* name){shot(menu,size[0],size[1],out/(prefix+"-"+name+".png"));++frames;};
                menu.main_menu();save("main");
                for(int i=0;i<3;++i)key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);save("credits");
                menu.main_menu();key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);save("graphics");
                key(menu,SDLK_RIGHT);key(menu,SDLK_ESCAPE);save("video-unsaved");
                key(menu,SDLK_RETURN);key(menu,SDLK_RIGHT);
                menu.pause();save("pause");menu.outcome(dok::SessionPhase::retry);save("outcome");
                if(size[0]==320){key(menu,SDLK_DOWN);const auto& f=menu.render(320,200);const auto selected=std::find(f.pixels.begin(),f.pixels.end(),0xff302b20);check(selected!=f.pixels.end(),"Second outcome row must be visible");const auto at=selected-f.pixels.begin();SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;click.button.x=int(at%f.width)+8;click.button.y=int(at/f.width)+4;menu.handle(click,320,200);check(menu.screen()==dok::MenuScreen::main,"Second outcome row must be clickable at 320x200");}
                menu.main_menu();key(menu,SDLK_RETURN);save("title");key(menu,SDLK_RETURN);save("readme");
                menu.preview_level(1);save("intro");
                if(size[0]==320){SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;click.button.x=30;click.button.y=158;menu.handle(click,320,200);check(!menu.active(),"Intro Start mouse hit at 320x200");}
                menu.ending();save("ending");
            }
            if(argc==4&&dataset>=0)for(int backend=0;backend<2;++backend)for(int lang=0;lang<2;++lang)for(auto size:{std::array<int,2>{320,200},{640,400},{2560,1440},{1920,1440}}){
                settings.locale=lang?dok::LocaleId::en:dok::LocaleId::ru;settings.video.renderer=backend?dok::RendererKind::opengl:dok::RendererKind::software;settings.video.width=size[0];settings.video.height=size[1];settings.video.fullscreen=false;
                dok::Display display(settings.video,false);
                const auto prefix="readback-"+std::to_string(dataset)+(backend?"-gl":"-sw")+(lang?"-en-":"-ru-")+std::to_string(size[0])+"x"+std::to_string(size[1]);
                auto save=[&](const char* name){const auto actual=display.size();const auto& frame=menu.render(actual[0],actual[1]);display.begin();display.pixels(frame.pixels,frame.width,frame.height,false);display.screenshot((out/(prefix+"-"+name+".png")).u8string());display.present();++readbacks;};
                menu.main_menu();key(menu,SDLK_RETURN);save("title");key(menu,SDLK_RETURN);save("readme");
                menu.main_menu();for(int i=0;i<3;++i)key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);save("credits");
                menu.preview_level(1);save("intro");menu.outcome(dok::SessionPhase::retry);save("outcome");
                menu.main_menu();key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_DOWN);key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);save("bindings");
                menu.main_menu();key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);save("graphics");
                key(menu,SDLK_RIGHT);key(menu,SDLK_ESCAPE);save("video-unsaved");
            }
        }
        std::ofstream report(out/"report.json");report<<"{\"menu_frames\":"<<frames<<",\"readback_frames\":"<<readbacks<<",\"font_contact_sheets\":3,\"source_fonts\":6,\"no_res\":true,\"audio\":false}\n";
        std::cout<<"PASS "<<frames<<" quiet CPU menu frames, "<<readbacks<<" GL/software readbacks, 6 source fonts, 3 glyph comparisons\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
    SDL_Quit();return 0;
}
