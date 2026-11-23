#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ArrangementRuler final : public juce::Component, public juce::SettableTooltipClient {
public:
    int headerWidth = 0;
    std::function<juce::Range<double>()> visibleBars;
    std::function<double()> playheadBar;
    std::function<void(double)> locate, pan;

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff2e363e));
        g.setFont(juce::FontOptions(11.0f));
        g.setColour(juce::Colour(0xffc7d2dc));
        g.drawText(TRANS("BARS"), getLocalBounds().removeFromLeft(headerWidth).reduced(5, 0), juce::Justification::centredLeft);
        auto row = getLocalBounds().withTrimmedLeft(headerWidth).withTrimmedBottom(3);
        if (row.isEmpty() || !visibleBars) return;
        g.setColour(juce::Colour(0xff233443));
        g.fillRoundedRectangle(row.toFloat(), 4);
        const auto bars = visibleBars();
        if (bars.isEmpty()) return;
        const auto xAt = [&](double bar) { return row.getX() + juce::roundToInt((bar - bars.getStart()) * row.getWidth() / bars.getLength()); };
        const int step = std::max(1, static_cast<int>(bars.getLength() / (row.getWidth() < 420 ? 4 : 8)));
        for (int bar = static_cast<int>(std::ceil(bars.getStart())); bar < bars.getEnd(); bar += step) {
            int x = xAt(bar);
            g.setColour(juce::Colour(0xff526675));
            g.drawVerticalLine(x, static_cast<float>(row.getBottom() - 6), static_cast<float>(row.getBottom()));
            g.setColour(juce::Colour(0xffc7d2dc));
            g.drawText(juce::String(bar), row.withLeft(x).withWidth(std::min(46, row.getRight() - x)).withTrimmedLeft(3),
                       juce::Justification::centredLeft);
        }
        const auto cursor = playheadBar();
        if (cursor >= bars.getStart() && cursor <= bars.getEnd()) {
            const int x = std::min(xAt(cursor), row.getRight() - 1);
            juce::Path marker;
            marker.addTriangle(float(x - 4), 0, float(x + 4), 0, float(x), 6);
            g.setColour(juce::Colour(0xffefb45c));
            g.fillPath(marker);
            g.drawVerticalLine(x, 5.f, float(getHeight()));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        tracking_ = e.mods.isLeftButtonDown() && !e.mods.isPopupMenu() && e.x >= headerWidth && getWidth() > headerWidth;
        dragged_ = false;
        lastX_ = e.x;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!tracking_ || (!dragged_ && e.getDistanceFromDragStart() < 4)) return;
        dragged_ = true;
        if (getWidth() > headerWidth) pan(double(lastX_ - e.x) / (getWidth() - headerWidth));
        lastX_ = e.x;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (tracking_ && !dragged_ && getLocalBounds().contains(e.getPosition()) && e.x >= headerWidth && getWidth() > headerWidth)
            locate(double(e.x - headerWidth) / (getWidth() - headerWidth));
        tracking_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
private:
    int lastX_ = 0;
    bool tracking_ = false, dragged_ = false;
};
