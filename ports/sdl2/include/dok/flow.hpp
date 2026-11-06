#pragma once
#include "world.hpp"
namespace dok {
enum class SessionPhase { playing, retry, game_over, completed };
// Observe once after a simulation frame. Retry confirmation is a port UI policy;
// the restored state follows the native check-death routine.
class GameFlow {
public:
    explicit GameFlow(World& world);
    struct Snapshot {Pose spawn;SessionPhase phase=SessionPhase::playing;};
    Snapshot snapshot()const {return {spawn_,phase_};}
    void restore(Snapshot s) {if(s.phase!=SessionPhase::playing)throw std::runtime_error("Only live sessions can be restored");spawn_=s.spawn;phase_=s.phase;}
    SessionPhase observe();
    bool retry();
    SessionPhase phase() const noexcept { return phase_; }
private:
    World& world_;
    Pose spawn_;
    SessionPhase phase_=SessionPhase::playing;
};
}
