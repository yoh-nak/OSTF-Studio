#pragma once
#include "JapaneseMora.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace ostfstudio::singing {
struct RecordedMora {
    std::vector<float> frames;
    double rate{}, period{};
    int loopStart{}, loopEnd{}, fade{}, consonantFrames{};

    // Read at the source rate inside each grain. Changing the spacing of the
    // grains changes the sung pitch without shifting the vowel's resonances.
    float pitchedGrain(double centre, double phase, double targetPeriod) const {
        const double mark = std::round(centre / period) * period;
        const auto sample = [&](double at) {
            at = std::clamp(at, 0., double(frames.size() - 2));
            const int i = int(at);
            return std::lerp(frames[i], frames[i + 1], float(at - i));
        };
        const double blend = .5 - .5 * std::cos(juce::MathConstants<double>::pi * phase);
        return std::lerp(sample(mark + phase * targetPeriod),
                         sample(mark + (phase - 1.) * targetPeriod), float(blend));
    }

    float read(double& position, double step) const {
        if (position >= loopEnd) {
            const int restart = loopStart + fade;
            position = restart + std::fmod(position - loopEnd, double(loopEnd - restart));
        }
        auto linear = [&](double pos) {
            const auto i = int(pos);
            return frames[i] + float(pos - i) * (frames[i + 1] - frames[i]);
        };
        float out = linear(position);
        if (position > loopEnd - fade) {
            const auto overlap = position - (loopEnd - fade);
            out += float(overlap / fade) * (linear(loopStart + overlap) - out);
        }
        position += step;
        return out;
    }
};

struct VoiceBank {
    juce::File folder;
    juce::String name;
    juce::File portrait;
    std::map<int, RecordedMora> sounds;
    static std::unique_ptr<VoiceBank> load(const juce::File& directory, juce::String& error);
    static const VoiceBank& factory();
    static const std::array<RecordedMora, 40>& english();
    const RecordedMora* find(Phone p) const {
        auto it = sounds.find(p.sampleKey());
        return it == sounds.end() ? nullptr : &it->second;
    }
};
}
