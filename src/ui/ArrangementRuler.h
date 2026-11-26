#pragma once
#include "ui/TimelinePan.h"

#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

class ArrangementRuler final : public juce::Component, public juce::SettableTooltipClient {
public:
    juce::Colour headerBackground{0xff2e363e};
    int headerWidth = 0, labelInset = 0;
    std::function<juce::Range<double>()> visibleBars;
    std::function<double()> playheadBar;
    std::function<void(double)> locate, pan;

    std::function<void(double, double)> chooseCycle;
    std::function<juce::Range<double>()> cycleBars;
    bool cycleMode = false, alignHeaderRight = false;

    // Return a component-local pixel so rulers and their grids use the same rounding origin.
    int playheadColumn() const {
        if (!visibleBars || !playheadBar || getWidth() <= headerWidth) return -1;
        const auto bars = visibleBars();
        const auto cursor = playheadBar();
        if (bars.isEmpty() || cursor < bars.getStart() || cursor > bars.getEnd()) return -1;
        return std::min(getWidth() - 1, headerWidth + juce::roundToInt(
            (cursor - bars.getStart()) * (getWidth() - headerWidth) / bars.getLength()));
    }

    void paint(juce::Graphics& g) override {
        g.setColour(headerBackground);
        g.fillRect(getLocalBounds().withWidth(headerWidth));
        g.setColour(juce::Colour(0xff2e363e));
        g.fillRect(getLocalBounds().withTrimmedLeft(headerWidth));
        const juce::Font rulerFont(juce::FontOptions(std::clamp(float(getHeight() - 3), 12.f, 15.f), juce::Font::bold));
        g.setFont(rulerFont);
        g.setColour(juce::Colour(alignHeaderRight ? 0xffe1e9ef : 0xffc7d2dc));
        g.drawText(TRANS("BARS"), getLocalBounds().removeFromLeft(headerWidth).withTrimmedLeft(labelInset).reduced(5, 0), alignHeaderRight ? juce::Justification::centredRight : juce::Justification::centredLeft);
        auto row = getLocalBounds().withTrimmedLeft(headerWidth).withTrimmedBottom(3);
        if (row.isEmpty() || !visibleBars) return;
        g.setColour(juce::Colour(0xff233443));
        g.fillRoundedRectangle(row.toFloat(), 4);
        const auto bars = visibleBars();
        if (bars.isEmpty()) return;
        const auto xAt = [&](double bar) { return row.getX() + juce::roundToInt((bar - bars.getStart()) * row.getWidth() / bars.getLength()); };
        if (cycleBars) {
            const auto region = cycleBars().getIntersectionWith(bars);
            if (!region.isEmpty()) {
                const auto left = xAt(region.getStart()), right = xAt(region.getEnd());
                g.setColour(juce::Colour(0xff346969));
                g.fillRect(left, 0, std::max(1, right - left), row.getHeight());
                g.setColour(juce::Colour(0xff78d9be));
                g.fillRect(left, 0, std::max(1, right - left), 3);
            }
        }
        const auto widest = juce::String(static_cast<int>(std::ceil(bars.getEnd())));
        const int labelWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(rulerFont, widest)) + 12;
        const int step = std::max(1, static_cast<int>(std::ceil(labelWidth * bars.getLength() / row.getWidth())));
        for (int bar = static_cast<int>(std::ceil(bars.getStart())); bar < bars.getEnd(); bar += step) {
            int x = xAt(bar);
            g.setColour(juce::Colour(0xff526675));
            g.drawVerticalLine(x, static_cast<float>(row.getBottom() - 6), static_cast<float>(row.getBottom()));
            g.setColour(juce::Colour(0xffc7d2dc));
            g.drawText(juce::String(bar), row.withLeft(x).withWidth(std::min(labelWidth, row.getRight() - x)).withTrimmedLeft(3),
                       juce::Justification::centredLeft);
        }
        if (const int x = playheadColumn(); x >= 0) {
            juce::Path marker;
            marker.addTriangle(float(x - 4), 0, float(x + 4), 0, float(x), 6);
            g.setColour(juce::Colour(0xffefb45c));
            g.fillPath(marker);
            g.drawVerticalLine(x, 5.f, float(getHeight()));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        tracking_ = e.mods.isLeftButtonDown() && !e.mods.isPopupMenu() && e.x >= headerWidth && getWidth() > headerWidth;
        panOnly_ = TimelinePan::requested(e.mods);
        choosing_ = tracking_ && !panOnly_ && chooseCycle && (cycleMode || e.mods.isShiftDown());
        anchor_ = fraction(e.x);
        dragged_ = false;
        lastX_ = e.x;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!tracking_ || (!dragged_ && e.getDistanceFromDragStart() < 4)) return;
        dragged_ = true;
        if (choosing_) { chooseCycle(anchor_, fraction(e.x)); return; }
        if (getWidth() > headerWidth) pan(double(lastX_ - e.x) / (getWidth() - headerWidth));
        lastX_ = e.x;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (tracking_ && !panOnly_ && !dragged_ && getLocalBounds().contains(e.getPosition()) && e.x >= headerWidth && getWidth() > headerWidth)
            locate(double(e.x - headerWidth) / (getWidth() - headerWidth));
        tracking_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
private:
    double fraction(int x) const { return std::clamp(double(x - headerWidth) / std::max(1, getWidth() - headerWidth), 0., 1.); }
    bool choosing_ = false, panOnly_ = false;
    double anchor_ = 0;
    int lastX_ = 0;
    bool tracking_ = false, dragged_ = false;
};
