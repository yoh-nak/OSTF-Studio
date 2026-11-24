#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "EffectControlPanel.h"
#include <array>
#include <cmath>
#include <vector>

namespace {
enum class Mode {
    eight,
    dynamic,
    multi,
    bus,
    multiBus,
    gate,
    delay,
    deEss,
    filter,
    highPass,
    lowPass,
    graphic,
    parametric,
    electric,
    acoustic,
    bass
};
constexpr std::pair<const char*, Mode> modes[] = {{"EightBandEQ", Mode::eight},
                                                  {"DynamicEQ", Mode::dynamic},
                                                  {"MultibandCompressor", Mode::multi},
                                                  {"BusCompressor", Mode::bus},
                                                  {"MultibandBusCompressor", Mode::multiBus},
                                                  {"Gate", Mode::gate},
                                                  {"Delay", Mode::delay},
                                                  {"DeEsser", Mode::deEss},
                                                  {"Filter", Mode::filter},
                                                  {"HighPass", Mode::highPass},
                                                  {"LowPass", Mode::lowPass},
                                                  {"GraphicEQ", Mode::graphic},
                                                  {"ParametricEQ", Mode::parametric},
                                                  {"ElectricRig", Mode::electric},
                                                  {"AcousticRig", Mode::acoustic},
                                                  {"BassRig", Mode::bass}};
using Parameter = juce::AudioParameterFloat;
using Coeff = juce::dsp::IIR::ArrayCoefficients<float>;
using Biquad = juce::dsp::IIR::Filter<float>;

class MixingEffect final : public juce::AudioProcessor {
  public:
    MixingEffect(Mode m, const char* product, const char* key)
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          mode(m), name(product), stateTag(key) {
        auto control = [&](juce::String id, juce::String title, float min, float max, float value, bool log = false) {
            juce::NormalisableRange<float> range(min, max);
            if (log)
                range.setSkewForCentre(std::sqrt(min * max));
            auto* p = new Parameter(juce::ParameterID{id, 1}, title, range, value);
            params.push_back(p);
            addParameter(p);
        };
        if (isEq()) {
            const int bands = bandCount();
            for (int i = 0; i < bands; ++i) {
                const float hz = centre(i);
                auto id = juce::String(i + 1);
                if (mode != Mode::graphic)
                    control("freq" + id, "B" + id + " Hz", 25, 20000, hz, true);
                control("gain" + id, mode == Mode::graphic ? juce::String(hz, 0) + " Hz" : "B" + id + " dB", -18, 18,
                        0);
                if (mode != Mode::graphic)
                    control("q" + id, "B" + id + " Q", .25, 8, 1.3f, true);
            }
        } else if (isMulti() || mode == Mode::bus) {
            if (isMulti()) {
                control("lowX", "Low split Hz", 60, 750, 200, true);
                control("highX", "High split Hz", 900, 9000, 3200, true);
            }
            const int count = isMulti() ? 3 : 1;
            const bool glue = mode != Mode::multi;
            for (int i = 0; i < count; ++i) {
                const juce::String band = count == 1 ? "" : i == 0 ? "Low " : i == 1 ? "Mid " : "High ";
                auto suffix = juce::String(i + 1);
                control("threshold" + suffix, band + "threshold dB", -60, 0, glue ? -16 : -22);
                control("ratio" + suffix, band + "ratio", 1, 16, glue ? 2 : 4);
            }
            control("attack", "Attack ms", .2, 100, glue ? 25 : 8, true);
            control("release", "Release ms", 15, 1000, glue ? 200 : 110, true);
            control("mix", "Wet %", 0, 100, glue ? 78 : 100);
        } else if (mode == Mode::dynamic || mode == Mode::deEss) {
            control("frequency", "Detector Hz", mode == Mode::deEss ? 2500.f : 40.f, 16000,
                    mode == Mode::deEss ? 6500 : 1200, true);
            control("q", "Bandwidth Q", .3, 8, 1.2f, true);
            control("threshold", "Threshold dB", -66, -3, -28);
            control("range", "Maximum cut dB", 0, 24, 10);
            control("release", "Release ms", 10, 500, 90, true);
        } else if (mode == Mode::gate) {
            control("threshold", "Open dB", -80, 0, -35);
            control("floor", "Closed dB", -90, 0, -70);
            control("attack", "Attack ms", .1, 50, 2, true);
            control("release", "Release ms", 5, 800, 100, true);
        } else if (mode == Mode::delay) {
            control("time", "Delay ms", 1, 1500, 325, true);
            control("feedback", "Feedback %", 0, 93, 34);
            control("mix", "Wet %", 0, 100, 26);
        } else if (mode == Mode::electric || mode == Mode::acoustic || mode == Mode::bass) {
            if (mode == Mode::acoustic) {
                control("cut", "Low cut Hz", 30, 300, 85, true);
                control("notch", "Notch Hz", 70, 1800, 240, true);
                control("body", "Notch cut dB", -18, 0, -4);
                control("air", "Air dB", -9, 9, 2);
                control("room", "Room %", 0, 45, 10);
            } else {
                control("drive", "Drive dB", 0, 36, mode == Mode::electric ? 16 : 8);
                control("tone", "Cabinet Hz", 500, 12000, mode == Mode::electric ? 3800 : 2400, true);
                control("blend", mode == Mode::bass ? "Amp blend %" : "Amp mix %", 0, 100,
                        mode == Mode::bass ? 55 : 85);
                control("threshold", "Compressor dB", -36, 0, mode == Mode::bass ? -17 : -12);
                control("output", "Output dB", -24, 12, -2);
            }
        } else {
            control("cutoff", "Cutoff Hz", 20, 20000, mode == Mode::highPass ? 110 : 3200, true);
            control("q", "Resonance Q", .35, 8, .707f, true);
            if (mode == Mode::filter)
                control("morph", "Low / band / high", 0, 2, .65f);
        }
    }
    const juce::String getName() const override { return name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override {
        return mode == Mode::delay ? 145.0 : mode == Mode::acoustic ? 3.0 : .2;
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new ostfstudio::EffectControlPanel(*this, params, mode == Mode::graphic); }
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        const auto in = b.getMainInputChannelSet();
        return in == b.getMainOutputChannelSet() &&
               (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::XmlElement xml(stateTag);
        for (auto* p : params)
            xml.setAttribute(p->paramID, p->convertTo0to1(p->get()));
        copyXmlToBinary(xml, dest);
    }
    void setStateInformation(const void* data, int size) override {
        if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(stateTag))
            for (auto* p : params) {
                auto v = xml->getDoubleAttribute(p->paramID, p->convertTo0to1(p->get()));
                if (std::isfinite(v))
                    p->setValueNotifyingHost(float(std::clamp(v, 0., 1.)));
            }
    }
    void prepareToPlay(double rate, int block) override {
        sr = rate;
        for (auto& band : filters)
            for (auto& f : band) {
                *f.coefficients = std::array<float, 6>{1, 0, 0, 1, 0, 0};
                f.prepare({sr, juce::uint32(std::max(1, block)), 1});
            }
        if (mode == Mode::delay)
            delay.setSize(2, int(std::ceil(sr * 1.5)) + 2);
        if (mode == Mode::acoustic)
            room.setSampleRate(sr);
        reset();
    }
    void releaseResources() override {}
    void reset() override {
        for (auto& b : filters)
            for (auto& f : b)
                f.reset();
        envelope = {};
        low = {};
        mid = {};
        write = 0;
        delay.clear();
        room.reset();
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals safe;
        const int channels = audio.getNumChannels(), samples = audio.getNumSamples();
        if (channels < 1 || channels > 2 || samples == 0)
            return;
        if (isEq()) {
            for (int i = 0; i < bandCount(); ++i) {
                const bool fixed = mode == Mode::graphic;
                set(i, Coeff::makePeakFilter(sr, hz(fixed ? centre(i) : p(i * 3)), fixed ? 1.414f : p(i * 3 + 2),
                                             gain(p(fixed ? i : i * 3 + 1))));
                for (int ch = 0; ch < channels; ++ch)
                    run(audio, ch, i);
            }
        } else if (mode == Mode::delay) {
            const int len = delay.getNumSamples(), offset = std::clamp(int(sr * p(0) * .001), 1, len - 1);
            const float feedback = p(1) * .01f, wet = p(2) * .01f;
            for (int i = 0; i < samples; ++i) {
                const int read = (write + len - offset) % len;
                for (int ch = 0; ch < channels; ++ch) {
                    float dry = audio.getSample(ch, i), echo = delay.getSample(ch, read);
                    delay.setSample(ch, write, dry + echo * feedback);
                    audio.setSample(ch, i, dry + wet * (echo - dry));
                }
                write = (write + 1) % len;
            }
        } else if (mode == Mode::lowPass || mode == Mode::highPass || mode == Mode::filter) {
            set(0, mode == Mode::highPass ? Coeff::makeHighPass(sr, hz(p(0)), p(1))
                                          : Coeff::makeLowPass(sr, hz(p(0)), p(1)));
            if (mode != Mode::filter) {
                for (int ch = 0; ch < channels; ++ch)
                    run(audio, ch, 0);
                return;
            }
            set(1, Coeff::makeBandPass(sr, hz(p(0)), p(1)));
            set(2, Coeff::makeHighPass(sr, hz(p(0)), p(1)));
            const float morph = p(2);
            for (int ch = 0; ch < channels; ++ch)
                for (auto& sample : juce::Span<float>(audio.getWritePointer(ch), size_t(samples))) {
                    const float a = filters[0][ch].processSample(sample), b = filters[1][ch].processSample(sample),
                                c = filters[2][ch].processSample(sample);
                    sample = morph < 1 ? a + morph * (b - a) : b + (morph - 1) * (c - b);
                }
        } else if (isMulti() || mode == Mode::bus || mode == Mode::gate || mode == Mode::deEss || mode == Mode::dynamic)
            dynamics(audio);
        else
            rig(audio);
    }

  private:
    bool isEq() const { return mode == Mode::eight || mode == Mode::graphic || mode == Mode::parametric; }
    bool isMulti() const { return mode == Mode::multi || mode == Mode::multiBus; }
    int bandCount() const { return mode == Mode::graphic ? 10 : mode == Mode::eight ? 8 : 4; }
    float centre(int i) const {
        if (mode == Mode::graphic)
            return 31.25f * float(1 << i);
        return float(55 * std::pow(2., i * (mode == Mode::eight ? 1.12 : 2.4)));
    }
    float p(int i) const { return params[size_t(i)]->get(); }
    float hz(float f) const { return std::min(f, float(sr * .475)); }
    static float gain(float db) { return juce::Decibels::decibelsToGain(db); }
    float coefficient(float ms) const { return float(std::exp(-1000. / (sr * ms))); }
    float pole(float f) const { return float(1. - std::exp(-juce::MathConstants<double>::twoPi * hz(f) / sr)); }
    void set(int i, const std::array<float, 6>& c) {
        for (auto& f : filters[i])
            *f.coefficients = c;
    }
    void run(juce::AudioBuffer<float>& a, int ch, int band) {
        auto* data = a.getWritePointer(ch);
        for (int i = 0; i < a.getNumSamples(); ++i)
            data[i] = filters[band][ch].processSample(data[i]);
    }
    float follow(int band, float level, float attack, float release) {
        const float smooth = level > envelope[band] ? attack : release;
        return envelope[band] = level + smooth * (envelope[band] - level);
    }
    static float compression(float level, float threshold, float exponent) {
        return level > threshold ? std::pow(threshold / level, exponent) : 1.f;
    }
    void dynamics(juce::AudioBuffer<float>& a) {
        const bool multi = isMulti(), spectral = mode == Mode::deEss || mode == Mode::dynamic,
                   gate = mode == Mode::gate;
        const int n = multi ? 3 : 1, base = multi ? 2 : 0;
        const float attack = coefficient(spectral ? 2 : p(gate ? 2 : base + 2 * n));
        const float release = coefficient(p(spectral ? 4 : gate ? 3 : base + 2 * n + 1));
        const float wet = gate || spectral ? 1 : p(base + 2 * n + 2) * .01f;
        const float floor = gate ? gain(p(1)) : spectral ? gain(-p(3)) : 0;
        std::array<float, 3> threshold{}, exp{};
        for (int b = 0; b < n; ++b) {
            threshold[b] = gain(p(spectral ? 2 : base + 2 * b));
            exp[b] = gate || spectral ? 1 : 1 - 1 / p(base + 2 * b + 1);
        }
        if (spectral)
            set(0, mode == Mode::deEss ? Coeff::makeHighPass(sr, hz(p(0)), p(1))
                                       : Coeff::makeBandPass(sr, hz(p(0)), p(1)));
        const float lowPole = multi ? pole(p(0)) : 0, highPole = multi ? pole(p(1)) : 0;
        for (int i = 0; i < a.getNumSamples(); ++i) {
            float bands[3][2]{}, peaks[3]{}, out[2]{};
            for (int ch = 0; ch < a.getNumChannels(); ++ch) {
                const float dry = a.getSample(ch, i);
                if (multi) {
                    low[ch] += lowPole * (dry - low[ch]);
                    const float upper = dry - low[ch];
                    mid[ch] += highPole * (upper - mid[ch]);
                    bands[0][ch] = low[ch];
                    bands[1][ch] = mid[ch];
                    bands[2][ch] = upper - mid[ch];
                } else if (spectral) {
                    bands[0][ch] = filters[0][ch].processSample(dry);
                    out[ch] = dry - bands[0][ch];
                } else
                    bands[0][ch] = dry;
                for (int b = 0; b < n; ++b)
                    peaks[b] = std::max(peaks[b], std::abs(bands[b][ch]));
            }
            for (int b = 0; b < n; ++b) {
                const bool busDetector = mode == Mode::bus || mode == Mode::multiBus;
                const float level = busDetector ? std::sqrt(follow(b, peaks[b] * peaks[b], attack, release))
                                                : follow(b, peaks[b], attack, release);
                const float open = std::clamp((level / threshold[b] - .5f) * 2, 0.f, 1.f);
                const float g =
                    gate ? floor + (1 - floor) * open : std::max(floor, compression(level, threshold[b], exp[b]));
                for (int ch = 0; ch < a.getNumChannels(); ++ch)
                    out[ch] += bands[b][ch] * g;
            }
            for (int ch = 0; ch < a.getNumChannels(); ++ch) {
                const float dry = a.getSample(ch, i);
                a.setSample(ch, i, dry + wet * (out[ch] - dry));
            }
        }
    }
    void rig(juce::AudioBuffer<float>& a) {
        if (mode == Mode::acoustic) {
            set(0, Coeff::makeHighPass(sr, hz(p(0))));
            set(1, Coeff::makePeakFilter(sr, hz(p(1)), 4.f, gain(p(2))));
            set(2, Coeff::makeHighShelf(sr, hz(5500), .707f, gain(p(3))));
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                for (int b = 0; b < 3; ++b)
                    run(a, ch, b);
            juce::Reverb::Parameters r;
            r.roomSize = .35f;
            r.wetLevel = p(4) / 300.f;
            r.dryLevel = (1 - p(4) / 100.f) * .5f;
            room.setParameters(r);
            if (a.getNumChannels() == 2)
                room.processStereo(a.getWritePointer(0), a.getWritePointer(1), a.getNumSamples());
            else
                room.processMono(a.getWritePointer(0), a.getNumSamples());
            return;
        }
        const bool bass = mode == Mode::bass;
        const float drive = gain(p(0)), wet = p(2) * .01f, threshold = gain(p(3)), output = gain(p(4));
        set(0, Coeff::makeHighPass(sr, hz(bass ? 160 : 78)));
        set(1, Coeff::makeLowPass(sr, hz(p(1))));
        const float attack = coefficient(6), release = coefficient(110);
        for (int i = 0; i < a.getNumSamples(); ++i) {
            float peak = 0;
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                peak = std::max(peak, std::abs(a.getSample(ch, i)));
            const float g = compression(follow(0, peak, attack, release), threshold, .65f);
            for (int ch = 0; ch < a.getNumChannels(); ++ch) {
                const float dry = a.getSample(ch, i) * g, upper = filters[0][ch].processSample(dry);
                const float amp =
                    filters[1][ch].processSample(.6f * std::tanh(upper * drive)) + (bass ? dry - upper : 0);
                a.setSample(ch, i, output * (dry + wet * (amp - dry)));
            }
        }
    }
    Mode mode;
    juce::String name, stateTag;
    std::vector<Parameter*> params;
    std::array<std::array<Biquad, 2>, 10> filters;
    std::array<float, 2> low{}, mid{};
    std::array<float, 3> envelope{};
    juce::AudioBuffer<float> delay;
    int write = 0;
    double sr = 44100;
    juce::Reverb room;
};
} // namespace
juce::AudioProcessor* makeMixingEffect(const char* type, const char* name) {
    for (auto [key, mode] : modes)
        if (std::string_view(type) == key)
            return new MixingEffect(mode, name, key);
    return nullptr;
}
