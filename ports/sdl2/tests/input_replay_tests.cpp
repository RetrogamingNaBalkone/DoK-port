#include "dok/input_replay.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
static void check(bool v){if(!v)throw std::runtime_error("Input replay regression");}
int main(){try{
    std::istringstream source("DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n416 83 1 -0.5 0 1 0 1 0 2 0 0\n832 90 0 0 1 0 0 0 1 -1 1 0\n");
    auto replay=dok::InputReplay::read(source);check(!replay.inertia&&replay.dataset=="old_gold"&&replay.level=="LEVEL4"&&!replay.finished());
    auto first=replay.take(416);check(first.heading==83&&first.actions.forward==1&&first.actions.strafe==-0.5f&&first.actions.run&&first.actions.fire&&first.actions.select_weapon==2);
    auto second=replay.take(832);check(second.actions.use&&second.actions.next_weapon&&replay.finished());
    std::ostringstream output;output<<"DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n";dok::InputReplay::write_sample(output,first);dok::InputReplay::write_sample(output,second);
    std::istringstream roundtrip(output.str());auto loaded=dok::InputReplay::read(roundtrip);check(loaded.take(416).actions.strafe==-0.5f&&loaded.take(832).actions.use);
    for(const auto& text:{"","DOK_INPUT_REPLAY 2 old_gold LEVEL4 0\n416 0 0 0 0 0 0 0 0 -1 0 0\n",
        "DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n416 360 0 0 0 0 0 0 0 -1 0 0\n",
        "DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n416 0 2 0 0 0 0 0 0 -1 0 0\n",
        "DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n416 0 0 0 0 2 0 0 0 -1 0 0\n",
        "DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n416 0 0 0 0 0 0 0 0 9 0 0\n",
        "DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n416 0 0 0 0 0 0 0 0 -1 0 0 trailing\n",
        "DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n416 0 0 0 0 0 0 0 0 -1 0 0\n416 0 0 0 0 0 0 0 0 -1 0 0\n"}){
        bool rejected=false;try{std::istringstream input(text);dok::InputReplay::read(input);}catch(const std::runtime_error&){rejected=true;}check(rejected);
    }
    bool divergence=false;try{loaded.take(1248);}catch(const std::runtime_error&){divergence=true;}check(divergence);
    std::cout<<"Logical replay roundtrip, ranges, ordering and callback divergence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
