#pragma once
#include "audio.hpp"
#include <SDL.h>
#include <map>
namespace dok {
class SdlAudio {
public:
    explicit SdlAudio(const Archive& archive);
    ~SdlAudio();
    SdlAudio(const SdlAudio&)=delete;
    SdlAudio& operator=(const SdlAudio&)=delete;
    void command(const AudioCommand& command);
    void pause(bool paused);
    void volume(int percent);
private:
    static void callback(void* self,Uint8* stream,int bytes);
    SDL_AudioDeviceID device_=0;
    unsigned rate_=48000;
    bool old_gold_=false;
    int volume_=100;
    MonoVoice voice_;
    std::map<std::string,Sound> sounds_;
};
}
