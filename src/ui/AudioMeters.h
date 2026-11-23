#pragma once

#include "core/MixerState.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace ostfstudio {
using StereoPeak = std::array<float, 2>;

// One audio producer, one UI consumer. Keep transients between display refreshes.
class PeakMailbox {
public:
    void publish(int side, float amplitude) noexcept {
        auto& peak = peaks_[static_cast<std::size_t>(side)];
        auto old = peak.load(std::memory_order_relaxed);
        while (amplitude > old && std::isfinite(amplitude)
               && !peak.compare_exchange_weak(old, amplitude, std::memory_order_relaxed)) {}
    }
    StereoPeak take() noexcept {
        return {peaks_[0].exchange(0, std::memory_order_relaxed), peaks_[1].exchange(0, std::memory_order_relaxed)};
    }
private:
    static_assert(std::atomic<float>::is_always_lock_free);
    std::array<std::atomic<float>, 2> peaks_{};
};

struct MeterFrame {
    std::array<StereoPeak, 16> tracks{};
    std::array<StereoPeak, mixerOutputCapacity> outputs{};
    StereoPeak cue{};
};
}
