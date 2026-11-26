#include <JuceHeader.h>
#include "plugins/studio/HostAudio.h"
#include "ui/MainComponent.h"
#include "ui/StudioLanguage.h"

namespace {
class StudioProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    StudioProcessor() : AudioProcessor(BusesProperties().withOutput("Studio", juce::AudioChannelSet::stereo(), true)) {}
    ~StudioProcessor() override {
        stopTimer();
        if (workspace) juce::MessageManager::callSync([this] {
            host.projectChanged = {};
            workspace.reset();
        });
    }
    const juce::String getName() const override { return "OSTF Studio"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainInputChannelSet().isDisabled() && b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate, int frames) override {
        requestedRate = rate;
        requestedBlock = std::max(1, frames);
        previousPlaying = false;
        expectedSample.reset();
        generation.fetch_add(1);
    }
    void releaseResources() override { playing = false; requestedRate = 0; generation.fetch_add(1); }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;
        auto* head = getPlayHead();
        const auto pos = head ? head->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
        if (pos) {
            auto samples = pos->getTimeInSamples();
            const auto rate = requestedRate.load();
            if (!samples && rate > 0)
                if (const auto time = pos->getTimeInSeconds()) samples = std::llround(*time * rate);
            const auto active = pos->getIsPlaying();
            const bool jumped = active && samples && expectedSample && std::abs(*samples - *expectedSample) > 1;
            if (samples) {
                seconds = double(*samples) / std::max(1., rate);
                expectedSample = *samples + audio.getNumSamples();
            } else {
                expectedSample.reset();
            }
            hasPosition = true;
            playing = active;
            if (active != previousPlaying || jumped) generation.fetch_add(1);
            previousPlaying = active;
        } else {
            hasPosition = false;
            previousPlaying = false;
            expectedSample.reset();
        }
        host.render(audio, midi, requestedRate.load());
        midi.clear();
    }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override {
        class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
        public:
            explicit Editor(StudioProcessor& p) : AudioProcessorEditor(p), owner(p), studio(p.studio()) {
                follow.setButtonText(TRANS("Follow DAW transport"));
                follow.setToggleState(p.follow.load(), juce::dontSendNotification);
                follow.onClick = [this] {
                    owner.follow = follow.getToggleState();
                    owner.generation.fetch_add(1);
                    owner.notifyProjectChange();
                };
                follow.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
                addAndMakeVisible(follow);
                addAndMakeVisible(studio);
                setResizable(true, true);
                setResizeLimits(760, 520, 3840, 2400);
                setSize(1140, 720);
                startTimerHz(5);
            }
            void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff202b31)); }
            void resized() override {
                auto r = getLocalBounds();
                follow.setBounds(r.removeFromTop(26).removeFromLeft(220));
                studio.setBounds(r);
            }
        private:
            void timerCallback() override { follow.setToggleState(owner.follow.load(), juce::dontSendNotification); }
            StudioProcessor& owner;
            MainComponent& studio;
            juce::ToggleButton follow;
        };
        return new Editor(*this);
    }
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MessageManager::callSync([&] {
            juce::MemoryBlock package;
            if (studio().saveHostProject(package)) {
                juce::MemoryOutputStream out(dest, false);
                out.writeInt(juce::ByteOrder::littleEndianInt("OST1"));
                out.writeBool(follow.load());
                out.write(package.getData(), package.getSize());
            }
        });
    }
    void setStateInformation(const void* data, int size) override {
        if (size < 4 || !data) return;
        juce::MemoryInputStream in(data, size_t(size), false);
        const bool stamped = in.readInt() == (int)juce::ByteOrder::littleEndianInt("OST1");
        const bool enabled = stamped ? in.readBool() : true;
        const auto offset = stamped ? size_t(in.getPosition()) : 0u;
        juce::MessageManager::callSync([&] {
            const juce::ScopedValueSetter<bool> restoring(recalling, true);
            if (studio().restoreHostProject(static_cast<const char*>(data) + offset, size_t(size) - offset)) {
                follow = enabled;
                generation.fetch_add(1);
            }
        });
    }
private:
    void notifyProjectChange() {
        if (!recalling) updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    }
    MainComponent& studio() {
        if (!workspace) {
            ostfstudio::loadStudioLanguage();
            workspace = std::make_unique<MainComponent>(&host);
            host.projectChanged = [this] { notifyProjectChange(); };
            startTimerHz(40);
        }
        configureAudio();
        return *workspace;
    }
    void configureAudio() {
        const auto rate = requestedRate.load();
        if (rate > 0) workspace->setHostAudioFormat(rate, requestedBlock.load());
    }
    void timerCallback() override {
        configureAudio();
        const auto request = generation.load();
        if (request == applied || !follow.load() || (!hasPosition && requestedRate > 0)) return;
        if (workspace->followHostTransport(playing.load(), seconds.load())) applied = request;
    }
    ostfstudio::HostAudio host;
    std::unique_ptr<MainComponent> workspace;
    std::atomic<double> requestedRate{0}, seconds{0};
    std::atomic<int> requestedBlock{512};
    std::atomic<bool> follow{true}, playing{false}, hasPosition{false};
    std::atomic<unsigned> generation{1};
    unsigned applied = 0;
    bool previousPlaying = false, recalling = false;
    std::optional<juce::int64> expectedSample;
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StudioProcessor(); }
