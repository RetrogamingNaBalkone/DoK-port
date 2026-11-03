#pragma once
#include "world.hpp"
namespace dok {
enum class SessionPhase { playing, retry, game_over, completed };
// Observe once after a simulation frame. Retry confirmation is a port UI policy;
// the restored state follows the native check-death routine.
class GameFlow {
public:
    explicit GameFlow(World& world);
    SessionPhase observe();
    bool retry();
    SessionPhase phase() const noexcept { return phase_; }
private:
    World& world_;
    Pose spawn_;
    SessionPhase phase_=SessionPhase::playing;
};
}
