#include <juce_audio_processors/juce_audio_processors.h>
#include "ui/MidiDragButton.h"
#include "ui/PluginLibrary.h"
#include "ui/PluginRoles.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr auto& pattern = ostfstudio::generatorPitches;
constexpr auto velocity = static_cast<juce::uint8>(ostfstudio::generatorVelocity);

bool canHost(const juce::PluginDescription& description) {
    return description.pluginFormatName == "VST3" && description.isInstrument
        && !isOstfArranger(description) && !isOstfGenerator(description);
}

class PatternEditor final : public juce::AudioProcessorEditor {
public:
    PatternEditor(juce::AudioProcessor& processor, std::function<juce::String(juce::File)> load,
                  std::function<juce::String(const juce::PluginDescription&)> select,
                  std::function<juce::String()> currentName, std::function<void()> edit)
        : AudioProcessorEditor(processor), load_(std::move(load)), select_(std::move(select)),
          currentName_(std::move(currentName)) {
        addAndMakeVisible(drag_);
        addAndMakeVisible(status_);
        addAndMakeVisible(patternLabel_);
        patternLabel_.setText("Pattern: C - E - G - E", juce::dontSendNotification);
        drag_.setButtonText("Drag MIDI clip to a DAW track");
        drag_.setTooltip("Drop the pattern onto a MIDI or instrument track in another DAW.");
        drag_.writeMidi = [](juce::OutputStream& stream) {
            constexpr int ticksPerQuarter = 480;
            juce::MidiMessageSequence notes;
            for (int i = 0; i < static_cast<int>(std::size(pattern)); ++i) {
                notes.addEvent(juce::MidiMessage::noteOn(1, pattern[i], velocity), i * ticksPerQuarter);
                notes.addEvent(juce::MidiMessage::noteOff(1, pattern[i]), i * ticksPerQuarter + ticksPerQuarter / 2);
            }
            notes.addEvent(juce::MidiMessage::endOfTrack(), std::size(pattern) * ticksPerQuarter);
            juce::MidiFile midi;
            midi.setTicksPerQuarterNote(ticksPerQuarter);
            midi.addTrack(notes);
            return midi.writeTo(stream, 0);
        };
        drag_.reportError = [this](const juce::String& error) { status_.setText(error, juce::dontSendNotification); };
        addAndMakeVisible(instrument_);
        addAndMakeVisible(edit_);
        edit_.onClick = std::move(edit);
        instrument_.onClick = [this] { chooseInstrument(); };
        refreshInstrument();
        setSize(410, 164);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16);
        patternLabel_.setBounds(area.removeFromTop(22));
        auto row = area.removeFromTop(35);
        edit_.setBounds(row.removeFromRight(118).reduced(2));
        instrument_.setBounds(row.reduced(2));
        status_.setBounds(area.removeFromTop(32));
        drag_.setBounds(area);
    }

private:
    void refreshInstrument() {
        const auto name = currentName_();
        instrument_.setButtonText(name.isEmpty() ? "Choose instrument..." : name + "  v");
        instrument_.setTooltip(name.isEmpty() ? "Choose a scanned VST3 instrument or browse for one." : "Replace " + name);
        edit_.setEnabled(name.isNotEmpty());
    }

    void browse() {
        chooser_ = std::make_unique<juce::FileChooser>("Choose a VST3 instrument", juce::File{}, "*.vst3", useNativeVst3Chooser);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<PatternEditor>(this)](const juce::FileChooser& picker) {
                if (!safe || picker.getResult() == juce::File{}) return;
                safe->status_.setText(safe->load_(picker.getResult()), juce::dontSendNotification);
                safe->refreshInstrument();
            });
    }

    void chooseInstrument() {
        juce::KnownPluginList library;
        if (const auto xml = juce::XmlDocument::parse(pluginDataDirectory().getChildFile("vst3-cache.xml")))
            library.recreateFromXml(*xml);
        registerBundledVst3(library, "OSTF GM Sound Module.vst3");
        auto types = library.getTypes();
        types.removeIf([](const auto& description) { return !canHost(description); });
        const auto manufacturer = [](const juce::PluginDescription& description) {
            return description.manufacturerName.isEmpty() ? juce::String("Unknown manufacturer") : description.manufacturerName;
        };
        std::stable_sort(types.begin(), types.end(), [&](const auto& a, const auto& b) {
            const auto comparison = manufacturer(a).compareNatural(manufacturer(b));
            return comparison == 0 ? a.name.compareNatural(b.name) < 0 : comparison < 0;
        });
        juce::PopupMenu menu;
        if (types.isEmpty()) menu.addItem(2, "Scan VST3 in OSTF Studio to populate this list", false);
        for (int i = 0; i < types.size();) {
            const auto maker = manufacturer(types.getReference(i));
            juce::PopupMenu group;
            do {
                group.addItem(i + 3, types.getReference(i).name);
                ++i;
            } while (i < types.size() && manufacturer(types.getReference(i)) == maker);
            menu.addSubMenu(maker, group);
        }
        menu.addSeparator();
        menu.addItem(1, "Browse for VST3 file...");
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&instrument_),
            [safe = juce::Component::SafePointer<PatternEditor>(this), types = std::move(types)](int action) {
                if (!safe) return;
                if (action == 1) safe->browse();
                if (action < 3 || action - 3 >= types.size()) return;
                safe->status_.setText(safe->select_(types.getReference(action - 3)), juce::dontSendNotification);
                safe->refreshInstrument();
            });
    }

    juce::Label status_, patternLabel_;
    MidiDragButton drag_;
    juce::TextButton instrument_, edit_ {"Edit instrument"};
    std::function<juce::String(juce::File)> load_;
    std::function<juce::String(const juce::PluginDescription&)> select_;
    std::function<juce::String()> currentName_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

class PatternProcessor : public juce::AudioProcessor {
public:
    PatternProcessor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& state) override { state.reset(); }
    void setStateInformation(const void*, int) override {}
    void releaseResources() override {}

    void prepareToPlay(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        nextPosition_.reset();
        soundingNote_ = -1;
    }

    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        audio.clear();
        midi.clear();
        if (audio.getNumSamples() == 0) return;
        const auto* playHead = getPlayHead();
        const auto position = playHead ? playHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
        const auto ppq = position ? position->getPpqPosition() : juce::Optional<double>{};
        const auto bpm = position ? position->getBpm() : juce::Optional<double>{};
        if (!position || !position->getIsPlaying() || !ppq || !bpm || !std::isfinite(*ppq)
            || !std::isfinite(*bpm) || *bpm <= 0.0 || sampleRate_ <= 0.0) {
            stopNote(midi, 0);
            nextPosition_.reset();
            return;
        }

        const double step = *bpm / (60.0 * sampleRate_);
        const bool continuous = nextPosition_ && std::abs(*nextPosition_ - *ppq) <= step * 2.0;
        if (!continuous) stopNote(midi, 0);
        const double start = continuous ? *nextPosition_ : *ppq;
        const double end = start + step * audio.getNumSamples();
        nextPosition_ = end;

        // The preview plays C4, E4, G4 and E4 on successive quarter notes.
        for (auto halfBeat = static_cast<long long>(std::ceil(start * 2.0));
             static_cast<double>(halfBeat) * 0.5 < end; ++halfBeat) {
            const auto offset = static_cast<int>((static_cast<double>(halfBeat) * 0.5 - start) / step);
            const auto sample = juce::jlimit(0, audio.getNumSamples() - 1, offset);
            stopNote(midi, sample);
            if (halfBeat % 2 == 0) {
                soundingNote_ = ostfstudio::generatorPitchAtBeat(halfBeat / 2);
                midi.addEvent(juce::MidiMessage::noteOn(1, soundingNote_, velocity), sample);
            }
        }
    }

private:
    void stopNote(juce::MidiBuffer& midi, int sample) {
        if (soundingNote_ < 0) return;
        midi.addEvent(juce::MidiMessage::noteOff(1, soundingNote_), sample);
        soundingNote_ = -1;
    }

    double sampleRate_ = 0.0;
    std::optional<double> nextPosition_;
    int soundingNote_ = -1;
};

class Arranger final : public PatternProcessor, private juce::AsyncUpdater {
public:
    Arranger() { formats_.addFormat(std::make_unique<juce::VST3PluginFormat>()); }
    ~Arranger() override { cancelPendingUpdate(); }
    const juce::String getName() const override { return "OSTF Arranger"; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    juce::AudioProcessorEditor* createEditor() override {
        return new PatternEditor(*this, [this](juce::File file) { return load(file, {}, {}); },
            [this](const juce::PluginDescription& description) { return loadInstrument(description, {}); },
            [this] {
                const juce::ScopedLock lock(lock_);
                return instrument_ ? instrument_->getName() : juce::String{};
            },
            [this] { openEditor(); });
    }

    void prepareToPlay(double rate, int block) override {
        PatternProcessor::prepareToPlay(rate, block);
        const juce::ScopedLock lock(lock_);
        rate_ = rate;
        block_ = block;
        prepared_ = true;
        if (instrument_) instrument_->prepareToPlay(rate_, block_);
    }
    void releaseResources() override {
        const juce::ScopedLock lock(lock_);
        if (instrument_ && prepared_) instrument_->releaseResources();
        prepared_ = false;
    }
    void reset() override {
        const juce::ScopedLock lock(lock_);
        if (instrument_) instrument_->reset();
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        PatternProcessor::processBlock(audio, midi);
        const juce::ScopedTryLock lock(lock_);
        if (lock.isLocked() && instrument_) {
            instrument_->setPlayHead(getPlayHead());
            instrument_->processBlock(audio, midi);
            if (instrument_->getMainBusNumOutputChannels() == 1 && audio.getNumChannels() == 2)
                audio.copyFrom(1, 0, audio, 0, 0, audio.getNumSamples());
        }
        midi.clear();
    }

    void getStateInformation(juce::MemoryBlock& data) override {
        juce::XmlElement state("OSTFArranger");
        const juce::ScopedLock lock(lock_);
        if (instrument_) {
            state.addChildElement(instrument_->getPluginDescription().createXml().release());
            juce::MemoryBlock contents;
            instrument_->getStateInformation(contents);
            state.setAttribute("instrument", contents.toBase64Encoding());
            const auto current = instrument_->getCurrentProgram();
            const auto program = current >= 0 && current < instrument_->getNumPrograms()
                ? instrument_->getProgramName(current) : juce::String{};
            state.setAttribute("displayPreset", instrument_->getName() + " / "
                + (program.isNotEmpty() ? program : "Preset not reported"));
        }
        copyXmlToBinary(state, data);
    }
    void setStateInformation(const void* data, int size) override {
        const auto state = getXmlFromBinary(data, size);
        if (!state || !state->hasTagName("OSTFArranger")) return;
        {
            const juce::ScopedLock lock(pendingLock_);
            pending_ = std::make_unique<juce::XmlElement>(*state);
        }
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) handleAsyncUpdate();
        else triggerAsyncUpdate();
    }

private:
    class InstrumentWindow final : public juce::DocumentWindow {
    public:
        explicit InstrumentWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colours::darkgrey, closeButton) { setUsingNativeTitleBar(true); }
        void closeButtonPressed() override { setVisible(false); }
    };

    void handleAsyncUpdate() override {
        std::unique_ptr<juce::XmlElement> state;
        {
            const juce::ScopedLock lock(pendingLock_);
            state = std::move(pending_);
        }
        if (!state) return;
        if (const auto* element = state->getChildByName("PLUGIN")) {
            juce::PluginDescription description;
            if (!description.loadFromXml(*element)) return;
            juce::MemoryBlock contents;
            contents.fromBase64Encoding(state->getStringAttribute("instrument"));
            load(juce::File(description.fileOrIdentifier), description.createIdentifierString(), contents);
        } else {
            const juce::ScopedLock lock(lock_);
            window_.reset();
            if (instrument_ && prepared_) instrument_->releaseResources();
            instrument_.reset();
        }
    }

    juce::String load(const juce::File& file, const juce::String& identifier, const juce::MemoryBlock& state) {
        if (!file.exists() || !file.hasFileExtension("vst3") || file.getFileNameWithoutExtension() == "OSTF Arranger")
            return "Choose a VST3 instrument bundle";
        juce::OwnedArray<juce::PluginDescription> choices;
        formats_.getFormat(0)->findAllTypesForFile(choices, file.getFullPathName());
        for (const auto* description : choices) {
            if (!canHost(*description) || (identifier.isNotEmpty() && description->createIdentifierString() != identifier))
                continue;
            const auto message = loadInstrument(*description, state);
            if (message.startsWith("Instrument: ")) return message;
        }
        return "Could not load a compatible instrument";
    }

    juce::String loadInstrument(const juce::PluginDescription& description, const juce::MemoryBlock& state) {
        const juce::File bundle(description.fileOrIdentifier);
        if (!canHost(description) || !bundle.exists())
            return "Instrument unavailable. Rescan in OSTF Studio or browse for the VST3.";
        double rate;
        int block;
        {
            const juce::ScopedLock lock(lock_);
            rate = rate_;
            block = block_;
        }
        juce::String error;
        auto next = formats_.createPluginInstance(description, rate, block, error);
        if (!next) return "Could not load instrument: " + error;
        next->disableNonMainBuses();
        if (!next->acceptsMidi() || next->getTotalNumInputChannels() != 0
            || next->getMainBusNumOutputChannels() < 1 || next->getTotalNumOutputChannels() > 2)
            return "Choose a MIDI instrument with a mono or stereo output.";
        if (state.getSize() > 0) next->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const juce::ScopedLock lock(lock_);
        if (prepared_) next->prepareToPlay(rate_, block_);
        window_.reset();
        if (instrument_ && prepared_) instrument_->releaseResources();
        instrument_ = std::move(next);
        return "Instrument: " + instrument_->getName();
    }

    void openEditor() {
        const juce::ScopedLock lock(lock_);
        if (!instrument_) return;
        if (!window_) {
            auto* editor = instrument_->createEditorAndMakeActive();
            if (!editor) editor = new juce::GenericAudioProcessorEditor(*instrument_);
            window_ = std::make_unique<InstrumentWindow>(instrument_->getName());
            window_->setContentOwned(editor, true);
            window_->centreWithSize(window_->getWidth(), window_->getHeight());
        }
        window_->setVisible(true);
        window_->toFront(true);
    }

    juce::AudioPluginFormatManager formats_;
    juce::CriticalSection lock_, pendingLock_;
    std::unique_ptr<juce::AudioPluginInstance> instrument_;
    std::unique_ptr<InstrumentWindow> window_;
    std::unique_ptr<juce::XmlElement> pending_;
    double rate_ = 44100.0;
    int block_ = 512;
    bool prepared_ = false;
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Arranger();
}
