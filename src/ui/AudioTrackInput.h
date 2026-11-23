#pragma once
#include <juce_core/juce_core.h>

namespace ostfstudio {
// Device channel numbers stay stable as other inputs are enabled or disabled.
// JUCE delivers enabled channels packed in ascending device order.
struct AudioTrackInput {
    int first = -1;
    double gainDb = 0;
    int physical(const juce::BigInteger& active, int channels) const {
        if (channels < 1 || channels > 2) return -1;
        for (int ch = first < 0 ? 0 : first; ch < 64; ++ch) {
            if (active[ch] && (channels == 1 || (ch < 63 && active[ch + 1]))) return ch;
            if (first >= 0) break;
        }
        return -1;
    }
    int bufferIndex(const juce::BigInteger& active, int channels) const {
        int ch = physical(active, channels);
        return ch < 0 ? -1 : packedIndex(active, ch);
    }
private:
    static int packedIndex(const juce::BigInteger& active, int ch) {
        int index = 0;
        for (int i = 0; i < ch; ++i) if (active[i]) ++index;
        return index;
    }
};

}
