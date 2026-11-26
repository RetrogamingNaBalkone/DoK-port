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
    struct Sample { double work,present,total,simulation,discovery,world_draw,swap; };
    std::vector<Sample> samples_;
    std::filesystem::path path_;
    Uint64 start_=0,ready_=0,simulation_start_=0,simulation_end_=0,discovery_end_=0,world_end_=0,swap_end_=0;
public:
    FrameTimings() {
        const char* path=std::getenv("DOK_FRAME_TIMINGS");
        if(path&&*path){path_=std::filesystem::u8path(path);
            if(std::filesystem::exists(path_))throw std::runtime_error("Frame timing output already exists");
            samples_.reserve(120000);
        }
    }
    void begin(){if(!path_.empty()){start_=SDL_GetPerformanceCounter();world_end_=0;}}
    void simulation_start(){if(!path_.empty())simulation_start_=SDL_GetPerformanceCounter();}
    void simulation_ready(){if(!path_.empty())simulation_end_=SDL_GetPerformanceCounter();}
    void discovery_ready(){if(!path_.empty())discovery_end_=SDL_GetPerformanceCounter();}
    void world_ready(){if(!path_.empty())world_end_=SDL_GetPerformanceCounter();}
    void ready(){if(!path_.empty())ready_=SDL_GetPerformanceCounter();}
    void swapped(){if(!path_.empty())swap_end_=SDL_GetPerformanceCounter();}
    void end(){if(!path_.empty()&&samples_.size()<120000){
        const auto end=SDL_GetPerformanceCounter();const double scale=1000.0/SDL_GetPerformanceFrequency();
        samples_.push_back({(ready_-start_)*scale,(end-ready_)*scale,(end-start_)*scale,
            (simulation_end_-simulation_start_)*scale,(discovery_end_-simulation_end_)*scale,
            world_end_?(world_end_-discovery_end_)*scale:0.0,(swap_end_-ready_)*scale});
    }}
    void write()const{
        if(path_.empty())return;
        std::ofstream file(path_);if(!file)throw std::runtime_error("Cannot write frame timings");
        file<<"frame,work_ms,present_ms,total_ms,simulation_ms,discovery_ms,world_draw_ms,swap_ms\n";
        for(std::size_t i=0;i<samples_.size();++i){
            const auto& sample=samples_[i];
            file<<i<<','<<sample.work<<','<<sample.present<<','<<sample.total<<','
                <<sample.simulation<<','<<sample.discovery<<','<<sample.world_draw<<','<<sample.swap<<'\n';
        }
        file.flush();if(!file)throw std::runtime_error("Frame timing write failed");
    }
};
}
