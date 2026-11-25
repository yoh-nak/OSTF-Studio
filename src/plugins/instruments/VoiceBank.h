#pragma once
#include "JapaneseMora.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace ostfstudio::singing {
struct RecordedMora {
    std::vector<float> frames;
    double rate{}, period{};
    int loopStart{}, loopEnd{}, fade{}, consonantFrames{};
    int voicedStart{}; // First sustained, periodic cycle in the attack, in source frames.
    std::vector<int> pitchMarks;
    // Octave-spaced low-pass versions used when a period is played faster than recorded.
    std::vector<std::vector<float>> filtered;
    void preparePitchMarks();
    float cycleSample(double at, double step) const {
        if (step <= 1. || filtered.empty()) return sampleAt(at);
        const auto level = std::clamp(std::log2(step),0.,double(filtered.size()));
        const int low = int(level), high = std::min(low+1,int(filtered.size()));
        const auto lookup = [&](int octave) {
            if (!octave) return sampleAt(at);
            const auto& data=filtered[size_t(octave-1)];
            const auto pos=std::clamp(std::ldexp(at,-octave),0.,double(data.size()-1));
            const auto i=size_t(pos);
            return std::lerp(data[i],data[std::min(i+1,data.size()-1)],float(pos-i));
        };
        return std::lerp(lookup(low),lookup(high),float(level-low));
    }
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
