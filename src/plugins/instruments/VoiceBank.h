#pragma once
#include "JapaneseMora.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace ostfstudio::singing {
struct RecordedMora {
    std::vector<float> frames;
    double rate{}, period{};
    int loopStart{}, loopEnd{}, fade{}, consonantFrames{};
    std::vector<int> pitchMarks;
    void preparePitchMarks();
    int nearestMark(double at) const {
        const auto next = std::lower_bound(pitchMarks.begin(), pitchMarks.end(), at);
        if (next == pitchMarks.begin()) return *next;
        if (next == pitchMarks.end() || at - *(next-1) < *next - at) return *(next-1);
        return *next;
    }
    float sampleAt(double at) const {
        at = std::clamp(at, 0., double(frames.size()-2));
        const int i = int(at);
        return std::lerp(frames[i],frames[i+1],float(at-i));
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
