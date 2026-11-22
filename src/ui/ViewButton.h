#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ViewButton final : public juce::Button {
public:
    enum class Icon { piano, mixer, plugins, outputs, lanes };
    explicit ViewButton(Icon icon) : Button({}), icon_(icon) {}

    void paintButton(juce::Graphics& g, bool hover, bool down) override {
        const auto on = getToggleState();
        const auto rect = getLocalBounds().toFloat().reduced(1);
        g.setColour(juce::Colour(on ? 0xff30545b : hover || down ? 0xff394651 : 0xff232c34));
        g.fillRoundedRectangle(rect, 4);
        g.setColour(juce::Colour(on ? 0xff74dfd1 : 0xffcfdae1).withMultipliedAlpha(isEnabled() ? 1.0f : 0.38f));
        const float x = rect.getCentreX() - 10, y = rect.getCentreY() - 9;
        const auto line = [&](int x1, int y1, int x2, int y2) { g.drawLine(x+x1, y+y1, x+x2, y+y2, 1.6f); };
        switch (icon_) {
            case Icon::piano:
                g.drawRect(x, y+2, 20.0f, 14.0f, 1.6f);
                for (int i : {5, 10, 15}) line(i, 3, i, 15);
                for (int i : {4, 14}) g.fillRect(x+i, y+3, 3.0f, 7.0f);
                break;
            case Icon::mixer:
                for (int i = 0; i < 3; ++i) {
                    line(3+i*7, 1, 3+i*7, 17);
                    g.fillRoundedRectangle(x+i*7, y+(i == 1 ? 3 : 9), 6, 4, 1);
                }
                break;
            case Icon::plugins:
                line(6, 0, 6, 6); line(14, 0, 14, 6);
                g.drawRoundedRectangle(x+3, y+6, 14, 7, 2, 1.6f);
                line(10, 13, 10, 18); break;
            case Icon::outputs:
                line(1, 3, 6, 3); line(1, 15, 6, 15); line(6, 3, 6, 15);
                line(6, 9, 19, 9); line(14, 4, 19, 9); line(14, 14, 19, 9); break;
            case Icon::lanes:
                for (int i : {3, 9, 15}) {
                    line(0, i, 3, i); line(7, i, i == 9 ? 15 : 20, i);
                }
                break;
        }
        if (hasKeyboardFocus(false)) g.drawRoundedRectangle(rect, 4, 1.5f);
    }
private:
    Icon icon_;
};
