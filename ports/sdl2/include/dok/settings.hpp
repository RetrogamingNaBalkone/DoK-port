#pragma once
#include "sdl_input.hpp"
#include "resources.hpp"

namespace dok {
LocaleId system_locale();
struct VideoSettings {
    RendererKind renderer=RendererKind::opengl;
    int width=1280,height=720;
    int software_mode=0;
    bool fullscreen=false;
};
bool operator==(const VideoSettings& a,const VideoSettings& b);
inline bool operator!=(const VideoSettings& a,const VideoSettings& b){return !(a==b);}
struct AppSettings {
    VideoSettings video;
    LocaleId locale=system_locale();
    bool linear=false,show_fps=true,inertia=true;
    int master_volume=100,effects_volume=100,music_volume=100;
    DatasetId last_dataset=DatasetId::unknown;
    InputBindings input=InputBindings::defaults();
};
// The only permitted software framebuffer sizes. Presentation/window size is separate.
inline constexpr std::array<std::array<int,2>,5> software_modes{{{320,200},{640,400},{640,480},{800,600},{1024,768}}};
AppSettings load_settings(const std::filesystem::path& file,std::string& warning);
bool save_settings(const std::filesystem::path& file,const AppSettings& value,std::string& error);
// Persist explicit user edits, without copying unrelated command-line overrides.
void merge_settings_edits(AppSettings& stored,const AppSettings& before,const AppSettings& after,bool video=true);
std::string encode_settings(const AppSettings& value);
} // namespace dok
