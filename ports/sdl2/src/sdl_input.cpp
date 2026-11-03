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
InputBindings InputBindings::defaults(){
    InputBindings result;result.buttons.fill(SDL_CONTROLLER_BUTTON_INVALID);
    auto bind=[&](Action a,SDL_Scancode first,SDL_Scancode second=SDL_SCANCODE_UNKNOWN){result.keys[index(a)]={first,second};};
    bind(Action::forward,SDL_SCANCODE_W,SDL_SCANCODE_UP);bind(Action::backward,SDL_SCANCODE_S,SDL_SCANCODE_DOWN);
    bind(Action::strafe_left,SDL_SCANCODE_A,SDL_SCANCODE_Q);bind(Action::strafe_right,SDL_SCANCODE_D,SDL_SCANCODE_E);
    bind(Action::turn_left,SDL_SCANCODE_LEFT);bind(Action::turn_right,SDL_SCANCODE_RIGHT);
    bind(Action::fire,SDL_SCANCODE_LCTRL,SDL_SCANCODE_RCTRL);bind(Action::use,SDL_SCANCODE_SPACE,SDL_SCANCODE_RETURN);
    bind(Action::map,SDL_SCANCODE_TAB);bind(Action::pause,SDL_SCANCODE_ESCAPE);
    bind(Action::run,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT);bind(Action::slow,SDL_SCANCODE_LALT);
    bind(Action::next_weapon,SDL_SCANCODE_PAGEDOWN);bind(Action::previous_weapon,SDL_SCANCODE_PAGEUP);
    bind(Action::quicksave,SDL_SCANCODE_F5);bind(Action::quickload,SDL_SCANCODE_F9);
    result.buttons[index(Action::use)]=SDL_CONTROLLER_BUTTON_A;
    result.buttons[index(Action::fire)]=SDL_CONTROLLER_BUTTON_X;
    result.buttons[index(Action::map)]=SDL_CONTROLLER_BUTTON_Y;
    result.buttons[index(Action::pause)]=SDL_CONTROLLER_BUTTON_START;
    result.buttons[index(Action::run)]=SDL_CONTROLLER_BUTTON_LEFTSTICK;
    result.buttons[index(Action::next_weapon)]=SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    result.buttons[index(Action::previous_weapon)]=SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    return result;
}
SdlInput::SdlInput(InputBindings settings):bindings(std::move(settings)){
    for(int i=0;i<SDL_NumJoysticks();++i)open(i);
}
SdlInput::~SdlInput(){for(auto& entry:controllers_)SDL_GameControllerClose(entry.second.handle);if(capture_)SDL_SetRelativeMouseMode(SDL_FALSE);}
void SdlInput::open(int device_index){
    if(!SDL_IsGameController(device_index))return;
    auto* handle=SDL_GameControllerOpen(device_index);if(!handle)return;
    auto id=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(handle));
    if(controllers_.count(id)){SDL_GameControllerClose(handle);return;}
    Controller controller;controller.handle=handle;
    for(int i=0;i<SDL_CONTROLLER_AXIS_MAX;++i)controller.axes[i]=SDL_GameControllerGetAxis(handle,static_cast<SDL_GameControllerAxis>(i));
    for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;++i)controller.buttons[i]=SDL_GameControllerGetButton(handle,static_cast<SDL_GameControllerButton>(i))!=0;
    controllers_.emplace(id,controller);
}
bool SdlInput::capture_mouse(bool enabled){
    if(SDL_SetRelativeMouseMode(enabled&&focused_?SDL_TRUE:SDL_FALSE)!=0)return false;
    capture_=enabled;mouse_x_=0;return true;
}
void SdlInput::handle(const SDL_Event& event){
    switch(event.type){
    case SDL_KEYDOWN:case SDL_KEYUP:
        if(focused_&&event.key.keysym.scancode>SDL_SCANCODE_UNKNOWN&&event.key.keysym.scancode<SDL_NUM_SCANCODES)
            keys_[event.key.keysym.scancode]=event.type==SDL_KEYDOWN;
        break;
    case SDL_MOUSEMOTION:if(focused_&&capture_)mouse_x_+=event.motion.xrel;break;
    case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:
        if(focused_&&event.button.button==SDL_BUTTON_LEFT)mouse_fire_=event.type==SDL_MOUSEBUTTONDOWN;break;
    case SDL_CONTROLLERDEVICEADDED:open(event.cdevice.which);break;
    case SDL_CONTROLLERDEVICEREMOVED:{
        auto found=controllers_.find(event.cdevice.which);
        if(found!=controllers_.end()){SDL_GameControllerClose(found->second.handle);controllers_.erase(found);}break;}
    case SDL_CONTROLLERAXISMOTION:{
        auto found=controllers_.find(event.caxis.which);
        if(found!=controllers_.end()&&event.caxis.axis<SDL_CONTROLLER_AXIS_MAX)found->second.axes[event.caxis.axis]=event.caxis.value;break;}
    case SDL_CONTROLLERBUTTONDOWN:case SDL_CONTROLLERBUTTONUP:{
        auto found=controllers_.find(event.cbutton.which);
        if(found!=controllers_.end()&&event.cbutton.button<SDL_CONTROLLER_BUTTON_MAX)
            found->second.buttons[event.cbutton.button]=event.type==SDL_CONTROLLERBUTTONDOWN;break;}
    case SDL_WINDOWEVENT:
        if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            focused_=false;keys_.fill(false);previous_.fill(false);mouse_x_=0;mouse_fire_=false;
            if(capture_)SDL_SetRelativeMouseMode(SDL_FALSE);
        }else if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){focused_=true;if(capture_)SDL_SetRelativeMouseMode(SDL_TRUE);}
        break;
    default:break;
    }
}
InputActions SdlInput::sample(){
    InputActions result;if(!focused_)return result;
    std::array<bool,index(Action::count)> held{};
    for(unsigned i=0;i<held.size();++i)for(auto key:bindings.keys[i])
        if(key>SDL_SCANCODE_UNKNOWN&&key<SDL_NUM_SCANCODES)held[i]=held[i]||keys_[key];
    float pad_forward=0,pad_strafe=0,pad_turn=0;
    const auto zone=std::clamp(bindings.stick_deadzone,0.0f,0.95f);
    for(const auto& pair:controllers_){const auto& controller=pair.second;
        for(unsigned i=0;i<held.size();++i){auto button=bindings.buttons[i];
            if(button>=0&&button<SDL_CONTROLLER_BUTTON_MAX)held[i]=held[i]||controller.buttons[button];}
        float x=axis(controller.axes[SDL_CONTROLLER_AXIS_LEFTX]),y=axis(controller.axes[SDL_CONTROLLER_AXIS_LEFTY]);
        const auto length=std::sqrt(x*x+y*y);
        if(length>zone){const auto scale=(std::min(length,1.0f)-zone)/(1-zone)/length;x*=scale;y*=scale;}
        else x=y=0;
        strongest(pad_forward,-y);strongest(pad_strafe,x);
        strongest(pad_turn,deadzone(axis(controller.axes[SDL_CONTROLLER_AXIS_RIGHTX]),zone));
        held[index(Action::fire)]=held[index(Action::fire)]||controller.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]>16384;
    }
    auto down=[&](Action a){return held[index(a)];};
    auto edge=[&](Action a){return held[index(a)]&&!previous_[index(a)];};
    result.forward=std::clamp(pad_forward+int(down(Action::forward))-int(down(Action::backward)),-1.0f,1.0f);
    result.strafe=std::clamp(pad_strafe+int(down(Action::strafe_right))-int(down(Action::strafe_left)),-1.0f,1.0f);
    result.turn=std::clamp(pad_turn+int(down(Action::turn_right))-int(down(Action::turn_left)),-1.0f,1.0f);
    result.yaw_delta=mouse_x_*std::max(0.0f,bindings.mouse_degrees_per_count);mouse_x_=0;
    result.fire=down(Action::fire)||mouse_fire_;result.use=down(Action::use);
    result.map=edge(Action::map);result.pause=edge(Action::pause);result.run=down(Action::run);result.slow=down(Action::slow);
    result.next_weapon=edge(Action::next_weapon);result.previous_weapon=edge(Action::previous_weapon);
    result.quicksave=edge(Action::quicksave);result.quickload=edge(Action::quickload);
    for(int i=0;i<8;++i)if(keys_[SDL_SCANCODE_1+i])result.select_weapon=i+1;
    previous_=held;return result;
}
} // namespace dok
