#pragma once
#include <juce_core/juce_core.h>

namespace ostfstudio {
struct SongMusicEdit {
    enum Mode { defaults = 1, transform, audition } mode = defaults;
    juce::var key, scale; // Void keeps the authored definition.
    bool removeScale = false;
    double bpm = 0, rate = 1;
    int numerator = 0, denominator = 0, semitones = 0;
};

inline const juce::StringArray& chromaticNames() {
    static const juce::StringArray names {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    return names;
}
inline int pitchClassOf(const juce::var& spelled) {
    const auto step = spelled["step"].toString();
    const int natural = chromaticNames().indexOf(step);
    return step.length() == 1 && natural >= 0 ? (natural + int(spelled["alter"]) + 12) % 12 : -1;
}
inline juce::var spelledClass(int pc) {
    const auto name = chromaticNames()[pc];
    auto* object = new juce::DynamicObject();
    object->setProperty("step", name.substring(0, 1));
    object->setProperty("alter", name.endsWith("b") ? -1 : int(name.endsWith("#")));
    return juce::var(object);
}
inline juce::String shiftedLabel(juce::String label, int shift) {
    if (shift % 12 == 0 || label.isEmpty()) return label;
    auto shiftHead = [shift](juce::String text) {
        if (text.isEmpty()) return text;
        const int pc = chromaticNames().indexOf(text.substring(0, 1));
        if (pc < 0) return text;
        int pos = 1, delta = 0;
        while (pos < text.length()) {
            const auto c = text[pos];
            if (c == '#' || c == 0x266f) ++delta;
            else if (c == 'b' || c == 0x266d) --delta;
            else break;
            ++pos;
        }
        // Only a pitch prefix or a chord-quality suffix identifies a musical label.
        // Free-form names such as "Blues" and "Dorian" can begin with A-G as well.
        if (pos < text.length() && juce::CharacterFunctions::isLetter(text[pos]) && text[pos] < 128) {
            const auto suffix = text.substring(pos);
            bool quality = false;
            for (auto q : {"maj", "min", "dim", "aug", "sus", "add", "omit", "dom", "alt", "no"})
                quality |= suffix.startsWithIgnoreCase(q);
            const bool minor = (text[pos] == 'm' || text[pos] == 'M') && (pos + 1 == text.length()
                || !juce::CharacterFunctions::isLetter(text[pos+1]) || suffix.startsWithIgnoreCase("mMaj"));
            if (!quality && !minor) return text;
        }
        return chromaticNames()[((pc + delta + shift) % 12 + 12) % 12] + text.substring(pos);
    };
    const int slash = label.lastIndexOfChar('/');
    return slash < 0 ? shiftHead(label) : shiftHead(label.substring(0, slash)) + "/" + shiftHead(label.substring(slash + 1));
}
}
