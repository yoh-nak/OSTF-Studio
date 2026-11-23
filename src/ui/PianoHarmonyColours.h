#pragma once
#include "ui/TheoryGuide.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoHarmony {
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

inline juce::Colour degreeColour(int pitch, int root) {
    static constexpr juce::uint32 palette[] {
        0xffffce69, 0xffc58f83, 0xfff2a56e, 0xffa5c973, 0xff76c9a3, 0xff67c5ce,
        0xff91a8ce, 0xff83b9f4, 0xffa79cda, 0xffb795ef, 0xffdc93be, 0xfff4a1ae
    };
    return root >= 0 ? juce::Colour(palette[(pitch + 12 - root) % 12]) : toneColour(Tone::unknown);
}
}
