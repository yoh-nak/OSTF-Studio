#include "StudioInstrument.h"
#include "../EditorProgramParameter.h"
#include "../StudioPluginEditor.h"
#include <array>
#include <cmath>

namespace ostfstudio {
namespace {
constexpr double tau = juce::MathConstants<double>::twoPi;
struct Patch {
    const char* name;
    float attack, decay, sustain, release, colour, motion;
};
constexpr Patch banks[][4] {
    {{"Glass Keys", .004f,.85f,.24f,.32f,1.f,2.f}, {"Prism Pluck",.003f,.21f,.13f,.12f,1.8f,3.f},
     {"Bell Cascade",.002f,1.6f,.04f,.9f,2.1f,3.51f}, {"Reed Electric",.026f,.48f,.65f,.22f,.85f,1.f}},
    {{"Round Bass",.005f,.25f,.70f,.08f,.20f,.12f}, {"Offbeat Sub",.003f,.12f,.5f,.055f,.13f,.32f},
     {"Twin Saw",.012f,.5f,.72f,.20f,.56f,.66f}, {"Velvet Brass",.085f,.5f,.65f,.34f,.35f,.82f}},
    {{"Studio Kit",.001f,.32f,0,.09f,.90f,1.f}, {"Night Drive",.001f,.44f,0,.10f,.78f,.84f},
     {"Tight Machine",.001f,.18f,0,.07f,1.1f,1.15f}, {"Broken Circuit",.001f,.57f,0,.12f,1.25f,.70f}},
    {{"Air Pad",.23f,.90f,.71f,.82f,.15f,.19f}, {"Glass Tide",.13f,.70f,.69f,.76f,.44f,.33f},
     {"Horizon Sweep",.065f,.62f,.61f,.60f,.78f,.39f}, {"Digital Stab",.006f,.24f,.19f,.16f,.65f,1.f}}
};
bool isCymbal(int key) {
    switch (key) {
        case 42: case 44: case 46: case 49: case 51: case 52: case 53: case 55: case 57: case 59: return true;
        default: return false;
    }
}
double wrap(double phase) { return phase - std::floor(phase); }
float sine(double phase) { return float(std::sin(tau * phase)); }
// Subtract the discontinuity integrated over the sample interval from a naive saw.
float saw(double phase, double step) {
    double value = 2 * phase - 1;
    if (phase < step) { const auto x = phase / step; value -= 2*x - x*x - 1; }
    else if (phase > 1-step) { const auto x = (phase-1) / step; value -= x*x + 2*x + 1; }
    return float(value);
}

struct WaveTables {
    static constexpr int size = 1024, levels = 7;
    using Table = std::array<float, size + 1>;
    std::array<std::array<Table, 3>, levels> frames {};
    WaveTables() {
        for (int level = 0; level < levels; ++level) {
            for (int frame = 0; frame < 3; ++frame) {
                auto& table = frames[level][frame];
                for (int h = 1; h <= (1 << level); ++h) {
                    const float weight = frame == 0 ? (h % 2 ? 1.f/(h*h) : 0.f)
                        : frame == 1 ? 1.f/h : ((h % 3 == 1 ? 1.f : .18f) / std::sqrt(float(h)));
                    for (int n = 0; n < size; ++n) table[n] += weight * sine(double(h*n)/size);
                }
                float peak = .001f;
                for (auto sample : table) peak = std::max(peak, std::abs(sample));
                for (auto& sample : table) sample /= peak;
                table[size] = table[0];
            }
        }
    }
    float read(int level, double phase, float position) const {
        const float pos = std::clamp(position, 0.f, 1.f) * 2;
        const int frame = std::min(1, int(pos)), index = std::min(size-1, int(phase*size));
        const float fraction = float(phase*size-index);
        const auto at = [&](int f) {
            const auto& table = frames[level][f];
            return std::lerp(table[index], table[index+1], fraction);
        };
        return std::lerp(at(frame), at(frame+1), pos-frame);
    }
};
const WaveTables& tables() { static const WaveTables waves; return waves; }

struct Voice {
    int channel = 0, key = 0, program = 0;
    bool keyed = false, releasing = false;
    std::uint64_t age = 0;
    double phase = 0, second = 0, sweep = 0, hz = 0;
    float amplitude = 0, envelope = 0, transient = 1, transientDecay = 1;
    float attackStep = 1, decayFactor = 0, releaseFactor = 0;
    float low = 0, pole = 0, lastNoise = 0;
    std::uint32_t seed = 1;
};

class Instrument final : public juce::AudioProcessor {
public:
    Instrument(Engine e, juce::String title)
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          engine(e), name(std::move(title)) {
        if (engine == Engine::Wavetable) (void)tables();
        addParameter(tone = new juce::AudioParameterFloat({"colour",1}, "Tone", 0.f, 1.f, .5f));
        addParameter(level = new juce::AudioParameterFloat({"gain",1}, "Output (dB)", -36.f, 6.f, -3.f));
        addParameter(editorProgram = new ostfstudio::EditorProgramParameter("midiProgram", "MIDI Program", program, 4));
    }
    const juce::String getName() const override { return name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            || layout.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 4; }
    void chooseProgram(int preset) { editorProgram->choose(preset); }
    int getCurrentProgram() override { return program.load(); }
    void setCurrentProgram(int p) override { if (p >= 0 && p < 4) program.store(p); }
    const juce::String getProgramName(int p) override { return p >= 0 && p < 4 ? patch(p).name : ""; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& out) override {
        juce::XmlElement state("StudioInstrument");
        state.setAttribute("engine", int(engine));
        state.setAttribute("program", program.load());
        state.setAttribute("tone", double(tone->get()));
        state.setAttribute("output", double(level->get()));
        copyXmlToBinary(state, out);
    }
    void setStateInformation(const void* data, int length) override {
        if (auto state = getXmlFromBinary(data,length); state && state->hasTagName("StudioInstrument")
                && state->getIntAttribute("engine",-1) == int(engine)) {
            setCurrentProgram(state->getIntAttribute("program"));
            *tone = float(state->getDoubleAttribute("tone",.5));
            *level = float(state->getDoubleAttribute("output",-3));
        }
    }
    void prepareToPlay(double rate, int) override { sampleRate = std::max(1., rate); reset(); }
    void releaseResources() override { reset(); }
    void reset() override { voices.fill({}); channels.fill({}); }
    void processBlock(juce::AudioBuffer<float>& out, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;
        out.clear();
        int from = 0;
        for (auto packet : midi) {
            const int next = std::clamp(packet.samplePosition, from, out.getNumSamples());
            render(out, from, next);
            handle(packet.getMessage());
            from = next;
        }
        render(out, from, out.getNumSamples());
        midi.clear();
    }
private:
    const Patch& patch(int p) const { return banks[int(engine)][p]; }
    struct Channel { double bend = 1; float volume = 1, expression = 1, pan = .5f; bool pedal = false; };
    Engine engine;
    juce::String name;
    std::atomic<int> program {0};
    ostfstudio::EditorProgramParameter* editorProgram{};
    juce::AudioParameterFloat *tone, *level;
    std::array<Voice, 32> voices {};
    std::array<Channel,16> channels {};
    double sampleRate = 44100;
    void start(int ch, int key, float velocity) {
        auto slot = std::find_if(voices.begin(), voices.end(), [](const auto& v){return !v.channel;});
        if (slot == voices.end()) slot = std::min_element(voices.begin(), voices.end(), [](const auto& a, const auto& b){
            return a.amplitude*a.envelope < b.amplitude*b.envelope;
        });
        if (engine == Engine::Drums && (key == 42 || key == 44))
            for (auto& v : voices) if (v.channel == ch && v.key == 46) v.releasing = true;
        auto& v = *slot;
        v = {};
        v.channel = ch; v.key = key; v.keyed = true; v.program = program.load();
        const auto& p = patch(v.program);
        v.hz = juce::MidiMessage::getMidiNoteInHertz(key);
        v.amplitude = velocity * std::sqrt(velocity);
        v.seed = 234523u + unsigned(key)*12347u;
        const bool drum = engine == Engine::Drums;
        const bool hat = key == 42 || key == 44 || key == 46;
        const float decay = drum && hat ? (key == 46 ? .48f : .075f) * p.motion : p.decay;
        v.attackStep = float(1/(p.attack*sampleRate));
        v.decayFactor = float(std::exp(-1./(decay*sampleRate)*(drum ? 5. : 1.)));
        v.releaseFactor = float(std::exp(-5./(p.release*sampleRate)));
        v.transientDecay = float(std::exp(-1./(sampleRate*(drum ? .026 : decay*.48))));
        if (drum) v.hz = p.colour * (key == 35 || key == 36 ? 52. : key == 38 || key == 40 ? 177.
                : isCymbal(key) ? (hat ? 3600. : 2200.) : 85.*std::exp2((key-41)/15.));
    }
    void handle(const juce::MidiMessage& m) {
        const int ch = m.getChannel();
        if (ch < 1 || ch > 16) return;
        auto& c = channels[size_t(ch-1)];
        if (m.isProgramChange()) setCurrentProgram(m.getProgramChangeNumber());
        else if (m.isNoteOn()) start(ch, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff() && engine != Engine::Drums) {
            for (auto& v : voices) if (v.channel == ch && v.key == m.getNoteNumber()) {
                v.keyed = false; v.releasing = !c.pedal;
            }
        } else if (m.isPitchWheel()) c.bend = std::exp2((m.getPitchWheelValue()-8192)/(8192.*6));
        else if (m.isController()) {
            const int cc = m.getControllerNumber(), value = m.getControllerValue();
            if (cc == 7) c.volume = value/127.f;
            if (cc == 11) c.expression = value/127.f;
            if (cc == 10) c.pan = value/127.f;
            if (cc == 64) c.pedal = value >= 64;
            if (cc == 121) c = {};
            for (auto& v : voices) if (v.channel == ch) {
                if (cc == 120) v.channel = 0;
                if (cc == 123 || ((cc == 64 || cc == 121) && !c.pedal && !v.keyed)) v.releasing = true;
            }
        }
    }
    float percussion(Voice& v) const {
        v.seed = v.seed*1664525u+1013904223u;
        const float noise = (v.seed >> 8)*(2.f/16777216.f)-1.f;
        const float high = (noise-v.lastNoise)*.5f;
        v.lastNoise = noise;
        const float body = sine(v.phase);
        if (v.key == 35 || v.key == 36) return .92f*body + .14f*high*v.transient;
        if (isCymbal(v.key))
            return .73f*high + .17f*sine(v.phase)*sine(v.second);
        if (v.key == 39) {
            const double time = v.age / sampleRate;
            const float burst = time < .035 ? .25f + .75f*float(std::exp(-4.*std::fmod(time,.012)/.012)) : .55f;
            return noise*burst;
        }
        if (v.key == 38 || v.key == 40) return .68f*noise + .32f*body*v.transient;
        return .8f*body + .25f*noise*v.transient;
    }
    void render(juce::AudioBuffer<float>& out, int from, int end) {
        if (from >= end || !out.getNumChannels()) return;
        const float toneValue = tone->get(), gain = juce::Decibels::decibelsToGain(level->get())*.29f;
        for (auto& v : voices) {
            if (!v.channel) continue;
            const auto& c = channels[size_t(v.channel-1)];
            const auto& p = patch(v.program);
            const double step = std::min(.43, v.hz*c.bend/sampleRate);
            const int mip = std::clamp(int(std::floor(std::log2(.48/step))),0,WaveTables::levels-1);
            const float stereo = out.getNumChannels() == 1 ? 1.f : std::cos(c.pan*float(tau/4));
            const float right = std::sin(c.pan*float(tau/4));
            const float amplitude = gain*c.volume*c.expression*v.amplitude;
            for (int i = from; i < end; ++i) {
                if (v.releasing) v.envelope *= v.releaseFactor;
                else if (v.age*v.attackStep < 1.f) v.envelope = std::min(1.f,v.envelope+v.attackStep);
                else v.envelope = p.sustain+(v.envelope-p.sustain)*v.decayFactor;
                if ((v.releasing || v.age*v.attackStep >= 1.f) && v.envelope < .00002f) { v.channel=0; break; }
                float value = 0;
                double increment = step, secondStep = step;
                switch (engine) {
                    case Engine::FM: {
                        secondStep = std::min(.47, step*p.motion);
                        const float index = (p.colour*(.18f+v.transient*2.f)*(.25f+toneValue))
                            * float(std::min(1., .18/std::max(step,secondStep)));
                        value = sine(v.phase + sine(v.second)*index/tau);
                        break;
                    }
                    case Engine::Analog: {
                        secondStep = step*(1. + p.motion*.009);
                        const float source = .55f*saw(v.phase,step) + .29f*saw(v.second,secondStep) + .22f*sine(v.phase);
                        const float cutoff = float(std::min(.72, tau*(180.+(800.+5800.*toneValue)*(p.colour+.45*v.transient))/sampleRate));
                        v.low += cutoff*(source-v.low-.28f*v.pole);
                        v.pole += cutoff*(v.low-v.pole);
                        value = v.pole;
                        break;
                    }
                    case Engine::Wavetable: {
                        secondStep = step*1.003;
                        const float morph = p.colour + (toneValue-.5f)*.7f + .19f*sine(v.sweep);
                        value = .65f*tables().read(mip,v.phase,morph) + .35f*tables().read(mip,v.second,morph);
                        v.sweep = wrap(v.sweep+p.motion/sampleRate);
                        break;
                    }
                    case Engine::Drums:
                        value = percussion(v);
                        v.low += (.16f+.82f*toneValue)*(value-v.low);
                        value = v.low;
                        increment = std::min(.48,step*(1.+3.5*v.transient)); secondStep = increment*1.41;
                        break;
                }
                value *= amplitude*v.envelope;
                out.addSample(0,i,value*stereo);
                if (out.getNumChannels() > 1) out.addSample(1,i,value*right);
                v.phase=wrap(v.phase+increment); v.second=wrap(v.second+secondStep);
                v.transient *= v.transientDecay;
                ++v.age;
            }
        }
        // Leave ordinary playing linear and round peaks from dense chords.
        for (int ch = 0; ch < out.getNumChannels(); ++ch) {
            auto* samples = out.getWritePointer(ch);
            for (int i = from; i < end; ++i) {
                const float peak = std::abs(samples[i]);
                if (peak > .85f) samples[i] = std::copysign(.85f + .15f*std::tanh((peak-.85f)/.15f), samples[i]);
            }
        }
    }
};

class InstrumentEditor final : public ostfstudio::StudioPluginEditor, private juce::Timer {
public:
    explicit InstrumentEditor(Instrument& instrument)
        : StudioPluginEditor(instrument), processor(instrument) {
        for (int i=0; i<processor.getNumPrograms(); ++i)
            presets.addItem("PC " + juce::String(i) + "   " + processor.getProgramName(i), i+1);
        presets.setTitle("Factory preset");
        presets.onChange = [this]{processor.chooseProgram(presets.getSelectedId()-1);};
        addAndMakeVisible(presets);
        for (size_t i=0; i<controls.size(); ++i) {
            auto& param = *static_cast<juce::RangedAudioParameter*>(processor.getParameters()[int(i)]);
            auto& control = controls[i];
            labels[i].setText(param.getName(32),juce::dontSendNotification);
            labels[i].setJustificationType(juce::Justification::centred);
            addAndMakeVisible(labels[i]);
            control.setTitle(param.getName(32));
            control.setSliderStyle(i == 0 ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearVertical);
            control.setTextBoxStyle(juce::Slider::TextBoxBelow,false,70,19);
            control.setNumDecimalPlacesToDisplay(2);
            addAndMakeVisible(control);
            attachments[i] = std::make_unique<juce::SliderParameterAttachment>(param,control);
        }
        setSize(320, 208);
        timerCallback(); startTimerHz(8);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(10);
        presets.setBounds(area.removeFromTop(30)); area.removeFromTop(6);
        for (int i=0; i<2; ++i) {
            auto strip = area.removeFromLeft(area.getWidth()/(2-i)).reduced(8,0);
            labels[size_t(i)].setBounds(strip.removeFromTop(22));
            controls[size_t(i)].setBounds(strip);
        }
    }
private:
    void timerCallback() override { presets.setSelectedId(processor.getCurrentProgram()+1,juce::dontSendNotification); }
    Instrument& processor;
    juce::ComboBox presets;
    std::array<juce::Slider,2> controls;
    std::array<juce::Label,2> labels;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>,2> attachments;
};
juce::AudioProcessorEditor* Instrument::createEditor() { return new InstrumentEditor(*this); }
}
juce::AudioProcessor* makeStudioInstrument(Engine engine, const juce::String& name) {
    return new Instrument(engine, name);
}
}
