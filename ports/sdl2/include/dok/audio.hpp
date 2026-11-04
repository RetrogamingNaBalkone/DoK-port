#pragma once
#include "resources.hpp"
namespace dok {
using SoundId=std::uint64_t;
enum class AudioAction { once, loop, stop, stop_all };
struct AudioCommand {AudioAction action;SoundId id;std::string resource;};
inline SoundId sound_id(std::uint32_t owner,unsigned binding=0x4c){return (SoundId(owner)<<16)|binding;}
void apply_audio_gain(std::int16_t* samples,std::size_t count,int percent);
// Logical playhead only. The archive resource is resolved by SdlAudio, not saved
// as a pointer. Capture/restore must run under SDL's audio-device lock.
struct VoiceSnapshot {
    SoundId id=0;
    bool loop=false,old_gold=false;
    unsigned output_rate=0;
    std::uint64_t phase=0;
};
class MonoVoice {
public:
    bool play(SoundId id,const Sound& sound,bool loop,bool old_gold=false);
    void stop(SoundId id=0);
    void render(std::int16_t* output,std::size_t samples,unsigned output_rate);
    SoundId active() const noexcept{return id_;}
    VoiceSnapshot snapshot() const noexcept;
    bool restore(const VoiceSnapshot& state,const Sound* sound);
    bool uses(const Sound& sound)const noexcept{return sound_==&sound;}
private:
    const Sound* sound_=nullptr;
    SoundId id_=0;
    bool loop_=false,old_gold_=false;
    unsigned period_=0,output_rate_=0;
    unsigned source_clock_=1000000;
    std::size_t sample_count_=0;
    std::uint64_t phase_=0;
};
}
