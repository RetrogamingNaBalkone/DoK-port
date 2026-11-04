#pragma once
#include "contracts.hpp"
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dok {
// Diagnostic logical-input recordings. No position, inventory, health or world
// writes are represented. The ordinary player callback consumes every sample.
struct InputReplaySample {
    std::uint64_t tick=0;
    int heading=0;
    InputActions actions;
};
class InputReplay {
public:
    std::string dataset,level;
    bool inertia=true;
    std::vector<InputReplaySample> samples;
    static InputReplay read(std::istream& in){
        InputReplay replay;std::string line,magic;unsigned version;int inertia;
        if(!std::getline(in,line))throw std::runtime_error("Empty input replay");
        std::istringstream header(line);std::string extra;
        if(!(header>>magic>>version>>replay.dataset>>replay.level>>inertia)||magic!="DOK_INPUT_REPLAY"||version!=1||(inertia!=0&&inertia!=1)||(header>>extra))throw std::runtime_error("Invalid input replay header");
        replay.inertia=inertia!=0;
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;
            InputReplaySample sample;int run,slow,fire,use,next,previous;
            auto& a=sample.actions;std::istringstream row(line);
            if(!(row>>sample.tick>>sample.heading>>a.forward>>a.strafe>>a.turn>>run>>slow>>fire>>use>>a.select_weapon>>next>>previous)||(row>>extra))throw std::runtime_error("Invalid input replay sample");
            for(float axis:{a.forward,a.strafe,a.turn})if(!std::isfinite(axis)||axis < -1||axis > 1)throw std::runtime_error("Invalid input replay axis");
            for(int flag:{run,slow,fire,use,next,previous})if(flag!=0&&flag!=1)throw std::runtime_error("Invalid input replay action");
            if(sample.heading<0||sample.heading>=360||a.select_weapon < -1||a.select_weapon>8||!sample.tick||(!replay.samples.empty()&&sample.tick<=replay.samples.back().tick))throw std::runtime_error("Invalid input replay tick/heading/weapon");
            a.run=run!=0;a.slow=slow!=0;a.fire=fire!=0;a.use=use!=0;a.next_weapon=next!=0;a.previous_weapon=previous!=0;
            replay.samples.push_back(sample);
        }
        if(replay.samples.empty())throw std::runtime_error("Input replay has no samples");
        return replay;
    }
    InputReplaySample take(std::uint64_t tick){
        if(cursor_>=samples.size()||samples[cursor_].tick!=tick)throw std::runtime_error("Input replay diverged from player callback ticks");
        return samples[cursor_++];
    }
    bool finished()const noexcept{return cursor_==samples.size();}
    static void write_sample(std::ostream& out,const InputReplaySample& sample){
        const auto& a=sample.actions;
        out<<sample.tick<<' '<<sample.heading<<' '<<a.forward<<' '<<a.strafe<<' '<<a.turn<<' '<<a.run<<' '<<a.slow<<' '<<a.fire<<' '<<a.use<<' '<<a.select_weapon<<' '<<a.next_weapon<<' '<<a.previous_weapon<<'\n';
    }
private:
    std::size_t cursor_=0;
};
}
