#include "ui/MainComponent.h"
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
constexpr int trackHeaderWidth = 465;
constexpr int timelineNavigationHeight = 24;
constexpr int trackNameWidth = 150;
constexpr int arrangementHeadingHeight = 24;
constexpr int globalRowHeight = 28;
constexpr int theoryGuideHeight = 52, fitLaneHeight = 24;
enum PianoColourMode { plainNotes = 1, chordNotes, scaleNotes, chordAndScaleNotes };
using Tone = ostfstudio::TheoryGuide::Tone;
juce::Colour toneColour(Tone tone) {
    switch (tone) {
        case Tone::root: return juce::Colour(0xfff2c56d);
        case Tone::guide: return juce::Colour(0xff60c9b4);
        case Tone::chord: return juce::Colour(0xffb0ddb5);
        case Tone::scale: return juce::Colour(0xff94bff0);
        case Tone::outside: return juce::Colour(0xffd6a2bc);
        default: return juce::Colour(0xff88919a);
    }
}
juce::String toneName(Tone tone) {
    switch (tone) {
        case Tone::root: return TRANS("Root");
        case Tone::guide: return TRANS("3rd / 7th");
        case Tone::chord: return TRANS("Chord tone");
        case Tone::scale: return TRANS("In scale");
        case Tone::outside: return TRANS("Outside reference");
        default: return TRANS("No reference");
    }
}

struct PianoLayout {
    juce::Rectangle<int> heading, lane, keys, ruler, grid;
    explicit PianoLayout(juce::Rectangle<int> area) {
        const bool compact = area.getHeight() < 130;
        area.reduce(13, compact ? 2 : 8);
        heading = area.removeFromTop(compact ? 0 : 24);
        lane = area.removeFromBottom(std::min(84, area.getHeight() / 3)).withTrimmedLeft(52);
        keys = area.removeFromLeft(52);
        ruler = area.removeFromTop(compact ? 13 : 18);
        keys.removeFromTop(ruler.getHeight());
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

juce::Rectangle<int> workspaceBounds(juce::Rectangle<int> bounds) {
    const int headerHeight = TransportPanel::panelHeight(bounds.getWidth()) + menuHeight + viewBarHeight(bounds.getWidth());
    bounds.reduce(18, 18);
    bounds.removeFromTop(headerHeight);
    bounds.removeFromBottom(26);
    return bounds;
}

enum MenuCommand { openFile = 1, createProject, showPiano, showMixer, showDevices, showAbout, showPlugins,
                   saveScore, saveProject, showOutputs, addPitched, addDrums, addAudio, removeTrack,
                   exportMidi0, exportMidi1, languageSystem, languageEnglish, languageJapanese, showLeadSheet, showPartScore, importMedia, showArrangement, showSongInformation, addMelody, addChorus, addBus, addReturn, copyTrack, renameTrackItem, smallerUI, largerUI, showShortcuts };
constexpr int timelineMenuBase = 100;
constexpr int showTheory = 80, showChordFit = 81, showScaleFit = 82, scaleReferenceBase = 1000;
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
    setWantsKeyboardFocus(true);
    viewPreferences_.setStorageParameters(ostfstudio::viewSettingsOptions());
    if (const auto* config = viewPreferences_.getUserSettings()) {
        interfaceScale_ = std::clamp(config->getIntValue("interfaceScale", 100), minInterfaceScale, maxInterfaceScale);
        juce::Desktop::getInstance().setGlobalScaleFactor(interfaceScale_ / 100.f);
        timelineLaneMask_ = static_cast<unsigned>(config->getIntValue("timelineLanes", 0xff));
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
    addAndMakeVisible(documentLabel_);
    documentLabel_.setJustificationType(juce::Justification::centredRight);
    documentLabel_.setFont(juce::FontOptions(14.0f));
    documentLabel_.setComponentID("current-document");
    const auto addView = [this](ViewButton& button, const char* label, int command) {
        const auto name = juce::translate(label);
        button.setName(name);
        button.setTitle(name);
        button.setTooltip(name);
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
    deck_.home.onClick = [this] { navigateTransport(TransportKey::home); };
    deck_.rewind.onClick = [this] { navigateTransport(TransportKey::rewind); };
    deck_.forward.onClick = [this] { navigateTransport(TransportKey::forward); };
    deck_.follow.onClick = [this] { displayTransportPosition(cursorTick_); };
    addChildComponent(trackPicker_);
    addChildComponent(performanceLane_);
    performanceLane_.setTitle(TRANS("Performance lane"));
    performanceLane_.onChange = [this] { pianoVelocity_.cancel(); resized(); repaint(); };
    for (auto* zoom : {&arrangementZoom_, &pianoZoom_}) {
        const bool piano = zoom == &pianoZoom_;
        zoom->setSliderStyle(juce::Slider::LinearHorizontal);
        zoom->setRange(0.0, 1.0);
        zoom->setDoubleClickReturnValue(true, 0.0);
        zoom->textFromValueFunction = [this](double value) {
            if (!project_ || value == 0.0) return TRANS("Fit song");
            const double span = std::pow(project_->bars.endBar - project_->bars.firstBar, 1.0 - value);
            return juce::String(span, std::abs(span - std::round(span)) < 0.05 ? 0 : 1)
                + (span < 1.05 ? TRANS(" bar") : TRANS(" bars"));
        };
        zoom->setTextBoxStyle(juce::Slider::TextBoxRight, true, 72, 18);
        zoom->setTitle(piano ? TRANS("Piano roll horizontal zoom") : TRANS("Arrangement horizontal zoom"));
        zoom->setTooltip(TRANS("Drag right to zoom in. Double-click to fit the song."));
        zoom->onValueChange = [this, piano] { updateHorizontalRange(piano); repaint(); };
        addChildComponent(zoom);
    }
    for (auto* label : {&arrangementZoomLabel_, &pianoZoomLabel_, &heightLabel_}) {
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
    for (auto* scroll : {&arrangementScroll_, &pianoScroll_}) {
        scroll->setComponentID(scroll == &pianoScroll_ ? "piano-horizontal-scroll" : "arrangement-horizontal-scroll");
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
    addChildComponent(pianoGrid_);
    addChildComponent(pianoVelocity_);
    pianoGrid_.tickRange = [this] {
        const auto span = horizontalRange(true);
        const auto bar = project_ ? project_->ticksPerBar : 1;
        return juce::Range<double>((span.start - 1) * bar, (span.end - 1) * bar);
    };
    pianoGrid_.commit = [this](const auto& edits) { applyNoteGesture(edits); };
    pianoGrid_.undoRedo = [this](bool redo) { undoPianoEdit(redo); };
    addChildComponent(pianoColours_);
    pianoColours_.setComponentID("piano-note-colours");
    pianoColours_.setTitle(TRANS("Note colours"));
    pianoColours_.setTooltip(TRANS("Colour notes and pitch rows by the harmony at each beat. Hover a note for its interval."));
    pianoColours_.addItem(TRANS("Normal notes"), plainNotes);
    pianoColours_.addItem(TRANS("Chord tones"), chordNotes);
    pianoColours_.addItem(TRANS("Scale tones"), scaleNotes);
    pianoColours_.addItem(TRANS("Chord + scale"), chordAndScaleNotes);
    const auto* settings = viewPreferences_.getUserSettings();
    const int colourMode = settings ? settings->getIntValue("pianoColourMode", chordAndScaleNotes) : chordAndScaleNotes;
    pianoColours_.setSelectedId(std::clamp(colourMode, int(plainNotes), int(chordAndScaleNotes)), juce::dontSendNotification);
    pianoColours_.onChange = [this] {
        if (auto* prefs = viewPreferences_.getUserSettings()) prefs->setValue("pianoColourMode", pianoColours_.getSelectedId());
        if (!theory_ || (!theoryVisible_ && pianoColours_.getSelectedId() == plainNotes)) refreshTheory();
        repaint();
    };
    addChildComponent(pianoSnap_);
    pianoSnap_.setTitle(TRANS("Note grid"));
    pianoSnap_.setTooltip(TRANS("Snap note edits to this grid"));
    for (int division : {4, 8, 16, 32}) pianoSnap_.addItem(TRANS("Grid") + " 1/" + juce::String(division), division);
    pianoSnap_.addItem(TRANS("Grid off"), 1);
    pianoSnap_.setSelectedId(16, juce::dontSendNotification);
    pianoSnap_.onChange = [this] {
        pianoGrid_.grid = pianoSnap_.getSelectedId() == 1 ? 1
            : std::max<juce::int64>(1, pianoGrid_.quarter * 4 / pianoSnap_.getSelectedId());
        repaint();
    };
    pianoGrid_.theoryHint = [this](juce::int64 tick, const ostfstudio::PitchedNote& n) -> juce::String {
        const int mode = pianoColours_.getSelectedId();
        if (mode == plainNotes || !theory_ || !pitchedSelection() || !n.enabled || n.fixedTrigger) return {};
        const auto i = theory_->firstCell(double(tick));
        if (i >= theory_->cells.size()) return {};
        const auto& cell = theory_->cells[i];
        juce::StringArray details;
        details.add(juce::MidiMessage::getMidiNoteName(n.pitch, true, true, 4));
        if (mode != scaleNotes && theory_->chordMask(cell)) {
            const auto& h = theory_->chords[cell.chord];
            details.add(h.name + "  " + TRANS("Interval") + " " + theory_->interval(n.pitch, h.root) + " / "
                + (theory_->contains(h.mask, n.pitch) ? toneName(theory_->tone(n.pitch, &h, 0)) : TRANS("Outside chord tones")));
        }
        const auto s = theory_->scaleForNote(theoryTracks_[*selectedTrack_][i], n);
        if (mode != chordNotes && s >= 0 && theory_->scales[s].mask)
            details.add(theory_->scales[s].name + "  " + TRANS("Interval") + " " + theory_->interval(n.pitch, theory_->scales[s].tonic) + " / " + (theory_->contains(theory_->scales[s].mask, n.pitch)
                ? TRANS("In scale") : TRANS("Outside scale")));
        if (details.size() == 1) details.add(TRANS("No reference"));
        return details.joinIntoString("\n");
    };
    rowOverlay_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(rowOverlay_);
    ruler_.headerWidth = trackHeaderWidth;
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
    setupRuler(ruler_, arrangementScroll_);
    setupRuler(pianoRuler_, pianoScroll_);
    addChildComponent(pianoRuler_);
    rowOverlay_.addAndMakeVisible(ruler_);
    waveforms_.onChange = [this] { repaint(rowOverlay_.getBounds()); };
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
    mixer_.requestReturn = [this] { editOutput(true); };
    mixer_.rowMenu = [this](bool output, std::size_t index) { showRowMenu(output, index); };
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
    statusLabel_.setFont(juce::FontOptions(13.0f));
    refreshTimelineLanes();
    newProject();
}

juce::StringArray MainComponent::getMenuBarNames() { return {TRANS("File"), TRANS("View"), TRANS("Track"), TRANS("Settings"), TRANS("Help")}; }

juce::PopupMenu MainComponent::timelineMenu() const {
    juce::PopupMenu lanes;
    for (std::size_t i = 0; i < std::size(selectableTimelineLanes); ++i)
        lanes.addItem(timelineMenuBase + static_cast<int>(i), juce::translate(timelineMenuLabels[i]), true,
                      (timelineLaneMask_ & laneBit(selectableTimelineLanes[i])) != 0);
    return lanes;
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
            juce::PopupMenu languages;
            const auto selected = viewPreferences_.getUserSettings()->getValue("language", "system");
            languages.addItem(languageSystem, TRANS("Use system language"), true, selected == "system");
            languages.addItem(languageEnglish, "English", true, selected == "en");
            languages.addItem(languageJapanese, juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"), true, selected == "ja");
            menu.addSubMenu(TRANS("Language"), languages);
            break;
        }
        case 4:
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
            menu.addItem(renameTrackItem, TRANS("Rename track..."), editable && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            menu.addItem(copyTrack, TRANS("Duplicate track"), add && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            menu.addItem(removeTrack, TRANS("Delete selected track..."), editable && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount());
            break;
        }
    }
    return menu;
}

void MainComponent::menuItemSelected(int item, int) {
    if (item == showTheory || item == showChordFit || item == showScaleFit || item >= scaleReferenceBase) {
        if (!project_) return;
        if (item == showTheory) theoryVisible_ = !theoryVisible_;
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
        case importMedia:
            if (!importWindow_)
                importWindow_ = makeMediaImportWindow([this](const juce::File& file) { openDocument(file); });
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
    auto bounds = workspaceBounds(getLocalBounds());
    if (pluginBrowserButton_.getToggleState()) bounds.removeFromRight(juce::jmin(230, bounds.getWidth() / 3) + 8);
    return bounds;
}

int MainComponent::getArrangementHeight(int workspaceHeight) const {
    if (!editorVisible_) return workspaceHeight;
    return mixerButton_.getToggleState() ? workspaceHeight / 2 : workspaceHeight * 3 / 5;
}

void MainComponent::scrollBarMoved(juce::ScrollBar* bar, double) {
    if (bar == &verticalScroll_) resized();
    repaint();
}

void MainComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (rowOverlay_.isVisible() && rowOverlay_.getBounds().contains(event.getPosition())
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

void MainComponent::updateHorizontalRange(bool piano, bool reset) {
    if (!project_) return;
    auto& zoom = piano ? pianoZoom_ : arrangementZoom_;
    auto& scroll = piano ? pianoScroll_ : arrangementScroll_;
    const auto song = project_->bars;
    const auto length = song.endBar - song.firstBar;
    if (reset && zoom.getValue() > 0.0 && scroll.getCurrentRangeSize() > 0.0) {
        const double previous = juce::jlimit(1.0, double(length), scroll.getCurrentRangeSize());
        zoom.setValue(length > 1 ? 1.0 - std::log(previous) / std::log(length) : 0.0, juce::dontSendNotification);
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
}

ostfstudio::TimelineExtent MainComponent::horizontalRange(bool piano) const {
    const auto& scroll = piano ? pianoScroll_ : arrangementScroll_;
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
    return workspaceMode_ == WorkspaceMode::arrangement && project_
        && (id == generatorDragId || (pluginBrowser_ && id.startsWith("ostf-vst3:")));
}

std::optional<std::size_t> MainComponent::pluginDropTrack(juce::Point<int> position) const {
    if (!rowOverlay_.getBounds().withTrimmedTop(globalRowHeight).contains(position)) return std::nullopt;
    for (std::size_t i = 0; i < std::min(trackRows_.size(), trackControls_.size()); ++i)
        if (trackRows_[i].contains(position) && !trackControls_[i]->loading) return i;
    return std::nullopt;
}

std::size_t MainComponent::visibleOutputCount() const {
    return project_ && showOutputTracks_ ? mixer_.routing().outputCount() : 0;
}

std::optional<std::size_t> MainComponent::outputDropTrack(juce::Point<int> position) const {
    if (!rowOverlay_.getBounds().withTrimmedTop(globalRowHeight).contains(position)) return std::nullopt;
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
            refreshTrackTheory(*track);
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
    if (transportActive_) {
        if (transport_.isRunning() && recording == recordTrack_.has_value()) return;
        stopTransport();
    }
    if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
    if (!canEditTracks()) {
        statusLabel_.setText(TRANS("Waiting for instruments and effects to finish loading"), juce::dontSendNotification);
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
    const auto start = cursorTick_ >= project_->playbackEndTick() ? 0 : std::max<juce::int64>(0, cursorTick_);
    const auto result = transport_.start(deviceSettings_->devices(), *project_, recording, tracks, inserts, start);
    if (result.failed()) {
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return;
    }
    transportActive_ = true;
    recordTrack_ = recording ? selectedTrack_ : std::nullopt;
    deck_.play.setToggleState(!recording, juce::dontSendNotification);
    deck_.stop.setToggleState(false, juce::dontSendNotification);
    deck_.record.setToggleState(recording, juce::dontSendNotification);
    for (auto* button : {&deck_.home, &deck_.rewind, &deck_.forward}) button->setEnabled(!recording);
    displayTransportPosition(start);
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
    PreviewTransport::Cursor parked{};
    auto recorded = transport_.stop(&parked);
    if (recordTrack_ && project_) {
        const auto count = static_cast<int>(recorded.size());
        const bool added = project_->addSessionClip(*recordTrack_, "Recorded MIDI", std::move(recorded));
        if (added) refreshTrackTheory(*recordTrack_);
        selectTrack(*recordTrack_);
        statusLabel_.setText(added ? juce::String(count) + TRANS(" MIDI notes captured") : TRANS("No MIDI notes captured"),
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
    stopTimer();
    for (auto& track : trackControls_) track->meter.clear();
    for (auto& output : outputControls_) output.meter.clear();
    mixer_.clearMeters();
    repaint();
}

void MainComponent::showShortcutList() {
    const std::pair<const char*, const char*> rows[] = {
        {"Space", "Play / stop"}, {"Home key", "Return to song start"}, {"F1", "Show shortcuts"},
        {"Click the bar ruler", "Move the playhead"}, {"Drag the bar ruler", "Scroll the timeline horizontally"},
        {"Shift + wheel / horizontal scroll", "Scroll the timeline horizontally"},
        {"Mouse wheel", "Scroll through tracks"}, {"Right-click a track name", "Open track actions"},
        {"Double-click empty piano grid", "Add a note"}, {"Drag note / edge", "Move / resize notes"},
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
    if (key.getModifiers().isAnyModifierKeyDown() || isCurrentlyBlockedByAnotherModalComponent()) return false;
    const auto* focus = juce::Component::getCurrentlyFocusedComponent();
    if (focus && (dynamic_cast<const juce::TextEditor*>(focus) || focus->findParentComponentOfClass<juce::TextEditor>())) return false;
    if (key.isKeyCode(juce::KeyPress::F1Key)) { showShortcutList(); return true; }
    if (!project_) return false;
    if (key.isKeyCode(juce::KeyPress::homeKey)) {
        navigateTransport(TransportKey::home);
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
    const int previousArrangement = ruler_.playheadColumn(), previousPiano = pianoRuler_.playheadColumn();
    if (theoryVisible_ && theory_ && tick != cursorTick_
        && theory_->firstCell(double(cursorTick_)) != theory_->firstCell(double(tick)))
        repaint(rowOverlay_.getBounds().withWidth(trackHeaderWidth).withTrimmedLeft(trackNameWidth));
    cursorTick_ = tick;
    if (scoreView_ && scoreView_->isVisible()) scoreView_->playbackPosition(tick);
    if (!project_) return;
    deck_.setPosition(*project_, std::max<juce::int64>(0, tick), seconds);
    bool scrolled = false;
    if (deck_.follow.getToggleState() && tick >= 0) {
        const auto bar = 1.0 + double(tick) / project_->ticksPerBar;
        for (auto* scroll : {&arrangementScroll_, &pianoScroll_}) {
            if (!scroll->isEnabled() || !scroll->isVisible() || scroll == anchoredView) continue;
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
    if (!transport_.isRunning()) { stopTransport(); return; }
    const auto frame = transport_.takeMeters();
    const auto cursor = transport_.cursor();
    displayTransportPosition(cursor.tick, cursor.seconds);
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
    if (result.wasOk()) {
        stopTransport();
        scoreView_.reset();
        project_ = std::move(next);
        scaleReference_ = -1;
        theory_.reset();
        refreshTheory();
        currentFilename_ = filename;
        legacySourceFile_ = project_->sourceVersion != "5.0.0" && sourceFolder != juce::File{}
            ? sourceFolder.getChildFile(filename) : juce::File{};
        updateDocumentLabel();
        displayTransportPosition(0);
        updateHorizontalRange(false, true);
        updateHorizontalRange(true, true);
        for (auto& control : outputControls_) {
            control.effects.slots.clear();
            control.effects.loading = 0;
            updateEffectButton(control.effects);
        }
        if (plainScore && sourceFolder != juce::File{})
            for (auto& audio : project_->audio) audio.localFile = sourceFolder.getChildFile(audio.path);
        waveforms_.load(project_->audio);
        refreshTimelineLanes();
        ++projectGeneration_;
        selectedTrack_.reset();
        trackRows_.clear();
        verticalScroll_.setCurrentRangeStart(0, juce::dontSendNotification);
        groupDefaults_.fill(TrackRowSize::normal);
        groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)] = globalRowHeight;
        for (auto& output : outputControls_) output.size.resetSize();
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
            mixerSelected_ = false;
            refreshEditor();
        }
    }
    statusLabel_.setText(result.wasOk() ? project_->title + " / " + juce::String(project_->bars.endBar - 1) + TRANS(" bars")
                                         + (legacySourceFile_ == juce::File{} ? juce::String{} :
                                            " / OSTF " + project_->sourceVersion + TRANS(" (saves as 5.0.0)"))
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
                safe->currentFilename_ = file.getFileName();
                safe->legacySourceFile_ = juce::File{};
                safe->updateDocumentLabel();
            }
            safe->statusLabel_.setText(result.wasOk() ? TRANS("Saved ") + file.getFileName() : juce::translate(result.getErrorMessage()),
                                       juce::dontSendNotification);
        });
}

void MainComponent::updateDocumentLabel() {
    const auto name = project_->title + "   " + juce::String::charToString(0x00b7) + "   "
        + (currentFilename_.isEmpty() ? TRANS("Unsaved project") : currentFilename_);
    documentLabel_.setText(name, juce::dontSendNotification);
    documentLabel_.setTooltip(name);
}

void MainComponent::editSongInformation() {
    if (!project_) return;
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = TRANS("Song information");
    options.dialogBackgroundColour = juce::Colour(panel);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.content.setOwned(new SongInformationPanel(project_->songInformation(),
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_](const auto& info) {
            if (!safe || generation != safe->projectGeneration_) return juce::Result::fail("The project has changed.");
            auto result = safe->project_->applySongInformation(info);
            if (result.wasOk()) {
                safe->updateDocumentLabel();
                safe->refreshEditor();
                safe->statusLabel_.setText(TRANS("Song information updated. Save the project to keep your changes."), juce::dontSendNotification);
            }
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
    const auto visible = rowOverlay_.getBounds().withTrimmedTop(globalRowHeight);
    const auto delta = row.getY() < visible.getY() ? row.getY() - visible.getY()
                     : std::max(0, row.getBottom() - visible.getBottom());
    verticalScroll_.setCurrentRangeStart(verticalScroll_.getCurrentRangeStart() + delta, juce::sendNotificationSync);
}

void MainComponent::showRowMenu(bool output, std::size_t index) {
    if (!canEditTracks() || (output ? !mixer_.routing().hasOutput(index)
                                   : index >= project_->sourceTrackCount())) return;
    const bool send = ostfstudio::isReturnOutput(index);
    const bool room = !output ? project_->sourceTrackCount() < ostfstudio::OstfpPreview::maxTracks
        : send ? returnAdd_.isEnabled() : busAdd_.isEnabled();
    juce::PopupMenu menu;
    menu.addSectionHeader(output ? mixer_.outputName(index) : juce::String::fromUTF8(project_->trackLanes[index].label));
    menu.addItem(renameTrackItem, TRANS("Rename track..."));
    if (!output || index != 0) {
        menu.addItem(copyTrack, TRANS("Duplicate track"), room);
        menu.addItem(removeTrack, TRANS("Delete track..."));
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, output, send, index](int choice) {
            if (!safe || safe->projectGeneration_ != generation) return;
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
    auto dialog = std::make_unique<juce::AlertWindow>(TRANS("Rename track"), juce::String{},
        juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("name", output ? mixer_.outputName(index) : juce::String::fromUTF8(project_->trackLanes[index].label), TRANS("Track name"));
    dialog->getTextEditor("name")->selectAll();
    dialog->addButton(TRANS("OK"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(TRANS("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto* window = dialog.release();
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<MainComponent>(this), window, index, output, generation = projectGeneration_](int choice) {
            if (!safe || choice != 1 || safe->projectGeneration_ != generation) return;
            const auto name = window->getTextEditorContents("name").trim();
            if (output) {
                if (name.isEmpty()) safe->statusLabel_.setText(TRANS("Enter a track name."), juce::dontSendNotification);
                else {
                    safe->mixer_.renameOutput(index, name);
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
        }), true);
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
                               std::optional<std::size_t> duplicate, ostfstudio::MixerTrackGroup group) {
    if (!canEditTracks()) return;
    stopTransport();
    std::unique_ptr<ostfstudio::OstfpPreview> updated;
    const auto result = project_->editTracks(kind, remove, updated, duplicate, group);
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

void MainComponent::rebuildTrackControls() {
    trackControls_.clear();
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i) {
        auto controls = std::make_unique<TrackControls>();
        controls->size.assignHeight(groupDefaults_[static_cast<std::size_t>(trackHeightGroup(i))]);
        const auto name = juce::String::fromUTF8(project_->trackLanes[i].label);
        controls->size.setTitle(name + " " + TRANS("track height"));
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
        controls->size.changed = [this] { refreshGroupHeight(); resized(); repaint(); };
        for (auto* part : std::array<juce::Component*, 9>{&controls->mute, &controls->solo, &controls->volume,
                 &controls->pan, &controls->plugin, &controls->mix, &controls->preset, &controls->meter, &controls->size})
            rowOverlay_.addAndMakeVisible(part);
        controls->effects.button.onClick = [this, i] { showEffects(false, i); };
        updateEffectButton(controls->effects);
        rowOverlay_.addAndMakeVisible(controls->effects.button);
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
            if (entry == audio.end() && audio.size() >= ostfstudio::OstfpPreview::maxTracks) return;
            safe->stopTransport();
            ostfstudio::ProjectAudio linked{index, file.getFileName(), file, {}};
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
            if (!reader || reader->sampleRate <= 0 || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024
                || reader->numChannels < 1 || reader->numChannels > 2) {
                safe->statusLabel_.setText(TRANS("Unsupported WAV") + ": " + file.getFileName(), juce::dontSendNotification);
                return;
            }
            linked.durationTicks = std::clamp<juce::int64>(static_cast<juce::int64>(std::ceil(
                safe->project_->clock.tickAt(reader->lengthInSamples / reader->sampleRate))), 1, safe->project_->playbackEndTick());
            if (entry == audio.end()) audio.push_back(std::move(linked));
            else *entry = std::move(linked);
            safe->waveforms_.load(audio);
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
        control.mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
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

void MainComponent::loadEffect(bool output, std::size_t index, std::size_t position, const juce::PluginDescription& description) {
    auto& chain = effectChain(output, index);
    ++chain.loading;
    refreshTrackActions();
    updateEffectButton(chain);
    pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512,
        [safe = juce::Component::SafePointer<MainComponent>(this), output, index, position,
         generation = projectGeneration_, description](std::unique_ptr<juce::AudioPluginInstance> effect, const juce::String& error) {
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
                slot.id = effectId(description);
            }
            safe->updateEffectButton(chain);
        });
}

void MainComponent::mouseDown(const juce::MouseEvent& event) {
    pendingMidiDrag_.reset();
    if (!rowOverlay_.isVisible() || !rowOverlay_.getBounds().contains(event.getPosition())) return;
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
    mixerSelected_ = false;
    refreshEditor();
}

void MainComponent::refreshPianoNotes() {
    const bool changed = pianoGeneration_ != projectGeneration_ || pianoTrack_ != selectedTrack_;
    if (pianoGeneration_ != projectGeneration_) { noteHistory_.clear(); noteHistoryCursor_ = 0; }
    pianoGeneration_ = projectGeneration_;
    pianoTrack_ = selectedTrack_;
    auto& grid = pianoGrid_;
    grid.notes.clear();
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
    grid.editable = track < project_->sourceTrackCount() && project_->trackKinds[track] != ostfstudio::TrackKind::audio;
    grid.setTooltip(grid.editable ? TRANS("Double-click to add a note. Drag its body to move or either edge to resize.")
                                  : TRANS("Read only"));
    grid.endTick = project_->playbackEndTick();
    grid.quarter = project_->ppq;
    if (changed) {
        grid.resetRange(project_->trackKinds[track] == ostfstudio::TrackKind::drum);
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
    if (!theoryVisible_ || track >= theoryTracks_.size() || theoryTracks_[track].empty()) return 0;
    if (selectedTrack_ != track && !trackControls_[track]->size.expanded()) return 0;
    return fitLaneHeight * (int(chordFitVisible_) + int(scaleFitVisible_));
}

juce::String MainComponent::getTooltip() {
    if (!project_ || workspaceMode_ != WorkspaceMode::arrangement) return {};
    const auto p = getMouseXYRelative();
    if (!rowOverlay_.getBounds().contains(p)) return {};
    if (theoryVisible_ && theory_ && functionRow_.contains(p)) {
        const auto range = horizontalRange(false);
        const double tick = (range.start - 1 + double(p.x - functionRow_.getX()) / functionRow_.getWidth()
                             * (range.end - range.start)) * project_->ticksPerBar;
        const auto& functions = theory_->functions;
        auto it = std::partition_point(functions.begin(), functions.end(), [tick](const auto& f) { return f.end <= tick; });
        if (it != functions.end() && it->start <= tick) {
            auto name = theory_->chords[it->chord].name + "  " + it->degree + "  " + juce::translate(it->function);
            const auto next = std::next(it);
            if (next != functions.end() && next->start == it->end && next->key == it->key && it->function == "Secondary dominant")
                name += "  " + juce::String::charToString(0x2192) + "  " + theory_->chords[next->chord].name;
            return name;
        }
    }
    for (std::size_t i = 0; i < trackRows_.size(); ++i) {
        if (trackRows_[i].withWidth(trackNameWidth).contains(p) && !trackRows_[i].isEmpty())
            return juce::String::fromUTF8(project_->trackLanes[i].label);
        const auto row = trackRows_[i].withTrimmedLeft(trackHeaderWidth);
        if (row.isEmpty()) continue;
        const auto lanes = row.withY(row.getBottom() + TrackRowSize::grip).withHeight(theoryTrackHeight(i));
        if (!lanes.contains(p)) continue;
        const auto range = horizontalRange(false);
        const auto bar = range.start + double(p.x - row.getX()) / row.getWidth() * (range.end - range.start);
        const auto c = theory_->firstCell((bar - 1) * project_->ticksPerBar);
        if (c >= theory_->cells.size()) return {};
        const auto& context = theory_->cells[c];
        const auto& fit = theoryTracks_[i][c];
        juce::StringArray lines;
        if (chordFitVisible_) {
            auto line = TRANS("Chord fit");
            if (context.chord >= 0) line += " / " + theory_->chords[context.chord].name;
            lines.add(line + " " + (fit.chord.total ? juce::String(fit.chord.percent()) + "%" : juce::String::charToString(0x2014)));
        }
        if (scaleFitVisible_) {
            auto line = TRANS("Scale fit");
            if (fit.backgroundScale >= 0) line += " / " + theory_->scales[fit.backgroundScale].name;
            else if (fit.backgroundScale == -2) line += " / " + TRANS("Select scale reference");
            lines.add(line + " " + (fit.scale.total ? juce::String(fit.scale.percent()) + "%" : juce::String::charToString(0x2014)));
        }
        return lines.joinIntoString("\n");
    }
    return {};
}

void MainComponent::finishNoteEdit(bool playing) {
    refreshTimelineLanes();
    if (pianoScroll_.getMaximumRangeLimit() != project_->bars.endBar) {
        updateHorizontalRange(true);
        updateHorizontalRange(false);
    }
    refreshEditor();
    if (playing) startTransport(false);
}

void MainComponent::applyNoteGesture(const std::vector<ostfstudio::NoteEdit>& edits) {
    if (!project_ || !selectedTrack_ || edits.empty()) return;
    if (recordTrack_) { statusLabel_.setText(TRANS("Finish recording before editing notes."), juce::dontSendNotification); return; }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    juce::MemoryBlock before;
    juce::StringArray ids;
    const auto result = project_->editNotes(*selectedTrack_, edits, before, ids);
    if (result.failed()) {
        if (playing) startTransport(false);
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return;
    }
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
    pianoGrid_.selection = std::move(ids);
    if (theory_ && (theory_->cells.empty() || theory_->cells.back().end != project_->playbackEndTick())) {
        theory_.reset();
        refreshTheory();
    } else refreshTrackTheory(*selectedTrack_);
    if (!edits.empty() && edits.front().action != ostfstudio::NoteEdit::erase) {
        const int p = edits.front().note.pitch;
        const int shift = p < pianoGrid_.low ? p - pianoGrid_.low : std::max(0, p - pianoGrid_.high);
        pianoGrid_.low += shift; pianoGrid_.high += shift;
    }
    finishNoteEdit(playing);
}

void MainComponent::undoPianoEdit(bool redo) {
    if (!project_ || recordTrack_ || (redo ? noteHistoryCursor_ >= noteHistory_.size() : noteHistoryCursor_ == 0)) return;
    const auto& revision = noteHistory_[redo ? noteHistoryCursor_ : noteHistoryCursor_ - 1];
    if (!project_->addedClips.empty() || project_->sourceScore != (redo ? revision.before : revision.after)) {
        noteHistory_.clear(); noteHistoryCursor_ = 0; return;
    }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    const auto result = project_->restoreNoteScore(redo ? revision.after : revision.before);
    if (result.wasOk()) {
        if (redo) ++noteHistoryCursor_; else --noteHistoryCursor_;
        pianoGrid_.selection.clear();
        theory_.reset();
        refreshTheory();
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
    const bool piano = arranging && project_ && editorVisible_ && !showMixer;
    refreshPianoNotes();
    pianoGrid_.setVisible(piano && selectedTrack_.has_value());
    pianoRuler_.setVisible(pianoGrid_.isVisible());
    if (!pianoGrid_.isVisible()) pianoVelocity_.setVisible(false);
    pianoSnap_.setVisible(piano && pianoGrid_.editable);
    pianoColours_.setVisible(piano && pitchedSelection());
    pianoButton_.setToggleState(piano, juce::dontSendNotification);
    outputButton_.setToggleState(arranging && showOutputTracks_, juce::dontSendNotification);
    for (auto* button : {&pianoButton_, &mixerButton_, &leadButton_, &scoreButton_}) button->setEnabled(project_ != nullptr);
    outputButton_.setEnabled(arranging && project_ != nullptr);
    songInfoButton_.setEnabled(project_ != nullptr);
    lanesButton_.setEnabled(arranging);
    trackPicker_.setVisible(arranging && project_ && editorVisible_);
    performanceLane_.setVisible(piano && selectedTrack_);
    const auto previous = performanceLaneNames_[performanceLane_.getSelectedId() - 1];
    performanceLaneNames_ = juce::StringArray{"Velocity"};
    if (project_ && selectedTrack_ && *selectedTrack_ < project_->performanceCurves.size())
        for (const auto& curve : project_->performanceCurves[*selectedTrack_])
            performanceLaneNames_.addIfNotAlreadyThere(curve.label);
    performanceLane_.clear(juce::dontSendNotification);
    for (int i = 0; i < performanceLaneNames_.size(); ++i) performanceLane_.addItem(juce::translate(performanceLaneNames_[i]), i + 1);
    performanceLane_.setSelectedId(std::max(1, performanceLaneNames_.indexOf(previous) + 1), juce::dontSendNotification);
    arrangementZoom_.setVisible(arranging && project_ != nullptr);
    arrangementZoomLabel_.setVisible(arrangementZoom_.isVisible());
    heightLabel_.setVisible(arrangementZoom_.isVisible());
    heightScope_.setVisible(arrangementZoom_.isVisible());
    groupHeight_.setVisible(arrangementZoom_.isVisible());
    refreshGroupHeight();
    arrangementScroll_.setVisible(arranging && project_ != nullptr);
    pianoZoom_.setVisible(piano);
    pianoZoomLabel_.setVisible(piano);
    pianoScroll_.setVisible(piano);
    midiDrag_.setVisible(piano);
    midiDrag_.setEnabled(selectedTrack_ && canExportTrack(*selectedTrack_));
    mixer_.setVisible(arranging && project_ != nullptr && showMixer);
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
    const int barHeight = viewBarHeight(getWidth());
    menuBar_.setBounds(getLocalBounds().removeFromTop(menuHeight));
    auto viewBar = getLocalBounds().withTrimmedTop(menuHeight).removeFromTop(barHeight).reduced(8, 0);
    auto row = viewBar.removeFromTop(32);
    const int width = std::clamp(getWidth() / 24, 24, 39);
    const auto place = [&](ViewButton& button) { button.setBounds(row.removeFromLeft(width).reduced(2, 2)); };
    for (auto* button : {&arrangementButton_, &leadButton_, &scoreButton_, &pianoButton_, &mixerButton_,
                         &pluginBrowserButton_, &outputButton_, &lanesButton_, &songInfoButton_}) place(*button);
    if (barHeight > 32) { documentLabel_.setBounds(row); row = viewBar; }
    row.removeFromLeft(8);
    for (auto* button : {&melodyAdd_, &chorusAdd_, &pitchedAdd_, &drumsAdd_, &audioAdd_, &busAdd_, &returnAdd_}) place(*button);
    row.removeFromLeft(8);
    place(uiSmaller_); place(uiLarger_);
    if (barHeight == 32) documentLabel_.setBounds(row.withTrimmedRight(8));
    deck_.setBounds(18, menuHeight + barHeight + 8, std::min(1480, getWidth() - 36), TransportPanel::panelHeight(getWidth()));
    statusLabel_.setBounds(20, getHeight() - 37, getWidth() - 40, 24);
    auto editor = getWorkspace();
    if (pluginBrowser_) {
        const auto full = workspaceBounds(getLocalBounds());
        pluginBrowser_->setBounds(full.withLeft(editor.getRight() + 8));
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
        const bool stacked = arrangement.getWidth() < 720;
        auto navigation = arrangement.removeFromBottom(timelineNavigationHeight * (stacked ? 2 : 1));
        auto heights = (stacked ? navigation.removeFromTop(timelineNavigationHeight) : navigation.removeFromLeft(325)).reduced(0, 2);
        heightLabel_.setBounds(heights.removeFromLeft(48));
        heightScope_.setBounds(heights.removeFromLeft(145));
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
        const int globalHeight = globalRowHeight + (theoryVisible_ ? theoryGuideHeight : 0) + static_cast<int>(view.globalLanes.size())
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
        ruler_.setBounds(rowOverlay_.getLocalBounds().removeFromTop(globalRowHeight));
        ruler_.toFront(false);
        int y = arrangement.getY() + globalHeight - juce::roundToInt(verticalScroll_.getCurrentRangeStart());
        const auto firstChord = std::find_if(view.globalLanes.begin(), view.globalLanes.end(),
            [](const auto& lane) { return lane.kind == TimelineLane::chord; });
        const int after = int(firstChord - view.globalLanes.begin()) + int(firstChord != view.globalLanes.end());
        const int guideY = ruler_.getBounds().getBottom() + arrangement.getY()
            - juce::roundToInt(verticalScroll_.getCurrentRangeStart()) + after * groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)];
        functionRow_ = arrangement.withTrimmedLeft(trackHeaderWidth).withY(guideY).withHeight(theoryGuideHeight / 2);
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
            if (!visible) continue;
            placeGrip(strip.size, trackRows_[i]);
            auto header = rowOverlay_.getLocalArea(this, trackRows_[i]).withWidth(trackHeaderWidth - 3);
            if (strip.size.compact()) {
                header.removeFromLeft(trackNameWidth);
                strip.mute.setBounds(header.removeFromLeft(29).reduced(2, 0));
                strip.solo.setBounds(header.removeFromLeft(29).reduced(2, 0));
                strip.effects.button.setBounds(header.removeFromRight(48).reduced(2, 0));
                strip.meter.setBounds(header.reduced(8, 0).withSizeKeepingCentre(header.getWidth() - 16, 9));
                continue;
            }
            if (strip.size.expanded()) {
                auto top = header.removeFromTop(26);
                top.removeFromLeft(trackNameWidth);
                strip.mute.setBounds(top.removeFromLeft(29).reduced(2, 1));
                strip.solo.setBounds(top.removeFromLeft(29).reduced(2, 1));
                strip.effects.button.setBounds(top.removeFromRight(50).reduced(2, 1));
                strip.plugin.setBounds(top.reduced(2, 1));
                auto middle = header.removeFromTop(24);
                middle.removeFromLeft(trackNameWidth);
                strip.volume.setBounds(middle.removeFromLeft(128).reduced(3, 0));
                strip.pan.setBounds(middle.removeFromLeft(98).reduced(3, 0));
                strip.mix.setBounds(middle.reduced(2, 1));
                auto meter = header.removeFromLeft(171).reduced(6, 0);
                strip.meter.setBounds(meter.withHeight(meter.getHeight() >= 26 ? 24 : 10));
                strip.preset.setBounds(header.reduced(4, 0).withHeight(16));
                continue;
            }
            strip.meter.setBounds(header.withWidth(trackNameWidth).removeFromBottom(10).reduced(5, 1));
            auto row = header.withTrimmedLeft(trackNameWidth);
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
        y += arrangementHeadingHeight;
        for (std::size_t ordinal = 0; ordinal < outputs; ++ordinal) {
            const auto i = mixer_.routing().outputAt(ordinal);
            auto& strip = outputControls_[i];
            outputRows_[i] = {arrangement.getX(), y, arrangement.getWidth(), strip.size.height() - TrackRowSize::grip};
            y += strip.size.height();
            placeGrip(strip.size, outputRows_[i]);
            auto header = rowOverlay_.getLocalArea(this, outputRows_[i]).withWidth(trackHeaderWidth);
            auto top = header.removeFromTop(std::min(34, header.getHeight())).withTrimmedLeft(trackNameWidth);
            strip.mute.setBounds(top.removeFromLeft(29).reduced(2, 1));
            strip.effects.button.setBounds(top.removeFromRight(54).reduced(2, 1));
            strip.volume.setVisible(!strip.size.compact());
            if (!strip.size.compact()) strip.volume.setBounds(top.removeFromLeft(140).reduced(4, 0));
            strip.meter.setBounds(strip.size.expanded() ? header.reduced(6, 2).withHeight(24)
                              : top.reduced(5, 0).withSizeKeepingCentre(top.getWidth() - 10, 9));
        }
    }
    editor.removeFromTop(arrangerHeight + 8);
    const PianoLayout pianoLayout(editor.withTrimmedTop(30).withTrimmedBottom(timelineNavigationHeight));
    pianoGrid_.setBounds(pianoLayout.grid);
    pianoRuler_.setBounds(pianoLayout.ruler);
    pianoVelocity_.setBounds(pianoLayout.lane.withTrimmedTop(16));
    pianoVelocity_.setVisible(pianoGrid_.isVisible() && pianoGrid_.editable && performanceLane_.getSelectedId() == 1
                              && pianoLayout.lane.getHeight() >= 20);
    editor.reduce(6, 4);
    auto toolbar = editor.removeFromTop(26);
    if (midiDrag_.isVisible()) midiDrag_.setBounds(toolbar.removeFromRight(192));
    const auto pickerWidth = performanceLane_.isVisible() ? juce::jmin(180, toolbar.getWidth() / 2) : 180;
    trackPicker_.setBounds(toolbar.removeFromLeft(pickerWidth));
    toolbar.removeFromLeft(6);
    performanceLane_.setBounds(toolbar.removeFromLeft(250).withTrimmedRight(4));
    mixer_.setBounds(editor);
    auto pianoNavigation = editor.removeFromBottom(timelineNavigationHeight).reduced(7, 2);
    if (pianoSnap_.isVisible()) pianoSnap_.setBounds(pianoNavigation.removeFromRight(104).reduced(2, 0));
    if (pianoColours_.isVisible()) pianoColours_.setBounds(pianoNavigation.removeFromRight(std::min(158, pianoNavigation.getWidth() / 3)).reduced(2, 0));
    pianoZoomLabel_.setBounds(pianoNavigation.removeFromLeft(44));
    pianoZoom_.setBounds(pianoNavigation.removeFromLeft(162));
    pianoScroll_.setBounds(pianoNavigation.withTrimmedLeft(8).reduced(0, 3));
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(bg));
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
    juce::Graphics::ScopedSaveState clipped(g);
    if (project_) {
        area = rowOverlay_.getBounds();
        g.reduceClipRegion(area);
        area.translate(0, -juce::roundToInt(verticalScroll_.getCurrentRangeStart()));
    }
    const auto outputs = visibleOutputCount();
    const int laneHeight = project_ ? globalRowHeight : arrangementLaneHeight(area, view, outputs);
    auto labels = area.removeFromLeft(project_ ? trackHeaderWidth : 108);
    if (laneHeight < 12 || area.getWidth() < 40) return;
    int rowHeight = laneHeight - 4;

    auto drawRow = [&](const juce::String& label, int y) {
        g.setColour(juce::Colour(0xffe1e9ef));
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        const int labelHeight = project_ && rowHeight >= TrackRowSize::normal - TrackRowSize::grip
                              ? std::min(27, rowHeight - 10) : rowHeight;
        g.drawText(label,
                         juce::Rectangle<int>(labels.getX() + 5, y, std::min(trackNameWidth, labels.getWidth()) - 10, labelHeight),
                         juce::Justification::centredLeft, true);
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
            auto caption = bounds.reduced(4, 0);
            if (rowHeight >= 70 - TrackRowSize::grip) caption = caption.removeFromTop(18);
            g.setFont(juce::FontOptions(13.f));
            g.drawText(label, caption, juce::Justification::centredLeft);
        }
    };

    int y = area.getY() + laneHeight;
    const int globalLaneHeight = project_ ? groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)] : laneHeight;
    rowHeight = globalLaneHeight - 4;
    const juce::uint32 globalColours[] {0xff314d72, 0xff493c64, 0xff31495a, 0xff31404e, 0xff31404e, 0xff2c6058};
    bool drewTheory = false;
    const auto globalTheory = [&] {
        drawTheoryRows(g, juce::Rectangle<int>(labels.getX(), y, labels.getWidth() + area.getWidth(), theoryGuideHeight),
                       labels.getWidth(), std::nullopt);
        y += theoryGuideHeight;
        drewTheory = true;
    };
    for (std::size_t i = 0; i < view.globalLanes.size(); ++i) {
        drawLane(view.globalLanes[i], y, juce::Colour(globalColours[i % std::size(globalColours)]));
        y += globalLaneHeight;
        if (theoryVisible_ && project_ && view.globalLanes[i].kind == TimelineLane::chord) globalTheory();
    }
    if (theoryVisible_ && project_ && !drewTheory) globalTheory();

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
        drawLane(view.trackLanes[i], y, juce::Colour(trackColours[i % std::size(trackColours)]));
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
                const auto endBar = missing && audio->durationTicks <= 0 ? start + 1
                    : 1.0 + waveforms_.endTick(*audio, project_->clock) / project_->ticksPerBar;
                auto clip = clipBounds({start, endBar}, area.withY(y).withHeight(rowHeight)).reduced(0, 2);
                if (!clip.isEmpty()) {
                    g.setColour(juce::Colour(missing ? 0xff754842 : 0xff386d70));
                    g.fillRoundedRectangle(clip.toFloat(), 3);
                    auto body = clip.reduced(0, 2);
                    const auto labelArea = missing ? body : body.removeFromTop(std::min(14, body.getHeight() / 3));
                    if (!missing)
                        waveforms_.draw(g, body, *audio, project_->clock,
                                        (std::max(start, visible.start) - 1) * project_->ticksPerBar,
                                        (std::min(endBar, visible.end) - 1) * project_->ticksPerBar);
                    g.setColour(juce::Colours::white);
                    const auto label = missing ? TRANS("Missing WAV") : audio->path.fromLastOccurrenceOf("/", false, false);
                    if (clip.getWidth() >= 25)
                        g.drawText(label, labelArea.reduced(3, 0), juce::Justification::centredLeft);
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
    const auto bars = horizontalRange(false);
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
        g.drawText(name, header.removeFromLeft(trackNameWidth).reduced(4, 0), juce::Justification::centredLeft);
        g.setColour(juce::Colour(hardwareAmber));
        g.drawText(current, header.reduced(4, 0), juce::Justification::centredLeft);
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
        auto atCursor = [this](const auto& spans) -> const typename std::decay_t<decltype(spans)>::value_type* {
            auto it = std::partition_point(spans.begin(), spans.end(), [this](const auto& span) { return span.end <= cursorTick_; });
            return it != spans.end() && it->start <= cursorTick_ ? &*it : nullptr;
        };
        const auto* function = atCursor(theory_->functions);
        const auto* progression = atCursor(theory_->progressions);
        juce::String reading;
        if (function)
            reading = theory_->chords[function->chord].name + "   " + function->degree + "  " + juce::translate(function->function);
        const auto top = band(bounds.getY(), bounds.getHeight() / 2, TRANS("Function"), reading);
        const auto lower = band(top.getBottom(), bounds.getBottom() - top.getBottom(), TRANS("Progression"),
                                progression ? progression->label : juce::String());
        if (clip.isEmpty()) return;
        auto visibleSpans = [&](const auto& list, auto draw) {
            auto it = std::partition_point(list.begin(), list.end(), [first](const auto& f) { return f.end <= first; });
            for (; it != list.end() && it->start < last; ++it) draw(*it);
        };
        visibleSpans(theory_->functions, [&](const auto& f) {
            const auto label = f.degree.isEmpty() ? juce::String::charToString(0x2014) : f.degree + " " + juce::translate(f.function);
            auto colour = juce::Colour(0xff505667);
            if (f.function.startsWith("Tonic")) colour = juce::Colour(0xff306453);
            if (f.function == "Subdominant") colour = juce::Colour(0xff365979);
            if (f.function == "Dominant" || f.function == "Secondary dominant") colour = juce::Colour(0xff795338);
            cell(top, f.start, f.end, colour, label, f.degree);
        });
        visibleSpans(theory_->progressions, [&](const auto& p) {
            cell(lower, p.start, p.end, juce::Colour(0xff46556b), p.label, p.label.upToFirstOccurrenceOf(" ", false, false));
        });
        return;
    }
    if (*track >= theoryTracks_.size()) return;
    const auto& fits = theoryTracks_[*track];
    const auto currentCell = theory_->firstCell(double(cursorTick_));
    const auto height = bounds.getHeight() / std::max(1, int(chordFitVisible_) + int(scaleFitVisible_));
    int y = bounds.getY();
    for (const bool chord : {true, false}) {
        if (chord ? !chordFitVisible_ : !scaleFitVisible_) continue;
        auto referenceName = [&](const auto& c, const auto& f) -> juce::String {
            if (chord) return c.chord >= 0 ? theory_->chords[c.chord].name : juce::String();
            if (!f.noteScoped.empty()) return TRANS("Targeted scale");
            if (f.backgroundScale >= 0) return theory_->scales[f.backgroundScale].name;
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
            if (value >= 0) colour = juce::Colour(chord ? 0xff634c42 : 0xff4b4260).interpolatedWith(
                juce::Colour(chord ? 0xff28736f : 0xff426d91), value / 100.f);
            const auto next = chord ? f.chordEnd : i + 1;
            cell(row, chord ? theory_->cells[f.chordBegin].start : c.start, theory_->cells[next - 1].end, colour, label, shortLabel);
            i = next - 1;
        }
    }
}

void MainComponent::drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    const PianoLayout layout(area);
    const auto mode = pianoColours_.getSelectedId();
    const bool fit = theory_ && pitchedSelection() && mode != plainNotes;
    const bool showChord = mode == chordNotes || mode == chordAndScaleNotes;
    const bool showScale = mode == scaleNotes || mode == chordAndScaleNotes;
    auto heading = layout.heading;
    if (fit && heading.getWidth() > 330 && !heading.isEmpty()) {
        const juce::Font font(juce::FontOptions(13.f));
        g.setFont(font);
        for (const auto tone : {Tone::outside, Tone::scale, Tone::chord, Tone::guide, Tone::root}) {
            if ((tone == Tone::scale && !showScale) || (tone != Tone::outside && tone != Tone::scale && !showChord)) continue;
            const auto label = toneName(tone);
            const int width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, label)) + 25;
            if (heading.getWidth() < width + 120) break;
            auto entry = heading.removeFromRight(width);
            g.setColour(toneColour(tone));
            g.fillEllipse(entry.getX() + 2.f, entry.getCentreY() - 4.f, 8.f, 8.f);
            g.setColour(juce::Colours::white);
            g.drawText(label, entry.withTrimmedLeft(14), juce::Justification::centredLeft);
        }
    }
    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    const auto name = selectedTrack_ ? TRANS("PIANO ROLL  /  ") + juce::String::fromUTF8(project_->trackLanes[*selectedTrack_].label)
                                     : TRANS("PIANO ROLL");
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
    const auto visible = horizontalRange(true);
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

    const auto rowHeight = static_cast<float>(area.getHeight()) / static_cast<float>(high - low + 1);
    auto noteY = [&](int pitch) { return static_cast<float>(area.getY()) + static_cast<float>(high - pitch) * rowHeight; };
    auto tickX = [&](juce::int64 tick) {
        return static_cast<float>(area.getX() + (static_cast<double>(tick) - origin) / length * area.getWidth());
    };
    const auto dirty = area.getIntersection(g.getClipBounds());
    const double left = origin + length * (dirty.getX() - area.getX()) / area.getWidth();
    const double right = origin + length * (dirty.getRight() - area.getX()) / area.getWidth();
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

    if (fit) {
        const auto& fits = theoryTracks_[*selectedTrack_];
        for (auto i = theory_->firstCell(left); i < fits.size() && theory_->cells[i].start < right; ++i) {
            const auto& c = theory_->cells[i];
            const int scale = fits[i].backgroundScale;
            const auto* chord = showChord && c.chord >= 0 ? &theory_->chords[c.chord] : nullptr;
            const int scaleMask = showScale && scale >= 0 ? theory_->scales[scale].mask : 0;
            const auto x = std::max(float(area.getX()), tickX(c.start));
            const auto w = std::min(float(area.getRight()), tickX(c.end)) - x;
            if (w <= 0) continue;
            for (int p = low; p <= high; ++p) {
                const auto tone = theory_->tone(p, chord, scaleMask);
                if (tone == Tone::outside || tone == Tone::unknown) continue;
                g.setColour(toneColour(tone).withAlpha(tone == Tone::scale ? 0.14f : 0.24f));
                g.fillRect(x, noteY(p), w, rowHeight);
            }
        }
    }
    const auto step = pianoGrid_.grid;
    if (pianoGrid_.editable && step > 1 && double(step) * area.getWidth() / length >= 8) {
        g.setColour(juce::Colour(0xff354653));
        for (auto tick = static_cast<juce::int64>(std::ceil(origin / step)) * step; tick < origin + length; tick += step)
            g.drawVerticalLine(juce::roundToInt(tickX(tick)), float(area.getY()), float(area.getBottom()));
    }
    const auto barStep = std::max<juce::int64>(1, static_cast<juce::int64>((visible.end - visible.start) / (area.getWidth() < 420 ? 4 : 8)));
    for (auto bar = firstBar; bar < endBar; bar += barStep) {
        const auto x = juce::roundToInt(tickX(bar * barLength));
        g.setColour(juce::Colour(0xff526578));
        g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }

    g.saveState();
    g.reduceClipRegion(area);
    const auto firstNote = std::upper_bound(pianoMaxEnds_.begin(), pianoMaxEnds_.end(), left) - pianoMaxEnds_.begin();
    for (auto it = notes.begin() + firstNote; it != notes.end() && it->startTick < right; ++it) {
        const auto& note = *it;
        if (note.endTick <= left) continue;
        const auto x = tickX(note.startTick);
        const auto width = std::max(2.0f, tickX(note.endTick) - x);
        const auto height = std::max(1.0f, rowHeight - 1.0f);
        g.setColour(juce::Colour(fit ? 0xff818a92 : accent));
        g.fillRoundedRectangle(x, noteY(note.pitch), width, height, 2.0f);
        if (!fit || !note.enabled || note.fixedTrigger) continue;
        const auto& fits = theoryTracks_[*selectedTrack_];
        for (auto i = theory_->firstCell(std::max(left, double(note.startTick))); i < fits.size(); ++i) {
            const auto& c = theory_->cells[i];
            if (c.start >= note.endTick || c.start >= right) break;
            const auto a = tickX(std::max(note.startTick, c.start)), b = tickX(std::min(note.endTick, c.end));
            const int index = showScale ? theory_->scaleForNote(fits[i], note) : -1;
            const int scaleMask = index < 0 ? 0 : theory_->scales[index].mask;
            const auto* chord = showChord && theory_->chordMask(c) ? &theory_->chords[c.chord] : nullptr;
            auto rect = juce::Rectangle<float>(a, noteY(note.pitch), std::max(1.f, b-a), height);
            g.setColour(toneColour(theory_->tone(note.pitch, chord, scaleMask)));
            g.fillRect(rect);
            if (mode == chordAndScaleNotes && scaleMask && height >= 5.f) {
                g.setColour(toneColour(theory_->contains(scaleMask, note.pitch) ? Tone::scale : Tone::outside));
                g.fillRect(rect.removeFromBottom(2.f));
            }
            if (rect.getWidth() >= 33.f && rect.getHeight() >= 13.f && (chord || scaleMask)) {
                const int root = chord ? chord->root : theory_->scales[index].tonic;
                g.setFont(juce::FontOptions(13.f, juce::Font::bold));
                g.setColour(juce::Colour(0xff152632));
                g.drawText(theory_->interval(note.pitch, root), rect.toNearestInt().reduced(3, 0), juce::Justification::centredLeft);
            }
        }
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
