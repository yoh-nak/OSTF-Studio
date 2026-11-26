#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace ostfstudio {
class CompressorListen {
public:
    enum State { idle, requested, listening, ready, noSignal };
    std::atomic<State> state{idle};
    // All measurement fields belong to the audio thread. Publish the completed result through state.
    void beginBlock(double sampleRate) {
        if (state.load() != requested) return;
        window = std::max(1, static_cast<int>(std::round(sampleRate * 0.02)));
        samples = windows = active = 0;
        peak = 0;
        histogram.fill(0);
        state.store(listening);
    }
    void measure(float magnitude) {
        peak = std::max(peak, magnitude);
        if (++samples < window) return;
        if (peak >= .001f && std::isfinite(peak)) {
            const int db = std::clamp(static_cast<int>(std::lround(20 * std::log10(peak))), -60, 0);
            ++histogram[static_cast<size_t>(db + 60)];
            ++active;
        }
        peak = 0;
        samples = 0;
        if (++windows < 150) return;
        if (!active) { state.store(noSignal); return; }
        int count = 0;
        for (int i = 0; i <= 60; ++i) {
            count += histogram[static_cast<size_t>(i)];
            if (count * 10 >= active * 9) {
                peakDb = static_cast<float>(i - 60);
                break;
            }
        }
        state.store(ready);
    }
    // Three dB of static gain reduction at the 90th percentile of active 20 ms peaks.
    float threshold(float ratio) const {
        return std::clamp(peakDb - 3.0f / (1.0f - 1.0f / ratio), -48.0f, 0.0f);
    }
private:
    int window{}, samples{}, windows{}, active{};
    float peak{}, peakDb{};
    std::array<int, 61> histogram{};
};
}
