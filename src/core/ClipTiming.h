#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

namespace ostfstudio {
struct WarpPoint {
    double position, source;
    bool operator==(const WarpPoint&) const = default;
};

struct ClipTiming {
    bool loop = false;
    std::int64_t cycleTicks = 0; // Zero plays at the recorded sample rate.
    std::vector<WarpPoint> markers;
    std::int64_t leadingTicks = 0; // Silence before the source onset.
    std::int64_t sourceLimitTicks = 0; // Retain a cropped one-shot when its container grows.
    double phaseOffset = 0; // Cycle position at the start of a sliced source.
    bool operator==(const ClipTiming&) const = default;
    bool valid() const {
        if (!std::isfinite(phaseOffset) || phaseOffset < 0 || phaseOffset > 1) return false;
        if (leadingTicks < 0 || sourceLimitTicks < 0) return false;
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
        phase += phaseOffset;
        if (loop) phase -= std::floor(phase);
        return phase >= 1 ? double(frames) : sourcePhase(phase) * frames;
    }
};
}
