#pragma once
#include <cstdint>
#include <string>

namespace dok {
enum class RendererKind { software, opengl };
enum class WindowMode { windowed, borderless, exclusive };
enum class HudLayout { automatic, classic, overlay };
enum class LocaleId { ru, en };
// These independent axes are sampled together. Backends cannot suppress strafe
// when a turn or forward movement is present. Device-specific codes stay in SDL.
struct InputActions {
    float forward{},strafe{},turn{};
    float yaw_delta{}; // Relative mouse yaw in degrees, consumed once.
    bool fire{},use{},map{},pause{};
    bool run{},slow{},next_weapon{},previous_weapon{},quicksave{},quickload{};
    int select_weapon{-1};
};
struct ViewConfig {
    int drawable_width{},drawable_height{};
    int scene_x{},scene_y{},scene_width{},scene_height{};
    double reference_horizontal_fov{},reference_scene_aspect{};
    double pixel_height_over_width{1.0};
    bool force_four_three{};
    HudLayout hud{HudLayout::automatic};
};
// Game/save state deliberately excludes SDL objects and renderer handles.
}
