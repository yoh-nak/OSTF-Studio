#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

namespace ostfstudio {
struct WarpPoint {
    double position, source;
};

struct ClipTiming {
    bool loop = false;
    std::int64_t cycleTicks = 0; // Zero plays at the recorded sample rate.
    std::vector<WarpPoint> markers;
    bool valid() const {
        if (cycleTicks < 0 || markers.size() > 128 || (!cycleTicks && !markers.empty())) return false;
        WarpPoint previous{0, 0};
        for (auto p : markers) {
            if (!(p.position > previous.position && p.position < 1 && p.source > previous.source && p.source < 1))
                return false;
            previous = p;
        }
        return true;
    }
    double sourcePhase(double position) const {
        if (markers.empty()) return position;
        const auto it = std::upper_bound(markers.begin(), markers.end(), position,
            [](double x, WarpPoint p) { return x < p.position; });
        const auto left = it == markers.begin() ? WarpPoint{0, 0} : *std::prev(it);
        const auto right = it == markers.end() ? WarpPoint{1, 1} : *it;
        return left.source + (position - left.position) * (right.source - left.source) / (right.position - left.position);
    }
    double frameAt(double elapsedFrames, double elapsedTicks, int frames) const {
        auto phase = cycleTicks ? elapsedTicks / double(cycleTicks) : elapsedFrames / frames;
        if (phase < 0) return -1;
        if (loop) phase -= std::floor(phase);
        return phase >= 1 ? double(frames) : sourcePhase(phase) * frames;
    }
};
}
