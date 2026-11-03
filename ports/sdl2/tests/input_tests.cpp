#include "dok/sdl_input.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool value){if(!value)throw std::runtime_error("Input scenario failed");}
static void key(dok::SdlInput& input,SDL_Scancode code,bool down){SDL_Event event{};event.type=down?SDL_KEYDOWN:SDL_KEYUP;event.key.keysym.scancode=code;input.handle(event);}
static void pump(dok::SdlInput& input){SDL_Event event;SDL_PumpEvents();while(SDL_PollEvent(&event))input.handle(event);}
int main(){
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)!=0)return 2;
    int result=0;
    try{
        dok::SdlInput input;
        key(input,SDL_SCANCODE_W,true);key(input,SDL_SCANCODE_D,true);key(input,SDL_SCANCODE_RIGHT,true);
        auto state=input.sample();check(state.forward==1&&state.strafe==1&&state.turn==1);
        key(input,SDL_SCANCODE_TAB,true);check(input.sample().map);check(!input.sample().map);
        SDL_Event focus{};focus.type=SDL_WINDOWEVENT;focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input.handle(focus);
        state=input.sample();check(!state.forward&&!state.strafe&&!state.turn&&!state.map);
        focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;input.handle(focus);check(!input.sample().forward);
        input.bindings.keys[static_cast<unsigned>(dok::Action::forward)]={SDL_SCANCODE_I,SDL_SCANCODE_UNKNOWN};
        key(input,SDL_SCANCODE_W,true);check(!input.sample().forward);key(input,SDL_SCANCODE_W,false);
        key(input,SDL_SCANCODE_I,true);check(input.sample().forward==1);key(input,SDL_SCANCODE_I,false);
        key(input,SDL_SCANCODE_1,true);check(input.sample().select_weapon==1);key(input,SDL_SCANCODE_1,false);
        key(input,SDL_SCANCODE_2,true);check(input.sample().select_weapon==2);key(input,SDL_SCANCODE_2,false);
        input.clear();input.bindings.keyboard(dok::InputPreset::classic);
        key(input,SDL_SCANCODE_UP,true);key(input,SDL_SCANCODE_RIGHT,true);key(input,SDL_SCANCODE_LALT,true);
        state=input.sample();check(state.forward==1&&state.strafe==1&&state.turn==0&&!state.slow);
        key(input,SDL_SCANCODE_LALT,false);state=input.sample();check(state.forward==1&&state.turn==1&&state.strafe==0);
        input.clear();input.bindings.keyboard(dok::InputPreset::modern);
        input.bindings.mouse[static_cast<unsigned>(dok::Action::fire)]=SDL_BUTTON_RIGHT;
        SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;input.handle(click);check(!input.sample().fire);
        click.button.button=SDL_BUTTON_RIGHT;input.handle(click);check(input.sample().fire);input.clear();
        const auto baseline=input.controller_count();
        const auto device=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);
        check(device>=0);auto* joystick=SDL_JoystickOpen(device);check(joystick!=nullptr);
        pump(input);check(input.controller_count()==baseline+1);
        check(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTX,16384)==0);
        check(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,-32768)==0);
        check(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,32767)==0);
        pump(input);state=input.sample();check(state.forward>0.8f&&state.strafe>0.4f&&state.turn==1);
        check(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTX,3000)==0);
        check(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,-3000)==0);
        check(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,1000)==0);
        check(SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_A,1)==0);
        pump(input);state=input.sample();check(!state.forward&&!state.strafe&&!state.turn&&state.use);
        input.clear();input.bindings.gamepad(dok::InputPreset::classic);
        check(SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_A,0)==0);pump(input);
        check(SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_DPAD_UP,1)==0);
        check(SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1)==0);
        check(SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,1)==0);
        check(SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_A,1)==0);
        check(SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_B,1)==0);
        pump(input);state=input.sample();check(state.forward==1&&state.turn==1&&state.strafe==-1&&state.fire&&state.run&&!state.use);
        input.clear();input.bindings.gamepad(dok::InputPreset::modern);
        SDL_Event trigger{};trigger.type=SDL_CONTROLLERAXISMOTION;trigger.caxis.which=SDL_JoystickInstanceID(joystick);trigger.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;trigger.caxis.value=24000;input.handle(trigger);
        trigger.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERLEFT;input.handle(trigger);state=input.sample();check(state.fire&&state.run);
        check(SDL_JoystickDetachVirtual(device)==0);pump(input);SDL_JoystickClose(joystick);
        check(input.controller_count()==baseline&&!input.sample().use);
        std::cout<<"Independent axes, focus, remapping, deadzones and SDL virtual controller hotplug passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<": "<<SDL_GetError()<<'\n';result=1;}
    SDL_Quit();return result;
}
