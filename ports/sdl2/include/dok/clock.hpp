#pragma once
#include <cstdint>
#include <stdexcept>

namespace dok {
// Logical time uses the DOS PIT/119 scale. Host elapsed time is monotonic;
// pausing is implemented by not submitting elapsed time, never by changing FPS.
class GameClock {
public:
    struct State {std::uint64_t ticks=0,remainder=0;};
    State snapshot() const noexcept {return {ticks_,remainder_};}
    void restore(State value) {if(value.remainder>=denominator)throw std::runtime_error("Invalid clock remainder");ticks_=value.ticks;remainder_=value.remainder;}
    static constexpr std::uint64_t pit_hz = 1193182;
    static constexpr std::uint64_t denominator = 119000000000ULL;
    void advance_nanoseconds(std::uint64_t elapsed) noexcept {
        // Split before multiplying so even UINT64_MAX elapsed is well defined.
        const auto whole = elapsed / denominator;
        const auto fraction = (elapsed % denominator) * pit_hz + remainder_;
        ticks_ += whole * pit_hz + fraction / denominator;
        remainder_ = fraction % denominator;
    }
    std::uint64_t ticks() const noexcept { return ticks_; }
    std::uint32_t native_ticks() const noexcept { return static_cast<std::uint32_t>(ticks_); }
    std::uint64_t remainder() const noexcept { return remainder_; }
private:
    std::uint64_t ticks_ = 0;
    std::uint64_t remainder_ = 0;
};

// Explicit native unsigned arithmetic, including wrap and separate DIVs.
// OG Ghidra MZ 2a5b:015b..0194; PKR/PKE clock references in SP05.
inline std::uint32_t original_pit_ticks(std::uint32_t interrupts,
                                      std::uint32_t period,
                                      std::uint16_t calibration,
                                      std::uint16_t counter,
                                      std::uint32_t base) noexcept {
    const auto phase = static_cast<std::uint16_t>(calibration - counter);
    return static_cast<std::uint32_t>(interrupts * period) / 119u + phase / 119u + base;
}

class Stopwatch {
public:
    struct State {std::uint32_t started=0,elapsed=0;bool running=false;};
    State snapshot() const noexcept {return {started_,elapsed_,running_};}
    void restore(State value) noexcept {started_=value.started;elapsed_=value.elapsed;running_=value.running;}
    void start(std::uint32_t now) noexcept {
        if (!running_) { started_ = now; running_ = true; }
    }
    std::uint32_t read(std::uint32_t now) const noexcept {
        return running_ ? now - started_ : elapsed_;
    }
    void stop(std::uint32_t now) noexcept {
        elapsed_ = read(now);
        running_ = false;
    }
    bool running() const noexcept { return running_; }
private:
    std::uint32_t started_ = 0, elapsed_ = 0;
    bool running_ = false;
};
} // namespace dok
