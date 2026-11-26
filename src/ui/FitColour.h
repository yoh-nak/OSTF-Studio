#pragma once
#include <juce_graphics/juce_graphics.h>

namespace ostfstudio {
inline juce::Colour fitColour(int percent) {
    return juce::Colour(percent < 0 ? 0xffa5afb8 : percent < 50 ? 0xfffa9499 : percent < 75 ? 0xfff6ad69 :
                        percent < 90 ? 0xfff4d46f : percent < 100 ? 0xff91c8ec : 0xff91dbbf);
}
}
