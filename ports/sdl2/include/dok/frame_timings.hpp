#pragma once
#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace dok {
// Opt-in diagnostic capture. No disk I/O inside the measured frame.
class FrameTimings {
    struct Sample { double work,present,total; };
    std::vector<Sample> samples_;
    std::filesystem::path path_;
    Uint64 start_=0,ready_=0;
public:
    FrameTimings() {
        const char* path=std::getenv("DOK_FRAME_TIMINGS");
        if(path&&*path){path_=std::filesystem::u8path(path);
            if(std::filesystem::exists(path_))throw std::runtime_error("Frame timing output already exists");
            samples_.reserve(120000);
        }
    }
    void begin(){if(!path_.empty())start_=SDL_GetPerformanceCounter();}
    void ready(){if(!path_.empty())ready_=SDL_GetPerformanceCounter();}
    void end(){if(!path_.empty()&&samples_.size()<120000){
        const auto end=SDL_GetPerformanceCounter();const double scale=1000.0/SDL_GetPerformanceFrequency();
        samples_.push_back({(ready_-start_)*scale,(end-ready_)*scale,(end-start_)*scale});
    }}
    void write()const{
        if(path_.empty())return;
        std::ofstream file(path_);if(!file)throw std::runtime_error("Cannot write frame timings");
        file<<"frame,work_ms,present_ms,total_ms\n";
        for(std::size_t i=0;i<samples_.size();++i)file<<i<<','<<samples_[i].work<<','<<samples_[i].present<<','<<samples_[i].total<<'\n';
        file.flush();if(!file)throw std::runtime_error("Frame timing write failed");
    }
};
}
