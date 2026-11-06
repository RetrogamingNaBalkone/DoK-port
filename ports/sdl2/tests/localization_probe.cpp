#include "dok/localization.hpp"
#include "dok/frontend.hpp"
#include "dok/display.hpp"
#include "dok/world.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void key(dok::Frontend& menu,SDL_Keycode code,int w,int h){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=code;e.key.keysym.scancode=SDL_GetScancodeFromKey(code);menu.handle(e,w,h);menu.render(w,h);}
int main(int argc,char** argv){
    SDL_SetMainReady();if(argc!=3||SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0)return 2;
    int result=0;try{
        const auto root=std::filesystem::u8path(argv[1]),out=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(out);
        unsigned fields=0,stories=0,pages=0,renders=0;
        for(const char* path:{"originals/Old_Gold_3D/OLD_GOLD.RES","originals/pkr/RUS/PKR.RES","originals/pkr/ENG/PKE.RES"}){
            const auto a=dok::Archive::open(root/path);const auto dataset=a.identify();
            for(const auto& resource:a.entries())if(resource.name.rfind("LEVEL",0)==0){
                dok::World world(dok::load_level(a,resource.name));const auto maps=world.maps;
                for(const auto& object:world.definition.objects)for(const auto& f:object.fields)if(f.kind==dok::FieldKind::text){
                    world.notify(object,f.offset);check(!world.message_key.empty(),"Untranslated original object field");
                    for(auto locale:{dok::LocaleId::ru,dok::LocaleId::en}){
                        const auto text=dok::cp866(dok::game_text(world.message_key,locale));
                        check(!text.empty()&&text.size()<=38,"Gameplay message does not fit native width");
                        check(world.visible_message(locale)==text,"World message locale mismatch");
                    }
                    ++fields;
                }
                check(world.simulation_ticks()==0&&world.maps==maps,"Text switching changed the world");
            }
            for(const auto& resource:a.entries()){
                const auto semantic=dok::game_story_key(dataset,resource.name);if(semantic.empty())continue;
                for(auto locale:{dok::LocaleId::ru,dok::LocaleId::en}){
                    const auto encoded=dok::cp866(dok::game_text(semantic,locale));check(!encoded.empty(),"Missing story translation");
                    for(unsigned columns:{16u,32u,38u,72u})for(unsigned rows:{1u,4u,18u}){
                        const auto content=dok::paginate_story({reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()},columns,rows);
                        check(!content.empty(),"Empty translated story");
                        for(const auto& page:content){check(page.size()<=rows,"Story row overflow");for(const auto& line:page)check(line.size()<=columns,"Story column overflow");}
                        pages+=unsigned(content.size());
                    }
                    ++stories;
                }
            }
            for(int backend=0;backend<2;++backend)for(auto locale:{dok::LocaleId::ru,dok::LocaleId::en})for(auto size:{std::array<int,2>{320,200},std::array<int,2>{640,480},std::array<int,2>{1280,720}}){
                dok::AppSettings s;s.locale=locale;s.video.renderer=backend?dok::RendererKind::opengl:dok::RendererKind::software;s.video.width=size[0];s.video.height=size[1];
                dok::Display display(s.video,false);dok::Frontend menu(s);menu.datasets({{&a,"single.RES"}});menu.render(size[0],size[1]);
                key(menu,SDLK_RETURN,size[0],size[1]);check(menu.screen()==dok::MenuScreen::title,"Single RES should skip dataset selection");
                key(menu,SDLK_RETURN,size[0],size[1]);check(menu.screen()==dok::MenuScreen::dataset_menu,"Dataset menu route");
                key(menu,SDLK_RETURN,size[0],size[1]);check(menu.screen()==(dataset==dok::DatasetId::old_gold?dok::MenuScreen::levels:dok::MenuScreen::readme),"Story route");
                display.begin();const auto& frame=menu.render(size[0],size[1]);display.pixels(frame.pixels,frame.width,frame.height,false);
                if(size[0]==640){const std::string prefix=std::to_string(int(dataset))+"-"+(locale==dok::LocaleId::ru?"ru":"en")+"-"+(backend?"gl":"sw");display.screenshot((out/(prefix+"-story.png")).u8string());}
                display.present();
                int guard=0;while(menu.screen()==dok::MenuScreen::readme&&guard++<300)key(menu,SDLK_RETURN,size[0],size[1]);
                check(menu.screen()==dok::MenuScreen::levels,"Translated prologue must terminate");
                menu.ending();menu.render(size[0],size[1]);
                key(menu,SDLK_ESCAPE,size[0],size[1]);
                ++renders;
            }
        }
        std::cout<<"{\"original_object_fields\":"<<fields<<",\"localized_story_variants\":"<<stories<<",\"pagination_pages_checked\":"<<pages<<",\"single_res_backend_locale_size_cases\":"<<renders<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    SDL_Quit();return result;
}
