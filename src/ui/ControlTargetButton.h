#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// All assignable surfaces use the same two-knob, six-pad controller symbol.
class ControlTargetButton final : public juce::TextButton {
public:
    ControlTargetButton() : TextButton(TRANS("Control here")) { setTitle(getButtonText()); }
    void paintButton(juce::Graphics& g, bool hover, bool down) override {
        const bool active = getToggleState();
        const auto box = getLocalBounds().toFloat().reduced(1);
        auto fill = juce::Colour(active ? 0xff147367 : 0xff334854);
        g.setColour(down ? fill.darker() : hover ? fill.brighter(.18f) : fill);
        g.fillRoundedRectangle(box, 4);
        g.setColour(hasKeyboardFocus(false) || active ? juce::Colour(0xff8fe7ce) : juce::Colour(0xffaabcc3));
        g.drawRoundedRectangle(box.reduced(.5f), 4, 1);
        const auto face = juce::Rectangle<float>(22, 16).withCentre(box.getCentre());
        g.drawRoundedRectangle(face, 2, 1.3f);
        for (int i = 0; i < 2; ++i)
            g.drawEllipse(face.getX() + 3, face.getY() + 2.5f + i * 6, 4, 4, 1.2f);
        for (int i = 0; i < 6; ++i)
            g.fillRoundedRectangle(face.getX() + 10 + (i % 3) * 3.5f, face.getY() + 4 + (i / 3) * 5, 2.6f, 3, .5f);
    }
};
