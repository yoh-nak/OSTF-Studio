#include "ui/MainComponent.h"
#include "ui/DemoArrangement.h"
#include "core/MidiGeneratorPattern.h"
#include "ui/PluginRoles.h"
#include "ui/StudioLanguage.h"
#include "ui/SongMidi.h"
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
constexpr int viewBarHeight = 32;
constexpr int trackHeaderWidth = 410;
constexpr int timelineNavigationHeight = 24;
constexpr int trackNameWidth = 95;
constexpr int arrangementHeadingHeight = 24;

juce::String outputName(std::size_t index) {
    if (index == 0) return TRANS("Master");
    if (index == 1) return TRANS("Bus A");
    if (index == 2) return TRANS("Bus B");
    return TRANS("Return ") + juce::String(static_cast<int>(index - 2));
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
    bounds.removeFromTop(54 + menuHeight + viewBarHeight);
    bounds.removeFromBottom(26);
    return bounds;
}

enum MenuCommand { openFile = 1, openDemoSong, showPiano, showMixer, showDevices, showAbout, showPlugins,
                   saveScore, saveProject, showOutputs, addPitched, addDrums, addAudio, removeTrack,
                   exportMidi0, exportMidi1, languageSystem, languageEnglish, languageJapanese };
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
        title_.setText(track + TRANS(" / FX chain"), juce::dontSendNotification);
        title_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        direction_.setText(TRANS("Drag handles or use Up / Down. Audio flows top to bottom."), juce::dontSendNotification);
        direction_.setFont(juce::FontOptions(12.0f));
        for (auto* component : std::array<juce::Component*, 4>{&title_, &direction_, &add_, &browser_})
            addAndMakeVisible(*component);
        add_.onClick = [this, add = std::move(add)] { add(add_); };
        browser_.onClick = std::move(browser);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto& row = rows_[i];
            const auto index = static_cast<int>(i);
            row.handle.setTitle("Drag effect " + juce::String(index + 1));
            row.handle.setTooltip(TRANS("Drag to a new position in the chain"));
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
        juce::TextButton handle {"::"}, up {TRANS("Up")}, down {TRANS("Down")}, open {TRANS("Edit")}, remove {TRANS("Remove")};
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
    juce::TextButton add_ {TRANS("Add effect")}, browser_ {TRANS("Plug-in Browser")};
    int count_ = 0;
    std::optional<int> dragSource_, dragTarget_;
};

class GeneratorPanel final : public juce::Component {
public:
    GeneratorPanel(std::unique_ptr<juce::AudioProcessorEditor> editor, juce::DragAndDropContainer& dragHost)
        : editor_(std::move(editor)), dragHost_(dragHost) {
        addAndMakeVisible(*editor_);
        addAndMakeVisible(ostfDrag_);
        ostfDrag_.setTooltip(TRANS("Drop on an OSTF Studio arrangement track to add the pattern as editable OSTF notes."));
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
    juce::TextButton ostfDrag_ {TRANS("Drag notes to an OSTF Studio track")};
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
        output_.addItem(TRANS("Master"), 1);
        output_.addItem(TRANS("Bus A"), 2);
        output_.addItem(TRANS("Bus B"), 3);
        output_.setSelectedId(channel.outputBus + 2);
        for (std::size_t i = 0; i < mixer.returnCount(); ++i)
            target_.addItem(TRANS("Return ") + juce::String(static_cast<int>(i + 1)), static_cast<int>(i + 1));
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
            g.drawText(juce::translate(label), 12, y, 65, 29, juce::Justification::centredLeft);
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
    viewPreferences_.setStorageParameters(ostfstudio::viewSettingsOptions());
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
    const auto addView = [this](ViewButton& button, const char* label, int command) {
        const auto name = juce::translate(label);
        button.setName(name);
        button.setTitle(name);
        button.setTooltip(name);
        button.setToggleable(command != 0);
        addAndMakeVisible(button);
        button.onClick = [this, command] { menuItemSelected(command, 1); };
    };
    addView(pianoButton_, "Piano Roll", showPiano);
    addView(mixerButton_, "Mixer", showMixer);
    addView(pluginBrowserButton_, "Plug-in Browser", showPlugins);
    addView(outputButton_, "Output Tracks", showOutputs);
    addView(lanesButton_, "Global Timeline Lanes", 0);
    lanesButton_.onClick = [this] {
        timelineMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&lanesButton_),
            [safe = juce::Component::SafePointer<MainComponent>(this)](int id) { if (safe && id) safe->menuItemSelected(id, 1); });
    };
    addAndMakeVisible(transportPosition_);
    transportPosition_.setText(TRANS("Bar 1"), juce::dontSendNotification);
    playButton_.onClick = [this] { startTransport(false); };
    stopButton_.onClick = [this] { stopTransport(); };
    recordButton_.onClick = [this] { startTransport(true); };
    recordButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
    addChildComponent(trackPicker_);
    addChildComponent(performanceLane_);
    performanceLane_.setTitle(TRANS("Performance lane"));
    performanceLane_.onChange = [this] { repaint(); };
    for (auto* zoom : {&arrangementZoom_, &pianoZoom_}) {
        zoom->addItem(TRANS("Fit song"), 1);
        for (int bars : {1, 2, 4, 8, 16, 32, 64}) zoom->addItem(juce::String(bars) + (bars == 1 ? TRANS(" bar") : TRANS(" bars")), bars + 1);
        zoom->setSelectedId(1, juce::dontSendNotification);
        const bool piano = zoom == &pianoZoom_;
        zoom->setTitle(piano ? TRANS("Piano roll horizontal zoom") : TRANS("Arrangement horizontal zoom"));
        zoom->setTooltip(piano ? TRANS("Set how many bars are visible in the piano roll") : TRANS("Set how many bars are visible in the arrangement"));
        zoom->onChange = [this, piano] { updateHorizontalRange(piano); repaint(); };
        addChildComponent(zoom);
    }
    for (auto* scroll : {&arrangementScroll_, &pianoScroll_}) {
        scroll->setAutoHide(false);
        scroll->setSingleStepSize(1.0);
        scroll->addListener(this);
        addChildComponent(scroll);
    }
    addChildComponent(midiDrag_);
    midiDrag_.setButtonText(TRANS("Drag MIDI to another DAW"));
    midiDrag_.setTooltip(TRANS("Export the selected track as a MIDI file and drag it into another DAW."));
    midiDrag_.writeMidi = [this](juce::OutputStream& stream) { return selectedTrack_ && writeTrackMidi(*selectedTrack_, stream); };
    midiDrag_.reportError = [this](const juce::String& error) { statusLabel_.setText(error, juce::dontSendNotification); };
    addChildComponent(mixer_);
    setupOutputTracks();
    pluginFormats_.addFormat(std::make_unique<juce::VST3PluginFormat>());
    trackPicker_.setTitle(TRANS("Arrangement track"));
    trackPicker_.setTextWhenNothingSelected(TRANS("Select track"));
    trackPicker_.onChange = [this] {
        if (const auto id = trackPicker_.getSelectedId(); id > 0) selectTrack(static_cast<std::size_t>(id - 1));
    };
    mixer_.channelChanged = [this](std::size_t index) {
        auto& controls = *trackControls_[index];
        const auto& mix = mixer_.channel(index);
        controls.mute.setToggleState(mix.muted, juce::dontSendNotification);
        controls.solo.setToggleState(mix.soloed, juce::dontSendNotification);
        controls.volume.setValue(mix.volumeDb, juce::dontSendNotification);
        controls.pan.setValue(mix.pan * 100.0f, juce::dontSendNotification);
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
    menuItemSelected(demoMenuBase, 0);
}

juce::StringArray MainComponent::getMenuBarNames() { return {TRANS("File"), TRANS("View"), TRANS("Settings"), TRANS("Help"), TRANS("Track")}; }

juce::PopupMenu MainComponent::timelineMenu() const {
    juce::PopupMenu lanes;
    for (std::size_t i = 0; i < std::size(selectableTimelineLanes); ++i)
        lanes.addItem(timelineMenuBase + static_cast<int>(i), juce::translate(timelineMenuLabels[i]), true,
                      (timelineLaneMask_ & laneBit(selectableTimelineLanes[i])) != 0);
    return lanes;
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu menu;
    switch (index) {
        case 0: {
            menu.addItem(openFile, TRANS("Open OSTF / OSTFP..."));
            juce::PopupMenu demos;
            for (std::size_t i = 0; i < std::size(gallery); ++i)
                demos.addItem(demoMenuBase + static_cast<int>(i), gallery[i].title);
            demos.addSeparator();
            demos.addItem(openDemoSong, TRANS("Official specification example"));
            menu.addSubMenu(TRANS("Demo songs"), demos);
            menu.addSeparator();
            menu.addItem(saveScore, TRANS("Save as OSTF..."), project_ != nullptr);
            menu.addItem(saveProject, TRANS("Save as OSTFP..."), project_ != nullptr);
            juce::PopupMenu midi;
            midi.addItem(exportMidi0, TRANS("Format 0 - single track"));
            midi.addItem(exportMidi1, TRANS("Format 1 - separate tracks"));
            menu.addSubMenu(TRANS("Save as MIDI (SMF)"), midi, project_ != nullptr);
            break;
        }
        case 1: {
            menu.addItem(showPiano, TRANS("Piano Roll"), project_ != nullptr, pianoButton_.getToggleState());
            menu.addItem(showMixer, TRANS("Mixer"), project_ != nullptr, mixerButton_.getToggleState());
            menu.addItem(showPlugins, TRANS("Plug-in Browser"), true, pluginBrowserButton_.getToggleState());
            menu.addItem(showOutputs, TRANS("Output Tracks"), project_ != nullptr, showOutputTracks_);
            menu.addSubMenu(TRANS("Global Timeline Lanes"), timelineMenu());
            break;
        }
        case 2: {
            menu.addItem(showDevices, TRANS("Audio / MIDI Devices..."));
            juce::PopupMenu languages;
            const auto selected = viewPreferences_.getUserSettings()->getValue("language", "system");
            languages.addItem(languageSystem, TRANS("Use system language"), true, selected == "system");
            languages.addItem(languageEnglish, "English", true, selected == "en");
            languages.addItem(languageJapanese, juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"), true, selected == "ja");
            menu.addSubMenu(TRANS("Language"), languages);
            break;
        }
        case 3: menu.addItem(showAbout, TRANS("About OSTF Studio")); break;
        case 4: {
            const auto editable = canEditTracks();
            const auto add = editable && project_->sourceTrackCount() < ostfstudio::OstfpPreview::maxTracks;
            menu.addItem(addPitched, TRANS("Add pitched track"), add);
            menu.addItem(addDrums, TRANS("Add drum / percussion track"), add);
            menu.addItem(addAudio, TRANS("Add audio track"), add);
            menu.addSeparator();
            menu.addItem(removeTrack, TRANS("Delete selected track..."), editable && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
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
            statusLabel_.setText(TRANS("Bundled demo is unavailable"), juce::dontSendNotification);
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
        case saveScore: saveDocument(SaveFormat::score); break;
        case saveProject: saveDocument(SaveFormat::package); break;
        case exportMidi0: saveDocument(SaveFormat::midi0); break;
        case exportMidi1: saveDocument(SaveFormat::midi1); break;
        case languageSystem:
        case languageEnglish:
        case languageJapanese: {
            auto* settings = viewPreferences_.getUserSettings();
            settings->setValue("language", item == languageSystem ? "system" : item == languageEnglish ? "en" : "ja");
            const bool saved = settings->saveIfNeeded();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, juce::translate("Language"),
                juce::translate(saved ? "The selected language will be used the next time OSTF Studio starts."
                                      : "Could not save the language setting."));
            break;
        }
        case showPiano:
        case showMixer:
            editorVisible_ = !(item == showMixer ? mixerButton_ : pianoButton_).getToggleState();
            mixerButton_.setToggleState(editorVisible_ && item == showMixer, juce::dontSendNotification);
            refreshEditor();
            break;
        case showDevices:
            if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
            deviceSettings_->setVisible(true);
            deviceSettings_->toFront(true);
            break;
        case showPlugins: togglePluginBrowser(); break;
        case showOutputs: showOutputTracks_ = !showOutputTracks_; refreshEditor(); break;
        case addPitched: editTracks(ostfstudio::TrackKind::pitched); break;
        case addDrums: editTracks(ostfstudio::TrackKind::drum); break;
        case addAudio: editTracks(ostfstudio::TrackKind::audio); break;
        case removeTrack:
            if (canEditTracks() && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount()) {
                const auto index = *selectedTrack_;
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, TRANS("Delete track"),
                    TRANS("Remove {track} and its clips from this project? Audio files stay on disk.")
                        .replace("{track}", juce::String::fromUTF8(project_->trackLanes[index].label)),
                    TRANS("Delete track"), TRANS("Cancel"), this,
                    juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this),
                        index, generation = projectGeneration_](int confirmed) {
                        if (confirmed && safe && safe->projectGeneration_ == generation)
                            safe->editTracks(ostfstudio::TrackKind::pitched, index);
                    }));
            }
            break;
        case showAbout:
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                TRANS("About OSTF Studio"), "OSTF Studio " OSTF_STUDIO_DISPLAY_VERSION "\n"
                + TRANS("OSTF 5.0 timeline, piano roll and session mixer.") + "\n\n"
                + TRANS("Original Studio code: GNU GPL version 3. Provided without warranty.")
                + "\n" + TRANS("See the included Licenses folder and the corresponding source on GitHub Releases."));
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
    if (!editorVisible_) return workspaceHeight;
    const auto view = currentArrangement();
    const auto outputs = visibleOutputCount();
    const auto rows = 1 + static_cast<int>(view.globalLanes.size() + view.trackLanes.size() + outputs);
    const auto target = mixerButton_.getToggleState() ? workspaceHeight / 2 : workspaceHeight * 3 / 5;
    return std::max(target, 26 + (project_ ? timelineNavigationHeight : 0) + arrangementHeadingHeight + rows * 12
                   + (outputs ? arrangementHeadingHeight : 0));
}

void MainComponent::updateHorizontalRange(bool piano, bool reset) {
    if (!project_) return;
    auto& zoom = piano ? pianoZoom_ : arrangementZoom_;
    auto& scroll = piano ? pianoScroll_ : arrangementScroll_;
    const auto song = project_->bars;
    const auto length = song.endBar - song.firstBar;
    const auto visible = zoom.getSelectedId() <= 1 ? length : std::min(length, zoom.getSelectedId() - 1);
    const auto start = reset ? static_cast<double>(song.firstBar) : scroll.getCurrentRangeStart();
    scroll.setRangeLimits(song.firstBar, song.endBar, juce::dontSendNotification);
    scroll.setCurrentRange(start, visible, juce::dontSendNotification);
    scroll.setEnabled(visible < length);
}

ostfstudio::TimelineExtent MainComponent::horizontalRange(bool piano) const {
    const auto& scroll = piano ? pianoScroll_ : arrangementScroll_;
    return {scroll.getCurrentRangeStart(), scroll.getCurrentRange().getEnd()};
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

std::vector<std::size_t> MainComponent::displayedTrackOrder() const {
    std::vector<std::size_t> order;
    if (!project_) return order;
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i)
        if (project_->isGlobalNoteTrack(i) &&
            (project_->chordVoicingTrack != i || (timelineLaneMask_ & laneBit(TimelineLane::voicing)) != 0))
            order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [this](const auto a, const auto b) {
        if (project_->chordVoicingTrack == a) return project_->chordVoicingTrack != b;
        if (project_->chordVoicingTrack == b) return false;
        return project_->mixerGroups[a] < project_->mixerGroups[b];
    });
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i)
        if (!project_->isGlobalNoteTrack(i)) order.push_back(i);
    return order;
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
        const auto visible = horizontalRange(false);
        const auto x = juce::jlimit(0, timeline.getWidth() - 1, details.localPosition.x - timeline.getX());
        const auto bar = static_cast<int>(std::floor(visible.start + x * (visible.end - visible.start) / timeline.getWidth()));
        if (project_->insertGeneratorPattern(*track, bar)) {
            selectTrack(*track);
            statusLabel_.setText(TRANS("OSTF pattern added at bar ") + juce::String(bar) + TRANS(" / current session"),
                                 juce::dontSendNotification);
        } else {
            statusLabel_.setText(TRANS("The OSTF pattern does not fit at this position"), juce::dontSendNotification);
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

void MainComponent::restoreInstrumentAssignments(const ostfstudio::SessionMix& mix) {
    if (std::none_of(mix.instruments.begin(), mix.instruments.end(), [](const auto& item) { return item.isObject(); })) return;
    const auto types = ensurePluginBrowser().types();
    for (std::size_t index = 0; index < std::min(mix.instruments.size(), trackControls_.size()); ++index) {
        const auto& reference = mix.instruments[index];
        if (!reference.isObject()) continue;
        auto& track = *trackControls_[index];
        track.instrumentAssignment = reference;
        const auto match = std::find_if(types.begin(), types.end(), [&](const auto& description) {
            return matchesVst3Instrument(reference, description);
        });
        if (match == types.end()) track.plugin.setButtonText(reference["name"].toString() + TRANS(" (missing)"));
        else loadTrackPlugin(index, *match, true);
    }
}

void MainComponent::startTransport(bool recording) {
    if (!project_ || (recording && !selectedTrack_)) return;
    if (recording && (*selectedTrack_ >= project_->sourceTrackCount()
        || project_->trackKinds[*selectedTrack_] != ostfstudio::TrackKind::pitched)) {
        statusLabel_.setText(TRANS("MIDI recording requires a pitched track in this preview"), juce::dontSendNotification);
        return;
    }
    if (transportActive_) stopTransport();
    if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
    if (std::any_of(trackControls_.begin(), trackControls_.end(), [](const auto& track) { return track->loading; })) {
        statusLabel_.setText(TRANS("Waiting for track instruments to finish loading"), juce::dontSendNotification);
        return;
    }
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
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return;
    }
    transportActive_ = true;
    recordTrack_ = recording ? selectedTrack_ : std::nullopt;
    playButton_.setToggleState(!recording, juce::dontSendNotification);
    recordButton_.setToggleState(recording, juce::dontSendNotification);
    statusLabel_.setText(recording ? TRANS("Recording MIDI into the selected track") : TRANS("Playing arrangement"),
                         juce::dontSendNotification);
    if (!unavailable.empty())
        statusLabel_.setText(TRANS("No playable instrument on: ") + unavailable.front()
                             + (unavailable.size() > 1 ? TRANS(" (and other tracks)") : ""), juce::dontSendNotification);
    else if (!recording && project_->audio.empty()
             && std::none_of(tracks.begin(), tracks.end(), [](const auto& track) { return track.instrument; }))
        statusLabel_.setText(TRANS("Assign a VST3 instrument to hear playback"), juce::dontSendNotification);
    startTimerHz(24);
}

void MainComponent::stopTransport() {
    if (!transportActive_) return;
    auto recorded = transport_.stop();
    if (recordTrack_ && project_) {
        const auto count = static_cast<int>(recorded.size());
        const bool added = project_->addSessionClip(*recordTrack_, "Recorded MIDI", std::move(recorded));
        selectTrack(*recordTrack_);
        statusLabel_.setText(added ? juce::String(count) + TRANS(" MIDI notes captured") : TRANS("No MIDI notes captured"),
                             juce::dontSendNotification);
    } else {
        statusLabel_.setText(TRANS("Stopped"), juce::dontSendNotification);
    }
    recordTrack_.reset();
    transportActive_ = false;
    playButton_.setToggleState(false, juce::dontSendNotification);
    recordButton_.setToggleState(false, juce::dontSendNotification);
    transportPosition_.setText(TRANS("Bar 1"), juce::dontSendNotification);
    stopTimer();
    repaint();
}

void MainComponent::timerCallback() {
    if (!transport_.isRunning()) { stopTransport(); return; }
    transportPosition_.setText(TRANS("Bar ") + juce::String(1 + transport_.positionTick() / project_->ticksPerBar), juce::dontSendNotification);
    repaint();
}

void MainComponent::openDemo() {
    juce::MemoryInputStream stream(BinaryData::_10comprehensivecore_ostfp,
                                   BinaryData::_10comprehensivecore_ostfpSize, false);
    openPreview(stream);
}

void MainComponent::inspectOSTF() {
    chooser_ = std::make_unique<juce::FileChooser>(TRANS("Open an OSTF score, OSTFP package, or JSON"), juce::File{},
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
        statusLabel_.setText(TRANS("Could not read: ") + file.getFileName(), juce::dontSendNotification);
        return false;
    }
    if (!file.hasFileExtension("json")) return openPreview(input, file.hasFileExtension("ostf"), file.getParentDirectory());
    juce::var parsed;
    const bool inspected = juce::JSON::parse(input.readEntireStreamAsString(), parsed).wasOk() && !parsed.isVoid();
    statusLabel_.setText((inspected ? TRANS("JSON object or array read: ") : TRANS("Expected a JSON object or array: "))
                         + file.getFileName(), juce::dontSendNotification);
    return inspected;
}

bool MainComponent::openPreview(juce::InputStream& input, bool plainScore, const juce::File& sourceFolder) {
    std::unique_ptr<ostfstudio::OstfpPreview> next;
    const auto result = plainScore ? ostfstudio::readOstfPreview(input, next) : ostfstudio::readOstfpPreview(input, next);
    if (result.wasOk()) {
        stopTransport();
        project_ = std::move(next);
        updateHorizontalRange(false, true);
        updateHorizontalRange(true, true);
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
            restoreInstrumentAssignments(*saved);
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
    statusLabel_.setText(result.wasOk() ? project_->title + " / " + juce::String(project_->bars.endBar - 1) + TRANS(" bars")
                                       : juce::translate(result.getErrorMessage()),
                         juce::dontSendNotification);
    repaint();
    return result.wasOk();
}

void MainComponent::saveDocument(SaveFormat format) {
    if (!project_) return;
    const bool midi = format == SaveFormat::midi0 || format == SaveFormat::midi1;
    const bool package = format == SaveFormat::package;
    const juce::String extension = midi ? "mid" : package ? "ostfp" : "ostf";
    auto title = juce::File::createLegalFileName(project_->title).trim();
    if (title.isEmpty()) title = "Untitled";
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(title + "." + extension);
    chooser_ = std::make_unique<juce::FileChooser>(juce::translate(midi ? "Save as MIDI (SMF)" : package ? "Save as OSTFP" : "Save as OSTF"), start, "*." + extension);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, package, midi, format,
         extension](const juce::FileChooser& chooser) {
            if (!safe || safe->projectGeneration_ != generation) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension(extension)) {
                safe->statusLabel_.setText(TRANS("Choose a .") + extension + TRANS(" filename"), juce::dontSendNotification);
                return;
            }
            if (safe->recordTrack_) safe->stopTransport();
            juce::TemporaryFile temporary(file);
            auto result = juce::Result::fail(TRANS("Could not open a temporary file for saving"));
            {
                juce::FileOutputStream output(temporary.getFile());
                if (output.openedOk() && midi) {
                    std::vector<int> channels;
                    for (const auto& track : safe->trackControls_) channels.push_back(track->midiChannel);
                    result = ostfstudio::writeSongMidi(*safe->project_, format == SaveFormat::midi0
                        ? ostfstudio::SmfFormat::merged : ostfstudio::SmfFormat::multitrack, channels, output);
                } else if (output.openedOk()) {
                    auto mix = safe->mixer_.snapshot();
                    for (const auto& track : safe->trackControls_) {
                        mix.midiRoutes.push_back({track->midiChannel, track->outputTrack});
                        mix.instruments.push_back(track->instrumentAssignment);
                    }
                    for (std::size_t i = 0; i < safe->outputControls_.size(); ++i)
                        for (const auto& slot : safe->outputControls_[i].effects.slots) mix.outputEffects[i].add(slot.id);
                    for (const auto kind : safe->project_->trackKinds) mix.audioTracks.push_back(kind == ostfstudio::TrackKind::audio);
                    mix.trackEffects.resize(safe->trackControls_.size());
                    for (std::size_t i = 0; i < safe->trackControls_.size(); ++i)
                        for (const auto& slot : safe->trackControls_[i]->effects.slots) mix.trackEffects[i].add(slot.id);
                    result = package ? ostfstudio::writeOstfpPreview(*safe->project_, mix, output)
                                     : ostfstudio::writeOstfPreview(*safe->project_, mix, file, output);
                }
                output.flush();
                if (result.wasOk()) result = output.getStatus();
            }
            if (result.wasOk() && !temporary.overwriteTargetFileWithTemporary())
                result = juce::Result::fail(TRANS("Could not replace the destination file"));
            safe->statusLabel_.setText(result.wasOk() ? TRANS("Saved ") + file.getFileName() : juce::translate(result.getErrorMessage()),
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
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
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
        mix.tracks.insert(mix.tracks.begin() + static_cast<std::ptrdiff_t>(project_->sourceTrackCount() - 1), ostfstudio::ChannelMix{});
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
        track.instrumentAssignment = old.instrumentAssignment;
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
    if (!trackControls_.empty()) selectTrack(remove ? std::min(*remove, trackControls_.size() - 1) : project_->sourceTrackCount() - 1);
    else refreshEditor();
    statusLabel_.setText(remove ? TRANS("Track removed / save to keep changes") : TRANS("Track added / save to keep changes"),
                         juce::dontSendNotification);
}

void MainComponent::rebuildTrackControls() {
    trackControls_.clear();
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i) {
        auto controls = std::make_unique<TrackControls>();
        const auto name = juce::String::fromUTF8(project_->trackLanes[i].label);
        controls->mute.setTitle(name + " mute");
        controls->mute.setTooltip(TRANS("Mute ") + name);
        controls->mute.setClickingTogglesState(true);
        controls->mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
        controls->solo.setTitle(name + " solo");
        controls->solo.setTooltip(TRANS("Solo ") + name);
        controls->solo.setClickingTogglesState(true);
        controls->solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8a622b));
        controls->volume.setName(name + " volume");
        controls->volume.setTooltip(TRANS("Volume in dB. Double-click to reset to 0 dB."));
        controls->volume.setSliderStyle(juce::Slider::LinearHorizontal);
        controls->volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 41, 16);
        controls->volume.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(hardwareAmber));
        controls->volume.setRange(-60.0, 6.0, 0.1);
        controls->volume.setValue(0.0, juce::dontSendNotification);
        controls->volume.setDoubleClickReturnValue(true, 0.0);
        controls->mute.onClick = [this, i, control = &controls->mute] { mixer_.setMuted(i, control->getToggleState()); };
        controls->solo.onClick = [this, i, control = &controls->solo] { mixer_.setSoloed(i, control->getToggleState()); };
        controls->volume.onValueChange = [this, i, control = &controls->volume] { mixer_.setVolumeDb(i, control->getValue()); };
        controls->pan.setName(name + " pan");
        controls->pan.setTooltip(TRANS("Stereo pan. Double-click to centre."));
        controls->pan.setRange(-100, 100, 1);
        controls->pan.setSliderStyle(juce::Slider::LinearHorizontal);
        controls->pan.setTextBoxStyle(juce::Slider::TextBoxRight, false, 29, 15);
        controls->pan.setDoubleClickReturnValue(true, 0.0);
        controls->pan.onValueChange = [this, i, control = &controls->pan] { mixer_.setPan(i, static_cast<float>(control->getValue() / 100.0)); };
        controls->plugin.setTooltip(TRANS("Assign a local plug-in to ") + name);
        controls->plugin.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25221e));
        controls->plugin.setColour(juce::TextButton::textColourOffId, juce::Colour(hardwareAmber));
        controls->plugin.onClick = [this, i] { showTrackPluginMenu(i); };
        controls->mix.setTooltip(TRANS("Track volume, pan, sends and routing"));
        controls->mix.onClick = [this, i] { showTrackMix(i); };
        controls->preset.setFont(juce::FontOptions(10.0f));
        controls->preset.setColour(juce::Label::textColourId, juce::Colour(muted));
        controls->preset.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(controls->mute);
        addAndMakeVisible(controls->solo);
        addAndMakeVisible(controls->volume);
        addAndMakeVisible(controls->pan);
        addAndMakeVisible(controls->plugin);
        addAndMakeVisible(controls->mix);
        addAndMakeVisible(controls->preset);
        controls->effects.button.onClick = [this, i] { showEffects(false, i); };
        updateEffectButton(controls->effects);
        addAndMakeVisible(controls->effects.button);
        controls->midiChannel = project_->sourceMidiChannels[i];
        if (project_->trackKinds[i] == ostfstudio::TrackKind::audio) {
            controls->plugin.setButtonText(TRANS("Link WAV"));
            controls->plugin.setTooltip(TRANS("Link a WAV file to this audio track"));
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
    menu.addSubMenu(TRANS("MIDI channel ") + juce::String(control.midiChannel), channels);
    juce::PopupMenu outputs;
    outputs.addItem(outputId, TRANS("This track"), true, !control.outputTrack);
    const auto sourceLimit = std::min(PreviewTransport::maxTracks, trackControls_.size());
    for (std::size_t i = 0; i < sourceLimit; ++i) {
        if (i != index && i < project_->sourceTrackCount() && trackControls_[i]->playableInstrument && trackControls_[i]->instance->acceptsMidi())
            outputs.addItem(outputId + 1 + static_cast<int>(i),
                            juce::String::fromUTF8(project_->trackLanes[i].label) + " / "
                            + trackControls_[i]->instance->getName(), true, control.outputTrack == i);
    }
    menu.addSubMenu(TRANS("Instrument destination"), outputs);
    menu.addSeparator();
    menu.addItem(8, TRANS("Link WAV on this track..."), !control.loading && index < project_->sourceTrackCount());
    menu.addItem(1, TRANS("Choose plug-in file..."), !control.loading);
    menu.addItem(3, TRANS("Choose plug-in bundle folder..."), !control.loading);
    menu.addItem(4, TRANS("Load selected VST3"), control.candidatePath.hasFileExtension("vst3") && !control.loading);
    menu.addItem(5, TRANS("Open plug-in editor"), control.instance != nullptr && !control.loading);
    menu.addSeparator();
    menu.addItem(2, TRANS("Clear assignment"), (control.pluginPath != juce::File{} || control.instrumentAssignment.isObject()) && !control.loading);
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
                strip.instrumentAssignment = juce::var{};
                strip.pluginPath = juce::File{};
                strip.candidatePath = juce::File{};
                strip.plugin.setButtonText(TRANS("Instrument"));
                strip.plugin.setTooltip(TRANS("Assign a local plug-in to this track"));
                self->updatePresetLabel(index);
                self->statusLabel_.setText(TRANS("Plug-in assignment cleared"), juce::dontSendNotification);
            }
        });
}

void MainComponent::linkAudio(std::size_t index) {
    if (!project_ || index >= project_->sourceTrackCount()) return;
    chooser_ = std::make_unique<juce::FileChooser>(TRANS("Link WAV at the start of the track"), juce::File{}, "*.wav");
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
            safe->statusLabel_.setText(TRANS("Linked at song start: ") + file.getFileName(), juce::dontSendNotification);
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
    pluginChooser_ = std::make_unique<juce::FileChooser>(TRANS("Assign a local plug-in"), directory, filter);
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
                self->statusLabel_.setText(TRANS("Choose a plug-in matching ") + juce::String(filter), juce::dontSendNotification);
                return;
            }
            auto& strip = *self->trackControls_[index];
            strip.candidatePath = file;
            const auto detail = file.hasFileExtension("vst3") ? " / Choose Load VST3 to configure" : " / AU assignment only";
            self->statusLabel_.setText(TRANS("Assigned ") + file.getFileName() + detail, juce::dontSendNotification);
        });
}

void MainComponent::scanTrackPlugin(std::size_t index) {
    auto& strip = *trackControls_[index];
    if (strip.loading || !strip.candidatePath.exists() || !strip.candidatePath.hasFileExtension("vst3")) return;
    strip.loading = true;
    strip.plugin.setButtonText(TRANS("Reading..."));
    statusLabel_.setText(TRANS("Reading VST3 components: ") + strip.candidatePath.getFileName(), juce::dontSendNotification);
    juce::Timer::callAfterDelay(65, [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_] {
        if (self != nullptr && self->projectGeneration_ == generation) self->discoverTrackPlugin(index);
    });
}

void MainComponent::discoverTrackPlugin(std::size_t index) {
    auto& strip = *trackControls_[index];
    juce::OwnedArray<juce::PluginDescription> found;
    pluginFormats_.getFormat(0)->findAllTypesForFile(found, strip.candidatePath.getFullPathName());
    strip.loading = false;
    strip.plugin.setButtonText(strip.instance ? strip.instance->getName() : TRANS("Instrument"));
    if (found.isEmpty()) {
        statusLabel_.setText(TRANS("No VST3 component found in ") + strip.candidatePath.getFileName(), juce::dontSendNotification);
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

void MainComponent::loadTrackPlugin(std::size_t index, const juce::PluginDescription& description, bool restoring) {
    auto& strip = *trackControls_[index];
    if (strip.loading) return;
    if (isOstfGenerator(description)) {
        statusLabel_.setText(TRANS("Use OSTF Arranger to load an instrument and play the arrangement"), juce::dontSendNotification);
        return;
    }
    if (!description.isInstrument) {
        appendEffect(false, index, description);
        return;
    }
    strip.loading = true;
    strip.plugin.setButtonText(TRANS("Loading..."));
    statusLabel_.setText(TRANS("Loading plug-in: ") + description.name, juce::dontSendNotification);
    juce::Timer::callAfterDelay(65,
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_, description, restoring] {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            self->pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512,
                [self, index, generation, description, restoring](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
                    if (self == nullptr || self->projectGeneration_ != generation) return;
                    auto& target = *self->trackControls_[index];
                    target.loading = false;
                    if (!instance) {
                        target.plugin.setButtonText(target.instance ? target.instance->getName() : TRANS("Instrument"));
                        self->statusLabel_.setText(TRANS("VST3 load failed: ") + error, juce::dontSendNotification);
                        return;
                    }
                    self->stopTransport();
                    target.editor.reset();
                    if (description.isInstrument) instance->disableNonMainBuses();
                    target.playableInstrument = PreviewTransport::supportsInstrument(*instance);
                    target.pluginPath = juce::File(description.fileOrIdentifier);
                    if (!restoring) target.outputTrack.reset();
                    target.instance = std::move(instance);
                    if (!restoring) {
                        auto reference = std::make_unique<juce::DynamicObject>();
                        reference->setProperty("format", "vst3");
                        reference->setProperty("pluginId", juce::String::toHexString(description.uniqueId));
                        reference->setProperty("namespace", "org.juce.vst3.uid");
                        reference->setProperty("name", description.name);
                        if (description.manufacturerName.isNotEmpty()) reference->setProperty("vendor", description.manufacturerName);
                        target.instrumentAssignment = juce::var(reference.release());
                    }
                    target.plugin.setButtonText(target.instance->getName());
                    self->updatePresetLabel(index);
                    if (!restoring) self->openTrackPluginEditor(index);
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
    statusLabel_.setText(strip.playableInstrument ? TRANS("Instrument ready for track playback")
                                                 : TRANS("Plug-in editor ready / audio routing unavailable for this plug-in"),
                         juce::dontSendNotification);
}

void MainComponent::updatePresetLabel(std::size_t index) {
    auto& strip = *trackControls_[index];
    juce::String label;
    if (auto* instance = strip.instance.get()) {
        if (const auto program = instance->getCurrentProgram(); program >= 0 && program < instance->getNumPrograms()) {
            label = instance->getProgramName(program);
        }
        if (label.isEmpty()) label = TRANS("Preset not reported");
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
        control.mute.setTooltip(TRANS("Mute ") + outputName(i));
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
    chain.button.setTooltip(TRANS("Audio effects in signal order"));
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
                            + (slot.id.isEmpty() ? TRANS("Loading...") : TRANS(" (unavailable)"));
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
            if (effects.isEmpty()) menu.addItem(1, TRANS("Scan VST3 effects in the Plug-in Browser"), false);
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
                safe->statusLabel_.setText(effect ? TRANS("Insert requires a stereo audio effect") : TRANS("Could not load FX: ") + error,
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
    editorVisible_ = true;
    selectedTrack_ = index;
    trackPicker_.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification);
    mixerButton_.setToggleState(false, juce::dontSendNotification);
    refreshEditor();
}

void MainComponent::refreshEditor() {
    const bool showMixer = mixerButton_.getToggleState();
    const bool piano = project_ && editorVisible_ && !showMixer;
    pianoButton_.setToggleState(piano, juce::dontSendNotification);
    outputButton_.setToggleState(showOutputTracks_, juce::dontSendNotification);
    for (auto* button : {&pianoButton_, &mixerButton_, &outputButton_}) button->setEnabled(project_ != nullptr);
    trackPicker_.setVisible(project_ && editorVisible_);
    performanceLane_.setVisible(piano && selectedTrack_);
    const auto previous = performanceLaneNames_[performanceLane_.getSelectedId() - 1];
    performanceLaneNames_ = juce::StringArray{"Velocity"};
    if (project_ && selectedTrack_ && *selectedTrack_ < project_->performanceCurves.size())
        for (const auto& curve : project_->performanceCurves[*selectedTrack_])
            performanceLaneNames_.addIfNotAlreadyThere(curve.label);
    performanceLane_.clear(juce::dontSendNotification);
    for (int i = 0; i < performanceLaneNames_.size(); ++i) performanceLane_.addItem(juce::translate(performanceLaneNames_[i]), i + 1);
    performanceLane_.setSelectedId(std::max(1, performanceLaneNames_.indexOf(previous) + 1), juce::dontSendNotification);
    arrangementZoom_.setVisible(project_ != nullptr);
    arrangementScroll_.setVisible(project_ != nullptr);
    pianoZoom_.setVisible(piano);
    pianoScroll_.setVisible(piano);
    midiDrag_.setVisible(piano);
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
    auto viewBar = getLocalBounds().withTrimmedTop(menuHeight).removeFromTop(viewBarHeight).withTrimmedLeft(18);
    for (auto* button : {&pianoButton_, &mixerButton_, &pluginBrowserButton_, &outputButton_, &lanesButton_})
        button->setBounds(viewBar.removeFromLeft(39).reduced(2, 2));
    auto transportRow = getLocalBounds().withTrimmedTop(menuHeight + viewBarHeight + 14).removeFromTop(28).withTrimmedLeft(221).withTrimmedRight(18);
    playButton_.setBounds(transportRow.removeFromLeft(54).reduced(2, 0));
    stopButton_.setBounds(transportRow.removeFromLeft(54).reduced(2, 0));
    recordButton_.setBounds(transportRow.removeFromLeft(94).reduced(2, 0));
    transportPosition_.setBounds(transportRow.removeFromLeft(72));
    statusLabel_.setBounds(20, getHeight() - 37, getWidth() - 40, 24);
    auto editor = getWorkspace();
    if (pluginBrowser_) {
        const auto full = workspaceBounds(getLocalBounds());
        pluginBrowser_->setBounds(full.withLeft(editor.getRight() + 8));
    }
    const auto arrangerHeight = getArrangementHeight(editor.getHeight());
    auto arrangement = editor.withHeight(arrangerHeight).reduced(13);
    if (project_) {
        auto navigation = arrangement.removeFromBottom(timelineNavigationHeight).reduced(0, 2);
        arrangementZoom_.setBounds(navigation.removeFromLeft(116));
        navigation.removeFromLeft(std::max(4, trackHeaderWidth - 116));
        arrangementScroll_.setBounds(navigation.reduced(0, 3));
    }
    const auto outputs = visibleOutputCount();
    for (std::size_t i = 0; i < outputControls_.size(); ++i) {
        outputControls_[i].effects.button.setVisible(i < outputs);
        outputControls_[i].volume.setVisible(i < outputs);
        outputControls_[i].mute.setVisible(i < outputs);
    }
    if (project_) {
        const auto view = currentArrangement();
        const auto height = arrangementLaneHeight(arrangement, view, outputs);
        const int trackY = arrangement.getY() + (1 + static_cast<int>(view.globalLanes.size())) * height;
        int y = trackY;
        bool arrangementHeading = false;
        trackRows_.assign(trackControls_.size(), {});
        for (const auto i : displayedTrackOrder()) {
            if (!project_->isGlobalNoteTrack(i) && !arrangementHeading) {
                arrangementHeading = true;
                y += arrangementHeadingHeight;
            }
            trackRows_[i] = {arrangement.getX(), y, arrangement.getWidth(), height - 4};
            y += height;
        }
        for (std::size_t i = 0; i < trackControls_.size(); ++i) {
            auto row = trackRows_[i].withTrimmedLeft(trackNameWidth).withWidth(trackHeaderWidth - trackNameWidth - 3);
            const bool visible = !trackRows_[i].isEmpty();
            auto& strip = *trackControls_[i];
            for (auto* control : std::array<juce::Component*, 8>{&strip.mute, &strip.solo, &strip.volume,
                     &strip.pan, &strip.mix, &strip.effects.button, &strip.preset, &strip.plugin}) control->setVisible(visible);
            if (!visible) continue;
            strip.mute.setBounds(row.removeFromLeft(29).reduced(2));
            strip.solo.setBounds(row.removeFromLeft(29).reduced(2));
            strip.volume.setBounds(row.removeFromLeft(65).reduced(2, 0));
            strip.pan.setBounds(row.removeFromLeft(50).reduced(2, 0));
            strip.mix.setBounds(row.removeFromLeft(28).reduced(1));
            strip.effects.button.setBounds(row.removeFromRight(44).reduced(2, 1));
            auto instrument = row.reduced(1);
            strip.preset.setBounds(instrument.removeFromBottom(std::min(13, instrument.getHeight() / 2)));
            strip.plugin.setBounds(instrument);
        }
        const int outputY = y + arrangementHeadingHeight;
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
    const auto pickerWidth = performanceLane_.isVisible() ? juce::jmin(180, toolbar.getWidth() / 2) : 180;
    trackPicker_.setBounds(toolbar.removeFromLeft(pickerWidth));
    toolbar.removeFromLeft(6);
    performanceLane_.setBounds(toolbar.removeFromLeft(250).withTrimmedRight(4));
    mixer_.setBounds(editor);
    auto pianoNavigation = editor.removeFromBottom(timelineNavigationHeight).reduced(7, 2);
    pianoZoom_.setBounds(pianoNavigation.removeFromLeft(116));
    pianoScroll_.setBounds(pianoNavigation.withTrimmedLeft(8).reduced(0, 3));
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(bg));
    g.setColour(juce::Colour(0xff1b2229));
    g.fillRect(0, menuHeight, getWidth(), viewBarHeight);
    auto area = getLocalBounds().reduced(18);
    area.removeFromTop(menuHeight + viewBarHeight);
    auto top = area.removeFromTop(54);
    auto title = top.removeFromLeft(190);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    g.drawText("OSTF Studio", title.removeFromTop(29), juce::Justification::centredLeft);
    g.setColour(juce::Colour(muted));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(project_ ? TRANS("GLOBAL MUSICAL TIMELINE") : TRANS("GLOBAL MUSICAL TIMELINE / DEMO"), title, juce::Justification::centredLeft, 1);
    area = getWorkspace();
    if (!project_) {
        drawArrangement(g, area, currentArrangement());
        return;
    }

    auto arrangementArea = area.removeFromTop(getArrangementHeight(area.getHeight()));
    area.removeFromTop(8);
    drawArrangement(g, arrangementArea, currentArrangement());
    if (!editorVisible_) return;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff303840), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(panel), 0.0f, static_cast<float>(area.getBottom()), false));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.removeFromTop(30);
    if (!mixerButton_.getToggleState()) {
        area.removeFromBottom(timelineNavigationHeight);
        drawPianoRoll(g, area);
    }
}

void MainComponent::drawArrangement(juce::Graphics& g, juce::Rectangle<int> area,
                                    const ostfstudio::ArrangementView& view) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff313941), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(panel), 0.0f, static_cast<float>(area.getBottom()), false));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 13);
    if (project_) area.removeFromBottom(timelineNavigationHeight);
    const auto outputs = visibleOutputCount();
    const int laneHeight = arrangementLaneHeight(area, view, outputs);
    auto labels = area.removeFromLeft(project_ ? trackHeaderWidth : 108);
    if (laneHeight < 12 || area.getWidth() < 40) return;
    const int rowHeight = laneHeight - 4;

    auto drawRow = [&](const juce::String& label, int y) {
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(label,
                         {labels.getX() + 5, y, trackNameWidth - 10, rowHeight},
                         juce::Justification::centredLeft, 1);
        const auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff233443));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        return row;
    };

    const auto bars = view.bars;
    const auto visible = project_ ? horizontalRange(false)
                                  : ostfstudio::TimelineExtent{static_cast<double>(bars.firstBar), static_cast<double>(bars.endBar)};
    const auto widthInBars = visible.end - visible.start;
    if (widthInBars <= 0.0) return;
    const auto positionX = [visible, widthInBars](double bar, juce::Rectangle<int> row) {
        return row.getX() + juce::roundToInt((bar - visible.start) / widthInBars * row.getWidth());
    };
    const auto clipBounds = [&](ostfstudio::TimelineExtent span, juce::Rectangle<int> row) {
        const auto start = std::max(span.start, visible.start);
        const auto end = std::min(span.end, visible.end);
        if (end <= start) return juce::Rectangle<int>{};
        const auto x = positionX(start, row);
        return juce::Rectangle<int>(x, row.getY(), positionX(end, row) - x, row.getHeight());
    };
    const int tickStep = juce::jmax(1, static_cast<int>(widthInBars / (area.getWidth() < 420 ? 4 : 8)));
    const auto firstBar = static_cast<int>(std::ceil(visible.start));
    const auto ruler = drawRow(TRANS("BARS"), area.getY());
    for (int bar = firstBar; bar < visible.end; bar += tickStep) {
        const int x = positionX(bar, ruler);
        g.setColour(juce::Colour(muted));
        g.drawText(juce::String(bar), ruler.withLeft(x).withWidth(std::min(36, ruler.getRight() - x)).withTrimmedLeft(3),
                   juce::Justification::centredLeft);
    }

    auto drawLane = [&](const ostfstudio::ArrangementLane& lane, int y, juce::Colour colour) {
        const auto row = drawRow(lane.kind == TimelineLane::none ? juce::String::fromUTF8(lane.label) : juce::translate(lane.label), y);
        g.setColour(juce::Colour(0xff3a4e5e));
        for (int bar = firstBar; bar < visible.end; bar += tickStep) {
            g.drawVerticalLine(positionX(bar, row), static_cast<float>(row.getY()), static_cast<float>(row.getBottom()));
        }

        for (const auto& region : lane.regions) {
            const auto span = region.exactBars.value_or(ostfstudio::TimelineExtent{
                static_cast<double>(region.bars.firstBar), static_cast<double>(region.bars.endBar)});
            const auto cell = clipBounds(span, row);
            if (cell.isEmpty()) continue;
            const auto bounds = cell.reduced(cell.getWidth() >= 6 ? 2 : 0, 2);
            if (bounds.isEmpty()) continue;
            g.setColour(colour);
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.93f));
            auto label = juce::String::fromUTF8(region.label);
            if (region.noteCount > 0) label = "[" + juce::String(region.noteCount) + TRANS(" notes] ") + label;
            g.drawFittedText(label, bounds.reduced(4, 0), juce::Justification::centredLeft, 1);
        }
    };

    int y = area.getY() + laneHeight;
    const juce::uint32 globalColours[] {0xff314d72, 0xff493c64, 0xff31495a, 0xff31404e, 0xff31404e, 0xff2c6058};
    for (std::size_t i = 0; i < view.globalLanes.size(); ++i) {
        drawLane(view.globalLanes[i], y, juce::Colour(globalColours[i % std::size(globalColours)]));
        y += laneHeight;
    }

    const juce::uint32 trackColours[] {0xff2a657d, 0xff265e57, 0xff4b517f, 0xff744366, 0xff526070};
    auto heading = [&](const char* title) {
        g.setColour(juce::Colour(accent));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(juce::translate(title), labels.getX() + 4, y, labels.getWidth() + area.getWidth() - 4,
                   arrangementHeadingHeight, juce::Justification::centredLeft);
        y += arrangementHeadingHeight;
    };
    auto drawTrack = [&](std::size_t i) {
        const auto channel = juce::Rectangle<int>(labels.getX(), y, labels.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff353d45));
        g.fillRoundedRectangle(channel.toFloat().reduced(1.0f), 3.0f);
        g.setColour(juce::Colour(0xff525d67));
        g.drawHorizontalLine(y + 1, static_cast<float>(channel.getX() + 4), static_cast<float>(channel.getRight() - 4));
        drawLane(view.trackLanes[i], y, juce::Colour(trackColours[i % std::size(trackColours)]));
        if (project_) {
            const auto audio = std::find_if(project_->audio.begin(), project_->audio.end(), [i](const auto& item) { return item.track == i; });
            if (audio != project_->audio.end()) {
                const auto label = audio->embedded.isEmpty() && !audio->localFile.existsAsFile() ? TRANS("Missing WAV") : "WAV";
                g.setColour(juce::Colour(0xffe7b46c));
                g.drawText(label, area.getX() + 6, y, 80, rowHeight, juce::Justification::centredLeft);
            }
            const juce::Rectangle<int> hit(labels.getX(), y, labels.getWidth() + area.getWidth(), rowHeight);
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
    };
    if (project_) {
        bool arrangementHeading = false;
        for (const auto i : displayedTrackOrder()) {
            if (!project_->isGlobalNoteTrack(i) && !arrangementHeading) {
                heading("ARRANGEMENT TRACKS");
                arrangementHeading = true;
            }
            drawTrack(i);
        }
    } else {
        heading("ARRANGEMENT TRACKS / DEMO");
        for (std::size_t i = 0; i < view.trackLanes.size(); ++i) drawTrack(i);
    }
    if (outputs) {
        g.setColour(juce::Colour(accent));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(TRANS("OUTPUT TRACKS"), labels.getX() + 4, y, labels.getWidth(), arrangementHeadingHeight,
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
            g.drawFittedText(i == 0 ? TRANS("Out 1-2") : TRANS("To Master"), row.withTrimmedLeft(trackHeaderWidth).reduced(8, 0),
                             juce::Justification::centredLeft, 1);
            if (outputDropHighlight_ == i) {
                g.setColour(juce::Colour(hardwareAmber));
                g.drawRoundedRectangle(row.toFloat().reduced(1.0f), 4.0f, 2.0f);
            }
            y += laneHeight;
        }
    }
    if (transportActive_ && project_) {
        const auto bar = 1.0 + static_cast<double>(transport_.positionTick()) / project_->ticksPerBar;
        if (bar >= visible.start && bar < visible.end) {
            g.setColour(juce::Colour(hardwareAmber));
            g.drawVerticalLine(positionX(bar, ruler), static_cast<float>(ruler.getY()), static_cast<float>(y - 4));
        }
    }
}

void MainComponent::drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 8);
    const auto heading = area.removeFromTop(24);
    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    const auto name = selectedTrack_ ? TRANS("PIANO ROLL  /  ") + juce::String::fromUTF8(project_->trackLanes[*selectedTrack_].label)
                                     : TRANS("PIANO ROLL");
    g.drawFittedText(name, heading, juce::Justification::centredLeft, 1);
    if (!selectedTrack_) {
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(TRANS("Select an arrangement track to inspect its notes."),
                   area, juce::Justification::centred);
        return;
    }

    const auto& notes = project_->pitchedNotes[*selectedTrack_];
    const auto laneName = performanceLane_.getText();
    const auto& curves = project_->performanceCurves[*selectedTrack_];
    int low = notes.empty() ? 55 : notes.front().pitch;
    int high = notes.empty() ? 67 : low;
    for (const auto& note : notes) {
        low = std::min(low, note.pitch);
        high = std::max(high, note.pitch);
    }
    low = std::max(0, low - 3);
    high = std::min(127, high + 3);
    const auto visible = horizontalRange(true);
    const auto barLength = project_->ticksPerBar;
    const auto firstBar = static_cast<juce::int64>(std::ceil(visible.start)) - 1;
    const auto endBar = static_cast<juce::int64>(std::ceil(visible.end)) - 1;
    const auto origin = (visible.start - 1.0) * barLength;
    const auto length = (visible.end - visible.start) * barLength;
    if (length <= 0.0) return;
    auto lane = area.removeFromBottom(std::min(84, area.getHeight() / 3));
    auto keys = area.removeFromLeft(52);
    lane.removeFromLeft(52);
    const auto ruler = area.removeFromTop(18);
    keys.removeFromTop(18);
    if (area.isEmpty()) return;

    const auto rowHeight = static_cast<float>(area.getHeight()) / static_cast<float>(high - low + 1);
    auto noteY = [&](int pitch) { return static_cast<float>(area.getY()) + static_cast<float>(high - pitch) * rowHeight; };
    auto tickX = [&](juce::int64 tick) {
        return static_cast<float>(area.getX() + (static_cast<double>(tick) - origin) / length * area.getWidth());
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
    if (performanceLane_.getSelectedId() == 1) {
        for (const auto& note : notes) {
            if (!note.velocity) continue;
            any = true;
            const auto x = tickX(note.startTick);
            g.drawLine(x, yFor(0), x, yFor(*note.velocity / 127.0), 2.0f);
        }
    } else {
        double centsRange = 100.0;
        for (const auto& curve : curves) if (curve.label == performanceLaneNames_[performanceLane_.getSelectedId() - 1] && curve.cents)
            for (const auto& point : curve.points) centsRange = std::max(centsRange, std::abs(point.value));
        for (const auto& curve : curves) {
            if (curve.label != performanceLaneNames_[performanceLane_.getSelectedId() - 1]) continue;
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
        g.drawText(TRANS("No authored values"), lane, juce::Justification::centred);
    }
    g.restoreState();
}
