#include "dok/audio.hpp"
#include <stdexcept>
#include <iostream>
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
    std::cout<<"Mono admission, PCM, EOF and Old Gold PIT/zero terminator passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
