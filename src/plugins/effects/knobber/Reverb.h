#pragma once
#include "../../PluginCcMap.h"
#include "../../../ui/MasterSpectrum.h"
#include "../../StudioPluginEditor.h"
#include "Engine.h"
#include "WetWaveform.h"
#include <signalsmith-stretch.h>

namespace knobverb {
class Reverb final : public juce::AudioProcessor {
public:
  Reverb();
  const juce::String getName() const override { return "SUPER KNOBBER REVERB"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  bool hasEditor() const override { return true; }
  juce::AudioProcessorEditor *createEditor() override;
  bool isBusesLayoutSupported(const BusesLayout &) const override;
  double getTailLengthSeconds() const override;
  void prepareToPlay(double, int) override;
  void reset() override;
  void releaseResources() override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  int getNumPrograms() override { return std::size(types); }
  int getCurrentProgram() override { return algorithm->getIndex(); }
  void setCurrentProgram(int n) override { select(n, 0, 0); }
  const juce::String getProgramName(int n) override {
    return juce::isPositiveAndBelow(n, getNumPrograms()) ? types[n] : "";
  }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override;
  void setStateInformation(const void *, int) override;
  void select(int type, int source, int style);
  juce::AudioProcessorValueTreeState state;
    ostfstudio::PluginCcMap ccMap{*this};
  std::atomic<int> sourceIndex{0}, styleIndex{0};
  MasterSpectrum inputSpectrum, wetSpectrum, preEqSpectrum;
  std::atomic<float> wetPeak{0}, inputPeak{0}, outputPeak{0};
  MasterSpectrum postEqSpectrum, filteredSpectrum;
  WetWaveform waveform;
  std::atomic<float> wetRmsDb{-84},wetCorrelation{0};
  // Preserve low-level reverb detail. The limiter has unity slope at its soft knee.
  static float limitWet(float x) {
    const auto magnitude=std::abs(x);
    return magnitude<=.75f ? x : std::copysign(.75f+.25f*std::tanh((magnitude-.75f)*4.f),x);
  }

private:
  double wetL2=0,wetR2=0,wetLR=0;
  juce::AudioParameterChoice *algorithm;
  std::array<std::atomic<float> *, 108> parameters;
  std::unique_ptr<Engine> engine;
  std::array<signalsmith::stretch::SignalsmithStretch<float>, 2> voices;
  juce::AudioBuffer<float> wet, shift[2];
  juce::SmoothedValue<float> blend, shimmer, pitchReturn, gain, calibration;
};
} // namespace knobverb
