#include "dok/sdl_input.hpp"
#include <algorithm>
#include <cmath>

namespace dok {
namespace {
constexpr unsigned index(Action action){return static_cast<unsigned>(action);}
float axis(Sint16 value){return value<0?value/32768.0f:value/32767.0f;}
float deadzone(float value,float zone){const auto magnitude=std::abs(value);return magnitude<=zone?0:std::copysign((magnitude-zone)/(1-zone),value);}
void strongest(float& target,float value){if(std::abs(value)>std::abs(target))target=value;}
}
unsigned wheel_binding(const SDL_MouseWheelEvent& event){
    float value=event.preciseY!=0?event.preciseY:float(event.y);
    if(event.direction==SDL_MOUSEWHEEL_FLIPPED)value=-value;
    return value>0?6:value<0?7:0;
}
InputBindings InputBindings::defaults(){
    InputBindings result;result.keyboard(InputPreset::modern);result.gamepad(InputPreset::modern);return result;
}
void InputBindings::keyboard(InputPreset preset){
    keys={};mouse={};keyboard_preset=preset;
    auto bind=[&](Action a,SDL_Scancode first,SDL_Scancode second=SDL_SCANCODE_UNKNOWN){keys[index(a)]={first,second};};
    bind(Action::forward,SDL_SCANCODE_W,SDL_SCANCODE_UP);bind(Action::backward,SDL_SCANCODE_S,SDL_SCANCODE_DOWN);
    bind(Action::strafe_left,SDL_SCANCODE_A,SDL_SCANCODE_Q);bind(Action::strafe_right,SDL_SCANCODE_D,SDL_SCANCODE_E);
    bind(Action::turn_left,SDL_SCANCODE_LEFT);bind(Action::turn_right,SDL_SCANCODE_RIGHT);
    bind(Action::fire,SDL_SCANCODE_LCTRL,SDL_SCANCODE_RCTRL);bind(Action::use,SDL_SCANCODE_SPACE,SDL_SCANCODE_RETURN);
    bind(Action::map,SDL_SCANCODE_TAB);bind(Action::pause,SDL_SCANCODE_ESCAPE);
    bind(Action::run,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT);bind(Action::slow,SDL_SCANCODE_LALT);
    bind(Action::next_weapon,SDL_SCANCODE_PAGEDOWN);bind(Action::previous_weapon,SDL_SCANCODE_PAGEUP);
    bind(Action::quicksave,SDL_SCANCODE_F5);bind(Action::quickload,SDL_SCANCODE_F9);
    bind(Action::weapon1,SDL_SCANCODE_1);bind(Action::weapon2,SDL_SCANCODE_2);
    mouse[index(Action::fire)]=SDL_BUTTON_LEFT;
    mouse[index(Action::next_weapon)]=6;mouse[index(Action::previous_weapon)]=7;
    if(preset==InputPreset::classic){
        bind(Action::forward,SDL_SCANCODE_UP);bind(Action::backward,SDL_SCANCODE_DOWN);
        bind(Action::strafe_left,SDL_SCANCODE_UNKNOWN);bind(Action::strafe_right,SDL_SCANCODE_UNKNOWN);
        bind(Action::slow,SDL_SCANCODE_UNKNOWN);bind(Action::strafe_modifier,SDL_SCANCODE_LALT,SDL_SCANCODE_RALT);
        bind(Action::use,SDL_SCANCODE_SPACE);
    }
}
void InputBindings::gamepad(InputPreset preset){
    buttons.fill(SDL_CONTROLLER_BUTTON_INVALID);triggers.fill(SDL_CONTROLLER_AXIS_INVALID);
    pad_preset=preset;classic_pad=preset==InputPreset::classic;
    buttons[index(Action::map)]=SDL_CONTROLLER_BUTTON_BACK;buttons[index(Action::pause)]=SDL_CONTROLLER_BUTTON_START;
    if(classic_pad){
        buttons[index(Action::forward)]=SDL_CONTROLLER_BUTTON_DPAD_UP;buttons[index(Action::backward)]=SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        buttons[index(Action::turn_left)]=SDL_CONTROLLER_BUTTON_DPAD_LEFT;buttons[index(Action::turn_right)]=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        buttons[index(Action::strafe_left)]=SDL_CONTROLLER_BUTTON_LEFTSHOULDER;buttons[index(Action::strafe_right)]=SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        // Face-button positions: west fires, south runs, north uses, east cycles.
        buttons[index(Action::fire)]=SDL_CONTROLLER_BUTTON_X;buttons[index(Action::run)]=SDL_CONTROLLER_BUTTON_A;
        buttons[index(Action::use)]=SDL_CONTROLLER_BUTTON_Y;buttons[index(Action::next_weapon)]=SDL_CONTROLLER_BUTTON_B;
    }else{
        buttons[index(Action::use)]=SDL_CONTROLLER_BUTTON_A;
        buttons[index(Action::next_weapon)]=SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;buttons[index(Action::previous_weapon)]=SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        triggers[index(Action::fire)]=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;triggers[index(Action::run)]=SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    }
}
SdlInput::SdlInput(InputBindings settings):bindings(std::move(settings)){
    for(int i=0;i<SDL_NumJoysticks();++i)open(i);
}
SdlInput::~SdlInput(){for(auto& entry:controllers_)SDL_GameControllerClose(entry.second.handle);if(capture_)SDL_SetRelativeMouseMode(SDL_FALSE);SDL_ShowCursor(SDL_ENABLE);}
void SdlInput::open(int device_index){
    if(!SDL_IsGameController(device_index))return;
    auto* handle=SDL_GameControllerOpen(device_index);if(!handle)return;
    auto id=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(handle));
    if(controllers_.count(id)){SDL_GameControllerClose(handle);return;}
    Controller controller;controller.handle=handle;
    if(focused_&&bindings.gamepad_enabled){
        for(int i=0;i<SDL_CONTROLLER_AXIS_MAX;++i)controller.axes[i]=SDL_GameControllerGetAxis(handle,static_cast<SDL_GameControllerAxis>(i));
        for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;++i)controller.buttons[i]=SDL_GameControllerGetButton(handle,static_cast<SDL_GameControllerButton>(i))!=0;
    }
    controllers_.emplace(id,controller);
}
void SdlInput::configure(const InputBindings& settings){
    const bool changed=bindings.mouse_enabled!=settings.mouse_enabled||bindings.gamepad_enabled!=settings.gamepad_enabled;
    bindings=settings;
    if(changed){clear();capture_mouse(gameplay_mouse_);}
}
bool SdlInput::capture_mouse(bool enabled){
    // Gameplay hides the pointer even when mouse input/capture is disabled.
    gameplay_mouse_=enabled;
    const bool capture=enabled&&bindings.mouse_enabled;
    const bool ok=SDL_SetRelativeMouseMode(capture&&focused_?SDL_TRUE:SDL_FALSE)==0;
    SDL_ShowCursor(enabled&&focused_?SDL_DISABLE:SDL_ENABLE);
    if(ok)capture_=capture;
    mouse_x_=0;return ok;
}
void SdlInput::clear(){
    keys_.fill(false);previous_.fill(false);presses_.fill(false);wheel_presses_.fill(false);mouse_buttons_.fill(false);mouse_x_=0;
    for(auto& entry:controllers_){entry.second.axes.fill(0);entry.second.buttons.fill(false);}
}
void SdlInput::handle(const SDL_Event& event){
    switch(event.type){
    case SDL_KEYDOWN:case SDL_KEYUP:
        if(event.type==SDL_KEYDOWN&&event.key.repeat)break;
        if(focused_&&event.key.keysym.scancode>SDL_SCANCODE_UNKNOWN&&event.key.keysym.scancode<SDL_NUM_SCANCODES){
            if(event.type==SDL_KEYUP&&keys_[event.key.keysym.scancode])
                for(auto key:bindings.keys[index(Action::map)])if(key==event.key.keysym.scancode)presses_[index(Action::map)]=true;
            if(event.type==SDL_KEYDOWN&&!keys_[event.key.keysym.scancode])
                for(auto action:{Action::fire,Action::weapon1,Action::weapon2,Action::quicksave,Action::quickload})for(auto key:bindings.keys[index(action)])
                    if(key==event.key.keysym.scancode)presses_[index(action)]=true;
            keys_[event.key.keysym.scancode]=event.type==SDL_KEYDOWN;
        }
        break;
    case SDL_MOUSEMOTION:if(bindings.mouse_enabled&&focused_&&capture_)mouse_x_+=event.motion.xrel;break;
    case SDL_MOUSEWHEEL:
        if(bindings.mouse_enabled&&focused_){const auto button=wheel_binding(event.wheel);
            if(button)for(unsigned i=0;i<wheel_presses_.size();++i)if(bindings.mouse[i]==button)wheel_presses_[i]=true;
        }break;
    case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:
        if(bindings.mouse_enabled&&focused_&&event.button.button<mouse_buttons_.size()){
            if(event.type==SDL_MOUSEBUTTONUP&&mouse_buttons_[event.button.button]&&bindings.mouse[index(Action::map)]==event.button.button)
                presses_[index(Action::map)]=true;
            if(event.type==SDL_MOUSEBUTTONDOWN&&!mouse_buttons_[event.button.button])
                for(auto action:{Action::fire,Action::weapon1,Action::weapon2,Action::quicksave,Action::quickload})if(bindings.mouse[index(action)]&&bindings.mouse[index(action)]==event.button.button)presses_[index(action)]=true;
            mouse_buttons_[event.button.button]=event.type==SDL_MOUSEBUTTONDOWN;
        }break;
    case SDL_CONTROLLERDEVICEADDED:open(event.cdevice.which);break;
    case SDL_CONTROLLERDEVICEREMOVED:{
        auto found=controllers_.find(event.cdevice.which);
        if(found!=controllers_.end()){SDL_GameControllerClose(found->second.handle);controllers_.erase(found);presses_.fill(false);}break;}
    case SDL_CONTROLLERAXISMOTION:{
        auto found=controllers_.find(event.caxis.which);
        if(bindings.gamepad_enabled&&focused_&&found!=controllers_.end()&&event.caxis.axis<SDL_CONTROLLER_AXIS_MAX){
            if(event.caxis.value<=16384&&found->second.axes[event.caxis.axis]>16384&&bindings.triggers[index(Action::map)]==event.caxis.axis)
                presses_[index(Action::map)]=true;
            if(event.caxis.value>16384&&found->second.axes[event.caxis.axis]<=16384)
                for(auto action:{Action::fire,Action::weapon1,Action::weapon2,Action::quicksave,Action::quickload})if(bindings.triggers[index(action)]==event.caxis.axis)presses_[index(action)]=true;
            found->second.axes[event.caxis.axis]=event.caxis.value;
        }break;}
    case SDL_CONTROLLERBUTTONDOWN:case SDL_CONTROLLERBUTTONUP:{
        auto found=controllers_.find(event.cbutton.which);
        if(bindings.gamepad_enabled&&focused_&&found!=controllers_.end()&&event.cbutton.button<SDL_CONTROLLER_BUTTON_MAX){
            if(event.type==SDL_CONTROLLERBUTTONUP&&found->second.buttons[event.cbutton.button]&&bindings.buttons[index(Action::map)]==event.cbutton.button)
                presses_[index(Action::map)]=true;
            if(event.type==SDL_CONTROLLERBUTTONDOWN&&!found->second.buttons[event.cbutton.button])
                for(auto action:{Action::fire,Action::weapon1,Action::weapon2,Action::quicksave,Action::quickload})if(bindings.buttons[index(action)]==event.cbutton.button)presses_[index(action)]=true;
            found->second.buttons[event.cbutton.button]=event.type==SDL_CONTROLLERBUTTONDOWN;
        }break;}
    case SDL_WINDOWEVENT:
        if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            focused_=false;clear();
            capture_mouse(gameplay_mouse_);
        }else if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){focused_=true;capture_mouse(gameplay_mouse_);}
        break;
    default:break;
    }
}
InputActions SdlInput::sample(){
    InputActions result;if(!focused_)return result;
    std::array<bool,index(Action::count)> held{};
    for(unsigned i=0;i<held.size();++i)for(auto key:bindings.keys[i])
        if(key>SDL_SCANCODE_UNKNOWN&&key<SDL_NUM_SCANCODES)held[i]=held[i]||keys_[key];
    if(bindings.mouse_enabled)for(unsigned i=0;i<held.size();++i)if(bindings.mouse[i]&&bindings.mouse[i]<mouse_buttons_.size())held[i]=held[i]||mouse_buttons_[bindings.mouse[i]];
    // Alt modifies only keyboard turning; a second device keeps independent axes.
    if(held[index(Action::strafe_modifier)]){
        held[index(Action::strafe_left)] = held[index(Action::strafe_left)]||held[index(Action::turn_left)];
        held[index(Action::strafe_right)] = held[index(Action::strafe_right)]||held[index(Action::turn_right)];
        held[index(Action::turn_left)]=held[index(Action::turn_right)]=false;
    }
    float pad_forward=0,pad_strafe=0,pad_turn=0;
    const auto zone=std::clamp(bindings.stick_deadzone,0.0f,0.95f);
    if(bindings.gamepad_enabled)for(const auto& pair:controllers_){const auto& controller=pair.second;
        for(unsigned i=0;i<held.size();++i){auto button=bindings.buttons[i];
            if(button>=0&&button<SDL_CONTROLLER_BUTTON_MAX)held[i]=held[i]||controller.buttons[button];}
        for(unsigned i=0;i<held.size();++i){auto trigger=bindings.triggers[i];
            if(trigger==SDL_CONTROLLER_AXIS_TRIGGERLEFT||trigger==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                held[i]=held[i]||controller.axes[trigger]>16384;}
        if(bindings.classic_pad)continue;
        float x=axis(controller.axes[SDL_CONTROLLER_AXIS_LEFTX]),y=axis(controller.axes[SDL_CONTROLLER_AXIS_LEFTY]);
        const auto length=std::sqrt(x*x+y*y);
        if(length>zone){const auto scale=(std::min(length,1.0f)-zone)/(1-zone)/length;x*=scale;y*=scale;}
        else x=y=0;
        strongest(pad_forward,-y);strongest(pad_strafe,x);
        strongest(pad_turn,deadzone(axis(controller.axes[SDL_CONTROLLER_AXIS_RIGHTX]),zone));

    }
    const auto physical=held;
    for(unsigned i=0;i<held.size();++i)held[i]=held[i]||wheel_presses_[i];
    auto down=[&](Action a){return held[index(a)];};
    auto edge=[&](Action a){return held[index(a)]&&!previous_[index(a)];};
    result.forward=std::clamp(pad_forward+int(down(Action::forward))-int(down(Action::backward)),-1.0f,1.0f);
    result.strafe=std::clamp(pad_strafe+int(down(Action::strafe_right))-int(down(Action::strafe_left)),-1.0f,1.0f);
    result.turn=std::clamp(pad_turn*bindings.turn_sensitivity+int(down(Action::turn_right))-int(down(Action::turn_left)),-1.0f,1.0f);
    result.yaw_delta=bindings.mouse_enabled?mouse_x_*std::max(0.0f,bindings.mouse_degrees_per_count):0;mouse_x_=0;
    result.fire=down(Action::fire);result.use=down(Action::use);
    result.fire_pressed=presses_[index(Action::fire)]||edge(Action::fire);
    // Originals toggle the map on release (keyboard state 2), not key repeat.
    result.map=presses_[index(Action::map)]||wheel_presses_[index(Action::map)]||(!physical[index(Action::map)]&&previous_[index(Action::map)]);
    result.pause=edge(Action::pause);result.run=down(Action::run);result.slow=down(Action::slow);
    result.next_weapon=edge(Action::next_weapon);result.previous_weapon=edge(Action::previous_weapon);
    result.quicksave=presses_[index(Action::quicksave)]||edge(Action::quicksave);result.quickload=presses_[index(Action::quickload)]||edge(Action::quickload);
    if(presses_[index(Action::weapon1)]||edge(Action::weapon1))result.select_weapon=1;
    if(presses_[index(Action::weapon2)]||edge(Action::weapon2))result.select_weapon=2;
    presses_.fill(false);wheel_presses_.fill(false);previous_=physical;return result;
}
} // namespace dok
