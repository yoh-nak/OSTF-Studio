#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class TimelinePan {
public:
    std::function<void(double)> scroll;
    static bool requested(juce::ModifierKeys mods) {
        return mods.isLeftButtonDown() && mods.isAltDown() && mods.isShiftDown() && !mods.isPopupMenu();
    }
    bool begin(juce::Component& view, const juce::MouseEvent& e, juce::Rectangle<int> grid) {
        active = scroll && requested(e.mods) && grid.contains(e.getPosition());
        if (active) {
            lastX = e.x;
            width = grid.getWidth();
            previous = view.getMouseCursor();
            view.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        return active;
    }
    bool move(const juce::MouseEvent& e) {
        if (!active) return false;
        const int delta = lastX - e.x;
        lastX = e.x;
        if (delta) scroll(double(delta) / width);
        return true;
    }
    bool end(juce::Component& view) {
        if (!active) return false;
        active = false;
        view.setMouseCursor(previous);
        return true;
    }
private:
    bool active = false;
    int lastX = 0, width = 1;
    juce::MouseCursor previous;
};
