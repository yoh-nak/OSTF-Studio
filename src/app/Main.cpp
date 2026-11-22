#include <JuceHeader.h>
#include <BinaryData.h>
#include "ui/MainComponent.h"
#include "ui/PluginRoles.h"

namespace {
int checkGmPlugin(const juce::String& plugin, const juce::String& bank, const juce::String& arrangerPath) {
    juce::AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> descriptions;
    formats.getFormat(0)->findAllTypesForFile(descriptions, juce::File(plugin).getFullPathName());
    if (descriptions.size() != 1) return 1;
    juce::String error;
    auto instance = formats.createPluginInstance(*descriptions[0], 44100, 256, error);
    if (!instance || !PreviewTransport::supportsInstrument(*instance)) return 2;
    juce::XmlElement state("OSTF_GM");
    state.setAttribute("bank", bank);
    state.setAttribute("p1", 1); // The test bank contains program 0 only.
    juce::MemoryBlock binary, component;
    juce::AudioProcessor::copyXmlToBinary(state, component);
    juce::XmlElement vst3State("VST3PluginState");
    vst3State.createNewChildElement("IComponent")->addTextElement(component.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary(vst3State, binary);
    instance->setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
    instance->prepareToPlay(44100, 256);
    const auto noteSounds = [&](int channel) {
        instance->reset();
        juce::AudioBuffer<float> audio(2, 256);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(channel, 60, 1.0f), 32);
        instance->processBlock(audio, midi);
        return audio.getMagnitude(32, 224) > 0.001f;
    };
    const auto channel1 = noteSounds(1);
    const auto channel2 = noteSounds(2);
    const auto channel3 = noteSounds(3);
    const auto drums = noteSounds(10);
    juce::XmlElement arrangerState("OSTFArranger");
    arrangerState.addChildElement(descriptions[0]->createXml().release());
    juce::MemoryBlock hostedState;
    instance->getStateInformation(hostedState);
    arrangerState.setAttribute("instrument", hostedState.toBase64Encoding());
    class PlayingHead final : public juce::AudioPlayHead {
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo position;
            position.setTimeInSamples(0);
            position.setPpqPosition(0);
            position.setIsPlaying(true);
            position.setBpm(120);
            return position;
        }
    } playhead;
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer destination;
    instance->releaseResources();
    descriptions.clear();
    formats.getFormat(0)->findAllTypesForFile(descriptions, juce::File(arrangerPath).getFullPathName());
    if (descriptions.size() != 1 || !isOstfArranger(*descriptions[0])) return 35;
    auto arranger = formats.createPluginInstance(*descriptions[0], 44100, 256, error);
    if (!arranger || !PreviewTransport::supportsInstrument(*arranger)) return 36;
    juce::AudioProcessor::copyXmlToBinary(arrangerState, component);
    vst3State.getChildByName("IComponent")->deleteAllChildElements();
    vst3State.getChildByName("IComponent")->addTextElement(component.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary(vst3State, binary);
    arranger->setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
    arranger->prepareToPlay(44100, 256);
    arranger->setPlayHead(&playhead);
    audio.clear();
    destination.clear();
    arranger->processBlock(audio, destination);
    const bool hostedSounds = audio.getMagnitude(0, 256) > 0.001f;
    arranger->releaseResources();
    arranger->setPlayHead(nullptr);
    if (!hostedSounds) return 37;
    const int failures = (!channel1 ? 1 : 0) | (channel2 ? 2 : 0) | (!channel3 ? 4 : 0) | (drums ? 8 : 0);
    return failures == 0 ? 0 : 16 + failures;
}

class StartupScreen final : public juce::Component {
public:
    StartupScreen() {
        logo_ = juce::ImageCache::getFromMemory(BinaryData::AppIcon_png, BinaryData::AppIcon_pngSize);
        addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowHasDropShadow);
        centreWithSize(460, 205);
        setVisible(true);
    }

    void setStage(juce::String stage, float progress) {
        stage_ = std::move(stage);
        progress_ = progress;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff293642), 0, 0, juce::Colour(0xff10151c),
                                              0, static_cast<float>(area.getBottom()), false));
        g.fillAll();
        g.setColour(juce::Colour(0xff52616e));
        g.drawRect(area, 1);
        g.drawImageWithin(logo_, 29, 31, 70, 70, juce::RectanglePlacement::centred);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(32.0f, juce::Font::bold));
        g.drawText("OSTF Studio", 120, 34, 300, 39, juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(13.0f));
        g.setColour(juce::Colour(0xffb1bfce));
        g.drawText("DAW Preview", 121, 75, 300, 22, juce::Justification::centredLeft);
        g.drawText(stage_, 32, 126, 396, 23, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff46545f));
        g.fillRoundedRectangle(32.0f, 162.0f, 396.0f, 4.0f, 2.0f);
        g.setColour(juce::Colour(0xff51c5b4));
        g.fillRoundedRectangle(32.0f, 162.0f, 396.0f * progress_, 4.0f, 2.0f);
    }

private:
    juce::Image logo_;
    juce::String stage_ {"Starting OSTF Studio..."};
    float progress_ = 0.2f;
};
}

class OSTFStudioApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "OSTF Studio"; }
    const juce::String getApplicationVersion() override { return "0.1.0-bootstrap"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        const auto args = getCommandLineParameterArray();
        if (args.size() == 4 && args[0] == "--gm-plugin-smoke") {
            juce::MessageManager::callAsync([this, args] {
                setApplicationReturnValue(checkGmPlugin(args[1], args[2], args[3]));
                quit();
            });
            return;
        }
        if ((args.size() == 2 || args.size() == 3) && args[0] == "--scan-vst3") {
            juce::MessageManager::callAsync([this, token = args[1], target = args[2]] {
                setApplicationReturnValue(PluginBrowser::runScanWorker(token, target));
                quit();
            });
            return;
        }
        const bool startupSmoke = !args.isEmpty() && args[0] == "--startup-smoke";
        if (const auto path = args[startupSmoke ? 1 : 0]; path.isNotEmpty())
            queueDocument(path);
        startup_ = std::make_unique<StartupScreen>();
        juce::Timer::callAfterDelay(45, [this, startupSmoke] {
            if (!startup_) return;
            startup_->setStage("Preparing timeline and demo project...", 0.65f);
            juce::Timer::callAfterDelay(45, [this, startupSmoke] {
                if (!startup_) return;
                mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
                startup_.reset();
                const bool opened = openQueuedDocument();
                if (startupSmoke) juce::Timer::callAfterDelay(500, [this, opened] {
                    setApplicationReturnValue(mainWindow_ && mainWindow_->isShowing() && opened ? 0 : 1);
                    quit();
                });
            });
        });
    }
    void anotherInstanceStarted(const juce::String& commandLine) override {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        if (!args.isEmpty()) {
            queueDocument(args[0].unquoted());
            if (mainWindow_) openQueuedDocument();
        }
    }
    void shutdown() override { startup_.reset(); mainWindow_.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    void queueDocument(const juce::String& path) {
        if (path.startsWith("-")) return;
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(path);
        if (file.hasFileExtension("ostf;ostfp")) queuedDocument_ = file;
    }

    bool openQueuedDocument() {
        if (queuedDocument_ == juce::File{}) return true;
        const auto file = std::exchange(queuedDocument_, juce::File{});
        return static_cast<MainComponent*>(mainWindow_->getContentComponent())->openDocument(file);
    }

    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff121822), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(680, 550, 8192, 8192);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
#if JUCE_WINDOWS
            if (auto* peer = getPeer()) {
                peer->setIcon(juce::ImageCache::getFromMemory(BinaryData::AppIcon_png,
                                                              BinaryData::AppIcon_pngSize));
            }
#endif
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<StartupScreen> startup_;
    juce::File queuedDocument_;
};

START_JUCE_APPLICATION(OSTFStudioApplication)
