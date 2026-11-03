#include "dok/clock.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>

static void check(bool condition) {
    if (!condition) throw std::runtime_error("Clock vector failed");
}
int main() {
    try {
        for (const auto fps : {30u, 60u, 144u}) {
            dok::GameClock clock;
            std::uint64_t previous = 0;
            for (std::uint64_t frame = 1; frame <= fps * 119u; ++frame) {
                const auto now = frame * 1000000000ULL / fps;
                clock.advance_nanoseconds(now - previous);
                previous = now;
            }
            check(clock.ticks() == 1193182 && clock.remainder() == 0);
        }
        dok::GameClock large, split;
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        large.advance_nanoseconds(maximum);
        split.advance_nanoseconds(maximum / 2);
        split.advance_nanoseconds(maximum - maximum / 2);
        check(large.ticks() == split.ticks() && large.remainder() == split.remainder());
        check(large.native_ticks() == static_cast<std::uint32_t>(large.ticks()));
        // Each remainder is discarded separately in the original, not once.
        check(dok::original_pit_ticks(1, 118, 118, 0, 0) == 0);
        check(dok::original_pit_ticks(65536, 65536, 0, 65535, 42) == 42);
        check(dok::original_pit_ticks(0, 0, 0, 1, 0) == 550);
        check(dok::original_pit_ticks(1, 119, 0, 0, 0xffffffffu) == 0);
        dok::Stopwatch watch;
        check(watch.read(100) == 0);
        watch.start(0xfffffff0u);
        watch.start(12); // Starting a running watch must not reset it.
        check(watch.read(16) == 32);
        watch.stop(16);
        check(watch.read(1000) == 32);
        watch.stop(2000);
        check(watch.read(2000) == 32);
        watch.start(2000); // DOS start restarts, it does not resume elapsed time.
        check(watch.read(2001) == 1);
        std::cout << "Clock partition, overflow, PIT and stopwatch vectors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
