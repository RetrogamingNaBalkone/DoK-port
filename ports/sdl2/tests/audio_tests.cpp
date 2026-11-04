#include "dok/audio.hpp"
#include <stdexcept>
#include <iostream>
#include <array>
static void check(bool v){if(!v)throw std::runtime_error("Mono audio invariant failed");}
int main(){try{
    dok::Sound sample;sample.frequency=10000;sample.samples={0,128,255};dok::MonoVoice voice;
    check(voice.play(1,sample,true));check(!voice.play(2,sample,false));check(!voice.play(1,sample,false));
    std::int16_t out[7];voice.render(out,7,10000);
    check(out[0]==-32768&&out[1]==0&&out[2]==32512&&out[3]==out[0]&&out[6]==out[0]);
    voice.stop(2);check(voice.active()==1);voice.stop(1);check(!voice.active());
    check(voice.play(2,sample,false));voice.render(out,7,10000);check(!voice.active()&&out[3]==0&&out[6]==0);
    dok::Sound og;og.frequency=16000;og.samples={32,64,0,63};
    check(voice.play(3,og,false,true));std::int16_t native[150];voice.render(native,150,1193182);
    check(native[0]==-24576&&native[73]==-24576&&native[74]==-16384&&native[147]==-16384&&native[148]==0&&!voice.active());
    check(voice.play(3,og,true,true));std::array<std::int16_t,300> held{};voice.render(held.data(),held.size(),1193182);
    check(held[147]==-16384&&held[148]==-16384&&held[221]==-16384&&held[222]==-24576);
    // Same-identity requests mutate OG's live loop flag before failed admission.
    check(!voice.play(3,og,false,true));voice.render(held.data(),held.size(),1193182);check(!voice.active());
    check(voice.play(3,og,true,true));check(!voice.play(4,og,false,true));
    voice.render(held.data(),held.size(),1193182);check(voice.active()==3);voice.stop();
    dok::Sound empty;empty.frequency=16000;empty.samples={0,12};
    check(voice.play(5,empty,true,true));voice.render(out,7,48000);check(voice.active()==5&&out[0]==0);
    check(!voice.play(5,empty,false,true));voice.render(out,7,48000);check(!voice.active());
    for(bool old_gold:{false,true})for(bool loop:{false,true}){
        auto sound=old_gold?og:sample;
        check(voice.play(6,sound,loop,old_gold));
        voice.render(out,7,48000);const auto saved=voice.snapshot();
        std::array<std::int16_t,80> expected{},restored{};voice.render(expected.data(),expected.size(),48000);
        dok::MonoVoice copy;check(copy.restore(saved,&sound));copy.render(restored.data(),restored.size(),48000);
        check(expected==restored&&voice.active()==copy.active());
        auto bad=saved;bad.phase=UINT64_MAX;const auto before=copy.snapshot();
        check(!copy.restore(bad,&sound));check(copy.snapshot().id==before.id&&copy.snapshot().phase==before.phase);
        voice.stop();
    }
    voice.render(out,7,44100);check(!voice.snapshot().id&&!voice.snapshot().output_rate);
    check(voice.restore({},nullptr));
    std::int16_t gain[]={-32768,-1,0,1,32512};dok::apply_audio_gain(gain,5,50);
    check(gain[0]==-16384&&gain[1]==0&&gain[4]==16256);dok::apply_audio_gain(gain,5,-1);check(gain[0]==0&&gain[4]==0);
    std::cout<<"Mono admission/PCM/EOF, OG live loop and sentinel hold, transactional playhead restore passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
