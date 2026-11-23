#include "ui/MainComponent.h"
#include "ui/SmkPresetEditor.h"
#include "ui/GlobalTimelineEdit.h"
#include "ui/ExternalAudioEdit.h"
#include "ui/MediaImport.h"
#include "ui/SongInformationPanel.h"
#include "ui/DemoArrangement.h"
#include "core/MidiGeneratorPattern.h"
#include "ui/PluginRoles.h"
#include "ui/StudioLanguage.h"
#include "ui/SongMidi.h"
#include "StockEffects.h"
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
int viewBarHeight(int width) { return width < 860 ? 64 : 32; }
constexpr int minInterfaceScale = 75;
constexpr int maxInterfaceScale = 150;
constexpr int timelineNavigationHeight = 32;
constexpr int arrangementHeadingHeight = 24;
constexpr int columnGripWidth = 8;
constexpr int defaultTimelineHeight = 28;
enum PianoColourMode { plainNotes = 1, chordNotes, scaleNotes, chordAndScaleNotes };
using namespace pianoHarmony;

juce::String scaleContextName(const ostfstudio::TheoryGuide& guide, const ostfstudio::TheoryGuide::TrackCell& cell, int transpose) {
    juce::StringArray names;
    if (cell.backgroundScale == -2) names.add(TRANS("Select scale reference"));
    if (cell.backgroundScale >= 0)
        names.add(ostfstudio::shiftedLabel(guide.scales[cell.backgroundScale].name, transpose));
    for (auto id : cell.noteScoped)
        names.addIfNotAlreadyThere(ostfstudio::shiftedLabel(guide.scales[id].name, transpose) + " (" + TRANS("Targeted scale") + ")");
    return names.isEmpty() ? TRANS("No reference") : names.joinIntoString(" / ");
}

struct PianoLayout {
    juce::Rectangle<int> heading, inspector, lane, divider, keys, ruler, chords, grid;
    PianoLayout(juce::Rectangle<int> area, int headerWidth, bool scrollbar, bool contextStrip,
                double parameterFraction, int keyWidth = 52) {
        const bool compact = area.getHeight() < 130;
        area.reduce(13, compact ? 2 : 8);
        heading = area.removeFromTop(compact ? 0 : 24);
        if (scrollbar) area.removeFromRight(14);
        inspector = area.removeFromLeft(headerWidth - keyWidth);
        const int height = area.getHeight();
        const auto size = parameterFraction > 0 ? juce::roundToInt(height * std::clamp(parameterFraction, 0.08, 0.65))
                                                : std::min(84, height / 3);
        lane = area.removeFromBottom(std::clamp(size, std::min(20, height / 3), std::max(height / 3, height - 70)))
                   .withTrimmedLeft(keyWidth);
        divider = area.removeFromBottom(std::min(7, area.getHeight() / 8)).withTrimmedLeft(keyWidth);
        keys = area.removeFromLeft(keyWidth);
        ruler = area.removeFromTop(compact ? 16 : 21);
        chords = area.removeFromTop(contextStrip ? (compact ? 13 : 21) : 0);
        keys.removeFromTop(ruler.getHeight() + chords.getHeight());
        grid = area;
    }
};

juce::String effectId(const juce::PluginDescription& description) {
    return description.pluginFormatName + "|" + description.manufacturerName + "|" + description.name
        + "|" + juce::String(description.uniqueId);
}
constexpr auto generatorDragId = "ostf-studio:generator-pattern";

int arrangementLaneHeight(juce::Rectangle<int> inner, const ostfstudio::ArrangementView& view, std::size_t outputs) {
    const auto rows = 1 + static_cast<int>(view.globalLanes.size() + view.trackLanes.size() + outputs);
    return juce::jmin(42, (inner.getHeight() - arrangementHeadingHeight * (outputs ? 2 : 1)) / rows);
}

juce::Rectangle<int> workspaceBounds(juce::Rectangle<int> bounds, int extra) {
    const int headerHeight = TransportPanel::panelHeight(bounds.getWidth()) + menuHeight + viewBarHeight(bounds.getWidth());
    bounds.reduce(18, 18);
    bounds.removeFromTop(headerHeight + extra);
    bounds.removeFromBottom(26);
    return bounds;
}

int browserWidthFor(int available, int preferred) {
    // Keep both panes usable when UI scaling or a smaller window reduces the available width.
    const int minimum = std::min(170, available / 3);
    const int maximum = std::max(minimum, std::min(available / 2, available - 460 - columnGripWidth));
    return std::clamp(preferred, minimum, maximum);
}

enum MenuCommand { openFile = 1, createProject, showPiano, showMixer, showDevices, showAbout, showPlugins,
                   saveScore, saveProject, showOutputs, addPitched, addDrums, addAudio, removeTrack,
                   exportMidi0, exportMidi1, languageSystem, languageEnglish, languageJapanese, showLeadSheet, showPartScore, importMedia, showArrangement, showSongInformation, addMelody, addChorus, addBus, addReturn, copyTrack, renameTrackItem, smallerUI, largerUI, showShortcuts, aiConnections, controllerSettings, initialPatchItem, cursorPatchItem, externalSplice, externalMelodyne, externalLink, linkTempoItem };
constexpr int themeBlue = 89, themeHarmonic = 90, themeGraphite = 91, themeAubergine = 92;
constexpr const char* workspaceThemes[]{"blue", "harmonic", "graphite", "aubergine"};
constexpr int timelineMenuBase = 100;
constexpr int showTheory = 80, showChordFit = 81, showScaleFit = 82, scaleReferenceBase = 1000;
constexpr int musicSettings = 83, musicDefaults = 84, musicTransform = 85, musicAudition = 86, undoScore = 87, redoScore = 88;
constexpr int demoMenuBase = 300;
constexpr struct { const char* title; const char* resource; } gallery[] {
    {"Neon Platforms (City Pop)", "neonplatforms_ostfp"},
    {"Blue Hour Waltz (Jazz Waltz)", "bluehourwaltz_ostfp"},
    {"Relay Circuit (Techno)", "relaycircuit_ostfp"},
    {"Paper Lantern Replies (R&B)", "paperlanternreplies_ostfp"},
    {"Copper Mile (Rock)", "coppermile_ostfp"},
    {"First Light Vector (Trance)", "firstlightvector_ostfp"},
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

class ControlledPluginEditor final : public juce::Component {
public:
    ControlledPluginEditor(std::unique_ptr<juce::Component> content, std::function<void(juce::Button&)> choose)
        : plugin(std::move(content)) {
        addAndMakeVisible(*plugin);
        addAndMakeVisible(control);
        control.onClick = [this, choose] { choose(control); };
        setSize(std::max(150, plugin->getWidth()), plugin->getHeight() + 28);
    }
    void resized() override {
        auto area = getLocalBounds();
        control.setBounds(area.removeFromTop(28).removeFromRight(38).reduced(2));
        plugin->setBounds(area);
    }
    void childBoundsChanged(juce::Component* child) override {
        if (child == plugin.get() && getWidth() > 0)
            setSize(std::max(150, child->getWidth()), child->getHeight() + 28);
    }
    ControlTargetButton control;
private:
    std::unique_ptr<juce::Component> plugin;
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
        output_.addItem(mixer.outputName(0), 1);
        for (std::size_t i = 0; i < mixer.routing().busCount; ++i)
            output_.addItem(mixer.outputName(ostfstudio::busOutput(i)), static_cast<int>(i + 2));
        output_.setSelectedId(channel.outputBus + 2);
        for (std::size_t i = 0; i < mixer.returnCount(); ++i)
            target_.addItem(mixer.outputName(ostfstudio::returnOutput(i)), static_cast<int>(i + 1));
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
    stockEffectChanges_.changed = [this] { markSessionEdited(); };
    setWantsKeyboardFocus(true);
    viewPreferences_.setStorageParameters(ostfstudio::viewSettingsOptions());
    for (int i = 0; i < 128; ++i)
        padMapping_[i] = juce::jlimit(0, 127, viewPreferences_.getUserSettings()->getIntValue("padInput" + juce::String(i), i));
    if (const auto* config = viewPreferences_.getUserSettings()) {
        interfaceScale_ = std::clamp(config->getIntValue("interfaceScale", 100), minInterfaceScale, maxInterfaceScale);
        juce::Desktop::getInstance().setGlobalScaleFactor(interfaceScale_ / 100.f);
        timelineLaneMask_ = static_cast<unsigned>(config->getIntValue("timelineLanes", 0xff));
        for (int i = 0; i < 4; ++i)
            if (config->getValue("workspaceTheme") == workspaceThemes[i])
                pianoInspector_.theme = static_cast<pianoHarmony::Theme>(i);
        parameterFraction_ = config->getDoubleValue("parameterLaneRatio", 0);
        theoryVisible_ = config->getBoolValue("theoryGuide", false);
        chordFitVisible_ = config->getBoolValue("showChordFit", true);
        scaleFitVisible_ = config->getBoolValue("showScaleFit", true);
    }
    setSize(1100, 690);
    menuBar_.setModel(this);
    menuBar_.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff20262d));
    addAndMakeVisible(menuBar_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(deck_);
    basics_.commit = [this](auto field, auto value, int choice) { editSongField(field, value, choice); };
    basics_.advanced = [this] { editSongInformation(); };
    addChildComponent(toolbarHint_);
    toolbarHint_.setInterceptsMouseClicks(false, false);
    toolbarHint_.setFont(juce::FontOptions(14.f, juce::Font::bold));
    toolbarHint_.setColour(juce::Label::textColourId, juce::Colour(0xff94e8dd));
    const auto addView = [this](ViewButton& button, const char* label, int command) {
        const auto name = juce::translate(label);
        button.setName(name);
        button.setTitle(name);
        button.setTooltip(name);
        button.onHint = [this](const juce::String& hint) {
            toolbarHint_.setText(hint, juce::dontSendNotification);
            toolbarHint_.setVisible(hint.isNotEmpty());
        };
        button.setToggleable(command == showArrangement || command == showLeadSheet || command == showPartScore
                             || command == showPiano || command == showMixer || command == showPlugins || command == showOutputs);
        addAndMakeVisible(button);
        button.onClick = [this, command] { menuItemSelected(command, 1); };
    };
    addView(pianoButton_, "Piano Roll", showPiano);
    addView(mixerButton_, "Mixer", showMixer);
    addView(pluginBrowserButton_, "Plug-in Browser", showPlugins);
    addView(outputButton_, "Output Tracks", showOutputs);
    addView(lanesButton_, "Global Timeline Lanes", 0);
    addView(arrangementButton_, "Arrangement", showArrangement);
    addView(leadButton_, "Lead sheet", showLeadSheet);
    addView(scoreButton_, "Score", showPartScore);
    addView(songInfoButton_, "Song information", showSongInformation);
    addView(theoryButton_, "Show Theory Guide", showTheory);
    theoryButton_.setComponentID("theory-guide");
    theoryButton_.setToggleable(true);
    addView(musicButton_, "Song settings", musicSettings);
    musicButton_.setComponentID("song-settings");
    addView(melodyAdd_, "Add melody track", addMelody);
    addView(chorusAdd_, "Add chorus vocal track", addChorus);
    addView(pitchedAdd_, "Add pitched track", addPitched);
    addView(drumsAdd_, "Add drum / percussion track", addDrums);
    addView(audioAdd_, "Add audio track", addAudio);
    addView(busAdd_, "Add bus track", addBus);
    addView(returnAdd_, "Add return track", addReturn);
    addView(uiSmaller_, "Zoom out interface", smallerUI);
    addView(uiLarger_, "Zoom in interface", largerUI);
    arrangementButton_.setTooltip(TRANS("Show the arrangement timeline"));
    leadButton_.setTooltip(TRANS("Show the lead sheet"));
    scoreButton_.setTooltip(TRANS("Show the score for a track"));
    pianoButton_.setTooltip(TRANS("Show or hide the piano roll"));
    mixerButton_.setTooltip(TRANS("Show or hide the mixer"));
    outputButton_.setTooltip(TRANS("Show or hide output tracks"));
    pluginBrowserButton_.setTooltip(TRANS("Show or hide the plug-in browser"));
    lanesButton_.setTooltip(TRANS("Choose the visible global timeline lanes"));
    lanesButton_.onClick = [this] {
        auto menu = timelineMenu();
        menu.addSeparator();
        menu.addSubMenu(TRANS("Theory Guide"), theoryMenu());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&lanesButton_),
            [safe = juce::Component::SafePointer<MainComponent>(this)](int id) { if (safe && id) safe->menuItemSelected(id, 1); });
    };
    deck_.play.onClick = [this] { startTransport(false); };
    deck_.stop.onClick = [this] { stopTransport(); };
    deck_.record.onClick = [this] { startTransport(true); };
    deck_.record.setTooltip(TRANS("Arm a track with R, then record from the cursor."));
    deck_.home.onClick = [this] { navigateTransport(TransportKey::home); };
    deck_.rewind.onClick = [this] { navigateTransport(TransportKey::rewind); };
    deck_.forward.onClick = [this] { navigateTransport(TransportKey::forward); };
    deck_.follow.onClick = [this] { displayTransportPosition(cursorTick_); };
    addChildComponent(trackPicker_);
    addChildComponent(performanceLane_);
    performanceLane_.setComponentID("performance-picker");
    performanceLane_.setTitle(TRANS("Performance lane"));
    performanceLane_.onChange = [this] { pianoVelocity_.cancel(); refreshControllerLane(); resized(); repaint(); };
    addChildComponent(controllerEditor_);
    controllerEditor_.edit = [this](auto id, auto tick, auto value){editPerformancePoint(id,tick,value);};
    addChildComponent(drumMode_); addChildComponent(drumSteps_);
    drumMode_.setComponentID("drum-mode"); drumMode_.addItem(TRANS("Drum notes"),1); drumMode_.addItem(TRANS("Slot steps"),2);
    drumMode_.setSelectedId(1,juce::dontSendNotification);
    drumMode_.onChange=[this]{refreshEditor();};
    addChildComponent(drumPads_);
    drumPads_.selectionChanged = [this](int row) { drumSteps_.selectRow(row); };
    drumSteps_.rowChanged = [this](int row) { drumPads_.select(row); };
    drumPads_.edit = [this](int row, std::optional<int> pitch) {
        if (!project_ || !selectedTrack_ || recordTrack_)
            return;
        const bool playing = transportActive_;
        if (playing)
            stopTransport();
        juce::MemoryBlock before;
        auto r = project_->editDrumPad(*selectedTrack_, row, pitch, before);
        if (r.wasOk()) {
            rememberScoreEdit(std::move(before));
            finishNoteEdit(playing);
        } else {
            if (playing)
                startTransport(false);
            statusLabel_.setText(juce::translate(r.getErrorMessage()), juce::dontSendNotification);
        }
    };
    drumPads_.reorder = [this](int from, int to) {
        if (!project_ || !selectedTrack_ || recordTrack_) return;
        const bool playing = transportActive_;
        if (playing) stopTransport();
        juce::MemoryBlock before;
        auto result = project_->editDrumPad(*selectedTrack_, from, {}, before, to);
        if (result.wasOk()) {
            rememberScoreEdit(std::move(before));
            finishNoteEdit(playing);
            drumPads_.select(to);
        } else {
            if (playing) startTransport(false);
            statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
        }
    };
    drumPads_.learnedInputs = [this](int target) {
        juce::StringArray inputs;
        for (int i = 0; i < 128; ++i)
            if (i != target && padMapping_[i] == target) inputs.add(juce::String(i));
        return inputs.joinIntoString(", ");
    };
    drumPads_.learnNote = [this](int pitch) {
        const bool arm = pitch >= 0 && learnTarget_.load() != pitch;
        cancelPadLearn();
        if (arm) {
            learnTarget_ = pitch;
            drumPads_.setLearning(true);
            statusLabel_.setText(TRANS("Strike the MIDI pad to assign"), juce::dontSendNotification);
        }
    };
    drumPads_.trigger = [this](int row, bool down) {
        if (!project_ || !selectedTrack_ || *selectedTrack_ >= project_->slotsByTrack.size())
            return;
        const auto &pads = project_->slotsByTrack[*selectedTrack_];
        if (row < 0 || row >= int(pads.size()))
            return;
        const auto &pad = pads[row];
        if (recordTrack_) {
            if (*recordTrack_ == *selectedTrack_ && pad.pitch >= 0)
                transport_.inputNote(down ? juce::MidiMessage::noteOn(10, pad.pitch, juce::uint8(100))
                                          : juce::MidiMessage::noteOff(10, pad.pitch));
        }
        if (down && pad.pitch >= 0) flashPad(pad.pitch);
    };
    drumSteps_.commit=[this](const auto& edits){applyNoteGesture(edits);};
    drumSteps_.loopChanged=[this]{updateDrumLoop();};

    arrangementZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
    arrangementZoom_.setRange(0.0, 1.0);
    arrangementZoom_.setDoubleClickReturnValue(true, 0.0);
    arrangementZoom_.textFromValueFunction = [this](double value) {
        if (!project_ || value == 0.0) return TRANS("Fit song");
        const double span = std::pow(project_->bars.endBar - project_->bars.firstBar, 1.0 - value);
        return (std::abs(span - std::round(span)) < 0.05 ? juce::String(juce::roundToInt(span)) : juce::String(span, 1))
            + (span < 1.05 ? TRANS(" bar") : TRANS(" bars"));
    };
    arrangementZoom_.setTextBoxStyle(juce::Slider::TextBoxRight, true, 72, 18);
    arrangementZoom_.setTitle(TRANS("Timeline horizontal zoom"));
    arrangementZoom_.setTooltip(TRANS("Drag right to zoom in. Double-click to fit the song."));
    arrangementZoom_.onValueChange = [this] { updateHorizontalRange(); repaint(); };
    addChildComponent(arrangementZoom_);
    for (auto* label : {&arrangementZoomLabel_, &heightLabel_}) {
        label->setText(label == &heightLabel_ ? TRANS("Height") : TRANS("Zoom"), juce::dontSendNotification);
        label->setFont(juce::FontOptions(12.0f));
        addChildComponent(label);
    }
    const std::pair<HeightGroup, const char*> scopes[] {
        {HeightGroup::all, "All tracks"}, {HeightGroup::global, "Global tracks"}, {HeightGroup::lanes, "Timeline lanes"},
        {HeightGroup::voicing, "Voicing"}, {HeightGroup::melody, "Melody"}, {HeightGroup::vocal, "Vocal"},
        {HeightGroup::arrangement, "Arrangement tracks"}, {HeightGroup::pitched, "Pitched tracks"},
        {HeightGroup::drums, "Drum tracks"}, {HeightGroup::audio, "Audio tracks"},
        {HeightGroup::outputs, "Output tracks"}, {HeightGroup::master, "Master"},
        {HeightGroup::bus, "Buses"}, {HeightGroup::returns, "Returns"}
    };
    for (const auto& [group, name] : scopes) {
        if (group == HeightGroup::global || group == HeightGroup::arrangement || group == HeightGroup::outputs)
            heightScope_.addSeparator();
        heightScope_.addItem(juce::translate(name), static_cast<int>(group));
    }
    heightScope_.setLookAndFeel(&heightMenuStyle_);
    heightScope_.setTitle(TRANS("Track height group"));
    heightScope_.setTooltip(TRANS("Choose which tracks to resize together."));
    heightScope_.setSelectedId(static_cast<int>(HeightGroup::all), juce::dontSendNotification);
    heightScope_.onChange = [this] { refreshGroupHeight(); };
    addChildComponent(heightScope_);
    groupHeight_.setTitle(TRANS("Group track height"));
    groupHeight_.setTooltip(TRANS("Set all rows in the selected group to the same height."));
    groupHeight_.setRange(TrackRowSize::minimum, TrackRowSize::maximum, 1);
    groupHeight_.setSliderStyle(juce::Slider::LinearHorizontal);
    groupHeight_.textFromValueFunction = [this](double value) {
        return mixedHeights_ ? TRANS("Mixed") : juce::String(juce::roundToInt(value)) + " px";
    };
    groupHeight_.setTextBoxStyle(juce::Slider::TextBoxRight, true, 50, 18);
    groupHeight_.onValueChange = [this] { applyGroupHeight(); };
    groupHeight_.onDragEnd = [this] { if (mixedHeights_) applyGroupHeight(); };
    addChildComponent(groupHeight_);
    arrangementScroll_.setComponentID("arrangement-horizontal-scroll");
    arrangementScroll_.setAutoHide(false);
    arrangementScroll_.setSingleStepSize(1.0);
    arrangementScroll_.addListener(this);
    addChildComponent(arrangementScroll_);
    addChildComponent(midiDrag_);
    midiDrag_.setButtonText(TRANS("Drag MIDI to another DAW"));
    midiDrag_.setTooltip(TRANS("Export the selected track as a MIDI file and drag it into another DAW."));
    midiDrag_.writeMidi = [this](juce::OutputStream& stream) { return selectedTrack_ && writeTrackMidi(*selectedTrack_, stream); };
    midiDrag_.reportError = [this](const juce::String& error) { statusLabel_.setText(error, juce::dontSendNotification); };
    addChildComponent(mixer_);
    pianoGrid_.selectionChanged = [this] { pianoInspector_.updateActions(pianoGrid_.grid > 1, pianoGrid_.selection.size()); repaint(); };
    addChildComponent(pianoGrid_);
    addChildComponent(pianoInspectorView_);
    pianoInspectorView_.setViewedComponent(&pianoInspector_, false);
    pianoInspectorView_.setScrollBarsShown(true, false);
    addChildComponent(pianoDivider_);
    addChildComponent(parameterDivider_);
    parameterDivider_.setComponentID("piano-parameter-divider");
    parameterDivider_.resizeAt = [this](int y) {
        parameterFraction_ = std::clamp(double(parameterBottom_ - y) / std::max(1, parameterHeight_), 0.08, 0.65);
        if (auto* prefs = viewPreferences_.getUserSettings())
            prefs->setValue("parameterLaneRatio", parameterFraction_);
        resized();
        repaint();
    };
    addChildComponent(pitchZoom_);
    pitchZoom_.setComponentID("piano-pitch-zoom");
    pitchZoom_.addItem(TRANS("Pitch rows: fit notes"), 1);
    for (int n : {8, 12, 18, 24, 36, 48, 72, 128})
        pitchZoom_.addItem(TRANS("Pitch rows") + "  " + juce::String(n), n);
    const int pitchRows = viewPreferences_.getUserSettings()->getIntValue("pitchRows", 1);
    pitchZoom_.setSelectedId(pitchZoom_.indexOfItemId(pitchRows) >= 0 ? pitchRows : 1, juce::dontSendNotification);
    pitchZoom_.onChange = [this] {
        const int rows = pitchZoom_.getSelectedId();
        if (auto* prefs = viewPreferences_.getUserSettings())
            prefs->setValue("pitchRows", rows);
        if (rows == 1)
            pianoGrid_.fitPitchRange();
        else
            pianoGrid_.setPitchRows(rows);
        repaint();
    };
    addChildComponent(columnDivider_);
    addChildComponent(arrangementColumnDivider_);
    arrangementColumnDivider_.setComponentID("arrangement-column-divider");
    arrangementColumnDivider_.resizeAt = columnDivider_.resizeAt = [this](int left) {
        const int width = left + columnGripWidth - rowOverlay_.getX();
        if (width == trackHeaderWidth) return;
        preferredHeaderWidth_ = std::clamp(width, 400, 760);
        if (auto* prefs = viewPreferences_.getUserSettings()) prefs->setValue("trackHeaderWidth", preferredHeaderWidth_);
        resized();
        repaint();
    };
    addChildComponent(browserDivider_);
    browserDivider_.setComponentID("plugin-browser-divider");
    browserDivider_.setTooltip(TRANS("Drag to resize the plug-in browser."));
    browserDivider_.resizeAt = [this](int left) {
        const auto full = workspaceBounds(getLocalBounds(), topBasicsHeight());
        const int width = browserWidthFor(full.getWidth(), full.getRight() - left - columnGripWidth);
        if (pluginBrowser_ && width != pluginBrowser_->getWidth()) {
            preferredBrowserWidth_ = width;
            viewPreferences_.getUserSettings()->setValue("pluginBrowserWidth", width);
            resized();
            repaint();
        }
    };
    pianoDivider_.resizeAt = [this](int y) {
        const auto workspace = getWorkspace();
        arrangementFraction_ = std::clamp(double(y - workspace.getY()) / std::max(1, workspace.getHeight()), 0.18, 0.82);
        if (auto* prefs = viewPreferences_.getUserSettings()) prefs->setValue("arrangementFraction", arrangementFraction_);
        resized();
        repaint();
    };
    addChildComponent(pianoVelocity_);
    pianoGrid_.tickRange = [this] {
        const auto span = horizontalRange();
        const auto bar = project_ ? project_->ticksPerBar : 1;
        return juce::Range<double>((span.start - 1) * bar, (span.end - 1) * bar);
    };
    pianoGrid_.commit = [this](const auto& edits) { applyNoteGesture(edits); };
    pianoGrid_.undoRedo = [this](bool redo) { undoPianoEdit(redo); };
    addChildComponent(pianoColours_);
    pianoColours_.setComponentID("piano-note-colours");
    pianoColours_.setTitle(TRANS("Note colours"));
    pianoColours_.setTooltip(TRANS("Colour notes and pitch rows by the harmony at each beat. Hover a note for its interval."));
    pianoColours_.addItem(TRANS("Piano keys"), plainNotes);
    pianoColours_.addItem(TRANS("Chord mode"), chordNotes);
    pianoColours_.addItem(TRANS("Scale mode"), scaleNotes);
    pianoColours_.addItem(TRANS("Chord + scale"), chordAndScaleNotes);
    const auto* settings = viewPreferences_.getUserSettings();
    if (settings) preferredBrowserWidth_ = settings->getIntValue("pluginBrowserWidth", 230);
    if (settings) preferredHeaderWidth_ = std::clamp(settings->getIntValue("trackHeaderWidth", 487), 400, 760);
    if (settings) arrangementFraction_ = std::clamp(settings->getDoubleValue("arrangementFraction", .6), .18, .82);
    const int colourMode = settings ? settings->getIntValue("pianoColourMode", chordNotes) : chordNotes;
    pianoColours_.setSelectedId(std::clamp(colourMode, int(plainNotes), int(chordAndScaleNotes)), juce::dontSendNotification);
    pianoColours_.onChange = [this] {
        if (auto* prefs = viewPreferences_.getUserSettings()) prefs->setValue("pianoColourMode", pianoColours_.getSelectedId());
        if (!theory_ || (!theoryVisible_ && pianoColours_.getSelectedId() == plainNotes)) refreshTheory();
        resized(); repaint();
    };
    addChildComponent(pianoSnap_);
    pianoSnap_.setTitle(TRANS("Note grid"));
    pianoSnap_.setTooltip(TRANS("Snap note edits to this grid"));
    for (int division : {4, 8, 16, 32}) pianoSnap_.addItem(TRANS("Grid") + " 1/" + juce::String(division), division);
    pianoSnap_.addItem(TRANS("Grid") + " 1/8 T", 12);
    pianoSnap_.addItem(TRANS("Grid") + " 1/16 T", 24);
    pianoSnap_.addItem(TRANS("Grid off"), 1);
    pianoSnap_.setSelectedId(16, juce::dontSendNotification);
    pianoSnap_.onChange = [this] {
        pianoGrid_.grid = pianoSnap_.getSelectedId() == 1 ? 1
            : std::max(1.0, pianoGrid_.quarter * 4.0 / pianoSnap_.getSelectedId());
        pianoGrid_.selectionChanged();
        if (project_ && drumSteps_.song == project_.get()) {
            drumSteps_.grid = pianoGrid_.grid > 1 ? pianoGrid_.grid : std::max(1., project_->ppq / 4.);
            drumSteps_.repaint();
        }
        repaint();
    };
    pianoGrid_.previewEdit = [this](const auto& edits) { previewNoteFit(edits); };
    pianoGrid_.inspectNote = [this](const auto& n) { inspectedNote_ = n; pianoGrid_.setDescription(noteFitReading()); repaint(); };
    pianoGrid_.theoryHint = [this](juce::int64 tick, const ostfstudio::PitchedNote& n) -> juce::String {
        const int mode = pianoColours_.getSelectedId();
        if (mode == plainNotes || !theory_ || !pitchedSelection() || !n.enabled || n.fixedTrigger) return {};
        const auto i = theory_->firstCell(double(tick));
        if (i >= theory_->cells.size()) return {};
        const auto& cell = theory_->cells[i];
        juce::StringArray details;
        details.add(juce::MidiMessage::getMidiNoteName(n.pitch + auditionShift_, true, true, 4));
        const auto s = theory_->scaleForNote(theoryTracks_[*selectedTrack_][i], n);
        if (theory_->chordMask(cell)) {
            const auto& h = theory_->chords[cell.chord];
            details.add(ostfstudio::shiftedLabel(h.name, auditionShift_) + "  " + TRANS("Interval") + " " + theory_->interval(n.pitch, h.root) + " / "
                + toneName(theory_->tone(n.pitch, &h, s >= 0 ? theory_->scales[s].mask : 0)));
        }
        if (mode != chordNotes && s >= 0 && theory_->scales[s].mask)
            details.add(ostfstudio::shiftedLabel(theory_->scales[s].name, auditionShift_) + "  " + TRANS("Interval") + " " + theory_->interval(n.pitch, theory_->scales[s].tonic) + " / " + (theory_->contains(theory_->scales[s].mask, n.pitch)
                ? TRANS("In scale") : TRANS("Outside scale")));
        if (details.size() == 1) details.add(TRANS("No reference"));
        return details.joinIntoString("\n");
    };
    rowOverlay_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(rowOverlay_);
    basics_.setComponentID("song-basics-strip");
    addChildComponent(basics_);
    importTrackDrop_.setText(TRANS("Drop to add a new track"), juce::dontSendNotification);
    importTrackDrop_.setComponentID("import-new-track");
    importTrackDrop_.setJustificationType(juce::Justification::centred);
    importTrackDrop_.setInterceptsMouseClicks(false, false);
    importTrackDrop_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff234c53));
    importTrackDrop_.setColour(juce::Label::outlineColourId, juce::Colour(0xff73d8cc));
    addChildComponent(importTrackDrop_);
    ruler_.headerWidth = trackHeaderWidth;
    ruler_.alignHeaderRight = true;
    basics_.setVisible(project_ && workspaceMode_ == WorkspaceMode::arrangement);
    ruler_.setComponentID("arrangement-ruler");
    pianoRuler_.setComponentID("piano-ruler");
    const auto setupRuler = [this](ArrangementRuler& ruler, juce::ScrollBar& scroll) {
        ruler.setTitle(TRANS("Bar ruler"));
        ruler.setTooltip(TRANS("Click to position the playhead. Drag left or right to scroll."));
        ruler.visibleBars = [&scroll] { return scroll.getCurrentRange(); };
        ruler.playheadBar = [this] { return project_ && project_->ticksPerBar > 0 && cursorTick_ >= 0
            ? project_->bars.firstBar + double(cursorTick_) / project_->ticksPerBar : -1.0; };
        ruler.locate = [this, &scroll](double fraction) {
            if (!project_) return;
            const auto visible = scroll.getCurrentRange();
            const auto bar = visible.getStart() + fraction * visible.getLength();
            locateTransport(std::llround((bar - project_->bars.firstBar) * project_->ticksPerBar), &scroll);
        };
        ruler.pan = [this, &scroll](double fraction) { panTimeline(scroll, fraction); };
    };
    for (auto* pan : {&viewportPan_, &pianoGrid_.viewportPan, &pianoVelocity_.viewportPan, &controllerEditor_.viewportPan,
                      &clipLane_.viewportPan, &audioEditor_.viewportPan, &tempoEditor_.viewportPan})
        pan->scroll = [this](double fraction) { panArrangement(fraction); };
    setupRuler(ruler_, arrangementScroll_);
    setupRuler(pianoRuler_, arrangementScroll_);
    setupRuler(audioEditor_.recordRuler, arrangementScroll_);
    for (auto* r : {&pianoRuler_, &audioEditor_.recordRuler}) {
        r->setTooltip(TRANS("Shift-drag on the ruler to set the recording loop. Click to position the cursor."));
        r->cycleBars = [this] {
            if (!project_ || !cycleButton_.getToggleState() || editorCycle_.isEmpty()) return juce::Range<double>{};
            const auto bar = [&](auto tick) { return project_->bars.firstBar + double(tick) / project_->ticksPerBar; };
            return juce::Range<double>{bar(editorCycle_.getStart()), bar(editorCycle_.getEnd())};
        };
        r->chooseCycle = [this](double a, double b) {
            if (!project_ || recordTrack_) return;
            const auto visible = arrangementScroll_.getCurrentRange();
            const auto tick = [&](double fraction) {
                const auto bar = visible.getStart() + fraction * visible.getLength() - project_->bars.firstBar;
                return std::clamp<juce::int64>(std::llround(bar * project_->ticksPerBar), 0, project_->playbackEndTick());
            };
            auto left = tick(std::min(a, b)), right = tick(std::max(a, b));
            if (right <= left) return;
            markSessionEdited();
            editorCycle_ = {left, right};
            cycleButton_.setToggleState(true, juce::dontSendNotification);
            pianoRuler_.cycleMode = audioEditor_.recordRuler.cycleMode = true;
            pianoRuler_.repaint(); audioEditor_.recordRuler.repaint();
            if (!transportActive_ && !editorCycle_.contains(cursorTick_)) locateTransport(left);
            updateDrumLoop();
        };
    }
    addChildComponent(cycleButton_);
    addChildComponent(editorRecord_);
    editorRecord_.onClick = [this] {
        if (recordTrack_) { stopTransport(); return; }
        if (selectedTrack_) setRecordArm(*selectedTrack_, true);
        startTransport(true);
    };
    cycleButton_.setClickingTogglesState(true);
    cycleButton_.setTooltip(TRANS("Drag across the editor ruler to select the loop range"));
    cycleButton_.onClick = [this] {
        markSessionEdited();
        const bool on = cycleButton_.getToggleState();
        pianoRuler_.cycleMode = audioEditor_.recordRuler.cycleMode = on;
        if (on && editorCycle_.isEmpty() && project_) {
            const auto start = std::clamp<juce::int64>(cursorTick_, 0, project_->playbackEndTick() - 1);
            editorCycle_ = {start, std::min(project_->playbackEndTick(), start + project_->ticksPerBar)};
        }
        if (on && !transportActive_ && !editorCycle_.contains(cursorTick_)) locateTransport(editorCycle_.getStart());
        pianoRuler_.repaint(); audioEditor_.recordRuler.repaint(); updateDrumLoop();
    };
    auto* preferences = viewPreferences_.getUserSettings();
    deck_.click.setToggleState(preferences->getBoolValue("metronome"), juce::dontSendNotification);
    deck_.countIn.setSelectedId(1 + std::clamp(preferences->getIntValue("countInBars", preferences->getBoolValue("countIn") ? 1 : 0),
                                               0, TransportPanel::maxCountInBars), juce::dontSendNotification);
    transport_.metronome = deck_.click.getToggleState();
    deck_.click.onClick = [this] {
        transport_.metronome = deck_.click.getToggleState();
        viewPreferences_.getUserSettings()->setValue("metronome", deck_.click.getToggleState());
    };
    deck_.countIn.onChange = [this] { viewPreferences_.getUserSettings()->setValue("countInBars", deck_.countIn.getSelectedId() - 1); };
    audioEditor_.channels.onChange = [this] {
        if (selectedTrack_ && project_) {
            captureChannels_[project_->trackIds[int(*selectedTrack_)]] = audioEditor_.channels.getSelectedId();
            refreshMidiRoutes();
            markSessionEdited();
        }
    };
    addChildComponent(paneControl_);
    paneControl_.onClick = [this] {
        const juce::String target = audioEditor_.isVisible() && project_ && selectedTrack_ ? "audio:" + project_->trackIds[int(*selectedTrack_)]
            : drumSteps_.isVisible() ? juce::String("steps") : juce::String("piano");
        chooseControlTarget(paneControl_, target, targetCaption(target));
    };
    deck_.control.onClick = [this] { chooseControlTarget(deck_.control, "mix", TRANS("Transport and mixer")); };
    mixer_.control.onClick = [this] { chooseControlTarget(mixer_.control, "mix", TRANS("Transport and mixer")); };
    addChildComponent(clipLane_);
    clipLane_.setTitle(TRANS("Clips"));
    clipLane_.visible = [this] { return arrangementScroll_.getCurrentRange(); };
    clipLane_.select = [this](std::optional<double> bar) { selectEditorClip(bar, true); };
    addChildComponent(pianoRuler_);
    rowOverlay_.addAndMakeVisible(ruler_);
    rowOverlay_.addAndMakeVisible(aiButton_);
    const juce::StringArray studioNames{"AI Arrangement Studio", "AI Lyric Studio", "AI Vocal Studio"};
    studioModeIndex_ = std::clamp(viewPreferences_.getUserSettings()->getIntValue("aiStudioMode", 1), 1, 3) - 1;
    for (int i = 0; i < 3; ++i) {
        auto& button = aiStudios_[i];
        rowOverlay_.addAndMakeVisible(button);
        button.setButtonText(studioNames[i]);
        button.setTitle(studioNames[i]);
        button.setRadioGroupId(914);
        button.setClickingTogglesState(true);
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff17634f));
        button.setToggleState(i == studioModeIndex_, juce::dontSendNotification);
        button.onClick = [this, i] {
            studioModeIndex_ = i;
            viewPreferences_.getUserSettings()->setValue("aiStudioMode", i + 1);
            applyAiSelection();
        };
    }
    rowOverlay_.addChildComponent(modelPicker_);
    rowOverlay_.addChildComponent(fetchModels_);
    modelPicker_.setTitle(TRANS("Analysis model"));
    modelPicker_.onChange = [this] {
        const auto index = modelPicker_.getSelectedId() - 2;
        aiModel_ = index >= 0 && index < aiModels_.size() ? aiModels_[index] : juce::String{};
        viewPreferences_.getUserSettings()->setValue("studioChatGptModel", aiModel_);
        applyAiSelection();
    };
    fetchModels_.setTitle(TRANS("Refresh models"));
    fetchModels_.onClick = [this] { refreshAiModels(); };
    aiModel_ = viewPreferences_.getUserSettings()->getValue("studioChatGptModel");
    displayAiState(TRANS("ChatGPT disconnected"), 0xff773d36);
    if (viewPreferences_.getUserSettings()->getValue("surfacePort").isNotEmpty() || viewPreferences_.getUserSettings()->getValue("workspaceControlPorts").isNotEmpty())
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] { if (safe) safe->openController(false); });
    aiButton_.onClick = [this] { aiConnectionMenu(); };
    aiButton_.setTooltip(TRANS("Connect ChatGPT or choose the analysis model."));
    if (viewPreferences_.getUserSettings()->getBoolValue("studioChatGptBrowserLogin", false)) updateAiConnection(AccountAction::restore);
    addChildComponent(audioEditor_);
    addChildComponent(tempoEditor_);
    tempoEditor_.visibleBars = [this] { return arrangementScroll_.getCurrentRange(); };
    tempoEditor_.edit = [this](juce::int64 start, double from, juce::int64 end, double to) {
        if (!project_ || recordTrack_) return;
        const bool playing = transportActive_;
        if (playing) stopTransport();
        juce::MemoryBlock before;
        auto result = project_->drawTempo(start, from, end, to, before);
        if (result.wasOk() && !before.isEmpty()) {
            rememberScoreEdit(std::move(before));
            updateDocumentLabel();
            finishNoteEdit(false);
        } else if (result.failed()) {
            statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        }
        if (playing) startTransport(false);
    };
    audioEditor_.sourceClip = [this] { return selectedTrack_ ? clipOn(*selectedTrack_) : nullptr; };
    audioEditor_.clock = [this]() -> const ostfstudio::TempoMap& { return auditionClock_ ? *auditionClock_ : project_->clock; };
    audioEditor_.onSplice = [this] { openSplice(); };
    audioEditor_.selectWave = [this] {
        if (!clipLane_.clips.empty()) selectEditorClip(clipLane_.clips.front().bars.getStart(), false);
    };
    audioEditor_.onMelodyne = [this] { audioEditor_.selectWave(); editMelodyneCopy(); };
    audioEditor_.onApply = [this] { applyMelodyneCopy(); };
    audioEditor_.hasWorkingCopy = [this] {
        return project_ && selectedTrack_ && melodyneCopies_.contains(project_->trackIds[int(*selectedTrack_)]);
    };
    audioEditor_.onEdit = [this](const ostfstudio::ClipTiming& edit) {
        auto* clip = selectedTrack_ ? clipOn(*selectedTrack_) : nullptr;
        if (!clip || recordTrack_) return;
        const bool playing = transportActive_;
        if (playing) stopTransport();
        if (!edit.loop && edit.cycleTicks && edit.cycleTicks != clip->timing.cycleTicks)
            clip->durationTicks = std::clamp<juce::int64>(edit.cycleTicks, 1, project_->playbackEndTick() - clip->startTick);
        clip->timing = edit;
        markSessionEdited();
        finishNoteEdit(playing);
    };
    waveforms_.onChange = [this] { repaint(rowOverlay_.getBounds()); audioEditor_.repaint(); };
    verticalScroll_.setAutoHide(false);
    verticalScroll_.setComponentID("arrangement-vertical-scroll");
    verticalScroll_.setSingleStepSize(32);
    verticalScroll_.addListener(this);
    addChildComponent(verticalScroll_);
    setupOutputTracks();
    pluginFormats_.addFormat(std::make_unique<juce::VST3PluginFormat>());
    trackPicker_.setTitle(TRANS("Arrangement track"));
    trackPicker_.setTextWhenNothingSelected(TRANS("Select track"));
    trackPicker_.onChange = [this] {
        if (const auto id = trackPicker_.getSelectedId(); id > 0) selectTrack(static_cast<std::size_t>(id - 1));
    };
    mixer_.channelChanged = [this](std::size_t index) {
        markSessionEdited();
        auto& controls = *trackControls_[index];
        const auto& mix = mixer_.channel(index);
        controls.mute.setToggleState(mix.muted, juce::dontSendNotification);
        controls.cue.setToggleState(mix.cue, juce::dontSendNotification);
        controls.solo.setToggleState(mix.soloed, juce::dontSendNotification);
        controls.volume.setValue(mix.volumeDb, juce::dontSendNotification);
        controls.pan.setValue(mix.pan * 100.0f, juce::dontSendNotification);
        updateTransportMix();
        refreshController();
    };
    mixer_.routingChanged = [this] {
        markSessionEdited();
        for (std::size_t i = 0; i < outputControls_.size(); ++i) {
            outputControls_[i].volume.setValue(mixer_.outputVolume(i), juce::dontSendNotification);
            outputControls_[i].mute.setToggleState(mixer_.routing().outputMuted[i], juce::dontSendNotification);
        }
        updateTransportMix();
    };
    mixer_.requestReturn = [this] { editOutput(true); };
    mixer_.rowMenu = [this](bool output, std::size_t index) { showRowMenu(output, index); };
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
    statusLabel_.setFont(juce::FontOptions(13.0f));
    refreshTimelineLanes();
    newProject();
}

juce::StringArray MainComponent::getMenuBarNames() { return {TRANS("File"), TRANS("View"), TRANS("Track"), TRANS("Settings"), TRANS("External connections"), TRANS("Help")}; }

juce::PopupMenu MainComponent::timelineMenu() const {
    juce::PopupMenu lanes;
    for (std::size_t i = 0; i < std::size(selectableTimelineLanes); ++i)
        lanes.addItem(timelineMenuBase + static_cast<int>(i), juce::translate(timelineMenuLabels[i]), true,
                      (timelineLaneMask_ & laneBit(selectableTimelineLanes[i])) != 0);
    return lanes;
}

juce::PopupMenu MainComponent::musicMenu() const {
    juce::PopupMenu menu;
    const bool available = project_ && !recordTrack_;
    menu.addItem(musicDefaults, TRANS("Song defaults..."), available);
    menu.addItem(musicTransform, TRANS("Transpose or change tempo of score..."), available);
    menu.addItem(musicAudition, TRANS("Playback and display overrides..."), available, auditionShift_ || auditionRate_ != 1);
    menu.addSeparator();
    menu.addItem(undoScore, TRANS("Undo score edit"), available && noteHistoryCursor_ > 0);
    menu.addItem(redoScore, TRANS("Redo score edit"), available && noteHistoryCursor_ < noteHistory_.size());
    return menu;
}

int MainComponent::timelineRowHeight() const {
    const int height = groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)];
    return height > 0 ? height : defaultTimelineHeight;
}

int MainComponent::topBasicsHeight() const {
    if (!project_ || workspaceMode_ != WorkspaceMode::arrangement || getWidth() >= 2330) return 0;
    return SongBasicsPanel::stripHeight + 4;
}

void MainComponent::editSongField(SongBasicsPanel::Field field, const juce::String& value, int choice) {
    using namespace ostfstudio;
    if (!project_ || recordTrack_) return;
    auto reject = [&](const juce::String& message) {
        statusLabel_.setText(message, juce::dontSendNotification);
        basics_.sync(*project_);
    };
    juce::MemoryBlock before;
    if (field <= SongBasicsPanel::mood) {
        auto info = project_->songInformation();
        if (field == SongBasicsPanel::title) info.title = value;
        else {
            auto tags = juce::StringArray::fromTokens(value, ("," + juce::String::charToString(0x3001)), "");
            tags.trim();
            tags.removeEmptyStrings();
            (field == SongBasicsPanel::genre ? info.genres : info.moods) = tags;
        }
        auto result = project_->applySongInformation(info, &before);
        if (result.failed()) { reject(juce::translate(result.getErrorMessage())); return; }
        if (!before.isEmpty()) rememberScoreEdit(std::move(before));
        updateDocumentLabel();
        return;
    }
    SongMusicEdit edit;
    const int root = (choice - 10) / 8;
    const int mode = (choice - 10) % 8;
    if (field == SongBasicsPanel::key) {
        if (choice != 1 && choice != 2 && (choice < 10 || root >= 12 || mode > 1)) return;
        auto* obj = new juce::DynamicObject();
        edit.key = juce::var(obj);
        obj->setProperty("state", choice == 1 ? "unspecified" : choice == 2 ? "atonal" : "tonal");
        if (choice >= 10) {
            obj->setProperty("tonic", spelledClass(root));
            obj->setProperty("mode", mode == 0 ? "major" : "minor");
        }
    } else if (field == SongBasicsPanel::scale) {
        if (choice == 1) edit.removeScale = true;
        else {
            if (choice < 10 || root >= 12 || mode >= 7) return;
            const auto scale = juce::JSON::parse(project_->sourceScore.toString())["song"]["scale"];
            auto* obj = new juce::DynamicObject();
            edit.scale = juce::var(obj);
            obj->setProperty("id", scale.hasProperty("id") ? scale["id"].toString() : "scale-" + juce::Uuid().toString());
            obj->setProperty("tonic", spelledClass(root));
            obj->setProperty("label", chromaticNames()[root] + " " + SongBasicsPanel::scaleNames[mode]);
            juce::Array<juce::var> steps;
            for (int step : SongBasicsPanel::scaleSteps[mode]) steps.add(step);
            obj->setProperty("intervals", steps);
        }
    } else if (field == SongBasicsPanel::tempo) {
        const auto utf8 = value.toStdString();
        char* end = nullptr;
        edit.bpm = std::strtod(utf8.c_str(), &end);
        if (end == utf8.c_str() || *end || !std::isfinite(edit.bpm) || edit.bpm <= 0 || edit.bpm > 1000) {
            reject(TRANS("Enter a tempo above zero and at most 1000 BPM.")); return;
        }
    } else if (field == SongBasicsPanel::meter) {
        const auto parts = juce::StringArray::fromTokens(value, "/", "");
        if (parts.size() != 2 || !parts[0].trim().containsOnly("0123456789") || !parts[1].trim().containsOnly("0123456789")
            || (edit.numerator = parts[0].getIntValue()) <= 0 || (edit.denominator = parts[1].getIntValue()) <= 0) {
            reject(TRANS("Enter a time signature such as 4/4 or 6/8.")); return;
        }
    }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    auto result = project_->applySongMusic(edit, before);
    if (result.wasOk() && !before.isEmpty()) {
        rememberScoreEdit(std::move(before));
        inspectedNote_.reset();
        scaleReference_ = -1;
        theory_.reset();
        refreshTheory();
        finishNoteEdit(false);
        updateDocumentLabel();
    }
    if (playing) startTransport(false);
    if (result.failed()) reject(juce::translate(result.getErrorMessage()));
}

void MainComponent::editSongMusic(ostfstudio::SongMusicEdit::Mode mode) {
    using namespace ostfstudio;
    if (!project_ || recordTrack_) return;
    if (mode == SongMusicEdit::defaults) { editSongInformation(); return; }
    const bool audition = mode == SongMusicEdit::audition;
    auto dialog = std::make_unique<juce::AlertWindow>(
        audition ? TRANS("Playback and display overrides") : TRANS("Transpose or change tempo of score"),
        audition ? TRANS("Temporary MIDI pitch and playback speed. Reset with 0 semitones and 100%.")
                 : TRANS("Transpose pitched notes and harmony, and multiply all tempo changes. WAV recordings retain their original pitch and speed."),
        juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("shift", juce::String(audition ? auditionShift_ : 0), TRANS("Transpose (semitones)"));
    dialog->addTextEditor("rate", juce::String(audition ? auditionRate_ * 100 : 100, 4), TRANS("Tempo (%)"));
    dialog->addButton(TRANS("Apply"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(TRANS("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto* window = dialog.release();
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [=, safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_](int answer) {
            if (!safe || !answer || generation != safe->projectGeneration_ || safe->recordTrack_) return;
            auto text = [&](const char* id) { return window->getTextEditorContents(id).trim(); };
            auto fail = [&](const juce::String& msg) {
                safe->statusLabel_.setText(msg, juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, TRANS("Song settings"), msg);
            };
            SongMusicEdit edit; edit.mode = mode;
            double shift, percent;
            if (!SongMusicFields::readNumber(text("shift"), shift) || std::floor(shift) != shift || std::abs(shift) > 127
                || !SongMusicFields::readNumber(text("rate"), percent) || percent <= 0) { fail(TRANS("Enter whole semitones and a positive tempo percentage.")); return; }
            edit.semitones = int(shift); edit.rate = percent / 100;
            const bool playing = safe->transportActive_;
            if (playing) safe->stopTransport();
            juce::MemoryBlock before;
            auto result = audition ? safe->project_->checkAudition(edit.semitones, edit.rate) : safe->project_->applySongMusic(edit, before);
            if (result.wasOk()) {
                if (audition) {
                    safe->auditionShift_ = edit.semitones; safe->auditionRate_ = edit.rate;
                    safe->auditionClock_ = edit.rate == 1 ? std::nullopt : safe->project_->clock.scaled(edit.rate);
                } else if (!before.isEmpty()) {
                    safe->rememberScoreEdit(std::move(before));
                    safe->inspectedNote_.reset(); safe->scaleReference_ = -1; safe->theory_.reset(); safe->refreshTheory();
                    safe->finishNoteEdit(false);
                }
                safe->displayTransportPosition(std::clamp<juce::int64>(safe->cursorTick_, 0, safe->project_->playbackEndTick()));
                safe->updateDocumentLabel(); safe->repaint();
            }
            if (playing) safe->startTransport(false);
            if (result.failed()) fail(juce::translate(result.getErrorMessage()));
        }), true);
}

juce::PopupMenu MainComponent::theoryMenu() const {
    juce::PopupMenu menu;
    menu.addItem(showTheory, TRANS("Show Theory Guide"), project_ != nullptr, theoryVisible_);
    menu.addItem(showChordFit, TRANS("Chord tone fit"), theoryVisible_, chordFitVisible_);
    menu.addItem(showScaleFit, TRANS("Scale fit"), theoryVisible_, scaleFitVisible_);
    if (theory_ && !theory_->scales.empty()) {
        juce::PopupMenu refs;
        refs.addItem(scaleReferenceBase, TRANS("Automatic"), true, scaleReference_ == -1);
        for (int i = 0; i < static_cast<int>(theory_->scales.size()); ++i)
            refs.addItem(scaleReferenceBase + 1 + i, theory_->scales[i].name, true, scaleReference_ == i);
        menu.addSubMenu(TRANS("Scale reference"), refs, (theoryVisible_ && scaleFitVisible_) || pianoColours_.getSelectedId() >= scaleNotes);
    }
    return menu;
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu menu;
    switch (index) {
        case 0: {
            menu.addItem(createProject, TRANS("New project"));
            menu.addItem(openFile, TRANS("Open OSTF / OSTFP..."));
            juce::PopupMenu demos;
            for (std::size_t i = 0; i < std::size(gallery); ++i)
                demos.addItem(demoMenuBase + static_cast<int>(i), gallery[i].title);
            menu.addSubMenu(TRANS("Demo songs"), demos);
            menu.addItem(showSongInformation, TRANS("Song information..."), project_ != nullptr);
            menu.addSubMenu(TRANS("Song settings"), musicMenu(), project_ != nullptr);
            menu.addSeparator();
            menu.addItem(saveScore, TRANS("Save as OSTF..."), canEditTracks());
            menu.addItem(saveProject, TRANS("Save as OSTFP..."), canEditTracks());
            juce::PopupMenu midi;
            midi.addItem(exportMidi0, TRANS("Format 0 - single track"));
            midi.addItem(exportMidi1, TRANS("Format 1 - separate tracks"));
            menu.addSubMenu(TRANS("Save as MIDI (SMF)"), midi, project_ != nullptr);
            menu.addSeparator();
            menu.addItem(importMedia, TRANS("Import and transcription"));
            break;
        }
        case 1: {
            menu.addItem(showArrangement, TRANS("Arrangement"), true, workspaceMode_ == WorkspaceMode::arrangement);
            menu.addItem(showPiano, TRANS("Piano Roll"), project_ != nullptr, pianoButton_.getToggleState());
            menu.addItem(showMixer, TRANS("Mixer"), project_ != nullptr, mixer_.isVisible());
            menu.addItem(showPlugins, TRANS("Plug-in Browser"), true, pluginBrowserButton_.getToggleState());
            menu.addItem(showOutputs, TRANS("Output Tracks"), outputButton_.isEnabled(), outputButton_.getToggleState());
            menu.addSubMenu(TRANS("Global Timeline Lanes"), timelineMenu(), lanesButton_.isEnabled());
            menu.addSubMenu(TRANS("Theory Guide"), theoryMenu(), lanesButton_.isEnabled());
            menu.addSeparator();
            menu.addItem(showLeadSheet, TRANS("Lead sheet"), project_ != nullptr, leadButton_.getToggleState());
            menu.addItem(showPartScore, TRANS("Score"), project_ != nullptr, scoreButton_.getToggleState());
            break;
        }
        case 3: {
            menu.addItem(showDevices, TRANS("Audio / MIDI Devices..."));
            menu.addItem(aiConnections, TRANS("AI connections..."));
            menu.addItem(controllerSettings, TRANS("M-VAVE controllers..."));
            juce::PopupMenu themes;
            themes.addItem(themeBlue, TRANS("Studio Blue"), true, blueTheme());
            themes.addItem(themeHarmonic, TRANS("Harmonic colours"), true,
                           pianoInspector_.theme == pianoHarmony::Theme::harmonic);
            themes.addItem(themeGraphite, TRANS("Graphite"), true,
                           pianoInspector_.theme == pianoHarmony::Theme::graphite);
            themes.addItem(themeAubergine, TRANS("Aubergine"), true,
                           pianoInspector_.theme == pianoHarmony::Theme::aubergine);
            menu.addSubMenu(TRANS("Colour theme"), themes);
            juce::PopupMenu languages;
            const auto selected = viewPreferences_.getUserSettings()->getValue("language", "system");
            languages.addItem(languageSystem, TRANS("Use system language"), true, selected == "system");
            languages.addItem(languageEnglish, "English", true, selected == "en");
            languages.addItem(languageJapanese, juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"), true, selected == "ja");
            menu.addSubMenu(TRANS("Language"), languages);
            break;
        }
        case 4:
            menu.addItem(externalSplice, "Splice...");
            menu.addItem(externalMelodyne, "Melodyne...", !recordTrack_);
            menu.addSeparator();
            menu.addItem(externalLink, "Ableton Link", !recordTrack_, link_ != nullptr);
            if (link_) {
                const auto state = link_->state(linkQuantum());
                menu.addItem(linkTempoItem, TRANS("Link tempo") + "  " + juce::String(state.tempo, 2) + " BPM   /   " +
                             juce::String(int(state.peers)) + " " + TRANS("peers") + "...", !recordTrack_);
            }
            break;
        case 5:
            menu.addItem(showShortcuts, TRANS("Shortcuts..."));
            menu.addItem(showAbout, TRANS("About OSTF Studio"));
            break;
        case 2: {
            const auto editable = canEditTracks();
            const auto add = editable && project_->sourceTrackCount() < ostfstudio::OstfpPreview::maxTracks;
            menu.addItem(addMelody, TRANS("Add melody track"), add);
            menu.addItem(addChorus, TRANS("Add chorus vocal track"), add);
            menu.addItem(addPitched, TRANS("Add pitched track"), add);
            menu.addItem(addDrums, TRANS("Add drum / percussion track"), add);
            menu.addItem(addAudio, TRANS("Add audio track"), add);
            menu.addSeparator();
            menu.addItem(addBus, TRANS("Add bus track"), editable && mixer_.routing().busCount < ostfstudio::maxMixerBuses);
            menu.addItem(addReturn, TRANS("Add return track"), editable && mixer_.returnCount() < ostfstudio::maxMixerReturns);
            menu.addSeparator();
            menu.addItem(cursorPatchItem, TRANS("MIDI patch at cursor..."), editable && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            menu.addItem(initialPatchItem, TRANS("Initial MIDI patch..."), editable && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            menu.addItem(renameTrackItem, TRANS("Rename track..."), editable && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            menu.addItem(copyTrack, TRANS("Duplicate track"), add && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            menu.addItem(removeTrack, TRANS("Delete selected track..."), editable && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            break;
        }
    }
    return menu;
}

void MainComponent::menuItemSelected(int item, int) {
    if (item >= themeBlue && item <= themeAubergine) {
        pianoInspector_.theme = static_cast<pianoHarmony::Theme>(item - themeBlue);
        if (auto* config = viewPreferences_.getUserSettings()) {
            config->setValue("workspaceTheme", workspaceThemes[item - themeBlue]);
            viewPreferences_.saveIfNeeded();
        }
        menuItemsChanged();
        repaint();
        return;
    }
    if (item == musicSettings) {
        musicMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&musicButton_),
            [safe = juce::Component::SafePointer<MainComponent>(this)](int id) { if (safe && id) safe->menuItemSelected(id, 0); });
        return;
    }
    if (item >= musicDefaults && item <= musicAudition) {
        editSongMusic(static_cast<ostfstudio::SongMusicEdit::Mode>(item - musicDefaults + 1)); return;
    }
    if (item == undoScore || item == redoScore) { undoPianoEdit(item == redoScore); return; }
    if (item == showTheory || item == showChordFit || item == showScaleFit || item >= scaleReferenceBase) {
        if (!project_) return;
        if (item == showTheory) {
            theoryVisible_ = !theoryVisible_;
            theoryButton_.setToggleState(theoryVisible_, juce::dontSendNotification);
        }
        else if (item == showChordFit) chordFitVisible_ = !chordFitVisible_;
        else if (item == showScaleFit) scaleFitVisible_ = !scaleFitVisible_;
        else scaleReference_ = item - scaleReferenceBase - 1;
        if (item == showTheory || item >= scaleReferenceBase) refreshTheory();
        if (auto* config = viewPreferences_.getUserSettings()) {
            config->setValue("theoryGuide", theoryVisible_);
            config->setValue("showChordFit", chordFitVisible_);
            config->setValue("showScaleFit", scaleFitVisible_);
        }
        resized();
        repaint();
        return;
    }
    if (item >= demoMenuBase && item < demoMenuBase + static_cast<int>(std::size(gallery))) {
        int size = 0;
        if (const auto* data = BinaryData::getNamedResource(gallery[item - demoMenuBase].resource, size)) {
            juce::MemoryInputStream stream(data, static_cast<std::size_t>(size), false);
            openPreview(stream, BinaryData::getNamedResourceOriginalFilename(gallery[item - demoMenuBase].resource));
        } else {
            statusLabel_.setText(TRANS("Bundled demo is unavailable"), juce::dontSendNotification);
        }
        return;
    }
    if (item >= timelineMenuBase && item < timelineMenuBase + static_cast<int>(std::size(selectableTimelineLanes))) {
        const auto lane = selectableTimelineLanes[static_cast<std::size_t>(item - timelineMenuBase)];
        timelineLaneMask_ ^= laneBit(lane);
        if (!(timelineLaneMask_ & laneBit(lane)) && selectedGlobal_ == lane) {
            selectedGlobal_.reset();
            refreshEditor();
        }
        if (auto* config = viewPreferences_.getUserSettings()) config->setValue("timelineLanes", static_cast<int>(timelineLaneMask_));
        viewPreferences_.saveIfNeeded();
        refreshTimelineLanes();
        resized();
        repaint();
        return;
    }
    switch (item) {
        case openFile: inspectOSTF(); break;
        case createProject:
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, TRANS("New project"),
                TRANS("Start a new project? Unsaved changes in the open project will be lost."),
                TRANS("New project"), TRANS("Cancel"), this,
                juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this),
                    generation = projectGeneration_](int answer) {
                    if (answer && safe && safe->projectGeneration_ == generation) safe->newProject();
                }));
            break;
        case saveScore: saveDocument(SaveFormat::score); break;
        case showArrangement: setWorkspaceMode(WorkspaceMode::arrangement); break;
        case showLeadSheet: setWorkspaceMode(WorkspaceMode::leadSheet); break;
        case showPartScore: setWorkspaceMode(WorkspaceMode::score); break;
        case controllerSettings: openController(true); break;
        case aiConnections: aiConnectionMenu(); break;
        case importMedia:
            if (!importWindow_)
                importWindow_ = makeMediaImportWindow([this](std::unique_ptr<ostfstudio::OstfpPreview> draft) {
                    openImportedSong(std::move(draft));
                    importWindow_->setVisible(false);
                });
            setImportConnections(*importWindow_, activeAi_ ? std::vector{activeAi_} : std::vector<ostfstudio::AnalysisConnection>{});
            importWindow_->setVisible(true);
            importWindow_->toFront(true);
            break;
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
            editorVisible_ = workspaceMode_ != WorkspaceMode::arrangement
                          || !(item == showMixer ? mixerButton_ : pianoButton_).getToggleState();
            workspaceMode_ = WorkspaceMode::arrangement;
            mixerSelected_ = editorVisible_ && item == showMixer;
            refreshEditor();
            break;
        case showDevices:
            if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
            deviceSettings_->devices();
            deviceSettings_->setVisible(true);
            deviceSettings_->toFront(true);
            break;
        case showPlugins: togglePluginBrowser(); break;
        case showOutputs: showOutputTracks_ = !showOutputTracks_; refreshEditor(); break;
        case showSongInformation: editSongInformation(); break;
        case smallerUI: changeInterfaceScale(-1); break;
        case largerUI: changeInterfaceScale(1); break;
        case addMelody:
        case addChorus:
            editTracks(ostfstudio::TrackKind::pitched, {}, {}, item == addMelody ? ostfstudio::MixerTrackGroup::melody : ostfstudio::MixerTrackGroup::vocal);
            break;
        case addBus: editOutput(false); break;
        case addReturn: editOutput(true); break;
        case cursorPatchItem: if (selectedTrack_) showPatch(*selectedTrack_, cursorTick_); break;
        case initialPatchItem: if (selectedTrack_) showPatch(*selectedTrack_, 0); break;
        case renameTrackItem: if (selectedTrack_) renameTrack(*selectedTrack_); break;
        case copyTrack:
            if (selectedTrack_) editTracks(ostfstudio::TrackKind::pitched, {}, selectedTrack_);
            break;
        case addPitched: editTracks(ostfstudio::TrackKind::pitched); break;
        case addDrums: editTracks(ostfstudio::TrackKind::drum); break;
        case addAudio: editTracks(ostfstudio::TrackKind::audio); break;
        case removeTrack:
            if (selectedTrack_) confirmRemoval(false, *selectedTrack_);
            break;
        case externalSplice: openSplice(); break;
        case externalMelodyne: editMelodyneCopy(); break;
        case externalLink: toggleLink(); break;
        case linkTempoItem: editLinkTempo(); break;
        case showShortcuts: showShortcutList(); break;
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
    auto bounds = workspaceBounds(getLocalBounds(), topBasicsHeight());
    if (pluginBrowserButton_.getToggleState()) bounds.removeFromRight(browserWidthFor(bounds.getWidth(), preferredBrowserWidth_) + columnGripWidth);
    return bounds;
}

int MainComponent::getArrangementHeight(int workspaceHeight) const {
    if (!editorVisible_) return workspaceHeight;
    const int navigationRows = getWorkspace().getWidth() - 26 < 765 ? 2 : 1;
    const bool compactClips = workspaceHeight < 320 && clipLane_.isVisible();
    const int minimum = std::min(workspaceHeight, (compactClips ? 2 : 26) + navigationRows * timelineNavigationHeight
                                              + timelineRowHeight() + TrackRowSize::minimum);
    const double fraction = mixerButton_.getToggleState() ? 0.5 : arrangementFraction_;
    return std::clamp(juce::roundToInt(workspaceHeight * fraction), minimum, std::max(minimum, workspaceHeight - (clipLane_.isVisible() ? 160 : 136)));
}

void MainComponent::scrollBarMoved(juce::ScrollBar* bar, double) {
    clipLane_.repaint();
    if (bar == &verticalScroll_) resized();
    if (bar == &arrangementScroll_) tempoEditor_.repaint();
    repaint();
}

void MainComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (((rowOverlay_.isVisible() && rowOverlay_.getBounds().contains(event.getPosition()))
         || (pianoGrid_.isVisible() && pianoGrid_.getBounds().contains(event.getPosition())))
        && (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))) {
        const auto delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        panArrangement(-0.32 * delta);
        return;
    }
    if (verticalScroll_.isVisible() && rowOverlay_.getBounds().contains(event.getPosition())
        && std::abs(wheel.deltaY) > std::abs(wheel.deltaX)) {
        verticalScroll_.mouseWheelMove(event.getEventRelativeTo(&verticalScroll_), wheel);
        return;
    }
    Component::mouseWheelMove(event, wheel);
}

void MainComponent::panArrangement(double fraction) {
    panTimeline(arrangementScroll_, fraction);
}

void MainComponent::panTimeline(juce::ScrollBar& scroll, double fraction) {
    if (!scroll.isEnabled()) return;
    const auto range = scroll.getCurrentRange();
    scroll.setCurrentRangeStart(range.getStart() + fraction * range.getLength(), juce::sendNotificationSync);
    if (transportActive_ && scroll.getCurrentRangeStart() != range.getStart())
        deck_.follow.setToggleState(false, juce::dontSendNotification);
}

void MainComponent::updateHorizontalRange(bool reset) {
    if (!project_) return;
    auto& zoom = arrangementZoom_;
    auto& scroll = arrangementScroll_;
    const auto song = project_->bars;
    const auto length = song.endBar - song.firstBar;
    if (reset) {
        const double initialBars = std::min(10.0, double(length));
        zoom.setValue(length > 1 ? 1.0 - std::log(initialBars) / std::log(length) : 0.0, juce::dontSendNotification);
    }
    const double visible = std::pow(length, 1.0 - zoom.getValue());
    double start = reset ? double(song.firstBar) : scroll.getCurrentRangeStart();
    if (!reset && cursorTick_ >= 0 && project_->ticksPerBar > 0) {
        const double cursor = song.firstBar + double(cursorTick_) / project_->ticksPerBar;
        const auto previous = scroll.getCurrentRange();
        if (previous.contains(cursor))
            start = cursor - (cursor - previous.getStart()) * visible / previous.getLength();
    }
    scroll.setRangeLimits(song.firstBar, song.endBar, juce::dontSendNotification);
    scroll.setCurrentRange(start, visible, juce::dontSendNotification);
    scroll.setEnabled(visible < length);
    zoom.setEnabled(length > 1);
    zoom.updateText();
    tempoEditor_.repaint();
}

ostfstudio::TimelineExtent MainComponent::horizontalRange() const {
    const auto& scroll = arrangementScroll_;
    return {scroll.getCurrentRangeStart(), scroll.getCurrentRange().getEnd()};
}

MainComponent::HeightGroup MainComponent::trackHeightGroup(std::size_t index) const {
    if (project_->chordVoicingTrack == index) return HeightGroup::voicing;
    if (project_->mixerGroups[index] == ostfstudio::MixerTrackGroup::melody) return HeightGroup::melody;
    if (project_->mixerGroups[index] == ostfstudio::MixerTrackGroup::vocal) return HeightGroup::vocal;
    if (project_->trackKinds[index] == ostfstudio::TrackKind::drum) return HeightGroup::drums;
    if (project_->trackKinds[index] == ostfstudio::TrackKind::audio) return HeightGroup::audio;
    return HeightGroup::pitched;
}

MainComponent::HeightGroup MainComponent::outputHeightGroup(std::size_t index) {
    if (index == 0) return HeightGroup::master;
    return ostfstudio::isReturnOutput(index) ? HeightGroup::returns : HeightGroup::bus;
}

bool MainComponent::includesHeightGroup(HeightGroup scope, HeightGroup group) {
    switch (scope) {
        case HeightGroup::all: return true;
        case HeightGroup::global: return group >= HeightGroup::global && group <= HeightGroup::vocal;
        case HeightGroup::arrangement: return group >= HeightGroup::arrangement && group <= HeightGroup::audio;
        case HeightGroup::outputs: return group >= HeightGroup::outputs && group <= HeightGroup::returns;
        default: return scope == group;
    }
}

void MainComponent::applyGroupHeight() {
    if (!project_) return;
    const auto scope = static_cast<HeightGroup>(heightScope_.getSelectedId());
    const int height = juce::roundToInt(groupHeight_.getValue());
    for (std::size_t i = 1; i < groupDefaults_.size(); ++i)
        if (includesHeightGroup(scope, static_cast<HeightGroup>(i))) groupDefaults_[i] = height;
    for (std::size_t i = 0; i < trackControls_.size(); ++i)
        if (includesHeightGroup(scope, trackHeightGroup(i))) trackControls_[i]->size.assignHeight(height);
    for (std::size_t i = 0; i < outputControls_.size(); ++i)
        if (includesHeightGroup(scope, outputHeightGroup(i))) outputControls_[i].size.assignHeight(height);
    mixedHeights_ = false;
    groupHeight_.updateText();
    resized();
    repaint();
}

void MainComponent::refreshGroupHeight() {
    if (!project_) return;
    const auto scope = static_cast<HeightGroup>(heightScope_.getSelectedId());
    groupHeight_.setRange(scope == HeightGroup::lanes ? 18 : TrackRowSize::minimum, TrackRowSize::maximum, 1);
    int first = 0;
    mixedHeights_ = false;
    auto measure = [&](HeightGroup group, int height) {
        if (!includesHeightGroup(scope, group)) return;
        if (first == 0) first = height;
        else if (first != height) mixedHeights_ = true;
    };
    measure(HeightGroup::lanes, groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)]);
    for (std::size_t i = 0; i < trackControls_.size(); ++i)
        measure(trackHeightGroup(i), trackControls_[i]->size.height());
    for (std::size_t row = 0; row < mixer_.routing().outputCount(); ++row) {
        const auto slot = mixer_.routing().outputAt(row);
        measure(outputHeightGroup(slot), outputControls_[slot].size.height());
    }
    groupHeight_.setValue(first ? first : groupDefaults_[static_cast<std::size_t>(scope)], juce::dontSendNotification);
    groupHeight_.updateText();
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
    return workspaceMode_ == WorkspaceMode::arrangement && project_ &&
           (dynamic_cast<ImportResultRow*>(details.sourceComponent.get()) || id == generatorDragId ||
            (pluginBrowser_ && id.startsWith("ostf-vst3:")));
}

std::optional<std::size_t> MainComponent::pluginDropTrack(juce::Point<int> position) const {
    if (!rowOverlay_.getBounds().withTrimmedTop(timelineRowHeight()).contains(position)) return std::nullopt;
    for (std::size_t i = 0; i < std::min(trackRows_.size(), trackControls_.size()); ++i)
        if (trackRows_[i].contains(position) && !trackControls_[i]->loading) return i;
    return std::nullopt;
}

std::size_t MainComponent::visibleOutputCount() const {
    return project_ && showOutputTracks_ ? mixer_.routing().outputCount() : 0;
}

std::optional<std::size_t> MainComponent::outputDropTrack(juce::Point<int> position) const {
    if (!rowOverlay_.getBounds().withTrimmedTop(timelineRowHeight()).contains(position)) return std::nullopt;
    for (std::size_t row = 0; row < visibleOutputCount(); ++row) {
        const auto i = mixer_.routing().outputAt(row);
        if (outputRows_[i].contains(position) && outputControls_[i].effects.loading == 0) return i;
    }
    return std::nullopt;
}

void MainComponent::itemDragMove(const SourceDetails& details) {
    const auto id = details.description.toString();
    const auto plugin = pluginBrowser_ && id.startsWith("ostf-vst3:") ? pluginBrowser_->find(id.substring(10)) : std::nullopt;
    const auto output = plugin && !plugin->isInstrument ? outputDropTrack(details.localPosition) : std::nullopt;
    auto track = pluginDropTrack(details.localPosition);
    const auto* part = dynamic_cast<ImportResultRow*>(details.sourceComponent.get());
    importTrackDrop_.setVisible(part && part->index >= 0 && canEditTracks() && !recordTrack_);
    if (importTrackDrop_.isVisible()) {
        importTrackDrop_.toFront(false);
        if (importTrackDrop_.getBounds().contains(details.localPosition)) track.reset();
    }
    if (auto* row = part; row && track) {
        if (*track >= project_->sourceTrackCount() || row->index < 0 ||
            project_->trackKinds[*track] != row->source.trackKinds[static_cast<std::size_t>(row->index)])
            track.reset();
    }
    if (track == pluginDropHighlight_ && output == outputDropHighlight_) return;
    outputDropHighlight_ = output;
    pluginDropHighlight_ = track;
    repaint();
}

void MainComponent::itemDragExit(const SourceDetails&) {
    importTrackDrop_.setVisible(false);
    if (!pluginDropHighlight_ && !outputDropHighlight_) return;
    outputDropHighlight_.reset();
    pluginDropHighlight_.reset();
    repaint();
}

void MainComponent::itemDropped(const SourceDetails& details) {
    itemDragExit(details);
    if (!isInterestedInDragSource(details)) return;
    const auto id = details.description.toString();
    if (auto* row = dynamic_cast<ImportResultRow*>(details.sourceComponent.get())) {
        auto pos = details.localPosition;
        const auto track = pluginDropTrack(pos);
        const auto lane = globalLaneAt(pos);
        if (!rowOverlay_.getBounds().contains(pos)) return;
        const bool matched = track && *track < project_->sourceTrackCount() && row->index >= 0
            && project_->trackKinds[*track] == row->source.trackKinds[size_t(row->index)];
        const bool create = row->index >= 0 && (!matched || importTrackDrop_.getBounds().contains(pos));
        if (row->index < 0 && lane != TimelineLane::chord) {
            statusLabel_.setText(TRANS("Drop the progression on the chord lane."), juce::dontSendNotification);
            return;
        }
        if (recordTrack_)
            return;
        const auto grid = std::max(1.0, pianoGrid_.grid);
        const auto hit = arrangementTickAt(pos.x);
        auto tick = static_cast<juce::int64>(std::llround(std::round(hit / grid) * grid));
        if (pos.x < rowOverlay_.getX() + trackHeaderWidth) tick = std::max<juce::int64>(0,cursorTick_);
        if (create) {
            const auto i = static_cast<std::size_t>(row->index);
            if (!canEditTracks() || project_->sourceTrackCount() >= ostfstudio::OstfpPreview::maxTracks) {
                statusLabel_.setText(TRANS("Choose a matching destination track."),juce::dontSendNotification);
                return;
            }
            if (i < row->source.sourceTrackCount())
                editTracks(row->source.trackKinds[i], {}, {}, row->source.mixerGroups[i], row, tick);
            return;
        }
        if (row->index == -1) {
            const auto score = juce::JSON::parse(project_->sourceScore.toString());
            const auto chord = ostfstudio::globalEventAt(score, TimelineLane::chord, hit);
            if (chord.isObject()) tick = static_cast<juce::int64>(chord["start"]);
        }
        placeResult(*row, track, tick, false);
        return;
    }
    if (const auto output = outputDropTrack(details.localPosition); output && id.startsWith("ostf-vst3:")) {
        if (const auto plugin = pluginBrowser_->find(id.substring(10)); plugin && !plugin->isInstrument)
            appendEffect(true, *output, *plugin);
        return;
    }
    const auto track = pluginDropTrack(details.localPosition);
    if (!track) return;
    if (details.description.toString() == generatorDragId) {
        const auto timeline = trackRows_[*track].withTrimmedLeft(trackHeaderWidth);
        const auto visible = horizontalRange();
        const auto x = juce::jlimit(0, timeline.getWidth() - 1, details.localPosition.x - timeline.getX());
        const auto bar = static_cast<int>(std::floor(visible.start + x * (visible.end - visible.start) / timeline.getWidth()));
        if (project_->insertGeneratorPattern(*track, bar)) {
            refreshTrackTheory(*track);
            selectTrack(*track);
            markSessionEdited();
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

void MainComponent::placeResult(ImportResultRow& row, std::optional<std::size_t> track, juce::int64 tick,
                                bool replace) {
    if (!project_ || recordTrack_)
        return;
    bool playing = transportActive_;
    if (playing)
        stopTransport();
    juce::MemoryBlock before;
    auto result = ostfstudio::placeImportTrack(row.source, row.index, *project_, track, tick, before, replace);
    if (result.wasOk()) {
        if (row.index >= 0 && row.source.trackKinds[row.index] == ostfstudio::TrackKind::audio)
            markSessionEdited();
        else if (!before.isEmpty())
            rememberScoreEdit(std::move(before));
        else
            markSessionEdited();
        waveforms_.load(project_->audio);
        theory_.reset();
        refreshTheory();
        if (track)
            selectTrack(*track);
        finishNoteEdit(playing);
    } else if (playing)
        startTransport(false);
    if (result.getErrorMessage() == TRANS("Confirm replacement of existing material.")) {
        auto safe = juce::Component::SafePointer<MainComponent>(this);
        auto source = juce::Component::SafePointer<ImportResultRow>(&row);
        auto generation = projectGeneration_;
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon, TRANS("Replace"),
            row.index == -1 ? TRANS("Replace the chords covered by this progression?")
                            : TRANS("Replace the WAV on this track?"),
            TRANS("Replace"), TRANS("Cancel"), this,
            juce::ModalCallbackFunction::create([safe, source, generation, track, tick](int answer) {
                if (answer && safe && source && safe->projectGeneration_ == generation)
                    safe->placeResult(*source, track, tick, true);
            }));
        return;
    }
    statusLabel_.setText(result.wasOk() ? TRANS("Import result placed.") : juce::translate(result.getErrorMessage()),
                         juce::dontSendNotification);
}

void MainComponent::updateTransportMix() {
    if (!project_) return;
    for (std::size_t i = 0; i < trackControls_.size(); ++i)
        transport_.setChannelMix(i, mixer_.channel(i), mixer_.isAudible(i), mixer_.routing().cueDb);
    transport_.setOutputMix(mixer_.routing());
}

void MainComponent::openController(bool show) {
    if (!surfaceWindow_) {
        surfaceWindow_ = std::make_unique<PluginWindow>("M-VAVE");
        surfacePanel_ = new ControllerSurface();
        auto* tabs = new juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop);
        presetEditor_ = new SmkPresetEditor(*viewPreferences_.getUserSettings());
        tabs->addTab(TRANS("Studio assignments"), juce::Colour(panel), surfacePanel_, true);
        tabs->addTab(TRANS("SMK-25 II preset editor"), juce::Colour(panel), presetEditor_, true);
#if JUCE_WINDOWS && defined(_MSC_VER)
        surfacePanel_->recommended.onClick = [this, tabs] {
            if (recordTrack_) return;
            if (surfaceMap_.port.isEmpty()) surfacePanel_->selectOnlySmkInput();
            tabs->setCurrentTabIndex(1);
            presetEditor_->beginRecommended();
        };
#endif
        tabs->setSize(790, 508);
        surfaceWindow_->setContentOwned(tabs, true);
        surfaceWindow_->centreWithSize(tabs->getWidth(), tabs->getHeight());
        presetEditor_->useRig = [this](const juce::String& port, bool enabled) {
            if (recordTrack_ || port.isEmpty() || surfaceMap_.port != port || !surfaceMap_.learnable()) return false;
            {
                const juce::SpinLock::ScopedLockType lock(surfaceLock_);
                surfaceMap_.studioRig = enabled;
                surfaceCount_ = 0;
                surfaceLearn_ = -1;
            }
            saveControllerBindings();
            return true;
        };
        presetEditor_->changeTarget = [this](int mode) {
            rigTarget_ = std::clamp(mode, 0, 2);
            viewPreferences_.getUserSettings()->setValue("rigControlTarget", rigTarget_);
            const juce::String target = mode == 1 ? "mix" : mode == 2 ? "selected" : "follow";
            for (auto port : juce::MidiInput::getAvailableDevices()) if (port.identifier == surfaceMap_.port) {
                assignControlTarget(port, target, targetCaption(target));
                break;
            }
            refreshController();
        };
        surfacePanel_->controlTarget.onChange = [this] { presetEditor_->changeTarget(surfacePanel_->controlTarget.getSelectedId() - 1); };
        presetEditor_->resetRig = [this](const juce::String& port) {
            if (surfaceMap_.port != port || recordTrack_) return false;
            const auto key = "surface" + juce::String::toHexString(port.hashCode64()) + ".preset";
            for (int page = 0; page < 8; ++page) viewPreferences_.getUserSettings()->removeValue(key + juce::String(page));
            {
                const juce::SpinLock::ScopedLockType lock(surfaceLock_);
                surfaceMap_.layout = surfaceMap_.factoryLayout();
            }
            return presetEditor_->useRig(port, true);
        };
        presetEditor_->applyBindings = [this](const ostfstudio::Smk25Preset& preset, int page, const juce::String& port) {
            if (recordTrack_ || !surfaceMap_.learnable() || surfaceMap_.port != port || page < 0 || page >= 8) return false;
            auto layout = std::make_shared<ostfstudio::Smk25Rig>(*surfaceMap_.layout);
            layout->presets[page] = preset;
            if (!layout->rebuild()) return false;
            {
                const juce::SpinLock::ScopedLockType lock(surfaceLock_);
                surfaceMap_.layout = layout;
            }
            const auto& bytes = preset.bytes();
            viewPreferences_.getUserSettings()->setValue("surface" + juce::String::toHexString(port.hashCode64()) + ".preset" +
                juce::String(page), juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding());
            return presetEditor_->useRig(port, true);
        };
        surfacePanel_->manualBinding = [this](int index, int key) {
            if (recordTrack_ || !surfaceMap_.learnable() || index < 0 || index >= 24 || key < -1 || key >= 2048) return;
            auto duplicate = [key](const auto& keys, int slot) {
                auto it = std::find(keys.begin(), keys.end(), key);
                return key >= 0 && it != keys.end() && int(it - keys.begin()) != slot;
            };
            if (index < 16 ? duplicate(surfaceMap_.notes, index) : duplicate(surfaceMap_.cc, index - 16)) {
                surfacePanel_->status.setText(TRANS("This input is already assigned to another control."), juce::dontSendNotification);
                return;
            }
            {
                const juce::SpinLock::ScopedLockType lock(surfaceLock_);
                surfaceLearn_ = -1;
                surfaceCount_ = 0;
                surfaceMap_.bind(index, key);
            }
            saveControllerBindings();
        };
        surfacePanel_->selected = [this](auto port) {
            if (!recordTrack_ && port.identifier.isEmpty() && surfaceMap_.port.isNotEmpty()) {
                auto* prefs = viewPreferences_.getUserSettings();
                auto inputs = juce::StringArray::fromLines(prefs->getValue("workspaceControlPorts"));
                inputs.removeString(surfaceMap_.port);
                prefs->setValue("workspaceControlPorts", inputs.joinIntoString("\n"));
            }
            connectController(port);
        };
        surfacePanel_->learn = [this](int index) {
            if (recordTrack_) return;
            {
                const juce::SpinLock::ScopedLockType lock(surfaceLock_);
                surfaceLearn_ = index;
            }
            surfacePanel_->learningControl = index;
            surfacePanel_->repaint();
            surfacePanel_->status.setText(
                index < 0 ? (surfacePanel_->learning.getToggleState() ? TRANS("Select a pad or knob to learn its input.") : TRANS("Press MIDI Learn, then select a control."))
                          : (index < 16 ? TRANS("Strike the MIDI pad to assign") : TRANS("Turn the hardware knob.")),
                juce::dontSendNotification);
        };
        surfacePanel_->panMode = [this](bool pan) {
            surfacePan_ = pan;
            refreshController();
        };
        surfacePanel_->audition = [this](int index) {
            if (surfaceMap_.studioRig && surfacePanel_->rigBank == 1) { applySurfaceAction({ostfstudio::SurfaceAction::pad,index,0,surfacePanel_->rigPage,1}); return; }
            if (recordTrack_ || surfaceMap_.pitches[index] < 0) return;
            flashPad(surfaceMap_.pitches[index]);
            surfacePanel_->flashes[index] = juce::Time::getMillisecondCounter();
            surfacePanel_->repaint();
            if (!transportActive_ && !monitorActive_) startTimer(210);
        };
        auto restore = [this] {
            auto saved = viewPreferences_.getUserSettings()->getValue("surfacePort");
            juce::MidiDeviceInfo match;
            for (auto p : juce::MidiInput::getAvailableDevices())
                if (p.identifier == saved)
                    match = p;
            if (match.identifier != surfaceMap_.port) {
                connectController(match);
                if (match.identifier.isEmpty()) viewPreferences_.getUserSettings()->setValue("surfacePort", saved);
            }
            surfacePanel_->refreshPorts(surfaceMap_.port);
            syncControlInputs();
        };
        restore();
        // The first explicit opening can claim a single, unambiguous supported input.
        if (show && !viewPreferences_.getUserSettings()->containsKey("surfacePort")) {
            juce::Array<juce::MidiDeviceInfo> supported;
            for (auto p : juce::MidiInput::getAvailableDevices())
                if (ostfstudio::surfaceModel(p.name) != ostfstudio::SurfaceModel::none) supported.add(p);
            if (supported.size() == 1) connectController(supported[0]);
        }
        surfaceDevices_ = juce::MidiDeviceListConnection::make(restore);
    }
    if (show) {
        surfaceWindow_->setVisible(true);
        surfaceWindow_->toFront(true);
    }
    refreshController();
}

ostfstudio::ControllerMap MainComponent::controllerProfile(const juce::MidiDeviceInfo& port) {
    ostfstudio::ControllerMap next;
    next.model = ostfstudio::surfaceModel(port.name);
    if (next.model == ostfstudio::SurfaceModel::none && port.identifier.isNotEmpty()) {
        next.model = ostfstudio::SurfaceModel::generic;
        next.notes.fill(-1);
        next.cc.fill(-1);
    }
    next.port = port.identifier;
    auto* prefs = viewPreferences_.getUserSettings();
    const auto prefix = "surface" + juce::String::toHexString(next.port.hashCode64()) + ".";
    auto layout = std::make_shared<ostfstudio::Smk25Rig>();
    for (int page = 0; page < 8; ++page) {
        juce::MemoryBlock bytes;
        if (!bytes.fromBase64Encoding(prefs->getValue(prefix + "preset" + juce::String(page)))) continue;
        if (auto preset = ostfstudio::Smk25Preset::read({static_cast<const std::uint8_t*>(bytes.getData()), bytes.getSize()}))
            layout->presets[page] = *preset;
    }
    if (layout->rebuild()) next.layout = layout;
    next.studioRig = prefs->getBoolValue(prefix + "allBanks", false);

    for (int i = 0; i < 24; ++i) {
        auto& key = i < 16 ? next.notes[i] : next.cc[i - 16];
        key = std::clamp(prefs->getIntValue(prefix + juce::String(i), key), -1, 2047);
    }
    return next;
}

void MainComponent::connectController(juce::MidiDeviceInfo port) {
    if (recordTrack_) {
        surfacePanel_->refreshPorts(surfaceMap_.port);
        return;
    }
    auto next = controllerProfile(port);
    bool opened = false;
    if (next.model != ostfstudio::SurfaceModel::none) {
        if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
        auto& devices = deviceSettings_->midiDevices();
        opened = port.identifier == surfaceMap_.port ? openedSurfacePort_
                                                     : !devices.isMidiInputDeviceEnabled(port.identifier);
        devices.setMidiInputDeviceEnabled(port.identifier, true);
        if (!devices.isMidiInputDeviceEnabled(port.identifier)) {
            surfacePanel_->status.setText(TRANS("Could not open MIDI input."), juce::dontSendNotification);
            surfacePanel_->refreshPorts(surfaceMap_.port);
            return;
        }

    }
    const auto assigned = juce::StringArray::fromLines(viewPreferences_.getUserSettings()->getValue("workspaceControlPorts"));
    if (openedSurfacePort_ && deviceSettings_ && next.port != surfaceMap_.port && !assigned.contains(surfaceMap_.port)
        && std::none_of(midiRoutes_.begin(), midiRoutes_.end(), [this](const auto& entry) {
            return entry.second.enabled && entry.second.port == surfaceMap_.port;
        }))
        deviceSettings_->midiDevices().setMidiInputDeviceEnabled(surfaceMap_.port, false);
    openedSurfacePort_ = opened;
    {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        surfaceMap_ = next;
        surfaceInputStatus_ = -1;
        surfaceCount_ = 0;
        surfaceLearn_ = -1;
    }
    viewPreferences_.getUserSettings()->setValue("surfacePort", next.port);
    rigTarget_ = std::clamp(viewPreferences_.getUserSettings()->getIntValue("rigControlTarget", 0), 0, 2);
    syncControlInputs();
    surfacePanel_->model = next.model;
    surfacePanel_->learningControl = -1;
    surfacePanel_->learning.setToggleState(false, juce::dontSendNotification);
    surfacePanel_->inputLabel.setText("MIDI IN", juce::dontSendNotification);
    surfacePanel_->refreshPorts(next.port);
    surfacePanel_->status.setText(
        next.model == ostfstudio::SurfaceModel::smk25ii
            ? TRANS("Select a drum track for the pads. Use MIDI Learn, select a control, then move it on the device.")
        : next.model == ostfstudio::SurfaceModel::mixer
            ? TRANS("Mackie Control input: faders control volume and encoders control pan.")
            : next.model == ostfstudio::SurfaceModel::generic ? TRANS("Use MIDI Learn, select a pad or knob, then play or move the hardware control.") : TRANS("Select a MIDI input for live assignments."),
        juce::dontSendNotification);
    refreshController();
    attachPadInput(project_ != nullptr || next.port.isNotEmpty() || !otherSurfaces_.empty());
    if (armedId_.isNotEmpty()) restartInputMonitor();
}

size_t MainComponent::rigMixerBank(int page) const {
    const auto groups = std::max(size_t(1), (trackControls_.size() + 7) / 8);
    return size_t(page) % groups * 8;
}

void MainComponent::refreshController() {
    if (!surfacePanel_) return;
    auto& p = *surfacePanel_;
    p.slots.clear();
    p.tracks.clear();
    p.levels.fill(0);
    p.pans.fill(0.5);
    std::array<int, 16> pitches;
    pitches.fill(-1);
    if (project_ && selectedTrack_ && *selectedTrack_ < project_->slotsByTrack.size() &&
        project_->trackKinds[*selectedTrack_] == ostfstudio::TrackKind::drum) {
        const auto& slots = project_->slotsByTrack[*selectedTrack_];
        for (size_t i = 0; i < std::min(slots.size(), pitches.size()); ++i) {
            p.slots.add(slots[i].name);
            pitches[i] = slots[i].pitch;
        }
    }
    {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        surfaceMap_.pitches = pitches;
    }
    p.rig = surfaceMap_.studioRig;
    const size_t bank = p.rig ? rigMixerBank(p.rigPage) : selectedTrack_.value_or(0) / 8 * 8;
    if (project_)
        for (size_t i = 0; i < 8 && bank + i < trackControls_.size(); ++i) {
            p.tracks.add(juce::String::fromUTF8(project_->trackLanes[bank + i].label));
            auto& strip = *trackControls_[bank + i];
            p.levels[i] = strip.volume.valueToProportionOfLength(strip.volume.getValue());
            p.pans[i] = strip.pan.valueToProportionOfLength(strip.pan.getValue());
        }
    const bool newBindings = p.padInputs != surfaceMap_.notes || p.knobInputs != surfaceMap_.cc;
    p.padInputs = surfaceMap_.notes;
    p.knobInputs = surfaceMap_.cc;
    if (newBindings) p.showBinding();
    p.showManualControls(surfaceMap_.learnable(), !recordTrack_);
    presetEditor_->setDestination(surfaceMap_.learnable() && !recordTrack_ ? surfaceMap_.port : juce::String{}, p.devices.getText());
    presetEditor_->showRig(surfaceMap_.studioRig, rigTarget_);
    const auto assignedTarget = activeControlTarget(surfaceMap_.port, p.rigPage);
    if (surfaceMap_.studioRig) {
        p.status.setText(targetCaption(assignedTarget) + "   /   " + TRANS("A: drum pads. B: tracks 1-8, mute, solo, arm, play, stop, record, cue, target."), juce::dontSendNotification);
        p.showManualControls(false, false);
    }
    p.showingParameters = false;
    p.controlTarget.setVisible(p.rig);
    p.controlTarget.setTextWhenNothingSelected(targetCaption(assignedTarget));
    p.controlTarget.setSelectedId(assignedTarget == "follow" ? 1 : assignedTarget == "mix" ? 2 : assignedTarget == "selected" ? 3 : 0, juce::dontSendNotification);
    if (p.rig) {
        const auto& hardware = surfaceMap_.layout->presets[p.rigPage];
        auto bindings = hardware.studioBindings(p.rigBank).value();
        p.padInputs = bindings.notes;
        p.knobInputs = bindings.cc;
        if (p.rigBank == 1) {
            p.slots.clear();
            for (int i = 0; i < 8; ++i) p.slots.add(p.tracks[i]);
            for (auto* label : {"Mute","Solo","Record arm","Play","Stop","Record","Cue","Target"}) p.slots.add(juce::translate(label));
        }
        for (int i = 0; i < 16; ++i) {
            const auto rgb = hardware.control(p.rigBank, i).rgb;
            p.rigColours[i] = juce::Colour::fromRGB(juce::uint8(rgb[0]),juce::uint8(rgb[1]),juce::uint8(rgb[2]));
        }
        auto* plugin = surfacePlugin();
        p.showingParameters = plugin || (assignedTarget != "mix" && assignedTarget != "follow");
        p.parameterNames.clear();
        p.parameterValues.fill(0);
        if (assignedTarget == "piano") p.parameterNames = juce::StringArray{TRANS("Start velocity"), TRANS("End velocity"), TRANS("Strength"), TRANS("Strum spread")};
        if (assignedTarget == "steps") p.parameterNames = juce::StringArray{TRANS("Pad"), TRANS("Bars"), TRANS("Repeats")};
        if (assignedTarget.startsWith("audio:")) p.parameterNames = juce::StringArray{TRANS("Volume"), TRANS("Pan"), TRANS("Input gain")};
        if (plugin) for (int i = 0; i < 8; ++i) {
            const int index = ostfstudio::controlParameterIndex(controlTargets_, surfaceMap_.port, p.rigPage, p.rigBank, i);
            auto& params = plugin->getParameters();
            auto* param = juce::isPositiveAndBelow(index, params.size()) ? params[index] : nullptr;
            p.parameterNames.add(param ? param->getName(40) : juce::String{});
            if (param) p.parameterValues[i] = param->getValue();
        }
    }
    p.layer.setVisible(surfaceMap_.learnable() && !p.rig);
    p.learning.setVisible(p.layer.isVisible());
    p.learning.setEnabled(!recordTrack_ && !surfaceMap_.studioRig);
    p.recommended.setEnabled(!recordTrack_);
    p.devices.setEnabled(!recordTrack_);
    p.repaint();
}

void MainComponent::saveControllerBindings() {
    auto* prefs = viewPreferences_.getUserSettings();
    const auto prefix = "surface" + juce::String::toHexString(surfaceMap_.port.hashCode64()) + ".";
    prefs->setValue(prefix + "allBanks", surfaceMap_.studioRig);
    for (int i = 0; i < 24; ++i)
        prefs->setValue(prefix + juce::String(i), i < 16 ? surfaceMap_.notes[i] : surfaceMap_.cc[i - 16]);
    cancelPadLearn();
    surfacePanel_->learningControl = -1;
    surfacePanel_->learning.setToggleState(false, juce::dontSendNotification);
    refreshController();
    surfacePanel_->showBinding();
    if (!surfaceMap_.studioRig) surfacePanel_->status.setText(TRANS("MIDI assignment saved."), juce::dontSendNotification);
    restartInputMonitor();
}

juce::String MainComponent::pluginTarget(bool output, size_t index, int slot) const {
    const auto& chain = output ? outputControls_[index].effects : trackControls_[index]->effects;
    const auto id = slot >= 0 ? chain.slots[size_t(slot)].id : effectId(trackControls_[index]->instance->getPluginDescription());
    juce::Array<juce::var> path;
    path.add(output); path.add(output ? juce::String(int(index)) : project_->trackIds[int(index)]);
    path.add(slot); path.add(id); path.add(controlProjectId_);
    return juce::JSON::toString(juce::var(path), true);
}

juce::String MainComponent::activeControlTarget(const juce::String& port, int page) const {
    const auto fallback = port == surfaceMap_.port && rigTarget_ == 2 ? "selected" :
        port == surfaceMap_.port && rigTarget_ == 0 && surfaceMap_.studioRig ? "follow" : "mix";
    return ostfstudio::resolveControlTarget(controlTargets_, port, page, fallback);
}

juce::String MainComponent::targetCaption(const juce::String& target) const {
    if (target == "mix") return TRANS("Transport and mixer");
    if (target == "piano") return TRANS("Piano roll");
    if (target == "steps") return TRANS("Step sequencer");
    if (target.startsWith("audio:")) return TRANS("Audio input and mix");
    if (target == "follow") return TRANS("Follow active editor");
    if (target == "selected") return TRANS("Instrument");
    if (auto* instance = surfacePlugin(target)) return instance->getName();
    return TRANS("Assigned plug-in unavailable");
}

void MainComponent::remapEffectTargets(bool output, size_t index, int from, int to) {
    auto* prefs = viewPreferences_.getUserSettings();
    const auto track = output ? juce::String(int(index)) : project_->trackIds[int(index)];
    for (const auto& port : juce::StringArray::fromLines(prefs->getValue("workspaceControlPorts"))) {
        const auto prefix = "surface" + juce::String::toHexString(port.hashCode64()) + ".target.";
        for (int page = -1; page < 8; ++page) {
            const auto key = prefix + juce::String(page);
            const auto target = prefs->getValue(key);
            if (!target.startsWithChar('[')) continue;
            auto path = juce::JSON::parse(target);
            if (path.size() != 5 || path[4].toString() != controlProjectId_ || bool(path[0]) != output
                || path[1].toString() != track || int(path[2]) < 0) continue;
            const int slot = ostfstudio::movedEffectSlot(int(path[2]), from, to);
            if (slot == int(path[2])) continue;
            path.getArray()->set(2, slot);
            const auto next = slot >= 0 ? juce::JSON::toString(path, true) : juce::String("unavailable");
            prefs->setValue(key, next);
            controlTargets_[{port, page}] = next;
        }
    }
    refreshControlButtons();
}

void MainComponent::syncControlInputs() {
    auto* prefs = viewPreferences_.getUserSettings();
    const auto assigned = juce::StringArray::fromLines(prefs->getValue("workspaceControlPorts"));
    std::map<juce::String, ostfstudio::ControllerMap> active;
    controlPortNames_.clear();
    for (auto port : juce::MidiInput::getAvailableDevices()) {
        controlPortNames_.emplace(port.identifier, port.name);
        if (!assigned.contains(port.identifier)) continue;
        const auto prefix = "surface" + juce::String::toHexString(port.identifier.hashCode64()) + ".target.";
        for (int page = -1; page < 8; ++page) {
            const auto key = prefs->getValue(prefix + juce::String(page));
            if (key.isNotEmpty()) controlTargets_[{port.identifier, page}] = key;
        }
        if (port.identifier == surfaceMap_.port) continue;
        if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
        auto& manager = deviceSettings_->midiDevices();
        if (!manager.isMidiInputDeviceEnabled(port.identifier)) manager.setMidiInputDeviceEnabled(port.identifier, true);
        if (manager.isMidiInputDeviceEnabled(port.identifier)) active.emplace(port.identifier, controllerProfile(port));
    }
    {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        otherSurfaces_.swap(active);
    }
    if (!otherSurfaces_.empty()) attachPadInput(true);
    refreshControlButtons();
}

void MainComponent::chooseControlTarget(juce::Button& anchor, const juce::String& target, const juce::String& caption) {
    if (!surfaceWindow_) openController(false);
    const auto ports = juce::MidiInput::getAvailableDevices();
    juce::Array<juce::MidiDeviceInfo> candidates;
    for (auto port : ports)
        if (port.identifier == surfaceMap_.port || otherSurfaces_.contains(port.identifier)
            || ostfstudio::surfaceModel(port.name) != ostfstudio::SurfaceModel::none) candidates.add(port);
    if (candidates.isEmpty() || juce::ModifierKeys::getCurrentModifiers().isShiftDown()) candidates = ports;
    if (candidates.isEmpty()) { openController(true); return; }
    if (candidates.size() == 1) { assignControlTarget(candidates[0], target, caption); return; }
    juce::PopupMenu menu;
    menu.addSectionHeader(caption);
    for (int i = 0; i < candidates.size(); ++i) {
        const auto port = candidates[i].identifier;
        const auto p = lastControlPages_.find(port);
        const int page = p == lastControlPages_.end() ? -1 : p->second;
        const auto current = activeControlTarget(port, page);
        const bool duplicate = std::count_if(candidates.begin(), candidates.end(),
            [&](const auto& device) { return device.name == candidates[i].name; }) > 1;
        const auto label = duplicate ? candidates[i].name + " (" + port + ")" : candidates[i].name;
        menu.addItem(i + 1, label + "  " + (page >= 0 ? "P" + juce::String(page + 1) + "  " : juce::String{})
                         + targetCaption(current), true, current == target);
    }
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&anchor),
        [safe = juce::Component::SafePointer<MainComponent>(this), candidates, target, caption](int item) {
            if (safe && item > 0 && item <= candidates.size()) safe->assignControlTarget(candidates[item - 1], target, caption);
        });
}

void MainComponent::assignControlTarget(const juce::MidiDeviceInfo& port, const juce::String& target, const juce::String& caption) {
    if (port.identifier.isEmpty()) return;
    if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
    auto& devices = deviceSettings_->midiDevices();
    if (!devices.isMidiInputDeviceEnabled(port.identifier)) devices.setMidiInputDeviceEnabled(port.identifier, true);
    if (!devices.isMidiInputDeviceEnabled(port.identifier)) {
        statusLabel_.setText(TRANS("Could not open MIDI input."), juce::dontSendNotification);
        return;
    }
    const auto p = lastControlPages_.find(port.identifier);
    const int page = p == lastControlPages_.end() ? -1 : p->second;
    auto* prefs = viewPreferences_.getUserSettings();
    const auto prefix = "surface" + juce::String::toHexString(port.identifier.hashCode64()) + ".target.";
    prefs->setValue(prefix + juce::String(page), target);
    auto ports = juce::StringArray::fromLines(prefs->getValue("workspaceControlPorts"));
    ports.addIfNotAlreadyThere(port.identifier);
    prefs->setValue("workspaceControlPorts", ports.joinIntoString("\n"));
    syncControlInputs();
    if (surfaceMap_.port.isEmpty() && !recordTrack_) connectController(port);
    if (!recordTrack_ && !transportActive_ && armedId_.isNotEmpty()) restartInputMonitor();
    refreshController();
    refreshControlButtons();
    statusLabel_.setText(port.name + (page < 0 ? "  " : "  P" + juce::String(page + 1) + "  ") + caption, juce::dontSendNotification);
}

void MainComponent::refreshControlButtons() {
    const auto mark = [this](ControlTargetButton& button, const juce::String& target) {
        juce::StringArray assigned;
        for (const auto& [id, name] : controlPortNames_) {
            if (id != surfaceMap_.port && !otherSurfaces_.contains(id)) continue;
            const auto page = lastControlPages_.find(id);
            const int preset = page == lastControlPages_.end() ? -1 : page->second;
            if (activeControlTarget(id, preset) == target)
                assigned.add(name + (preset < 0 ? juce::String{} : " P" + juce::String(preset + 1)));
        }
        button.setToggleState(!assigned.isEmpty(), juce::dontSendNotification);
        button.setTitle(targetCaption(target) + " / " + (assigned.isEmpty() ? TRANS("Control here") : TRANS("Controlling")));
        button.setTooltip(targetCaption(target) + "  " + assigned.joinIntoString(", ") + "\n"
                          + TRANS("Assign a controller here. Shift-click to choose any MIDI input."));
    };
    mark(deck_.control, "mix"); mark(mixer_.control, "mix"); mark(paneControl_, audioEditor_.isVisible() && selectedTrack_ && project_ ? "audio:" + project_->trackIds[int(*selectedTrack_)] :
        drumSteps_.isVisible() ? juce::String("steps") : juce::String("piano"));
    if (!project_) return;
    auto markWindow = [&](auto& window, bool output, size_t track, int slot) {
        if (window) if (auto* body = dynamic_cast<ControlledPluginEditor*>(window->getContentComponent()))
            mark(body->control, pluginTarget(output, track, slot));
    };
    for (size_t i = 0; i < trackControls_.size(); ++i) {
        markWindow(trackControls_[i]->editor, false, i, -1);
        auto& slots = trackControls_[i]->effects.slots;
        for (size_t j = 0; j < slots.size(); ++j) markWindow(slots[j].editor, false, i, int(j));
    }
    for (size_t i = 0; i < outputControls_.size(); ++i) {
        auto& slots = outputControls_[i].effects.slots;
        for (size_t j = 0; j < slots.size(); ++j) markWindow(slots[j].editor, true, i, int(j));
    }
}

juce::AudioPluginInstance* MainComponent::surfacePlugin(const juce::String& requested) const {
    const auto t = requested.isEmpty() ? activeControlTarget(surfaceMap_.port, surfacePanel_ ? surfacePanel_->rigPage : -1) : requested;
    if (t.startsWithChar('[') && project_) {
        const auto path = juce::JSON::parse(t);
        if (path.size() != 5 || path[4].toString() != controlProjectId_) return nullptr;
        const bool output = bool(path[0]);
        const auto index = output ? path[1].toString().getIntValue() : project_->trackIds.indexOf(path[1].toString());
        const int slot = int(path[2]);
        if (index < 0 || (output ? size_t(index) >= outputControls_.size() : size_t(index) >= trackControls_.size())) return nullptr;
        if (slot < 0 && !output) {
            const auto& plugin = trackControls_[size_t(index)]->instance;
            return plugin && effectId(plugin->getPluginDescription()) == path[3].toString() ? plugin.get() : nullptr;
        }
        const auto& slots = output ? outputControls_[size_t(index)].effects.slots : trackControls_[size_t(index)]->effects.slots;
        if (slot >= 0 && size_t(slot) < slots.size() && slots[size_t(slot)].id == path[3].toString()) return slots[size_t(slot)].effect.get();
        return nullptr;
    }
    juce::AudioPluginInstance* plugin = nullptr;
    if (t == "follow") {
        auto focused = [&](auto& slots) {
            for (auto& fx : slots) if (fx.editor && fx.editor->isActiveWindow()) plugin = fx.effect.get();
        };
        for (auto& strip : trackControls_) {
            if (strip->editor && strip->editor->isActiveWindow()) plugin = strip->instance.get();
            focused(strip->effects.slots);
        }
        for (auto& strip : outputControls_) focused(strip.effects.slots);
    } else if (t == "selected" && selectedTrack_ && *selectedTrack_ < trackControls_.size()) plugin = trackControls_[*selectedTrack_]->instance.get();
    return plugin;
}

void MainComponent::applySurfaceAction(ostfstudio::SurfaceAction a) {
    using A = ostfstudio::SurfaceAction;
    if (!surfacePanel_) return;
    if (a.kind == A::learned) {
        {
            const juce::SpinLock::ScopedLockType lock(surfaceLock_);
            surfaceMap_.bind(a.index, a.value);
        }
        saveControllerBindings();
        return;
    }
    const auto port = a.port.isEmpty() ? surfaceMap_.port : a.port;
    const bool primary = port == surfaceMap_.port;
    if (a.page >= 0 && (!lastControlPages_.contains(port) || lastControlPages_[port] != a.page)) {
        lastControlPages_[port] = a.page;
        refreshControlButtons();
    }
    const auto target = activeControlTarget(port, a.page);
    if (primary && a.page >= 0) {
        surfacePanel_->rigPage = a.page;
        surfacePanel_->rigBank = a.bank;
        refreshController();
    }
    if (a.page >= 0 && a.kind == A::pad && a.bank == 1) {
        surfacePanel_->flashes[a.index] = juce::Time::getMillisecondCounter();
        if (a.index == 15) {
            juce::MidiDeviceInfo info;
            for (auto device : juce::MidiInput::getAvailableDevices()) if (device.identifier == port) info = device;
            if (info.identifier.isNotEmpty()) {
                const auto next = target == "mix" ? "selected" : target == "selected" ? "follow" : "mix";
                assignControlTarget(info, next, targetCaption(next));
            }
            return;
        }
        if (!project_) return;
        if (a.index < 8) {
            const auto track = rigMixerBank(a.page) + size_t(a.index);
            if (!recordTrack_ && track < trackControls_.size()) selectTrack(track);
            return;
        }
        if (a.index == 11) { startTransport(false); return; }
        if (a.index == 12) { stopTransport(); return; }
        if (a.index == 13) { startTransport(true); return; }
        if (!selectedTrack_ || *selectedTrack_ >= trackControls_.size()) return;
        auto& strip = *trackControls_[*selectedTrack_];
        if (a.index == 8) strip.mute.triggerClick();
        if (a.index == 9) strip.solo.triggerClick();
        if (a.index == 10) strip.arm.triggerClick();
        if (a.index == 14) strip.cue.triggerClick();
        return;
    }
    const bool continuous = a.kind == A::knob || a.kind == A::fader || a.kind == A::relativePan;
    if (continuous && (target == "steps" || target.startsWith("audio:"))) {
        const bool relative = a.kind == A::relativePan;
        const double v = relative ? double(a.value) : a.value / (a.kind == A::fader ? 16383. : 127.);
        if (target == "steps") {
            if (!drumSteps_.isVisible()) return;
            if (a.index == 0) drumPads_.chooseSlot(v, relative);
            else if (a.index == 1 || a.index == 2) drumSteps_.adjustPattern(a.index - 1, v, relative);
        } else if (project_) {
            const int track = project_->trackIds.indexOf(target.substring(6));
            if (track < 0 || size_t(track) >= trackControls_.size() || project_->trackKinds[track] != ostfstudio::TrackKind::audio) return;
            auto& strip = *trackControls_[track];
            std::array<juce::Slider*,3> knobs{&strip.volume, &strip.pan, &strip.inputGain};
            if (a.index >= 0 && a.index < int(knobs.size())) {
                auto& slider = *knobs[a.index];
                slider.setValue(relative ? slider.getValue() + v : slider.proportionOfLengthToValue(v), juce::sendNotificationSync);
            }
        }
        return;
    }
    if (continuous && target == "piano") {
        const double v = a.kind == A::relativePan ? double(a.value) : a.value / (a.kind == A::fader ? 16383. : 127.);
        pianoInspector_.adjustFromController(a.index, v, a.kind == A::relativePan);
        return;
    }
    if (continuous && target != "mix") {
        auto* plugin = surfacePlugin(target);
        if (plugin || target != "follow") {
            if (plugin) {
                const auto index = ostfstudio::controlParameterIndex(controlTargets_, port, a.page, a.bank, a.index);
                auto& params = plugin->getParameters();
                if (auto* param = juce::isPositiveAndBelow(index, params.size()) ? params[index] : nullptr; param && param->isAutomatable()) {
                    param->beginChangeGesture();
                    const float value = a.kind == A::relativePan ? param->getValue() + float(a.value) / 127.f
                        : float(a.value) / (a.kind == A::fader ? 16383.f : 127.f);
                    param->setValueNotifyingHost(std::clamp(value, 0.f, 1.f));
                    param->endChangeGesture();
                }
            }
            return;
        }
    }
    if (a.kind == A::pad) {
        if (a.value >= 0) flashPad(a.value);
        surfacePanel_->flashes[a.index] = juce::Time::getMillisecondCounter();
        surfacePanel_->repaint();
        if (!transportActive_ && !monitorActive_) startTimer(210);
        return;
    }
    const auto track = (a.page >= 0 ? rigMixerBank(a.page) : selectedTrack_.value_or(0) / 8 * 8) + size_t(a.index);
    if (!project_ || track >= trackControls_.size()) return;
    auto& strip = *trackControls_[track];
    const bool pan = a.kind == A::relativePan || (a.kind == A::knob && (a.page >= 0 ? a.bank == 1 : surfacePan_));
    auto& slider = pan ? strip.pan : strip.volume;
    if (a.kind == A::relativePan)
        slider.setValue(slider.getValue() + a.value, juce::sendNotificationSync);
    else
        slider.setValue(slider.proportionOfLengthToValue(a.value / (a.kind == A::fader ? 16383.0 : 127.0)),
                        juce::sendNotificationSync);
    refreshController();
}

void MainComponent::cancelPadLearn() {
    learnTarget_ = -1;
    learnedPair_ = -1;
    drumPads_.setLearning(false);
}

void MainComponent::attachPadInput(bool enable) {
    if (enable == padListening_) return;
    if (enable) {
        if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
        deviceSettings_->midiDevices().addMidiInputDeviceCallback({}, this);
    } else {
        deviceSettings_->midiDevices().removeMidiInputDeviceCallback({}, this);
        padUpdate_.cancelPendingUpdate();
        cancelPadLearn();
        for (auto& hit : padPressed_) hit = false;
    }
    padListening_ = enable;
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput* port, const juce::MidiMessage& message) {
    if (port) {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        const auto& portId = port->getIdentifier();
        bool matched = false;
        for (auto& monitor : inputMonitors_)
            if (monitor.route.accepts(portId, message.getChannel())) { monitor.received = true; matched = true; }
        if (matched) padUpdate_.triggerAsyncUpdate();
        const bool kitAccepts = drumInputMonitor_ && *drumInputMonitor_ < inputMonitors_.size()
            && inputMonitors_[*drumInputMonitor_].route.accepts(portId, message.getChannel());
        const bool primary = surfaceMap_.port.isNotEmpty() && portId == surfaceMap_.port;
        const auto other = otherSurfaces_.find(portId);
        if (primary || other != otherSurfaces_.end()) {
            const auto& mapped = primary ? surfaceMap_ : other->second;
            if (primary && message.getRawDataSize()) {
                surfaceInputStatus_ = message.getRawData()[0];
                surfaceInputData_ = message.getRawDataSize() > 1 ? message.getRawData()[1] : 0;
                padUpdate_.triggerAsyncUpdate();
            }
            auto action = mapped.action(message);
            if (!primary && action.kind == ostfstudio::SurfaceAction::pad && action.index >= 0 && action.index < 16)
                action.value = surfaceMap_.pitches[action.index];
            if (!kitAccepts && action.kind == ostfstudio::SurfaceAction::pad) action.value = -1;
            if (primary && kitAccepts && surfaceMap_.learnable() && message.isNoteOn() && surfaceLearn_ < 0) {
                const int target = learnTarget_.load();
                if (target >= 0) {
                    auto it = std::find(surfaceMap_.pitches.begin(), surfaceMap_.pitches.end(), target);
                    if (it != surfaceMap_.pitches.end()) {
                        surfaceLearn_ = int(it - surfaceMap_.pitches.begin());
                        learnTarget_ = -1;
                    }
                }
            }
            if (primary && surfaceLearn_ >= 0 && (surfaceLearn_ < 16 ? message.isNoteOn() : message.isController())) {
                const auto value = surfaceLearn_ < 16 ? message.getNoteNumber() : message.getControllerNumber();
                action = {ostfstudio::SurfaceAction::learned, surfaceLearn_, (message.getChannel() - 1) * 128 + value};
                if (surfaceCount_ < surfaceEvents_.size()) surfaceLearn_ = -1;
            }
            if (action.kind != ostfstudio::SurfaceAction::none) {
                action.port = portId;
                if (surfaceCount_ < surfaceEvents_.size()) surfaceEvents_[surfaceCount_++] = action;
                padUpdate_.triggerAsyncUpdate();
                return;
            }
            if (mapped.model == ostfstudio::SurfaceModel::mixer) return;
        }
        if (!kitAccepts) return;
    }
    if (!message.isNoteOn()) return;
    const int input = message.getNoteNumber();
    const int target = learnTarget_.exchange(-1);
    if (target >= 0) learnedPair_ = 128 * input + target;
    padPressed_[input] = true;
    padUpdate_.triggerAsyncUpdate();
}

void MainComponent::showPadInput() {
    if (!padListening_) return;
    std::array<ostfstudio::SurfaceAction, 128> events;
    size_t count;
    std::vector<bool> received(trackControls_.size());
    int surfaceStatus, surfaceData;
    {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        for (size_t i = 0; i < std::min(inputMonitors_.size(), received.size()); ++i)
            received[i] = std::exchange(inputMonitors_[i].received, false);
        surfaceStatus = std::exchange(surfaceInputStatus_, -1);
        surfaceData = surfaceInputData_;
        count = std::exchange(surfaceCount_, 0);
        std::copy_n(surfaceEvents_.begin(), count, events.begin());
    }
    for (size_t i = 0; i < received.size(); ++i)
        if (received[i]) trackControls_[i]->inputLamp.pulse();
    if (surfaceStatus >= 0 && surfacePanel_) {
        const int type = surfaceStatus >> 4;
        const auto name = type == 9 || type == 8 ? "Note " : type == 11 ? "CC " : "MIDI ";
        surfacePanel_->received(surfaceStatus >= 0xf0 ? "0x" + juce::String::toHexString(surfaceStatus)
            : juce::String(name) + juce::String(surfaceData) + "  Ch " + juce::String(1 + (surfaceStatus & 15)));
    }
    for (size_t i = 0; i < count; ++i)
        applySurfaceAction(events[i]);
    const int pair = learnedPair_.exchange(-1);
    if (pair >= 0) {
        const int input = pair / 128, target = pair % 128;
        padMapping_[input] = target;
        viewPreferences_.getUserSettings()->setValue("padInput" + juce::String(input), target);
        drumPads_.setLearning(false);
        drumPads_.refreshAssignment();
        statusLabel_.setText("MIDI " + juce::String(input) + " -> " + juce::String(target), juce::dontSendNotification);
    }
    for (int i = 0; i < 128; ++i)
        if (padPressed_[i].exchange(false)) flashPad(padMapping_[i]);
}

void MainComponent::flashPad(int pitch) {
    drumPads_.flash(pitch);
    drumSteps_.flash(pitch);
    if (!transportActive_) startTimer(170);
}

MainComponent::~MainComponent() {
    surfaceDevices_ = {};
    aiConnectionWorker_.removeAllJobs(true, -1);
    if (spliceInstance_ && deviceSettings_) deviceSettings_->devices().removeAudioCallback(&splicePlayer_);
    splicePlayer_.setProcessor(nullptr);
    spliceWindow_.reset();
    attachPadInput(false);
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

namespace {
std::shared_ptr<PreviewTransport::MidiDestination> openTrackOutput(juce::AudioDeviceManager& devices, const juce::String& id) {
    const bool useDefault = devices.getDefaultMidiOutputIdentifier() == id;
    std::shared_ptr<juce::MidiOutput> owned;
    if (!useDefault) owned = juce::MidiOutput::openDevice(id);
    auto port = [&devices, owned, useDefault, id]() -> juce::MidiOutput* {
        if (!useDefault) return owned.get();
        return devices.getDefaultMidiOutputIdentifier() == id ? devices.getDefaultMidiOutput() : nullptr;
    };
    if (!port()) return {};
    port()->startBackgroundThread();
    auto dest = std::make_shared<PreviewTransport::MidiDestination>();
    dest->clearPending = [port] { if (auto* out = port()) out->clearAllPendingMessages(); };
    dest->send = [port](const juce::MidiBuffer& block, double time, double rate) {
        if (auto* out = port()) {
            if (time == 0) out->sendBlockOfMessagesNow(block);
            else out->sendBlockOfMessages(block, time, rate);
        }
    };
    return dest;
}
}
void MainComponent::restartInputMonitor() {
    if (recordTrack_) return;
    const bool playing = transportActive_;
    if (playing || monitorActive_) stopTransport();
    if (playing) startTransport(false);
    else if (armedId_.isNotEmpty()) { monitorPending_ = true; startTimerHz(24); }
}

void MainComponent::startTransport(bool recording, bool replaceConfirmed, bool monitorOnly) {
    if (recording && project_ && !recordTrack_) {
        auto armed = project_->trackIds.indexOf(armedId_);
        if (armed < 0) {
            statusLabel_.setText(TRANS("Press R on the track you want to record."), juce::dontSendNotification);
            return;
        }
        selectTrack(static_cast<std::size_t>(armed));
    }
    if (!project_ || (recording && (!selectedTrack_ || selectedGlobal_ == TimelineLane::tempo))) return;
    if (recording && *selectedTrack_ >= project_->sourceTrackCount()) {
        statusLabel_.setText(TRANS("Select a pitched or drum track for MIDI recording."), juce::dontSendNotification);
        return;
    }
    if (transportActive_) {
        if (transport_.isRunning() && recording == recordTrack_.has_value()) return;
        stopTransport();
    }
    if (recording && cursorTick_ >= project_->playbackEndTick()) {
        statusLabel_.setText(TRANS("Place the recording cursor inside the song."), juce::dontSendNotification);
        return;
    }
    const bool audioTake = recording && project_->trackKinds[*selectedTrack_] == ostfstudio::TrackKind::audio;
    if (audioTake && clipOn(*selectedTrack_) && !replaceConfirmed) {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, TRANS("Record"),
            TRANS("Replace this clip with the new recording when you stop?"), TRANS("Record"), TRANS("Cancel"), this,
            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this),
                generation = projectGeneration_, track = selectedTrack_](int answer) {
                if (safe && answer && generation == safe->projectGeneration_ && safe->selectedTrack_ == track && !safe->transportActive_
                    && track && safe->armedId_ == safe->project_->trackIds[int(*track)])
                    safe->startTransport(true, true);
            }));
        return;
    }
    if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
    if (!canEditTracks()) {
        statusLabel_.setText(TRANS("Waiting for instruments and effects to finish loading"), juce::dontSendNotification);
        return;
    }
    if (monitorActive_) { transport_.stop(); monitorActive_ = false; }
    monitorPending_ = false;
    const auto armed = project_->trackIds.indexOf(armedId_);
    std::optional<size_t> inputTrack;
    if (armed >= 0 && size_t(armed) < project_->sourceTrackCount() && project_->trackKinds[size_t(armed)] != ostfstudio::TrackKind::audio)
        inputTrack = size_t(armed);
    if (recording && !audioTake) inputTrack = selectedTrack_;
    if (monitorOnly && !inputTrack) return;
    const auto localDest = inputTrack ? trackControls_[*inputTrack]->outputTrack.value_or(*inputTrack) : size_t(0);
    const auto monitorDest = inputTrack && midiOutputs_.contains(armedId_) ? *inputTrack : localDest;
    auto& devices = deviceSettings_->devices();
    std::map<juce::String,std::shared_ptr<PreviewTransport::MidiDestination>> openPorts;
    updateTransportMix();
    std::vector<PreviewTransport::TrackSetup> tracks;
    tracks.reserve(trackControls_.size());
    std::vector<juce::String> unavailable;
    for (std::size_t i = 0; i < trackControls_.size(); ++i) {
        const auto& track = *trackControls_[i];
        const auto destination = track.outputTrack.value_or(i);
        tracks.push_back({track.outputTrack, track.playableInstrument ? track.instance.get() : nullptr, track.midiChannel});
        auto& route = tracks.back();
        if (monitorOnly && i != monitorDest) route.instrument = nullptr;
        if (const auto out = midiOutputs_.find(project_->trackIds[int(i)]); out != midiOutputs_.end()) {
            route.outputTrack.reset();
            route.instrument = nullptr;
            if (!monitorOnly || i == monitorDest) {
                auto& port = openPorts[out->second.identifier];
                if (!port) port = openTrackOutput(devices, out->second.identifier);
                if (!port) {
                    statusLabel_.setText(TRANS("Could not open MIDI output") + "  " + out->second.name, juce::dontSendNotification);
                    return;
                }
                route.external = port;
            }
            continue;
        }
        if ((track.outputTrack || track.pluginPath != juce::File{})
            && (i >= PreviewTransport::maxTracks || destination >= PreviewTransport::maxTracks || destination >= trackControls_.size()
                         || (!trackControls_[destination]->playableInstrument && !midiOutputs_.contains(project_->trackIds[int(destination)]))))
            unavailable.push_back(juce::String::fromUTF8(project_->trackLanes[i].label));
    }
    PreviewTransport::Inserts inserts {};
    const auto copyEffects = [](const EffectChain& chain, PreviewTransport::Chain& destination) {
        for (std::size_t slot = 0; slot < chain.slots.size(); ++slot) destination[slot] = chain.slots[slot].effect.get();
    };
    for (std::size_t i = 0; i < outputControls_.size(); ++i) copyEffects(outputControls_[i].effects, inserts[i]);
    for (std::size_t i = 0; i < std::min(trackControls_.size(), PreviewTransport::maxTracks); ++i)
        if ((!monitorOnly || i == monitorDest) && !tracks[i].external) copyEffects(trackControls_[i]->effects, inserts[PreviewTransport::outputCount + i]);
    const auto start = cursorTick_ >= project_->playbackEndTick() ? 0 : std::max<juce::int64>(0, cursorTick_);
    PreviewTransport::TakeOptions take;
    take.cycle = drumSteps_.looping() ? drumSteps_.range() : (cycleButton_.getToggleState() && !drumSteps_.isVisible()
        ? editorCycle_ : juce::Range<juce::int64>{});
    take.surface = surfaceMap_;
    take.otherSurfaces = otherSurfaces_;
    if (monitorOnly) take.cycle = {};
    if (inputTrack) {
        const auto input = midiRoutes_.find(project_->trackIds[int(*inputTrack)]);
        if (input != midiRoutes_.end()) take.midiInput = input->second;
    }
    take.countInBars = recording ? deck_.countIn.getSelectedId() - 1 : 0;
    if (audioTake) {
        const auto it = captureChannels_.find(project_->trackIds[int(*selectedTrack_)]);
        take.inputChannels = it == captureChannels_.end() ? 1 : it->second;
        const auto input = audioInputs_[project_->trackIds[int(*selectedTrack_)]];
        const auto* device = deviceSettings_->devices().getCurrentAudioDevice();
        take.firstInputBuffer = device ? input.bufferIndex(device->getActiveInputChannels(), take.inputChannels) : -1;
        take.inputGainDb = input.gainDb;
    }
    std::array<int,128> armedPadMap;
    if (inputTrack && project_->trackKinds[*inputTrack] == ostfstudio::TrackKind::drum) {
        armedPadMap = padMapping_;
        take.drumMapping = &armedPadMap;
        take.surface.pitches.fill(-1);
        const auto& pads = project_->slotsByTrack[*inputTrack];
        for (size_t i = 0; i < std::min(size_t(16), pads.size()); ++i) take.surface.pitches[i] = pads[i].pitch;
        for (auto& entry : take.otherSurfaces) entry.second.pitches = take.surface.pitches;
    }
    if (recording && !take.cycle.isEmpty() && start >= take.cycle.getEnd()) {
        statusLabel_.setText(TRANS("Place the cursor before the end of the recording loop."), juce::dontSendNotification);
        return;
    }
    if (recording) {
        cancelPadLearn();
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        surfaceLearn_ = -1;
    }
    if (link_ && !monitorOnly) {
        take.link = link_.get();
        take.linkQuantum = linkQuantum();
        take.announceLinkStart = !followingLink_;
        take.linkTempo = link_->state(take.linkQuantum).tempo;
    }
    const auto result = transport_.start(deviceSettings_->devices(), *project_, recording, tracks, inserts, start,
                                         auditionShift_, auditionRate_, inputTrack, &take, monitorOnly);
    if (result.failed()) {
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return;
    }
    if (monitorOnly) { monitorActive_ = true; startTimerHz(24); return; }
    transportActive_ = true;
    if (take.link) lastLinkPlaying_ = true;
    activeLoop_ = recording ? take.cycle : juce::Range<juce::int64>{};
    recordTrack_ = recording ? selectedTrack_ : std::nullopt;
    if (recording)
        refreshEditor();
    updateDrumLoop();
    deck_.play.setToggleState(!recording, juce::dontSendNotification);
    deck_.stop.setToggleState(false, juce::dontSendNotification);
    deck_.record.setToggleState(recording, juce::dontSendNotification);
    for (auto* button : {&deck_.home, &deck_.rewind, &deck_.forward}) button->setEnabled(!recording);
    displayTransportPosition(recording || activeLoop_.isEmpty() || activeLoop_.contains(start) ? start : activeLoop_.getStart());
    statusLabel_.setText(audioTake ? TRANS("Recording audio") : recording ? TRANS("Recording MIDI into the selected track") : TRANS("Playing arrangement"),
                         juce::dontSendNotification);
    if (!unavailable.empty())
        statusLabel_.setText(TRANS("No playable instrument on: ") + unavailable.front()
                             + (unavailable.size() > 1 ? TRANS(" (and other tracks)") : ""), juce::dontSendNotification);
    else if (!recording && project_->audio.empty()
             && std::none_of(tracks.begin(), tracks.end(), [](const auto& track) { return track.instrument || track.external; }))
        statusLabel_.setText(TRANS("Assign a VST3 instrument to hear playback"), juce::dontSendNotification);
    startTimerHz(24);
}

void MainComponent::stopTransport() {
    if (transportActive_ && link_ && !followingLink_) {
        link_->stop();
        lastLinkPlaying_ = false;
    }
    if (!transportActive_) {
        if (monitorActive_) {
            transport_.stop();
            monitorActive_ = false;
            for (auto& strip : trackControls_) strip->meter.clear();
            for (auto& output : outputControls_) output.meter.clear();
            mixer_.clearMeters();
        }
        monitorPending_ = armedId_.isNotEmpty();
        if (monitorPending_) startTimerHz(24);
        return;
    }
    PreviewTransport::Cursor parked{};
    auto recorded = transport_.stop(&parked);
    if (recordTrack_ && project_) {
        const auto count = static_cast<int>(recorded.size());
        bool added = false;
        juce::String failure;
        const bool audio = project_->trackKinds[*recordTrack_] == ostfstudio::TrackKind::audio;
        if (audio) {
            if (auto clip = transport_.finishAudioTake(*recordTrack_)) {
                if (auto* existing = clipOn(*recordTrack_)) *existing = std::move(*clip);
                else project_->audio.push_back(std::move(*clip));
                waveforms_.load(project_->audio);
                markSessionEdited();
                melodyneCopies_.erase(project_->trackIds[int(*recordTrack_)]);
                added = true;
            } else failure = TRANS("No audio captured");
        } else if (!recorded.empty()) {
            juce::MemoryBlock before;
            auto result = project_->addRecordedClip(*recordTrack_, transport_.recordedRange(), recorded, before);
            added = result.wasOk();
            failure = result.getErrorMessage();
            if (added) rememberScoreEdit(std::move(before));
        }
        if (added) refreshTrackTheory(*recordTrack_);
        const auto finishedTrack = *recordTrack_;
        recordTrack_.reset();
        transportActive_ = false;
        selectTrack(finishedTrack);
        if (added) { editorVisible_ = false; refreshEditor(); }
        statusLabel_.setText(added && audio ? TRANS("Audio clip recorded") : added ? juce::String(count) + TRANS(" MIDI notes captured")
                                   : (failure.isEmpty() ? TRANS("No MIDI notes captured") : juce::translate(failure)),
                             juce::dontSendNotification);
    } else {
        statusLabel_.setText(TRANS("Stopped"), juce::dontSendNotification);
    }
    recordTrack_.reset();
    transportActive_ = false;
    deck_.play.setToggleState(false, juce::dontSendNotification);
    deck_.stop.setToggleState(true, juce::dontSendNotification);
    deck_.record.setToggleState(false, juce::dontSendNotification);
    for (auto* button : {&deck_.home, &deck_.rewind, &deck_.forward}) button->setEnabled(true);
    displayTransportPosition(parked.tick, parked.seconds);
    monitorPending_ = armedId_.isNotEmpty();
    if (link_ || hasAudioInputs_ || monitorPending_) startTimerHz(24); else stopTimer();
    for (auto& track : trackControls_) track->meter.clear();
    for (auto& output : outputControls_) output.meter.clear();
    mixer_.clearMeters();
    repaint();
}

void MainComponent::showShortcutList() {
    const std::pair<const char*, const char*> rows[] = {
        {"Space", "Play / stop"}, {"Home key", "Return to song start"}, {"F1", "Show shortcuts"},
        {"Click the bar ruler", "Move the playhead"}, {"Drag the bar ruler", "Scroll the timeline horizontally"},
        {"Alt/Option + Shift + drag", "Scroll the timeline horizontally"},
        {"Shift + wheel / horizontal scroll", "Scroll the timeline horizontally"},
        {"Mouse wheel", "Scroll through tracks"}, {"Right-click a track name", "Open track actions"},
        {"Double-click empty piano grid", "Add a note"}, {"Drag note / edge", "Move / resize notes"},
        {"B (piano roll)", "Toggle pencil"}, {"Alt/Option + pencil drag", "Repeat notes on the grid"},
        {"Shift during note drag", "Temporarily disable snap"},
        {"Shift-click / drag empty grid", "Select multiple notes"}, {"Alt / Option + drag", "Copy notes"},
        {"Drag a velocity handle", "Adjust note velocity"}, {"Ctrl / Cmd + drag vertically", "Adjust note velocity"}, {"Delete / Backspace", "Delete selected notes"},
        {"Arrow keys / Shift + Up or Down", "Nudge / transpose by an octave"},
        {"Ctrl / Cmd + Z / Shift + Z", "Undo / redo note edits"}, {"Wheel over piano grid", "Scroll pitches"}
    };
    // The content owns its labels for the lifetime of the asynchronous dialog.
    struct Guide final : juce::Component {
        juce::OwnedArray<juce::Label> labels;
        void add(const juce::String& text, int x, int y, int width, bool key) {
            auto* label = labels.add(new juce::Label({}, text));
            label->setColour(juce::Label::textColourId, juce::Colour(key ? 0xff91e2d6 : 0xffe1e6eb));
            label->setBounds(x, y, width, 23);
            addAndMakeVisible(*label);
        }
    };
    auto guide = std::make_unique<Guide>();
    int y = 10;
    for (const auto& [key, action] : rows) {
        guide->add(juce::translate(key), 12, y, 245, true);
        guide->add(juce::translate(action), 259, y, 267, false);
        y += 24;
    }
    guide->setSize(535, y + 10);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = TRANS("Shortcuts");
    options.dialogBackgroundColour = juce::Colour(0xff222c34);
    options.content.setOwned(guide.release());
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    options.launchAsync();
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (isCurrentlyBlockedByAnotherModalComponent()) return false;
    const auto* focus = juce::Component::getCurrentlyFocusedComponent();
    if (focus && (dynamic_cast<const juce::TextEditor*>(focus) || focus->findParentComponentOfClass<juce::TextEditor>())) return false;
    const auto mods = key.getModifiers();
    if (selectedGlobal_ && hasKeyboardFocus(false)) {
        if (mods.isCommandDown() && !mods.isAltDown()) {
            if (key.isKeyCode('C')) {
                globalCommand(2);
                return true;
            }
            if (key.isKeyCode('X')) {
                globalCommand(3);
                return true;
            }
            if (key.isKeyCode('V')) {
                globalCommand(4);
                return true;
            }
            if (key.isKeyCode('Z')) {
                undoPianoEdit(mods.isShiftDown());
                return true;
            }
        }
        if (key.isKeyCode(juce::KeyPress::returnKey)) {
            globalCommand(1);
            return true;
        }
    }
    if ((controllerEditor_.isVisible() || drumSteps_.isVisible() || tempoEditor_.isVisible()) && mods.isCommandDown() && !mods.isAltDown() && key.isKeyCode('Z')) {
        undoPianoEdit(mods.isShiftDown());
        return true;
    }
    if (mods.isAnyModifierKeyDown()) return false;
    if (key.isKeyCode(juce::KeyPress::F1Key)) { showShortcutList(); return true; }
    if (!project_) return false;
    if (key.isKeyCode(juce::KeyPress::homeKey)) {
        navigateTransport(TransportKey::home);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::F2Key) && selectedTrack_) {
        renameTrack(*selectedTrack_);
        return true;
    }
    if (!key.isKeyCode(juce::KeyPress::spaceKey)) return false;
    if (!spaceHeld_) {
        spaceHeld_ = true;
        if (transportActive_) stopTransport();
        else startTransport(false);
    }
    return true;
}

bool MainComponent::keyStateChanged(bool isKeyDown) {
    if (!isKeyDown && !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey)) spaceHeld_ = false;
    return false;
}

void MainComponent::focusOfChildComponentChanged(FocusChangeType) {
    if (!hasKeyboardFocus(true)) spaceHeld_ = false;
}

void MainComponent::navigateTransport(TransportKey::Action action) {
    if (!project_ || recordTrack_) return;
    if (transportActive_ && !transport_.isRunning()) stopTransport();
    const auto now = transportActive_ ? transport_.positionTick() : std::max<juce::int64>(0, cursorTick_);
    const auto end = project_->playbackEndTick();
    juce::int64 target = 0;
    if (action == TransportKey::forward) target = now + std::min(project_->ticksPerBar, end - now);
    if (action == TransportKey::rewind) target = std::max<juce::int64>(0, now - project_->ticksPerBar);
    locateTransport(target);
}

void MainComponent::locateTransport(juce::int64 tick, const juce::ScrollBar* anchoredView) {
    if (!project_ || recordTrack_) return;
    tick = std::clamp<juce::int64>(tick, 0, project_->playbackEndTick());
    if (transportActive_) transport_.seekToTick(tick);
    displayTransportPosition(tick, -1, anchoredView);
}

void MainComponent::displayTransportPosition(juce::int64 tick, double seconds, const juce::ScrollBar* anchoredView) {
    controllerEditor_.cursor = tick;
    if (controllerEditor_.isVisible()) controllerEditor_.repaint();
    if (drumSteps_.isVisible()) { drumSteps_.cursor=tick; drumSteps_.repaint(); }
    const int previousArrangement = ruler_.playheadColumn(), previousPiano = pianoRuler_.playheadColumn();
    if (theoryVisible_ && theory_ && tick != cursorTick_
        && theory_->firstCell(double(cursorTick_)) != theory_->firstCell(double(tick)))
        repaint(rowOverlay_.getBounds().withWidth(trackHeaderWidth).withTrimmedLeft(trackNameWidth));
    cursorTick_ = tick;
    if (scoreView_ && scoreView_->isVisible()) scoreView_->playbackPosition(tick);
    if (!project_) return;
    const auto bpm = project_->clock.bpmAt(std::max<juce::int64>(0, tick));
    const auto rate = link_ && bpm > 0 ? link_->state(linkQuantum()).tempo / bpm : auditionRate_;
    deck_.setPosition(*project_, std::max<juce::int64>(0, tick), seconds, auditionShift_, rate);
    for (const auto& [id, envelope] : project_->panAutomation) {
        const int i = project_->trackIds.indexOf(id);
        if (i < 0 || std::size_t(i) >= trackControls_.size()) continue;
        const float pan = float(envelope.valueAt(double(tick), mixer_.channel(i).pan));
        auto& slider = trackControls_[i]->pan;
        if (!slider.isMouseButtonDown()) slider.setValue(pan * 100.0, juce::dontSendNotification);
        mixer_.displayPan(i,pan);
    }
    bool scrolled = false;
    if (deck_.follow.getToggleState() && tick >= 0) {
        const auto bar = 1.0 + double(tick) / project_->ticksPerBar;
        auto* scroll = &arrangementScroll_;
        if (scroll->isEnabled() && scroll->isVisible() && scroll != anchoredView) {
            const auto range = scroll->getCurrentRange();
            if (bar < range.getStart() || bar >= range.getEnd() - range.getLength() * 0.05) {
                scroll->setCurrentRangeStart(bar - range.getLength() * 0.15, juce::dontSendNotification);
                scrolled |= range.getStart() != scroll->getCurrentRangeStart();
            }
        }
    }
    if (scrolled) { repaint(); return; }
    auto repaintCursor = [&](ArrangementRuler& ruler, int previous, int bottom) {
        if (!ruler.isVisible()) return;
        for (int col : {previous, ruler.playheadColumn()}) {
            if (col < 0) continue;
            const auto p = getLocalPoint(&ruler, juce::Point<int>{col, 0});
            repaint(p.x - 6, p.y, 13, std::max(0, bottom - p.y));
        }
    };
    repaintCursor(ruler_, previousArrangement, rowOverlay_.getBottom());
    repaintCursor(pianoRuler_, previousPiano, getWorkspace().getBottom());
}

void MainComponent::timerCallback() {
    pollLink();
    if (hasAudioInputs_) updateInputMeters();
    if (surfacePanel_) surfacePanel_->repaint();
    if (drumPads_.isVisible()) { drumPads_.repaint(); drumSteps_.repaint(); }
    if (!transportActive_ && !monitorActive_ && monitorPending_ && canEditTracks()) {
        monitorPending_ = false;
        startTransport(false, false, true);
    }
    if (!transportActive_ && !monitorActive_) { if (!hasAudioInputs_ && !monitorPending_ && !link_) stopTimer(); return; }
    if (!transport_.isRunning()) { stopTransport(); return; }
    if (recordTrack_ && transport_.countingIn())
        statusLabel_.setText(TRANS("Count-in"), juce::dontSendNotification);
    else if (recordTrack_ && statusLabel_.getText() == TRANS("Count-in"))
        statusLabel_.setText(project_->trackKinds[*recordTrack_] == ostfstudio::TrackKind::audio ? TRANS("Recording audio")
                             : TRANS("Recording MIDI into the selected track"), juce::dontSendNotification);
    const auto frame = transport_.takeMeters();
    if (transportActive_) {
        const auto cursor = transport_.cursor();
        displayTransportPosition(cursor.tick, cursor.seconds);
    }
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (std::size_t i = 0; i < std::min(trackControls_.size(), frame.tracks.size()); ++i)
        trackControls_[i]->meter.update(frame.tracks[i], now);
    for (std::size_t i = 0; i < outputControls_.size(); ++i)
        outputControls_[i].meter.update(frame.outputs[i], now);
    mixer_.updateMeters(frame, now);
}

void MainComponent::newProject() {
    juce::MemoryInputStream stream(BinaryData::NewProject_ostf, BinaryData::NewProject_ostfSize, false);
    if (openPreview(stream, {}, true)) {
        setWorkspaceMode(WorkspaceMode::arrangement);
        statusLabel_.setText(TRANS("Add your first track from the Track menu."), juce::dontSendNotification);
    }
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
    if (!file.hasFileExtension("json")) return openPreview(input, file.getFileName(), file.hasFileExtension("ostf"), file.getParentDirectory());
    juce::var parsed;
    const bool inspected = juce::JSON::parse(input.readEntireStreamAsString(), parsed).wasOk() && !parsed.isVoid();
    statusLabel_.setText((inspected ? TRANS("JSON object or array read: ") : TRANS("Expected a JSON object or array: "))
                         + file.getFileName(), juce::dontSendNotification);
    return inspected;
}

bool MainComponent::openPreview(juce::InputStream& input, const juce::String& filename, bool plainScore, const juce::File& sourceFolder) {
    std::unique_ptr<ostfstudio::OstfpPreview> next;
    const auto result = plainScore ? ostfstudio::readOstfPreview(input, next) : ostfstudio::readOstfpPreview(input, next);
    if (result.failed()) {
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return false;
    }
    adoptProject(std::move(next), filename, plainScore, sourceFolder);
    return true;
}

void MainComponent::openImportedSong(std::unique_ptr<ostfstudio::OstfpPreview> draft) {
    if (!draft)
        return;
    adoptProject(std::move(draft), {}, false, {});
    setWorkspaceMode(WorkspaceMode::arrangement);
}

void MainComponent::adoptProject(std::unique_ptr<ostfstudio::OstfpPreview> next, const juce::String& filename,
                                 bool plainScore, const juce::File& sourceFolder) {
    stopTransport();
    attachPadInput(false);
    armedId_.clear();
    midiRoutes_.clear();
    midiOutputs_.clear();
    monitorPending_ = false;
    midiDocument_ = filename.isNotEmpty() && sourceFolder != juce::File{} ? sourceFolder.getChildFile(filename).getFullPathName() : juce::String{};
    if (midiDocument_.isNotEmpty()) {
        auto stored = viewPreferences_.getUserSettings()->getXmlValue("midiRoutes" + juce::String::toHexString(midiDocument_.hashCode64()));
        if (stored) for (auto* row : stored->getChildWithTagNameIterator("OUTPUT"))
            midiOutputs_[row->getStringAttribute("track")] = {row->getStringAttribute("name"), row->getStringAttribute("port")};
        if (stored) for (auto* row : stored->getChildWithTagNameIterator("INPUT"))
            midiRoutes_[row->getStringAttribute("track")] = {row->getStringAttribute("port"), row->getStringAttribute("name"),
                std::clamp(row->getIntAttribute("channel"), 0, 16), row->getBoolAttribute("enabled", true)};
    }
    captureChannels_.clear();
    audioInputs_.clear();
    editorCycle_ = {};
    cycleButton_.setToggleState(false, juce::dontSendNotification);
    pianoRuler_.cycleMode = audioEditor_.recordRuler.cycleMode = false;
    scoreView_.reset();
    selectedGlobal_.reset();
    melodyneCopies_.clear();
    clipLane_.selected.reset();
    controlProjectId_ = midiDocument_.isNotEmpty() ? midiDocument_ : next ? next->documentId : juce::String{};
    if (controlProjectId_.isEmpty()) controlProjectId_ = juce::Uuid().toString();
    project_ = std::move(next);
    auditionShift_ = 0;
    auditionRate_ = 1;
    auditionClock_.reset();
    scaleReference_ = -1;
    theory_.reset();
    refreshTheory();
    savedScore_ = project_->sourceScore;
    sessionEdited_ = false;
    currentFilename_ = filename;
    legacySourceFile_ = project_->sourceVersion != "5.0.0" && sourceFolder != juce::File{}
                            ? sourceFolder.getChildFile(filename)
                            : juce::File{};
    updateDocumentLabel();
    displayTransportPosition(0);
    updateHorizontalRange(true);
    for (auto& control : outputControls_) {
        control.effects.slots.clear();
        control.effects.loading = 0;
        updateEffectButton(control.effects);
    }
    if (plainScore && sourceFolder != juce::File{})
        for (auto& audio : project_->audio)
            audio.localFile = sourceFolder.getChildFile(audio.path);
    waveforms_.load(project_->audio);
    refreshTimelineLanes();
    resizingAudio_.reset();
    ++projectGeneration_;
    selectedTrack_.reset();
    trackRows_.clear();
    verticalScroll_.setCurrentRangeStart(0, juce::dontSendNotification);
    groupDefaults_.fill(TrackRowSize::minimum);
    for (auto group : {HeightGroup::melody, HeightGroup::pitched, HeightGroup::drums, HeightGroup::audio})
        groupDefaults_[static_cast<size_t>(group)] = 100;
    groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)] = defaultTimelineHeight;
    for (auto& output : outputControls_) {
        output.size.resetSize();
        output.size.assignHeight(TrackRowSize::minimum);
    }
    trackPicker_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i)
        trackPicker_.addItem(juce::String::fromUTF8(project_->trackLanes[i].label), static_cast<int>(i + 1));
    trackPicker_.setEnabled(!project_->trackLanes.empty());
    mixer_.setTracks(*project_);
    rebuildTrackControls();
    if (const auto& saved = project_->savedMix) {
        captureChannels_ = saved->captureChannels;
        audioInputs_ = saved->audioInputs;
        refreshMidiRoutes();
        editorCycle_ = saved->recordCycle.getIntersectionWith({0, project_->playbackEndTick()});
        const bool cycleOn = !editorCycle_.isEmpty() && saved->cycleEnabled;
        cycleButton_.setToggleState(cycleOn, juce::dontSendNotification);
        pianoRuler_.cycleMode = audioEditor_.recordRuler.cycleMode = cycleOn;
        mixer_.restore(*saved);
        restoreInstrumentAssignments(*saved);
        for (std::size_t i = 0; i < std::min(trackControls_.size(), saved->midiRoutes.size()); ++i) {
            trackControls_[i]->midiChannel = saved->midiRoutes[i].channel;
            trackControls_[i]->outputTrack = saved->midiRoutes[i].destination;
        }
        for (std::size_t i = 0; i < outputControls_.size(); ++i) {
            auto ids = saved->outputEffects[i];
            if (ids.isEmpty() && saved->insertIds[i].isNotEmpty())
                ids.add(saved->insertIds[i]);
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
        mixerSelected_ = false;
        refreshEditor();
    }
    stockEffectChanges_.reset();
    sessionEdited_ = false;
    refreshWindowTitle();
    statusLabel_.setText(project_->title + " / " + juce::String(project_->bars.endBar - 1) + TRANS(" bars") +
                             (legacySourceFile_ == juce::File{}
                                  ? juce::String{}
                                  : " / OSTF " + project_->sourceVersion + TRANS(" (saves as 5.0.0)")),
                         juce::dontSendNotification);
    repaint();
}

void MainComponent::saveDocument(SaveFormat format) {
    if (!project_) return;
    const bool midi = format == SaveFormat::midi0 || format == SaveFormat::midi1;
    const bool package = format == SaveFormat::package;
    const juce::String extension = midi ? "mid" : package ? "ostfp" : "ostf";
    auto title = juce::File::createLegalFileName(project_->title).trim();
    if (title.isEmpty()) title = "Untitled";
    const bool upgrading = !midi && legacySourceFile_ != juce::File{};
    const auto start = upgrading
        ? legacySourceFile_.getSiblingFile(legacySourceFile_.getFileNameWithoutExtension() + "-5.0.0." + extension)
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(title + "." + extension);
    const auto dialogTitle = juce::translate(midi ? "Save as MIDI (SMF)" : package ? "Save as OSTFP" : "Save as OSTF")
        + (upgrading ? " (OSTF " + project_->sourceVersion + " " + juce::String::charToString(0x2192) + " 5.0.0)" : juce::String{});
    chooser_ = std::make_unique<juce::FileChooser>(dialogTitle, start, "*." + extension);
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
                    auto saveChain = [&](const EffectChain& chain, juce::StringArray& ids, bool output, std::size_t index) {
                        for (const auto& slot : chain.slots) {
                            const auto key = ostfstudio::SessionMix::effectSlot(output, index, ids.size());
                            ids.add(slot.id);
                            for (const auto& fx : ostfstudio::stockEffects) if (slot.id == fx.chainId()) {
                                juce::MemoryBlock state;
                                if (slot.effect) slot.effect->getStateInformation(state);
                                auto encoded = state.isEmpty() ? slot.savedState : state.toBase64Encoding();
                                if (encoded.isNotEmpty()) mix.stockEffectStates[key] = encoded;
                                break;
                            }
                        }
                    };
                    for (std::size_t i = 0; i < safe->outputControls_.size(); ++i)
                        saveChain(safe->outputControls_[i].effects, mix.outputEffects[i], true, i);
                    mix.recordCycle = safe->editorCycle_;
                    mix.cycleEnabled = safe->cycleButton_.getToggleState();
                    mix.captureChannels = safe->captureChannels_;
                    mix.audioInputs = safe->audioInputs_;
                    for (const auto kind : safe->project_->trackKinds) mix.audioTracks.push_back(kind == ostfstudio::TrackKind::audio);
                    mix.trackEffects.resize(safe->trackControls_.size());
                    for (std::size_t i = 0; i < safe->trackControls_.size(); ++i)
                        saveChain(safe->trackControls_[i]->effects, mix.trackEffects[i], false, i);
                    result = package ? ostfstudio::writeOstfpPreview(*safe->project_, mix, output)
                                     : ostfstudio::writeOstfPreview(*safe->project_, mix, file, output);
                }
                output.flush();
                if (result.wasOk()) result = output.getStatus();
            }
            if (result.wasOk() && !temporary.overwriteTargetFileWithTemporary())
                result = juce::Result::fail(TRANS("Could not replace the destination file"));
            if (result.wasOk() && !midi) {
                safe->savedScore_ = safe->project_->sourceScore;
                safe->stockEffectChanges_.reset();
                safe->sessionEdited_ = false;
                safe->currentFilename_ = file.getFileName();
                safe->midiDocument_ = file.getFullPathName();
                safe->saveMidiRoutes();
                safe->legacySourceFile_ = juce::File{};
                safe->updateDocumentLabel();
            }
            safe->statusLabel_.setText(result.wasOk() ? TRANS("Saved ") + file.getFileName() : juce::translate(result.getErrorMessage()),
                                       juce::dontSendNotification);
        });
}

void MainComponent::refreshWindowTitle() {
    if (!project_) return;
    juce::String caption = project_->title;
    if (currentFilename_.isNotEmpty()) caption += "  " + juce::String::charToString(0x00b7) + "  " + currentFilename_;
    if (currentFilename_.isEmpty() || sessionEdited_ || savedScore_ != project_->sourceScore)
        caption += "  [" + (currentFilename_.isEmpty() ? TRANS("Unsaved project") : TRANS("Unsaved changes")) + "]";
    if (auditionShift_ || auditionRate_ != 1) caption += "  [" + TRANS("Audition") + "]";
    caption += "  " + juce::String::charToString(0x2014) + "  OSTF Studio";
    setName(caption);
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>()) window->setName(caption);
}

void MainComponent::parentHierarchyChanged() {
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>(); window && getName().isNotEmpty())
        window->setName(getName());
}

void MainComponent::markSessionEdited() {
    if (sessionEdited_) return;
    sessionEdited_ = true;
    refreshWindowTitle();
}

void MainComponent::updateDocumentLabel() {
    basics_.sync(*project_);
    refreshWindowTitle();
}

void MainComponent::editSongInformation() {
    if (!project_ || recordTrack_) return;
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = TRANS("Song information");
    options.dialogBackgroundColour = juce::Colour(panel);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.content.setOwned(new SongInformationPanel(*project_,
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_](const auto& info, const auto& music) {
            if (!safe || safe->recordTrack_ || generation != safe->projectGeneration_) return juce::Result::fail("The project has changed.");
            const bool playing = safe->transportActive_;
            if (playing) safe->stopTransport();
            juce::MemoryBlock before;
            auto result = safe->project_->applySongDetails(info, music, before);
            if (result.wasOk() && !before.isEmpty()) {
                safe->rememberScoreEdit(std::move(before));
                safe->refreshTheory();
                safe->finishNoteEdit(false);
                safe->updateDocumentLabel();
            }
            if (playing) safe->startTransport(false);
            return result;
        }));
    options.launchAsync();
}

bool MainComponent::canEditTracks() const {
    if (!project_) return false;
    for (const auto& track : trackControls_)
        if (track->loading || track->effects.loading) return false;
    for (const auto& output : outputControls_)
        if (output.effects.loading) return false;
    return true;
}

void MainComponent::refreshTrackActions() {
    const bool ready = canEditTracks();
    const bool room = ready && project_->sourceTrackCount() < ostfstudio::OstfpPreview::maxTracks;
    for (auto* button : {&melodyAdd_, &chorusAdd_, &pitchedAdd_, &drumsAdd_, &audioAdd_}) button->setEnabled(room);
    busAdd_.setEnabled(ready && mixer_.routing().busCount < ostfstudio::maxMixerBuses);
    returnAdd_.setEnabled(ready && mixer_.returnCount() < ostfstudio::maxMixerReturns);
    uiSmaller_.setEnabled(interfaceScale_ > minInterfaceScale);
    uiLarger_.setEnabled(interfaceScale_ < maxInterfaceScale);
    const auto percent = " (" + juce::String(interfaceScale_) + "%)";
    uiLarger_.setTooltip(TRANS("Zoom in interface") + percent);
    uiSmaller_.setTooltip(TRANS("Zoom out interface") + percent);
}

void MainComponent::changeInterfaceScale(int step) {
    interfaceScale_ = std::clamp(interfaceScale_ + step * 25, minInterfaceScale, maxInterfaceScale);
    juce::Desktop::getInstance().setGlobalScaleFactor(interfaceScale_ / 100.f);
    viewPreferences_.getUserSettings()->setValue("interfaceScale", interfaceScale_);
    viewPreferences_.saveIfNeeded();
    refreshTrackActions();
}

void MainComponent::revealRow(juce::Rectangle<int> row) {
    if (!verticalScroll_.isVisible() || row.isEmpty()) return;
    const auto visible = rowOverlay_.getBounds().withTrimmedTop(timelineRowHeight());
    const auto delta = row.getY() < visible.getY() ? row.getY() - visible.getY()
                     : std::max(0, row.getBottom() - visible.getBottom());
    verticalScroll_.setCurrentRangeStart(verticalScroll_.getCurrentRangeStart() + delta, juce::sendNotificationSync);
}

void MainComponent::showPatch(std::size_t track, juce::int64 tick) {
    if (!project_ || recordTrack_ || track >= project_->sourceTrackCount()) return;
    tick = std::clamp<juce::int64>(tick, 0, project_->playbackEndTick() - 1);
    auto dialog = std::make_unique<juce::AlertWindow>(tick == 0 ? TRANS("Initial MIDI patch") : TRANS("MIDI patch at cursor"),
                                                      juce::String::fromUTF8(project_->trackLanes[track].label),
                                                      juce::MessageBoxIconType::NoIcon, this);
    const int channel = trackControls_[track]->midiChannel;
    std::array<int,3> current{-1,-1,-1};
    for (const auto& curve : project_->performanceCurves[track]) {
        const auto& k = curve.control;
        if (!curve.enabled || (k.channel && k.channel != channel)) continue;
        int field = k.type == "programChange" ? 2 : k.type == "controlChange" && k.number == 0 ? 0
                  : k.type == "controlChange" && k.number == 32 ? 1 : -1;
        if (field >= 0) for (const auto& p : curve.points) if (p.tick == tick) current[field] = juce::roundToInt(p.value * 127);
    }
    juce::StringArray choices{TRANS("No change")};
    for (int i = 0; i < 128; ++i) choices.add(juce::String(i));
    const std::array<juce::String,3> names{"Bank MSB (CC 0)", "Bank LSB (CC 32)", "Program (0-127)"};
    for (int i = 0; i < 3; ++i) {
        dialog->addComboBox(names[i], choices, names[i]);
        dialog->getComboBoxComponent(names[i])->setSelectedItemIndex(current[i] + 1);
    }
    dialog->addButton(TRANS("Apply"), 1);
    dialog->addButton(TRANS("Cancel"), 0);
    auto* w = dialog.release();
    w->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<MainComponent>(this), w, names, current, track, tick, channel, generation = projectGeneration_](int choice) {
            if (!safe || choice != 1 || safe->recordTrack_ || safe->projectGeneration_ != generation) return;
            std::array<int,3> values;
            for (int i = 0; i < 3; ++i) values[i] = w->getComboBoxComponent(names[i])->getSelectedItemIndex() - 1;
            if (values == current) return;
            const bool playing = safe->transportActive_;
            if (playing) safe->stopTransport();
            juce::MemoryBlock before;
            auto result = safe->project_->setPatchAt(track, tick, channel, values, before);
            if (result.wasOk()) safe->rememberScoreEdit(std::move(before));
            else safe->statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
            safe->finishNoteEdit(playing);
        }), true);
}

void MainComponent::showRowMenu(bool output, std::size_t index) {
    if (!canEditTracks() || (output ? !mixer_.routing().hasOutput(index)
                                   : index >= project_->sourceTrackCount())) return;
    const bool send = ostfstudio::isReturnOutput(index);
    const bool room = !output ? project_->sourceTrackCount() < ostfstudio::OstfpPreview::maxTracks
        : send ? returnAdd_.isEnabled() : busAdd_.isEnabled();
    juce::PopupMenu menu;
    menu.addSectionHeader(output ? mixer_.outputName(index) : juce::String::fromUTF8(project_->trackLanes[index].label));
    if (!output) {
        menu.addItem(initialPatchItem, TRANS("Initial MIDI patch..."));
        menu.addItem(cursorPatchItem, TRANS("MIDI patch at cursor..."));
    }
    menu.addItem(renameTrackItem, TRANS("Rename track..."));
    if (!output || index != 0) {
        menu.addItem(copyTrack, TRANS("Duplicate track"), room);
        menu.addItem(removeTrack, TRANS("Delete track..."));
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, output, send, index](int choice) {
            if (!safe || safe->projectGeneration_ != generation) return;
            if (choice == cursorPatchItem && !output) safe->showPatch(index, safe->cursorTick_);
            if (choice == initialPatchItem && !output) safe->showPatch(index, 0);
            if (choice == renameTrackItem) safe->renameTrack(index, output);
            if (choice == removeTrack) safe->confirmRemoval(output, index);
            if (choice == copyTrack) {
                if (output) safe->editOutput(send, index);
                else safe->editTracks(ostfstudio::TrackKind::pitched, {}, index);
            }
        });
}

void MainComponent::renameTrack(std::size_t index, bool output) {
    if (!project_ || (output ? !mixer_.routing().hasOutput(index) : index >= project_->sourceTrackCount())) return;
    if (index >= (output ? outputRows_.size() : trackRows_.size())) return;
    const auto row = output ? outputRows_[index] : trackRows_[index];
    if (row.isEmpty()) return;
    renameEditor_.onFocusLost = nullptr;
    renameRow_ = index;
    renameOutput_ = output;
    const auto oldName = output ? mixer_.outputName(index) : juce::String::fromUTF8(project_->trackLanes[index].label);
    renameEditor_.setComponentID("inline-track-name");
    renameEditor_.setText(oldName, false);
    addAndMakeVisible(renameEditor_);
    renameEditor_.setBounds(row.withTrimmedLeft(21).withWidth(trackNameWidth - 24).withHeight(std::min(25, row.getHeight())));
    renameEditor_.toFront(true);
    renameEditor_.setSelectAllWhenFocused(true);
    renameEditor_.grabKeyboardFocus();
    renameEditor_.selectAll();
    renameEditor_.onEscapeKey = [this] { renameEditor_.setVisible(false); };
    auto commit = [safe = juce::Component::SafePointer<MainComponent>(this), index, output, oldName, generation = projectGeneration_] {
            if (!safe || !safe->renameEditor_.isVisible()) return;
            const auto name = safe->renameEditor_.getText().trim();
            safe->renameEditor_.setVisible(false);
            if (!safe->project_ || generation != safe->projectGeneration_ || name == oldName ||
                (output ? !safe->mixer_.routing().hasOutput(index) : index >= safe->project_->sourceTrackCount())) return;
            if (output) {
                if (name.isEmpty()) safe->statusLabel_.setText(TRANS("Enter a track name."), juce::dontSendNotification);
                else {
                    safe->mixer_.renameOutput(index, name);
                    safe->markSessionEdited();
                    safe->outputControls_[index].size.setTitle(name + " " + TRANS("track height"));
                    safe->outputControls_[index].mute.setTooltip(TRANS("Mute ") + name);
                    safe->repaint();
                }
                return;
            }
            const auto result = safe->project_->renameTrack(index, name);
            if (result.failed()) {
                safe->statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
                return;
            }
            const auto itemId = static_cast<int>(index + 1);
            safe->refreshWindowTitle();
            safe->trackPicker_.changeItemText(itemId, name);
            if (safe->selectedTrack_ == index) safe->trackPicker_.setSelectedId(itemId, juce::dontSendNotification);
            safe->mixer_.renameTrack(index, name);
            auto& controls = *safe->trackControls_[index];
            controls.size.setTitle(name + " " + TRANS("track height"));
            controls.mute.setTooltip(TRANS("Mute ") + name);
            controls.solo.setTooltip(TRANS("Solo ") + name);
            controls.mute.setTitle(controls.mute.getTooltip());
            controls.solo.setTitle(controls.solo.getTooltip());
            controls.volume.setName(name + " volume");
            controls.pan.setName(name + " pan");
            safe->refreshEditor();
            safe->repaint();
    };
    renameEditor_.onReturnKey = commit;
    renameEditor_.onFocusLost = commit;
}

void MainComponent::confirmRemoval(bool output, std::size_t index) {
    if (!canEditTracks() || (output ? index == 0 || !mixer_.routing().hasOutput(index)
                                   : index >= project_->sourceTrackCount())) return;
    const auto name = output ? mixer_.outputName(index) : juce::String::fromUTF8(project_->trackLanes[index].label);
    const auto prompt = output ? (ostfstudio::isReturnOutput(index)
        ? TRANS("Delete {track} and its sends?") : TRANS("Delete {track} and route its tracks to Master?"))
        : TRANS("Remove {track} and its clips from this project? Audio files stay on disk.");
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, TRANS("Delete track"), prompt.replace("{track}", name),
        TRANS("Delete track"), TRANS("Cancel"), this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this),
            generation = projectGeneration_, output, index](int confirmed) {
            if (!confirmed || !safe || safe->projectGeneration_ != generation) return;
            if (output) safe->editOutput(ostfstudio::isReturnOutput(index), index, true);
            else safe->editTracks(ostfstudio::TrackKind::pitched, index);
        }));
}

void MainComponent::copyEffectChain(bool output, std::size_t from, std::size_t to) {
    const auto& source = effectChain(output, from);
    auto& target = effectChain(output, to);
    target.slots.resize(source.slots.size());
    for (std::size_t i = 0; i < source.slots.size(); ++i) {
        auto& slot = target.slots[i];
        const auto& old = source.slots[i];
        slot.id = old.id;
        slot.savedState = old.savedState;
        if (old.effect) {
            juce::MemoryBlock state;
            old.effect->getStateInformation(state);
            if (!state.isEmpty()) slot.savedState = state.toBase64Encoding();
            loadEffect(output, to, i, old.effect->getPluginDescription());
        }
    }
    updateEffectButton(target);
}

void MainComponent::editOutput(bool send, std::optional<std::size_t> source, bool remove) {
    if (!canEditTracks()) return;
    auto mix = mixer_.snapshot();
    auto& routing = mix.routing;
    auto& count = send ? routing.returnCount : routing.busCount;
    const auto limit = send ? ostfstudio::maxMixerReturns : ostfstudio::maxMixerBuses;
    const auto slotAt = send ? ostfstudio::returnOutput : ostfstudio::busOutput;
    if ((!remove && count >= limit) || (source && (!*source || !routing.hasOutput(*source)
        || send != ostfstudio::isReturnOutput(*source)))) return;
    const auto index = source ? (send ? ostfstudio::outputReturnIndex(*source) : ostfstudio::outputBusIndex(*source)) : count;
    stopTransport();
    resizingAudio_.reset();
    ++projectGeneration_;
    workspaceMode_ = WorkspaceMode::arrangement;
    showOutputTracks_ = true;
    if (remove) {
        for (std::size_t i = index; i + 1 < count; ++i) {
            const auto slot = slotAt(i), next = slotAt(i + 1);
            mix.outputNames[slot] = mix.outputNames[next];
            outputControls_[slot].effects.slots = std::move(outputControls_[next].effects.slots);
            outputControls_[slot].size.copySize(outputControls_[next].size);
            updateEffectButton(outputControls_[slot].effects);
        }
        routing.removeOutput(*source, mix.tracks);
        const auto last = slotAt(count);
        mix.outputNames[last].clear();
        outputControls_[last].effects.slots.clear();
        updateEffectButton(outputControls_[last].effects);
    } else {
        const auto dest = slotAt(count);
        auto& strip = outputControls_[dest];
        strip.size.resetSize();
        strip.size.assignHeight(groupDefaults_[static_cast<std::size_t>(outputHeightGroup(dest))]);
        if (source) {
            mix.outputNames[dest] = mixer_.outputName(*source) + TRANS(" (copy)");
            routing.outputGain(dest) = routing.outputGain(*source);
            routing.outputMuted[dest] = routing.outputMuted[*source];
            strip.size.copySize(outputControls_[*source].size);
            copyEffectChain(true, *source, dest);
        }
        ++count;
    }
    mixer_.restore(mix);
    refreshEditor();
    if (!remove) revealRow(outputRows_[slotAt(count - 1)]);
}

void MainComponent::editTracks(ostfstudio::TrackKind kind, std::optional<std::size_t> remove,
                               std::optional<std::size_t> duplicate, ostfstudio::MixerTrackGroup group,
                               const ImportResultRow* import, juce::int64 tick) {
    if (!canEditTracks()) return;
    stopTransport();
    std::unique_ptr<ostfstudio::OstfpPreview> updated;
    auto result = project_->editTracks(kind, remove, updated, duplicate, group);
    if (result.wasOk() && import) {
        const auto dest = updated->sourceTrackCount() - 1;
        result = updated->renameTrack(dest, juce::String::fromUTF8(import->source.trackLanes[import->index].label));
        juce::MemoryBlock previous;
        if (result.wasOk()) result = ostfstudio::placeImportTrack(import->source, import->index, *updated, dest, tick, previous);
    }
    if (result.failed()) {
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return;
    }
    auto mix = mixer_.snapshot();
    scoreView_.reset();
    workspaceMode_ = WorkspaceMode::arrangement;
    auto previous = std::move(trackControls_);
    project_ = std::move(updated);
    scaleReference_ = -1;
    theory_.reset();
    refreshTheory();
    waveforms_.load(project_->audio);
    std::erase_if(melodyneCopies_, [this](const auto& copy) { return !project_->trackIds.contains(copy.first); });
    resizingAudio_.reset();
    ++projectGeneration_;
    if (remove) {
        previous.erase(previous.begin() + static_cast<std::ptrdiff_t>(*remove));
        mix.tracks.erase(mix.tracks.begin() + static_cast<std::ptrdiff_t>(*remove));
    } else {
        const auto added = static_cast<std::ptrdiff_t>(project_->sourceTrackCount() - 1);
        const auto channel = duplicate ? mix.tracks[*duplicate] : ostfstudio::ChannelMix{};
        mix.tracks.insert(mix.tracks.begin() + added, channel);
        previous.insert(previous.begin() + added, nullptr);
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
        if (!previous[i]) continue;
        auto& old = *previous[i];
        auto& track = *trackControls_[i];
        track.size.copySize(old.size);
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
        } else if (!remove && track.outputTrack && *track.outputTrack >= project_->sourceTrackCount() - 1) {
            ++*track.outputTrack;
        }
        track.effects.slots = std::move(old.effects.slots);
        updateEffectButton(track.effects);
        track.plugin.setButtonText(old.plugin.getButtonText());
        updatePresetLabel(i);
    }
    mixer_.restore(mix);
    if (duplicate) {
        const auto dest = project_->sourceTrackCount() - 1;
        const auto& src = *trackControls_[*duplicate];
        auto& copy = *trackControls_[dest];
        copy.size.copySize(src.size);
        copy.midiChannel = src.midiChannel;
        copy.outputTrack = src.outputTrack;
        copy.pluginPath = src.pluginPath;
        copy.candidatePath = src.candidatePath;
        copy.instrumentAssignment = src.instrumentAssignment.clone();
        copy.plugin.setButtonText(src.plugin.getButtonText());
        if (src.instance) {
            juce::MemoryBlock state;
            src.instance->getStateInformation(state);
            loadTrackPlugin(dest, src.instance->getPluginDescription(), true, std::move(state));
        }
        copyEffectChain(false, *duplicate, dest);
    }
    if (!trackControls_.empty()) selectTrack(remove ? std::min(*remove, trackControls_.size() - 1) : project_->sourceTrackCount() - 1);
    else refreshEditor();
    if (selectedTrack_ && *selectedTrack_ < trackRows_.size()) revealRow(trackRows_[*selectedTrack_]);
    statusLabel_.setText(remove ? TRANS("Track removed / save to keep changes") : TRANS("Track added / save to keep changes"),
                         juce::dontSendNotification);
}

void MainComponent::saveMidiRoutes() {
    if (midiDocument_.isEmpty() || !project_) return;
    juce::XmlElement routes("ROUTES");
    for (const auto& [id, port] : midiOutputs_) if (project_->trackIds.contains(id)) {
        auto* item = routes.createNewChildElement("OUTPUT");
        item->setAttribute("track", id);
        item->setAttribute("port", port.identifier);
        item->setAttribute("name", port.name);
    }
    for (const auto& [id, route] : midiRoutes_) {
        if (!project_->trackIds.contains(id)) continue;
        auto* item = routes.createNewChildElement("INPUT");
        item->setAttribute("track", id);
        item->setAttribute("port", route.port);
        item->setAttribute("name", route.name);
        item->setAttribute("channel", route.channel);
        item->setAttribute("enabled", route.enabled);
    }
    viewPreferences_.getUserSettings()->setValue("midiRoutes" + juce::String::toHexString(midiDocument_.hashCode64()), &routes);
}

void MainComponent::setRecordArm(std::size_t i, bool arm) {
    if (recordTrack_ || !project_ || i >= project_->sourceTrackCount()) return;
    if (arm && project_->trackKinds[i] == ostfstudio::TrackKind::audio) {
        if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
        deviceSettings_->devices();
        startTimerHz(24);
    }
    armedId_ = arm ? project_->trackIds[int(i)] : juce::String{};
    if (arm) selectTrack(i);
    refreshMidiRoutes();
    restartInputMonitor();
}

void MainComponent::refreshMidiRoutes() {
    if (!project_) return;
    if (!project_->trackIds.contains(armedId_)) armedId_.clear();
    std::vector<InputMonitor> monitors;
    for (std::size_t i = 0; i < trackControls_.size(); ++i) {
        auto& strip = *trackControls_[i];
        const auto id = project_->trackIds[int(i)];
        auto route = midiRoutes_.contains(id) ? midiRoutes_.at(id) : ostfstudio::MidiInputRoute{};
        if (i >= project_->sourceTrackCount() || project_->trackKinds[i] == ostfstudio::TrackKind::audio) route.enabled = false;
        monitors.push_back({route});
        const auto description = !route.enabled ? TRANS("No input") :
            (route.port.isEmpty() ? TRANS("All enabled inputs") : route.name)
            + " / " + (route.channel == 0 ? TRANS("All channels") : "Ch " + juce::String(route.channel));
        const auto out = midiOutputs_.find(id);
        const auto outputLabel = out != midiOutputs_.end() ? "   OUT  " + out->second.name : juce::String{};
        strip.input.setTooltip(TRANS("MIDI input") + "  " + description + outputLabel);
        strip.input.getProperties().set("routeLabel", "IN  " + description + outputLabel);
        strip.input.setButtonText(strip.size.expanded() ? "IN  " + description + outputLabel : "IN");
        if (project_->trackKinds[i] == ostfstudio::TrackKind::audio) {
            const auto& in = audioInputs_[id];
            const int width = captureChannels_.contains(id) ? captureChannels_.at(id) : 1;
            auto label = juce::String(width == 2 ? "Stereo " : "Mono ") + (in.first < 0 ? TRANS("Auto") : juce::String(in.first + 1));
            if (width == 2 && in.first >= 0) label += "-" + juce::String(in.first + 2);
            strip.input.getProperties().set("routeLabel", "IN  " + label);
            strip.input.setTooltip(TRANS("Recording input") + "  " + label);
            strip.inputGain.setValue(in.gainDb, juce::dontSendNotification);
        }
        const bool armed = armedId_.isNotEmpty() && armedId_ == id;
        strip.arm.setToggleState(armed, juce::dontSendNotification);
        strip.arm.setTooltip(armed ? TRANS("Record armed. Press Record to start.") : TRANS("Arm this track for recording"));
    }
    const juce::SpinLock::ScopedLockType lock(surfaceLock_);
    inputMonitors_ = std::move(monitors);
}

void MainComponent::showTrackInput(std::size_t i) {
    if (!project_ || recordTrack_ || i >= project_->sourceTrackCount()) return;
    const auto id = project_->trackIds[int(i)];
    if (project_->trackKinds[i] == ostfstudio::TrackKind::audio) { showAudioInput(i); return; }
    const auto route = midiRoutes_[id];
    const auto ports = juce::MidiInput::getAvailableDevices();
    juce::PopupMenu menu, channels;
    menu.addSectionHeader(TRANS("MIDI input"));
    menu.addItem(1, TRANS("All enabled inputs"), true, route.enabled && route.port.isEmpty());
    menu.addItem(2, TRANS("No input"), true, !route.enabled);
    for (int p = 0; p < ports.size(); ++p)
        menu.addItem(100 + p, ports[p].name, true, route.enabled && ports[p].identifier == route.port);
    channels.addItem(10, TRANS("All channels"), true, route.channel == 0);
    for (int ch = 1; ch <= 16; ++ch) channels.addItem(10 + ch, "Ch " + juce::String(ch), true, route.channel == ch);
    menu.addSubMenu(TRANS("MIDI channel"), channels);
    menu.addItem(3, TRANS("Audio / MIDI Devices..."));
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&trackControls_[i]->input),
        [safe = juce::Component::SafePointer<MainComponent>(this), ports, id, generation = projectGeneration_](int item) {
            if (!safe || item == 0 || safe->recordTrack_ || safe->projectGeneration_ != generation) return;
            if (item == 3) { safe->menuItemSelected(showDevices, 3); return; }
            auto& target = safe->midiRoutes_[id];
            if (item == 1 || item == 2) { target.enabled = item == 1; target.port.clear(); target.name.clear(); }
            else if (item >= 10 && item <= 26) target.channel = item - 10;
            else if (juce::isPositiveAndBelow(item - 100, ports.size())) {
                const auto port = ports[item - 100];
                if (!safe->deviceSettings_->enableTrackInput(port)) {
                    safe->statusLabel_.setText(TRANS("Could not open MIDI input."), juce::dontSendNotification);
                    return;
                }
                target.port = port.identifier; target.name = port.name; target.enabled = true;
            }
            safe->saveMidiRoutes();
            safe->refreshMidiRoutes();
            safe->restartInputMonitor();
        });
}

void MainComponent::updateInputMeters() {
    if (!deviceSettings_ || !project_) return;
    const auto* device = deviceSettings_->midiDevices().getCurrentAudioDevice();
    const auto active = device ? device->getActiveInputChannels() : juce::BigInteger{};
    const auto peaks = deviceSettings_->takeInputPeaks();
    const auto now = juce::Time::getMillisecondCounterHiRes() * .001;
    for (size_t i = 0; i < trackControls_.size(); ++i) {
        if (project_->trackKinds[i] != ostfstudio::TrackKind::audio) continue;
        const auto id = project_->trackIds[int(i)];
        const int width = captureChannels_.contains(id) ? captureChannels_.at(id) : 1;
        const auto& route = audioInputs_[id];
        int ch = route.physical(active, width);
        const float gain = juce::Decibels::decibelsToGain(float(route.gainDb));
        ostfstudio::StereoPeak p{};
        if (ch >= 0) p = {peaks[ch] * gain, peaks[ch + width - 1] * gain};
        trackControls_[i]->recordingMeter.update(p, now);
    }
}

void MainComponent::showAudioInput(std::size_t i) {
    if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
    auto* device = deviceSettings_->devices().getCurrentAudioDevice();
    const auto id = project_->trackIds[int(i)];
    const auto first = audioInputs_[id].first;
    const int width = captureChannels_.contains(id) ? captureChannels_.at(id) : 1;
    juce::PopupMenu menu;
    menu.addSectionHeader(TRANS("Recording input"));
    menu.addItem(1, TRANS("First enabled input"), true, first < 0);
    if (device) {
        const auto names = device->getInputChannelNames();
        const auto mask = device->getActiveInputChannels();
        for (int n = 0; n < std::min(64, names.size()); ++n) {
            if (mask[n]) menu.addItem(10 + n, "Mono   " + names[n], true, width == 1 && first == n);
            if (n + 1 < std::min(64, names.size()) && mask[n] && mask[n+1])
                menu.addItem(100 + n, "Stereo   " + names[n] + " / " + names[n+1], true, width == 2 && first == n);
        }
    }
    menu.addItem(2, TRANS("Audio / MIDI Devices..."));
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&trackControls_[i]->input),
        [safe = juce::Component::SafePointer<MainComponent>(this), id, generation = projectGeneration_](int item) {
            if (!safe || !item || safe->recordTrack_ || safe->projectGeneration_ != generation) return;
            if (item == 2) { safe->menuItemSelected(showDevices, 3); return; }
            safe->audioInputs_[id].first = item == 1 ? -1 : item - (item >= 100 ? 100 : 10);
            if (item >= 10) safe->captureChannels_[id] = item >= 100 ? 2 : 1;
            safe->refreshMidiRoutes();
            safe->refreshEditor();
            safe->markSessionEdited();
        });
}

void MainComponent::rebuildTrackControls() {
    hasAudioInputs_ = std::find(project_->trackKinds.begin(), project_->trackKinds.end(), ostfstudio::TrackKind::audio) != project_->trackKinds.end();
    if (hasAudioInputs_) startTimerHz(24);
    trackControls_.clear();
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i) {
        auto controls = std::make_unique<TrackControls>();
        controls->size.assignHeight(groupDefaults_[static_cast<std::size_t>(trackHeightGroup(i))]);
        const auto name = juce::String::fromUTF8(project_->trackLanes[i].label);
        controls->size.setTitle(name + " " + TRANS("track height"));
        controls->arm.setTitle(name + " record arm");
        controls->arm.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffdd343e));
        controls->arm.onClick = [this, i] { setRecordArm(i, armedId_ != project_->trackIds[int(i)]); };
        controls->input.setTitle(name + " MIDI input");
        controls->input.onClick = [this, i] { showTrackInput(i); };
        for (juce::Component* c : std::array<juce::Component*,3>{&controls->arm, &controls->inputLamp, &controls->input}) rowOverlay_.addChildComponent(c);
        controls->cue.setTitle(name + " Cue");
        controls->cue.setTooltip(TRANS("Cue to headphones (outputs 3-4)"));
        controls->cue.setClickingTogglesState(true);
        controls->cue.onClick = [this, i, button = &controls->cue] { mixer_.setCue(i, button->getToggleState()); };
        rowOverlay_.addChildComponent(controls->cue);
        controls->mute.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff211f15));
        controls->mute.setTitle(name + " mute");
        controls->mute.setTooltip(TRANS("Mute ") + name);
        controls->mute.setClickingTogglesState(true);
        controls->mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffce42));
        controls->solo.setTitle(name + " solo");
        controls->solo.setTooltip(TRANS("Solo ") + name);
        controls->solo.setClickingTogglesState(true);
        controls->solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff277bdf));
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
        controls->size.changed = [this] { refreshMidiRoutes(); refreshGroupHeight(); resized(); repaint(); };
        for (auto* part : std::array<juce::Component*, 9>{&controls->mute, &controls->solo, &controls->volume,
                 &controls->pan, &controls->plugin, &controls->mix, &controls->preset, &controls->meter, &controls->size})
            rowOverlay_.addAndMakeVisible(part);
        controls->effects.button.onClick = [this, i] { showEffects(false, i); };
        updateEffectButton(controls->effects);
        rowOverlay_.addAndMakeVisible(controls->effects.button);
        controls->midiChannel = project_->sourceMidiChannels[i];
        if (project_->trackKinds[i] == ostfstudio::TrackKind::audio) {
            controls->input.setTitle(name + " audio input");
            controls->recordingMeter.setTitle(name + " input level");
            controls->inputGain.setTitle(name + " input gain");
            controls->inputGain.setTooltip(TRANS("Recording input gain (dB)"));
            controls->inputGain.setSliderStyle(juce::Slider::LinearHorizontal);
            controls->inputGain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 17);
            controls->inputGain.textFromValueFunction = [](double db) {
                return (db > 0 ? juce::String("+") : juce::String{}) + juce::String(db, 1) + " dB";
            };
            controls->inputGain.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
            controls->inputGain.setRange(-60, 24, .1);
            controls->inputGain.setDoubleClickReturnValue(true, 0);
            controls->inputGain.onValueChange = [this, i, slider = &controls->inputGain] {
                auto db = slider->getValue();
                audioInputs_[project_->trackIds[int(i)]].gainDb = db;
                if (recordTrack_ == i) transport_.setInputGain(db);
                markSessionEdited();
            };
            rowOverlay_.addChildComponent(controls->recordingMeter);
            rowOverlay_.addChildComponent(controls->inputGain);
            controls->plugin.setButtonText(TRANS("Link WAV"));
            controls->plugin.setTooltip(TRANS("Link a WAV file to this audio track"));
            controls->plugin.onClick = [this, i] { linkAudio(i); };
        }
        trackControls_.push_back(std::move(controls));
    }
    refreshMidiRoutes();
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
    const auto ports = juce::MidiOutput::getAvailableDevices();
    const auto id = project_->trackIds[int(index)];
    const auto stored = midiOutputs_.find(id);
    juce::PopupMenu hardware;
    hardware.addItem(1000, TRANS("Use instrument destination"), true, stored == midiOutputs_.end());
    for (int p = 0; p < ports.size(); ++p)
        hardware.addItem(1001 + p, ports[p].name, true, stored != midiOutputs_.end() && ports[p].identifier == stored->second.identifier);
    if (index < project_->sourceTrackCount() && project_->trackKinds[index] != ostfstudio::TrackKind::audio)
        menu.addSubMenu(stored == midiOutputs_.end() ? juce::String("MIDI OUT") : "MIDI OUT / " + stored->second.name, hardware);
    menu.addSubMenu(TRANS("Instrument destination"), outputs);
    menu.addItem(9, TRANS("Initial MIDI patch..."), index < project_->sourceTrackCount());
    menu.addSeparator();
    menu.addItem(8, TRANS("Link WAV on this track..."), !control.loading && index < project_->sourceTrackCount());
    menu.addItem(1, TRANS("Choose plug-in file..."), !control.loading);
    menu.addItem(3, TRANS("Choose plug-in bundle folder..."), !control.loading);
    menu.addItem(4, TRANS("Load selected VST3"), control.candidatePath.hasFileExtension("vst3") && !control.loading);
    menu.addItem(5, TRANS("Open plug-in editor"), control.instance != nullptr && !control.loading);
    menu.addSeparator();
    menu.addItem(2, TRANS("Clear assignment"), (control.pluginPath != juce::File{} || control.instrumentAssignment.isObject()) && !control.loading);
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&control.plugin),
        [self = juce::Component::SafePointer<MainComponent>(this), index, ports, id, generation = projectGeneration_](int selection) {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            if (selection == 1 || selection == 3) self->chooseTrackPlugin(index, selection == 3);
            if (selection == 4) self->scanTrackPlugin(index);
            if (selection == 5) self->openTrackPluginEditor(index);
            if (selection == 8) self->linkAudio(index);
            if (selection == 9) self->showPatch(index, 0);
            if (selection > channelId && selection <= channelId + 16) {
                self->stopTransport();
                if (self->trackControls_[index]->midiChannel != selection - channelId) self->markSessionEdited();
                self->trackControls_[index]->midiChannel = selection - channelId;
            }
            if (selection >= 1000) {
                self->stopTransport();
                if (selection == 1000) self->midiOutputs_.erase(id);
                else if (juce::isPositiveAndBelow(selection - 1001, ports.size())) self->midiOutputs_[id] = ports[selection - 1001];
                self->saveMidiRoutes();
                self->refreshMidiRoutes();
                return;
            }
            if (selection >= outputId) {
                self->midiOutputs_.erase(id);
                self->saveMidiRoutes();
                self->stopTransport();
                auto& route = self->trackControls_[index]->outputTrack;
                const auto next = selection == outputId ? std::nullopt : std::optional<std::size_t>(selection - outputId - 1);
                if (route != next) self->markSessionEdited();
                route = next;
            }
            if (selection == 2) {
                self->stopTransport();
                auto& strip = *self->trackControls_[index];
                strip.editor.reset();
                strip.instance.reset();
                strip.playableInstrument = false;
                if (!strip.instrumentAssignment.isVoid()) self->markSessionEdited();
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

ostfstudio::ProjectAudio* MainComponent::clipOn(std::size_t track) const {
    if (!project_) return nullptr;
    auto& clips = project_->audio;
    const auto it = std::find_if(clips.begin(), clips.end(), [track](const auto& clip) { return clip.track == track; });
    return it != clips.end() ? &*it : nullptr;
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    return project_ && !recordTrack_ && workspaceMode_ == WorkspaceMode::arrangement
           && files.size() == 1 && files[0].endsWithIgnoreCase(".wav");
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y) {
    if (!isInterestedInFileDrag(files)) return;
    auto track = pluginDropTrack({x, y});
    if (audioEditor_.isVisible() && audioEditor_.getBounds().contains(x, y)) track = selectedTrack_;
    if (!track || project_->trackKinds[*track] != ostfstudio::TrackKind::audio) {
        statusLabel_.setText(TRANS("Drop the WAV onto an audio track"), juce::dontSendNotification);
        return;
    }
    const auto tick = trackRows_[*track].withTrimmedLeft(trackHeaderWidth).contains(x, y)
        ? arrangementTickAt(x) : std::max<juce::int64>(0, cursorTick_);
    placeWav(juce::File(files[0]), *track, tick, false);
}

void MainComponent::placeWav(const juce::File& file, std::size_t index, juce::int64 tick, bool replace) {
    if (!project_ || index >= project_->sourceTrackCount() || recordTrack_) return;
    auto* prior = clipOn(index);
    if (prior && !replace) {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, TRANS("Replace"),
            TRANS("Replace the WAV on this track?"), TRANS("Replace"), TRANS("Cancel"), this,
            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this),
                generation = projectGeneration_, index, tick, file](int answer) {
                if (answer && safe && generation == safe->projectGeneration_) safe->placeWav(file, index, tick, true);
            }));
        return;
    }
    juce::WavAudioFormat wav;
    auto stream = file.createInputStream();
    std::unique_ptr<juce::AudioFormatReader> reader(stream ? wav.createReaderFor(stream.release(), true) : nullptr);
    if (!reader || reader->sampleRate <= 0 || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024
        || file.getSize() > 16 * 1024 * 1024 || reader->numChannels < 1 || reader->numChannels > 2) {
        statusLabel_.setText(TRANS("Unsupported WAV") + ": " + file.getFileName(), juce::dontSendNotification);
        return;
    }
    const auto end = project_->playbackEndTick();
    if (tick < 0 || tick >= end) {
        statusLabel_.setText(TRANS("Drop the sample before the song end"), juce::dontSendNotification);
        return;
    }
    ostfstudio::ProjectAudio clip{index, file.getFileName(), file, {}};
    clip.startTick = tick;
    clip.durationTicks = std::clamp<juce::int64>(std::ceil(project_->clock.tickAt(project_->clock.secondsAt(tick)
        + reader->lengthInSamples / reader->sampleRate) - tick), 1, end - tick);
    const bool playing = transportActive_;
    if (playing) stopTransport();
    if (prior) *prior = std::move(clip);
    else project_->audio.push_back(std::move(clip));
    markSessionEdited();
    waveforms_.load(project_->audio);
    selectTrack(index);
    finishNoteEdit(playing);
    statusLabel_.setText(file.getFileName(), juce::dontSendNotification);
}

void MainComponent::linkAudio(std::size_t index) {
    if (!project_ || index >= project_->sourceTrackCount()) return;
    chooser_ = std::make_unique<juce::FileChooser>(TRANS("Link WAV at the start of the track"), juce::File{}, "*.wav");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_](const juce::FileChooser& picker) {
            if (!safe || safe->projectGeneration_ != generation) return;
            const auto file = picker.getResult();
            if (file == juce::File{}) return;
            safe->placeWav(file, index, 0, false);
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
    refreshTrackActions();
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
    refreshTrackActions();
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

void MainComponent::loadTrackPlugin(std::size_t index, const juce::PluginDescription& description, bool restoring, juce::MemoryBlock state) {
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
    refreshTrackActions();
    strip.plugin.setButtonText(TRANS("Loading..."));
    statusLabel_.setText(TRANS("Loading plug-in: ") + description.name, juce::dontSendNotification);
    juce::Timer::callAfterDelay(65,
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_, description, restoring, state = std::move(state)] {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            self->pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512,
                [self, index, generation, description, restoring, state](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
                    if (self == nullptr || self->projectGeneration_ != generation) return;
                    auto& target = *self->trackControls_[index];
                    target.loading = false;
                    self->refreshTrackActions();
                    if (!instance) {
                        target.plugin.setButtonText(target.instance ? target.instance->getName() : TRANS("Instrument"));
                        self->statusLabel_.setText(TRANS("VST3 load failed: ") + error, juce::dontSendNotification);
                        return;
                    }
                    self->stopTransport();
                    target.editor.reset();
                    if (description.isInstrument) instance->disableNonMainBuses();
                    if (!state.isEmpty()) instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
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
                        self->markSessionEdited();
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
        std::unique_ptr<juce::Component> body;
        if (isOstfArranger(instance->getPluginDescription())) body = std::make_unique<GeneratorPanel>(std::move(editor), *this);
        else body = std::move(editor);
        const auto target = pluginTarget(false, index, -1);
        window->setContentOwned(new ControlledPluginEditor(std::move(body), [this, target, name = instance->getName()](juce::Button& button) {
            chooseControlTarget(button, target, name);
        }), true);
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
        control.size.changed = [this] { refreshGroupHeight(); resized(); repaint(); };
        control.size.setTitle(mixer_.outputName(i) + " " + TRANS("track height"));
        rowOverlay_.addChildComponent(control.size);
        rowOverlay_.addChildComponent(control.meter);
        rowOverlay_.addChildComponent(control.effects.button);
        auto& volume = control.volume;
        volume.setName(mixer_.outputName(i) + " volume");
        volume.setSliderStyle(juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 16);
        volume.setTextValueSuffix(" dB");
        volume.setRange(-60.0, 6.0, 0.1);
        volume.setValue(0.0, juce::dontSendNotification);
        volume.setDoubleClickReturnValue(true, 0.0);
        volume.onValueChange = [this, i] { mixer_.setOutputVolume(i, outputControls_[i].volume.getValue()); };
        rowOverlay_.addChildComponent(volume);
        control.mute.setClickingTogglesState(true);
        control.mute.setTooltip(TRANS("Mute ") + mixer_.outputName(i));
        control.mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffce42));
        control.mute.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff211f15));
        control.mute.onClick = [this, i] { mixer_.setOutputMuted(i, outputControls_[i].mute.getToggleState()); };
        rowOverlay_.addChildComponent(control.mute);
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
    const auto name = output ? mixer_.outputName(index) : juce::String::fromUTF8(project_->trackLanes[index].label);
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
            self->markSessionEdited();
            auto first = chain.slots.begin();
            if (from < to) std::rotate(first + from, first + from + 1, first + to + 1);
            else std::rotate(first + to, first + from, first + from + 1);
            self->remapEffectTargets(output, index, from, to);
        },
        [current, output, index](int position) {
            auto* self = current();
            if (!self) return;
            auto& chain = self->effectChain(output, index);
            if (chain.loading || static_cast<std::size_t>(position) >= chain.slots.size()) return;
            auto& slot = chain.slots[static_cast<std::size_t>(position)];
            if (!slot.effect) return;
            self->openEffectEditor(output, index, size_t(position));
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
            self->remapEffectTargets(output, index, position, -1);
            self->markSessionEdited();
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

namespace {
juce::File melodyneApplication() {
#if JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
        .getChildFile("Celemony/Melodyne 5/Melodyne.exe");
#elif JUCE_MAC
    return juce::File("/Applications/Melodyne 5/Melodyne.app");
#else
    return {};
#endif
}
}

void MainComponent::refreshClipLane() {
    clipLane_.clips.clear();
    if (project_ && selectedTrack_ && *selectedTrack_ < project_->trackLanes.size()) {
        const auto track = *selectedTrack_;
        if (const auto* audio = clipOn(track)) {
            const auto first = project_->bars.firstBar + double(audio->startTick) / project_->ticksPerBar;
            const auto last = project_->bars.firstBar + waveforms_.endTick(*audio, project_->clock) / project_->ticksPerBar;
            clipLane_.clips.push_back({{first, last}, audio->path.fromLastOccurrenceOf("/", false, false)});
        } else {
            for (const auto& region : project_->trackLanes[track].regions) {
                const auto bars = region.exactBars.value_or(ostfstudio::TimelineExtent{double(region.bars.firstBar), double(region.bars.endBar)});
                clipLane_.clips.push_back({{bars.start, bars.end}, juce::String::fromUTF8(region.label)});
            }
        }
    }
    if (clipLane_.selected && clipLane_.at(*clipLane_.selected) != clipLane_.selected) clipLane_.selected.reset();
    clipLane_.repaint();
}

void MainComponent::selectEditorClip(std::optional<double> bar, bool locate) {
    clipLane_.selected = bar;
    clipLane_.repaint();
    if (locate && bar && project_ && !recordTrack_)
        locateTransport(std::llround((*bar - project_->bars.firstBar) * project_->ticksPerBar), &arrangementScroll_);
}

void MainComponent::editMelodyneCopy() {
    const auto* clip = audioEditor_.sourceClip();
    if (recordTrack_) return;
    if (!clip || !selectedTrack_ || !clipLane_.selected || selectedGlobal_) {
        const auto app = melodyneApplication();
        if (!app.exists() || !app.startAsProcess())
            statusLabel_.setText(TRANS("Melodyne standalone could not be opened."), juce::dontSendNotification);
        return;
    }
    const auto source = sourceWavBytes(*clip);
    juce::int64 frames = 0;
    const auto bytes = ostfstudio::clipWav(*clip, source, project_->clock, frames);
    const auto digest = juce::SHA256(source).toHexString();
    if (bytes.isEmpty() || (clip->sha256.isNotEmpty() && digest != clip->sha256)) {
        statusLabel_.setText(TRANS("The source has changed. Create a new working copy."), juce::dontSendNotification);
        return;
    }
    const auto id = project_->trackIds[int(*selectedTrack_)];
    auto it = melodyneCopies_.find(id);
    if (it == melodyneCopies_.end() || it->second.originalHash != digest || it->second.startFrame != clip->startFrame
        || it->second.frames != frames || it->second.startTick != clip->startTick || it->second.durationTicks != clip->durationTicks
        || !it->second.wav.existsAsFile()) {
        auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("OSTF Studio").getChildFile("Melodyne").getChildFile(juce::Uuid().toString());
        auto name = juce::File::createLegalFileName(juce::File::isAbsolutePath(clip->path)
            ? juce::File(clip->path).getFileName() : clip->path.fromLastOccurrenceOf("/", false, false));
        const auto copy = folder.getChildFile(name.isEmpty() ? "Clip.wav" : name).withFileExtension(".wav");
        if (folder.createDirectory().failed() || !copy.replaceWithData(bytes.getData(), bytes.getSize())) {
            statusLabel_.setText(TRANS("Could not save the working WAV"), juce::dontSendNotification);
            return;
        }
        it = melodyneCopies_.insert_or_assign(id, MelodyneCopy{copy, digest, juce::SHA256(bytes).toHexString(), clip->startFrame, frames, clip->startTick, clip->durationTicks}).first;
    }
    const auto& copy = it->second.wav;
    const auto app = melodyneApplication();
#if JUCE_MAC
    juce::ChildProcess opener;
    const bool opened = app.isDirectory() && opener.start(juce::StringArray{"/usr/bin/open", "-a", app.getFullPathName(), copy.getFullPathName()});
#else
    const bool opened = app.existsAsFile() && app.startAsProcess(copy.getFullPathName().quoted());
#endif
    refreshEditor();
    statusLabel_.setText((opened ? TRANS("Use Replace Audio in Melodyne, then Apply edited WAV")
                                 : TRANS("Open this WAV in Melodyne, then use Apply edited WAV")) + "   " + copy.getFullPathName(),
                         juce::dontSendNotification);
}

void MainComponent::applyMelodyneCopy() {
    if (!selectedTrack_ || recordTrack_) return;
    auto* clip = clipOn(*selectedTrack_);
    const auto it = melodyneCopies_.find(project_->trackIds[int(*selectedTrack_)]);
    if (!clip || it == melodyneCopies_.end()) return;
    const auto& copy = it->second;
    juce::int64 frames = 0;
    const auto current = ostfstudio::clipWav(*clip, sourceWavBytes(*clip), project_->clock, frames);
    if (copy.startFrame != clip->startFrame || copy.startTick != clip->startTick || copy.durationTicks != clip->durationTicks
        || frames != copy.frames || juce::SHA256(current).toHexString() != copy.workingHash) {
        statusLabel_.setText(TRANS("The source has changed. Create a new working copy."), juce::dontSendNotification);
        return;
    }
    if (juce::SHA256(copy.wav).toHexString() == copy.workingHash) {
        statusLabel_.setText(TRANS("Use Replace Audio in Melodyne, then Apply edited WAV"), juce::dontSendNotification);
        return;
    }
    ostfstudio::ProjectAudio edited;
    const auto check = ostfstudio::prepareEditedWav(*clip, it->second.wav, it->second.originalHash, edited, frames);
    if (check.failed()) {
        statusLabel_.setText(juce::translate(check.getErrorMessage()), juce::dontSendNotification);
        return;
    }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    *clip = std::move(edited);
    melodyneCopies_.erase(it);
    markSessionEdited();
    waveforms_.load(project_->audio);
    finishNoteEdit(playing);
    statusLabel_.setText(TRANS("Edited WAV applied") + "   " + clip->path, juce::dontSendNotification);
}

void MainComponent::displayAiState(const juce::String& state, juce::uint32 colour) {
    aiButton_.setButtonText(state);
    aiButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(colour));
    aiButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    aiButton_.setTitle(state);
}

void MainComponent::updateAiConnection(AccountAction action) {
    if (!aiButton_.isEnabled() || signInPending_) return;
    aiButton_.setEnabled(false);
    signInPending_ = action == AccountAction::login;
    displayAiState(TRANS("ChatGPT connecting"), 0xff805510);
    const auto provider = ostfstudio::chatGptProvider({}, {}, aiModel_, aiMode());
    const auto safe = juce::Component::SafePointer<MainComponent>(this);
    activeAi_.reset();
    if (importWindow_) setImportConnections(*importWindow_, {});
    aiConnectionWorker_.addJob([safe, provider, action] {
        const auto showLink = [safe](const juce::URL& url) {
            juce::MessageManager::callAsync([safe, url] {
                if (!safe || !safe->signInPending_) return;
                safe->signInUrl_ = url;
                safe->aiButton_.setEnabled(true);
                safe->displayAiState(TRANS("ChatGPT sign-in pending"), 0xff805510);
            });
        };
        const auto result = action == AccountAction::login    ? provider->connect(showLink)
                            : action == AccountAction::logout ? provider->disconnect()
                            : provider->connected()
                                ? juce::Result::ok()
                                : juce::Result::fail(TRANS("Reconnect ChatGPT before starting AI analysis."));
        juce::StringArray labels;
        const auto models = result.wasOk() && action != AccountAction::logout ? provider->availableModels(true, &labels) : juce::StringArray{};
        juce::MessageManager::callAsync([safe, provider, result, action, models, labels] {
            if (!safe) return;
            safe->signInUrl_ = {};
            safe->signInPending_ = false;
            safe->activeAi_ = result.wasOk() && action != AccountAction::logout ? provider : nullptr;
            safe->aiButton_.setEnabled(true);
            safe->aiModels_ = models;
            safe->aiModelNames_ = labels;
            if (!models.isEmpty() && !models.contains(safe->aiModel_)) {
                safe->aiModel_.clear();
                safe->viewPreferences_.getUserSettings()->removeValue("studioChatGptModel");
            }
            safe->showModelChoices();
            safe->applyAiSelection();
            safe->resized();
            safe->displayAiState(safe->activeAi_ ? TRANS("ChatGPT connected") : TRANS("ChatGPT disconnected"),
                                 safe->activeAi_ ? 0xff17634f : 0xff773d36);
            safe->viewPreferences_.getUserSettings()->setValue("studioChatGptBrowserLogin", safe->activeAi_ != nullptr);
            if (safe->importWindow_)
                setImportConnections(*safe->importWindow_, safe->activeAi_
                                                               ? std::vector{safe->activeAi_}
                                                               : std::vector<ostfstudio::AnalysisConnection>{});
            safe->statusLabel_.setText(result.failed() ? result.getErrorMessage() : safe->aiButton_.getButtonText(),
                                       juce::dontSendNotification);
            if (result.failed() && action != AccountAction::restore)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "ChatGPT",
                                                       result.getErrorMessage());
        });
    });
}

void MainComponent::applyAiSelection() {
    if (!activeAi_) return;
    activeAi_ = ostfstudio::chatGptProvider({}, {}, aiModel_, aiMode());
    if (importWindow_) setImportConnections(*importWindow_, {activeAi_});
}

void MainComponent::showModelChoices() {
    modelPicker_.clear(juce::dontSendNotification);
    modelPicker_.addItem(TRANS("Automatic model"), 1);
    for (int i = 0; i < aiModels_.size(); ++i) {
        modelPicker_.addItem(aiModelNames_[i].isNotEmpty() ? aiModelNames_[i] : aiModels_[i], i + 2);
    }
    modelPicker_.setSelectedId(aiModels_.indexOf(aiModel_) + 2, juce::dontSendNotification);
}

void MainComponent::refreshAiModels() {
    if (!activeAi_ || refreshingModels_) return;
    refreshingModels_ = true;
    fetchModels_.setEnabled(false);
    auto provider = activeAi_;
    aiConnectionWorker_.addJob([safe = juce::Component::SafePointer<MainComponent>(this), provider] {
        juce::StringArray labels;
        const auto models = provider->availableModels(true, &labels);
        juce::MessageManager::callAsync([safe, models, labels] {
            if (!safe) return;
            safe->refreshingModels_ = false;
            safe->fetchModels_.setEnabled(true);
            if (!safe->activeAi_) return;
            if (models.isEmpty()) {
                safe->statusLabel_.setText(TRANS("Could not refresh the model list."), juce::dontSendNotification);
                return;
            }
            safe->aiModels_ = models;
            safe->aiModelNames_ = labels;
            if (!models.contains(safe->aiModel_)) {
                safe->aiModel_.clear();
                safe->viewPreferences_.getUserSettings()->removeValue("studioChatGptModel");
                safe->applyAiSelection();
            }
            safe->showModelChoices();
        });
    });
}

void MainComponent::aiConnectionMenu() {
    if (!aiButton_.isEnabled()) return;
    if (signInPending_) {
        signInUrl_.launchInDefaultBrowser();
        return;
    }
    if (!activeAi_) {
        updateAiConnection(AccountAction::login);
        return;
    }
    juce::PopupMenu menu;
    menu.addItem(1, TRANS("Sign in again..."));
    menu.addItem(2, TRANS("Disconnect from Studio"));
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&aiButton_),
                       [safe = juce::Component::SafePointer<MainComponent>(this)](int action) {
                           if (!safe) return;
                           if (action == 1) safe->updateAiConnection(AccountAction::login);
                           if (action == 2) safe->updateAiConnection(AccountAction::logout);
                       });
}

double MainComponent::linkQuantum() const {
    return project_ && project_->ppq > 0 ? std::max(1.0, double(project_->ticksPerBar) / project_->ppq) : 4.0;
}

void MainComponent::toggleLink() {
    if (recordTrack_) return;
    const bool playing = transportActive_;
    const juce::ScopedValueSetter<bool> internal(followingLink_, true);
    if (playing) stopTransport();
    if (link_) {
        link_.reset();
        lastLinkPlaying_ = false;
    } else {
        const auto tempo = project_ ? project_->clock.bpmAt(std::max<juce::int64>(0, cursorTick_)) * auditionRate_ : 120.0;
        link_ = std::make_unique<ostfstudio::LinkSession>(tempo > 0 ? tempo : 120);
        lastLinkPlaying_ = false;
        startTimerHz(24);
    }
    if (playing) {
        const juce::ScopedValueSetter<bool> publish(followingLink_, false);
        startTransport(false);
    }
}

void MainComponent::pollLink() {
    if (!link_ || !project_) return;
    const auto state = link_->state(linkQuantum());
    const juce::ScopedValueSetter<bool> internal(followingLink_, true);
    if (state.playing != lastLinkPlaying_) {
        lastLinkPlaying_ = state.playing;
        if (state.playing && !transportActive_) startTransport(false);
        else if (!state.playing && transportActive_) stopTransport();

    }
}

void MainComponent::editLinkTempo() {
    if (!link_ || recordTrack_) return;
    auto* dialog = new juce::AlertWindow("Ableton Link", TRANS("Link tempo"), juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("bpm", juce::String(link_->state(linkQuantum()).tempo, 2), "BPM");
    dialog->addButton(TRANS("Apply"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(TRANS("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<MainComponent>(this), dialog](int result) {
            const double tempo = dialog->getTextEditorContents("bpm").getDoubleValue();
            if (result && safe && safe->link_ && tempo >= 20 && tempo <= 999) safe->link_->setTempo(tempo);
        }), true);
}

void MainComponent::openSplice() {
    if (spliceWindow_) {
        if (!spliceWindow_->isVisible()) deviceSettings_->devices().addAudioCallback(&splicePlayer_);
        spliceWindow_->setVisible(true);
        spliceWindow_->toFront(true);
        return;
    }
    if (spliceLoading_) return;
    auto& browser = ensurePluginBrowser();
    for (const auto& type : browser.types()) {
        if (type.pluginFormatName != "VST3" || !type.name.trim().equalsIgnoreCase("Splice Sounds")
            || !type.isInstrument) continue;
        if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
        auto* device = deviceSettings_->devices().getCurrentAudioDevice();
        spliceLoading_ = true;
        pluginFormats_.createPluginInstanceAsync(type, device ? device->getCurrentSampleRate() : 44100.,
            device ? device->getCurrentBufferSizeSamples() : 512,
            [safe = juce::Component::SafePointer<MainComponent>(this)](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
                if (!safe) return;
                safe->spliceLoading_ = false;
                std::unique_ptr<juce::AudioProcessorEditor> editor(instance ? instance->createEditorAndMakeActive() : nullptr);
                if (!editor) {
                    safe->statusLabel_.setText(TRANS("Could not open Splice Sounds") + " " + error, juce::dontSendNotification);
                    return;
                }
                safe->spliceInstance_ = std::move(instance);
                safe->splicePlayer_.setProcessor(safe->spliceInstance_.get());
                safe->deviceSettings_->devices().addAudioCallback(&safe->splicePlayer_);
                auto window = std::make_unique<PluginWindow>("Splice Sounds");
                window->setContentOwned(editor.release(), true);
                window->centreWithSize(window->getWidth(), window->getHeight());
                window->onClosed = [safe] { if (safe) safe->deviceSettings_->devices().removeAudioCallback(&safe->splicePlayer_); };
                safe->spliceWindow_ = std::move(window);
                safe->spliceWindow_->setVisible(true);
                safe->spliceWindow_->toFront(true);
            });
        return;
    }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions{}.withTitle("Splice Sounds")
        .withMessage(TRANS("Install the Splice Sounds instrument from Splice, then scan your VST3 plugins."))
        .withButton({TRANS("Scan VST3"), 1}).withButton({TRANS("Get Splice Sounds"), 2})
        .withButton(juce::MessageBoxButtonOptions{TRANS("Cancel"), 0}.withRole(juce::MessageBoxButtonRole::cancel)),
        [safe = juce::Component::SafePointer<MainComponent>(this)](int choice) {
            if (!safe) return;
            if (choice == 1) {
                if (!safe->pluginBrowserButton_.getToggleState()) safe->togglePluginBrowser();
                safe->ensurePluginBrowser().scan();
            } else if (choice == 2) juce::URL("https://splice.com/tools/sounds-plugin").launchInDefaultBrowser();
        });
}

void MainComponent::openEffectEditor(bool output, std::size_t index, std::size_t position) {
    auto& slot = effectChain(output, index).slots[position];
    if (!slot.editor) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(slot.effect->createEditorAndMakeActive());
        if (!editor) editor = std::make_unique<juce::GenericAudioProcessorEditor>(*slot.effect);
        auto window = std::make_unique<PluginWindow>(slot.effect->getName());
        window->setContentOwned(new ControlledPluginEditor(std::move(editor), [this, output, fx = slot.effect.get()](juce::Button& button) {
            const size_t count = output ? outputControls_.size() : trackControls_.size();
            for (size_t track = 0; track < count; ++track) {
                const auto& effects = effectChain(output, track).slots;
                for (size_t pos = 0; pos < effects.size(); ++pos) if (effects[pos].effect.get() == fx) {
                    chooseControlTarget(button, pluginTarget(output, track, int(pos)), fx->getName());
                    return;
                }
            }
        }), true);
        window->centreWithSize(window->getWidth(), window->getHeight());
        slot.editor = std::move(window);
    }
    slot.editor->setVisible(true);
    slot.editor->toFront(true);
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
        auto& slot = chain.slots[static_cast<std::size_t>(i)];
        slot.id = ids[i];
        const auto& states = project_->savedMix->stockEffectStates;
        if (const auto state = states.find(ostfstudio::SessionMix::effectSlot(output, index, i)); state != states.end())
            slot.savedState = state->second;
        for (const auto& type : types)
            if (effectId(type) == ids[i]) { loadEffect(output, index, static_cast<std::size_t>(i), type); break; }
    }
    updateEffectButton(chain);
}

void MainComponent::loadEffect(bool output, std::size_t index, std::size_t position, const juce::PluginDescription& description, bool openEditor) {
    auto& chain = effectChain(output, index);
    ++chain.loading;
    refreshTrackActions();
    updateEffectButton(chain);
    pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512,
        [safe = juce::Component::SafePointer<MainComponent>(this), output, index, position,
         generation = projectGeneration_, description, openEditor](std::unique_ptr<juce::AudioPluginInstance> effect, const juce::String& error) {
            if (!safe || safe->projectGeneration_ != generation) return;
            auto& chain = safe->effectChain(output, index);
            --chain.loading;
            safe->refreshTrackActions();
            auto& slot = chain.slots[position];
            if (effect) effect->disableNonMainBuses();
            if (!effect || !PreviewTransport::supportsInsert(*effect)) {
                if (slot.id.isEmpty()) chain.slots.pop_back();
                safe->statusLabel_.setText(effect ? TRANS("Insert requires a stereo audio effect") : TRANS("Could not load FX: ") + error,
                                           juce::dontSendNotification);
            } else {
                safe->stopTransport();
                juce::MemoryBlock state;
                if (slot.savedState.isNotEmpty() && state.fromBase64Encoding(slot.savedState))
                    effect->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                slot.effect = std::move(effect);
                if (slot.id != effectId(description) || openEditor) safe->markSessionEdited();
                slot.id = effectId(description);
                for (const auto& fx : ostfstudio::stockEffects)
                    if (slot.id == fx.chainId()) { safe->stockEffectChanges_.observe(*slot.effect); break; }
                if (openEditor) {
                    std::rotate(chain.slots.begin(), chain.slots.begin() + position, chain.slots.begin() + position + 1);
                    safe->openEffectEditor(output, index, 0);
                }
            }
            safe->updateEffectButton(chain);
        });
}

std::optional<TimelineLane> MainComponent::globalLaneAt(juce::Point<int> p) const {
    if (!project_ || !rowOverlay_.isVisible() ||
        !rowOverlay_.getBounds().withTrimmedTop(timelineRowHeight()).contains(p))
        return {};
    int y = rowOverlay_.getY() + timelineRowHeight() - juce::roundToInt(verticalScroll_.getCurrentRangeStart());
    const int height = groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)];
    auto view = currentArrangement();
    for (const auto& lane : view.globalLanes) {
        if (p.y >= y && p.y < y + height)
            return lane.kind;
        y += height;
        if (theoryVisible_ && lane.kind == TimelineLane::chord)
            y += 2 * timelineRowHeight();
    }
    return {};
}

juce::int64 MainComponent::arrangementTickAt(int x, bool endBoundary) const {
    const auto area = rowOverlay_.getBounds().withTrimmedLeft(trackHeaderWidth);
    const auto range = horizontalRange();
    const auto bar =
        range.start - 1. +
        (range.end - range.start) * juce::jlimit(0., 1., double(x - area.getX()) / std::max(1, area.getWidth()));
    return std::clamp<juce::int64>(std::llround(bar * project_->ticksPerBar), 0, endBoundary ? project_->playbackEndTick() : project_->durationTicks - 1);
}

void MainComponent::applyGlobalChange(TimelineLane lane, juce::Array<juce::var> remove, juce::Array<juce::var> insert) {
    if (!project_ || recordTrack_)
        return;
    const bool playing = transportActive_;
    if (playing)
        stopTransport();
    juce::MemoryBlock before;
    auto result = project_->changeGlobalEvents(lane, remove, insert, before);
    if (result.wasOk()) {
        rememberScoreEdit(std::move(before));
        theory_.reset();
        refreshTheory();
        finishNoteEdit(playing);
    } else if (playing)
        startTransport(false);
    statusLabel_.setText(result.wasOk() ? TRANS("Global lane updated.") : juce::translate(result.getErrorMessage()),
                         juce::dontSendNotification);
    repaint();
}

void MainComponent::globalCommand(int command) {
    using namespace ostfstudio;
    if (!project_ || !selectedGlobal_ || recordTrack_)
        return;
    const auto lane = *selectedGlobal_;
    auto score = juce::JSON::parse(project_->sourceScore.toString());
    auto event = globalEventAt(score, lane, selectedGlobalTick_);
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    const auto generation = projectGeneration_;
    auto source = globalSource(lane);
    const char* timeKey = source.span ? "start" : "at";
    if (command == 4 && copiedLane_ == lane && !copiedEvent_.isVoid()) {
        auto copy = copiedEvent_.clone();
        auto snap = std::max<juce::int64>(1, project_->ppq / 4);
        auto at = (selectedGlobalTick_ / snap) * snap;
        bool replace = event.isObject() && (source.span || juce::int64(event[timeKey]) == at);
        if (replace) {
            copy.getDynamicObject()->setProperty(timeKey, event[timeKey]);
            if (source.span)
                copy.getDynamicObject()->setProperty("duration", event["duration"]);
            if (event.hasProperty("id"))
                copy.getDynamicObject()->setProperty("id", event["id"]);
            juce::AlertWindow::showOkCancelBox(
                juce::MessageBoxIconType::QuestionIcon, TRANS("Paste"),
                TRANS("Replace the selected event with the copied value?"), TRANS("Replace"), TRANS("Cancel"), this,
                juce::ModalCallbackFunction::create([safe, generation, lane, event, copy](int answer) {
                    if (answer && safe && safe->projectGeneration_ == generation)
                        safe->applyGlobalChange(lane, {event}, {copy});
                }));
        } else {
            copy.getDynamicObject()->setProperty(timeKey, at);
            if (copy.hasProperty("id"))
                copy.getDynamicObject()->setProperty("id", "event-" + juce::Uuid().toString());
            if (source.span)
                copy.getDynamicObject()->setProperty(
                    "duration",
                    juce::int64(std::llround(juce::int64(copy["duration"]) * double(project_->ppq) / copiedPpq_)));
            applyGlobalChange(lane, {}, {copy});
        }
        return;
    }
    if (event.isVoid()) {
        if (command == 1 && (lane == TimelineLane::key || lane == TimelineLane::scale))
            editSongMusic(SongMusicEdit::defaults);
        return;
    }
    if (command == 2 || command == 3) {
        copiedLane_ = lane;
        copiedEvent_ = event.clone();
        copiedPpq_ = project_->ppq;
        if (command == 3)
            applyGlobalChange(lane, {event}, {});
        return;
    }
    if (command != 1)
        return;
    auto* dialog = new juce::AlertWindow(TRANS("Edit global event"), {}, juce::MessageBoxIconType::NoIcon, this);
    juce::String value;
    if (lane == TimelineLane::section)
        value = event["name"].toString();
    if (lane == TimelineLane::chord)
        value = event["label"].toString();
    if (lane == TimelineLane::tempo)
        value = event["bpm"].toString();
    if (lane == TimelineLane::meter)
        value = event["numerator"].toString() + "/" + event["denominator"].toString();
    if (lane == TimelineLane::scale)
        value = event["label"].toString();
    if (lane == TimelineLane::key)
        value = event["key"]["mode"].toString();
    dialog->addTextEditor("value", value,
                          juce::translate(lane == TimelineLane::key     ? "Key mode"
                                          : lane == TimelineLane::scale ? "Scale name"
                                                                        : "Value"));
    if (lane == TimelineLane::key || lane == TimelineLane::scale) {
        dialog->addTextEditor("tonic", pitchText(lane == TimelineLane::key ? event["key"]["tonic"] : event["tonic"]),
                              TRANS("Tonic"));
        if (lane == TimelineLane::scale)
            dialog->addTextEditor("intervals", intervalsText(event), TRANS("Intervals in semitones"));
    }
    dialog->addButton(TRANS("Apply"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(TRANS("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true, juce::ModalCallbackFunction::create([safe, generation, lane, event, dialog](int answer) {
            if (!answer || !safe || safe->projectGeneration_ != generation)
                return;
            auto copy = event.clone();
            auto* o = copy.getDynamicObject();
            auto v = dialog->getTextEditorContents("value").trim();
            auto invalid = [&] {
                safe->statusLabel_.setText(TRANS("Enter a valid musical value."), juce::dontSendNotification);
            };
            if (lane == TimelineLane::section)
                o->setProperty("name", v);
            if (lane == TimelineLane::chord) {
                auto updated = editChordSymbol(copy, v);
                if (updated.failed()) {
                    safe->statusLabel_.setText(juce::translate(updated.getErrorMessage()), juce::dontSendNotification);
                    return;
                }
            }
            if (lane == TimelineLane::tempo) {
                auto number = juce::JSON::fromString(v);
                if (!(number.isInt() || number.isInt64() || number.isDouble()) || double(number) <= 0) {
                    invalid();
                    return;
                }
                o->setProperty("bpm", number);
            }
            if (lane == TimelineLane::meter) {
                auto parts = juce::StringArray::fromTokens(v, "/", "");
                if (parts.size() != 2 || !parts[0].containsOnly("0123456789") || !parts[1].containsOnly("0123456789")) {
                    invalid();
                    return;
                }
                o->setProperty("numerator", parts[0].getIntValue());
                o->setProperty("denominator", parts[1].getIntValue());
            }
            if (lane == TimelineLane::key || lane == TimelineLane::scale) {
                auto tonic = parseSpelling(dialog->getTextEditorContents("tonic"));
                if (tonic.isVoid()) {
                    invalid();
                    return;
                }
                if (lane == TimelineLane::key) {
                    auto key = copy["key"].clone();
                    key.getDynamicObject()->removeProperty("label");
                    key.getDynamicObject()->setProperty("state", "tonal");
                    key.getDynamicObject()->setProperty("tonic", tonic);
                    key.getDynamicObject()->setProperty("mode", v);
                    o->setProperty("key", key);
                } else {
                    juce::Array<juce::var> ints;
                    for (auto part :
                         juce::StringArray::fromTokens(dialog->getTextEditorContents("intervals"), " ,", "")) {
                        if (part.isEmpty())
                            continue;
                        if (!part.containsOnly("0123456789")) {
                            invalid();
                            return;
                        }
                        ints.add(part.getIntValue());
                    }
                    o->setProperty("tonic", tonic);
                    o->setProperty("intervals", ints);
                    o->setProperty("label", v);
                }
            }
            safe->applyGlobalChange(lane, {event}, {copy});
        }),
        true);
}

void MainComponent::mouseDown(const juce::MouseEvent& event) {
    if (rowOverlay_.isVisible() && viewportPan_.begin(*this, event, rowOverlay_.getBounds().withTrimmedLeft(trackHeaderWidth))) return;
    resizingAudio_.reset();
    pendingMidiDrag_.reset();
    const bool wasTempo = selectedGlobal_ == TimelineLane::tempo;
    if (!rowOverlay_.isVisible() || !rowOverlay_.getBounds().contains(event.getPosition())) return;
    if (auto lane = globalLaneAt(event.getPosition())) {
        if (*lane == TimelineLane::tempo && recordTrack_) return;
        clipLane_.selected.reset();
        selectedGlobal_ = lane;
        selectedGlobalTick_ = event.x < rowOverlay_.getX() + trackHeaderWidth
            ? std::max<juce::int64>(0, cursorTick_) : arrangementTickAt(event.x);
        if (*lane == TimelineLane::tempo) {
            editorVisible_ = true;
            mixerSelected_ = false;
            refreshEditor();
        } else if (wasTempo) refreshEditor();
        grabKeyboardFocus();
        repaint();
        if (event.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            auto score = juce::JSON::parse(project_->sourceScore.toString());
            bool hasEvent = !ostfstudio::globalEventAt(score, *lane, selectedGlobalTick_).isVoid();
            menu.addItem(1, TRANS("Edit value..."),
                         hasEvent || *lane == TimelineLane::key || *lane == TimelineLane::scale);
            menu.addItem(2, TRANS("Copy"), hasEvent);
            menu.addItem(3, TRANS("Cut"), hasEvent);
            menu.addItem(4, TRANS("Paste"), copiedLane_ == lane);
            const auto generation = projectGeneration_;
            menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                               [safe = juce::Component::SafePointer<MainComponent>(this), generation](int choice) {
                                   if (safe && safe->projectGeneration_ == generation && choice)
                                       safe->globalCommand(choice);
                               });
        }
        return;
    }
    selectedGlobal_.reset();
    if (wasTempo) refreshEditor();
    if (event.mods.isPopupMenu()) {
        if (auto output = outputDropTrack(event.getPosition())) showRowMenu(true, *output);
        else if (auto track = pluginDropTrack(event.getPosition())) showRowMenu(false, *track);
        return;
    }
    for (std::size_t i = 0; i < trackRows_.size(); ++i) {
        if (trackRows_[i].contains(event.getPosition())) {
            if (event.mods.isLeftButtonDown() && trackRows_[i].withWidth(trackNameWidth).contains(event.getPosition())
                && canExportTrack(i))
                pendingMidiDrag_ = i;
            selectTrack(i);
            const auto timeline = trackRows_[i].withTrimmedLeft(trackHeaderWidth);
            const auto bar = project_->bars.firstBar + double(arrangementTickAt(event.x)) / project_->ticksPerBar;
            selectEditorClip(timeline.contains(event.getPosition()) ? clipLane_.at(bar) : std::nullopt, false);
            if (auto* clip = clipOn(i); clip && !recordTrack_ && event.mods.isLeftButtonDown() && timeline.contains(event.getPosition())) {
                const auto& clock = auditionClock_ ? *auditionClock_ : project_->clock;
                const auto range = horizontalRange();
                const double bar = 1 + waveforms_.endTick(*clip, clock) / project_->ticksPerBar;
                const double edge = timeline.getX() + (bar - range.start) / (range.end - range.start) * timeline.getWidth();
                if (std::abs(event.x - edge) <= 8 && waveforms_.available(i)) {
                    resizingAudio_ = i;
                    previewAudioLength_ = clip->durationTicks;
                }
            }
            return;
        }
    }
}

void MainComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    if (TimelinePan::requested(event.mods)) return;
    if (!event.mods.isLeftButtonDown() || !project_)
        return;
    const auto p = event.getPosition();
    if (globalLaneAt(p) && selectedGlobal_) {
        globalCommand(1);
        return;
    }
    if (!rowOverlay_.getBounds().withWidth(trackNameWidth).contains(p))
        return;
    if (auto track = pluginDropTrack(p))
        renameTrack(*track);
    else if (auto out = outputDropTrack(p))
        renameTrack(*out, true);
}

void MainComponent::mouseDrag(const juce::MouseEvent& event) {
    if (viewportPan_.move(event)) return;
    if (resizingAudio_) {
        if (const auto* clip = clipOn(*resizingAudio_)) {
            auto maxLength = project_->playbackEndTick() - clip->startTick;
            if (!clip->timing.loop && !clip->timing.cycleTicks)
                maxLength = std::min<juce::int64>(maxLength, std::max<juce::int64>(1, std::ceil(waveforms_.nativeTicks(*clip, project_->clock))));
            previewAudioLength_ = std::clamp<juce::int64>(arrangementTickAt(event.x, true) - clip->startTick, 1, maxLength);
            repaint(trackRows_[*resizingAudio_]);
        }
        return;
    }
    if (!pendingMidiDrag_ || event.getDistanceFromDragStart() < 5) return;
    const auto track = *pendingMidiDrag_;
    pendingMidiDrag_.reset();
    const auto error = MidiDragButton::dragMidi(trackMidiFile_.getFile(), this,
        [this, track](juce::OutputStream& stream) { return writeTrackMidi(track, stream); });
    if (error.isNotEmpty()) statusLabel_.setText(error, juce::dontSendNotification);
}

void MainComponent::mouseUp(const juce::MouseEvent& event) {
    if (viewportPan_.end(*this)) return;
    if (!resizingAudio_) return;
    auto* clip = clipOn(*resizingAudio_);
    resizingAudio_.reset();
    if (!clip || recordTrack_ || !event.mouseWasDraggedSinceMouseDown()) return;
    const bool playing = transportActive_;
    if (playing) stopTransport();
    clip->durationTicks = previewAudioLength_;
    markSessionEdited();
    if (!clip->timing.loop && clip->timing.cycleTicks) clip->timing.cycleTicks = previewAudioLength_;
    finishNoteEdit(playing);
}

void MainComponent::selectTrack(std::size_t index) {
    if (recordTrack_ && *recordTrack_ != index) return;
    if (!project_ || index >= project_->trackLanes.size()) return;
    selectedGlobal_.reset();
    editorVisible_ = true;
    if (selectedTrack_ != index) clipLane_.selected.reset();
    selectedTrack_ = index;
    trackPicker_.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification);
    mixerSelected_ = false;
    refreshEditor();
}

void MainComponent::refreshPianoNotes() {
    const bool changed = pianoGeneration_ != projectGeneration_ || pianoTrack_ != selectedTrack_;
    if (changed) inspectedNote_.reset();
    if (pianoGeneration_ != projectGeneration_) { noteHistory_.clear(); noteHistoryCursor_ = 0; }
    pianoGeneration_ = projectGeneration_;
    pianoTrack_ = selectedTrack_;
    auto& grid = pianoGrid_;
    grid.notes.clear();
    grid.kitPitches.clear();
    pianoMaxEnds_.clear();
    grid.editable = false;
    if (!project_ || !selectedTrack_) { grid.resetRange(false); return; }
    const auto track = *selectedTrack_;
    grid.notes = project_->pitchedNotes[track];
    const auto& drums = project_->drumNotes[track];
    grid.notes.insert(grid.notes.end(), drums.begin(), drums.end());
    std::stable_sort(grid.notes.begin(), grid.notes.end(), [](const auto& a, const auto& b) { return a.startTick < b.startTick; });
    juce::int64 maxEnd = 0;
    for (const auto& note : grid.notes) {
        maxEnd = std::max(maxEnd, note.endTick);
        pianoMaxEnds_.push_back(maxEnd);
    }
    if (project_->trackKinds[track] == ostfstudio::TrackKind::drum) {
        std::set<int> mapped;
        for (const auto &s : project_->slotsByTrack[track])
            if (s.pitch >= 0)
                mapped.insert(s.pitch);
        for (const auto &n : grid.notes)
            if (n.pitch >= 0)
                mapped.insert(n.pitch);
        grid.kitPitches.assign(mapped.begin(), mapped.end());
    }
    grid.editable = track < project_->sourceTrackCount() && project_->trackKinds[track] != ostfstudio::TrackKind::audio;
    grid.setTooltip(grid.editable ? TRANS("Double-click to add a note. Drag its body to move or either edge to resize.")
                                  : TRANS("Read only"));
    grid.endTick = project_->playbackEndTick();
    grid.quarter = project_->ppq;
    if (changed) {
        grid.resetRange(project_->trackKinds[track] == ostfstudio::TrackKind::drum);
        if (pitchZoom_.getSelectedId() > 1)
            grid.setPitchRows(pitchZoom_.getSelectedId());
        pianoVelocity_.cancel();
    }
    pianoSnap_.onChange();
}

bool MainComponent::pitchedSelection() const {
    return project_ && selectedTrack_ && *selectedTrack_ < project_->trackKinds.size()
        && project_->trackKinds[*selectedTrack_] == ostfstudio::TrackKind::pitched;
}

void MainComponent::refreshTheory() {
    if ((!theoryVisible_ && pianoColours_.getSelectedId() == plainNotes) || !project_) { theory_.reset(); theoryTracks_.clear(); return; }
    if (!theory_) {
        theory_ = std::make_unique<ostfstudio::TheoryGuide>(juce::JSON::parse(project_->sourceScore.toString()),
                                                            project_->playbackEndTick());
    }
    theoryTracks_.resize(project_->trackKinds.size());
    for (std::size_t i = 0; i < theoryTracks_.size(); ++i) refreshTrackTheory(i);
}

void MainComponent::refreshTrackTheory(std::size_t track) {
    if (theory_ && track < theoryTracks_.size() && project_->trackKinds[track] == ostfstudio::TrackKind::pitched)
        theoryTracks_[track] = theory_->analyse(track, project_->pitchedNotes[track], scaleReference_);
}

int MainComponent::theoryTrackHeight(std::size_t track) const {
    if (!theoryVisible_ || track >= theoryTracks_.size() || (!chordFitVisible_ && !scaleFitVisible_)) return 0;
    if (track >= trackControls_.size() || !project_ || project_->trackKinds[track] == ostfstudio::TrackKind::audio) return 0;
    constexpr int fitLaneHeight = 16;
    if (theoryTracks_[track].empty()) return fitLaneHeight;
    return fitLaneHeight * (int(chordFitVisible_) + int(scaleFitVisible_));
}

juce::String MainComponent::getTooltip() {
    if (!project_ || workspaceMode_ != WorkspaceMode::arrangement) return {};
    const auto p = getMouseXYRelative();
    if (pianoGrid_.isVisible() && pianoGrid_.getWidth() > 0) {
        const auto strip = pianoGrid_.getBounds().withTop(pianoRuler_.getBottom()).withBottom(pianoGrid_.getY());
        if (strip.contains(p)) {
            const auto range = pianoGrid_.tickRange();
            const auto tick = static_cast<juce::int64>(std::floor(range.getStart() + (p.x-strip.getX()) * range.getLength() / strip.getWidth()));
            if (pianoColours_.getSelectedId() == plainNotes) return {};
            if (pianoColours_.getSelectedId() == scaleNotes && theory_ && pitchedSelection()) {
                const auto i = theory_->firstCell(double(tick));
                if (i >= theoryTracks_[*selectedTrack_].size()) return {};
                const auto& ref = theoryTracks_[*selectedTrack_][i];
                return scaleContextName(*theory_, ref, auditionShift_);
            }
            return ostfstudio::shiftedLabel(project_->contextAt(TimelineLane::chord, tick), auditionShift_);
        }
    }
    if (!rowOverlay_.getBounds().contains(p)) return {};
    if (theoryVisible_ && theory_ && functionRow_.contains(p)) {
        const auto range = horizontalRange();
        const double tick = (range.start - 1 + double(p.x - functionRow_.getX()) / functionRow_.getWidth()
                             * (range.end - range.start)) * project_->ticksPerBar;
        const auto& functions = theory_->functions;
        auto it = std::partition_point(functions.begin(), functions.end(), [tick](const auto& f) { return f.end <= tick; });
        if (it != functions.end() && it->start <= tick) {
            auto name = ostfstudio::shiftedLabel(theory_->chords[it->chord].name, auditionShift_) + "  " + it->degree + "  " + juce::translate(it->function);
            const auto next = std::next(it);
            if (next != functions.end() && next->start == it->end && next->key == it->key && it->function == "Secondary dominant")
                name += "  " + juce::String::charToString(0x2192) + "  " + ostfstudio::shiftedLabel(theory_->chords[next->chord].name, auditionShift_);
            const auto& groups = theory_->progressions;
            const auto group = std::partition_point(groups.begin(), groups.end(), [tick](const auto& p) { return p.end <= tick; });
            if (group != groups.end() && group->start <= tick) name += "   " + group->label;
            return name;
        }
    }
    for (std::size_t i = 0; i < trackRows_.size(); ++i) {
        if (trackRows_[i].withWidth(trackNameWidth).contains(p) && !trackRows_[i].isEmpty())
            return juce::String::fromUTF8(project_->trackLanes[i].label) + "  /  " +
                (project_->trackKinds[i] == ostfstudio::TrackKind::drum ? TRANS("Drum / percussion") :
                 project_->trackKinds[i] == ostfstudio::TrackKind::audio ? TRANS("Audio track") : TRANS("Pitched track"));
        const auto row = trackRows_[i].withTrimmedLeft(trackHeaderWidth);
        if (row.isEmpty()) continue;
        const auto lanes = row.withY(row.getBottom() + TrackRowSize::grip).withHeight(theoryTrackHeight(i));
        if (!lanes.contains(p)) continue;
        if (theoryTracks_[i].empty()) return TRANS("Pitch analysis requires pitched notes.");
        const auto range = horizontalRange();
        const auto bar = range.start + double(p.x - row.getX()) / row.getWidth() * (range.end - range.start);
        const auto c = theory_->firstCell((bar - 1) * project_->ticksPerBar);
        if (c >= theory_->cells.size()) return {};
        const auto& context = theory_->cells[c];
        const auto& fit = theoryTracks_[i][c];
        juce::StringArray lines;
        if (chordFitVisible_) {
            auto line = TRANS("Chord fit");
            if (context.chord >= 0) line += " / " + ostfstudio::shiftedLabel(theory_->chords[context.chord].name, auditionShift_);
            lines.add(line + " " + (fit.chord.total ? juce::String(fit.chord.percent()) + "%" : juce::String::charToString(0x2014)));
        }
        if (scaleFitVisible_) {
            auto line = TRANS("Scale fit");
            if (fit.backgroundScale >= 0) line += " / " + ostfstudio::shiftedLabel(theory_->scales[fit.backgroundScale].name, auditionShift_);
            else if (fit.backgroundScale == -2) line += " / " + TRANS("Select scale reference");
            lines.add(line + " " + (fit.scale.total ? juce::String(fit.scale.percent()) + "%" : juce::String::charToString(0x2014)));
        }
        return lines.joinIntoString("\n");
    }
    return {};
}

void MainComponent::previewNoteFit(const std::vector<ostfstudio::NoteEdit>& edits) {
    if (edits.empty()) {
        if (draggedNote_) { draggedNote_.reset(); previewFits_.clear(); pianoGrid_.setDescription(noteFitReading()); repaint(); }
        return;
    }
    if (!theory_ || !pitchedSelection()) return;
    draggedNote_ = edits.front().note;
    if (edits.front().action == ostfstudio::NoteEdit::duplicate) draggedNote_->id.clear();
    const auto track = *selectedTrack_;
    auto notes = project_->pitchedNotes[track];
    std::map<juce::String, const ostfstudio::PitchedNote*> replacements;
    for (const auto& e : edits) {
        if (e.action == ostfstudio::NoteEdit::duplicate) { notes.push_back(e.note); notes.back().id.clear(); }
        else replacements[e.note.id] = &e.note;
    }
    for (auto& note : notes) if (auto it = replacements.find(note.id); it != replacements.end()) note = *it->second;
    previewFits_ = theory_->analyse(track, notes, scaleReference_);
    pianoGrid_.setDescription(noteFitReading());
    repaint();
}

juce::String MainComponent::noteFitReading() const {
    const auto& note = draggedNote_ ? draggedNote_ : inspectedNote_;
    if (!theory_ || !pitchedSelection() || !note || note->fixedTrigger || !note->enabled
        || *selectedTrack_ >= theoryTracks_.size()) return {};
    const auto& fits = draggedNote_ ? previewFits_ : theoryTracks_[*selectedTrack_];
    ostfstudio::TheoryGuide::Fit chord, scale;
    for (auto i = theory_->firstCell(double(note->startTick)); i < fits.size() && theory_->cells[i].start < note->endTick; ++i) {
        const auto& cell = theory_->cells[i];
        const auto duration = std::min(note->endTick, cell.end) - std::max(note->startTick, cell.start);
        if (const int mask = theory_->chordMask(cell)) chord.add(theory_->contains(mask, note->pitch), duration);
        const auto s = theory_->scaleForNote(fits[i], *note);
        if (s >= 0 && theory_->scales[s].mask) scale.add(theory_->contains(theory_->scales[s].mask, note->pitch), duration);
    }
    auto percent = [](const auto& fit) { return fit.total == 0 ? juce::String::charToString(0x2014) : juce::String(fit.percent()) + "%"; };
    return TRANS("Selected note") + "   " + TRANS("Chord tone fit") + " " + percent(chord) + "   " + TRANS("Scale fit") + " " + percent(scale);
}

void MainComponent::rememberScoreEdit(juce::MemoryBlock before) {
    while (noteHistory_.size() > noteHistoryCursor_) noteHistory_.pop_back();
    if (!noteHistory_.empty() && noteHistory_.back().after != before) noteHistory_.clear();
    noteHistory_.push_back({std::move(before), project_->sourceScore});
    std::size_t bytes = 0;
    for (const auto& revision : noteHistory_) bytes += revision.before.getSize() + revision.after.getSize();
    while (noteHistory_.size() > 1 && (noteHistory_.size() > 64 || bytes > 32 * 1024 * 1024)) {
        bytes -= noteHistory_.front().before.getSize() + noteHistory_.front().after.getSize();
        noteHistory_.pop_front();
    }
    noteHistoryCursor_ = noteHistory_.size();
    refreshWindowTitle();
}

void MainComponent::finishNoteEdit(bool playing) {
    if ((auditionShift_ || auditionRate_ != 1) && project_->checkAudition(auditionShift_, auditionRate_).failed()) {
        auditionShift_ = 0; auditionRate_ = 1; updateDocumentLabel();
    }
    auditionClock_ = auditionRate_ == 1 ? std::nullopt : project_->clock.scaled(auditionRate_);
    refreshTimelineLanes();
    if (arrangementScroll_.getMaximumRangeLimit() != project_->bars.endBar) {
        updateHorizontalRange();
    }
    refreshEditor();
    displayTransportPosition(std::clamp<juce::int64>(cursorTick_, 0, project_->playbackEndTick()));
    if (playing) startTransport(false);
}

void MainComponent::applyNoteGesture(const std::vector<ostfstudio::NoteEdit>& edits) {
    if (!project_ || !selectedTrack_ || edits.empty()) return;
    if (recordTrack_) { statusLabel_.setText(TRANS("Finish recording before editing notes."), juce::dontSendNotification); return; }
    const bool playing = transportActive_;
    const bool liveLoop = playing && drumSteps_.looping() && !activeLoop_.isEmpty();
    if (playing && !liveLoop) stopTransport();
    juce::MemoryBlock before;
    juce::StringArray ids;
    const auto result = project_->editNotes(*selectedTrack_, edits, before, ids);
    if (result.failed()) {
        if (playing && !liveLoop) startTransport(false);
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return;
    }
    rememberScoreEdit(std::move(before));
    inspectedNote_.reset();
    if (!ids.isEmpty()) for (const auto& n : project_->pitchedNotes[*selectedTrack_])
        if (n.id == ids[0]) { inspectedNote_ = n; break; }
    pianoGrid_.selection = std::move(ids);
    if (theory_ && (theory_->cells.empty() || theory_->cells.back().end != project_->playbackEndTick())) {
        theory_.reset();
        refreshTheory();
    } else refreshTrackTheory(*selectedTrack_);
    if (!drumSteps_.isVisible() && !edits.empty() && edits.front().action != ostfstudio::NoteEdit::erase) {
        const int p = edits.front().note.pitch;
        const int shift = p < pianoGrid_.low ? p - pianoGrid_.low : std::max(0, p - pianoGrid_.high);
        pianoGrid_.low += shift; pianoGrid_.high += shift;
    }
    if (liveLoop) {
        if (const auto result = transport_.updateLoopNotes(*project_); result.failed()) {
            stopTransport();
            statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
        }
    }
    finishNoteEdit(playing && !liveLoop);
}

void MainComponent::undoPianoEdit(bool redo) {
    if (!project_ || recordTrack_ || (redo ? noteHistoryCursor_ >= noteHistory_.size() : noteHistoryCursor_ == 0)) return;
    const auto& revision = noteHistory_[redo ? noteHistoryCursor_ : noteHistoryCursor_ - 1];
    if ((!project_->addedClips.empty() && revision.panTrack.isEmpty()) || project_->sourceScore != (redo ? revision.before : revision.after)) {
        noteHistory_.clear(); noteHistoryCursor_ = 0; return;
    }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    if (revision.panTrack.isNotEmpty()) {
        const auto env = redo ? revision.newPan : revision.oldPan;
        if (env.points.empty()) {
            project_->panAutomation.erase(revision.panTrack);
            const int index = project_->trackIds.indexOf(revision.panTrack);
            if (index >= 0) mixer_.setPan(index,mixer_.channel(index).pan);
        } else project_->panAutomation[revision.panTrack]=env;
        if (redo) ++noteHistoryCursor_; else --noteHistoryCursor_;
        finishNoteEdit(playing); return;
    }
    const auto result = project_->restoreNoteScore(redo ? revision.after : revision.before);
    if (result.wasOk()) {
        if (redo) ++noteHistoryCursor_; else --noteHistoryCursor_;
        inspectedNote_.reset();
        pianoGrid_.selection.clear();
        theory_.reset();
        refreshTheory();
        updateDocumentLabel();
        finishNoteEdit(playing);
    } else {
        if (playing) startTransport(false);
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
    }
}

void MainComponent::setWorkspaceMode(WorkspaceMode mode) {
    if (mode != WorkspaceMode::arrangement && !project_) return;
    if (workspaceMode_ == mode) return;
    workspaceMode_ = mode;
    refreshEditor();
}

void MainComponent::updateDrumLoop() {
    if (!transportActive_ || recordTrack_ || !transport_.isRunning()) return;
    const auto range = drumSteps_.looping() ? drumSteps_.range() : cycleButton_.getToggleState() && !drumSteps_.isVisible()
        ? editorCycle_ : juce::Range<juce::int64>{};
    if (range == activeLoop_) return;
    activeLoop_ = range;
    transport_.setLoop(range);
    if (!range.isEmpty() && !range.contains(transport_.positionTick())) transport_.seekToTick(range.getStart());
}

void MainComponent::refreshControllerLane() {
    auto& lane = controllerEditor_;
    lane.cancel();
    lane.points.clear();
    const auto label = performanceLaneNames_[performanceLane_.getSelectedId() - 1];
    const bool pan = label == "Pan automation";
    const bool editable = performanceLane_.isVisible() && (pan || controllerKeys_.contains(label));
    lane.setVisible(editable);
    if (!editable || !selectedTrack_ || !project_) return;
    if (pan) {
        lane.linear = true;
        lane.bipolar = true;
        lane.maximum = 0;
        const auto it = project_->panAutomation.find(project_->trackIds[int(*selectedTrack_)]);
        if (it != project_->panAutomation.end()) {
            lane.linear = it->second.linear;
            for (const auto& p : it->second.points)
                lane.points.push_back({p.tick, (p.value + 1) / 2, juce::String(p.tick)});
        }
    } else {
        lane.maximum = controllerKeys_.at(label).maximum();
        lane.bipolar = lane.maximum == 16383;
        lane.linear = false;
        for (const auto& c : project_->performanceCurves[*selectedTrack_])
            if (c.label == label) lane.points.insert(lane.points.end(), c.points.begin(), c.points.end());
        std::stable_sort(lane.points.begin(), lane.points.end(),
                         [](const auto& a, const auto& b) { return a.tick < b.tick; });
    }
    lane.repaint();
}

void MainComponent::editPerformancePoint(juce::String id, juce::int64 tick, std::optional<double> value) {
    if (!project_ || !selectedTrack_ || recordTrack_) return;
    const auto label = performanceLaneNames_[performanceLane_.getSelectedId() - 1];
    const bool playing = transportActive_;
    if (playing) stopTransport();
    const auto track = *selectedTrack_;
    if (label == "Pan automation") {
        const auto key = project_->trackIds[int(track)];
        const auto it = project_->panAutomation.find(key);
        const auto old = it == project_->panAutomation.end() ? ostfstudio::PanEnvelope{} : it->second;
        auto env = old;
        if (env.id.isEmpty()) env.id = "pan-" + juce::Uuid().toString();
        std::erase_if(env.points, [&](const auto& p) {
            return (id.isNotEmpty() && p.tick == id.getLargeIntValue()) || (value && p.tick == tick);
        });
        if (value) env.points.push_back({tick, *value * 2 - 1});
        std::sort(env.points.begin(), env.points.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
        if (env.points.empty()) {
            project_->panAutomation.erase(key);
            mixer_.setPan(track, mixer_.channel(track).pan);
        } else
            project_->panAutomation[key] = env;
        rememberScoreEdit(project_->sourceScore);
        markSessionEdited();
        auto& revision = noteHistory_.back();
        revision.panTrack = key;
        revision.oldPan = old;
        revision.newPan = std::move(env);
    } else if (auto k = controllerKeys_.find(label); k != controllerKeys_.end()) {
        juce::MemoryBlock before;
        if (id.isEmpty() && value)
            for (const auto& p : controllerEditor_.points)
                if (p.tick == tick) {
                    id = p.id;
                    break;
                }
        auto result = project_->editController(
            track, k->second, id, tick,
            value ? std::optional<int>(juce::roundToInt(*value * k->second.maximum())) : std::nullopt, before);
        if (result.failed()) {
            statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
            if (playing) startTransport(false);
            return;
        }
        rememberScoreEdit(std::move(before));
    }
    finishNoteEdit(playing);
}

void MainComponent::refreshEditor() {
    refreshTrackActions();
    const bool arranging = workspaceMode_ == WorkspaceMode::arrangement;
    const bool reading = project_ && !arranging;
    if (reading && !scoreView_) {
        scoreView_ = std::make_unique<ScoreView>();
        addChildComponent(*scoreView_);
    }
    if (scoreView_) {
        if (reading) {
            const bool lead = workspaceMode_ == WorkspaceMode::leadSheet;
            if (!scoreView_->isVisible() || lead != leadButton_.getToggleState())
                scoreView_->showScore(*project_, lead, selectedTrack_);
            else
                scoreView_->refresh(*project_);
            scoreView_->playbackPosition(cursorTick_);
        }
        scoreView_->setVisible(reading);
    }
    arrangementButton_.setToggleState(arranging, juce::dontSendNotification);
    leadButton_.setToggleState(reading && workspaceMode_ == WorkspaceMode::leadSheet, juce::dontSendNotification);
    scoreButton_.setToggleState(reading && workspaceMode_ == WorkspaceMode::score, juce::dontSendNotification);
    const bool showMixer = arranging && mixerSelected_;
    deck_.follow.setEnabled(arranging && project_ != nullptr);
    mixerButton_.setToggleState(showMixer, juce::dontSendNotification);
    const bool editing = arranging && project_ && editorVisible_ && !showMixer;
    const bool tempo = editing && selectedGlobal_ == TimelineLane::tempo;
    tempoEditor_.setVisible(tempo);
    if (tempo) tempoEditor_.bind(project_.get());
    deck_.record.setEnabled(project_ && (armedId_.isNotEmpty() || (selectedTrack_ && selectedGlobal_ != TimelineLane::tempo)));
    const bool audio = editing && !tempo && selectedTrack_ && project_->trackKinds[*selectedTrack_] == ostfstudio::TrackKind::audio;
    audioEditor_.setVisible(audio);
    if (audio) {
        audioEditor_.bind(project_->ppq, project_->ticksPerBar, project_->bars.firstBar, !recordTrack_.has_value());
        const auto it = captureChannels_.find(project_->trackIds[int(*selectedTrack_)]);
        audioEditor_.channels.setSelectedId(it == captureChannels_.end() ? 1 : it->second, juce::dontSendNotification);
    }
    const bool piano = editing && !audio && !tempo;
    refreshPianoNotes();
    pianoGrid_.setDescription(noteFitReading());
    const bool drum = piano && selectedTrack_ && pianoGrid_.percussion;
    {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        drumInputMonitor_ = drum ? selectedTrack_ : std::nullopt;
    }
    drumMode_.setVisible(drum);
    const bool steps = drum && drumMode_.getSelectedId()==2;
    drumSteps_.bind(drum ? project_.get() : nullptr,selectedTrack_.value_or(0),std::max<juce::int64>(0,cursorTick_),pianoGrid_.grid);
    drumSteps_.setVisible(steps);
    cycleButton_.setVisible(editing && !tempo && selectedTrack_ && !steps);
    cycleButton_.setEnabled(!recordTrack_);
    editorRecord_.setVisible(editing && !tempo && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
    editorRecord_.setButtonText(recordTrack_ ? TRANS("Stop recording") : TRANS("Record"));
    editorRecord_.setToggleState(recordTrack_.has_value(), juce::dontSendNotification);
    drumPads_.setVisible(drum);
    const auto kitId = drum ? project_->trackIds[int(*selectedTrack_)] : juce::String{};
    if (drum)
        drumPads_.bind(project_->slotsByTrack[*selectedTrack_], steps, padTrack_ != kitId, !recordTrack_);
    if (kitId != padTrack_) cancelPadLearn();
    padTrack_ = kitId;
    refreshController();
    attachPadInput(project_ != nullptr || surfaceMap_.port.isNotEmpty() || !otherSurfaces_.empty());
    updateDrumLoop();
    pianoGrid_.setVisible(piano && selectedTrack_.has_value() && !steps);
    pianoRuler_.setVisible(pianoGrid_.isVisible());
    paneControl_.setVisible(pianoGrid_.isVisible() || drumSteps_.isVisible() || audioEditor_.isVisible());
    refreshControlButtons();
    refreshClipLane();
    clipLane_.setVisible(pianoGrid_.isVisible() || audioEditor_.isVisible());
    if (!pianoGrid_.isVisible()) pianoVelocity_.setVisible(false);
    pianoSnap_.setVisible(piano && pianoGrid_.editable);
    pianoColours_.setVisible(piano && pitchedSelection());
    pianoButton_.setToggleState(editing, juce::dontSendNotification);
    outputButton_.setToggleState(arranging && showOutputTracks_, juce::dontSendNotification);
    for (auto* button : {&pianoButton_, &mixerButton_, &leadButton_, &scoreButton_}) button->setEnabled(project_ != nullptr);
    outputButton_.setEnabled(arranging && project_ != nullptr);
    songInfoButton_.setEnabled(project_ != nullptr);
    musicButton_.setEnabled(project_ != nullptr);
    basics_.setEnabled(project_ && !recordTrack_);
    theoryButton_.setEnabled(arranging && project_ != nullptr);
    theoryButton_.setToggleState(theoryVisible_ && arranging, juce::dontSendNotification);
    lanesButton_.setEnabled(arranging);
    trackPicker_.setVisible(arranging && project_ && editorVisible_ && !tempo);
    trackPicker_.setEnabled(!recordTrack_ && project_ && !project_->trackLanes.empty());
    performanceLane_.setVisible(piano && selectedTrack_ && !steps);
    const auto previous = performanceLaneNames_[performanceLane_.getSelectedId() - 1];
    performanceLaneNames_ = juce::StringArray{"Velocity"};
    if (project_ && selectedTrack_ && *selectedTrack_ < project_->performanceCurves.size())
        for (const auto& curve : project_->performanceCurves[*selectedTrack_])
            performanceLaneNames_.addIfNotAlreadyThere(curve.label);
    performanceLane_.clear(juce::dontSendNotification);
    for (int i = 0; i < performanceLaneNames_.size(); ++i) performanceLane_.addItem(juce::translate(performanceLaneNames_[i]), i + 1);
    controllerKeys_.clear();
    if (project_ && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount()) {
        if (pianoGrid_.editable) {
            for (const auto& c : project_->performanceCurves[*selectedTrack_])
                if (c.control.type.isNotEmpty()) controllerKeys_[c.label]=c.control;
            const auto add = [&](ostfstudio::MidiControl key, juce::PopupMenu* menu=nullptr) {
                const auto text=key.label(); controllerKeys_[text]=key;
                if (performanceLaneNames_.contains(text)) return;
                performanceLaneNames_.add(text);
                if (menu) menu->addItem(performanceLaneNames_.size(),text);
                else performanceLane_.addItem(juce::translate(text),performanceLaneNames_.size());
            };
            add({"programChange"}); add({"controlChange",0}); add({"controlChange",32});
            add({"pitchBend"}); add({"channelPressure"}); add({"controlChange",7}); add({"controlChange",1}); add({"controlChange",11});
            for (int group=0; group<120; group+=16) {
                juce::PopupMenu groupMenu;
                for (int cc=group; cc<std::min(120,group+16); ++cc) add({"controlChange",cc},&groupMenu);
                if(groupMenu.getNumItems()) performanceLane_.getRootMenu()->addSubMenu("CC "+juce::String(group)+"-"+juce::String(std::min(119,group+15)),groupMenu);
            }
        }
        performanceLaneNames_.add("Pan automation");performanceLane_.addItem(TRANS("Pan automation"),performanceLaneNames_.size());
    }
    performanceLane_.setSelectedId(std::max(1, performanceLaneNames_.indexOf(previous) + 1), juce::dontSendNotification);
    refreshControllerLane();
    arrangementZoom_.setVisible(arranging && project_ != nullptr);
    arrangementZoomLabel_.setVisible(arrangementZoom_.isVisible());
    heightLabel_.setVisible(arrangementZoom_.isVisible());
    heightScope_.setVisible(arrangementZoom_.isVisible());
    groupHeight_.setVisible(arrangementZoom_.isVisible());
    refreshGroupHeight();
    arrangementScroll_.setVisible(arranging && project_ != nullptr);
    pianoInspectorView_.setVisible(piano && pianoGrid_.editable && !steps);
    pianoDivider_.setVisible(editing);
    parameterDivider_.setVisible(piano && !steps);
    pitchZoom_.setVisible(piano && !drum);
    columnDivider_.setVisible(piano || audio);
    arrangementColumnDivider_.setVisible(arranging && project_ != nullptr);
    midiDrag_.setVisible(piano);
    midiDrag_.setEnabled(selectedTrack_ && canExportTrack(*selectedTrack_));
    mixer_.setVisible(arranging && project_ != nullptr && showMixer);
    resized();
    repaint();
}

bool MainComponent::canExportTrack(std::size_t track) const {
    return project_ && track < project_->pitchedNotes.size()
        && (!project_->pitchedNotes[track].empty() || (track < project_->sourceTrackCount()
            && (!project_->drumNotes[track].empty() || std::any_of(project_->performanceCurves[track].begin(),project_->performanceCurves[track].end(),
                [](const auto& c){return c.control.type.isNotEmpty() && !c.points.empty();}))))
        && project_->ppq > 0 && project_->ppq <= 32767;
}

bool MainComponent::writeTrackMidi(std::size_t track, juce::OutputStream& stream) const {
    if (!canExportTrack(track)) return false;
    if (track < project_->sourceTrackCount())
        return ostfstudio::writeSongMidi(*project_, ostfstudio::SmfFormat::multitrack, {}, stream, track).wasOk();
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
    const int barHeight = viewBarHeight(getWidth());
    menuBar_.setBounds(getLocalBounds().removeFromTop(menuHeight));
    auto viewBar = getLocalBounds().withTrimmedTop(menuHeight).removeFromTop(barHeight).reduced(8, 0);
    auto row = viewBar.removeFromTop(32);
    const int width = std::clamp(getWidth() / 24, 24, 39);
    const auto place = [&](ViewButton& button) { button.setBounds(row.removeFromLeft(width).reduced(2, 2)); };
    for (auto* button : {&arrangementButton_, &leadButton_, &scoreButton_, &pianoButton_, &mixerButton_,
                         &pluginBrowserButton_, &outputButton_, &lanesButton_, &theoryButton_, &songInfoButton_, &musicButton_}) place(*button);
    if (barHeight > 32) { toolbarHint_.setBounds(row); row = viewBar; }
    row.removeFromLeft(8);
    for (auto* button : {&melodyAdd_, &chorusAdd_, &pitchedAdd_, &drumsAdd_, &audioAdd_, &busAdd_, &returnAdd_}) place(*button);
    row.removeFromLeft(8);
    place(uiSmaller_); place(uiLarger_);
    if (barHeight == 32) toolbarHint_.setBounds(row.withTrimmedRight(8));
    deck_.setBounds(18, menuHeight + barHeight + 8, std::min(1590, getWidth() - 36),
                    TransportPanel::panelHeight(getWidth()));
    statusLabel_.setBounds(20, getHeight() - 37, getWidth() - 40, 24);
    auto editor = getWorkspace();
    trackHeaderWidth = std::min(preferredHeaderWidth_, std::max(320, editor.getWidth() - 145));
    trackNameWidth = std::clamp(trackHeaderWidth - 315, 70, 300);
    ruler_.headerWidth = trackHeaderWidth;
    basics_.setVisible(project_ && workspaceMode_ == WorkspaceMode::arrangement);
    if (pluginBrowser_) {
        const auto full = workspaceBounds(getLocalBounds(), topBasicsHeight());
        browserDivider_.setVisible(pluginBrowser_->isVisible());
        browserDivider_.setBounds(full.withX(editor.getRight()).withWidth(columnGripWidth));
        pluginBrowser_->setBounds(full.withLeft(browserDivider_.getRight()));
    }
    if (workspaceMode_ != WorkspaceMode::arrangement) {
        rowOverlay_.setVisible(false);
        verticalScroll_.setVisible(false);
        if (scoreView_) scoreView_->setBounds(editor);
        return;
    }
    const auto arrangerHeight = getArrangementHeight(editor.getHeight());
    auto arrangement = editor.withHeight(arrangerHeight).reduced(13);
    if (project_) {
        const bool stacked = arrangement.getWidth() < 765;
        auto navigation = arrangement.removeFromBottom(timelineNavigationHeight * (stacked ? 2 : 1));
        auto heights = (stacked ? navigation.removeFromTop(timelineNavigationHeight) : navigation.removeFromLeft(362)).reduced(0, 2);
        heightLabel_.setBounds(heights.removeFromLeft(48));
        heightScope_.setBounds(heights.removeFromLeft(182));
        groupHeight_.setBounds(heights.removeFromLeft(128));
        navigation.reduce(0, 2);
        arrangementZoomLabel_.setBounds(navigation.removeFromLeft(44));
        arrangementZoom_.setBounds(navigation.removeFromLeft(162));
        arrangementScroll_.setBounds(navigation.withTrimmedLeft(8).reduced(0, 3));
    }
    const auto outputs = visibleOutputCount();
    for (std::size_t i = 0; i < outputControls_.size(); ++i) {
        outputControls_[i].size.setVisible(outputs && mixer_.routing().hasOutput(i));
        outputControls_[i].meter.setVisible(outputs && mixer_.routing().hasOutput(i));
        outputControls_[i].effects.button.setVisible(outputs && mixer_.routing().hasOutput(i));
        outputControls_[i].volume.setVisible(outputs && mixer_.routing().hasOutput(i));
        outputControls_[i].mute.setVisible(outputs && mixer_.routing().hasOutput(i));
    }
    rowOverlay_.setVisible(project_ != nullptr);
    if (!project_) verticalScroll_.setVisible(false);
    if (project_) {
        const auto view = currentArrangement();
        const auto order = displayedTrackOrder();
        const int globalHeight = timelineRowHeight() + (theoryVisible_ ? (2 * timelineRowHeight()) : 0) + static_cast<int>(view.globalLanes.size())
            * groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)];
        int contentHeight = globalHeight + (outputs ? arrangementHeadingHeight : 0);
        if (std::any_of(order.begin(), order.end(), [this](auto i) { return !project_->isGlobalNoteTrack(i); }))
            contentHeight += arrangementHeadingHeight;
        for (auto i : order) contentHeight += trackControls_[i]->size.height() + theoryTrackHeight(i);
        for (std::size_t i = 0; i < outputs; ++i) contentHeight += outputControls_[mixer_.routing().outputAt(i)].size.height();
        const auto viewportHeight = std::max(0, arrangement.getHeight());
        verticalScroll_.setRangeLimits(0, std::max(viewportHeight, contentHeight), juce::dontSendNotification);
        verticalScroll_.setCurrentRange(verticalScroll_.getCurrentRangeStart(), viewportHeight, juce::dontSendNotification);
        verticalScroll_.setVisible(contentHeight > viewportHeight);
        if (verticalScroll_.isVisible()) verticalScroll_.setBounds(arrangement.removeFromRight(14).withTrimmedLeft(2));
        rowOverlay_.setBounds(arrangement);
        arrangementColumnDivider_.setBounds(arrangement.withX(arrangement.getX() + trackHeaderWidth - columnGripWidth)
                                                      .withWidth(columnGripWidth));
        arrangementColumnDivider_.toFront(false);
        importTrackDrop_.setBounds(arrangement.withTop(std::max(arrangement.getY(), arrangement.getBottom() - 36)).reduced(4, 2));
        ruler_.setBounds(rowOverlay_.getLocalBounds().removeFromTop(timelineRowHeight()));
        ruler_.toFront(false);
        const int panelWidth = std::clamp(trackHeaderWidth - 120, 150, 460);
        const bool columns = panelWidth >= 282;
        const int panelHeight = columns ? 88 : (activeAi_ ? 113 : 84);
        const int aiY = std::max(0, (globalHeight - panelHeight) / 2) - juce::roundToInt(verticalScroll_.getCurrentRangeStart());
        const int visibleBottom = std::min(globalHeight, rowOverlay_.getHeight());
        auto placeAi = [visibleBottom](juce::Component& item, int x, int y, int w, int h, bool show = true) {
            item.setBounds(x, y, w, h);
            item.setVisible(show && y >= 0 && y + h <= visibleBottom);
            item.toFront(false);
        };
        const int gap = 4;
        if (columns) {
            const int modes = std::clamp(panelWidth * 42 / 100, 134, 164);
            const int right = 8 + modes + gap, settings = panelWidth - modes - gap;
            for (int i = 0; i < 3; ++i) placeAi(aiStudios_[i], 8, aiY + 30 * i, modes, 26);
            placeAi(aiButton_, right, aiY, settings, 27);
            placeAi(modelPicker_, right, aiY + 30, settings, 26, activeAi_ != nullptr);
            placeAi(fetchModels_, right, aiY + 60, settings, 26, activeAi_ != nullptr);
        } else {
            placeAi(aiButton_, 8, aiY, panelWidth, 25);
            placeAi(aiStudios_[0], 8, aiY + 29, panelWidth, 23);
            const int half = (panelWidth - gap) / 2;
            placeAi(aiStudios_[1], 8, aiY + 55, half, 23);
            placeAi(aiStudios_[2], 8 + half + gap, aiY + 55, panelWidth - half - gap, 23);
            placeAi(modelPicker_, 8, aiY + 82, panelWidth - 84, 24, activeAi_ != nullptr);
            placeAi(fetchModels_, 8 + panelWidth - 80, aiY + 82, 80, 24, activeAi_ != nullptr);
        }

        const bool besideTransport = topBasicsHeight() == 0;
        basics_.setBounds(besideTransport
            ? juce::Rectangle<int>(deck_.getRight() + 8, deck_.getBounds().getCentreY() - SongBasicsPanel::stripHeight / 2,
                                   getWidth() - deck_.getRight() - 26, SongBasicsPanel::stripHeight)
            : juce::Rectangle<int>(18, deck_.getBottom() + 3, getWidth() - 36, SongBasicsPanel::stripHeight));
        int y = arrangement.getY() + globalHeight - juce::roundToInt(verticalScroll_.getCurrentRangeStart());
        const auto firstChord = std::find_if(view.globalLanes.begin(), view.globalLanes.end(),
            [](const auto& lane) { return lane.kind == TimelineLane::chord; });
        const int after = int(firstChord - view.globalLanes.begin()) + int(firstChord != view.globalLanes.end());
        const int guideY = ruler_.getBounds().getBottom() + arrangement.getY()
            - juce::roundToInt(verticalScroll_.getCurrentRangeStart()) + after * groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)];
        functionRow_ = arrangement.withTrimmedLeft(trackHeaderWidth).withY(guideY).withHeight(2 * timelineRowHeight());
        bool arrangementHeading = false;
        trackRows_.assign(trackControls_.size(), {});
        for (const auto i : order) {
            if (!project_->isGlobalNoteTrack(i) && !arrangementHeading) {
                arrangementHeading = true;
                y += arrangementHeadingHeight;
            }
            const int height = trackControls_[i]->size.height();
            trackRows_[i] = {arrangement.getX(), y, arrangement.getWidth(), height - TrackRowSize::grip};
            y += height + theoryTrackHeight(i);
        }
        auto placeGrip = [this](TrackRowSize& size, juce::Rectangle<int> bounds) {
            bounds = rowOverlay_.getLocalArea(this, bounds);
            size.setBounds(bounds.withY(bounds.getBottom()).withHeight(TrackRowSize::grip));
        };
        for (std::size_t i = 0; i < trackControls_.size(); ++i) {
            const bool visible = !trackRows_[i].isEmpty();
            auto& strip = *trackControls_[i];
            for (auto* control : std::array<juce::Component*, 5>{&strip.mute, &strip.solo, &strip.effects.button,
                    &strip.size, &strip.meter}) control->setVisible(visible);
            for (auto* control : std::array<juce::Component*, 5>{&strip.volume, &strip.pan, &strip.mix,
                    &strip.preset, &strip.plugin}) control->setVisible(visible && !strip.size.compact());
            const bool canArm = i < project_->sourceTrackCount();
            const bool midi = canArm && project_->trackKinds[i] != ostfstudio::TrackKind::audio;
            strip.cue.setVisible(visible);
            strip.arm.setVisible(visible && canArm);
            const bool audio = canArm && !midi;
            strip.recordingMeter.setVisible(visible && audio);
            strip.inputGain.setVisible(visible && audio && !strip.size.compact());
            strip.input.setVisible(visible && canArm);
            if (visible && canArm) strip.input.setButtonText(strip.size.expanded() ? strip.input.getProperties()["routeLabel"].toString() : "IN");
            strip.inputLamp.setVisible(visible && midi);
            strip.arm.setEnabled(!recordTrack_);
            strip.input.setEnabled(!recordTrack_);
            if (!visible) continue;
            auto inputControls = [&](juce::Rectangle<int>& row) {
                strip.cue.setBounds(row.removeFromLeft(26).reduced(1, 0));
                if (canArm) strip.arm.setBounds(row.removeFromLeft(26).reduced(1, 0));
                if (midi) {
                    strip.inputLamp.setBounds(row.removeFromLeft(14));
                }
                if (canArm && !strip.size.expanded()) strip.input.setBounds(row.removeFromLeft(28).reduced(1, 0));
            };
            placeGrip(strip.size, trackRows_[i]);
            auto header = rowOverlay_.getLocalArea(this, trackRows_[i]).withWidth(trackHeaderWidth - columnGripWidth);
            if (strip.size.compact()) {
                header.removeFromLeft(trackNameWidth);
                strip.mute.setBounds(header.removeFromLeft(29).reduced(2, 0));
                strip.solo.setBounds(header.removeFromLeft(29).reduced(2, 0));
                inputControls(header);
                strip.effects.button.setBounds(header.removeFromRight(48).reduced(2, 0));
                if (audio) strip.recordingMeter.setBounds(header.removeFromLeft(header.getWidth()/2).reduced(3, 3));
                strip.meter.setBounds(header.reduced(8, 0).withSizeKeepingCentre(header.getWidth() - 16, 9));
                continue;
            }
            if (strip.size.expanded()) {
                auto top = header.removeFromTop(26);
                top.removeFromLeft(trackNameWidth);
                strip.mute.setBounds(top.removeFromLeft(29).reduced(2, 1));
                strip.solo.setBounds(top.removeFromLeft(29).reduced(2, 1));
                inputControls(top);
                strip.effects.button.setBounds(top.removeFromRight(50).reduced(2, 1));
                strip.plugin.setBounds(top.reduced(2, 1));
                auto middle = header.removeFromTop(24);
                middle.removeFromLeft(trackNameWidth);
                strip.volume.setBounds(middle.removeFromLeft(128).reduced(3, 0));
                strip.pan.setBounds(middle.removeFromLeft(98).reduced(3, 0));
                strip.mix.setBounds(middle.reduced(2, 1));
                auto meter = header.removeFromLeft(171).reduced(6, 0);
                strip.meter.setBounds(meter.withHeight(meter.getHeight() >= 26 ? 24 : 10));
                if (canArm) {
                    auto inputArea = header.removeFromTop(std::min(21, header.getHeight()));
                    if (audio) {
                        strip.inputGain.setBounds(inputArea.removeFromRight(std::min(117, inputArea.getWidth()/2)).reduced(2,0));
                        strip.recordingMeter.setBounds(inputArea.removeFromRight(inputArea.getWidth()/3).reduced(2,5));
                    }
                    strip.input.setBounds(inputArea.reduced(3,0));
                }
                if (audio) {
                    strip.preset.setText(TRANS("Recording input gain (dB)"), juce::dontSendNotification);
                    strip.preset.setJustificationType(juce::Justification::centredRight);
                }
                strip.preset.setBounds(header.reduced(4, 0).withHeight(std::min(16,header.getHeight())));
                continue;
            }
            strip.meter.setBounds(header.withWidth(trackNameWidth).removeFromBottom(10).reduced(5, 1));
            auto row = header.withTrimmedLeft(trackNameWidth);
            auto top = row.removeFromTop((row.getHeight() + 1) / 2);
            strip.mute.setBounds(top.removeFromLeft(27).reduced(1, 0));
            strip.solo.setBounds(top.removeFromLeft(27).reduced(1, 0));
            inputControls(top);
            strip.effects.button.setBounds(top.removeFromRight(44).reduced(1, 0));
            strip.plugin.setBounds(top.reduced(2, 0));
            strip.volume.setBounds(row.removeFromLeft(85).reduced(2, 0));
            strip.pan.setBounds(row.removeFromLeft(69).reduced(2, 0));
            strip.mix.setBounds(row.removeFromRight(44).reduced(1, 0));
            if (audio) {
                strip.preset.setBounds({});
                strip.inputGain.setBounds(row.reduced(2, 0));
                strip.recordingMeter.setBounds(strip.meter.getBounds().withTrimmedRight(trackNameWidth / 2));
                strip.meter.setBounds(strip.meter.getBounds().withTrimmedLeft(trackNameWidth / 2));
            } else strip.preset.setBounds(row.reduced(2, 0));
        }
        y += arrangementHeadingHeight;
        for (std::size_t ordinal = 0; ordinal < outputs; ++ordinal) {
            const auto i = mixer_.routing().outputAt(ordinal);
            auto& strip = outputControls_[i];
            outputRows_[i] = {arrangement.getX(), y, arrangement.getWidth(), strip.size.height() - TrackRowSize::grip};
            y += strip.size.height();
            placeGrip(strip.size, outputRows_[i]);
            auto header = rowOverlay_.getLocalArea(this, outputRows_[i]).withWidth(trackHeaderWidth - columnGripWidth);
            auto top = header.removeFromTop(std::min(34, header.getHeight())).withTrimmedLeft(trackNameWidth);
            strip.mute.setBounds(top.removeFromLeft(29).reduced(2, 1));
            strip.effects.button.setBounds(top.removeFromRight(54).reduced(2, 1));
            strip.volume.setVisible(!strip.size.compact());
            if (!strip.size.compact()) strip.volume.setBounds(top.removeFromLeft(140).reduced(4, 0));
            strip.meter.setBounds(strip.size.expanded() ? header.reduced(6, 2).withHeight(24)
                              : top.reduced(5, 0).withSizeKeepingCentre(top.getWidth() - 10, 9));
        }
    }
    if (renameEditor_.isVisible()) {
        juce::Rectangle<int> row;
        if (renameOutput_ && renameRow_ < outputRows_.size()) row = outputRows_[renameRow_];
        if (!renameOutput_ && renameRow_ < trackRows_.size()) row = trackRows_[renameRow_];
        const auto field = row.withTrimmedLeft(21).withWidth(trackNameWidth - 24).withHeight(std::min(25, row.getHeight()));
        if (project_ && !row.isEmpty() && rowOverlay_.getBounds().contains(field)) {
            renameEditor_.setBounds(field);
            renameEditor_.toFront(false);
        } else renameEditor_.setVisible(false);
    }
    pianoDivider_.setBounds(editor.withY(editor.getY() + arrangerHeight).withHeight(8));
    editor.removeFromTop(arrangerHeight + 8);
    const int clipHeight = clipLane_.isVisible() ? std::min(24, editor.getHeight() / 5) : 0;
    const PianoLayout pianoLayout(editor.withTrimmedTop(30 + clipHeight), trackHeaderWidth, verticalScroll_.isVisible(),
                                  pitchedSelection() && pianoColours_.getSelectedId() != plainNotes,
                                  parameterFraction_, drumSteps_.isVisible() ? DrumSteps::labelWidth : 52);
    audioEditor_.setBounds(editor.withTrimmedTop(30 + clipHeight));
    audioEditor_.setTimelineColumns(pianoLayout.ruler.getX() - audioEditor_.getX(), pianoLayout.ruler.getRight() - audioEditor_.getX());
    clipLane_.setBounds(pianoLayout.ruler.withY(editor.getY() + 30).withHeight(clipHeight));
    tempoEditor_.setBounds(editor);
    tempoEditor_.setTimelineColumns(rowOverlay_.getX() + trackHeaderWidth - editor.getX(), rowOverlay_.getRight() - editor.getX());
    pianoGrid_.setBounds(pianoLayout.grid);
    parameterDivider_.setBounds(pianoLayout.divider);
    parameterBottom_ = pianoLayout.lane.getBottom();
    parameterHeight_ = parameterBottom_ - pianoLayout.ruler.getY();
    pianoRuler_.setBounds(pianoLayout.ruler);
    drumSteps_.setBounds(pianoLayout.ruler.getUnion(pianoLayout.grid).getUnion(pianoLayout.lane).withLeft(pianoLayout.keys.getX()));
    controllerEditor_.setBounds(pianoLayout.lane.withTrimmedTop(16));
    pianoVelocity_.setBounds(pianoLayout.lane.withTrimmedTop(16));
    pianoVelocity_.setVisible(pianoGrid_.isVisible() && pianoGrid_.editable && performanceLane_.getSelectedId() == 1
                              && pianoLayout.lane.getHeight() >= 20);
    editor.reduce(6, 4);
    auto toolbar = editor.removeFromTop(26);
    if (paneControl_.isVisible()) paneControl_.setBounds(toolbar.removeFromRight(std::min(38, toolbar.getWidth() / 8)).reduced(1));
    if (editorRecord_.isVisible()) editorRecord_.setBounds(toolbar.removeFromRight(88).reduced(1));
    if (cycleButton_.isVisible()) cycleButton_.setBounds(toolbar.removeFromRight(90).reduced(1));
    if (midiDrag_.isVisible() && toolbar.getWidth() < 720) midiDrag_.setBounds({});
    else if (midiDrag_.isVisible()) midiDrag_.setBounds(toolbar.removeFromRight(192));
    const auto pickerWidth = performanceLane_.isVisible() ? juce::jmin(180, toolbar.getWidth() / 2) : 180;
    trackPicker_.setBounds(toolbar.removeFromLeft(pickerWidth));
    toolbar.removeFromLeft(6);
    performanceLane_.setBounds(toolbar.removeFromLeft(std::min(250, toolbar.getWidth() * 2 / 3)).withTrimmedRight(4));
    pitchZoom_.setBounds(toolbar.removeFromLeft(155));
    mixer_.setBounds(editor);
    auto inspector = pianoLayout.inspector;
    // Keep the grip inside the label column so the first tick remains editable.
    const int gripTop = drumSteps_.isVisible() ? drumSteps_.getY() + DrumSteps::headerHeight
                      : clipLane_.isVisible() ? clipLane_.getY() : pianoLayout.ruler.getY();
    columnDivider_.setBounds(pianoLayout.ruler.getX() - columnGripWidth, gripTop, columnGripWidth,
                             std::max(0, (audioEditor_.isVisible() ? audioEditor_.getBottom() : pianoLayout.lane.getBottom()) - gripTop));
    columnDivider_.toFront(false);
    auto pianoNavigation = inspector.removeFromTop(28).reduced(2, 1);
    if (pianoSnap_.isVisible()) pianoSnap_.setBounds(pianoNavigation.removeFromRight(137).reduced(2, 0));
    if (drumMode_.isVisible()) drumMode_.setBounds(pianoNavigation.removeFromLeft(std::min(210,pianoNavigation.getWidth())).reduced(2,0));
    if (pianoColours_.isVisible()) pianoColours_.setBounds(pianoNavigation.removeFromLeft(std::min(210, pianoNavigation.getWidth())).reduced(2, 0));
    if (drumPads_.isVisible())
        drumPads_.setBounds(inspector.removeFromTop(drumSteps_.isVisible() ? inspector.getHeight()
                                                                           : std::min(80, inspector.getHeight())));
    pianoInspectorView_.setBounds(inspector);
    const bool showLegend = pianoColours_.isVisible() && pianoColours_.getSelectedId() != plainNotes;
    pianoInspector_.showLegend = showLegend;
    pianoInspector_.updateActions(pianoGrid_.grid > 1, pianoGrid_.selection.size());
    const int inspectorHeight = PianoInspector::controlsHeight + (showLegend ? 112 : 0);
    pianoInspector_.setSize(std::max(1, inspector.getWidth() - (inspector.getHeight() < inspectorHeight ? pianoInspectorView_.getScrollBarThickness() : 0)), inspectorHeight);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(pianoInspector_.theme == pianoHarmony::Theme::graphite    ? 0xff202122
                           : pianoInspector_.theme == pianoHarmony::Theme::aubergine ? 0xff211925
                           : blueTheme()                                             ? 0xff111d29
                                                                                     : bg));
    g.setColour(juce::Colour(0xff1b2229));
    g.fillRect(0, menuHeight, getWidth(), viewBarHeight(getWidth()));
    if (workspaceMode_ != WorkspaceMode::arrangement) return;
    auto area = getWorkspace();
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
    if (!mixerButton_.getToggleState() && !audioEditor_.isVisible() && !tempoEditor_.isVisible()) {
        drawPianoRoll(g, area);
    }
}

void MainComponent::drawArrangement(juce::Graphics& g, juce::Rectangle<int> area,
                                    const ostfstudio::ArrangementView& view) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff313941), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(panel), 0.0f, static_cast<float>(area.getBottom()), false));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 13);
    juce::Graphics::ScopedSaveState clipped(g);
    if (project_) {
        area = rowOverlay_.getBounds();
        g.reduceClipRegion(area);
        area.translate(0, -juce::roundToInt(verticalScroll_.getCurrentRangeStart()));
    }
    const auto outputs = visibleOutputCount();
    const int laneHeight = project_ ? timelineRowHeight() : arrangementLaneHeight(area, view, outputs);
    auto labels = area.removeFromLeft(project_ ? trackHeaderWidth : 108);
    if (laneHeight < 12 || area.getWidth() < 40) return;
    int rowHeight = laneHeight - 4;

    auto themed = [this](juce::Colour colour) {
        if (pianoInspector_.theme == pianoHarmony::Theme::graphite)
            return colour.withMultipliedSaturation(0.10f);
        if (pianoInspector_.theme == pianoHarmony::Theme::aubergine)
            return juce::Colour::fromHSV(0.79f + 0.07f * colour.getHue(), 0.38f, colour.getBrightness(), 1.f);
        return colour;
    };
    auto drawRow = [&](const juce::String& label, int y, std::optional<ViewButton::Icon> icon = {}, bool global = false) {
        g.setColour(juce::Colour(0xffe1e9ef));
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        const int labelHeight = project_ && rowHeight >= TrackRowSize::normal - TrackRowSize::grip
                              ? std::min(27, rowHeight - 10) : rowHeight;
        if (icon) {
            g.setColour(juce::Colour(*icon == ViewButton::Icon::drums ? 0xffecc684 : 0xff8edbd4));
            ViewButton::drawIcon(g, *icon, labels.getX() + 5.f, y + (labelHeight - 18) * .5f);
            g.setColour(juce::Colour(0xffe1e9ef));
        }
        const int inset = icon ? 26 : 5;
        g.drawText(label,
                         juce::Rectangle<int>(labels.getX() + inset, y, (global ? labels.getWidth() : std::min(trackNameWidth, labels.getWidth())) - inset - 5, labelHeight),
                         global ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
        const auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff233443));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        return row;
    };

    const auto bars = view.bars;
    const auto visible = project_ ? horizontalRange()
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
    const auto ruler = area.withHeight(laneHeight);
    if (!project_) {
        drawRow(TRANS("BARS"), area.getY());
        for (int bar = firstBar; bar < visible.end; bar += tickStep) {
            const int x = positionX(bar, ruler);
            g.setColour(juce::Colour(muted));
            g.drawText(juce::String(bar), ruler.withLeft(x).withWidth(std::min(36, ruler.getRight() - x)).withTrimmedLeft(3),
                       juce::Justification::centredLeft);
        }
    }

    auto drawLane = [&](const ostfstudio::ArrangementLane& lane, int y, juce::Colour colour, std::optional<ViewButton::Icon> icon = {}) {
        const auto row = drawRow(lane.kind == TimelineLane::none ? juce::String::fromUTF8(lane.label) : juce::translate(lane.label), y, icon, lane.kind != TimelineLane::none);
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
            g.setColour(themed(colour));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
            if (selectedGlobal_ == lane.kind && project_) {
                const auto bar = 1. + static_cast<double>(selectedGlobalTick_) / project_->ticksPerBar;
                if (bar >= span.start && bar < span.end) {
                    g.setColour(juce::Colours::white);
                    g.drawRoundedRectangle(bounds.toFloat().reduced(0.7f), 3.f, 1.4f);
                }
            }
            g.setColour(juce::Colours::white.withAlpha(0.93f));
            auto label = juce::String::fromUTF8(region.label);
            if (lane.kind == TimelineLane::key || lane.kind == TimelineLane::scale || lane.kind == TimelineLane::chord)
                label = ostfstudio::shiftedLabel(label, auditionShift_);
            if (lane.kind == TimelineLane::tempo && auditionRate_ != 1)
                label = juce::String(label.getDoubleValue() * auditionRate_, 2);
            if (region.noteCount > 0) label = "[" + juce::String(region.noteCount) + TRANS(" notes] ") + label;
            auto caption = bounds.reduced(4, 0);
            if (rowHeight >= 70 - TrackRowSize::grip) caption = caption.removeFromTop(18);
            g.setFont(juce::FontOptions(13.f));
            g.drawText(label, caption, juce::Justification::centredLeft);
        }
    };

    int y = area.getY() + laneHeight;
    const int globalLaneHeight = project_ ? groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)] : laneHeight;
    rowHeight = globalLaneHeight - 4;
    constexpr juce::uint32 blueGlobal[] {0xff315478, 0xff3b6088, 0xff36506b, 0xff304b66, 0xff314961, 0xff345b71};
    const juce::uint32 globalColours[] {0xff314d72, 0xff493c64, 0xff31495a, 0xff31404e, 0xff31404e, 0xff2c6058};
    bool drewTheory = false;
    const auto globalTheory = [&] {
        drawTheoryRows(g, juce::Rectangle<int>(labels.getX(), y, labels.getWidth() + area.getWidth(), (2 * timelineRowHeight())),
                       labels.getWidth(), std::nullopt);
        y += 2 * timelineRowHeight();
        drewTheory = true;
    };
    for (std::size_t i = 0; i < view.globalLanes.size(); ++i) {
        drawLane(view.globalLanes[i], y, juce::Colour((blueTheme() ? blueGlobal : globalColours)[i % std::size(globalColours)]));
        y += globalLaneHeight;
        if (theoryVisible_ && project_ && view.globalLanes[i].kind == TimelineLane::chord) globalTheory();
    }
    if (theoryVisible_ && project_ && !drewTheory) globalTheory();

    constexpr juce::uint32 blueTracks[] {0xff335f83, 0xff356d91, 0xff455d88, 0xff416d82, 0xff495f78};
    const juce::uint32 trackColours[] {0xff2a657d, 0xff265e57, 0xff4b517f, 0xff744366, 0xff526070};
    auto heading = [&](const char* title) {
        g.setColour(juce::Colour(accent));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(juce::translate(title), labels.getX() + 4, y, labels.getWidth() + area.getWidth() - 4,
                   arrangementHeadingHeight, juce::Justification::centredLeft);
        y += arrangementHeadingHeight;
    };
    auto drawTrack = [&](std::size_t i) {
        if (project_) {
            if (i >= trackRows_.size()) return;
            y = trackRows_[i].getY();
            rowHeight = trackRows_[i].getHeight();
            const auto fitHeight = theoryTrackHeight(i);
            if (fitHeight > 0)
                drawTheoryRows(g, trackRows_[i].withY(y + trackControls_[i]->size.height()).withHeight(fitHeight),
                               labels.getWidth(), i);
            if (!trackRows_[i].intersects(g.getClipBounds())) {
                y += trackControls_[i]->size.height() + fitHeight;
                return;
            }
        }
        const auto channel = juce::Rectangle<int>(labels.getX(), y, labels.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff353d45));
        g.fillRoundedRectangle(channel.toFloat().reduced(1.0f), 3.0f);
        g.setColour(juce::Colour(0xff525d67));
        g.drawHorizontalLine(y + 1, static_cast<float>(channel.getX() + 4), static_cast<float>(channel.getRight() - 4));
        std::optional<ViewButton::Icon> icon;
        if (project_) {
            using Icon = ViewButton::Icon;
            const auto kind = project_->trackKinds[i];
            const auto group = project_->mixerGroups[i];
            icon = kind == ostfstudio::TrackKind::drum ? Icon::drums : kind == ostfstudio::TrackKind::audio ? Icon::audio
                 : group == ostfstudio::MixerTrackGroup::vocal ? Icon::chorus : group == ostfstudio::MixerTrackGroup::melody ? Icon::melody
                 : project_->chordVoicingTrack == i ? Icon::score : Icon::pitched;
        }
        drawLane(view.trackLanes[i], y, juce::Colour((blueTheme() ? blueTracks : trackColours)[i % std::size(trackColours)]), icon);
        if (project_) {
            if (trackControls_[i]->size.expanded()) {
                g.setColour(juce::Colour(muted));
                g.drawFittedText(TRANS("Volume / Pan"), channel.withWidth(trackNameWidth).withY(y + 26)
                                 .withHeight(24).reduced(5, 0), juce::Justification::centredLeft, 1);
                const auto& notes = project_->trackKinds[i] == ostfstudio::TrackKind::drum
                                  ? project_->drumNotes[i] : project_->pitchedNotes[i];
                if (!notes.empty() && project_->ticksPerBar > 0) {
                    const auto pitches = std::minmax_element(notes.begin(), notes.end(),
                        [](const auto& a, const auto& b) { return a.pitch < b.pitch; });
                    const auto low = pitches.first->pitch, high = pitches.second->pitch;
                    const int top = y + 22, space = std::max(1, rowHeight - 28);
                    const auto timeline = trackRows_[i].withTrimmedLeft(trackHeaderWidth);
                    g.setColour(juce::Colour(0xffb3ebdc));
                    for (const auto& note : notes) {
                        if (note.endTick <= (visible.start - 1) * project_->ticksPerBar
                            || note.startTick >= (visible.end - 1) * project_->ticksPerBar) continue;
                        const auto noteRect = clipBounds({1.0 + static_cast<double>(note.startTick) / project_->ticksPerBar,
                                                          1.0 + static_cast<double>(note.endTick) / project_->ticksPerBar}, timeline);
                        const int noteY = top + (high == low ? space / 2 : space * (high - note.pitch) / (high - low));
                        g.fillRect(noteRect.getX(), noteY, std::max(1, noteRect.getWidth() - 1), 2);
                    }
                }
            }
            const auto audio = std::find_if(project_->audio.begin(), project_->audio.end(), [i](const auto& item) { return item.track == i; });
            if (audio != project_->audio.end()) {
                const auto start = 1.0 + static_cast<double>(audio->startTick) / project_->ticksPerBar;
                const bool missing = !waveforms_.available(i);
                const auto& audioClock = auditionClock_ ? *auditionClock_ : project_->clock;
                const auto endBar = resizingAudio_ == i && previewAudioLength_ > 0 ? start + double(previewAudioLength_) / project_->ticksPerBar
                    : missing && audio->durationTicks <= 0 ? start + 1
                    : 1.0 + waveforms_.endTick(*audio, audioClock) / project_->ticksPerBar;
                auto clip = clipBounds({start, endBar}, area.withY(y).withHeight(rowHeight)).reduced(0, 2);
                if (!clip.isEmpty()) {
                    g.setColour(juce::Colour(missing ? 0xff754842 : 0xff386d70));
                    g.fillRoundedRectangle(clip.toFloat(), 3);
                    auto body = clip.reduced(0, 2);
                    const auto labelArea = missing ? body : body.removeFromTop(std::min(14, body.getHeight() / 3));
                    if (!missing) {
                        std::optional<ostfstudio::ClipTiming> preview;
                        if (resizingAudio_ == i && audio->timing.cycleTicks && !audio->timing.loop) {
                            preview = audio->timing;
                            preview->cycleTicks = previewAudioLength_;
                        }
                        waveforms_.draw(g, body, *audio, audioClock,
                                        (std::max(start, visible.start) - 1) * project_->ticksPerBar,
                                        (std::min(endBar, visible.end) - 1) * project_->ticksPerBar, preview ? &*preview : nullptr);
                    }
                    g.setColour(juce::Colours::white);
                    const auto label = missing ? TRANS("Missing WAV") : audio->path.fromLastOccurrenceOf("/", false, false);
                    if (selectedTrack_ == i && !missing && endBar <= visible.end) {
                        g.setColour(juce::Colour(0xffffc479));
                        g.fillRect(clip.getRight() - 3, clip.getY() + 1, 3, std::max(1, clip.getHeight() - 2));
                    }
                    g.setColour(juce::Colours::white);
                    if (clip.getWidth() >= 25)
                        g.drawText((audio->timing.loop ? juce::String::charToString(0x21bb) + " " : juce::String()) + label, labelArea.reduced(3, 0), juce::Justification::centredLeft);
                }
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
        y += project_ ? trackControls_[i]->size.height() + theoryTrackHeight(i) : laneHeight;
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
        for (std::size_t ordinal = 0; ordinal < outputs; ++ordinal) {
            const auto i = mixer_.routing().outputAt(ordinal);
            const auto row = outputRows_[i];
            g.setColour(juce::Colour(0xff343c46));
            g.fillRoundedRectangle(row.toFloat(), 4.0f);
            g.setColour(juce::Colour(hardwareAmber));
            g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawFittedText(mixer_.outputName(i), row.withWidth(trackNameWidth).withHeight(std::min(34, row.getHeight())).reduced(5, 0),
                             juce::Justification::centredLeft, 1);
            g.setColour(juce::Colour(muted));
            g.drawFittedText(i == 0 ? TRANS("Out 1-2") : TRANS("To Master"), row.withTrimmedLeft(trackHeaderWidth).reduced(8, 0),
                             juce::Justification::centredLeft, 1);
            if (outputDropHighlight_ == i) {
                g.setColour(juce::Colour(hardwareAmber));
                g.drawRoundedRectangle(row.toFloat().reduced(1.0f), 4.0f, 2.0f);
            }
            if (outputControls_[i].size.expanded()) {
                juce::StringArray names;
                for (const auto& slot : outputControls_[i].effects.slots) {
                    auto fields = juce::StringArray::fromTokens(slot.id, "|", "");
                    if (fields.size() >= 3) names.add(fields[2]);
                }
                g.drawFittedText(names.joinIntoString("  >  "), row.withTrimmedTop(61).withWidth(trackHeaderWidth).reduced(6, 0),
                                 juce::Justification::topLeft, 1);
            }
            y = row.getBottom() + TrackRowSize::grip;
        }
    }
    if (cursorTick_ >= 0 && project_) {
        const auto bar = 1.0 + static_cast<double>(cursorTick_) / project_->ticksPerBar;
        if (bar >= visible.start && (bar < visible.end || (bar == visible.end && bar == project_->bars.endBar))) {
            g.setColour(juce::Colour(hardwareAmber));
            g.drawVerticalLine(std::min(positionX(bar, ruler), ruler.getRight() - 1), static_cast<float>(ruler.getY()), static_cast<float>(y - 4));
        }
    }
}

void MainComponent::drawTheoryRows(juce::Graphics& g, juce::Rectangle<int> bounds, int labelWidth,
                                   std::optional<std::size_t> track) const {
    if (!theory_ || bounds.getHeight() < 4 || !bounds.intersects(g.getClipBounds())) return;
    const auto bars = horizontalRange();
    const double start = (bars.start - 1) * project_->ticksPerBar, end = (bars.end - 1) * project_->ticksPerBar;
    if (end <= start || bounds.getWidth() <= labelWidth) return;
    auto labels = bounds.removeFromLeft(labelWidth);
    const double scale = bounds.getWidth() / (end - start);
    auto x = [&](double tick) { return bounds.getX() + juce::roundToInt((std::clamp(tick, start, end) - start) * scale); };
    const auto clip = bounds.getIntersection(g.getClipBounds());
    const double first = start + (clip.getX() - bounds.getX()) / scale;
    const double last = start + (clip.getRight() - bounds.getX()) / scale;
    auto band = [&](int y, int height, const juce::String& name, const juce::String& current) {
        auto row = bounds.withY(y).withHeight(height);
        g.setColour(juce::Colour(0xff1b2831));
        g.fillRect(row.withTrimmedBottom(1));
        g.setColour(juce::Colour(0xffe1e9ef));
        g.setFont(juce::FontOptions(15.f, juce::Font::bold));
        auto header = labels.withY(y).withHeight(height);
        auto title = track ? header.removeFromLeft(trackNameWidth) : header.removeFromRight(trackNameWidth);
        g.drawText(name, title.reduced(5, 0), track ? juce::Justification::centredLeft : juce::Justification::centredRight);
        g.setColour(juce::Colour(hardwareAmber));
        if (track) g.drawText(current, header.reduced(4, 0), juce::Justification::centredLeft);
        return row;
    };
    auto cell = [&](juce::Rectangle<int> row, auto a, auto b, juce::Colour colour, const juce::String& name, const juce::String& shortName) {
        if (b <= first || a >= last) return;
        auto rect = row.withX(x(double(a))).withWidth(std::max(1, x(double(b)) - x(double(a)) - 1)).withTrimmedBottom(1);
        g.setColour(colour);
        g.fillRect(rect);
        const juce::Font font(juce::FontOptions(14.f));
        g.setFont(font);
        const auto text = rect.getWidth() >= juce::GlyphArrangement::getStringWidth(font, name) + 6 ? name : shortName;
        if (rect.getWidth() >= juce::GlyphArrangement::getStringWidth(font, text) + 4) {
            g.setColour(juce::Colours::white);
            g.drawText(text, rect.reduced(2, 0), juce::Justification::centredLeft);
        }
    };
    if (!track) {
        const auto degreeBand = band(bounds.getY(), bounds.getHeight() - timelineRowHeight(),
                                     TRANS("Degree") + " / " + TRANS("Progression"), {});
        const auto top = band(degreeBand.getBottom(), timelineRowHeight(), TRANS("Function"), {});
        if (clip.isEmpty()) return;
        auto visibleSpans = [&](const auto& list, auto draw) {
            auto it = std::partition_point(list.begin(), list.end(), [first](const auto& f) { return f.end <= first; });
            for (; it != list.end() && it->start < last; ++it) draw(*it);
        };
        visibleSpans(theory_->functions, [&](const auto& f) {
            juce::String symbol;
            if (f.function == "Tonic") symbol = "T";
            else if (f.function == "Tonic substitute") symbol = "T'";
            else if (f.function == "Subdominant") symbol = "S";
            else if (f.function == "Dominant") symbol = "D";
            else if (f.function == "Secondary dominant") symbol = "D/";
            else if (f.function == "Dominant substitute") symbol = "D'";
            auto colour = juce::Colour(0xff505667);
            if (f.function.startsWith("Tonic")) colour = juce::Colour(blueTheme() ? 0xff33617e : 0xff306453);
            if (f.function == "Subdominant") colour = juce::Colour(0xff365979);
            if (symbol.startsWith("D")) colour = juce::Colour(blueTheme() ? 0xff535884 : 0xff795338);
            auto degree = f.degree;
            const auto phrase = std::lower_bound(theory_->progressions.begin(), theory_->progressions.end(), f.start,
                [](const auto& p, auto startTick) { return p.start < startTick; });
            if (phrase != theory_->progressions.end() && phrase->start == f.start)
                degree = (degree + "    [" + phrase->label + "]").trim();
            cell(degreeBand.withTrimmedBottom(5), f.start, f.end,
                 juce::Colour(blueTheme() ? 0xff364e6e : 0xff504660), degree, f.degree);
            cell(top, f.start, f.end, colour, (symbol + "  " + juce::translate(f.function)).trim(), symbol);
        });
        visibleSpans(theory_->progressions, [&](const auto& p) {
            const int left = x(double(p.start)), right = x(double(p.end)) - 1;
            const int y = degreeBand.getBottom() - 2;
            g.setColour(juce::Colour(0xff94cbe4));
            g.drawHorizontalLine(y, float(left), float(std::max(left + 1, right)));
            if (p.start >= start) g.drawVerticalLine(left, float(y - 3), float(y + 1));
            if (p.end <= end) g.drawVerticalLine(right, float(y - 3), float(y + 1));
        });
        return;
    }
    if (*track >= theoryTracks_.size()) return;
    if (theoryTracks_[*track].empty()) {
        band(bounds.getY(), bounds.getHeight(), TRANS("Pitch fit"), TRANS("Not applicable"));
        return;
    }
    const auto& fits = *track == selectedTrack_ && draggedNote_ ? previewFits_ : theoryTracks_[*track];
    const auto& inspected = draggedNote_ ? draggedNote_ : inspectedNote_;
    const auto readTick = *track == selectedTrack_ && inspected && !transportActive_ ? inspected->startTick : cursorTick_;
    const auto currentCell = theory_->firstCell(double(readTick));
    const auto height = bounds.getHeight() / std::max(1, int(chordFitVisible_) + int(scaleFitVisible_));
    int y = bounds.getY();
    for (const bool chord : {true, false}) {
        if (chord ? !chordFitVisible_ : !scaleFitVisible_) continue;
        auto referenceName = [&](const auto& c, const auto& f) -> juce::String {
            if (chord) return c.chord >= 0 ? ostfstudio::shiftedLabel(theory_->chords[c.chord].name, auditionShift_) : juce::String();
            if (!f.noteScoped.empty()) return TRANS("Targeted scale");
            if (f.backgroundScale >= 0) return ostfstudio::shiftedLabel(theory_->scales[f.backgroundScale].name, auditionShift_);
            if (f.backgroundScale == -2) return TRANS("Select scale reference");
            return {};
        };
        auto percent = [](int value) { return value < 0 ? juce::String::charToString(0x2014) : juce::String(value) + "%"; };
        juce::String reading;
        if (currentCell < fits.size()) {
            const auto& f = fits[currentCell];
            reading = percent((chord ? f.chord : f.scale).percent()) + "   " + referenceName(theory_->cells[currentCell], f);
        }
        const auto row = band(y, height, chord ? TRANS("Chord fit") : TRANS("Scale fit"), reading);
        y += height;
        if (clip.isEmpty()) continue;
        for (auto i = theory_->firstCell(first); i < fits.size() && theory_->cells[i].start < last; ++i) {
            const auto& c = theory_->cells[i];
            const auto& f = fits[i];
            const auto value = (chord ? f.chord : f.scale).percent();
            const auto shortLabel = percent(value);
            const auto label = shortLabel + " " + referenceName(c, f);
            auto colour = juce::Colour(0xff313d46);
            if (value >= 0) {
                if (value < 50) colour = juce::Colour(0xff963f4e);
                else if (value < 75) colour = juce::Colour(0xff975323);
                else if (value < 90) colour = juce::Colour(0xff776629);
                else colour = juce::Colour(blueTheme() ? 0xff34698b : 0xff256d61);
            }
            const auto next = chord ? f.chordEnd : i + 1;
            cell(row, chord ? theory_->cells[f.chordBegin].start : c.start, theory_->cells[next - 1].end, colour, label, shortLabel);
            i = next - 1;
        }
    }
}

void MainComponent::drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    if (drumSteps_.isVisible()) return;
    const PianoLayout layout(area.withTrimmedTop(clipLane_.isVisible() ? clipLane_.getHeight() : 0), trackHeaderWidth, verticalScroll_.isVisible(),
                             pitchedSelection() && pianoColours_.getSelectedId() != plainNotes, parameterFraction_);
    const auto mode = pianoColours_.getSelectedId();
    const bool fit = theory_ && pitchedSelection() && mode != plainNotes;
    const bool scaleMode = mode == scaleNotes;
    auto heading = layout.heading;
    const auto reading = noteFitReading();
    if (fit && (!pianoInspectorView_.isVisible() || pianoInspectorView_.getHeight() < pianoInspector_.getHeight())
        && heading.getWidth() > 330 && !heading.isEmpty()) {
        const juce::Font font(juce::FontOptions(13.f));
        g.setFont(font);
        for (const auto tone : {Tone::outside, Tone::avoid, Tone::tension, Tone::fifth, Tone::guide, Tone::root}) {
            const auto label = toneName(tone);
            const int width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, label)) + 25;
            if (heading.getWidth() < width + (reading.isEmpty() ? 140 : 420)) break;
            auto entry = heading.removeFromRight(width);
            g.setColour(toneColour(tone));
            g.fillEllipse(entry.getX() + 2.f, entry.getCentreY() - 4.f, 8.f, 8.f);
            g.setColour(juce::Colours::white);
            g.drawText(label, entry.withTrimmedLeft(14), juce::Justification::centredLeft);
        }
    }
    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    auto name = selectedTrack_ ? TRANS("PIANO ROLL  /  ") + juce::String::fromUTF8(project_->trackLanes[*selectedTrack_].label)
                                     : TRANS("PIANO ROLL");
    if (!pianoGrid_.selection.isEmpty()) name += "    " + TRANS("Selected") + " " + juce::String(pianoGrid_.selection.size());
    if (reading.isNotEmpty()) name += "     " + reading;
    g.drawFittedText(selectedTrack_ && !pianoGrid_.editable ? name + "  /  " + TRANS("Read only") : name,
                     heading, juce::Justification::centredLeft, 1);
    if (!selectedTrack_) {
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(project_->trackLanes.empty() ? TRANS("Add your first track from the Track menu.")
                                               : TRANS("Select an arrangement track to inspect its notes."),
                   area, juce::Justification::centred);
        return;
    }

    const auto& notes = pianoGrid_.notes;
    const auto laneName = performanceLane_.getText();
    const auto& curves = project_->performanceCurves[*selectedTrack_];
    const int low = pianoGrid_.low, high = pianoGrid_.high;
    const auto visible = horizontalRange();
    const auto barLength = project_->ticksPerBar;
    const auto firstBar = static_cast<juce::int64>(std::ceil(visible.start)) - 1;
    const auto endBar = static_cast<juce::int64>(std::ceil(visible.end)) - 1;
    const auto origin = (visible.start - 1.0) * barLength;
    const auto length = (visible.end - visible.start) * barLength;
    if (length <= 0.0) return;
    auto lane = layout.lane;
    const auto keys = layout.keys, ruler = layout.ruler;
    area = layout.grid;
    if (area.isEmpty()) return;

    const auto rowHeight =
        static_cast<float>(area.getHeight()) / static_cast<float>(std::max(1, pianoGrid_.rowCount()));
    auto noteY = [&](int pitch) {
        return static_cast<float>(area.getY()) + static_cast<float>(pianoGrid_.pitchRow(pitch)) * rowHeight;
    };
    auto tickX = [&](juce::int64 tick) {
        return static_cast<float>(area.getX() + (static_cast<double>(tick) - origin) / length * area.getWidth());
    };
    g.setFont(juce::FontOptions(13.f, juce::Font::bold));
    g.setColour(juce::Colour(0xffe6daef));
    if (!layout.chords.isEmpty()) g.drawText(scaleMode ? TRANS("Scale") : TRANS("CHORD"), layout.chords.withX(keys.getX()).withWidth(keys.getWidth()-2), juce::Justification::centredLeft);
    if (scaleMode && fit && layout.chords.intersects(g.getClipBounds())) {
        const auto& fits = theoryTracks_[*selectedTrack_];
        for (auto i = theory_->firstCell(origin); i < fits.size() && theory_->cells[i].start < origin + length;) {
            const int id = fits[i].backgroundScale;
            const auto& targeted = fits[i].noteScoped;
            auto end = i + 1;
            while (end < fits.size() && theory_->cells[end].start < origin + length && fits[end].backgroundScale == id
                   && targeted == fits[end].noteScoped) ++end;
            auto cell = layout.chords.withLeft(std::max(area.getX(), juce::roundToInt(tickX(theory_->cells[i].start))))
                .withRight(std::min(area.getRight(), juce::roundToInt(tickX(theory_->cells[end-1].end))));
            const auto label = scaleContextName(*theory_, fits[i], auditionShift_);
            g.setColour(juce::Colour(0xff325673)); g.fillRect(cell.withTrimmedRight(1));
            g.setColour(juce::Colours::white); g.drawText(label, cell.reduced(3, 0), juce::Justification::centredLeft);
            i = end;
        }
    } else if (!scaleMode && layout.chords.intersects(g.getClipBounds())) for (const auto& chordLane : project_->globalLanes) if (chordLane.kind == TimelineLane::chord) {
        for (const auto& r : chordLane.regions) {
            if (!r.exactBars || r.exactBars->end <= visible.start || r.exactBars->start >= visible.end) continue;
            const auto x = [&](double bar) { return area.getX() + juce::roundToInt((bar-visible.start)/(visible.end-visible.start)*area.getWidth()); };
            const auto cell = layout.chords.withLeft(std::max(area.getX(), x(r.exactBars->start)))
                .withRight(std::min(area.getRight(), x(r.exactBars->end)));
            g.setColour(juce::Colour(blueTheme() ? 0xff385b7a : 0xff504361)); g.fillRect(cell.withTrimmedRight(1));
            g.setColour(juce::Colours::white);
            if (cell.getWidth() > 12) g.drawText(ostfstudio::shiftedLabel(juce::String::fromUTF8(r.label), auditionShift_),
                cell.reduced(2,0), juce::Justification::centredLeft, false);
        }
    }
    const auto dirty = area.getIntersection(g.getClipBounds());
    const double left = origin + length * (dirty.getX() - area.getX()) / area.getWidth();
    const double right = origin + length * (dirty.getRight() - area.getX()) / area.getWidth();
    constexpr bool isBlack[] {false, true, false, true, false, false, true, false, true, false, true, false};
    constexpr const char* pitchNames[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    g.setFont(juce::FontOptions(10.0f));
    for (int row = 0; row < pianoGrid_.rowCount(); ++row) {
        const int pitch = pianoGrid_.rowPitch(row);
        const auto y = noteY(pitch);
        const int sounding = pitch + (pitchedSelection() ? auditionShift_ : 0);
        const int pc = ((sounding % 12) + 12) % 12;
        const bool black = isBlack[pc];
        g.setColour(juce::Colour(black ? 0xff192735 : 0xff233443));
        g.fillRect(static_cast<float>(area.getX()), y, static_cast<float>(area.getWidth()), rowHeight);
        const auto key = juce::Rectangle<float>(static_cast<float>(keys.getX()), y, static_cast<float>(keys.getWidth() - 2), rowHeight);
        g.setColour(juce::Colour(black ? 0xff202830 : 0xffe6e6dc));
        g.fillRect(key.reduced(0, 0.5f));
        if (rowHeight >= 11.0f || (pitch % 12 == 0 && rowHeight * 12.0f >= 12.0f)) {
            g.setColour(black ? juce::Colours::white : juce::Colour(0xff283037));
            g.drawText(pianoGrid_.percussion ? juce::String(pitch)
                                             : juce::String(pitchNames[pc]) + juce::String((sounding - pc) / 12 - 1),
                       key.toNearestInt(), juce::Justification::centred);
        }
    }

    if (fit) {
        const auto& fits = theoryTracks_[*selectedTrack_];
        for (auto i = theory_->firstCell(left); i < fits.size() && theory_->cells[i].start < right; ++i) {
            const auto& c = theory_->cells[i];
            const int scale = fits[i].backgroundScale;
            const auto* chord = c.chord >= 0 ? &theory_->chords[c.chord] : nullptr;
            const int scaleMask = scale >= 0 ? theory_->scales[scale].mask : 0;
            const auto x = std::max(float(area.getX()), tickX(c.start));
            const auto w = std::min(float(area.getRight()), tickX(c.end)) - x;
            if (w <= 0) continue;
            for (int p = low; p <= high; ++p) {
                const auto tone = theory_->tone(p, chord, scaleMask);
                const int root = scaleMode ? (scale >= 0 ? theory_->scales[scale].tonic : -1) : (chord ? chord->root : -1);
                if (root < 0 || (scaleMode ? !theory_->contains(scaleMask, p) : tone == Tone::outside || tone == Tone::unknown)) continue;
                g.setColour(degreeColour(p, root).withAlpha(blueTheme() ? 0.10f : 0.18f));
                g.fillRect(x, noteY(p), w, rowHeight);
            }
        }
    }
    const auto step = pianoGrid_.grid;
    if (pianoGrid_.editable && step > 1 && double(step) * area.getWidth() / length >= 8) {
        g.setColour(juce::Colour(0xff354653));
        for (auto line = juce::int64(std::ceil((origin - 0.5) / step)); line * step < origin + length; ++line)
            g.drawVerticalLine(juce::roundToInt(tickX(std::llround(line * step))), float(area.getY()), float(area.getBottom()));
    }
    const auto barStep = std::max<juce::int64>(1, static_cast<juce::int64>((visible.end - visible.start) / (area.getWidth() < 420 ? 4 : 8)));
    for (auto bar = firstBar; bar < endBar; bar += barStep) {
        const auto x = juce::roundToInt(tickX(bar * barLength));
        g.setColour(juce::Colour(0xff526578));
        g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }

    g.saveState();
    g.reduceClipRegion(area);
    const auto drawNote = [&](const ostfstudio::PitchedNote& note) {
        if (note.endTick <= left || note.startTick >= right) return;
        const auto x = tickX(note.startTick);
        const auto width = std::max(2.0f, tickX(note.endTick) - x);
        const auto height = std::max(1.0f, rowHeight - 1.0f);
        g.setColour(juce::Colour(fit ? 0xff818a92 : accent));
        g.fillRoundedRectangle(x, noteY(note.pitch), width, height, 2.0f);
        if (!fit || !note.enabled || note.fixedTrigger) return;
        const auto& fits = theoryTracks_[*selectedTrack_];
        for (auto i = theory_->firstCell(std::max(left, double(note.startTick))); i < fits.size(); ++i) {
            const auto& c = theory_->cells[i];
            if (c.start >= note.endTick || c.start >= right) break;
            const auto a = tickX(std::max(note.startTick, c.start)), b = tickX(std::min(note.endTick, c.end));
            const int index = theory_->scaleForNote(fits[i], note);
            const int scaleMask = index < 0 ? 0 : theory_->scales[index].mask;
            const auto* chord = theory_->chordMask(c) ? &theory_->chords[c.chord] : nullptr;
            auto rect = juce::Rectangle<float>(a, noteY(note.pitch), std::max(1.f, b-a), height);
            const auto role = theory_->tone(note.pitch, chord, scaleMask);
            const int root = scaleMode ? (index >= 0 ? theory_->scales[index].tonic : -1) : (chord ? chord->root : -1);
            auto fill = degreeColour(note.pitch, root);
            g.setColour(fill);
            g.fillRect(rect);
            if (height >= 3.f) {
                g.setColour(toneColour(role));
                g.fillRect(rect.removeFromBottom(std::clamp(height * .27f, 1.f, 5.f)));
            }
            if (mode == chordAndScaleNotes && scaleMask && height >= 7.f) {
                g.setColour(toneColour(theory_->contains(scaleMask, note.pitch) ? Tone::scale : Tone::outside));
                g.fillRect(rect.removeFromBottom(2.f));
            }
            if (rect.getWidth() >= 33.f && rect.getHeight() >= 13.f && root >= 0) {
                g.setFont(juce::FontOptions(13.f, juce::Font::bold));
                g.setColour(juce::Colour(0xff152632));
                g.drawText(theory_->interval(note.pitch, root), rect.toNearestInt().reduced(3, 0), juce::Justification::centredLeft);
            }
        }
    };
    const auto firstNote = std::upper_bound(pianoMaxEnds_.begin(), pianoMaxEnds_.end(), left) - pianoMaxEnds_.begin();
    for (auto it = notes.begin() + firstNote; it != notes.end() && it->startTick < right; ++it) drawNote(*it);
    for (const auto& e : pianoGrid_.pendingEdits()) {
        auto note = e.note;
        if (e.action == ostfstudio::NoteEdit::duplicate) note.id.clear();
        drawNote(note);
    }
    g.restoreState();

    // Use the exact same tick-to-pixel mapping as the notes, including when the piano roll is zoomed.
    const auto drawCursor = [&](int bottom) {
        const int column = pianoRuler_.playheadColumn();
        if (column < 0) return;
        const int x = pianoRuler_.getX() + column;
        g.setColour(juce::Colour(hardwareAmber));
        g.fillRect(x, ruler.getBottom(), 2, std::max(0, bottom - ruler.getBottom()));
    };
    if (lane.getHeight() < 20) { drawCursor(area.getBottom()); return; }
    g.setColour(juce::Colour(0xff192735));
    g.fillRect(lane);
    const auto title = lane.removeFromTop(16);
    g.setColour(juce::Colour(muted));
    g.drawFittedText(laneName, title.reduced(4, 0), juce::Justification::centredLeft, 1);
    if (controllerEditor_.isVisible()) { drawCursor(title.getBottom()); return; }
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
    drawCursor(lane.getBottom());
}
