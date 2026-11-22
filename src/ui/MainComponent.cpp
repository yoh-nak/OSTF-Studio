#include "ui/MainComponent.h"
#include "ui/DemoArrangement.h"
#include "core/MidiGeneratorPattern.h"
#include "ui/PluginRoles.h"
#include <BinaryData.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace {
constexpr auto bg = 0xff15181d;
constexpr auto panel = 0xff242a31;
constexpr auto muted = 0xffaab6c4;
constexpr auto accent = 0xff46bdb2;
constexpr auto hardwareAmber = 0xffe7b46c;
constexpr int menuHeight = 28;
constexpr int trackHeaderWidth = 410;
constexpr int trackNameWidth = 95;
constexpr int arrangementHeadingHeight = 24;

juce::String outputName(std::size_t index) {
    if (index == 0) return "Master";
    if (index == 1) return "Bus A";
    if (index == 2) return "Bus B";
    return "Return " + juce::String(static_cast<int>(index - 2));
}

juce::String effectId(const juce::PluginDescription& description) {
    return description.pluginFormatName + "|" + description.manufacturerName + "|" + description.name
        + "|" + juce::String(description.uniqueId);
}
constexpr auto generatorDragId = "ostf-studio:generator-pattern";

int arrangementLaneHeight(juce::Rectangle<int> inner, const ostfstudio::ArrangementView& view, std::size_t outputs) {
    const auto rows = 1 + static_cast<int>(view.globalLanes.size() + view.trackLanes.size() + outputs);
    return juce::jmin(42, (inner.getHeight() - arrangementHeadingHeight * (outputs ? 2 : 1)) / rows);
}

juce::Rectangle<int> workspaceBounds(juce::Rectangle<int> bounds) {
    bounds.reduce(18, 18);
    bounds.removeFromTop(54 + menuHeight);
    bounds.removeFromBottom(26);
    return bounds;
}

enum MenuCommand { openFile = 1, openDemoSong, showPiano, showMixer, showDevices, showAbout, showPlugins,
                   saveScore, saveProject, showOutputs, addPitched, addDrums, addAudio, removeTrack };
constexpr int timelineMenuBase = 100;
constexpr int demoMenuBase = 300;
constexpr struct { const char* title; const char* resource; } gallery[] {
    {"Neon Platforms (City Pop)", "neonplatforms_ostfp"},
    {"Blue Hour Waltz (Jazz Waltz)", "bluehourwaltz_ostfp"},
    {"Relay Circuit (Techno)", "relaycircuit_ostfp"},
    {"Paper Lantern Replies (R&B)", "paperlanternreplies_ostfp"},
};
using ostfstudio::TimelineLane;
constexpr TimelineLane selectableTimelineLanes[] {TimelineLane::section, TimelineLane::chord, TimelineLane::voicing,
    TimelineLane::key, TimelineLane::scale, TimelineLane::meter, TimelineLane::tempo};
constexpr const char* timelineMenuLabels[] {"Sections", "Chords", "Chord Voicings", "Keys", "Scales", "Meter", "Tempo"};
static_assert(std::size(selectableTimelineLanes) == std::size(timelineMenuLabels));

constexpr unsigned laneBit(TimelineLane kind) { return 1u << static_cast<unsigned>(kind); }

class PluginWindow final : public juce::DocumentWindow {
public:
    explicit PluginWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colour(panel), DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);
    }
    std::function<void()> onClosed;
    void closeButtonPressed() override {
        setVisible(false);
        if (onClosed) onClosed();
    }
};

class EffectChainPanel final : public juce::Component, private juce::Timer {
public:
    struct Entry { juce::String name; bool editable; };
    struct State { std::vector<Entry> entries; bool busy = true; };

    EffectChainPanel(const juce::String& track, std::function<State()> read,
                     std::function<void(int, int)> move, std::function<void(int)> edit,
                     std::function<void(int)> remove, std::function<void(juce::Component&)> add,
                     std::function<void()> browser)
        : read_(std::move(read)), move_(std::move(move)) {
        title_.setText(track + " / FX chain", juce::dontSendNotification);
        title_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        direction_.setText("Drag handles or use Up / Down. Audio flows top to bottom.", juce::dontSendNotification);
        direction_.setFont(juce::FontOptions(12.0f));
        for (auto* component : std::array<juce::Component*, 4>{&title_, &direction_, &add_, &browser_})
            addAndMakeVisible(*component);
        add_.onClick = [this, add = std::move(add)] { add(add_); };
        browser_.onClick = std::move(browser);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto& row = rows_[i];
            const auto index = static_cast<int>(i);
            row.handle.setTitle("Drag effect " + juce::String(index + 1));
            row.handle.setTooltip("Drag to a new position in the chain");
            row.handle.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            row.handle.addMouseListener(this, false);
            row.name.setJustificationType(juce::Justification::centredLeft);
            row.up.setTitle("Move effect " + juce::String(index + 1) + " up");
            row.down.setTitle("Move effect " + juce::String(index + 1) + " down");
            row.open.onClick = [edit, index] { edit(index); };
            row.remove.onClick = [this, remove, index] { remove(index); refresh(); };
            row.up.onClick = [this, index] { moveRow(index, index - 1); };
            row.down.onClick = [this, index] { moveRow(index, index + 1); };
            for (auto* component : row.components()) addChildComponent(component);
        }
        refresh();
        startTimerHz(5);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        title_.setBounds(area.removeFromTop(24));
        direction_.setBounds(area.removeFromTop(22));
        for (int i = 0; i < count_; ++i) {
            auto& row = rows_[static_cast<std::size_t>(i)];
            auto line = area.removeFromTop(rowHeight).reduced(0, 2);
            row.handle.setBounds(line.removeFromLeft(40).reduced(1, 0));
            row.remove.setBounds(line.removeFromRight(60).reduced(1, 0));
            row.open.setBounds(line.removeFromRight(48).reduced(1, 0));
            row.down.setBounds(line.removeFromRight(54).reduced(1, 0));
            row.up.setBounds(line.removeFromRight(42).reduced(1, 0));
            row.name.setBounds(line.reduced(4, 0));
        }
        area.removeFromTop(4);
        add_.setBounds(area.removeFromLeft(116).reduced(1, 0));
        browser_.setBounds(area.removeFromLeft(115).reduced(1, 0));
    }

    void paint(juce::Graphics& g) override {
        if (!dragTarget_ || dragTarget_ == dragSource_) return;
        const auto& handle = rows_[static_cast<std::size_t>(*dragTarget_)].handle;
        const auto y = *dragTarget_ < *dragSource_ ? handle.getY() - 1 : handle.getBottom() + 1;
        g.setColour(juce::Colour(accent));
        g.fillRect(12, y, getWidth() - 24, 2);
    }

private:
    struct Row {
        juce::TextButton handle {"::"}, up {"Up"}, down {"Down"}, open {"Edit"}, remove {"Remove"};
        juce::Label name;
        std::array<juce::Component*, 6> components() { return {&handle, &name, &up, &down, &open, &remove}; }
    };

    void timerCallback() override { refresh(); }

    void refresh() {
        const auto state = read_();
        count_ = static_cast<int>(state.entries.size());
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            auto& row = rows_[static_cast<std::size_t>(i)];
            const bool visible = i < count_;
            for (auto* component : row.components()) component->setVisible(visible);
            if (!visible) continue;
            const auto& entry = state.entries[static_cast<std::size_t>(i)];
            row.name.setText(juce::String(i + 1) + ". " + entry.name, juce::dontSendNotification);
            row.name.setTooltip(entry.name);
            row.handle.setEnabled(!state.busy && count_ > 1);
            row.up.setEnabled(!state.busy && i > 0);
            row.down.setEnabled(!state.busy && i + 1 < count_);
            row.open.setEnabled(!state.busy && entry.editable);
            row.remove.setEnabled(!state.busy);
        }
        if (getHeight() != 102 + count_ * rowHeight) {
            dragSource_.reset();
            dragTarget_.reset();
            setSize(484, 102 + count_ * rowHeight);
        }
        add_.setEnabled(!state.busy && state.entries.size() < rows_.size());
    }

    void moveRow(int from, int to) {
        if (from != to && to >= 0 && to < count_) {
            move_(from, to);
            refresh();
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        if (!event.mods.isLeftButtonDown()) return;
        for (int i = 0; i < count_; ++i)
            if (event.eventComponent == &rows_[static_cast<std::size_t>(i)].handle) dragSource_ = i;
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!dragSource_ || event.getDistanceFromDragStart() < 4 || count_ < 2) return;
        const auto y = event.getEventRelativeTo(this).y - rows_.front().handle.getY();
        dragTarget_ = juce::jlimit(0, count_ - 1, y / rowHeight);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        const auto from = std::exchange(dragSource_, std::nullopt);
        const auto to = std::exchange(dragTarget_, std::nullopt);
        if (from && to) moveRow(*from, *to);
        repaint();
    }

    static constexpr int rowHeight = 30;
    std::function<State()> read_;
    std::function<void(int, int)> move_;
    std::array<Row, ostfstudio::SessionMix::maxEffects> rows_;
    juce::Label title_, direction_;
    juce::TextButton add_ {"Add effect"}, browser_ {"Plug-in Browser"};
    int count_ = 0;
    std::optional<int> dragSource_, dragTarget_;
};

class GeneratorPanel final : public juce::Component {
public:
    GeneratorPanel(std::unique_ptr<juce::AudioProcessorEditor> editor, juce::DragAndDropContainer& dragHost)
        : editor_(std::move(editor)), dragHost_(dragHost) {
        addAndMakeVisible(*editor_);
        addAndMakeVisible(ostfDrag_);
        ostfDrag_.setTooltip("Drop on an OSTF Studio arrangement track to add the pattern as editable OSTF notes.");
        ostfDrag_.addMouseListener(this, false);
        setSize(editor_->getWidth(), editor_->getHeight() + 44);
    }

    void resized() override {
        auto area = getLocalBounds();
        ostfDrag_.setBounds(area.removeFromBottom(44).reduced(16, 5));
        editor_->setBounds(area);
    }

private:
    void mouseDrag(const juce::MouseEvent& event) override {
        if (event.getDistanceFromDragStart() < 5 || dragHost_.isDragAndDropActive()) return;
        dragHost_.startDragging(generatorDragId, &ostfDrag_, juce::ScaledImage{}, true, nullptr, &event.source);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    juce::DragAndDropContainer& dragHost_;
    juce::TextButton ostfDrag_ {"Drag notes to an OSTF Studio track"};
};

class TrackMixPanel final : public juce::Component {
public:
    TrackMixPanel(MixerPanel& mixer, std::size_t index, const juce::String& trackName,
                  const juce::String& preset, std::function<bool()> stillOpen) {
        const auto& channel = mixer.channel(index);
        title_.setText(trackName, juce::dontSendNotification);
        title_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        preset_.setText(preset, juce::dontSendNotification);
        preset_.setTooltip(preset);
        preset_.setColour(juce::Label::textColourId, juce::Colour(muted));
        volume_.setRange(-60.0, 6.0, 0.1);
        volume_.setTextValueSuffix(" dB");
        volume_.setValue(channel.volumeDb);
        volume_.setDoubleClickReturnValue(true, 0.0);
        pan_.setRange(-100.0, 100.0, 1.0);
        pan_.setValue(channel.pan * 100.0);
        pan_.setDoubleClickReturnValue(true, 0.0);
        pan_.textFromValueFunction = [](double v) {
            if (v == 0.0) return juce::String("Center");
            return juce::String(v < 0 ? "L " : "R ") + juce::String(juce::roundToInt(std::abs(v)));
        };
        send_.setRange(0.0, 100.0, 1.0);
        send_.setTextValueSuffix("%");
        output_.addItem("Master", 1);
        output_.addItem("Bus A", 2);
        output_.addItem("Bus B", 3);
        output_.setSelectedId(channel.outputBus + 2);
        for (std::size_t i = 0; i < mixer.returnCount(); ++i)
            target_.addItem("Return " + juce::String(static_cast<int>(i + 1)), static_cast<int>(i + 1));
        target_.setSelectedId(1);
        send_.setValue(channel.returnSends[0] * 100.0);
        for (auto* slider : {&volume_, &pan_, &send_}) {
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, slider == &pan_, 65, 20);
            addAndMakeVisible(*slider);
        }
        for (auto* item : {static_cast<juce::Component*>(&title_), static_cast<juce::Component*>(&preset_),
                          static_cast<juce::Component*>(&output_), static_cast<juce::Component*>(&target_)})
            addAndMakeVisible(*item);
        volume_.onValueChange = [this, &mixer, index, stillOpen] { if (stillOpen()) mixer.setVolumeDb(index, volume_.getValue()); };
        pan_.onValueChange = [this, &mixer, index, stillOpen] {
            if (stillOpen()) mixer.setPan(index, static_cast<float>(pan_.getValue() / 100.0));
        };
        output_.onChange = [this, &mixer, index, stillOpen] {
            if (stillOpen()) mixer.setOutputBus(index, output_.getSelectedId() - 2);
        };
        target_.onChange = [this, &mixer, index, stillOpen] {
            if (stillOpen() && target_.getSelectedId() > 0)
                send_.setValue(mixer.channel(index).returnSends[static_cast<std::size_t>(target_.getSelectedId() - 1)] * 100.0,
                               juce::dontSendNotification);
        };
        send_.onValueChange = [this, &mixer, index, stillOpen] {
            if (stillOpen() && target_.getSelectedId() > 0)
                mixer.setSendLevel(index, static_cast<std::size_t>(target_.getSelectedId() - 1),
                                   static_cast<float>(send_.getValue() / 100.0));
        };
        setSize(340, 218);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        title_.setBounds(area.removeFromTop(24));
        preset_.setBounds(area.removeFromTop(20));
        for (auto* control : {static_cast<juce::Component*>(&volume_), static_cast<juce::Component*>(&pan_),
                             static_cast<juce::Component*>(&output_), static_cast<juce::Component*>(&target_),
                             static_cast<juce::Component*>(&send_)})
            control->setBounds(area.removeFromTop(29).withTrimmedLeft(68).reduced(2));
    }

    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(muted));
        int y = 56;
        for (const auto* label : {"Volume", "Pan", "Output", "Send to", "Send level"}) {
            g.drawText(label, 12, y, 65, 29, juce::Justification::centredLeft);
            y += 29;
        }
    }

private:
    juce::Label title_, preset_;
    juce::Slider volume_, pan_, send_;
    juce::ComboBox output_, target_;
};
}

MainComponent::MainComponent() {
    juce::PropertiesFile::Options options;
    options.applicationName = "OSTF Studio View";
    options.filenameSuffix = ".settings";
    options.folderName = "OSTF Studio";
    options.osxLibrarySubFolder = "Application Support";
    viewPreferences_.setStorageParameters(options);
    if (const auto* config = viewPreferences_.getUserSettings())
        timelineLaneMask_ = static_cast<unsigned>(config->getIntValue("timelineLanes", 0xff));
    setSize(1100, 690);
    menuBar_.setModel(this);
    menuBar_.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff20262d));
    addAndMakeVisible(menuBar_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(recordButton_);
    addAndMakeVisible(pluginBrowserButton_);
    addAndMakeVisible(transportPosition_);
    transportPosition_.setText("Bar 1", juce::dontSendNotification);
    playButton_.onClick = [this] { startTransport(false); };
    stopButton_.onClick = [this] { stopTransport(); };
    recordButton_.onClick = [this] { startTransport(true); };
    recordButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
    pluginBrowserButton_.onClick = [this] { togglePluginBrowser(); };
    addChildComponent(pianoButton_);
    addChildComponent(mixerButton_);
    addChildComponent(trackPicker_);
    addChildComponent(performanceLane_);
    performanceLane_.setTitle("Performance lane");
    performanceLane_.onChange = [this] { repaint(); };
    addChildComponent(midiDrag_);
    midiDrag_.setButtonText("Drag MIDI to another DAW");
    midiDrag_.setTooltip("Export the selected track as a MIDI file and drag it into another DAW.");
    midiDrag_.writeMidi = [this](juce::OutputStream& stream) { return selectedTrack_ && writeTrackMidi(*selectedTrack_, stream); };
    midiDrag_.reportError = [this](const juce::String& error) { statusLabel_.setText(error, juce::dontSendNotification); };
    addChildComponent(mixer_);
    setupOutputTracks();
    pluginFormats_.addFormat(std::make_unique<juce::VST3PluginFormat>());
    trackPicker_.setTitle("Arrangement track");
    trackPicker_.setTextWhenNothingSelected("Select track");
    trackPicker_.onChange = [this] {
        if (const auto id = trackPicker_.getSelectedId(); id > 0) selectTrack(static_cast<std::size_t>(id - 1));
    };
    pianoButton_.onClick = [this] {
        mixerButton_.setToggleState(false, juce::dontSendNotification);
        refreshEditor();
    };
    mixerButton_.onClick = [this] {
        mixerButton_.setToggleState(true, juce::dontSendNotification);
        refreshEditor();
    };
    mixer_.channelChanged = [this](std::size_t index) {
        auto& controls = *trackControls_[index];
        const auto& mix = mixer_.channel(index);
        controls.mute.setToggleState(mix.muted, juce::dontSendNotification);
        controls.solo.setToggleState(mix.soloed, juce::dontSendNotification);
        controls.volume.setValue(mix.volumeDb, juce::dontSendNotification);
        updateTransportMix();
    };
    mixer_.routingChanged = [this] {
        for (std::size_t i = 0; i < outputControls_.size(); ++i) {
            outputControls_[i].volume.setValue(mixer_.outputVolume(i), juce::dontSendNotification);
            outputControls_[i].mute.setToggleState(mixer_.routing().outputMuted[i], juce::dontSendNotification);
        }
        updateTransportMix();
    };
    mixer_.returnsChanged = [this] { updateTransportMix(); resized(); repaint(); };
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
    statusLabel_.setFont(juce::FontOptions(13.0f));
    refreshTimelineLanes();
    openDemo();
}

juce::StringArray MainComponent::getMenuBarNames() { return {"File", "View", "Settings", "Help", "Track"}; }

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu menu;
    switch (index) {
        case 0: {
            menu.addItem(openFile, "Open OSTF / OSTFP...");
            juce::PopupMenu demos;
            for (std::size_t i = 0; i < std::size(gallery); ++i)
                demos.addItem(demoMenuBase + static_cast<int>(i), gallery[i].title);
            demos.addSeparator();
            demos.addItem(openDemoSong, "Official specification example");
            menu.addSubMenu("Demo songs", demos);
            menu.addSeparator();
            menu.addItem(saveScore, "Save as OSTF...", project_ != nullptr);
            menu.addItem(saveProject, "Save as OSTFP...", project_ != nullptr);
            break;
        }
        case 1: {
            menu.addItem(showPiano, "Piano Roll", project_ != nullptr, !mixerButton_.getToggleState());
            menu.addItem(showMixer, "Mixer", project_ != nullptr, mixerButton_.getToggleState());
            menu.addItem(showPlugins, "Plug-in Browser", true, pluginBrowserButton_.getToggleState());
            menu.addItem(showOutputs, "Output Tracks", project_ != nullptr, showOutputTracks_);
            juce::PopupMenu lanes;
            for (std::size_t i = 0; i < std::size(selectableTimelineLanes); ++i)
                lanes.addItem(timelineMenuBase + static_cast<int>(i), timelineMenuLabels[i], true,
                              (timelineLaneMask_ & laneBit(selectableTimelineLanes[i])) != 0);
            menu.addSubMenu("Global Timeline Lanes", lanes);
            break;
        }
        case 2: menu.addItem(showDevices, "Audio / MIDI Devices..."); break;
        case 3: menu.addItem(showAbout, "About OSTF Studio"); break;
        case 4: {
            const auto editable = canEditTracks();
            const auto add = editable && project_->trackLanes.size() < ostfstudio::OstfpPreview::maxTracks;
            menu.addItem(addPitched, "Add pitched track", add);
            menu.addItem(addDrums, "Add drum / percussion track", add);
            menu.addItem(addAudio, "Add audio track", add);
            menu.addSeparator();
            menu.addItem(removeTrack, "Delete selected track...", editable && selectedTrack_.has_value());
            break;
        }
    }
    return menu;
}

void MainComponent::menuItemSelected(int item, int) {
    if (item >= demoMenuBase && item < demoMenuBase + static_cast<int>(std::size(gallery))) {
        int size = 0;
        if (const auto* data = BinaryData::getNamedResource(gallery[item - demoMenuBase].resource, size)) {
            juce::MemoryInputStream stream(data, static_cast<std::size_t>(size), false);
            openPreview(stream);
        } else {
            statusLabel_.setText("Bundled demo is unavailable", juce::dontSendNotification);
        }
        return;
    }
    if (item >= timelineMenuBase && item < timelineMenuBase + static_cast<int>(std::size(selectableTimelineLanes))) {
        timelineLaneMask_ ^= laneBit(selectableTimelineLanes[static_cast<std::size_t>(item - timelineMenuBase)]);
        if (auto* config = viewPreferences_.getUserSettings()) config->setValue("timelineLanes", static_cast<int>(timelineLaneMask_));
        viewPreferences_.saveIfNeeded();
        refreshTimelineLanes();
        resized();
        repaint();
        return;
    }
    switch (item) {
        case openFile: inspectOSTF(); break;
        case openDemoSong: openDemo(); break;
        case saveScore: saveDocument(false); break;
        case saveProject: saveDocument(true); break;
        case showPiano:
        case showMixer:
            mixerButton_.setToggleState(item == showMixer, juce::dontSendNotification);
            refreshEditor();
            break;
        case showDevices:
            if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
            deviceSettings_->setVisible(true);
            deviceSettings_->toFront(true);
            break;
        case showPlugins: togglePluginBrowser(); break;
        case showOutputs: showOutputTracks_ = !showOutputTracks_; resized(); repaint(); break;
        case addPitched: editTracks(ostfstudio::TrackKind::pitched); break;
        case addDrums: editTracks(ostfstudio::TrackKind::drum); break;
        case addAudio: editTracks(ostfstudio::TrackKind::audio); break;
        case removeTrack:
            if (canEditTracks() && selectedTrack_) {
                const auto index = *selectedTrack_;
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Delete track",
                    "Remove " + juce::String::fromUTF8(project_->trackLanes[index].label)
                        + " and its clips from this project? Audio files stay on disk.",
                    "Delete track", "Cancel", this,
                    juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this),
                        index, generation = projectGeneration_](int confirmed) {
                        if (confirmed && safe && safe->projectGeneration_ == generation)
                            safe->editTracks(ostfstudio::TrackKind::pitched, index);
                    }));
            }
            break;
        case showAbout:
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "About OSTF Studio", "OSTF 5.0 preview with a timeline, piano roll and session mixer.");
            break;
    }
}

PluginBrowser& MainComponent::ensurePluginBrowser() {
    if (!pluginBrowser_) {
        pluginBrowser_ = std::make_unique<PluginBrowser>();
        addChildComponent(*pluginBrowser_);
    }
    return *pluginBrowser_;
}

void MainComponent::togglePluginBrowser() {
    const bool shown = !pluginBrowserButton_.getToggleState();
    auto& browser = ensurePluginBrowser();
    if (shown && !browser.hasScanCache()) browser.scan();
    pluginBrowserButton_.setToggleState(shown, juce::dontSendNotification);
    pluginBrowser_->setVisible(shown);
    resized();
    repaint();
}

juce::Rectangle<int> MainComponent::getWorkspace() const {
    auto bounds = workspaceBounds(getLocalBounds());
    if (pluginBrowserButton_.getToggleState()) bounds.removeFromRight(juce::jmin(230, bounds.getWidth() / 3) + 8);
    return bounds;
}

int MainComponent::getArrangementHeight(int workspaceHeight) const {
    const auto view = currentArrangement();
    const auto outputs = visibleOutputCount();
    const auto rows = 1 + static_cast<int>(view.globalLanes.size() + view.trackLanes.size() + outputs);
    const auto target = mixerButton_.getToggleState() ? workspaceHeight / 2 : workspaceHeight * 3 / 5;
    return std::max(target, 26 + arrangementHeadingHeight + rows * 12
                   + (outputs ? arrangementHeadingHeight : 0));
}

void MainComponent::refreshTimelineLanes() {
    displayedGlobalLanes_.clear();
    const auto original = project_ ? project_->view() : ostfstudio::demo::arrangement;
    for (const auto& lane : original.globalLanes)
        if (lane.kind == TimelineLane::none || (timelineLaneMask_ & laneBit(lane.kind)) != 0)
            displayedGlobalLanes_.push_back(lane);
}

ostfstudio::ArrangementView MainComponent::currentArrangement() const {
    const auto original = project_ ? project_->view() : ostfstudio::demo::arrangement;
    return {original.bars, displayedGlobalLanes_, original.trackLanes};
}

bool MainComponent::isInterestedInDragSource(const SourceDetails& details) {
    const auto id = details.description.toString();
    return project_ && (id == generatorDragId || (pluginBrowser_ && id.startsWith("ostf-vst3:")));
}

std::optional<std::size_t> MainComponent::pluginDropTrack(juce::Point<int> position) const {
    for (std::size_t i = 0; i < std::min(trackRows_.size(), trackControls_.size()); ++i)
        if (trackRows_[i].contains(position) && !trackControls_[i]->loading) return i;
    return std::nullopt;
}

std::size_t MainComponent::visibleOutputCount() const {
    return project_ && showOutputTracks_ ? 3 + mixer_.returnCount() : 0;
}

std::optional<std::size_t> MainComponent::outputDropTrack(juce::Point<int> position) const {
    for (std::size_t i = 0; i < visibleOutputCount(); ++i)
        if (outputRows_[i].contains(position) && outputControls_[i].effects.loading == 0) return i;
    return std::nullopt;
}

void MainComponent::itemDragMove(const SourceDetails& details) {
    const auto id = details.description.toString();
    const auto plugin = pluginBrowser_ && id.startsWith("ostf-vst3:") ? pluginBrowser_->find(id.substring(10)) : std::nullopt;
    const auto output = plugin && !plugin->isInstrument ? outputDropTrack(details.localPosition) : std::nullopt;
    const auto track = pluginDropTrack(details.localPosition);
    if (track == pluginDropHighlight_ && output == outputDropHighlight_) return;
    outputDropHighlight_ = output;
    pluginDropHighlight_ = track;
    repaint();
}

void MainComponent::itemDragExit(const SourceDetails&) {
    if (!pluginDropHighlight_ && !outputDropHighlight_) return;
    outputDropHighlight_.reset();
    pluginDropHighlight_.reset();
    repaint();
}

void MainComponent::itemDropped(const SourceDetails& details) {
    itemDragExit(details);
    if (!isInterestedInDragSource(details)) return;
    const auto id = details.description.toString();
    if (const auto output = outputDropTrack(details.localPosition); output && id.startsWith("ostf-vst3:")) {
        if (const auto plugin = pluginBrowser_->find(id.substring(10)); plugin && !plugin->isInstrument)
            appendEffect(true, *output, *plugin);
        return;
    }
    const auto track = pluginDropTrack(details.localPosition);
    if (!track) return;
    if (details.description.toString() == generatorDragId) {
        const auto timeline = trackRows_[*track].withTrimmedLeft(trackHeaderWidth);
        const auto barCount = project_->bars.endBar - project_->bars.firstBar;
        const auto x = juce::jlimit(0, timeline.getWidth() - 1, details.localPosition.x - timeline.getX());
        const auto bar = project_->bars.firstBar + x * barCount / timeline.getWidth();
        if (project_->insertGeneratorPattern(*track, bar)) {
            selectTrack(*track);
            statusLabel_.setText("OSTF pattern added at bar " + juce::String(bar) + " / current session",
                                 juce::dontSendNotification);
        } else {
            statusLabel_.setText("The OSTF pattern does not fit at this position", juce::dontSendNotification);
        }
        return;
    }
    const auto plugin = pluginBrowser_->find(details.description.toString().substring(10));
    if (!plugin) return;
    selectTrack(*track);
    if (plugin->isInstrument && project_->trackKinds[*track] == ostfstudio::TrackKind::audio) return;
    if (plugin->isInstrument) loadTrackPlugin(*track, *plugin);
    else appendEffect(false, *track, *plugin);
}

void MainComponent::updateTransportMix() {
    if (!project_) return;
    for (std::size_t i = 0; i < trackControls_.size(); ++i)
        transport_.setChannelMix(i, mixer_.channel(i), mixer_.isAudible(i), mixer_.routing().cueDb);
    transport_.setOutputMix(mixer_.routing());
}

MainComponent::~MainComponent() {
    stopTimer();
    transport_.stop();
}

void MainComponent::startTransport(bool recording) {
    if (!project_ || (recording && !selectedTrack_)) return;
    if (recording && project_->trackKinds[*selectedTrack_] != ostfstudio::TrackKind::pitched) {
        statusLabel_.setText("MIDI recording requires a pitched track in this preview", juce::dontSendNotification);
        return;
    }
    if (transportActive_) stopTransport();
    if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
    updateTransportMix();
    std::vector<PreviewTransport::TrackSetup> tracks;
    tracks.reserve(trackControls_.size());
    std::vector<juce::String> unavailable;
    for (std::size_t i = 0; i < trackControls_.size(); ++i) {
        const auto& track = *trackControls_[i];
        const auto destination = track.outputTrack.value_or(i);
        tracks.push_back({track.outputTrack, track.playableInstrument ? track.instance.get() : nullptr, track.midiChannel});
        if ((track.outputTrack || track.pluginPath != juce::File{})
            && (i >= PreviewTransport::maxTracks || destination >= PreviewTransport::maxTracks || destination >= trackControls_.size()
                         || !trackControls_[destination]->playableInstrument))
            unavailable.push_back(juce::String::fromUTF8(project_->trackLanes[i].label));
    }
    PreviewTransport::Inserts inserts {};
    const auto copyEffects = [](const EffectChain& chain, PreviewTransport::Chain& destination) {
        for (std::size_t slot = 0; slot < chain.slots.size(); ++slot) destination[slot] = chain.slots[slot].effect.get();
    };
    for (std::size_t i = 0; i < outputControls_.size(); ++i) copyEffects(outputControls_[i].effects, inserts[i]);
    for (std::size_t i = 0; i < std::min(trackControls_.size(), PreviewTransport::maxTracks); ++i)
        copyEffects(trackControls_[i]->effects, inserts[PreviewTransport::outputCount + i]);
    const auto result = transport_.start(deviceSettings_->devices(), *project_, recording, tracks, inserts);
    if (result.failed()) {
        statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
        return;
    }
    transportActive_ = true;
    recordTrack_ = recording ? selectedTrack_ : std::nullopt;
    playButton_.setToggleState(!recording, juce::dontSendNotification);
    recordButton_.setToggleState(recording, juce::dontSendNotification);
    statusLabel_.setText(recording ? "Recording MIDI into the selected track" : "Playing arrangement",
                         juce::dontSendNotification);
    if (!unavailable.empty())
        statusLabel_.setText("No playable instrument on: " + unavailable.front()
                             + (unavailable.size() > 1 ? " (and other tracks)" : ""), juce::dontSendNotification);
    else if (!recording && project_->audio.empty()
             && std::none_of(tracks.begin(), tracks.end(), [](const auto& track) { return track.instrument; }))
        statusLabel_.setText("Assign a VST3 instrument to hear playback", juce::dontSendNotification);
    startTimerHz(24);
}

void MainComponent::stopTransport() {
    if (!transportActive_) return;
    auto recorded = transport_.stop();
    if (recordTrack_ && project_) {
        const auto count = static_cast<int>(recorded.size());
        const bool added = project_->addSessionClip(*recordTrack_, "Recorded MIDI", std::move(recorded));
        selectTrack(*recordTrack_);
        statusLabel_.setText(added ? juce::String(count) + " MIDI notes captured" : "No MIDI notes captured",
                             juce::dontSendNotification);
    } else {
        statusLabel_.setText("Stopped", juce::dontSendNotification);
    }
    recordTrack_.reset();
    transportActive_ = false;
    playButton_.setToggleState(false, juce::dontSendNotification);
    recordButton_.setToggleState(false, juce::dontSendNotification);
    transportPosition_.setText("Bar 1", juce::dontSendNotification);
    stopTimer();
    repaint();
}

void MainComponent::timerCallback() {
    if (!transport_.isRunning()) { stopTransport(); return; }
    transportPosition_.setText("Bar " + juce::String(1 + transport_.positionTick() / project_->ticksPerBar), juce::dontSendNotification);
    repaint();
}

void MainComponent::openDemo() {
    juce::MemoryInputStream stream(BinaryData::_10comprehensivecore_ostfp,
                                   BinaryData::_10comprehensivecore_ostfpSize, false);
    openPreview(stream);
}

void MainComponent::inspectOSTF() {
    chooser_ = std::make_unique<juce::FileChooser>("Open an OSTF score, OSTFP package, or JSON", juce::File{},
                                                   "*.ostfp;*.ostf;*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser) {
            if (self == nullptr) return;
            const auto selected = chooser.getResult();
            if (selected != juce::File{}) self->openDocument(selected);
        });
}

bool MainComponent::openDocument(const juce::File& file) {
    juce::FileInputStream input(file);
    if (!input.openedOk()) {
        statusLabel_.setText("Could not read: " + file.getFileName(), juce::dontSendNotification);
        return false;
    }
    if (!file.hasFileExtension("json")) return openPreview(input, file.hasFileExtension("ostf"), file.getParentDirectory());
    juce::var parsed;
    const bool inspected = juce::JSON::parse(input.readEntireStreamAsString(), parsed).wasOk() && !parsed.isVoid();
    statusLabel_.setText((inspected ? "JSON object or array read: " : "Expected a JSON object or array: ")
                         + file.getFileName(), juce::dontSendNotification);
    return inspected;
}

bool MainComponent::openPreview(juce::InputStream& input, bool plainScore, const juce::File& sourceFolder) {
    std::unique_ptr<ostfstudio::OstfpPreview> next;
    const auto result = plainScore ? ostfstudio::readOstfPreview(input, next) : ostfstudio::readOstfpPreview(input, next);
    if (result.wasOk()) {
        stopTransport();
        project_ = std::move(next);
        for (auto& control : outputControls_) {
            control.effects.slots.clear();
            control.effects.loading = 0;
            updateEffectButton(control.effects);
        }
        if (plainScore && sourceFolder != juce::File{})
            for (auto& audio : project_->audio) audio.localFile = sourceFolder.getChildFile(audio.path);
        refreshTimelineLanes();
        ++projectGeneration_;
        selectedTrack_.reset();
        trackRows_.clear();
        trackPicker_.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < project_->trackLanes.size(); ++i)
            trackPicker_.addItem(juce::String::fromUTF8(project_->trackLanes[i].label), static_cast<int>(i + 1));
        trackPicker_.setEnabled(!project_->trackLanes.empty());
        mixer_.setTracks(*project_);
        rebuildTrackControls();
        if (const auto& saved = project_->savedMix) {
            mixer_.restore(*saved);
            for (std::size_t i = 0; i < std::min(trackControls_.size(), saved->midiRoutes.size()); ++i) {
                trackControls_[i]->midiChannel = saved->midiRoutes[i].channel;
                trackControls_[i]->outputTrack = saved->midiRoutes[i].destination;
            }
            for (std::size_t i = 0; i < outputControls_.size(); ++i) {
                auto ids = saved->outputEffects[i];
                if (ids.isEmpty() && saved->insertIds[i].isNotEmpty()) ids.add(saved->insertIds[i]);
                restoreEffects(true, i, ids);
            }
            for (std::size_t i = 0; i < std::min(trackControls_.size(), saved->trackEffects.size()); ++i)
                restoreEffects(false, i, saved->trackEffects[i]);
        }
        const auto first = std::find_if(project_->pitchedNotes.begin(), project_->pitchedNotes.end(),
                                        [](const auto& notes) { return !notes.empty(); });
        if (first != project_->pitchedNotes.end())
            selectTrack(static_cast<std::size_t>(first - project_->pitchedNotes.begin()));
        else if (!project_->trackLanes.empty())
            selectTrack(0);
        else {
            mixerButton_.setToggleState(false, juce::dontSendNotification);
            refreshEditor();
        }
    }
    statusLabel_.setText(result.wasOk() ? project_->title + " / " + juce::String(project_->bars.endBar - 1) + " bars"
                                       : result.getErrorMessage(),
                         juce::dontSendNotification);
    repaint();
    return result.wasOk();
}

void MainComponent::saveDocument(bool package) {
    if (!project_) return;
    const juce::String extension = package ? "ostfp" : "ostf";
    auto title = juce::File::createLegalFileName(project_->title).trim();
    if (title.isEmpty()) title = "Untitled";
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(title + "." + extension);
    chooser_ = std::make_unique<juce::FileChooser>(package ? "Save as OSTFP" : "Save as OSTF", start, "*." + extension);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, package,
         extension](const juce::FileChooser& chooser) {
            if (!safe || safe->projectGeneration_ != generation) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension(extension)) {
                safe->statusLabel_.setText("Choose a ." + extension + " filename", juce::dontSendNotification);
                return;
            }
            if (safe->recordTrack_) safe->stopTransport();
            juce::TemporaryFile temporary(file);
            auto result = juce::Result::fail("Could not open a temporary file for saving");
            {
                juce::FileOutputStream output(temporary.getFile());
                if (output.openedOk()) {
                    auto mix = safe->mixer_.snapshot();
                    for (const auto& track : safe->trackControls_)
                        mix.midiRoutes.push_back({track->midiChannel, track->outputTrack});
                    for (std::size_t i = 0; i < safe->outputControls_.size(); ++i)
                        for (const auto& slot : safe->outputControls_[i].effects.slots) mix.outputEffects[i].add(slot.id);
                    for (const auto kind : safe->project_->trackKinds) mix.audioTracks.push_back(kind == ostfstudio::TrackKind::audio);
                    mix.trackEffects.resize(safe->trackControls_.size());
                    for (std::size_t i = 0; i < safe->trackControls_.size(); ++i)
                        for (const auto& slot : safe->trackControls_[i]->effects.slots) mix.trackEffects[i].add(slot.id);
                    result = package ? ostfstudio::writeOstfpPreview(*safe->project_, mix, output)
                                     : ostfstudio::writeOstfPreview(*safe->project_, mix, file, output);
                    output.flush();
                    if (result.wasOk()) result = output.getStatus();
                }
            }
            if (result.wasOk() && !temporary.overwriteTargetFileWithTemporary())
                result = juce::Result::fail("Could not replace the destination file");
            safe->statusLabel_.setText(result.wasOk() ? "Saved " + file.getFileName() : result.getErrorMessage(),
                                       juce::dontSendNotification);
        });
}

bool MainComponent::canEditTracks() const {
    if (!project_) return false;
    for (const auto& track : trackControls_)
        if (track->loading || track->effects.loading) return false;
    for (const auto& output : outputControls_)
        if (output.effects.loading) return false;
    return true;
}

void MainComponent::editTracks(ostfstudio::TrackKind kind, std::optional<std::size_t> remove) {
    if (!canEditTracks()) return;
    stopTransport();
    std::unique_ptr<ostfstudio::OstfpPreview> updated;
    const auto result = project_->editTracks(kind, remove, updated);
    if (result.failed()) {
        statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
        return;
    }
    auto mix = mixer_.snapshot();
    auto previous = std::move(trackControls_);
    project_ = std::move(updated);
    ++projectGeneration_;
    if (remove) {
        previous.erase(previous.begin() + static_cast<std::ptrdiff_t>(*remove));
        mix.tracks.erase(mix.tracks.begin() + static_cast<std::ptrdiff_t>(*remove));
    } else {
        mix.tracks.emplace_back();
    }
    refreshTimelineLanes();
    selectedTrack_.reset();
    pendingMidiDrag_.reset();
    pluginDropHighlight_.reset();
    outputDropHighlight_.reset();
    trackPicker_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i)
        trackPicker_.addItem(juce::String::fromUTF8(project_->trackLanes[i].label), static_cast<int>(i + 1));
    trackPicker_.setEnabled(!project_->trackLanes.empty());
    mixer_.setTracks(*project_);
    rebuildTrackControls();
    for (std::size_t i = 0; i < previous.size(); ++i) {
        auto& old = *previous[i];
        auto& track = *trackControls_[i];
        old.editor.reset();
        track.instance = std::move(old.instance);
        track.pluginPath = old.pluginPath;
        track.candidatePath = old.candidatePath;
        track.playableInstrument = old.playableInstrument;
        track.midiChannel = old.midiChannel;
        track.outputTrack = old.outputTrack;
        if (remove && track.outputTrack) {
            if (*track.outputTrack == *remove) track.outputTrack.reset();
            else if (*track.outputTrack > *remove) --*track.outputTrack;
        }
        track.effects.slots = std::move(old.effects.slots);
        updateEffectButton(track.effects);
        if (track.instance) track.plugin.setButtonText(track.instance->getName());
        updatePresetLabel(i);
    }
    mixer_.restore(mix);
    if (!trackControls_.empty()) selectTrack(std::min(remove.value_or(trackControls_.size() - 1), trackControls_.size() - 1));
    else refreshEditor();
    statusLabel_.setText(remove ? "Track removed / save to keep changes" : "Track added / save to keep changes",
                         juce::dontSendNotification);
}

void MainComponent::rebuildTrackControls() {
    trackControls_.clear();
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i) {
        auto controls = std::make_unique<TrackControls>();
        const auto name = juce::String::fromUTF8(project_->trackLanes[i].label);
        controls->mute.setTitle(name + " mute");
        controls->mute.setTooltip("Mute " + name);
        controls->mute.setClickingTogglesState(true);
        controls->mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
        controls->solo.setTitle(name + " solo");
        controls->solo.setTooltip("Solo " + name);
        controls->solo.setClickingTogglesState(true);
        controls->solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8a622b));
        controls->volume.setName(name + " volume");
        controls->volume.setTooltip("Volume in dB. Double-click to reset to 0 dB.");
        controls->volume.setSliderStyle(juce::Slider::LinearHorizontal);
        controls->volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 41, 16);
        controls->volume.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(hardwareAmber));
        controls->volume.setRange(-60.0, 6.0, 0.1);
        controls->volume.setValue(0.0, juce::dontSendNotification);
        controls->volume.setDoubleClickReturnValue(true, 0.0);
        controls->mute.onClick = [this, i, control = &controls->mute] { mixer_.setMuted(i, control->getToggleState()); };
        controls->solo.onClick = [this, i, control = &controls->solo] { mixer_.setSoloed(i, control->getToggleState()); };
        controls->volume.onValueChange = [this, i, control = &controls->volume] { mixer_.setVolumeDb(i, control->getValue()); };
        controls->plugin.setTooltip("Assign a local plug-in to " + name);
        controls->plugin.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25221e));
        controls->plugin.setColour(juce::TextButton::textColourOffId, juce::Colour(hardwareAmber));
        controls->plugin.onClick = [this, i] { showTrackPluginMenu(i); };
        controls->mix.setTooltip("Track volume, pan, sends and routing");
        controls->mix.onClick = [this, i] { showTrackMix(i); };
        controls->preset.setFont(juce::FontOptions(10.0f));
        controls->preset.setColour(juce::Label::textColourId, juce::Colour(muted));
        controls->preset.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(controls->mute);
        addAndMakeVisible(controls->solo);
        addAndMakeVisible(controls->volume);
        addAndMakeVisible(controls->plugin);
        addAndMakeVisible(controls->mix);
        addAndMakeVisible(controls->preset);
        controls->effects.button.onClick = [this, i] { showEffects(false, i); };
        updateEffectButton(controls->effects);
        addAndMakeVisible(controls->effects.button);
        if (project_->trackKinds[i] == ostfstudio::TrackKind::drum) controls->midiChannel = 10;
        if (project_->trackKinds[i] == ostfstudio::TrackKind::audio) {
            controls->plugin.setButtonText("Link WAV");
            controls->plugin.setTooltip("Link a WAV file to this audio track");
            controls->plugin.onClick = [this, i] { linkAudio(i); };
        }
        trackControls_.push_back(std::move(controls));
    }
    resized();
}

void MainComponent::showTrackPluginMenu(std::size_t index) {
    auto& control = *trackControls_[index];
    juce::PopupMenu menu;
    constexpr int channelId = 100;
    constexpr int outputId = 200;
    juce::PopupMenu channels;
    for (int channel = 1; channel <= 16; ++channel)
        channels.addItem(channelId + channel, juce::String(channel), true, control.midiChannel == channel);
    menu.addSubMenu("MIDI channel " + juce::String(control.midiChannel), channels);
    juce::PopupMenu outputs;
    outputs.addItem(outputId, "This track", true, !control.outputTrack);
    const auto sourceLimit = std::min(PreviewTransport::maxTracks, trackControls_.size());
    for (std::size_t i = 0; i < sourceLimit; ++i) {
        if (i != index && trackControls_[i]->playableInstrument && trackControls_[i]->instance->acceptsMidi())
            outputs.addItem(outputId + 1 + static_cast<int>(i),
                            juce::String::fromUTF8(project_->trackLanes[i].label) + " / "
                            + trackControls_[i]->instance->getName(), true, control.outputTrack == i);
    }
    menu.addSubMenu("Instrument destination", outputs);
    menu.addSeparator();
    menu.addItem(8, "Link WAV on this track...", !control.loading);
    menu.addItem(1, "Choose plug-in file...", !control.loading);
    menu.addItem(3, "Choose plug-in bundle folder...", !control.loading);
    menu.addItem(4, "Load selected VST3", control.candidatePath.hasFileExtension("vst3") && !control.loading);
    menu.addItem(5, "Open plug-in editor", control.instance != nullptr && !control.loading);
    menu.addSeparator();
    menu.addItem(2, "Clear assignment", control.pluginPath != juce::File{} && !control.loading);
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&control.plugin),
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_](int selection) {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            if (selection == 1 || selection == 3) self->chooseTrackPlugin(index, selection == 3);
            if (selection == 4) self->scanTrackPlugin(index);
            if (selection == 5) self->openTrackPluginEditor(index);
            if (selection == 8) self->linkAudio(index);
            if (selection > channelId && selection <= channelId + 16) {
                self->stopTransport();
                self->trackControls_[index]->midiChannel = selection - channelId;
            }
            if (selection >= outputId) {
                self->stopTransport();
                auto& route = self->trackControls_[index]->outputTrack;
                if (selection == outputId) route.reset();
                else route = static_cast<std::size_t>(selection - outputId - 1);
            }
            if (selection == 2) {
                self->stopTransport();
                auto& strip = *self->trackControls_[index];
                strip.editor.reset();
                strip.instance.reset();
                strip.playableInstrument = false;
                strip.pluginPath = juce::File{};
                strip.candidatePath = juce::File{};
                strip.plugin.setButtonText("Instrument");
                strip.plugin.setTooltip("Assign a local plug-in to this track");
                self->updatePresetLabel(index);
                self->statusLabel_.setText("Plug-in assignment cleared", juce::dontSendNotification);
            }
        });
}

void MainComponent::linkAudio(std::size_t index) {
    chooser_ = std::make_unique<juce::FileChooser>("Link WAV at the start of the track", juce::File{}, "*.wav");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_](const juce::FileChooser& picker) {
            if (!safe || safe->projectGeneration_ != generation) return;
            const auto file = picker.getResult();
            if (file == juce::File{}) return;
            auto& audio = safe->project_->audio;
            const auto entry = std::find_if(audio.begin(), audio.end(), [index](const auto& item) { return item.track == index; });
            if (entry == audio.end() && audio.size() >= 8) return;
            safe->stopTransport();
            ostfstudio::ProjectAudio linked{index, file.getFileName(), file, {}};
            if (entry == audio.end()) audio.push_back(std::move(linked));
            else *entry = std::move(linked);
            safe->statusLabel_.setText("Linked at song start: " + file.getFileName(), juce::dontSendNotification);
            safe->repaint();
        });
}

void MainComponent::chooseTrackPlugin(std::size_t index, bool bundleDirectory) {
#if JUCE_MAC
    const auto directory = juce::File("/Library/Audio/Plug-Ins");
    const auto filter = "*.vst3;*.component";
#else
    const auto commonFiles = juce::SystemStats::getEnvironmentVariable("CommonProgramFiles", {});
    const auto directory = commonFiles.isEmpty() ? juce::File{} : juce::File(commonFiles).getChildFile("VST3");
    const auto filter = "*.vst3";
#endif
    pluginChooser_ = std::make_unique<juce::FileChooser>("Assign a local plug-in", directory, filter);
    const auto selectionMode = bundleDirectory ? juce::FileBrowserComponent::canSelectDirectories
                                               : juce::FileBrowserComponent::canSelectFiles;
    pluginChooser_->launchAsync(juce::FileBrowserComponent::openMode | selectionMode,
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_, filter](const juce::FileChooser& picker) {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            const auto file = picker.getResult();
            if (file == juce::File{}) return;
            const bool supported = file.hasFileExtension("vst3")
#if JUCE_MAC
                || file.hasFileExtension("component")
#endif
                ;
            if (!file.exists() || !supported) {
                self->statusLabel_.setText("Choose a plug-in matching " + juce::String(filter), juce::dontSendNotification);
                return;
            }
            auto& strip = *self->trackControls_[index];
            strip.candidatePath = file;
            const auto detail = file.hasFileExtension("vst3") ? " / Choose Load VST3 to configure" : " / AU assignment only";
            self->statusLabel_.setText("Assigned " + file.getFileName() + detail, juce::dontSendNotification);
        });
}

void MainComponent::scanTrackPlugin(std::size_t index) {
    auto& strip = *trackControls_[index];
    if (strip.loading || !strip.candidatePath.exists() || !strip.candidatePath.hasFileExtension("vst3")) return;
    strip.loading = true;
    strip.plugin.setButtonText("Reading...");
    statusLabel_.setText("Reading VST3 components: " + strip.candidatePath.getFileName(), juce::dontSendNotification);
    juce::Timer::callAfterDelay(65, [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_] {
        if (self != nullptr && self->projectGeneration_ == generation) self->discoverTrackPlugin(index);
    });
}

void MainComponent::discoverTrackPlugin(std::size_t index) {
    auto& strip = *trackControls_[index];
    juce::OwnedArray<juce::PluginDescription> found;
    pluginFormats_.getFormat(0)->findAllTypesForFile(found, strip.candidatePath.getFullPathName());
    strip.loading = false;
    strip.plugin.setButtonText(strip.instance ? strip.instance->getName() : "Instrument");
    if (found.isEmpty()) {
        statusLabel_.setText("No VST3 component found in " + strip.candidatePath.getFileName(), juce::dontSendNotification);
        return;
    }
    if (found.size() == 1) {
        loadTrackPlugin(index, *found[0]);
        return;
    }
    juce::PopupMenu menu;
    std::vector<juce::PluginDescription> choices;
    for (const auto* description : found) {
        choices.push_back(*description);
        menu.addItem(static_cast<int>(choices.size()), description->name);
    }
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&strip.plugin),
        [self = juce::Component::SafePointer<MainComponent>(this), choices = std::move(choices),
         index, generation = projectGeneration_](int item) {
            if (self != nullptr && self->projectGeneration_ == generation && item > 0)
                self->loadTrackPlugin(index, choices[static_cast<std::size_t>(item - 1)]);
        });
}

void MainComponent::loadTrackPlugin(std::size_t index, const juce::PluginDescription& description) {
    auto& strip = *trackControls_[index];
    if (strip.loading) return;
    if (isOstfGenerator(description)) {
        statusLabel_.setText("Use OSTF Arranger to load an instrument and play the arrangement", juce::dontSendNotification);
        return;
    }
    if (!description.isInstrument) {
        appendEffect(false, index, description);
        return;
    }
    strip.loading = true;
    strip.plugin.setButtonText("Loading...");
    statusLabel_.setText("Loading plug-in: " + description.name, juce::dontSendNotification);
    juce::Timer::callAfterDelay(65,
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_, description] {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            self->pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512,
                [self, index, generation, description](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
                    if (self == nullptr || self->projectGeneration_ != generation) return;
                    auto& target = *self->trackControls_[index];
                    target.loading = false;
                    if (!instance) {
                        target.plugin.setButtonText(target.instance ? target.instance->getName() : "Instrument");
                        self->statusLabel_.setText("VST3 load failed: " + error, juce::dontSendNotification);
                        return;
                    }
                    self->stopTransport();
                    target.editor.reset();
                    if (description.isInstrument) instance->disableNonMainBuses();
                    target.playableInstrument = PreviewTransport::supportsInstrument(*instance);
                    target.pluginPath = juce::File(description.fileOrIdentifier);
                    target.outputTrack.reset();
                    target.instance = std::move(instance);
                    target.plugin.setButtonText(target.instance->getName());
                    self->updatePresetLabel(index);
                    self->openTrackPluginEditor(index);
                });
        });
}

void MainComponent::openTrackPluginEditor(std::size_t index) {
    auto& strip = *trackControls_[index];
    auto& instance = strip.instance;
    auto& pluginEditor = strip.editor;
    if (!instance) return;
    if (!pluginEditor) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorAndMakeActive());
        if (!editor) editor = std::make_unique<juce::GenericAudioProcessorEditor>(*instance);
        auto window = std::make_unique<PluginWindow>(instance->getName());
        if (isOstfArranger(instance->getPluginDescription()))
            window->setContentOwned(new GeneratorPanel(std::move(editor), *this), true);
        else
            window->setContentOwned(editor.release(), true);
        window->centreWithSize(window->getWidth(), window->getHeight());
        window->onClosed = [this, index] { updatePresetLabel(index); };
        pluginEditor = std::move(window);
    }
    pluginEditor->setVisible(true);
    pluginEditor->toFront(true);
    statusLabel_.setText(strip.playableInstrument ? "Instrument ready for track playback"
                                                 : "Plug-in editor ready / audio routing unavailable for this plug-in",
                         juce::dontSendNotification);
}

void MainComponent::updatePresetLabel(std::size_t index) {
    auto& strip = *trackControls_[index];
    juce::String label;
    if (auto* instance = strip.instance.get()) {
        if (const auto program = instance->getCurrentProgram(); program >= 0 && program < instance->getNumPrograms()) {
            label = instance->getProgramName(program);
        }
        if (label.isEmpty()) label = "Preset not reported";
    }
    strip.preset.setText(label, juce::dontSendNotification);
    strip.preset.setTooltip(label);
}

void MainComponent::showTrackMix(std::size_t index) {
    updatePresetLabel(index);
    auto& strip = *trackControls_[index];
    auto content = std::make_unique<TrackMixPanel>(mixer_, index, juce::String::fromUTF8(project_->trackLanes[index].label),
        strip.preset.getText(), [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_] {
            return safe != nullptr && safe->projectGeneration_ == generation;
        });
    juce::CallOutBox::launchAsynchronously(std::move(content), getLocalArea(&strip.mix, strip.mix.getLocalBounds()), this);
}

void MainComponent::setupOutputTracks() {
    for (std::size_t i = 0; i < outputControls_.size(); ++i) {
        auto& control = outputControls_[i];
        control.effects.button.onClick = [this, i] { showEffects(true, i); };
        updateEffectButton(control.effects);
        addChildComponent(control.effects.button);
        auto& volume = control.volume;
        volume.setName(outputName(i) + " volume");
        volume.setSliderStyle(juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 16);
        volume.setTextValueSuffix(" dB");
        volume.setRange(-60.0, 6.0, 0.1);
        volume.setValue(0.0, juce::dontSendNotification);
        volume.setDoubleClickReturnValue(true, 0.0);
        volume.onValueChange = [this, i] { mixer_.setOutputVolume(i, outputControls_[i].volume.getValue()); };
        addChildComponent(volume);
        control.mute.setClickingTogglesState(true);
        control.mute.setTooltip("Mute " + outputName(i));
        control.mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
        control.mute.onClick = [this, i] { mixer_.setOutputMuted(i, outputControls_[i].mute.getToggleState()); };
        addChildComponent(control.mute);
    }
}

MainComponent::EffectChain& MainComponent::effectChain(bool output, std::size_t index) {
    return output ? outputControls_[index].effects : trackControls_[index]->effects;
}

void MainComponent::updateEffectButton(EffectChain& chain) {
    chain.button.setButtonText("FX " + juce::String(static_cast<int>(chain.slots.size())));
    chain.button.setTooltip("Audio effects in signal order");
    chain.button.setEnabled(chain.loading == 0);
}

void MainComponent::showEffects(bool output, std::size_t index) {
    auto& chain = effectChain(output, index);
    if (chain.loading) return;
    const auto name = output ? outputName(index) : juce::String::fromUTF8(project_->trackLanes[index].label);
    const auto current = [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_]()
        -> MainComponent* {
        return safe && safe->projectGeneration_ == generation ? safe.getComponent() : nullptr;
    };
    auto panel = std::make_unique<EffectChainPanel>(name,
        [current, output, index] {
            EffectChainPanel::State state;
            if (auto* self = current()) {
                const auto& slots = self->effectChain(output, index);
                state.busy = slots.loading != 0;
                for (const auto& slot : slots.slots) {
                    const auto name = slot.effect ? slot.effect->getName()
                        : slot.id.upToLastOccurrenceOf("|", false, false).fromLastOccurrenceOf("|", false, false)
                            + (slot.id.isEmpty() ? "Loading..." : " (unavailable)");
                    state.entries.push_back({name, slot.effect != nullptr});
                }
            }
            return state;
        },
        [current, output, index](int from, int to) {
            auto* self = current();
            if (!self) return;
            auto& chain = self->effectChain(output, index);
            if (chain.loading || from < 0 || to < 0 || static_cast<std::size_t>(std::max(from, to)) >= chain.slots.size()
                || from == to) return;
            self->stopTransport();
            auto first = chain.slots.begin();
            if (from < to) std::rotate(first + from, first + from + 1, first + to + 1);
            else std::rotate(first + to, first + from, first + from + 1);
        },
        [current, output, index](int position) {
            auto* self = current();
            if (!self) return;
            auto& chain = self->effectChain(output, index);
            if (chain.loading || static_cast<std::size_t>(position) >= chain.slots.size()) return;
            auto& slot = chain.slots[static_cast<std::size_t>(position)];
            if (!slot.effect) return;
            if (!slot.editor) {
                std::unique_ptr<juce::AudioProcessorEditor> editor(slot.effect->createEditorAndMakeActive());
                if (!editor) editor = std::make_unique<juce::GenericAudioProcessorEditor>(*slot.effect);
                auto window = std::make_unique<PluginWindow>(slot.effect->getName());
                window->setContentOwned(editor.release(), true);
                window->centreWithSize(window->getWidth(), window->getHeight());
                slot.editor = std::move(window);
            }
            slot.editor->setVisible(true);
            slot.editor->toFront(true);
        },
        [current, output, index](int position) {
            auto* self = current();
            if (!self) return;
            auto& chain = self->effectChain(output, index);
            if (chain.loading || static_cast<std::size_t>(position) >= chain.slots.size()) return;
            self->stopTransport();
            auto slot = chain.slots.begin() + position;
            slot->editor.reset();
            slot->effect.reset();
            chain.slots.erase(slot);
            self->updateEffectButton(chain);
        },
        [current, output, index](juce::Component& target) {
            auto* self = current();
            if (!self) return;
            juce::PopupMenu menu;
            juce::Array<juce::PluginDescription> effects;
            for (const auto& description : self->ensurePluginBrowser().types()) {
                if (description.isInstrument || description.pluginFormatName != "VST3") continue;
                effects.add(description);
                menu.addItem(effects.size(), description.manufacturerName + " / " + description.name);
            }
            if (effects.isEmpty()) menu.addItem(1, "Scan VST3 effects in the Plug-in Browser", false);
            menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&target),
                [current, output, index, effects = std::move(effects)](int choice) {
                    if (auto* owner = current(); owner && choice > 0 && choice <= effects.size())
                        owner->appendEffect(output, index, effects[choice - 1]);
                });
        },
        [current] {
            if (auto* self = current(); self && !self->pluginBrowserButton_.getToggleState()) self->togglePluginBrowser();
        });
    juce::CallOutBox::launchAsynchronously(std::move(panel), getLocalArea(&chain.button, chain.button.getLocalBounds()), this);
}

void MainComponent::appendEffect(bool output, std::size_t index, const juce::PluginDescription& description) {
    auto& chain = effectChain(output, index);
    if (chain.loading || chain.slots.size() >= ostfstudio::SessionMix::maxEffects || description.isInstrument) return;
    chain.slots.emplace_back();
    loadEffect(output, index, chain.slots.size() - 1, description);
}

void MainComponent::restoreEffects(bool output, std::size_t index, const juce::StringArray& ids) {
    if (ids.isEmpty()) return;
    auto& chain = effectChain(output, index);
    const auto types = ensurePluginBrowser().types();
    chain.slots.resize(static_cast<std::size_t>(ids.size()));
    for (int i = 0; i < ids.size(); ++i) {
        chain.slots[static_cast<std::size_t>(i)].id = ids[i];
        for (const auto& type : types)
            if (effectId(type) == ids[i]) { loadEffect(output, index, static_cast<std::size_t>(i), type); break; }
    }
    updateEffectButton(chain);
}

void MainComponent::loadEffect(bool output, std::size_t index, std::size_t position, const juce::PluginDescription& description) {
    auto& chain = effectChain(output, index);
    ++chain.loading;
    updateEffectButton(chain);
    pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512,
        [safe = juce::Component::SafePointer<MainComponent>(this), output, index, position,
         generation = projectGeneration_, description](std::unique_ptr<juce::AudioPluginInstance> effect, const juce::String& error) {
            if (!safe || safe->projectGeneration_ != generation) return;
            auto& chain = safe->effectChain(output, index);
            --chain.loading;
            auto& slot = chain.slots[position];
            if (effect) effect->disableNonMainBuses();
            if (!effect || !PreviewTransport::supportsInsert(*effect)) {
                if (slot.id.isEmpty()) chain.slots.pop_back();
                safe->statusLabel_.setText(effect ? "Insert requires a stereo audio effect" : "Could not load FX: " + error,
                                           juce::dontSendNotification);
            } else {
                safe->stopTransport();
                slot.effect = std::move(effect);
                slot.id = effectId(description);
            }
            safe->updateEffectButton(chain);
        });
}

void MainComponent::mouseDown(const juce::MouseEvent& event) {
    pendingMidiDrag_.reset();
    for (std::size_t i = 0; i < trackRows_.size(); ++i) {
        if (trackRows_[i].contains(event.getPosition())) {
            if (event.mods.isLeftButtonDown() && trackRows_[i].withWidth(trackNameWidth).contains(event.getPosition())
                && canExportTrack(i))
                pendingMidiDrag_ = i;
            selectTrack(i);
            return;
        }
    }
}

void MainComponent::mouseDrag(const juce::MouseEvent& event) {
    if (!pendingMidiDrag_ || event.getDistanceFromDragStart() < 5) return;
    const auto track = *pendingMidiDrag_;
    pendingMidiDrag_.reset();
    const auto error = MidiDragButton::dragMidi(trackMidiFile_.getFile(), this,
        [this, track](juce::OutputStream& stream) { return writeTrackMidi(track, stream); });
    if (error.isNotEmpty()) statusLabel_.setText(error, juce::dontSendNotification);
}

void MainComponent::selectTrack(std::size_t index) {
    if (!project_ || index >= project_->trackLanes.size()) return;
    selectedTrack_ = index;
    trackPicker_.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification);
    mixerButton_.setToggleState(false, juce::dontSendNotification);
    refreshEditor();
}

void MainComponent::refreshEditor() {
    const bool showMixer = mixerButton_.getToggleState();
    pianoButton_.setToggleState(!showMixer, juce::dontSendNotification);
    pianoButton_.setVisible(project_ != nullptr);
    mixerButton_.setVisible(project_ != nullptr);
    trackPicker_.setVisible(project_ != nullptr);
    performanceLane_.setVisible(project_ && selectedTrack_ && !showMixer);
    const auto previous = performanceLane_.getText();
    performanceLaneNames_ = juce::StringArray{"Velocity"};
    if (project_ && selectedTrack_ && *selectedTrack_ < project_->performanceCurves.size())
        for (const auto& curve : project_->performanceCurves[*selectedTrack_])
            performanceLaneNames_.addIfNotAlreadyThere(curve.label);
    performanceLane_.clear(juce::dontSendNotification);
    for (int i = 0; i < performanceLaneNames_.size(); ++i) performanceLane_.addItem(performanceLaneNames_[i], i + 1);
    performanceLane_.setSelectedId(std::max(1, performanceLaneNames_.indexOf(previous) + 1), juce::dontSendNotification);
    midiDrag_.setVisible(project_ != nullptr && !showMixer);
    midiDrag_.setEnabled(selectedTrack_ && canExportTrack(*selectedTrack_));
    mixer_.setVisible(project_ != nullptr && showMixer);
    resized();
    repaint();
}

bool MainComponent::canExportTrack(std::size_t track) const {
    return project_ && track < project_->pitchedNotes.size() && !project_->pitchedNotes[track].empty()
        && project_->ppq > 0 && project_->ppq <= 32767;
}

bool MainComponent::writeTrackMidi(std::size_t track, juce::OutputStream& stream) const {
    if (!canExportTrack(track)) return false;
    juce::MidiMessageSequence sequence;
    for (const auto& note : project_->pitchedNotes[track]) {
        if (note.startTick < 0 || note.endTick > 0x0fffffff) return false;
        sequence.addEvent(juce::MidiMessage::noteOff(1, note.pitch), static_cast<double>(note.endTick));
    }
    for (const auto& note : project_->pitchedNotes[track])
        sequence.addEvent(juce::MidiMessage::noteOn(1, note.pitch, static_cast<juce::uint8>(note.velocity.value_or(92))),
                          static_cast<double>(note.startTick));
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(static_cast<int>(project_->ppq));
    midi.addTrack(sequence);
    return midi.writeTo(stream, 1);
}

void MainComponent::resized() {
    menuBar_.setBounds(getLocalBounds().removeFromTop(menuHeight));
    auto transportRow = getLocalBounds().withTrimmedTop(menuHeight + 14).removeFromTop(28).withTrimmedLeft(221).withTrimmedRight(18);
    playButton_.setBounds(transportRow.removeFromLeft(54).reduced(2, 0));
    stopButton_.setBounds(transportRow.removeFromLeft(54).reduced(2, 0));
    recordButton_.setBounds(transportRow.removeFromLeft(94).reduced(2, 0));
    transportPosition_.setBounds(transportRow.removeFromLeft(72));
    pluginBrowserButton_.setBounds(transportRow.removeFromRight(78));
    statusLabel_.setBounds(20, getHeight() - 37, getWidth() - 40, 24);
    auto editor = getWorkspace();
    if (pluginBrowser_) {
        const auto full = workspaceBounds(getLocalBounds());
        pluginBrowser_->setBounds(full.withLeft(editor.getRight() + 8));
    }
    const auto arrangerHeight = getArrangementHeight(editor.getHeight());
    auto arrangement = editor.withHeight(arrangerHeight);
    arrangement = arrangement.reduced(13);
    const auto outputs = visibleOutputCount();
    for (std::size_t i = 0; i < outputControls_.size(); ++i) {
        outputControls_[i].effects.button.setVisible(i < outputs);
        outputControls_[i].volume.setVisible(i < outputs);
        outputControls_[i].mute.setVisible(i < outputs);
    }
    if (project_) {
        const auto view = currentArrangement();
        const auto height = arrangementLaneHeight(arrangement, view, outputs);
        const int trackY = arrangement.getY() + (1 + static_cast<int>(view.globalLanes.size())) * height + arrangementHeadingHeight;
        for (std::size_t i = 0; i < trackControls_.size(); ++i) {
            auto row = juce::Rectangle<int>(arrangement.getX() + trackNameWidth, trackY + static_cast<int>(i) * height,
                                            trackHeaderWidth - trackNameWidth - 3, height - 4);
            auto& strip = *trackControls_[i];
            strip.mute.setBounds(row.removeFromLeft(29).reduced(2));
            strip.solo.setBounds(row.removeFromLeft(29).reduced(2));
            strip.volume.setBounds(row.removeFromLeft(74).reduced(2, 0));
            strip.mix.setBounds(row.removeFromLeft(36).reduced(1));
            strip.effects.button.setBounds(row.removeFromRight(54).reduced(2, 1));
            auto instrument = row.reduced(1);
            strip.preset.setBounds(instrument.removeFromBottom(std::min(13, instrument.getHeight() / 2)));
            strip.plugin.setBounds(instrument);
        }
        const int outputY = trackY + static_cast<int>(trackControls_.size()) * height + arrangementHeadingHeight;
        for (std::size_t i = 0; i < outputs; ++i) {
            outputRows_[i] = {arrangement.getX(), outputY + static_cast<int>(i) * height, arrangement.getWidth(), height - 4};
            auto header = outputRows_[i].withTrimmedLeft(trackNameWidth).withWidth(trackHeaderWidth - trackNameWidth);
            outputControls_[i].mute.setBounds(header.removeFromLeft(29).reduced(2));
            outputControls_[i].volume.setBounds(header.removeFromLeft(145).reduced(4, 0));
            outputControls_[i].effects.button.setBounds(header.removeFromRight(54).reduced(2, 1));
        }
    }
    editor.removeFromTop(arrangerHeight + 8);
    editor.reduce(6, 4);
    auto toolbar = editor.removeFromTop(26);
    if (midiDrag_.isVisible()) midiDrag_.setBounds(toolbar.removeFromRight(192));
    pianoButton_.setBounds(toolbar.removeFromLeft(96));
    toolbar.removeFromLeft(5);
    mixerButton_.setBounds(toolbar.removeFromLeft(74));
    toolbar.removeFromLeft(12);
    const auto pickerWidth = performanceLane_.isVisible() ? juce::jmin(180, toolbar.getWidth() / 2) : 180;
    trackPicker_.setBounds(toolbar.removeFromLeft(pickerWidth));
    toolbar.removeFromLeft(6);
    performanceLane_.setBounds(toolbar.removeFromLeft(250).withTrimmedRight(4));
    mixer_.setBounds(editor);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(bg));
    auto area = getLocalBounds().reduced(18);
    area.removeFromTop(menuHeight);
    auto top = area.removeFromTop(54);
    auto title = top.removeFromLeft(190);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    g.drawText("OSTF Studio", title.removeFromTop(29), juce::Justification::centredLeft);
    g.setColour(juce::Colour(muted));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(project_ ? "GLOBAL MUSICAL TIMELINE" : "GLOBAL MUSICAL TIMELINE / DEMO", title, juce::Justification::centredLeft, 1);
    area = getWorkspace();
    trackRows_.clear();
    if (!project_) {
        drawArrangement(g, area, currentArrangement());
        return;
    }

    auto arrangementArea = area.removeFromTop(getArrangementHeight(area.getHeight()));
    area.removeFromTop(8);
    drawArrangement(g, arrangementArea, currentArrangement());
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff303840), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(panel), 0.0f, static_cast<float>(area.getBottom()), false));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.removeFromTop(30);
    if (!mixerButton_.getToggleState()) drawPianoRoll(g, area);
}

void MainComponent::drawArrangement(juce::Graphics& g, juce::Rectangle<int> area,
                                    const ostfstudio::ArrangementView& view) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff313941), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(panel), 0.0f, static_cast<float>(area.getBottom()), false));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 13);
    const auto outputs = visibleOutputCount();
    const int laneHeight = arrangementLaneHeight(area, view, outputs);
    auto labels = area.removeFromLeft(project_ ? trackHeaderWidth : 108);
    if (laneHeight < 12 || area.getWidth() < 40) return;
    const int rowHeight = laneHeight - 4;

    auto drawRow = [&](const char* label, int y) {
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(juce::String::fromUTF8(label),
                         {labels.getX() + 5, y, trackNameWidth - 10, rowHeight},
                         juce::Justification::centredLeft, 1);
        const auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff233443));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        return row;
    };

    const auto bars = view.bars;
    auto clipBounds = [bars](ostfstudio::BarSpan clip, juce::Rectangle<int> row) {
        const auto extent = ostfstudio::projectBarSpan(clip, bars);
        if (!extent) return juce::Rectangle<int>{};
        const auto left = juce::roundToInt(extent->start * row.getWidth());
        const auto right = juce::roundToInt(extent->end * row.getWidth());
        return juce::Rectangle<int>(row.getX() + left, row.getY(), right - left, row.getHeight());
    };

    const int tickStep = juce::jmax(1, (bars.endBar - bars.firstBar) / (area.getWidth() < 420 ? 4 : 8));
    const auto ruler = drawRow("BARS", area.getY());
    for (int bar = bars.firstBar; bar < bars.endBar; bar += tickStep) {
        const auto cell = clipBounds({bar, bar + tickStep}, ruler);
        g.setColour(juce::Colour(muted));
        g.drawText(juce::String(bar), cell.withTrimmedLeft(5), juce::Justification::centredLeft);
    }

    auto drawLane = [&](const ostfstudio::ArrangementLane& lane, int y, juce::Colour colour) {
        const auto row = drawRow(lane.label, y);
        g.setColour(juce::Colour(0xff3a4e5e));
        for (int bar = bars.firstBar + tickStep; bar < bars.endBar; bar += tickStep) {
            const auto cell = clipBounds({bar, bar + tickStep}, row);
            g.drawVerticalLine(cell.getX(), static_cast<float>(row.getY()), static_cast<float>(row.getBottom()));
        }

        for (const auto& region : lane.regions) {
            const auto bounds = clipBounds(region.bars, row).reduced(2, 2);
            if (bounds.isEmpty()) continue;
            g.setColour(colour);
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.93f));
            auto label = juce::String::fromUTF8(region.label);
            if (region.noteCount > 0) label = "[" + juce::String(region.noteCount) + " notes] " + label;
            g.drawFittedText(label, bounds.reduced(4, 0), juce::Justification::centredLeft, 1);
        }
    };

    int y = area.getY() + laneHeight;
    const juce::uint32 globalColours[] {0xff314d72, 0xff493c64, 0xff31495a, 0xff31404e, 0xff31404e, 0xff2c6058};
    for (std::size_t i = 0; i < view.globalLanes.size(); ++i) {
        drawLane(view.globalLanes[i], y, juce::Colour(globalColours[i % std::size(globalColours)]));
        y += laneHeight;
    }

    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(project_ ? "ARRANGEMENT TRACKS / OSTFP" : "ARRANGEMENT TRACKS / DEMO", labels.getX() + 4, y,
               labels.getWidth() + area.getWidth() - 4, arrangementHeadingHeight, juce::Justification::centredLeft);
    y += arrangementHeadingHeight;

    const juce::uint32 trackColours[] {0xff2a657d, 0xff265e57, 0xff4b517f, 0xff744366, 0xff526070};
    for (std::size_t i = 0; i < view.trackLanes.size(); ++i) {
        const auto channel = juce::Rectangle<int>(labels.getX(), y, labels.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff353d45));
        g.fillRoundedRectangle(channel.toFloat().reduced(1.0f), 3.0f);
        g.setColour(juce::Colour(0xff525d67));
        g.drawHorizontalLine(y + 1, static_cast<float>(channel.getX() + 4), static_cast<float>(channel.getRight() - 4));
        drawLane(view.trackLanes[i], y, juce::Colour(trackColours[i % std::size(trackColours)]));
        if (project_) {
            const auto audio = std::find_if(project_->audio.begin(), project_->audio.end(), [i](const auto& item) { return item.track == i; });
            if (audio != project_->audio.end()) {
                const auto label = audio->embedded.isEmpty() && !audio->localFile.existsAsFile() ? "Missing WAV" : "WAV";
                g.setColour(juce::Colour(0xffe7b46c));
                g.drawText(label, area.getX() + 6, y, 80, rowHeight, juce::Justification::centredLeft);
            }
            const juce::Rectangle<int> hit(labels.getX(), y, labels.getWidth() + area.getWidth(), rowHeight);
            trackRows_.push_back(hit);
            if (selectedTrack_ == i) {
                g.setColour(juce::Colour(accent));
                g.drawRoundedRectangle(hit.toFloat().reduced(0.5f), 4.0f, 1.5f);
            }
            if (pluginDropHighlight_ == i) {
                g.setColour(juce::Colour(0xffe7b46c));
                g.drawRoundedRectangle(hit.toFloat().reduced(1.5f), 4.0f, 2.5f);
            }
        }
        y += laneHeight;
    }
    if (outputs) {
        g.setColour(juce::Colour(accent));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText("OUTPUT TRACKS", labels.getX() + 4, y, labels.getWidth(), arrangementHeadingHeight,
                   juce::Justification::centredLeft);
        y += arrangementHeadingHeight;
        for (std::size_t i = 0; i < outputs; ++i) {
            const auto row = outputRows_[i];
            g.setColour(juce::Colour(0xff343c46));
            g.fillRoundedRectangle(row.toFloat(), 4.0f);
            g.setColour(juce::Colour(hardwareAmber));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawFittedText(outputName(i), row.withWidth(trackNameWidth).reduced(5, 0), juce::Justification::centredLeft, 1);
            g.setColour(juce::Colour(muted));
            g.drawFittedText(i == 0 ? "Out 1-2" : "To Master", row.withTrimmedLeft(trackHeaderWidth).reduced(8, 0),
                             juce::Justification::centredLeft, 1);
            if (outputDropHighlight_ == i) {
                g.setColour(juce::Colour(hardwareAmber));
                g.drawRoundedRectangle(row.toFloat().reduced(1.0f), 4.0f, 2.0f);
            }
            y += laneHeight;
        }
    }
    if (transportActive_ && project_) {
        const auto ticks = static_cast<double>(project_->ticksPerBar) * (bars.endBar - bars.firstBar);
        const auto x = area.getX() + juce::roundToInt(static_cast<double>(transport_.positionTick()) / ticks * area.getWidth());
        g.setColour(juce::Colour(hardwareAmber));
        g.drawVerticalLine(x, static_cast<float>(ruler.getY()), static_cast<float>(y - 4));
    }
}

void MainComponent::drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 8);
    const auto heading = area.removeFromTop(24);
    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    const auto name = selectedTrack_ ? "PIANO ROLL  /  " + juce::String::fromUTF8(project_->trackLanes[*selectedTrack_].label)
                                     : juce::String("PIANO ROLL");
    g.drawFittedText(name, heading, juce::Justification::centredLeft, 1);
    if (!selectedTrack_) {
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("Select an arrangement track to inspect its notes.",
                   area, juce::Justification::centred);
        return;
    }

    const auto& notes = project_->pitchedNotes[*selectedTrack_];
    const auto laneName = performanceLane_.getText();
    const auto& curves = project_->performanceCurves[*selectedTrack_];
    auto firstTick = notes.empty() ? 0 : notes.front().startTick;
    auto lastTick = notes.empty() ? project_->ticksPerBar : notes.front().endTick;
    int low = notes.empty() ? 55 : notes.front().pitch;
    int high = notes.empty() ? 67 : low;
    for (const auto& note : notes) {
        firstTick = std::min(firstTick, note.startTick);
        lastTick = std::max(lastTick, note.endTick);
        low = std::min(low, note.pitch);
        high = std::max(high, note.pitch);
    }
    for (const auto& curve : curves) if (curve.label == laneName && !curve.points.empty()) {
        firstTick = std::min(firstTick, curve.points.front().tick);
        lastTick = std::max(lastTick, curve.points.back().tick + 1);
    }
    low = std::max(0, low - 3);
    high = std::min(127, high + 3);
    const auto barLength = project_->ticksPerBar;
    const auto firstBar = firstTick / barLength;
    const auto endBar = (lastTick - 1) / barLength + 1;
    const auto origin = firstBar * barLength;
    const auto length = static_cast<double>((endBar - firstBar) * barLength);
    auto lane = area.removeFromBottom(std::min(84, area.getHeight() / 3));
    auto keys = area.removeFromLeft(52);
    lane.removeFromLeft(52);
    const auto ruler = area.removeFromTop(18);
    keys.removeFromTop(18);
    if (area.isEmpty()) return;

    const auto rowHeight = static_cast<float>(area.getHeight()) / static_cast<float>(high - low + 1);
    auto noteY = [&](int pitch) { return static_cast<float>(area.getY()) + static_cast<float>(high - pitch) * rowHeight; };
    auto tickX = [&](juce::int64 tick) {
        return static_cast<float>(area.getX() + static_cast<double>(tick - origin) / length * area.getWidth());
    };
    constexpr bool isBlack[] {false, true, false, true, false, false, true, false, true, false, true, false};
    constexpr const char* pitchNames[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    g.setFont(juce::FontOptions(10.0f));
    for (int pitch = high; pitch >= low; --pitch) {
        const auto y = noteY(pitch);
        const bool black = isBlack[pitch % 12];
        g.setColour(juce::Colour(black ? 0xff192735 : 0xff233443));
        g.fillRect(static_cast<float>(area.getX()), y, static_cast<float>(area.getWidth()), rowHeight);
        const auto key = juce::Rectangle<float>(static_cast<float>(keys.getX()), y, static_cast<float>(keys.getWidth() - 2), rowHeight);
        g.setColour(juce::Colour(black ? 0xff243242 : 0xff526578));
        g.fillRect(key.reduced(0, 0.5f));
        if (rowHeight >= 11.0f || (pitch % 12 == 0 && rowHeight * 12.0f >= 12.0f)) {
            g.setColour(juce::Colours::white);
            g.drawText(juce::String(pitchNames[pitch % 12]) + juce::String(pitch / 12 - 1),
                       key.toNearestInt(), juce::Justification::centred);
        }
    }

    const auto barStep = std::max<juce::int64>(1, (endBar - firstBar + 7) / 8);
    for (auto bar = firstBar; bar < endBar; bar += barStep) {
        const auto x = juce::roundToInt(tickX(bar * barLength));
        g.setColour(juce::Colour(0xff526578));
        g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
        g.setColour(juce::Colour(muted));
        g.drawText(juce::String(bar + 1), x + 3, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft);
    }

    g.saveState();
    g.reduceClipRegion(area);
    g.setColour(juce::Colour(accent));
    for (const auto& note : notes) {
        const auto x = tickX(note.startTick);
        const auto width = std::max(2.0f, tickX(note.endTick) - x);
        g.fillRoundedRectangle(x, noteY(note.pitch), width, std::max(1.0f, rowHeight - 1.0f), 2.0f);
    }
    g.restoreState();

    if (lane.getHeight() < 20) return;
    g.setColour(juce::Colour(0xff192735));
    g.fillRect(lane);
    const auto title = lane.removeFromTop(16);
    g.setColour(juce::Colour(muted));
    g.drawFittedText(laneName, title.reduced(4, 0), juce::Justification::centredLeft, 1);
    for (auto bar = firstBar; bar < endBar; bar += barStep)
        g.drawVerticalLine(juce::roundToInt(tickX(bar * barLength)), static_cast<float>(lane.getY()),
                           static_cast<float>(lane.getBottom()));
    g.saveState();
    g.reduceClipRegion(lane);
    const auto yFor = [&](double value) {
        return static_cast<float>(lane.getBottom() - 1 - value * (lane.getHeight() - 3));
    };
    g.setColour(juce::Colour(accent));
    bool any = false;
    if (laneName == "Velocity") {
        for (const auto& note : notes) {
            if (!note.velocity) continue;
            any = true;
            const auto x = tickX(note.startTick);
            g.drawLine(x, yFor(0), x, yFor(*note.velocity / 127.0), 2.0f);
        }
    } else {
        double centsRange = 100.0;
        for (const auto& curve : curves) if (curve.label == laneName && curve.cents)
            for (const auto& point : curve.points) centsRange = std::max(centsRange, std::abs(point.value));
        for (const auto& curve : curves) {
            if (curve.label != laneName) continue;
            juce::Path line;
            bool firstPoint = true;
            float lastY = 0.0f;
            for (const auto& point : curve.points) {
                const auto x = tickX(point.tick);
                const auto y = yFor(curve.cents ? 0.5 + 0.5 * point.value / centsRange : point.value);
                if (firstPoint) line.startNewSubPath(x, y);
                else {
                    if (curve.stepped) line.lineTo(x, lastY);
                    line.lineTo(x, y);
                }
                lastY = y;
                firstPoint = false;
                g.fillEllipse(x - 2.0f, y - 2.0f, 4.0f, 4.0f);
                any = true;
            }
            if (curve.isCurve) g.strokePath(line, juce::PathStrokeType(1.4f));
        }
    }
    if (!any) {
        g.setColour(juce::Colour(muted));
        g.drawText("No authored values", lane, juce::Justification::centred);
    }
    g.restoreState();
}
