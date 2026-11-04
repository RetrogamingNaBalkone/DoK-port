#include "dok/sdl_audio.hpp"
#include <stdexcept>
#include <algorithm>
namespace dok {
SdlAudio::SdlAudio(const Archive& archive){
    old_gold_=archive.identify()==DatasetId::old_gold;
    for(const auto& entry:archive.entries()){
        const auto b=archive.get(entry.name);
        if(b.size>=36&&b.data[2]=='S'&&b.data[3]=='V'&&b.data[4]=='M')sounds_.emplace(entry.name,archive.sound(entry.name));
    }
    SDL_AudioSpec wanted{},actual{};wanted.freq=48000;wanted.format=AUDIO_S16SYS;wanted.channels=1;wanted.samples=512;
    wanted.callback=callback;wanted.userdata=this;
    device_=SDL_OpenAudioDevice(nullptr,0,&wanted,&actual,SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if(!device_)throw std::runtime_error(SDL_GetError());
    rate_=static_cast<unsigned>(actual.freq);SDL_PauseAudioDevice(device_,0);
}
SdlAudio::~SdlAudio(){if(device_)SDL_CloseAudioDevice(device_);}
void SdlAudio::callback(void* ptr,Uint8* stream,int bytes){
    auto& self=*static_cast<SdlAudio*>(ptr);self.voice_.render(reinterpret_cast<std::int16_t*>(stream),bytes/2,self.rate_);
    auto* samples=reinterpret_cast<std::int16_t*>(stream);for(int i=0;i<bytes/2;++i)samples[i]=static_cast<std::int16_t>(int(samples[i])*self.volume_/100);
}
void SdlAudio::volume(int percent){
    percent=std::clamp(percent,0,100);if(percent==requested_volume_)return;
    SDL_LockAudioDevice(device_);volume_=percent;SDL_UnlockAudioDevice(device_);requested_volume_=percent;
}
void SdlAudio::pause(bool paused){if(paused==paused_)return;SDL_PauseAudioDevice(device_,paused?1:0);paused_=paused;}
void SdlAudio::command(const AudioCommand& c){
    const auto sound=sounds_.find(c.resource);
    SDL_LockAudioDevice(device_);
    if(c.action==AudioAction::stop_all)voice_.stop();
    else if(c.action==AudioAction::stop)voice_.stop(c.id);
    else if(sound!=sounds_.end())voice_.play(c.id,sound->second,c.action==AudioAction::loop,old_gold_);
    SDL_UnlockAudioDevice(device_);
}
}
