#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TrackRowSize final : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr int minimum = 28, normal = 44, maximum = 180, grip = 6;
    TrackRowSize() {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setTooltip(TRANS("Drag to resize track. Double-click to fold or restore."));
    }
    int height() const { return height_; }
    bool compact() const { return height_ < 40; }
    bool expanded() const { return height_ >= 70; }
    void copySize(const TrackRowSize& other) { height_ = other.height_; restore_ = other.restore_; }
    void resetSize() { height_ = normal; restore_ = normal; }
    bool assignHeight(int requested) {
        const int next = juce::jlimit(minimum, maximum, requested);
        if (next == height_) return false;
        if (!compact() && next < 40) restore_ = height_;
        height_ = next;
        return true;
    }
    void mouseDown(const juce::MouseEvent& e) override { start_ = height_; screenY_ = e.getScreenY(); }
    void mouseDrag(const juce::MouseEvent& e) override { setHeight(start_ + e.getScreenY() - screenY_); }
    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (compact()) setHeight(restore_);
        else setHeight(minimum);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void paint(juce::Graphics& g) override {
        g.setColour(isMouseOverOrDragging() ? juce::Colour(0xff64d7c1) : juce::Colour(0xff64717b));
        g.fillRoundedRectangle(7.0f, 2.0f, isMouseOverOrDragging() ? 38.0f : 22.0f, 2.0f, 1.0f);
    }
    std::function<void()> changed;
private:
    void setHeight(int requested) {
        if (assignHeight(requested) && changed) changed();
    }
    int height_ = normal, restore_ = normal, start_ = normal, screenY_ = 0;
};
