#include "dok/audio.hpp"
#include "dok/sdl_audio.hpp"
#include "dok/frontend.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(bool value,const char* what){if(!value)throw std::runtime_error(what);}
static void u16(std::ostream& out,unsigned value){out.put(char(value));out.put(char(value>>8));}
static void u32(std::ostream& out,std::uint32_t value){u16(out,value);u16(out,value>>16);}
static void wav(const std::filesystem::path& path,const std::vector<std::int16_t>& samples){
    std::ofstream out(path,std::ios::binary);out.write("RIFF",4);u32(out,36+2*samples.size());out.write("WAVEfmt ",8);
    u32(out,16);u16(out,1);u16(out,1);u32(out,48000);u32(out,96000);u16(out,2);u16(out,16);
    out.write("data",4);u32(out,2*samples.size());for(auto value:samples)u16(out,static_cast<std::uint16_t>(value));
    check(bool(out),"WAV write failed");
}
static void matrix(){
    for(int count:{0,1,2,3})for(bool initial:{false,true})for(int request:{-1,0,1})for(int identity:{0,1}){
        dok::Sound sound;sound.frequency=16000;for(int i=0;i<count;++i)sound.samples.push_back(static_cast<std::uint8_t>(32+32*i));sound.samples.push_back(0);
        dok::MonoVoice voice;check(voice.play(1,sound,initial,true),"OG start rejected");
        std::cout<<count<<','<<initial<<','<<request<<','<<identity;
        for(int tick=0;tick<7;++tick){
            if(tick==1&&request>=0)voice.play(identity?2:1,sound,request!=0,true);
            std::array<std::int16_t,74> frame{};voice.render(frame.data(),frame.size(),1193182);
            std::cout<<','<<voice.active()<<','<<frame[0];
        }
        std::cout<<'\n';
    }
}
static void rates(){
    for(bool og:{false,true})for(unsigned frequency:{1u,10u,100u,256u,257u,1000u,3840u,4000u,8000u,11025u,16000u,22050u,44100u,65535u}){
        dok::Sound sound;sound.frequency=frequency;sound.samples={32,64,0};dok::MonoVoice voice;
        check(voice.play(1,sound,false,og),"Rate fixture rejected");
        std::vector<std::int16_t> data(65537);voice.render(data.data(),data.size(),og?1193182:1000000);
        auto first=std::find(data.begin(),data.end(),std::int16_t(-16384));
        check(first!=data.end(),"Rate fixture never reached its second sample");
        std::cout<<og<<','<<frequency<<','<<(first-data.begin())<<'\n';
    }
}
int main(int argc,char** argv){try{
    SDL_SetMainReady();
    if(argc==2&&std::string(argv[1])=="--native-matrix"){matrix();return 0;}
    if(argc==2&&std::string(argv[1])=="--rate-matrix"){rates();return 0;}
    check(argc>=3,"Usage: dok_audio_probe OUTPUT_DIRECTORY RES...");
    const auto output=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(output);
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);check(SDL_Init(SDL_INIT_AUDIO)==0,"SDL audio init failed");
    for(int argument=2;argument<argc;++argument){
        auto archive=dok::Archive::open(std::filesystem::u8path(argv[argument]));const bool og=archive.identify()==dok::DatasetId::old_gold;
        const auto profile=og?"old-gold":archive.identify()==dok::DatasetId::kremlin_pkr?"ru":"en";
        auto folder=output/profile;std::filesystem::create_directories(folder);
        unsigned count=0;
        for(const auto& resource:archive.entries()){
            const auto bytes=archive.get(resource.name);if(bytes.size<36||bytes.data[2]!='S'||bytes.data[3]!='V'||bytes.data[4]!='M')continue;
            auto sound=archive.sound(resource.name);dok::MonoVoice voice;
            check(voice.play(1,sound,false,og),"Original sound rejected");
            std::vector<std::int16_t> samples;std::array<std::int16_t,4096> chunk{};
            while(voice.active()){
                voice.render(chunk.data(),chunk.size(),48000);samples.insert(samples.end(),chunk.begin(),chunk.end());
                check(samples.size()<48000*120,"Sound failed to terminate");
            }
            const auto filename=resource.name==dok::cp866("ЗАСТАВКА_ЗВУК")?"title.wav":"sound-"+std::to_string(count)+".wav";
            wav(folder/filename,samples);++count;
        }
        dok::SdlAudio audio(archive);const auto title=dok::cp866("ЗАСТАВКА_ЗВУК");
        audio.command({dok::AudioAction::once,1,title});SDL_Delay(35);audio.pause(true);
        auto state=audio.snapshot();check(state.voice.id==1&&state.resource==title&&state.paused,"SDL active voice snapshot mismatch");
        SDL_Delay(25);check(audio.snapshot().voice.phase==state.voice.phase,"Paused audio advanced");
        audio.volume(33);state=audio.snapshot();check(state.volume==33,"SDL gain not applied");
        audio.command({dok::AudioAction::once,2,title});check(audio.snapshot().voice.id==1,"Busy identity admission changed voice");
        auto bad=state;bad.resource="missing";check(!audio.restore(bad),"Unknown snapshot resource admitted");
        check(audio.snapshot().voice.phase==state.voice.phase,"Failed restore mutated playhead");
        audio.command({dok::AudioAction::stop_all,0,{}});check(!audio.snapshot().voice.id,"Stop all failed");
        check(audio.restore(state),"Valid snapshot rejected");check(audio.snapshot().voice.phase==state.voice.phase,"Restored phase differs");
        audio.pause(false);SDL_Delay(35);audio.pause(true);check(audio.snapshot().voice.phase>state.voice.phase,"Restored voice did not resume");
        std::cout<<profile<<": "<<count<<" source sounds rendered; SDL pause/identity/gain/transactional snapshot PASS\n";
    }
    SDL_Quit();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}}
