#include "dok/audio.hpp"
#include <algorithm>
#include <stdexcept>
namespace dok {
bool MonoVoice::play(SoundId id,const Sound& sound,bool loop,bool old_gold){
    if(id_)return false;
    if(!id||sound.samples.empty()||!sound.frequency||sound.frequency>65535) return false;
    sample_count_=old_gold?std::find(sound.samples.begin(),sound.samples.end(),0)-sound.samples.begin():sound.samples.size();
    if(!sample_count_)return false;
    source_clock_=old_gold?1193182:1000000;
    period_=old_gold?((0x1234ddu/sound.frequency)&255u):(((1000000u+sound.frequency/2)/sound.frequency)&255u);
    if(!period_)period_=old_gold?65536:256;
    sound_=&sound;id_=id;loop_=loop;phase_=0;output_rate_=0;return true;
}
void MonoVoice::stop(SoundId id){if(!id||id==id_){sound_=nullptr;id_=0;phase_=0;output_rate_=0;}}
void MonoVoice::render(std::int16_t* out,std::size_t count,unsigned rate){
    if(!rate)throw std::invalid_argument("Invalid output rate");
    if(output_rate_&&output_rate_!=rate)throw std::invalid_argument("Output rate changed during playback");
    output_rate_=rate;
    for(std::size_t i=0;i<count;++i){
        out[i]=0;if(!sound_)continue;
        const auto duration=std::uint64_t(period_)*rate*sample_count_;
        if(phase_>=duration){if(loop_)phase_%=duration;else{stop();continue;}}
        const auto at=phase_/(std::uint64_t(period_)*rate);
        out[i]=static_cast<std::int16_t>((int(sound_->samples[at])-128)*256);
        phase_+=source_clock_;
    }
}
}
