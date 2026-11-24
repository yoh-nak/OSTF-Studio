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

// Average-responding VU envelope. A steady sine at -18 dBFS reads 0 VU.
class VolumeUnit {
public:
    void prepare(double rate) { coefficient = float(-std::expm1(-std::log(100.0) / (.300 * rate))); state = 0; }
    float process(const float* samples, int count, float gain) noexcept {
        for (int i = 0; i < count; ++i) state += coefficient * (std::abs(samples[i] * gain) - state);
        return state * 12.47728f; // pi/2 times 10^(18/20)
    }
private:
    float coefficient = 1, state = 0;
};

struct MasterReadout {
    double momentary = -INFINITY, integrated = -INFINITY;
    float truePeak = 0;
    std::array<StereoPeak, 96> scope{};
};
struct MeterFrame {
    std::array<float, 16> vu{};
    MasterReadout master;
    std::array<StereoPeak, 16> tracks{};
    std::array<StereoPeak, mixerOutputCapacity> outputs{};
    StereoPeak cue{};
};
}
