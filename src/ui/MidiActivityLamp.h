#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class MidiActivityLamp final : public juce::Component, private juce::Timer {
public:
    MidiActivityLamp() { setInterceptsMouseClicks(false, false); setTitle(TRANS("MIDI input activity")); }
    void pulse() {
        deadline = juce::Time::getMillisecondCounter() + 180;
        if (!isTimerRunning()) { startTimer(90); repaint(); }
    }
    void paint(juce::Graphics& g) override {
        auto dot = getLocalBounds().toFloat().withSizeKeepingCentre(9, 9);
        g.setColour(juce::Colour(isTimerRunning() ? 0xff8affba : 0xff28383b));
        g.fillEllipse(dot);
        g.setColour(juce::Colour(0xff82a7a2));
        g.drawEllipse(dot, 1);
    }
private:
    void timerCallback() override {
        if (static_cast<juce::int32>(juce::Time::getMillisecondCounter() - deadline) >= 0) { stopTimer(); repaint(); }
    }
    juce::uint32 deadline = 0;
};
