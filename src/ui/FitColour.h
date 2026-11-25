#pragma once
#include <juce_graphics/juce_graphics.h>

namespace ostfstudio {
inline juce::Colour fitColour(int percent) {
    return juce::Colour(percent < 0 ? 0xff9daeb6 : percent < 50 ? 0xffed98a2 : percent < 75 ? 0xffeab68b :
                        percent < 90 ? 0xffe4ce8c : 0xff99d5c4);
}
}
