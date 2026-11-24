#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ostfstudio {
// MIDI and preset recall update the same synthesizer value. Only a change made
// in the native editor emits a host edit gesture.
class EditorProgramParameter final : public juce::AudioProcessorParameterWithID {
public:
    EditorProgramParameter(juce::String id, juce::String title, std::atomic<int>& target, int count)
        : AudioProcessorParameterWithID(juce::ParameterID{id, 1}, title), program(target), last(count - 1) {}
    float getValue() const override { return float(program.load()) / float(last); }
    void setValue(float v) override { program.store(juce::jlimit(0, last, juce::roundToInt(v * last))); }
    int getNumSteps() const override { return last + 1; }
    bool isDiscrete() const override { return true; }
    float getDefaultValue() const override { return 0; }
    float getValueForText(const juce::String& s) const override { return float(juce::jlimit(0, last, s.getIntValue())) / last; }
    juce::String getText(float v, int) const override { return juce::String(juce::roundToInt(v * last)); }
    void choose(int index) {
        if (!juce::isPositiveAndBelow(index, last + 1) || program.load() == index) return;
        beginChangeGesture();
        setValueNotifyingHost(float(index) / last);
        endChangeGesture();
    }
private:
    std::atomic<int>& program;
    int last;
};
}
