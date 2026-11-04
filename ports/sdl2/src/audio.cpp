#include "dok/audio.hpp"
#include <algorithm>
#include <stdexcept>
namespace dok {
void apply_audio_gain(std::int16_t* samples,std::size_t count,int percent){
    percent=std::clamp(percent,0,100);
    for(std::size_t i=0;i<count;++i)samples[i]=static_cast<std::int16_t>(int(samples[i])*percent/100);
}
bool MonoVoice::play(SoundId id,const Sound& sound,bool loop,bool old_gold){
    if(id_){
        // OG wrappers mutate Sound+7 before admission; its IRQ reads bit40
        // directly. Kremlin DMA uses a separate latch and ignores this change.
        if(old_gold_&&old_gold&&id==id_)loop_=loop;
        return false;
    }
    if(!id||sound.samples.empty()||!sound.frequency||sound.frequency>65535) return false;
    sample_count_=old_gold?std::find(sound.samples.begin(),sound.samples.end(),0)-sound.samples.begin():sound.samples.size();
    source_clock_=old_gold?1193182:1000000;
    const auto og_rate=sound.frequency<32768?static_cast<std::int32_t>(sound.frequency):static_cast<std::int32_t>(sound.frequency)-65536;
    period_=old_gold?(static_cast<std::uint32_t>(0x1234dd/og_rate)&255u):(((1000000u+sound.frequency/2)/sound.frequency)&255u);
    if(!period_)period_=old_gold?65536:256;
    sound_=&sound;id_=id;loop_=loop;old_gold_=old_gold;phase_=0;output_rate_=0;return true;
}
void MonoVoice::stop(SoundId id){if(!id||id==id_){sound_=nullptr;id_=0;phase_=0;output_rate_=0;}}
VoiceSnapshot MonoVoice::snapshot()const noexcept{return id_?VoiceSnapshot{id_,loop_,old_gold_,output_rate_,phase_}:VoiceSnapshot{};}
bool MonoVoice::restore(const VoiceSnapshot& state,const Sound* sound){
    MonoVoice next;
    if(!state.id){
        if(state.phase||state.output_rate)return false;
    }else{
        if(!sound||!next.play(state.id,*sound,state.loop,state.old_gold))return false;
        if(!state.output_rate&&state.phase)return false;
        if(state.output_rate){
            const auto ticks=next.sample_count_+(next.old_gold_?1:0);
            if(ticks>UINT64_MAX/next.period_/state.output_rate)return false;
            const auto duration=std::uint64_t(next.period_)*state.output_rate*ticks;
            if(state.phase>duration&&state.phase-duration>next.source_clock_)return false;
        }
        next.output_rate_=state.output_rate;next.phase_=state.phase;
    }
    *this=next;return true;
}
void MonoVoice::render(std::int16_t* out,std::size_t count,unsigned rate){
    if(!rate)throw std::invalid_argument("Invalid output rate");
    if(!sound_){std::fill_n(out,count,0);return;}
    if(output_rate_&&output_rate_!=rate)throw std::invalid_argument("Output rate changed during playback");
    output_rate_=rate;
    for(std::size_t i=0;i<count;++i){
        out[i]=0;if(!sound_)continue;
        // OG's zero-terminator IRQ resets the pointer without writing the DAC.
        // Thus each loop holds the final byte for one extra PIT interval.
        const auto duration=std::uint64_t(period_)*rate*(sample_count_+(old_gold_&&loop_?1:0));
        if(phase_>=duration){if(loop_)phase_%=duration;else{stop();continue;}}
        const auto at=phase_/(std::uint64_t(period_)*rate);
        const auto value=at<sample_count_?sound_->samples[at]:(sample_count_?sound_->samples[sample_count_-1]:128);
        out[i]=static_cast<std::int16_t>((int(value)-128)*256);
        phase_+=source_clock_;
    }
}
}
