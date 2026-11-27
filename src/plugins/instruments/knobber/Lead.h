#pragma once
#include "../../PluginCcMap.h"
#include "Programs.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace knobber {
class Lead final : public juce::AudioProcessor {
  public:
    Lead();
    void prepareToPlay(double rate, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &layout) const override {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsMPE() const override { return true; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    const juce::String getName() const override { return "SUPER KNOBBER LEAD"; }
    double getTailLengthSeconds() const override { return 30; }
    int getNumPrograms() override { return static_cast<int>(std::size(programs)); }
    int getCurrentProgram() override { return program.load(); }
    const juce::String getProgramName(int index) override;
    void setCurrentProgram(int index) override;
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;
    juce::AudioProcessorValueTreeState state;
    ostfstudio::PluginCcMap ccMap{*this};

  private:
    struct Slot {
        Voice engine;
        Note note;
        int channel = 0;
        uint64_t age = 0;
        bool held = false;
        double keyPressure = -1;
    };
    struct Expression {
        double bend = 0, wheel = 0, pressure = 0, timbre = 0.5;
        bool pedal = false;
    };
    struct Key {
        uint64_t order = 0;
        int velocity = 0;
    };
    std::array<Slot, 3> slots;
    std::array<Expression, 16> channels;
    std::array<std::array<Key, 128>, 16> keys{};
    uint64_t clock = 0;
    Drive drive;
    Effects effects;
    OutputFilter filter;
    Parameters parameters;
    double sampleRate = 44100;
    bool mono = false, mpe = false;
    bool previousQuality = false;
    Stereo lastDry;
    std::atomic<int> program{0};
    std::array<std::atomic<float> *, std::size(controls)> values;
    std::atomic<float> *mpeValue = nullptr, *quality = nullptr;

    void start(Slot &, int channel, int note, int velocity);
    void receive(const juce::MidiMessage &);
    Stereo voices(double rate);
    void releaseKey(int channel, int note);
    void updateGates();
};
} // namespace knobber
