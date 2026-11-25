#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ostfstudio {
// Plug-in callbacks may run on the audio thread. Deliver editor selections and
// open dialogs only from the message thread, after the gesture has finished.
class InstrumentProgramWatch final : private juce::Timer {
    struct Gesture final : juce::AudioProcessorParameter::Listener {
        Gesture(juce::AudioProcessorParameter& p, int ch, int count)
            : parameter(p), channel(ch), lastProgram(count - 1) { parameter.addListener(this); }
        ~Gesture() override { parameter.removeListener(this); }
        void parameterGestureChanged(int, bool starting) override {
            if (starting) { value.store(-1); editing.store(true); }
            else { editing.store(false); const int v = value.exchange(-1); if (v >= 0) pending.store(v); }
        }
        void parameterValueChanged(int, float v) override {
            if (editing.load()) value.store(juce::jlimit(0, lastProgram, juce::roundToInt(v * lastProgram)));
        }
        juce::AudioProcessorParameter& parameter;
        int channel, lastProgram;
        std::atomic<bool> editing{false};
        std::atomic<int> value{-1}, pending{-1};
    };
public:
    InstrumentProgramWatch(juce::AudioProcessor& p, std::function<bool()> visible,
                           std::function<bool()> stopped, std::function<void(int,int)> offer)
        : processor(p), editorVisible(std::move(visible)), transportStopped(std::move(stopped)), notify(std::move(offer)) {
        for (auto* param : processor.getParameters()) {
            const auto name = param->getName(64);
            if ((processor.getName() == "GM Sound Module" || processor.getName() == "OSTF GM Sound Module") && name.startsWith("MIDI Program Ch ")) {
                const int channel = name.getTrailingIntValue();
                if (channel >= 1 && channel <= 16) gestures.push_back(std::make_unique<Gesture>(*param, channel, 128));
            } else if (name == "MIDI Program" && (processor.getName().startsWith("OSTF ")
                    || juce::StringArray{"FM Synth", "Analog Synth", "Wavetable Synth", "Drum Machine"}.contains(processor.getName())))
                gestures.push_back(std::make_unique<Gesture>(*param, 0, 4));
        }
        baseline = processor.getCurrentProgram();
        startTimerHz(12);
    }
    ~InstrumentProgramWatch() override { stopTimer(); }
private:
    void timerCallback() override {
        const bool visible = editorVisible();
        if (!gestures.empty()) {
            for (const auto& g : gestures) {
                if (visible && juce::Component::getCurrentlyModalComponent()) return;
                const int value = g->pending.exchange(-1);
                if (visible && value >= 0) { notify(g->channel, value); return; }
            }
        } else if (processor.getNumPrograms() > 1 && processor.getNumPrograms() <= 128) {
            const int current = processor.getCurrentProgram();
            const bool available = visible && transportStopped() && juce::Component::getCurrentlyModalComponent() == nullptr;
            const bool changed = current != baseline && available && wasAvailable && current >= 0 && current < processor.getNumPrograms();
            baseline = current;
            wasAvailable = available;
            if (changed) notify(0, current);
        }
    }
    juce::AudioProcessor& processor;
    std::function<bool()> editorVisible, transportStopped;
    std::function<void(int,int)> notify;
    std::vector<std::unique_ptr<Gesture>> gestures;
    int baseline{};
    bool wasAvailable{};
};
}
