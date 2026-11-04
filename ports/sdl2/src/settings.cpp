#include "dok/settings.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <map>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace dok {
LocaleId system_locale(){
    SDL_Locale* locales=SDL_GetPreferredLocales();
    const bool russian=locales&&locales[0].language&&std::string(locales[0].language)=="ru";
    SDL_free(locales);return russian?LocaleId::ru:LocaleId::en;
}
bool operator==(const VideoSettings& a,const VideoSettings& b){return a.renderer==b.renderer&&a.width==b.width&&a.height==b.height&&a.software_mode==b.software_mode&&a.fullscreen==b.fullscreen;}
namespace {
using Values=std::map<std::string,std::string>;
std::string trim(std::string s){const auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(" \t\r")-a+1);}
Values values(const std::string& text){
    Values result;std::istringstream in(text);std::string line,section;
    while(std::getline(in,line)){
        line=trim(line);if(line.empty()||line[0]=='#'||line[0]==';')continue;
        if(line.front()=='['&&line.back()==']'){section=line.substr(1,line.size()-2);continue;}
        const auto eq=line.find('=');if(eq==std::string::npos)throw std::runtime_error("Invalid settings line");
        const auto key=section+"."+trim(line.substr(0,eq));
        if(!result.emplace(key,trim(line.substr(eq+1))).second)throw std::runtime_error("Duplicate settings entry");
    }return result;
}
int integer(const Values& m,const std::string& key,int fallback,int lo,int hi){
    const auto it=m.find(key);if(it==m.end())return fallback;
    std::size_t n=0;int v=std::stoi(it->second,&n);if(n!=it->second.size()||v<lo||v>hi)throw std::runtime_error("Invalid setting: "+key);return v;
}
float number(const Values& m,const std::string& key,float fallback,float lo,float hi){
    const auto it=m.find(key);if(it==m.end())return fallback;
    std::size_t n=0;float v=std::stof(it->second,&n);if(n!=it->second.size()||!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error("Invalid setting: "+key);return v;
}
}
std::string encode_settings(const AppSettings& s){
    std::ostringstream o;o<<"[settings]\nversion=1\n[video]\nrenderer="<<int(s.video.renderer)<<"\nwidth="<<s.video.width<<"\nheight="<<s.video.height
      <<"\nsoftware_mode="<<s.video.software_mode<<"\nfullscreen="<<s.video.fullscreen<<"\nlinear="<<s.linear
      <<"\n[interface]\nlocale="<<int(s.locale)<<"\nfps="<<s.show_fps<<"\nlast_dataset="<<int(s.last_dataset)
      <<"\n[gameplay]\ninertia="<<s.inertia<<"\n[audio]\nmaster="<<s.master_volume<<"\neffects="<<s.effects_volume<<"\nmusic="<<s.music_volume
      <<"\n[input]\nkeyboard_preset="<<int(s.input.keyboard_preset)<<"\npad_preset="<<int(s.input.pad_preset)<<"\nclassic_pad="<<s.input.classic_pad
      <<"\nmouse_sensitivity="<<s.input.mouse_degrees_per_count<<"\nstick_sensitivity="<<s.input.turn_sensitivity<<"\ndeadzone="<<s.input.stick_deadzone<<'\n';
    for(unsigned i=0;i<static_cast<unsigned>(Action::count);++i){
        o<<"key"<<i<<"a="<<int(s.input.keys[i][0])<<"\nkey"<<i<<"b="<<int(s.input.keys[i][1])
         <<"\nmouse"<<i<<'='<<s.input.mouse[i]<<"\nbutton"<<i<<'='<<int(s.input.buttons[i])<<"\ntrigger"<<i<<'='<<int(s.input.triggers[i])<<'\n';
    }return o.str();
}
AppSettings load_settings(const std::filesystem::path& file,std::string& warning){
    warning.clear();AppSettings s;
    try{
        if(!std::filesystem::exists(file))return s;
        if(std::filesystem::file_size(file)>65536)throw std::runtime_error("Settings file too large");
        std::ifstream f(file,std::ios::binary);if(!f)throw std::runtime_error("Cannot read settings");
        std::ostringstream text;text<<f.rdbuf();const auto m=values(text.str());
        if(integer(m,"settings.version",0,1,1)!=1)throw std::runtime_error("Unsupported settings version");
        s.video.renderer=RendererKind(integer(m,"video.renderer",int(s.video.renderer),0,1));
        s.video.width=integer(m,"video.width",1280,320,7680);s.video.height=integer(m,"video.height",720,200,4320);
        s.video.software_mode=integer(m,"video.software_mode",0,0,4);s.video.fullscreen=integer(m,"video.fullscreen",0,0,1);
        s.linear=integer(m,"video.linear",0,0,1);s.locale=LocaleId(integer(m,"interface.locale",int(s.locale),0,1));
        s.show_fps=integer(m,"interface.fps",1,0,1);s.last_dataset=DatasetId(integer(m,"interface.last_dataset",0,0,3));
        s.inertia=integer(m,"gameplay.inertia",1,0,1);s.master_volume=integer(m,"audio.master",100,0,100);
        s.effects_volume=integer(m,"audio.effects",100,0,100);s.music_volume=integer(m,"audio.music",100,0,100);
        s.input.keyboard_preset=InputPreset(integer(m,"input.keyboard_preset",0,0,2));s.input.pad_preset=InputPreset(integer(m,"input.pad_preset",0,0,2));
        s.input.classic_pad=integer(m,"input.classic_pad",0,0,1);
        s.input.mouse_degrees_per_count=number(m,"input.mouse_sensitivity",0.15f,0.01f,2);
        s.input.turn_sensitivity=number(m,"input.stick_sensitivity",1,0.1f,3);s.input.stick_deadzone=number(m,"input.deadzone",0.18f,0,0.8f);
        for(unsigned i=0;i<static_cast<unsigned>(Action::count);++i){
            const auto n=std::to_string(i);
            s.input.keys[i][0]=SDL_Scancode(integer(m,"input.key"+n+"a",s.input.keys[i][0],0,SDL_NUM_SCANCODES-1));
            s.input.keys[i][1]=SDL_Scancode(integer(m,"input.key"+n+"b",s.input.keys[i][1],0,SDL_NUM_SCANCODES-1));
            s.input.mouse[i]=integer(m,"input.mouse"+n,s.input.mouse[i],0,5);
            s.input.buttons[i]=SDL_GameControllerButton(integer(m,"input.button"+n,s.input.buttons[i],-1,SDL_CONTROLLER_BUTTON_MAX-1));
            const int trigger=integer(m,"input.trigger"+n,s.input.triggers[i],-1,SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            if(trigger!=-1&&trigger!=SDL_CONTROLLER_AXIS_TRIGGERLEFT&&trigger!=SDL_CONTROLLER_AXIS_TRIGGERRIGHT)throw std::runtime_error("Invalid trigger");
            s.input.triggers[i]=SDL_GameControllerAxis(trigger);
        }
    }catch(const std::exception& e){warning=e.what();return AppSettings{};}return s;
}
bool save_settings(const std::filesystem::path& file,const AppSettings& s,std::string& error){
    error.clear();const auto temp=std::filesystem::path(file.wstring()+L".tmp");
    try{
        if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());
        {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<encode_settings(s);f.flush();if(!f)throw std::runtime_error("Cannot write settings");}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace settings");
#else
        std::filesystem::rename(temp,file);
#endif
        return true;
    }catch(const std::exception& e){error=e.what();std::error_code ec;std::filesystem::remove(temp,ec);return false;}
}
bool merge_settings_edits(AppSettings& stored,const AppSettings& a,const AppSettings& b,bool video){
    bool changed=false;
    if(video){
#define EDIT_VIDEO(f) if(a.video.f!=b.video.f&&stored.video.f!=b.video.f){stored.video.f=b.video.f;changed=true;}
        EDIT_VIDEO(renderer) EDIT_VIDEO(width) EDIT_VIDEO(height) EDIT_VIDEO(software_mode) EDIT_VIDEO(fullscreen)
#undef EDIT_VIDEO
    }
#define EDIT(f) if(a.f!=b.f&&stored.f!=b.f){stored.f=b.f;changed=true;}
    EDIT(locale) EDIT(linear) EDIT(show_fps) EDIT(inertia) EDIT(master_volume) EDIT(effects_volume) EDIT(music_volume) EDIT(last_dataset)
    EDIT(input.keys) EDIT(input.buttons) EDIT(input.triggers) EDIT(input.mouse) EDIT(input.classic_pad)
    EDIT(input.keyboard_preset) EDIT(input.pad_preset) EDIT(input.mouse_degrees_per_count) EDIT(input.turn_sensitivity) EDIT(input.stick_deadzone)
#undef EDIT
    return changed;
}
} // namespace dok
