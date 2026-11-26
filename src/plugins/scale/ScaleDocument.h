#pragma once
#include <juce_core/juce_core.h>

namespace ostfstudio::scale {
struct Scale { const char* name; std::vector<int> intervals; };
const std::vector<Scale>& catalog();
inline constexpr int maxBytes = 24 * 1024 * 1024;
class Document {
public:
    juce::Result importWorkspaceScales(const juce::String& edited, const juce::String& openedHash);
    juce::Result load(const juce::String& utf8);
    juce::Result saveAs(const juce::File& file) const;
    juce::Result exportForStudio(const juce::File& directory, juce::File& written) const;
    juce::Result assign(int chord, int tonic, int mode);
    std::vector<int> scope(int first, int last, bool repeats) const;
    juce::Result assign(const std::vector<int>& indices, int tonic, int mode);
    struct Assignment {int chord, tonic, mode;};
    juce::Result assignPlan(const std::vector<Assignment>&);
    juce::String text, baseHash;
    juce::var score;
};
struct LinkedScoreFile {
    juce::Result read(const juce::File& source, juce::String& text);
    juce::Result write(const Document& doc, const juce::File& target, bool workspaceScales=false);
    juce::File file;
private:
    juce::String diskHash, openedText;
};
juce::String digest(const juce::String& text);
extern const char* const tonicNames[12];
}
