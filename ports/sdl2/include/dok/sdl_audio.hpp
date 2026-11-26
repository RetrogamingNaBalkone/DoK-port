#pragma once
#include "audio.hpp"
#include <SDL.h>
#include <atomic>
#include <map>
namespace dok {
struct SdlAudioSnapshot {VoiceSnapshot voice;std::string resource;bool paused=false;int volume=100;};
class SdlAudio {
public:
    explicit SdlAudio(const Archive& archive);
    ~SdlAudio();
    SdlAudio(const SdlAudio&)=delete;
    SdlAudio& operator=(const SdlAudio&)=delete;
    void command(const AudioCommand& command);
    void pause(bool paused);
    void volume(int percent);
    SdlAudioSnapshot snapshot();
    bool can_restore(const SdlAudioSnapshot& state) const;
    bool restore(const SdlAudioSnapshot& state);
private:
    struct PendingSound {AudioAction action;SoundId id;const Sound* sound;};
    void consume_commands();
    bool prepare_restore(const SdlAudioSnapshot&,VoiceSnapshot&) const;
    static void callback(void* self,Uint8* stream,int bytes);
    SDL_AudioDeviceID device_=0;
    unsigned rate_=48000;
    bool old_gold_=false;
    int volume_=100;
    // Main-thread control state; callback reads volume_ under SDL's audio lock.
    int requested_volume_=100;
    bool paused_=false;
    MonoVoice voice_;
    std::map<std::string,Sound> sounds_;
    // One producer (main thread); SDL serializes the callback and snapshots.
    std::array<PendingSound,256> commands_{};
    std::atomic<unsigned> read_command_{0},write_command_{0};
};
}
