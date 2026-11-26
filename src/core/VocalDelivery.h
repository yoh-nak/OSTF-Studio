#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ostfstudio::singing {
inline double scoopCents(double seconds, double intensity) {
    const auto remaining = std::max(0.0, 1.0 - seconds/.12);
    return -140.0 * intensity * remaining * remaining;
}
inline double vibratoCents(double seconds, double intensity) {
    if (seconds <= .09) return 0;
    return 65.0 * intensity * std::min(1.0,(seconds-.09)/.14)
        * std::sin(2.0 * std::numbers::pi * 5.4 * seconds);
}
}
