#pragma once
#include "world.hpp"
#include "raster.hpp"
namespace dok {
struct HudPanel {int x,y,width,height;};
struct HudFrame {int width,height;std::vector<std::uint32_t> pixels;};
class Hud {
public:
    explicit Hud(const Archive& archive);
    IndexedSurface classic(const IndexedSurface& scene,const PlayerState& player,const std::string& message={}) const;
    IndexedSurface classic_mode(const IndexedSurface& scene,int frame_height,const PlayerState& player,const std::string& message={}) const;
    std::vector<HudPanel> overlay(IndexedSurface& scene,const PlayerState& player,const std::string& message={}) const;
    // Desktop presentation in screen pixels, independent of the 160-line world.
    // Reference remains valid until the next desktop call on this Hud.
    const HudFrame& desktop(int width,const PlayerState& player,const std::array<std::uint32_t,256>& palette,
                     const std::string& message,double seconds);
private:
    Image panel_,health_,charge_,money_;
    bool old_gold_{};
    std::array<Image,3> keys_;
    std::array<Image,2> weapons_;
    std::array<std::uint8_t,2048> font_{};
    int previous_health_=-1,previous_weapon_=-1,health_delta_=0,old_weapon_=-1;
    double health_changed_=-10,weapon_changed_=-10;
    HudFrame desktop_frame_{};
    PlayerState desktop_player_{};
    std::array<std::uint32_t,256> desktop_palette_{};
    std::string desktop_message_;
    bool desktop_valid_=false,desktop_animated_=false;
    double desktop_seconds_=0;
    void text(IndexedSurface& surface,int x,int y,const std::string& value) const;
};
}
