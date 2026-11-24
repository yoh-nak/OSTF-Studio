#pragma once
#include "JapaneseMora.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace ostfstudio::singing {
struct RecordedMora {
    std::vector<float> frames;
    double rate{}, period{};
    int loopStart{}, loopEnd{}, fade{}, consonantFrames{};

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
    const RecordedMora* find(Phone p) const {
        auto it = sounds.find(p.sampleKey());
        return it == sounds.end() ? nullptr : &it->second;
    }
};
}
