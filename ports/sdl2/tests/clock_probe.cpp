#include "dok/clock.hpp"
#include <iostream>
int main() {
    std::uint32_t interrupts, period, calibration, counter, base;
    while (std::cin >> interrupts >> period >> calibration >> counter >> base) {
        if (calibration > 65535 || counter > 65535) return 2;
        std::cout << dok::original_pit_ticks(interrupts, period,
            static_cast<std::uint16_t>(calibration), static_cast<std::uint16_t>(counter), base) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
