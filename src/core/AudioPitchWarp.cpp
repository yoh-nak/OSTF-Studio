#include "core/AudioPitchWarp.h"
#include <algorithm>
#include <array>
#include <signalsmith-stretch.h>
#include <cmath>

namespace ostfstudio {
void renderPitchWarp(const float* const* input, int frames, float* const* output, int length,
                     int channels, double rate, int semitones, const std::function<double(int)>& frameAt) {
    if (!length) return;
    signalsmith::stretch::SignalsmithStretch<float> stretch(0);
    stretch.presetDefault(channels, float(rate));
    stretch.setTransposeSemitones(float(semitones));
    struct Window {
        const float* samples;
        int first, size;
        float operator[](int i) const {
            const int frame = first+i;
            return frame>=0 && frame<size ? samples[frame] : 0.f;
        }
    };
    std::array<Window,2> in{};
    std::array<float*,2> out{};
    // Input leads audible output by both the analysis and synthesis latency.
    // Pad the end with silence so even a short final note gets its full output.
    const auto ahead = [&](int offset) {
        const int look = offset + stretch.outputLatency();
        const double source = frameAt(std::min(length,look)) + std::max(0,look-length);
        return int(std::llround(source)) + stretch.inputLatency();
    };
    int first = int(std::llround(frameAt(0)));
    int next = ahead(0);
    for (int ch=0;ch<channels;++ch) in[ch]={input[ch],first,frames};
    stretch.outputSeek(in,next-first);
    for (int i=0;i<length;) {
        const int count=std::min(256,length-i);
        first=next; next=ahead(i+count);
        for (int ch=0;ch<channels;++ch) {
            in[ch].first=first;
            out[ch]=output[ch]+i;
        }
        stretch.process(in,next-first,out,count);
        i+=count;
    }
}
}
