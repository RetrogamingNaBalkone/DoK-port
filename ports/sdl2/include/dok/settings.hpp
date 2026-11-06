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
    int fullscreen_width=1280,fullscreen_height=720;
};
bool operator==(const VideoSettings& a,const VideoSettings& b);
inline bool operator!=(const VideoSettings& a,const VideoSettings& b){return !(a==b);}
struct AppSettings {
    VideoSettings video;
    LocaleId locale=system_locale();
    bool linear=false,show_fps=true,inertia=true;
    bool saves_enabled=true;
    int mip_filter=0; // 0: legacy nearest/linear; 1: mipmaps; 2: trilinear; 3..5: anisotropic 2/4/8
    int texture_filter()const{return mip_filter?mip_filter+1:linear?1:0;}
    void texture_filter(int mode){mip_filter=mode>1?mode-1:0;linear=mode!=0;}
    // Independent presentation options; never enter simulation state.
    int frame_filter=0; // nearest, linear, sharp bilinear, experimental xBR
    bool fxaa=false,classic_hud=false,auto_hud=true;
    int effects_volume=100,music_volume=100;
    DatasetId last_dataset=DatasetId::unknown;
    InputBindings input=InputBindings::defaults();
    std::vector<std::string> resource_paths;
    std::array<std::string,4> resource_choices{};
};
// The only permitted software framebuffer sizes. Presentation/window size is separate.
inline constexpr std::array<std::array<int,2>,5> software_modes{{{320,200},{640,400},{640,480},{800,600},{1024,768}}};
AppSettings load_settings(const std::filesystem::path& file,std::string& warning);
bool save_settings(const std::filesystem::path& file,const AppSettings& value,std::string& error);
// Persist explicit user edits, without copying unrelated command-line overrides.
// Returns whether persistent values actually changed (no serialization needed).
bool merge_settings_edits(AppSettings& stored,const AppSettings& before,const AppSettings& after,bool video=true);
std::string encode_settings(const AppSettings& value);
} // namespace dok
