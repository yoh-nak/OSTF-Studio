#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ViewButton final : public juce::Button {
public:
    enum class Icon { arrangement, piano, mixer, plugins, outputs, lanes, leadSheet, score, information, melody, chorus, pitched, drums, audio, bus, send, zoomIn, zoomOut, theory, songMusic, headphones, samples, scissors, join, chordFit, scaleFit, undo, redo, history };
    explicit ViewButton(Icon icon) : Button({}), icon_(icon) {}

    void paintButton(juce::Graphics& g, bool hover, bool down) override {
        const auto on = getToggleState();
        const auto rect = getLocalBounds().toFloat().reduced(1);
        if (icon_ == Icon::headphones) {
            const auto face = on ? juce::Colour(0xff245e5c) : findColour(juce::TextButton::buttonColourId);
            getLookAndFeel().drawButtonBackground(g,*this,face,hover,down);
            g.setColour(juce::Colour(on ? 0xff83f2d8 : 0xffd6e2e5).withMultipliedAlpha(isEnabled() ? 1.f : .4f));
            if (hasKeyboardFocus(false)) g.drawRoundedRectangle(rect,4.f,1.f);
            const auto iconArea = rect.reduced(2).withSizeKeepingCentre(
                std::min(18.f,std::max(0.f,rect.getWidth()-4)),std::min(16.f,std::max(0.f,rect.getHeight()-4)));
            if (!iconArea.isEmpty()) {
                juce::Graphics::ScopedSaveState state(g);
                g.addTransform(juce::RectanglePlacement(juce::RectanglePlacement::centred)
                    .getTransformToFit({0.f,0.f,20.f,18.f},iconArea));
                drawIcon(g,icon_,0,0);
            }
            return;
        }
        g.setColour(juce::Colour(on ? 0xff30545b : hover || down ? 0xff394651 : 0xff232c34));
        g.fillRoundedRectangle(rect, 4);
        g.setColour(juce::Colour(on ? 0xff74dfd1 : 0xffcfdae1).withMultipliedAlpha(isEnabled() ? 1.0f : 0.38f));
        const float x = rect.getCentreX() - 10, y = rect.getCentreY() - 9;
        drawIcon(g, icon_, x, y);
        if (icon_ >= Icon::melody && icon_ <= Icon::send) {
            g.drawLine(x+14,y+14,x+20,y+14,1.6f); g.drawLine(x+17,y+11,x+17,y+17,1.6f);
        }
        if (hasKeyboardFocus(false)) g.drawRoundedRectangle(rect, 4, 1.5f);
    }
    static void drawIcon(juce::Graphics& g, Icon icon, float x, float y) {
        const auto line = [&](int x1, int y1, int x2, int y2) { g.drawLine(x+x1, y+y1, x+x2, y+y2, 1.6f); };
        switch (icon) {
            case Icon::undo:
            case Icon::redo: {
                const bool back = icon == Icon::undo;
                const auto px = [&](int offset) { return back ? offset : 20 - offset; };
                line(px(2),7,px(7),2); line(px(2),7,px(7),12);
                juce::Path turn;
                turn.startNewSubPath(x+px(3), y+7); turn.lineTo(x+px(12), y+7);
                turn.quadraticTo(x+px(19), y+7, x+px(17), y+16);
                g.strokePath(turn, juce::PathStrokeType(1.7f)); break;
            }
            case Icon::history:
                g.drawEllipse(x+2,y+1,16,16,1.6f); line(10,4,10,9); line(10,9,14,11); break;
            case Icon::chordFit:
                for (int i : {3,8,13}) { g.fillEllipse(x+4,y+i,5,3); }
                line(9,2,9,14); line(12,12,14,15); line(14,15,19,6); break;
            case Icon::scaleFit:
                for (int i=0;i<4;++i) g.fillRect(x+1+i*3,y+13-i*3,3.f,3.f);
                line(12,12,14,15); line(14,15,19,6); break;
            case Icon::scissors:
                g.drawEllipse(x+1,y+10,6,6,1.6f); g.drawEllipse(x+10,y+10,6,6,1.6f);
                line(5,11,16,1); line(12,11,2,1); break;
            case Icon::join:
                line(1,2,1,15); line(18,2,18,15); line(2,9,8,9); line(5,6,8,9); line(5,12,8,9);
                line(17,9,11,9); line(14,6,11,9); line(14,12,11,9); break;
            case Icon::headphones: {
                juce::Path band;
                band.startNewSubPath(x+3,y+12);
                band.lineTo(x+3,y+9);
                band.cubicTo(x+3,y+.5f,x+17,y+.5f,x+17,y+9);
                band.lineTo(x+17,y+12);
                g.strokePath(band, juce::PathStrokeType(2));
                g.fillRoundedRectangle(x + 2, y + 9, 4, 7, 1.5f);
                g.fillRoundedRectangle(x + 14, y + 9, 4, 7, 1.5f);
                break;
            }
            case Icon::theory:
                for (int i : {2,10,18}) { g.drawEllipse(x+i-2,y+(i==10?1:10),4,4,1.6f); }
                line(3,10,9,5); line(11,5,17,10); line(3,16,17,16); break;
            case Icon::songMusic:
                g.fillEllipse(x+1, y+11, 7, 5); line(7,13,7,3); line(7,3,11,5);
                line(16,2,16,15); line(13,5,16,2); line(16,2,19,5);
                line(13,12,16,15); line(16,15,19,12); break;
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
                if (icon == Icon::zoomIn) line(7,3,7,9);
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
            case Icon::samples:
                line(1, 5, 1, 16); line(1, 16, 19, 16); line(19, 16, 19, 5);
                line(19, 5, 9, 5); line(9, 5, 7, 2); line(7, 2, 1, 2); line(1, 2, 1, 5);
                for (int yRow : {8, 12}) { line(4, yRow, 5, yRow); line(8, yRow, 16, yRow); } break;
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
    }
private:
    Icon icon_;
};
