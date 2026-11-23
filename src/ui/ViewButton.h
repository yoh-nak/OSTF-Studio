#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ViewButton final : public juce::Button {
public:
    enum class Icon { arrangement, piano, mixer, plugins, outputs, lanes, leadSheet, score, information, melody, chorus, pitched, drums, audio, bus, send, zoomIn, zoomOut };
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
            case Icon::information:
                g.drawEllipse(x+2, y, 16, 16, 1.6f);
                g.fillEllipse(x+9, y+3, 2, 2); line(10,7,10,13); break;
            case Icon::melody:
                line(6, 12, 6, 1); line(6, 1, 12, 4);
                g.fillEllipse(x+1, y+11, 6, 4); break;
            case Icon::chorus:
                g.drawRoundedRectangle(x+4,y+1,6,10,3,1.5f);
                g.drawRoundedRectangle(x+1,y+6,12,8,4,1.4f); line(7,14,7,17); line(3,17,11,17); break;
            case Icon::pitched:
                g.drawRect(x,y+3,13.f,12.f,1.4f); line(4,4,4,15); line(9,4,9,15);
                g.fillRect(x+3,y+3,3.f,5.f); break;
            case Icon::drums:
                g.drawEllipse(x,y+5,13,5,1.4f); line(0,7,1,14); line(13,7,12,14);
                line(1,14,12,14); line(2,1,10,5); line(12,0,5,5); break;
            case Icon::audio:
                line(0,9,3,9); line(3,9,5,3); line(5,3,8,15); line(8,15,10,6); line(10,6,13,9); break;
            case Icon::bus:
                for (int h : {3,9,15}) { line(0,h,6,h); line(6,h,9,9); }
                line(9,9,13,9); break;
            case Icon::send:
                line(1,3,10,3); line(10,3,10,11); line(10,11,1,11);
                line(1,11,5,7); line(1,11,5,15); break;
            case Icon::zoomIn:
            case Icon::zoomOut:
                g.drawEllipse(x+1,y,13,13,1.7f); line(12,12,19,18); line(4,6,11,6);
                if (icon_ == Icon::zoomIn) line(7,3,7,9);
                break;
            case Icon::arrangement:
                line(1, 1, 1, 17);
                g.fillRoundedRectangle(x+4, y+2, 10, 3, 1);
                g.fillRoundedRectangle(x+9, y+8, 11, 3, 1);
                g.fillRoundedRectangle(x+4, y+14, 13, 3, 1);
                break;
            case Icon::leadSheet:
                g.drawRoundedRectangle(x+1, y+1, 18, 16, 2, 1.4f);
                line(4, 5, 9, 5); line(12, 5, 16, 5);
                line(4, 9, 7, 9); line(4, 13, 14, 13);
                break;
            case Icon::score:
                for (int i : {5, 9, 13}) g.drawLine(x, y+i, x+20, y+i, .8f);
                line(7, 13, 7, 3); line(7, 3, 15, 1); line(15, 1, 15, 11);
                g.fillEllipse(x+2, y+12, 5, 4); g.fillEllipse(x+10, y+10, 5, 4);
                break;
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
        if (icon_ >= Icon::melody && icon_ <= Icon::send) {
            line(14,14,20,14); line(17,11,17,17);
        }
        if (hasKeyboardFocus(false)) g.drawRoundedRectangle(rect, 4, 1.5f);
    }
private:
    Icon icon_;
};
