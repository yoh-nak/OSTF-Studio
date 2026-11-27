#include "EffectControlPanel.h"
#include <array>
#include <cmath>

namespace {
class Unmask final : public juce::AudioProcessor {
public:
    ostfstudio::PluginCcMap ccMap{*this};
    Unmask() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        auto add = [this](const char* id, const char* name, float lo, float hi, float value) {
            auto* p = new juce::AudioParameterFloat(juce::ParameterID{id,1},name,
                juce::NormalisableRange<float>(lo,hi,.1f),value);
            addParameter(p); controls.push_back(p); return p;
        };
        depth = add("depth","Max cut (dB)",0,24,9);
        threshold = add("threshold","Key threshold (dB)",-72,0,-36);
        attack = add("attack","Attack (ms)",1,100,8);
        release = add("release","Release (ms)",15,1500,150);
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
    double getTailLengthSeconds() const override { return 0; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        const auto main=layout.getMainInputChannelSet();
        if (layout.inputBuses.size()!=2 || layout.outputBuses.size()!=1) return false;
        const auto key=layout.inputBuses[1];
        return (main==juce::AudioChannelSet::mono() || main==juce::AudioChannelSet::stereo())
            && layout.getMainOutputChannelSet()==main
            && (key.isDisabled() || key==juce::AudioChannelSet::mono() || key==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double sampleRate,int) override {
        rate=std::max(1.,sampleRate);
        constexpr std::array<double,4> hz{180,650,2100,6500};
        for(size_t i=0;i<hz.size();++i) coefficients[i]=float(-std::expm1(-2*juce::MathConstants<double>::pi*std::min(.43*rate,hz[i])/rate));
        reset();
    }
    void reset() override { filters={}; envelope={}; }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi) override {
        ccMap.process(midi); midi.clear();
        juce::ScopedNoDenormals noDenormals;
        const auto main=getMainBusNumOutputChannels(), keys=getChannelCountOfBus(true,1);
        if (!keys || buffer.getNumChannels()<main+keys) {reset(); return;}
        const float a=float(-std::expm1(-1000./(rate*attack->get())));
        const float r=float(-std::expm1(-1000./(rate*release->get())));
        const float onset=juce::Decibels::decibelsToGain(threshold->get());
        const float maxReduction=1.f-juce::Decibels::decibelsToGain(-depth->get());
        for(int n=0;n<buffer.getNumSamples();++n) {
            std::array<float,5> key{};
            for(int ch=0;ch<keys;++ch) {
                const auto bands=split(buffer.getSample(main+ch,n),filters[2+ch]);
                for(size_t k=0;k<key.size();++k) key[k]=std::max(key[k],std::abs(bands[k]));
            }
            std::array<float,5> reduction{};
            for(size_t k=0;k<key.size();++k) {
                auto& env=envelope[k];
                env+=(key[k]-env)*(key[k]>env?a:r);
                reduction[k]=env>onset ? maxReduction*(1.f-onset/env) : 0.f;
            }
            for(int ch=0;ch<main;++ch) {
                float value=buffer.getSample(ch,n);
                const auto bands=split(value,filters[ch]);
                for(size_t k=0;k<bands.size();++k) value-=reduction[k]*bands[k];
                buffer.setSample(ch,n,value);
            }
        }
    }
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::XmlElement state("Unmask");
        for (const auto* p : controls) state.setAttribute(p->paramID, p->get());
        ccMap.save(state);
        copyXmlToBinary(state, dest);
    }
    void setStateInformation(const void* data, int size) override {
        auto state = getXmlFromBinary(data, size);
        if (!state || !state->hasTagName("Unmask")) return;
        ccMap.restore(*state);
        for (auto* p : controls) {
            const float value = static_cast<float>(state->getDoubleAttribute(p->paramID, p->get()));
            if (std::isfinite(value)) p->setValueNotifyingHost(p->convertTo0to1(value));
        }
    }
private:
    // Adjacent low-pass differences plus the high residual sum to the original sample.
    std::array<float,5> split(float input,std::array<float,4>& poles) const {
        std::array<float,5> out{};
        float previous=0;
        for(size_t i=0;i<poles.size();++i) {
            poles[i]+=coefficients[i]*(input-poles[i]);
            out[i]=poles[i]-previous; previous=poles[i];
        }
        out.back()=input-previous;
        return out;
    }
    std::vector<juce::AudioParameterFloat*> controls;
    juce::AudioParameterFloat *depth{}, *threshold{}, *attack{}, *release{};
    double rate=48000;
    std::array<float,4> coefficients{};
    std::array<std::array<float,4>,4> filters{};
    std::array<float,5> envelope{};
};
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Unmask; }
