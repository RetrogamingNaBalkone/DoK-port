#pragma once
#include "contracts.hpp"
#include <SDL.h>
#include <array>
#include <map>

namespace dok {
enum class Action : unsigned {forward,backward,strafe_left,strafe_right,turn_left,turn_right,
    fire,use,map,pause,run,slow,next_weapon,previous_weapon,quicksave,quickload,count};
struct InputBindings {
    std::array<std::array<SDL_Scancode,2>,static_cast<unsigned>(Action::count)> keys{};
    std::array<SDL_GameControllerButton,static_cast<unsigned>(Action::count)> buttons{};
    float stick_deadzone=0.18f,mouse_degrees_per_count=0.15f;
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
    bool capture_mouse(bool enabled);
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
    bool focused_=true,capture_=false,mouse_fire_=false;
    float mouse_x_=0;
};
} // namespace dok
