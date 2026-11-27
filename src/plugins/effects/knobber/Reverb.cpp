#include "Reverb.h"

namespace knobverb {
namespace {
juce::AudioProcessorValueTreeState::ParameterLayout layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout result;
  juce::StringArray algorithms;
  for (auto name : types)
    algorithms.add(name);
  result.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"rtype", 1}, "Algorithm", algorithms, initialType));
  for (const auto &c : controls)
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{c.id, 1}, c.name,
        juce::NormalisableRange<float>(float(c.low), float(c.high)),
        float(c.initial),
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(
            [](float value, int) {
              return juce::String(value, 2)
                  .trimCharactersAtEnd("0")
                  .trimCharactersAtEnd(".");
            })));
  return result;
}
constexpr double trims[] = {
    1.30, 1.34, 1.21, 1.21, 1.21, 1.21, 1.21, 1.21, 1.16, 1.16,
    1.16, 1.16, 1.16, 1.16, 1.16, 1.16, 1.16, 1.20, 1.20, 1.20,
    1.20, 1.24, 1.24, 1.21, 1.21, 1.18, 1.18, 1.18, 1.18, 1.08,
    1.08, 1.18, 1.18, 1.18, 1.18, 1.22, 1.14, 1.18, 1.18, 1.07,
    1.07, 1.22, 1.17, 1.14, 1.11, 1.15, 1.15, 1.25, 1.02};
} // namespace
Reverb::Reverb()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "KnobberReverb", layout()) {
  algorithm =
      dynamic_cast<juce::AudioParameterChoice *>(state.getParameter("rtype"));
  for (size_t i = 0; i < parameters.size(); ++i)
    parameters[i] = state.getRawParameterValue(controls[i].id);
  select(initialType, 0, 0);
}
void Reverb::select(int type, int source, int style) {
  if (!juce::isPositiveAndBelow(type, int(std::size(types))) ||
      !juce::isPositiveAndBelow(source, int(std::size(sources))) ||
      !juce::isPositiveAndBelow(style, int(std::size(styles))))
    return;
  std::array<double, 108> values;
  std::copy_n(typesBank[type], values.size(), values.begin());
  for (size_t i = 0; i < std::size(sourceMap); ++i)
    values[sourceMap[i]] = sourcesBank[source][i];
  for (size_t i = 0; i < std::size(styleMap); ++i)
    if (styleMap[i] >= 0)
      values[styleMap[i]] = stylesBank[style][i];
  constexpr int depthControls[] = {0, 1, 3, 4, 5, 8, 12, 15};
  for (int i = 0; i < 8; ++i) {
    const auto k = depthControls[i];
    const auto depth = depthBank[style][i];
    values[k] = (i == 0 || i == 1 || i == 5 || i == 6)
                    ? typesBank[type][k] * depth
                    : typesBank[type][k] + depth;
  }
  algorithm->setValueNotifyingHost(algorithm->convertTo0to1(float(type)));
  for (size_t i = 0; i < values.size(); ++i) {
    auto *p = state.getParameter(controls[i].id);
    p->setValueNotifyingHost(p->convertTo0to1(
        float(std::clamp(values[i], controls[i].low, controls[i].high))));
  }
  sourceIndex = source;
  styleIndex = style;
}
void Reverb::getStateInformation(juce::MemoryBlock &data) {
  auto tree = state.copyState();
    ccMap.save(tree);
  tree.setProperty("source", sourceIndex.load(), nullptr);
  tree.setProperty("style", styleIndex.load(), nullptr);
  if (auto xml = tree.createXml())
    copyXmlToBinary(*xml, data);
}
void Reverb::setStateInformation(const void *data, int size) {
  auto xml = getXmlFromBinary(data, size);
  if (!xml || !xml->hasTagName(state.state.getType()))
    return;
  auto tree = juce::ValueTree::fromXml(*xml);
  sourceIndex = juce::jlimit(0, 12, int(tree.getProperty("source", 0)));
  styleIndex = juce::jlimit(0, 9, int(tree.getProperty("style", 0)));
  ccMap.restore(tree);
    state.replaceState(tree);
}
bool Reverb::isBusesLayoutSupported(const BusesLayout &b) const {
  const auto in = b.getMainInputChannelSet();
  return in == b.getMainOutputChannelSet() &&
         (in == juce::AudioChannelSet::mono() ||
          in == juce::AudioChannelSet::stereo());
}
double Reverb::getTailLengthSeconds() const {
  return algorithm->getIndex() == 39 || algorithm->getIndex() == 40
             ? 120.
             : double(parameters[1]->load()) * 1.6 + 2.;
}
void Reverb::prepareToPlay(double rate, int block) {
  const auto chunk = juce::jlimit(1, 2048, block);
  wet.setSize(2, chunk);
  for (int i = 0; i < 2; ++i) {
    voices[i].configure(2, juce::roundToInt(rate * .060),
                        juce::roundToInt(rate * .015));
    shift[i].setSize(2, chunk);
  }
  for (auto *ramp : {&blend, &gain})
    ramp->reset(rate, .025);
  for (auto *ramp : {&shimmer, &pitchReturn})
    ramp->reset(rate, .03);
  calibration.reset(rate, .04);
  if (!engine)
    engine = std::make_unique<Engine>();
  reset();
}
void Reverb::reset() {
  if (!engine)
    return;
  engine->reset();
  for (auto &voice : voices)
    voice.reset();
  blend.setCurrentAndTargetValue(parameters[62]->load() * .01f);
  gain.setCurrentAndTargetValue(
      juce::Decibels::decibelsToGain(parameters[63]->load()));
  shimmer.setCurrentAndTargetValue(parameters[50]->load() * .01f);
  pitchReturn.setCurrentAndTargetValue(parameters[53]->load() * .01f);
  calibration.setCurrentAndTargetValue(float(trims[algorithm->getIndex()]));
  inputSpectrum.prepare(getSampleRate());
  wetSpectrum.prepare(getSampleRate());
  preEqSpectrum.prepare(getSampleRate());
  postEqSpectrum.prepare(getSampleRate());
  filteredSpectrum.prepare(getSampleRate());
  waveform.prepare(getSampleRate());
  wetPeak = 0; inputPeak = 0; outputPeak = 0;
  wetL2=wetR2=wetLR=0; wetCorrelation=0; wetRmsDb=-84;
}
void Reverb::releaseResources() { engine.reset(); }
void Reverb::processBlock(juce::AudioBuffer<float> &audio, juce::MidiBuffer &midi) {
  ccMap.process(midi);
  midi.clear();
  juce::ScopedNoDenormals safe;
  if (!engine || !audio.getNumChannels() || !wet.getNumSamples())
    return;
  std::array<double, 108> values;
  for (size_t i = 0; i < values.size(); ++i)
    values[i] = parameters[i]->load(std::memory_order_relaxed);
  const int type = algorithm->getIndex(), right = audio.getNumChannels() - 1;
  const auto rate = getSampleRate();
  blend.setTargetValue(float(values[62] * .01));
  gain.setTargetValue(juce::Decibels::decibelsToGain(float(values[63])));
  shimmer.setTargetValue(float(values[50] * .01));
  pitchReturn.setTargetValue(float(values[53] * .01));
  calibration.setTargetValue(float(trims[type]));
  for (int v = 0; v < 2; ++v)
    voices[v].setTransposeSemitones(float(values[51 + v]));
  double sumL=0,sumR=0,sumLR=0;
  float peak = 0, inPeak = 0, outPeak = 0;
  for (int start = 0; start < audio.getNumSamples();) {
    const int count =
        std::min(wet.getNumSamples(), audio.getNumSamples() - start);
    for (int i = 0; i < count; ++i) {
      const auto l = audio.getSample(0, start + i),
                 r = audio.getSample(right, start + i);
      inputSpectrum.push(l, r);
      inPeak = std::max({inPeak, std::abs(l), std::abs(r)});
      const auto out = engine->process(l, r, rate, type, values);
      wet.setSample(0, i, float(out.left));
      wet.setSample(1, i, float(out.right));
      filteredSpectrum.push(float(engine->filteredLeft), float(engine->filteredRight));
      postEqSpectrum.push(float(out.left), float(out.right));
      preEqSpectrum.push(float(engine->beforeLeft), float(engine->beforeRight));
    }
    for (int v = 0; v < 2; ++v)
      voices[v].process(wet.getArrayOfReadPointers(), count,
                        shift[v].getArrayOfWritePointers(), count);
    for (int i = 0; i < count; ++i) {
      const float sh = shimmer.getNextValue(),
                  level = (1 + pitchReturn.getNextValue()) *
                          calibration.getNextValue(),
                  mix = blend.getNextValue(), out = gain.getNextValue();
      float wetPair[2];
      for (int ch = 0; ch < 2; ++ch) {
        const auto shifted =
            .5f * (shift[0].getSample(ch, i) + shift[1].getSample(ch, i));
        wetPair[ch] =
            limitWet(level * ((1 - sh) * wet.getSample(ch, i) + sh * shifted));
        peak = std::max(peak, std::abs(out * mix * wetPair[ch]));
      }
      wetSpectrum.push(out * mix * wetPair[0], out * mix * wetPair[1]);
      const double leftWet=out*mix*wetPair[0],rightWet=out*mix*wetPair[1];
      waveform.push(float(leftWet),float(rightWet));
      sumL+=leftWet*leftWet; sumR+=rightWet*rightWet; sumLR+=leftWet*rightWet;
      for (int ch = 0; ch < audio.getNumChannels(); ++ch) {
        const auto value =
            right ? wetPair[ch] : .5f * (wetPair[0] + wetPair[1]);
        const float sample = out * ((1 - mix) * audio.getSample(ch, start + i) + mix * value);
        outPeak = std::max(outPeak, std::abs(sample));
        audio.setSample(ch, start + i, sample);
      }
    }
    start += count;
  }
  const auto publish = [](std::atomic<float>& meter, float value) {
    auto previous = meter.load(std::memory_order_relaxed);
    while (previous < value && !meter.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
  };
  const int n=audio.getNumSamples();
  if (n>0) {
    const auto weight=-std::expm1(-double(n)/(.25*rate));
    wetL2+=weight*(sumL/n-wetL2); wetR2+=weight*(sumR/n-wetR2); wetLR+=weight*(sumLR/n-wetLR);
    const double norm=std::sqrt(wetL2*wetR2);
    wetCorrelation.store(norm>1.e-12 ? float(std::clamp(wetLR/norm,-1.,1.)) : 0.f);
    wetRmsDb.store(juce::Decibels::gainToDecibels(float(std::sqrt(.5*(wetL2+wetR2))),-84.f));
  }
  publish(inputPeak, inPeak);
  publish(wetPeak, peak);
  publish(outputPeak, outPeak);
}
} // namespace knobverb
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new knobverb::Reverb();
}
