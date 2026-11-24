#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class SystemUsageMeter final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    SystemUsageMeter();
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    uint64_t previousTotal{}, previousIdle{};
    int cpu = -1, memory = -1;
};
