#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ostfstudio::console {
inline const juce::Colour lcdInk{0xffa4e9d4};
inline void drawLcd(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.isEmpty()) return;
    auto frame = area.toFloat();
    g.setColour(juce::Colour(0xff111b1e));
    g.fillRoundedRectangle(frame, 4.f);
    g.setColour(juce::Colour(0xff586369));
    g.drawRoundedRectangle(frame.reduced(.5f), 4.f, 1.f);
    auto glass = frame.reduced(3.f);
    juce::ColourGradient backlight(juce::Colour(0xff0c1315), glass.getTopLeft(),
                                   juce::Colour(0xff090d10), glass.getBottomLeft(), false);
    backlight.addColour(.12, juce::Colour(0xff172123));
    g.setGradientFill(backlight);
    g.fillRoundedRectangle(glass, 2.f);
    g.setColour(juce::Colours::black.withAlpha(.025f));
    for (int y = area.getY() + 5; y < area.getBottom()-3; y += 3)
        g.drawHorizontalLine(y, glass.getX()+1, glass.getRight()-1);
}
}
