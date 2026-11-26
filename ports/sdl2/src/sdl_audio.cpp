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
    auto& self=*static_cast<SdlAudio*>(ptr);
    self.consume_commands();
    self.voice_.render(reinterpret_cast<std::int16_t*>(stream),bytes/2,self.rate_);
    if(self.volume_!=100)apply_audio_gain(reinterpret_cast<std::int16_t*>(stream),bytes/2,self.volume_);
}
void SdlAudio::volume(int percent){
    percent=std::clamp(percent,0,100);if(percent==requested_volume_)return;
    SDL_LockAudioDevice(device_);volume_=percent;SDL_UnlockAudioDevice(device_);requested_volume_=percent;
}
void SdlAudio::pause(bool paused){if(paused==paused_)return;SDL_PauseAudioDevice(device_,paused?1:0);paused_=paused;}
void SdlAudio::command(const AudioCommand& c){
    const auto sound=sounds_.find(c.resource);
    if(sound==sounds_.end()&&c.action!=AudioAction::stop&&c.action!=AudioAction::stop_all)return;
    const auto write=write_command_.load(std::memory_order_relaxed);
    if(write-read_command_.load(std::memory_order_acquire)==commands_.size()){
        // A paused device or a large burst must not lose sound ordering.
        SDL_LockAudioDevice(device_);consume_commands();SDL_UnlockAudioDevice(device_);
    }
    commands_[write%commands_.size()]={c.action,c.id,sound==sounds_.end()?nullptr:&sound->second};
    write_command_.store(write+1,std::memory_order_release);
}
void SdlAudio::consume_commands(){
    auto read=read_command_.load(std::memory_order_relaxed);
    const auto write=write_command_.load(std::memory_order_acquire);
    for(;read!=write;++read){
        const auto& c=commands_[read%commands_.size()];
        if(c.action==AudioAction::stop_all)voice_.stop();
        else if(c.action==AudioAction::stop)voice_.stop(c.id);
        else{
            if(c.action==AudioAction::priority_once&&voice_.active()!=c.id)voice_.stop();
            if(c.action!=AudioAction::priority_once||voice_.active()!=c.id)
                voice_.play(c.id,*c.sound,c.action==AudioAction::loop,old_gold_);
        }
    }
    read_command_.store(read,std::memory_order_release);
}
SdlAudioSnapshot SdlAudio::snapshot(){
    SDL_LockAudioDevice(device_);
    consume_commands();
    SdlAudioSnapshot state{voice_.snapshot(),{},paused_,volume_};
    if(state.voice.id){
        // SoundId belongs to an owner/binding and does not identify PCM bytes.
        // Resolve the pointer only while locked, and save the archive key.
        for(const auto& entry:sounds_){
            if(voice_.uses(entry.second)){state.resource=entry.first;break;}
        }
    }
    SDL_UnlockAudioDevice(device_);return state;
}
bool SdlAudio::prepare_restore(const SdlAudioSnapshot& state,VoiceSnapshot& voice)const{
    if(state.volume<0||state.volume>100||(state.voice.id&&state.voice.old_gold!=old_gold_))return false;
    const auto found=sounds_.find(state.resource);if(state.voice.id&&found==sounds_.end())return false;
    if(!state.voice.id&&!state.resource.empty())return false;voice=state.voice;
    if(voice.output_rate&&voice.output_rate!=rate_){const auto old=voice.output_rate;const auto whole=voice.phase/old;if(whole>UINT64_MAX/rate_)return false;const auto base=whole*rate_;const auto fraction=(voice.phase%old)*rate_/old;if(fraction>UINT64_MAX-base)return false;voice.phase=base+fraction;voice.output_rate=rate_;}
    MonoVoice candidate;return candidate.restore(voice,found==sounds_.end()?nullptr:&found->second);
}
bool SdlAudio::can_restore(const SdlAudioSnapshot& state)const{VoiceSnapshot voice;return prepare_restore(state,voice);}
bool SdlAudio::restore(const SdlAudioSnapshot& state){
    VoiceSnapshot adjusted;if(!prepare_restore(state,adjusted))return false;const auto found=sounds_.find(state.resource);
    // Freeze before replacing the playhead so a callback cannot advance the
    // restored position between unlock and applying the saved pause state.
    SDL_PauseAudioDevice(device_,1);
    SDL_LockAudioDevice(device_);
    const bool result=voice_.restore(adjusted,found==sounds_.end()?nullptr:&found->second);
    if(result){
        read_command_.store(write_command_.load(std::memory_order_relaxed),std::memory_order_release);
        volume_=requested_volume_=state.volume;
    }
    SDL_UnlockAudioDevice(device_);
    if(result)paused_=state.paused;
    SDL_PauseAudioDevice(device_,paused_?1:0);
    return result;
}
}
