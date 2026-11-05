#pragma once
#include "contracts.hpp"
#include <SDL.h>
#include <array>
#include <map>

namespace dok {
enum class Action : unsigned {forward,backward,strafe_left,strafe_right,turn_left,turn_right,
    fire,use,map,pause,run,slow,next_weapon,previous_weapon,quicksave,quickload,strafe_modifier,weapon1,weapon2,count};
enum class InputPreset { modern, classic, custom };
// Mouse binding values 1..5 are buttons; 6/7 are wheel up/down.
unsigned wheel_binding(const SDL_MouseWheelEvent& event);
struct InputBindings {
    std::array<std::array<SDL_Scancode,2>,static_cast<unsigned>(Action::count)> keys{};
    std::array<SDL_GameControllerButton,static_cast<unsigned>(Action::count)> buttons{};
    std::array<SDL_GameControllerAxis,static_cast<unsigned>(Action::count)> triggers{};
    std::array<unsigned,static_cast<unsigned>(Action::count)> mouse{};
    float stick_deadzone=0.18f,mouse_degrees_per_count=0.15f;
    float turn_sensitivity=1.0f;
    bool classic_pad=false;
    bool mouse_enabled=true,gamepad_enabled=true;
    InputPreset keyboard_preset=InputPreset::modern,pad_preset=InputPreset::modern;
    void keyboard(InputPreset preset);
    void gamepad(InputPreset preset);
    static InputBindings defaults();
};
class SdlInput {
public:
    explicit SdlInput(InputBindings bindings=InputBindings::defaults());
    ~SdlInput();
    SdlInput(const SdlInput&)=delete;
    SdlInput& operator=(const SdlInput&)=delete;
    void handle(const SDL_Event& event);
    InputActions sample();
    void configure(const InputBindings& settings);
    bool capture_mouse(bool enabled);
    void clear();
    InputBindings bindings;
    std::size_t controller_count() const noexcept {return controllers_.size();}
private:
    struct Controller {
        SDL_GameController* handle{};
        std::array<Sint16,SDL_CONTROLLER_AXIS_MAX> axes{};
        std::array<bool,SDL_CONTROLLER_BUTTON_MAX> buttons{};
    };
    void open(int device_index);
    std::map<SDL_JoystickID,Controller> controllers_;
    std::array<bool,SDL_NUM_SCANCODES> keys_{};
    std::array<bool,static_cast<unsigned>(Action::count)> previous_{};
    std::array<bool,static_cast<unsigned>(Action::count)> presses_{};
    std::array<bool,static_cast<unsigned>(Action::count)> wheel_presses_{};
    bool focused_=true,capture_=false,gameplay_mouse_=false;
    std::array<bool,6> mouse_buttons_{};
    float mouse_x_=0;
};
} // namespace dok
