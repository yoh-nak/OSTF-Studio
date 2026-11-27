#include "EffectControlPanel.h"
#include <signalsmith-stretch.h>
#include <array>

namespace {
class Harmonizer final : public juce::AudioProcessor {
public:
    ostfstudio::PluginCcMap ccMap{*this};
    Harmonizer() : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        auto add = [this](const char* id, const char* label, juce::NormalisableRange<float> range, float initial) {
            auto* parameter = new juce::AudioParameterFloat(juce::ParameterID{id, 1}, label, range, initial);
            addParameter(parameter);
            controls.push_back(parameter);
            return parameter;
        };
        intervalA = add("intervalA", "Voice A (st)", {-12.f, 12.f, 1.f}, 7);
        intervalB = add("intervalB", "Voice B (st)", {-12.f, 12.f, 1.f}, 12);
        balance = add("balance", "A / B blend %", {0.f, 100.f}, 35);
        mix = add("mix", "Wet %", {0.f, 100.f}, 40);
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new ostfstudio::EffectControlPanel(*this, controls, false,ccMap); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    double getTailLengthSeconds() const override { return 0.125; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        const auto in = layout.getMainInputChannelSet();
        return in == layout.getMainOutputChannelSet() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double rate, int maxBlock) override {
        const int channels = getTotalNumOutputChannels();
        const int hop = std::max(1, juce::roundToInt(rate * 0.015));
        const int chunk = juce::jlimit(1, 1024, maxBlock);
        for (size_t i = 0; i < voices.size(); ++i) {
            voices[i].configure(channels, hop * 4, hop);
            shifted[i].setSize(channels, chunk);
        }
        const int latency = voices[0].inputLatency() + voices[0].outputLatency();
        setLatencySamples(latency);
        dryDelay.setSize(channels, latency);
        wetGain.reset(rate, 0.015);
        voiceBlend.reset(rate, 0.015);
        reset();
    }
    void releaseResources() override {}
    void reset() override {
        if (dryDelay.getNumSamples() == 0) return;
        for (auto& voice : voices) voice.reset();
        dryDelay.clear();
        writeIndex = 0;
        wetGain.setCurrentAndTargetValue(mix->get() * 0.01f);
        voiceBlend.setCurrentAndTargetValue(balance->get() * 0.01f);
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        ccMap.process(midi); midi.clear(); render(audio, false); }
    void processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override { render(audio, true); }
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::XmlElement state("Harmonizer");
        for (const auto* p : controls) state.setAttribute(p->paramID, p->get());
        ccMap.save(state);
        copyXmlToBinary(state, dest);
    }
    void setStateInformation(const void* data, int size) override {
        auto state = getXmlFromBinary(data, size);
        if (!state || !state->hasTagName("Harmonizer")) return;
        ccMap.restore(*state);
        for (auto* p : controls) {
            const float value = static_cast<float>(state->getDoubleAttribute(p->paramID, p->get()));
            if (std::isfinite(value)) p->setValueNotifyingHost(p->convertTo0to1(value));
        }
    }
private:
    void render(juce::AudioBuffer<float>& audio, bool bypassed) {
        juce::ScopedNoDenormals safe;
        const int channels = audio.getNumChannels();
        if (channels == 0 || channels != dryDelay.getNumChannels() || dryDelay.getNumSamples() == 0) return;
        voices[0].setTransposeSemitones(intervalA->get());
        voices[1].setTransposeSemitones(intervalB->get());
        wetGain.setTargetValue(mix->get() * 0.01f);
        voiceBlend.setTargetValue(balance->get() * 0.01f);
        for (int start = 0; start < audio.getNumSamples();) {
            const int count = std::min(shifted[0].getNumSamples(), audio.getNumSamples() - start);
            std::array<const float*, 2> inputs{};
            for (int ch = 0; ch < channels; ++ch) inputs[ch] = audio.getReadPointer(ch, start);
            for (size_t v = 0; v < voices.size(); ++v)
                voices[v].process(inputs, count, shifted[v].getArrayOfWritePointers(), count);
            for (int i = 0; i < count; ++i) {
                const float blend = voiceBlend.getNextValue(), wet = wetGain.getNextValue();
                for (int ch = 0; ch < channels; ++ch) {
                    // Align the original signal with both generated voices, including during host bypass.
                    auto& delayed = dryDelay.getWritePointer(ch)[writeIndex];
                    const float dry = delayed;
                    delayed = inputs[ch][i];
                    const float a = shifted[0].getSample(ch, i), b = shifted[1].getSample(ch, i);
                    const float harmony = a + blend * (b - a);
                    audio.setSample(ch, start + i, bypassed ? dry : dry + wet * (harmony - dry));
                }
                if (++writeIndex == dryDelay.getNumSamples()) writeIndex = 0;
            }
            start += count;
        }
    }
    std::vector<juce::AudioParameterFloat*> controls;
    juce::AudioParameterFloat *intervalA, *intervalB, *balance, *mix;
    std::array<signalsmith::stretch::SignalsmithStretch<float>, 2> voices;
    std::array<juce::AudioBuffer<float>, 2> shifted;
    juce::AudioBuffer<float> dryDelay;
    juce::SmoothedValue<float> wetGain, voiceBlend;
    int writeIndex = 0;
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Harmonizer(); }
