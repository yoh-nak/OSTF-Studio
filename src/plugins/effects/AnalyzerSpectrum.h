#pragma once
#include "AnalyzerMeters.h"
#include <juce_dsp/juce_dsp.h>

namespace ostfstudio::analyzer {
class Spectrum {
public:
    template <class Sample> void measure(int channel, int available, Sample sample) {
        auto& output = bins[channel];
        if (available < fftSize) {
            output.fill(0);
            return;
        }
        for (int i = 0; i < fftSize; ++i)
            work[i] = sample(i);
        window.multiplyWithWindowingTable(work.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(work.data(), true);
        for (int k = 0; k <= fftSize / 2; ++k)
            output[k] = work[k] * ((k == 0 || k == fftSize / 2) ? 1.f : 2.f) / fftSize;
    }
    std::array<std::array<float, fftSize / 2 + 1>, 2> bins{};

private:
    juce::dsp::FFT fft{12};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann, true};
    std::array<float, fftSize * 2> work{};
};
} // namespace ostfstudio::analyzer
