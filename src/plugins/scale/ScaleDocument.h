#pragma once
#include <juce_core/juce_core.h>

namespace ostfstudio::scale {
struct Scale { const char* name; std::vector<int> intervals; };
const std::vector<Scale>& catalog();
inline constexpr int maxBytes = 24 * 1024 * 1024;
class Document {
public:
    juce::Result load(const juce::String& utf8);
    juce::Result assign(int chord, int tonic, int mode);
    std::vector<int> scope(int first, int last, bool repeats) const;
    juce::Result assign(const std::vector<int>& indices, int tonic, int mode);
    juce::String text, baseHash;
    juce::var score;
};
juce::String digest(const juce::String& text);
extern const char* const tonicNames[12];
}
