#pragma once
#include "plugins/effects/AnalyzerSpectrum.h"

// The callback fills a stereo window. FFTs and band reduction run on the GUI
// thread only when the master detail page is visible.
class MasterSpectrum {
public:
    using Window = std::array<std::array<float,ostfstudio::analyzer::fftSize>,2>;
    void prepare(double sampleRate) {
        const juce::SpinLock::ScopedLockType guard(lock);
        rate = sampleRate; write = 0; fresh = false;
    }
    void push(float left, float right) noexcept {
        input[0][write]=left; input[1][write]=right;
        if (++write < int(input[0].size())) return;
        write=0;
        const juce::SpinLock::ScopedTryLockType guard(lock);
        if (guard.isLocked()) { published=input; fresh=true; }
    }
    bool read(std::array<float,31>& bands) {
        double sr;
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            if (!fresh) return false;
            working=published; fresh=false; sr=rate;
        }
        constexpr int n=ostfstudio::analyzer::fftSize;
        for (int ch=0;ch<2;++ch) spectrum.measure(ch,n,[&](int i){return working[ch][i];});
        bands.fill(0);
        for (size_t i = 0; i < bands.size(); ++i) {
            const double centre = 20. * std::exp2(i / 3.);
            if (centre > sr * .5) continue;
            const double bin = centre * n / sr;
            const int left = std::max(1, int(std::ceil(bin / std::exp2(1./6.))));
            const int right = std::min(n/2, int(std::floor(bin * std::exp2(1./6.))));
            for (const auto& channel : spectrum.bins) {
                if (left <= right) {
                    for (int k = left; k <= right; ++k) bands[i] = std::max(bands[i], channel[k]);
                } else {
                    const int k = std::clamp(int(bin), 0, n/2-1);
                    bands[i] = std::max(bands[i], std::lerp(channel[k], channel[k+1], float(bin-k)));
                }
            }
        }
        return true;
    }
private:
    juce::SpinLock lock;
    Window input{},published{},working{};
    ostfstudio::analyzer::Spectrum spectrum;
    int write=0;
    double rate=48000;
    bool fresh=false;
};
