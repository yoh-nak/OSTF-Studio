#pragma once
#include "ui/TheoryGuide.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoHarmony {
enum class Theme { studioBlue, harmonic };
using Tone = ostfstudio::TheoryGuide::Tone;
inline juce::Colour toneColour(Tone tone) {
    switch (tone) {
        case Tone::root: return juce::Colour(0xfff2c56d);
        case Tone::guide: return juce::Colour(0xff60c9b4);
        case Tone::fifth: return juce::Colour(0xff87c9fa);
        case Tone::tension: return juce::Colour(0xffc4a0ef);
        case Tone::avoid: return juce::Colour(0xffed9564);
        case Tone::chord: return juce::Colour(0xffb0ddb5);
        case Tone::scale: return juce::Colour(0xff94bff0);
        case Tone::outside: return juce::Colour(0xffd6a2bc);
        default: return juce::Colour(0xff88919a);
    }
}
inline juce::String toneName(Tone tone) {
    switch (tone) {
        case Tone::root: return TRANS("Root");
        case Tone::guide: return TRANS("3rd / 7th");
        case Tone::fifth: return TRANS("Perfect fifth");
        case Tone::tension: return TRANS("Available tension");
        case Tone::avoid: return TRANS("Avoid candidate");
        case Tone::chord: return TRANS("Chord tone");
        case Tone::scale: return TRANS("In scale");
        case Tone::outside: return TRANS("Outside reference");
        default: return TRANS("No reference");
    }
}

inline juce::Colour degreeColour(int pitch, int root, Theme theme) {
    static constexpr juce::uint32 blues[] {
        0xffc9edff, 0xff86aace, 0xffa8d5fc, 0xff87c0eb, 0xffa3deeb, 0xff84cedc,
        0xffa2b9dd, 0xff82b9ff, 0xffa4bee5, 0xffbed7fa, 0xff91b6db, 0xffb7e3f4
    };
    static constexpr juce::uint32 palette[] {
        0xffffce69, 0xffc58f83, 0xfff2a56e, 0xffa5c973, 0xff76c9a3, 0xff67c5ce,
        0xff91a8ce, 0xff83b9f4, 0xffa79cda, 0xffb795ef, 0xffdc93be, 0xfff4a1ae
    };
    if (root < 0) return toneColour(Tone::unknown);
    const int interval = (pitch + 12 - root) % 12;
    return juce::Colour(theme == Theme::studioBlue ? blues[interval] : palette[interval]);
}
}
