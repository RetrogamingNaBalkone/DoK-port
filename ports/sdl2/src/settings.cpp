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
bool operator==(const VideoSettings& a,const VideoSettings& b){return a.renderer==b.renderer&&a.width==b.width&&a.height==b.height&&a.software_mode==b.software_mode&&a.fullscreen==b.fullscreen&&a.fullscreen_width==b.fullscreen_width&&a.fullscreen_height==b.fullscreen_height;}
namespace {
using Values=std::map<std::string,std::string>;
std::string path_hex(const std::string& value){const char* digits="0123456789abcdef";std::string out;for(unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string path_unhex(const std::string& value){if(value.size()%2||value.size()>16384)throw std::runtime_error("Invalid resource path");std::string out;for(std::size_t i=0;i<value.size();i+=2){auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("Invalid resource path encoding");};const char c=char(digit(value[i])*16+digit(value[i+1]));if(!c||c=='\n'||c=='\r')throw std::runtime_error("Invalid resource path character");out+=c;}return out;}

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
    if(s.resource_paths.size()>64)throw std::invalid_argument("Too many saved resource paths");
    std::ostringstream o;o<<"[settings]\nversion=1\n[video]\nrenderer="<<int(s.video.renderer)<<"\nwidth="<<s.video.width<<"\nheight="<<s.video.height
      <<"\nsoftware_mode="<<s.video.software_mode<<"\nfullscreen="<<s.video.fullscreen<<"\nlinear="<<s.linear
      <<"\nframe_filter="<<s.frame_filter<<"\nfxaa="<<s.fxaa<<"\nclassic_hud="<<s.classic_hud<<"\nauto_hud="<<s.auto_hud<<"\nfullscreen_width="<<s.video.fullscreen_width<<"\nfullscreen_height="<<s.video.fullscreen_height
      <<"\n[interface]\nlocale="<<int(s.locale)<<"\nfps="<<s.show_fps<<"\nlast_dataset="<<int(s.last_dataset)
      <<"\n[gameplay]\ninertia="<<s.inertia<<"\n[audio]\nmaster=100\neffects="<<s.effects_volume<<"\nmusic="<<s.music_volume
      <<"\n[input]\nkeyboard_preset="<<int(s.input.keyboard_preset)<<"\npad_preset="<<int(s.input.pad_preset)<<"\nclassic_pad="<<s.input.classic_pad
      <<"\nmouse_enabled="<<s.input.mouse_enabled<<"\ngamepad_enabled="<<s.input.gamepad_enabled
      <<"\nmouse_sensitivity="<<s.input.mouse_degrees_per_count<<"\nstick_sensitivity="<<s.input.turn_sensitivity<<"\ndeadzone="<<s.input.stick_deadzone<<'\n';
    for(unsigned i=0;i<static_cast<unsigned>(Action::count);++i){
        o<<"key"<<i<<"a="<<int(s.input.keys[i][0])<<"\nkey"<<i<<"b="<<int(s.input.keys[i][1])
         <<"\nmouse"<<i<<'='<<s.input.mouse[i]<<"\nbutton"<<i<<'='<<int(s.input.buttons[i])<<"\ntrigger"<<i<<'='<<int(s.input.triggers[i])<<'\n';
    }
    o<<"[resources]\ncount="<<s.resource_paths.size()<<'\n';
    for(std::size_t i=0;i<s.resource_paths.size();++i)o<<"path"<<i<<'='<<path_hex(s.resource_paths[i])<<'\n';
    for(unsigned i=1;i<4;++i)o<<"choice"<<i<<'='<<path_hex(s.resource_choices[i])<<'\n';
    auto result=o.str();if(result.size()>65536)throw std::invalid_argument("Settings file too large");return result;
}
AppSettings load_settings(const std::filesystem::path& file,std::string& warning){
    warning.clear();AppSettings s;
    try{
        if(!std::filesystem::exists(file))return s;
        if(std::filesystem::file_size(file)>65536)throw std::runtime_error("Settings file too large");
        std::ifstream f(file,std::ios::binary);if(!f)throw std::runtime_error("Cannot read settings");
        std::ostringstream text;text<<f.rdbuf();const auto m=values(text.str());
        if(integer(m,"settings.version",0,1,1)!=1)throw std::runtime_error("Unsupported settings version");
        const int resource_count=integer(m,"resources.count",0,0,64);
        for(int i=0;i<resource_count;++i){const auto it=m.find("resources.path"+std::to_string(i));if(it==m.end())throw std::runtime_error("Missing resource path");s.resource_paths.push_back(path_unhex(it->second));}
        for(unsigned i=1;i<4;++i){const auto it=m.find("resources.choice"+std::to_string(i));if(it!=m.end())s.resource_choices[i]=path_unhex(it->second);}
        s.video.renderer=RendererKind(integer(m,"video.renderer",int(s.video.renderer),0,1));
        s.video.width=integer(m,"video.width",1280,320,7680);s.video.height=integer(m,"video.height",720,200,4320);
        s.video.software_mode=integer(m,"video.software_mode",0,0,4);s.video.fullscreen=integer(m,"video.fullscreen",0,0,1);
        s.video.fullscreen_width=integer(m,"video.fullscreen_width",s.video.width,320,7680);
        s.video.fullscreen_height=integer(m,"video.fullscreen_height",s.video.height,200,4320);
        s.auto_hud=integer(m,"video.auto_hud",1,0,1);
        s.frame_filter=integer(m,"video.frame_filter",0,0,2);s.fxaa=integer(m,"video.fxaa",0,0,1);s.classic_hud=integer(m,"video.classic_hud",0,0,1);
        s.linear=integer(m,"video.linear",0,0,1);s.locale=LocaleId(integer(m,"interface.locale",int(s.locale),0,1));
        s.show_fps=integer(m,"interface.fps",1,0,1);s.last_dataset=DatasetId(integer(m,"interface.last_dataset",0,0,3));
        s.inertia=integer(m,"gameplay.inertia",1,0,1);const int master=integer(m,"audio.master",100,0,100);
        s.effects_volume=integer(m,"audio.effects",100,0,100);s.music_volume=integer(m,"audio.music",100,0,100);
        // Legacy v1 used nested gains. Flatten once; encode writes master=100.
        s.effects_volume=master*s.effects_volume/100;s.music_volume=master*s.music_volume/100;
        s.input.keyboard_preset=InputPreset(integer(m,"input.keyboard_preset",0,0,2));s.input.pad_preset=InputPreset(integer(m,"input.pad_preset",0,0,2));
        s.input.mouse_enabled=integer(m,"input.mouse_enabled",1,0,1);s.input.gamepad_enabled=integer(m,"input.gamepad_enabled",1,0,1);
        s.input.classic_pad=integer(m,"input.classic_pad",0,0,1);
        s.input.mouse_degrees_per_count=number(m,"input.mouse_sensitivity",0.15f,0.01f,2);
        s.input.turn_sensitivity=number(m,"input.stick_sensitivity",1,0.1f,3);s.input.stick_deadzone=number(m,"input.deadzone",0.18f,0,0.8f);
        for(unsigned i=0;i<static_cast<unsigned>(Action::count);++i){
            const auto n=std::to_string(i);
            s.input.keys[i][0]=SDL_Scancode(integer(m,"input.key"+n+"a",s.input.keys[i][0],0,SDL_NUM_SCANCODES-1));
            s.input.keys[i][1]=SDL_Scancode(integer(m,"input.key"+n+"b",s.input.keys[i][1],0,SDL_NUM_SCANCODES-1));
            s.input.mouse[i]=integer(m,"input.mouse"+n,s.input.mouse[i],0,7);
            s.input.buttons[i]=SDL_GameControllerButton(integer(m,"input.button"+n,s.input.buttons[i],-1,SDL_CONTROLLER_BUTTON_MAX-1));
            const int trigger=integer(m,"input.trigger"+n,s.input.triggers[i],-1,SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            if(trigger!=-1&&trigger!=SDL_CONTROLLER_AXIS_TRIGGERLEFT&&trigger!=SDL_CONTROLLER_AXIS_TRIGGERRIGHT)throw std::runtime_error("Invalid trigger");
            s.input.triggers[i]=SDL_GameControllerAxis(trigger);
        }
        // Adopt the added wheel defaults in untouched stock keyboard profiles.
        const auto next=static_cast<unsigned>(Action::next_weapon),previous=static_cast<unsigned>(Action::previous_weapon);
        if(s.input.keyboard_preset!=InputPreset::custom&&!s.input.mouse[next]&&!s.input.mouse[previous]){s.input.mouse[next]=6;s.input.mouse[previous]=7;}
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
        EDIT_VIDEO(renderer) EDIT_VIDEO(width) EDIT_VIDEO(height) EDIT_VIDEO(software_mode) EDIT_VIDEO(fullscreen) EDIT_VIDEO(fullscreen_width) EDIT_VIDEO(fullscreen_height)
#undef EDIT_VIDEO
    }
#define EDIT(f) if(a.f!=b.f&&stored.f!=b.f){stored.f=b.f;changed=true;}
    EDIT(resource_paths) EDIT(resource_choices) EDIT(auto_hud) EDIT(frame_filter) EDIT(fxaa) EDIT(classic_hud) EDIT(locale) EDIT(linear) EDIT(show_fps) EDIT(inertia) EDIT(effects_volume) EDIT(music_volume) EDIT(last_dataset)
    EDIT(input.mouse_enabled) EDIT(input.gamepad_enabled)
    EDIT(input.keys) EDIT(input.buttons) EDIT(input.triggers) EDIT(input.mouse) EDIT(input.classic_pad)
    EDIT(input.keyboard_preset) EDIT(input.pad_preset) EDIT(input.mouse_degrees_per_count) EDIT(input.turn_sensitivity) EDIT(input.stick_deadzone)
#undef EDIT
    return changed;
}
} // namespace dok
