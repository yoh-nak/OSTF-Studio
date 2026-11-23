#pragma once
#include "ui/TimelinePan.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Arrangement coordinates let the lower editors select the same phrase or source region.
class EditorClipLane final : public juce::Component {
public:
    TimelinePan viewportPan;
    struct Clip { juce::Range<double> bars; juce::String label; };
    std::vector<Clip> clips;
    std::optional<double> selected;
    std::function<juce::Range<double>()> visible;
    std::function<void(std::optional<double>)> select;
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff192831));
        if (!visible) return;
        const auto range = visible();
        if (range.isEmpty()) return;
        for (const auto& clip : clips) {
            const auto shown = range.getIntersectionWith(clip.bars);
            if (shown.isEmpty()) continue;
            const auto x = [&](double bar) { return juce::roundToInt((bar - range.getStart()) * getWidth() / range.getLength()); };
            juce::Rectangle<int> bounds(x(shown.getStart()), 1, std::max(1, x(shown.getEnd()) - x(shown.getStart()) - 1), getHeight() - 2);
            g.setColour(juce::Colour(selected == clip.bars.getStart() ? 0xff247669 : 0xff354e62));
            g.fillRect(bounds);
            g.setColour(juce::Colours::white);
            g.drawText(clip.label, bounds.reduced(5, 0), juce::Justification::centredLeft);
        }
    }
    std::optional<double> at(double bar) const {
        for (const auto& clip : clips) if (clip.bars.contains(bar)) return clip.bars.getStart();
        return {};
    }
    void mouseDrag(const juce::MouseEvent& e) override { viewportPan.move(e); }
    void mouseUp(const juce::MouseEvent&) override { viewportPan.end(*this); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (viewportPan.begin(*this, e, getLocalBounds())) return;
        if (!visible || !select || !getWidth()) return;
        const auto range = visible();
        select(at(range.getStart() + e.x * range.getLength() / getWidth()));
    }
};
