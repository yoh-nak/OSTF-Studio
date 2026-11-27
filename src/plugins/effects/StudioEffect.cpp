#include "../PluginCcMap.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../StudioPluginEditor.h"
#include "CompressorListen.h"
#include <array>
#include <cmath>

juce::AudioProcessor* makeMixingEffect(const char* type, const char* name);

namespace {
enum class Kind { Reverb, Chorus, EQ, Compressor, Limiter, Phaser, Flanger, Distortion, Amp };
struct Control { const char* name; float low, high, initial; };
struct Design { Kind kind; const char* type; std::array<Control, 3> controls; };
constexpr Design designs[] {
    {Kind::Reverb, "Reverb", {{{"Room size %", 0, 100, 65}, {"Damping %", 0, 100, 50}, {"Wet %", 0, 100, 100}}}},
    {Kind::Chorus, "Chorus", {{{"Rate Hz", 0.05f, 5, 0.75f}, {"Depth %", 0, 100, 36}, {"Wet %", 0, 100, 30}}}},
    {Kind::EQ, "EQ", {{{"Low shelf dB", -12, 12, -1.5f}, {"Mid 1 kHz dB", -12, 12, 0}, {"High shelf dB", -12, 12, 1.5f}}}},
    {Kind::Compressor, "Compressor", {{{"Threshold dB", -48, 0, -18}, {"Ratio", 1, 16, 3}, {"Release ms", 20, 600, 140}}}},
    {Kind::Limiter, "Limiter", {{{"Ceiling dB", -18, 0, -1.2f}, {"Input gain dB", 0, 18, 0}, {"Release ms", 20, 500, 90}}}},
    {Kind::Phaser, "Phaser", {{{"Rate Hz", 0.05f, 4, 0.35f}, {"Depth %", 0, 100, 64}, {"Wet %", 0, 100, 30}}}},
    {Kind::Flanger, "Flanger", {{{"Rate Hz", 0.05f, 4, 0.22f}, {"Depth %", 0, 100, 48}, {"Wet %", 0, 100, 28}}}},
    {Kind::Distortion, "Distortion", {{{"Drive dB", 0, 36, 12}, {"Tone Hz", 500, 12000, 4500}, {"Wet %", 0, 100, 32}}}},
    {Kind::Amp, "Amp", {{{"Preamp dB", 0, 36, 16}, {"Cabinet tone Hz", 1200, 7000, 3500}, {"Wet %", 0, 100, 65}}}},
};

class RackEditor final : public ostfstudio::StudioPluginEditor, private juce::Timer {
public:
    RackEditor(juce::AudioProcessor& processor, const std::array<juce::AudioParameterFloat*, 3>& params,
               ostfstudio::CompressorListen* measurement,ostfstudio::PluginCcMap& map)
        : StudioPluginEditor(processor), title(processor.getName()), listen(measurement), threshold(params[0]), ratio(params[1]),ccButton(map) {
        addAndMakeVisible(ccButton);
        for (std::size_t i = 0; i < sliders.size(); ++i) {
            auto& slider = sliders[i];
            const auto name = params[i]->getName(64);
            const bool fader = name == "Wet %" || name == "Threshold dB" || name == "Ceiling dB";
            slider.setName(name);
            slider.setTitle(name);
            slider.setSliderStyle(fader ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 21);
            labels[i].setText(name, juce::dontSendNotification);
            labels[i].setJustificationType(juce::Justification::centred);
            labels[i].setInterceptsMouseClicks(false, false);
            addAndMakeVisible(labels[i]);
            addAndMakeVisible(slider);
            bindings[i] = std::make_unique<juce::SliderParameterAttachment>(*params[i], slider);
        }
        if (listen) {
            addAndMakeVisible(learn);
            addAndMakeVisible(apply);
            addAndMakeVisible(advice);
            learn.onClick = [this] { listen->state.store(ostfstudio::CompressorListen::requested); timerCallback(); };
            apply.onClick = [this] {
                if (listen->state.load() != ostfstudio::CompressorListen::ready || ratio->get() <= 1.0f) return;
                threshold->beginChangeGesture();
                threshold->setValueNotifyingHost(threshold->convertTo0to1(listen->threshold(ratio->get())));
                threshold->endChangeGesture();
                listen->state.store(ostfstudio::CompressorListen::idle);
                timerCallback();
            };
            timerCallback();
            startTimerHz(5);
        }
        setSize(456, listen ? 270 : 230);
    }

    void resized() override {
        ccButton.setBounds(getWidth()-96,12,78,27);
        auto panel = getLocalBounds().reduced(18, 12).withTrimmedTop(43);
        if (listen) {
            auto row = panel.removeFromBottom(40).reduced(3, 5);
            learn.setBounds(row.removeFromLeft(110));
            apply.setBounds(row.removeFromRight(70));
            advice.setBounds(row);
        }
        const int width = panel.getWidth() / static_cast<int>(sliders.size());
        for (std::size_t i = 0; i < sliders.size(); ++i) {
            auto bay = panel.removeFromLeft(width).reduced(8, 5);
            labels[i].setBounds(bay.removeFromTop(23));
            sliders[i].setBounds(bay.reduced(3, 3));
        }
    }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        const auto colour = juce::Colour(face.panel);
        ostfstudio::console::LookAndFeel::verticalGradient(g, bounds, colour.brighter(.08f).getARGB(), face.background);
        g.fillAll();
        g.setColour(juce::Colour(face.background));
        g.fillRect(0, 0, getWidth(), 48);
        g.setColour(juce::Colour(face.accent));
        g.fillRect(20, 14, 3, 20);
        g.setFont(juce::FontOptions(22.f, juce::Font::bold));
        g.setColour(juce::Colour(0xfff3f1e9));
        g.drawText(title, 32, 4, getWidth()-64, 40, juce::Justification::centredLeft);
        for (std::size_t i = 1; i < sliders.size(); ++i) {
            g.setColour(juce::Colours::white.withAlpha(.13f));
            const int x = (sliders[i-1].getRight() + sliders[i].getX()) / 2;
            g.drawVerticalLine(x, 68.f, bounds.getBottom()-24.f);
        }
        for (auto x : {9.f, bounds.getRight()-9.f}) {
            for (auto y : {9.f, bounds.getBottom()-9.f}) {
                g.setColour(juce::Colour(0xff161b1d));
                g.fillEllipse(x-3.6f,y-3.4f,7.2f,7.2f);
                g.setColour(juce::Colour(0xff788184));
                g.fillEllipse(x-2.7f,y-3.f,5.4f,5.4f);
                g.setColour(juce::Colour(0xff2d3437));
                g.drawLine(x-1.6f,y+1.f,x+1.6f,y-1.5f,1.1f);
            }
        }
    }
private:
    void timerCallback() override {
        using Meter = ostfstudio::CompressorListen;
        const auto state = listen->state.load();
        const bool busy = state == Meter::requested || state == Meter::listening;
        learn.setEnabled(!busy);
        const bool usable = state == Meter::ready && ratio->get() > 1.0f;
        apply.setEnabled(usable);
        const juce::String text = busy ? "Listening to input..." : state == Meter::noSignal ? "No input detected"
            : usable ? juce::String(listen->threshold(ratio->get()), 1) + " dB threshold"
            : state == Meter::ready ? "Choose a ratio above 1:1" : "";
        advice.setText(text, juce::dontSendNotification);
    }
    const juce::String title;
    ostfstudio::CompressorListen* listen;
    juce::AudioParameterFloat *threshold, *ratio;
    juce::TextButton learn{"Listen (3 s)"}, apply{"Apply"};
    juce::Label advice;
    ostfstudio::PluginCcButton ccButton;
    std::array<juce::Slider, 3> sliders;
    std::array<juce::Label, 3> labels;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, 3> bindings;
};

class StudioEffect final : public juce::AudioProcessor {
public:
    ostfstudio::PluginCcMap ccMap{*this};
    StudioEffect(const Design& design, const char* name)
        : AudioProcessor([&] {
              auto io = BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true);
              if (design.kind == Kind::Compressor) io = io.withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
              return io.withOutput("Output", juce::AudioChannelSet::stereo(), true);
          }()),
          design_(design), name_(name) {
        for (int i = 0; i < 3; ++i) {
            const auto& c = design.controls[i];
            params_[i] = new juce::AudioParameterFloat(juce::ParameterID{"control" + juce::String(i + 1), 1},
                                                       c.name, c.low, c.high, c.initial);
            addParameter(params_[i]);
        }
    }
    const juce::String getName() const override { return name_; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new RackEditor(*this, params_, design_.kind == Kind::Compressor ? &listen_ : nullptr,ccMap); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    double getTailLengthSeconds() const override { return design_.kind == Kind::Reverb ? 5.0 : 0.2; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        if (layout.inputBuses.size() > 1 && !layout.inputBuses[1].isDisabled()
            && layout.inputBuses[1] != juce::AudioChannelSet::mono() && layout.inputBuses[1] != juce::AudioChannelSet::stereo()) return false;
        const auto input = layout.getMainInputChannelSet();
        return input == layout.getMainOutputChannelSet()
            && (input == juce::AudioChannelSet::stereo() || input == juce::AudioChannelSet::mono());
    }
    void getStateInformation(juce::MemoryBlock& destination) override {
        juce::XmlElement xml(design_.type);
        for (auto* p : params_) xml.setAttribute(p->paramID, p->convertTo0to1(p->get()));
        ccMap.save(xml);
        copyXmlToBinary(xml, destination);
    }
    void setStateInformation(const void* data, int size) override {
        if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(design_.type)) {
            ccMap.restore(*xml);
            for (auto* p : params_) {
                const auto value = xml->getDoubleAttribute(p->paramID, p->convertTo0to1(p->get()));
                if (std::isfinite(value)) p->setValueNotifyingHost(static_cast<float>(std::clamp(value, 0.0, 1.0)));
            }
        }
    }
    void prepareToPlay(double rate, int block) override {
        rate_ = rate;
        const juce::dsp::ProcessSpec spec{rate, static_cast<juce::uint32>(std::max(1, block)),
                                         static_cast<juce::uint32>(getTotalNumOutputChannels())};
        if (design_.kind == Kind::Reverb) reverb_.setSampleRate(rate);
        if (design_.kind == Kind::Chorus || design_.kind == Kind::Flanger) {
            chorus_.prepare(spec);
            chorus_.setCentreDelay(design_.kind == Kind::Flanger ? 2.2f : 14.0f);
            chorus_.setFeedback(design_.kind == Kind::Flanger ? 0.52f : 0.08f);
        }
        if (design_.kind == Kind::Phaser) phaser_.prepare(spec);
        if (design_.kind == Kind::EQ)
            for (auto& channel : filters_) for (auto& filter : channel) {
                *filter.coefficients = std::array<float, 6>{1, 0, 0, 1, 0, 0};
                filter.prepare(spec);
            }
        reset();
    }
    void releaseResources() override {}
    void reset() override {
        reverb_.reset();
        if (design_.kind == Kind::Chorus || design_.kind == Kind::Flanger) chorus_.reset();
        if (design_.kind == Kind::Phaser) phaser_.reset();
        for (auto& channel : filters_) for (auto& filter : channel) filter.reset();
        cabinet_ = {};
        dc_ = {};
        reduction_ = 1.0f;
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        ccMap.process(midi); midi.clear();
        auto audio = getBusBuffer(buffer, false, 0);
        juce::ScopedNoDenormals noDenormals;
        if (audio.getNumChannels() < 1 || audio.getNumSamples() == 0) return;
        const float a = *params_[0], b = *params_[1], c = *params_[2];
        juce::dsp::AudioBlock<float> block(audio);
        juce::dsp::ProcessContextReplacing<float> context(block);
        switch (design_.kind) {
            case Kind::Reverb: {
                juce::Reverb::Parameters p;
                p.roomSize = a * 0.01f; p.damping = b * 0.01f;
                // JUCE internally scales wet by three and dry by two.
                p.wetLevel = c / 300.0f; p.dryLevel = (1.0f - c * 0.01f) * 0.5f;
                reverb_.setParameters(p);
                if (audio.getNumChannels() == 1) reverb_.processMono(audio.getWritePointer(0), audio.getNumSamples());
                else reverb_.processStereo(audio.getWritePointer(0), audio.getWritePointer(1), audio.getNumSamples());
                break;
            }
            case Kind::Chorus: case Kind::Flanger:
                chorus_.setRate(a); chorus_.setDepth(b * 0.01f); chorus_.setMix(c * 0.01f); chorus_.process(context); break;
            case Kind::Phaser:
                phaser_.setRate(a); phaser_.setDepth(b * 0.01f); phaser_.setFeedback(0.33f);
                phaser_.setMix(c * 0.01f); phaser_.process(context); break;
            case Kind::EQ: {
                using Coeff = juce::dsp::IIR::ArrayCoefficients<float>;
                const auto gain = [](float db) { return juce::Decibels::decibelsToGain(db); };
                const auto high = static_cast<float>(std::min(6000.0, rate_ * 0.4));
                const std::array coefficients {Coeff::makeLowShelf(rate_, 180, 0.707f, gain(a)),
                    Coeff::makePeakFilter(rate_, std::min(1000.f, high), 0.8f, gain(b)),
                    Coeff::makeHighShelf(rate_, high, 0.707f, gain(c))};
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    for (int band = 0; band < 3; ++band) {
                        auto& filter = filters_[ch][band];
                        *filter.coefficients = coefficients[band];
                        auto* data = audio.getWritePointer(ch);
                        for (int i = 0; i < audio.getNumSamples(); ++i) data[i] = filter.processSample(data[i]);
                    }
                break;
            }
            case Kind::Compressor: case Kind::Limiter: {
                const bool limit = design_.kind == Kind::Limiter;
                if (!limit) listen_.beginBlock(rate_);
                bool measuring = !limit && listen_.state.load() == ostfstudio::CompressorListen::listening;
                const float threshold = juce::Decibels::decibelsToGain(a);
                const float input = limit ? juce::Decibels::decibelsToGain(b) : 1.0f;
                const float attack = limit ? 0.0f : static_cast<float>(std::exp(-1.0 / (rate_ * 0.008)));
                const float release = static_cast<float>(std::exp(-1000.0 / (rate_ * c)));
                const bool external = !limit && getChannelCountOfBus(true, 1) > 0;
                const auto detector = getBusBuffer(buffer, true, external ? 1 : 0);
                for (int i = 0; i < audio.getNumSamples(); ++i) {
                    float peak = 0;
                    for (int ch = 0; ch < detector.getNumChannels(); ++ch) peak = std::max(peak, input * std::abs(detector.getSample(ch, i)));
                    if (measuring) {
                        listen_.measure(peak);
                        measuring = listen_.state.load(std::memory_order_relaxed) == ostfstudio::CompressorListen::listening;
                    }
                    const float target = peak <= threshold ? 1.0f : limit ? threshold / peak : std::pow(threshold / peak, 1.0f - 1.0f / b);
                    const float smooth = target < reduction_ ? attack : release;
                    reduction_ = target + smooth * (reduction_ - target);
                    for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.setSample(ch, i, audio.getSample(ch, i) * input * reduction_);
                }
                break;
            }
            case Kind::Distortion: case Kind::Amp: {
                const bool amp = design_.kind == Kind::Amp;
                const float drive = juce::Decibels::decibelsToGain(a), wet = c * 0.01f;
                const auto pole = [this](float hz) { return static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * hz / rate_)); };
                const float tone = pole(b), bass = pole(95.0f);
                for (int ch = 0; ch < audio.getNumChannels(); ++ch) {
                    auto* data = audio.getWritePointer(ch);
                    for (int i = 0; i < audio.getNumSamples(); ++i) {
                        const float dry = data[i];
                        dc_[ch] += bass * (dry - dc_[ch]);
                        const float preamp = amp ? dry - dc_[ch] : dry;
                        const float shaped = std::tanh(preamp * drive) * 0.55f;
                        cabinet_[ch][0] += tone * (shaped - cabinet_[ch][0]);
                        cabinet_[ch][1] += tone * (cabinet_[ch][0] - cabinet_[ch][1]);
                        data[i] = dry + wet * ((amp ? cabinet_[ch][1] : cabinet_[ch][0]) - dry);
                    }
                }
                break;
            }
        }
    }
private:
    const Design& design_;
    juce::String name_;
    std::array<juce::AudioParameterFloat*, 3> params_;
    double rate_ = 44100.0;
    juce::Reverb reverb_;
    juce::dsp::Chorus<float> chorus_;
    juce::dsp::Phaser<float> phaser_;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 3>, 2> filters_;
    std::array<std::array<float, 2>, 2> cabinet_{};
    std::array<float, 2> dc_{};
    float reduction_ = 1;
    ostfstudio::CompressorListen listen_;
};
}

juce::AudioProcessor* makeStudioEffect(const char* type, const char* name) {
    for (const auto& design : designs) if (std::string_view(type) == design.type) return new StudioEffect(design, name);
    return makeMixingEffect(type, name);
}
