#include <JuceHeader.h>
#include <BinaryData.h>
#include "ui/MainComponent.h"
#include "ui/StudioLanguage.h"
#include "ui/PluginRoles.h"
#include <cmath>

int checkSharedPartEditing(std::unique_ptr<juce::AudioPluginInstance>, std::unique_ptr<juce::AudioPluginInstance>);

namespace {
int checkGmPlugin(const juce::String& plugin, const juce::String& bank, const juce::String& arrangerPath) {
    juce::AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> descriptions;
    formats.getFormat(0)->findAllTypesForFile(descriptions, juce::File(plugin).getFullPathName());
    if (descriptions.size() != 1) return 1;
    juce::MemoryInputStream demo(BinaryData::neonplatforms_ostfp, BinaryData::neonplatforms_ostfpSize, false);
    std::unique_ptr<ostfstudio::OstfpPreview> score;
    if (ostfstudio::readOstfpPreview(demo, score).failed() || !score->savedMix || score->savedMix->instruments.empty()
        || !matchesVst3Instrument(score->savedMix->instruments.front(), *descriptions[0])) return 39;
    juce::String error;
    auto instance = formats.createPluginInstance(*descriptions[0], 44100, 256, error);
    if (!instance || !PreviewTransport::supportsInstrument(*instance)) return 2;
    const auto fluid = juce::File(plugin).getChildFile("Contents/Resources/FluidR3_GM.sf2");
    auto second = formats.createPluginInstance(*descriptions[0], 44100, 256, error);
    if (!second) return 2;
    if (const auto result = checkSharedPartEditing(std::move(instance), std::move(second))) return result;
    instance = formats.createPluginInstance(*descriptions[0], 44100, 256, error);
    if (!instance) return 2;
    if (fluid.existsAsFile()) {
        juce::MemoryBlock state, component;
        instance->getStateInformation(state);
        const auto wrapper = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
        const auto* child = wrapper ? wrapper->getChildByName("IComponent") : nullptr;
        if (!child || !component.fromBase64Encoding(child->getAllSubText())) return 40;
        const auto xml = juce::AudioProcessor::getXmlFromBinary(component.getData(), static_cast<int>(component.getSize()));
        if (!xml || xml->getStringAttribute("bankName") != "FluidR3 GM") return 41;
    }
    {
        const auto pitched = std::find(score->trackKinds.begin(), score->trackKinds.end(), ostfstudio::TrackKind::pitched);
        if (pitched == score->trackKinds.end()) return 51;
        const auto track = size_t(pitched - score->trackKinds.begin());
        std::vector<PreviewTransport::TrackSetup> routes(score->trackKinds.size());
        routes[track].instrument = instance.get();
        PreviewTransport monitor;
        if (monitor.preparePlayback(*score, routes, {}, 44100, 512, 0, 1, true).failed()) return 52;
        monitor.prepareRecording(*score, routes, track, false);
        monitor.setChannelMix(track, {}, true, 0);
        monitor.setOutputMix({});
        juce::AudioBuffer<float> block(2,512);
        monitor.renderBlock(block.getArrayOfWritePointers(),2,512);
        if (block.getMagnitude(0,512) > .0001f) return 53;
        monitor.inputFromDevice(juce::MidiMessage::noteOn(1,60,juce::uint8(118)), "test-keys");
        monitor.renderBlock(block.getArrayOfWritePointers(),2,512);
        if (block.getMagnitude(0,512) < .0001f || monitor.takeMeters().tracks[track][0] < .0001f || monitor.positionTick() != 0) return 54;
        monitor.inputFromDevice(juce::MidiMessage::noteOff(1,60), "test-keys");
        monitor.renderBlock(block.getArrayOfWritePointers(),2,512);
        if (!monitor.stop().empty()) return 55;
    }
    instance->prepareToPlay(44100, 2048);
    const auto renderPreview = [&](int channel, int program, int pitch) {
        instance->reset();
        juce::AudioBuffer<float> audio(2, 2048);
        audio.clear();
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::programChange(channel, program), 0);
        events.addEvent(juce::MidiMessage::noteOn(channel, pitch, 0.9f), 1);
        instance->processBlock(audio, events);
        return audio;
    };
    const auto piano = renderPreview(1, 0, 60);
    const auto bass = renderPreview(1, 33, 60);
    const auto kick = renderPreview(10, 0, 36);
    const auto snare = renderPreview(10, 0, 38);
    instance->releaseResources();
    auto differs = [](const auto& a, const auto& b) {
        double squaredDifference = 0.0;
        for (int sample = 0; sample < a.getNumSamples(); ++sample) {
            const auto difference = a.getSample(0, sample) - b.getSample(0, sample);
            squaredDifference += difference * difference;
        }
        return std::sqrt(squaredDifference / a.getNumSamples()) > 0.0005;
    };
    if (piano.getMagnitude(0, 2048) < 0.003f || bass.getMagnitude(0, 2048) < 0.003f || !differs(piano, bass)
        || kick.getMagnitude(0, 2048) < 0.003f || snare.getMagnitude(0, 2048) < 0.003f || !differs(kick, snare))
        return 38;
    // Verify bank latching and the optional second percussion channel through the VST3 host.
    instance->prepareToPlay(44100, 2048);
    const auto bankEvents = [&](int channel, int msb, int lsb) {
        juce::AudioBuffer<float> empty(2, 16);
        juce::MidiBuffer messages;
        messages.addEvent(juce::MidiMessage::controllerEvent(channel, 0, msb), 0);
        messages.addEvent(juce::MidiMessage::controllerEvent(channel, 32, lsb), 1);
        instance->processBlock(empty, messages);
    };
    bankEvents(11, 120, 0);
    const auto secondKick = renderPreview(11, 0, 36);
    if (secondKick.getMagnitude(0, 2048) < .003f || differs(secondKick, kick)) return 56;
    bankEvents(10, 121, 0);
    if (differs(renderPreview(10, 0, 60), piano)) return 57;
    if (fluid.existsAsFile()) {
        const auto electric = renderPreview(2, 4, 60);
        bankEvents(2, 121, 1);
        if (!differs(electric, renderPreview(2, 4, 60))) return 58;
    }
    instance->releaseResources();
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
    auto* part = state.createNewChildElement("part");
    part->setAttribute("channel", 0);
    part->setAttribute("c6", 1);
    auto applyState = [&](const juce::XmlElement& settings) {
        juce::AudioProcessor::copyXmlToBinary(settings, component);
        vst3State.getChildByName("IComponent")->deleteAllChildElements();
        vst3State.getChildByName("IComponent")->addTextElement(component.toBase64Encoding());
        juce::AudioProcessor::copyXmlToBinary(vst3State, binary);
        instance->setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
    };
    applyState(state);
    if (noteSounds(1) || !noteSounds(3)) return 42;
    part->setAttribute("c6", 0);
    applyState(state);
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
        g.drawText(OSTF_STUDIO_DISPLAY_VERSION, 121, 75, 300, 22, juce::Justification::centredLeft);
        g.drawText(stage_, 32, 126, 396, 23, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff46545f));
        g.fillRoundedRectangle(32.0f, 162.0f, 396.0f, 4.0f, 2.0f);
        g.setColour(juce::Colour(0xff51c5b4));
        g.fillRoundedRectangle(32.0f, 162.0f, 396.0f * progress_, 4.0f, 2.0f);
    }

private:
    juce::Image logo_;
    juce::String stage_ {TRANS("Starting OSTF Studio...")};
    float progress_ = 0.2f;
};
}

int checkStockEffects(const juce::StringArray&);
int checkStudioInstruments(const juce::StringArray&);
int checkTrackView(const juce::File&);
std::unique_ptr<juce::Timer> startSpliceWindowCheck(std::function<void(int)>);
std::unique_ptr<juce::Timer> startPluginFocusCheck(const juce::File&, std::function<void(int)>);
int checkPianoEdits(const juce::File&);
int checkTheoryGuide(const juce::File&);
int checkNotation(const juce::File&, const juce::File&);
int checkTransport(const juce::File&, const juce::File&);

class OSTFStudioApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "OSTF Studio"; }
    const juce::String getApplicationVersion() override { return OSTF_STUDIO_DISPLAY_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        const auto args = getCommandLineParameterArray();
        const bool spliceCheck = args.size() == 1 && (args[0] == "--splice-window-smoke" || args[0] == "--splice-pane-smoke");
        if (spliceCheck || (args.size() == 2 && args[0] == "--plugin-focus-smoke")) {
            auto finished = [this](int result) {
                setApplicationReturnValue(result);
                quit();
            };
            windowCheck_ = spliceCheck ? startSpliceWindowCheck(finished) : startPluginFocusCheck(juce::File(args[1]), finished);
            return;
        }
#if JUCE_WINDOWS && defined(_MSC_VER)
        if (args.size() == 4 && args[0] == "--render-score-page") {
            ostfstudio::runWindowsScorePageRenderer(juce::File(args[1]), juce::File(args[2]), args[3].getIntValue());
            quit();
            return;
        }
#endif
        if (args.size() == 3 && args[0] == "--transport-smoke") {
            juce::MessageManager::callAsync([this, args] {
                setApplicationReturnValue(checkTransport(juce::File(args[1]), juce::File(args[2])));
                quit();
            });
            return;
        }
        if (args.size()==3 && args[0]=="--notation-smoke") {
            juce::MessageManager::callAsync([this,args]{
                setApplicationReturnValue(checkNotation(juce::File(args[1]),juce::File(args[2])));
                quit();
            });
            return;
        }
        if (args.size() == 2 && (args[0] == "--track-view-smoke" || args[0] == "--piano-edit-smoke" || args[0] == "--theory-guide-smoke")) {
            juce::MessageManager::callAsync([this, args] {
                const juce::File fixture(args[1]);
                if (args[0] == "--theory-guide-smoke") setApplicationReturnValue(checkTheoryGuide(fixture));
                else setApplicationReturnValue(args[0] == "--track-view-smoke" ? checkTrackView(fixture) : checkPianoEdits(fixture));
                quit();
            });
            return;
        }
        if (args.size() == 3 && args[0] == "--studio-instruments-smoke") {
            juce::MessageManager::callAsync([this,args] {
                setApplicationReturnValue(checkStudioInstruments(args));
                quit();
            });
            return;
        }
        if (args.size() == 4 && args[0] == "--stock-effects-smoke") {
            juce::MessageManager::callAsync([this, args] {
                setApplicationReturnValue(checkStockEffects(args));
                quit();
            });
            return;
        }
        if (args.size() == 1 && args[0] == "--asio-backend-smoke") {
            std::unique_ptr<juce::AudioIODeviceType> backend(juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
            setApplicationReturnValue(backend && backend->getTypeName() == "ASIO" ? 0 : 1);
            quit();
            return;
        }
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
        ostfstudio::loadStudioLanguage();
        const bool startupSmoke = !args.isEmpty() && args[0] == "--startup-smoke";
        if (const auto path = args[startupSmoke ? 1 : 0]; path.isNotEmpty())
            queueDocument(path);
        startup_ = std::make_unique<StartupScreen>();
        juce::Timer::callAfterDelay(45, [this, startupSmoke] {
            if (!startup_) return;
            startup_->setStage(TRANS("Preparing workspace..."), 0.65f);
            juce::Timer::callAfterDelay(45, [this, startupSmoke] {
                if (!startup_) return;
                mainWindow_ = createStudioWindow();
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
    void shutdown() override {
        windowCheck_.reset();
        startup_.reset();
        mainWindow_.reset();
    }
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
        std::function<void()> close;
        void closeButtonPressed() override {
            if (close)
                close();
        }
    };

    std::unique_ptr<MainWindow> createStudioWindow() {
        auto window = std::make_unique<MainWindow>(getApplicationName());
        window->close = [this] { systemRequestedQuit(); };
        return window;
    }
    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<StartupScreen> startup_;
    std::unique_ptr<juce::Timer> windowCheck_;
    juce::File queuedDocument_;
};

START_JUCE_APPLICATION(OSTFStudioApplication)
