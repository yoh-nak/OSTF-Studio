#include "ui/Notation.h"
#include "ui/MainComponent.h"
#include "ui/InstrumentProgramWatch.h"
#include "ui/GlobalTimelineEdit.h"
#include "ui/ExternalAudioEdit.h"
#include "ui/AudioRegionEdits.h"
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
constexpr auto panel = 0xff242a31;
constexpr auto muted = 0xffaab6c4;
constexpr auto accent = 0xff46bdb2;
constexpr auto hardwareAmber = 0xffe7b46c;
constexpr int menuHeight = 28;
int toolbarButtonWidth(int width) { return std::clamp(width / 24, 24, 39); }

int viewBarHeight(int width) {
    const bool wrapIcons = width < 964;
    const int available = width - (wrapIcons ? 32 : 64) - (wrapIcons ? 9 : 23) * toolbarButtonWidth(width);
    const int rows = (wrapIcons ? 2 : 1) + (available < 394 ? 1 : 0);
    return 32 * rows;
}
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
    juce::Rectangle<int> heading, inspector, lane, divider, keys, ruler, chords, review, vocals, grid;
    PianoLayout(juce::Rectangle<int> area, int headerWidth, bool scrollbar, bool contextStrip,
                double parameterFraction, int keyWidth = 52, int reviewHeight = 0, bool showVocals = false) {
        const bool compact = area.getHeight() - reviewHeight < 130;
        area.reduce(13, compact ? 2 : 8);
        heading = area.removeFromTop(compact ? 0 : 24);
        if (scrollbar) area.removeFromRight(14);
        inspector = area.removeFromLeft(headerWidth - keyWidth);
        const int height = std::max(0, area.getHeight() - reviewHeight);
        const auto size = parameterFraction > 0 ? juce::roundToInt(height * std::clamp(parameterFraction, 0.08, 0.65))
                                                : std::min(84, height / 3);
        const int laneHeight = std::clamp(size, std::min(20, height / 3), std::max(height / 3, height - 70));
        const bool collapseLane = reviewHeight > 0 && laneHeight < 20;
        lane = area.removeFromBottom(collapseLane ? 0 : laneHeight).withTrimmedLeft(keyWidth);
        divider = area.removeFromBottom(std::min(collapseLane ? 3 : 7, area.getHeight() / 8)).withTrimmedLeft(keyWidth);
        keys = area.removeFromLeft(keyWidth);
        ruler = area.removeFromTop(compact ? 16 : 21);
        chords = area.removeFromTop(contextStrip && !(compact && reviewHeight > 0) ? (compact ? 13 : 21) : 0);
        review = area.removeFromTop(std::min(reviewHeight, area.getHeight()));
        if (review.getHeight() > 0) inspector.setTop(review.getBottom());
        if (showVocals) {
            vocals = area.removeFromTop(std::clamp(area.getHeight() - 42, 0, 54)).withLeft(inspector.getX());
            inspector.setTop(vocals.getBottom());
        }
        keys.removeFromTop(ruler.getHeight() + chords.getHeight() + review.getHeight() + vocals.getHeight());
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

bool stackTimelineNavigation(int innerWidth, int workspaceHeight) {
    return innerWidth < (workspaceHeight < 320 ? 500 : 765);
}

enum MenuCommand { openFile = 1, createProject, showPiano, showMixer, showDevices, showAbout, showPlugins,
                   saveScore, saveProject, showOutputs, addPitched, addDrums, addAudio, removeTrack,
                   exportMidi0, exportMidi1, languageSystem, languageEnglish, languageJapanese, showLeadSheet, showPartScore, importMedia, showArrangement, showSongInformation, addMelody, addChorus, addBus, addReturn, copyTrack, renameTrackItem, smallerUI, largerUI, showShortcuts, aiConnections, controllerSettings, initialPatchItem, cursorPatchItem, externalSplice, externalMelodyne, externalLink, linkTempoItem, studioArrangement, studioLyric, studioVocal, exportMusicXml, exportChordPro, showSamplesPane, moveRowUp, moveRowDown };
constexpr struct { const char* name; const char* first; const char* second; const char* third; } studioModes[] {
    {"AI Arrangement Studio", "Keep just piano and bass in the verse. Suggest an eight-bar build into the chorus.",
                              "Suggest two bass lines with different rhythms over these chords.",
                              "Suggest a two-bar drum fill leading into the last chorus."},
    {"AI Lyric Studio", "Write four lines for a chorus about a train station at dawn.",
                        "Keep the meaning of this verse and give each line eight syllables.",
                        "Give me three closing lines that echo the opening image."},
    {"AI Vocal Studio", "Suggest places to breathe in this melody.",
                        "Add a harmony voice just for the last two bars of the chorus.",
                        "Mark the words to emphasize in the first vocal phrase."}
};
static_assert(std::size(studioModes) == studioVocal - studioArrangement + 1);
constexpr int firstThemeId = 89;
constexpr int themeCount = static_cast<int>(std::size(ostfstudio::console::finishes));
constexpr int timelineMenuBase = 100;
constexpr int showTheory = 80, showChordFit = 81, showScaleFit = 82, scaleReferenceBase = 1000;
constexpr int musicSettings = 83, musicTransform = 85, musicAudition = 86, undoScore = 87, redoScore = 88;
constexpr int demoMenuBase = 300;
constexpr struct { const char* title; const char* resource; } gallery[] {
    {"Neon Platforms (City Pop)", "neonplatforms_ostfp"},
    {"Blue Hour Waltz (Jazz Waltz)", "bluehourwaltz_ostfp"},
    {"Blue Hour Waltz (Shared GM)", "bluehourwaltzsharedgm_ostfp"},
    {"Relay Circuit (Techno)", "relaycircuit_ostfp"},
    {"Paper Lantern Replies (R&B / Japanese vocals)", "paperlanternreplies_ostfp"},
    {"Copper Mile (Rock)", "coppermile_ostfp"},
    {"First Light Vector (Trance)", "firstlightvector_ostfp"},
};
using ostfstudio::TimelineLane;
constexpr TimelineLane selectableTimelineLanes[] {TimelineLane::section, TimelineLane::chord, TimelineLane::voicing,
    TimelineLane::key, TimelineLane::scale, TimelineLane::meter, TimelineLane::tempo,
    TimelineLane::displayLyrics, TimelineLane::pronunciation};
constexpr const char* timelineMenuLabels[] {"Sections", "Chords", "Chord Voicings", "Keys", "Scales", "Meter", "Tempo",
    "Display lyrics", "Pronunciation"};
static_assert(std::size(selectableTimelineLanes) == std::size(timelineMenuLabels));

constexpr unsigned laneBit(TimelineLane kind) { return 1u << static_cast<unsigned>(kind); }

juce::String vocalDepthKind(const juce::String& lane) {
    if (lane == "Vibrato depth") return "vibrato";
    if (lane == "Scoop depth") return "scoop";
    return {};
}

juce::var instrumentReference(const juce::PluginDescription& description) {
    auto ref = std::make_unique<juce::DynamicObject>();
    ref->setProperty("format", "vst3");
    ref->setProperty("pluginId", juce::String::toHexString(description.uniqueId));
    ref->setProperty("namespace", "org.juce.vst3.uid");
    ref->setProperty("name", description.name);
    if (description.manufacturerName.isNotEmpty()) ref->setProperty("vendor", description.manufacturerName);
    return juce::var(ref.release());
}

template <class Matches>
const juce::PluginDescription* findInstrument(const juce::Array<juce::PluginDescription>& types, Matches matches) {
    const auto folder = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    const juce::PluginDescription* first = nullptr;
    for (const auto& type : types) if (matches(type)) {
        if (type.manufacturerName == "OSTF Studio" && juce::File(type.fileOrIdentifier).getParentDirectory() == folder)
            return &type;
        if (!first) first = &type;
    }
    return first;
}

void showStudioWindow(juce::DocumentWindow& window, juce::Component& owner) {
    if (!window.isOnDesktop()) {
        window.setAssociatedComponent(&owner);
        window.addToDesktop();
    }
    window.setVisible(true);
    window.toFront(true);
}

class PluginWindow final : public juce::DocumentWindow {
public:
    explicit PluginWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colour(panel), DocumentWindow::closeButton, false) {
        setUsingNativeTitleBar(true);
    }
    std::function<void()> onClosed;
    std::unique_ptr<ostfstudio::InstrumentProgramWatch> programWatch;
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
            row.open.onClick = [this, edit, index] {
                if (auto* popup = findParentComponentOfClass<juce::CallOutBox>()) {
                    popup->exitModalState(0);
                    popup->setVisible(false);
                }
                edit(index);
            };
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
        volume_.setTooltip(TRANS("Volume"));
        pan_.setTooltip(TRANS("Pan"));
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
        timelineLaneMask_ = static_cast<unsigned>(config->getIntValue("timelineLanesWithLyrics",
            config->getIntValue("timelineLanes", 0xff) | laneBit(TimelineLane::displayLyrics) | laneBit(TimelineLane::pronunciation)));
        for (int i = 0; i < themeCount; ++i)
            if (config->getValue("workspaceTheme") == ostfstudio::console::finishes[i].key)
                pianoInspector_.theme = static_cast<pianoHarmony::Theme>(i);
        parameterFraction_ = config->getDoubleValue("parameterLaneRatio", 0);
        joinKeepsRight_ = config->getBoolValue("globalJoinKeepsRight", false);
        theoryVisible_ = config->getBoolValue("theoryGuide", false);
        chordFitVisible_ = config->getBoolValue("showChordFit", true);
        scaleFitVisible_ = config->getBoolValue("showScaleFit", true);
    }
    consoleStyle_.setFinish(finish());
    setLookAndFeel(&consoleStyle_);
    setSize(1100, 690);
    menuBar_.setModel(this);
    menuBar_.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff20262d));
    addAndMakeVisible(menuBar_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(deck_);
    basics_.commit = [this](auto field, auto value, int choice) { editSongField(field, value, choice); };
    basics_.advanced = [this] { editSongInformation(); };
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
    addView(sampleBrowserButton_, "Media library", showSamplesPane);
    sampleBrowserButton_.setToggleable(true);
    addView(outputButton_, "Output Tracks", showOutputs);
    addView(lanesButton_, "Global Timeline Lanes", 0);
    addView(arrangementButton_, "Arrangement", showArrangement);
    addView(leadButton_, "Lead sheet", showLeadSheet);
    addView(scoreButton_, "Score", showPartScore);
    addView(songInfoButton_, "Song information", showSongInformation);
    addView(chordFitButton_, "Chord fit", showChordFit);
    addView(scaleFitButton_, "Scale fit", showScaleFit);
    chordFitButton_.setToggleable(true); scaleFitButton_.setToggleable(true);
    addView(theoryButton_, "Show Theory Guide", showTheory);
    theoryButton_.setComponentID("theory-guide");
    theoryButton_.setToggleable(true);
    addView(musicButton_, "Transpose and tempo", musicSettings);
    musicButton_.setComponentID("song-settings");
    addView(melodyAdd_, "Add melody track", addMelody);
    addView(chorusAdd_, "Add chorus vocal track", addChorus);
    addView(pitchedAdd_, "Add pitched track", addPitched);
    addView(drumsAdd_, "Add drum / percussion track", addDrums);
    addView(audioAdd_, "Add audio track", addAudio);
    addView(busAdd_, "Add bus track", addBus);
    addView(returnAdd_, "Add return track", addReturn);
    addView(splitTool_, "Scissors: split at click", 0);
    addView(joinTool_, "Join with next clip", 0);
    splitTool_.setToggleable(true);
    joinTool_.setToggleable(true);
    auto chooseTool = [this](ClipTool tool) {
        clipTool_ = clipTool_ == tool ? ClipTool::select : tool;
        splitTool_.setToggleState(clipTool_ == ClipTool::split, juce::dontSendNotification);
        joinTool_.setToggleState(clipTool_ == ClipTool::join, juce::dontSendNotification);
    };
    addChildComponent(chordReview_);
    chordReview_.timeline = [this] {
        if (pianoGrid_.isVisible() && pianoGrid_.getWidth() && pianoGrid_.tickRange)
            return std::pair{chordReview_.getLocalArea(&pianoGrid_, pianoGrid_.getLocalBounds()), pianoGrid_.tickRange()};
        const auto range = horizontalRange();
        const double ticks = project_ ? double(project_->ticksPerBar) : 1.0;
        auto plot = chordReview_.getLocalArea(&rowOverlay_, rowOverlay_.getLocalBounds().withTrimmedLeft(trackHeaderWidth));
        return std::pair{plot, juce::Range<double>((range.start-1)*ticks,(range.end-1)*ticks)};
    };
    chordReview_.choose = [this](int choice){
        const int index = chordReview_.selected();
        if (juce::isPositiveAndBelow(index,int(pendingReview_.size()))) pendingReview_[size_t(index)].choice = choice;
        refreshChordReview();
    };
    chordReview_.decide = [this](bool all, ostfstudio::ChordReviewAction action) { resolveChordReview(all, action); };
    splitTool_.onClick = [chooseTool] { chooseTool(ClipTool::split); };
    joinTool_.onClick = [chooseTool] { chooseTool(ClipTool::join); };
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
    deck_.play.setTooltip(TRANS("Play"));
    deck_.stop.setTooltip(TRANS("Stop"));
    deck_.record.setTooltip(TRANS("Arm a track with R, then record from the cursor."));
    deck_.end.setTooltip(TRANS("Go to end of song"));
    deck_.home.onClick = [this] { navigateTransport(TransportKey::home); };
    deck_.rewind.onClick = [this] { navigateTransport(TransportKey::rewind); };
    deck_.end.onClick = [this] { navigateTransport(TransportKey::end); };
    deck_.forward.onClick = [this] { navigateTransport(TransportKey::forward); };
    deck_.follow.onClick = [this] { displayTransportPosition(cursorTick_); };
    addChildComponent(trackPicker_);
    addChildComponent(performanceLane_);
    performanceLane_.setComponentID("performance-picker");
    performanceLane_.setTitle(TRANS("Performance lane"));
    performanceLane_.onChange = [this] { pianoVelocity_.cancel(); refreshControllerLane(); resized(); repaint(); };
    addChildComponent(enterPerformance_);
    enterPerformance_.setComponentID("performance-value-entry");
    enterPerformance_.onClick = [this] { enterPerformanceValue(); };
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
    addChildComponent(vocalLane_);
    vocalLane_.secondsAt = [this](juce::int64 tick) { return project_->clock.secondsAt(tick); };
    vocalLane_.lookup = [this](const juce::String& id) -> const ostfstudio::VocalNote* {
        if (!project_) return nullptr;
        auto it = project_->vocalNotes.find(id);
        return it != project_->vocalNotes.end() ? &it->second : nullptr;
    };
    vocalLane_.edit = [this](const auto& n) { editVocalNote(n); };
    vocalLane_.entrance.onClick = [this] {
        for (const auto& n : pianoGrid_.notes) {
            const auto* v = vocalLane_.lookup(n.id);
            if (n.enabled && v && v->text.isNotEmpty()) {
                locateTransport(n.startTick);
                arrangementScroll_.setCurrentRangeStart(1.0 + n.startTick / project_->ticksPerBar);
                break;
            }
        }
    };
    vocalLane_.voice.onClick = [this] { if (selectedTrack_) useTrackSinger(*selectedTrack_,false); };
    vocalLane_.overlay.onClick = [this] { repaint(); };
    pianoGrid_.editLyrics = [this](const auto& n) { if (vocalLane_.isVisible()) editVocalNote(n); };
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
        const bool mixer = mixerButton_.getToggleState();
        auto& fraction = mixer ? mixerSplitFraction_ : arrangementFraction_;
        fraction = std::clamp(double(y - workspace.getY()) / std::max(1, workspace.getHeight()), 0.18, 0.82);
        if (auto* prefs = viewPreferences_.getUserSettings()) prefs->setValue(mixer ? "mixerSplitFraction" : "arrangementFraction", fraction);
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
    if (settings) {
        preferredBrowserWidth_ = settings->getIntValue("pluginBrowserWidth", 230);
    }
    if (settings) preferredHeaderWidth_ = std::clamp(settings->getIntValue("trackHeaderWidth", 487), 400, 760);
    if (settings) mixerSplitFraction_ = std::clamp(settings->getDoubleValue("mixerSplitFraction", .5), .18, .82);
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
    addChildComponent(clipLane_);
    clipLane_.setTitle(TRANS("Clips"));
    clipLane_.context = [this](double bar) { if (selectedTrack_) midiClipMenu(*selectedTrack_, bar); };
    clipLane_.beginResize = [this](double bar, double tolerance) { return selectedTrack_ && beginMidiResize(*selectedTrack_, bar, tolerance); };
    clipLane_.resizeLoop = [this](double bar, bool finish) { dragMidiResize(bar, finish); };
    clipLane_.visible = [this] { return arrangementScroll_.getCurrentRange(); };
    clipLane_.select = [this](std::optional<double> bar) { selectEditorClip(bar, true); };
    addChildComponent(pianoRuler_);
    rowOverlay_.addAndMakeVisible(ruler_);
    rowOverlay_.addAndMakeVisible(promptGuide_);
    studioModeIndex_ = std::clamp(viewPreferences_.getUserSettings()->getIntValue("aiStudioMode", 1), 1, int(std::size(studioModes))) - 1;
    updatePromptGuide();
    addAndMakeVisible(aiButton_);
    aiButton_.setComponentID("chatgpt-connection");
    addChildComponent(modelPicker_);
    addChildComponent(fetchModels_);
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
        if (auto* clip = audioEditor_.sourceClip()) selectEditorClip(1. + double(clip->startTick) / project_->ticksPerBar, false);
    };
    audioEditor_.onMelodyne = [this] { audioEditor_.selectWave(); editMelodyneCopy(); };
    audioEditor_.onApply = [this] { applyMelodyneCopy(); };
    audioEditor_.hasWorkingCopy = [this] {
        return project_ && selectedTrack_ && audioEditor_.sourceClip() && melodyneCopies_.contains(audioEditor_.sourceClip()->regionId);
    };
    audioEditor_.onEdit = [this](const ostfstudio::ClipTiming& edit) {
        auto* clip = selectedTrack_ ? clipOn(*selectedTrack_) : nullptr;
        if (!clip || recordTrack_) return;
        const bool playing = transportActive_;
        if (playing) stopTransport();
        if (!edit.loop && edit.cycleTicks && edit.cycleTicks != clip->timing.cycleTicks) {
            clip->durationTicks = std::clamp<juce::int64>(edit.leadingTicks + edit.cycleTicks, 1, project_->playbackEndTick() - clip->startTick);
        }
        const bool newWarpLength = edit.cycleTicks != clip->timing.cycleTicks;
        clip->timing = edit;
        if (newWarpLength) clip->timing.sourceLimitTicks = 0;
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
    };
    mixer_.cueMonitorChanged = [this](bool listen) { transport_.setListenToCue(listen); };
    mixer_.routingChanged = [this] {
        markSessionEdited();
        for (std::size_t i = 0; i < outputControls_.size(); ++i) {
            outputControls_[i].volume.setValue(mixer_.outputVolume(i), juce::dontSendNotification);
            outputControls_[i].mute.setToggleState(mixer_.routing().outputMuted[i], juce::dontSendNotification);
        }
        updateTransportMix();
    };
    mixer_.requestAddOutput = [this](bool isReturn) { editOutput(isReturn); };
    mixer_.rowMenu = [this](bool output, std::size_t index) { showRowMenu(output, index); };
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
    statusLabel_.setFont(juce::FontOptions(13.0f));
    refreshTimelineLanes();
    newProject();
}

juce::StringArray MainComponent::getMenuBarNames() { return {TRANS("File"), TRANS("View"), TRANS("Track"), TRANS("Mode"), TRANS("Settings"), TRANS("External connections"), TRANS("Help")}; }

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
    menu.addItem(musicTransform, TRANS("Transpose or change tempo of score..."), available);
    menu.addItem(musicAudition, TRANS("Playback and display overrides..."), available, auditionShift_ || auditionRate_ != 1);
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
    if (theory_ && !theory_->scales.empty()) {
        juce::PopupMenu refs;
        refs.addItem(scaleReferenceBase, TRANS("Automatic"), true, scaleReference_ == -1);
        for (int i = 0; i < static_cast<int>(theory_->scales.size()); ++i)
            refs.addItem(scaleReferenceBase + 1 + i, theory_->scales[i].name, true, scaleReference_ == i);
        menu.addSubMenu(TRANS("Scale reference"), refs, scaleFitVisible_ || pianoColours_.getSelectedId() >= scaleNotes);
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
                demos.addItem(demoMenuBase + static_cast<int>(i), juce::translate(gallery[i].title));
            menu.addSubMenu(TRANS("Demo songs"), demos);
            menu.addItem(showSongInformation, TRANS("Song information..."), project_ != nullptr);
            menu.addSubMenu(TRANS("Transpose and tempo"), musicMenu(), project_ != nullptr);
            menu.addSeparator();
            menu.addItem(undoScore, TRANS("Undo score edit"), project_ && !recordTrack_ && noteHistoryCursor_ > 0);
            menu.addItem(redoScore, TRANS("Redo score edit"), project_ && !recordTrack_ && noteHistoryCursor_ < noteHistory_.size());
            menu.addSeparator();
            menu.addItem(saveScore, TRANS("Save as OSTF..."), canEditTracks());
            menu.addItem(saveProject, TRANS("Save as OSTFP..."), canEditTracks());
            juce::PopupMenu midi;
            midi.addItem(exportMidi0, TRANS("Format 0 - single track"));
            midi.addItem(exportMidi1, TRANS("Format 1 - separate tracks"));
            menu.addSubMenu(TRANS("Save as MIDI (SMF)"), midi, project_ != nullptr);
            menu.addSeparator();
            menu.addItem(exportMusicXml, TRANS("Export MusicXML..."), project_ != nullptr);
            menu.addItem(exportChordPro, TRANS("Export ChordPro..."), project_ != nullptr);
            menu.addSeparator();
            menu.addItem(importMedia, TRANS("Import and transcription"));
            break;
        }
        case 1: {
            menu.addItem(showArrangement, TRANS("Arrangement"), true, workspaceMode_ == WorkspaceMode::arrangement);
            menu.addItem(showPiano, TRANS("Piano Roll"), project_ != nullptr, pianoButton_.getToggleState());
            menu.addItem(showMixer, TRANS("Mixer"), project_ != nullptr, mixer_.isVisible());
            menu.addItem(showSamplesPane, TRANS("Media library"), true, sampleBrowserButton_.getToggleState());
            menu.addItem(showPlugins, TRANS("Plug-in Browser"), true, pluginBrowserButton_.getToggleState());
            menu.addItem(showOutputs, TRANS("Output Tracks"), outputButton_.isEnabled(), outputButton_.getToggleState());
            menu.addSubMenu(TRANS("Global Timeline Lanes"), timelineMenu(), lanesButton_.isEnabled());
            menu.addSubMenu(TRANS("Theory Guide"), theoryMenu(), lanesButton_.isEnabled());
            menu.addItem(showChordFit, TRANS("Chord fit"), project_ != nullptr, chordFitVisible_);
            menu.addItem(showScaleFit, TRANS("Scale fit"), project_ != nullptr, scaleFitVisible_);
            menu.addSeparator();
            menu.addItem(showLeadSheet, TRANS("Lead sheet"), project_ != nullptr, leadButton_.getToggleState());
            menu.addItem(showPartScore, TRANS("Score"), project_ != nullptr, scoreButton_.getToggleState());
            break;
        }
        case 3:
            for (int i = 0; i < int(std::size(studioModes)); ++i)
                menu.addItem(studioArrangement + i, studioModes[i].name, true, i == studioModeIndex_);
            break;
        case 4: {
            menu.addItem(showDevices, TRANS("Audio / MIDI Devices..."));
            menu.addItem(aiConnections, TRANS("AI connections..."));
            menu.addItem(controllerSettings, "Mackie Control...");
            juce::PopupMenu themes;
            for (int i = 0; i < themeCount; ++i)
                themes.addItem(firstThemeId + i, juce::translate(ostfstudio::console::finishes[i].name),
                               true, static_cast<int>(pianoInspector_.theme) == i);
            menu.addSubMenu(TRANS("Colour theme"), themes);
            juce::PopupMenu languages;
            const auto selected = viewPreferences_.getUserSettings()->getValue("language", "system");
            languages.addItem(languageSystem, TRANS("Use system language"), true, selected == "system");
            languages.addItem(languageEnglish, "English", true, selected == "en");
            languages.addItem(languageJapanese, juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"), true, selected == "ja");
            menu.addSubMenu(TRANS("Language"), languages);
            break;
        }
        case 5:
            menu.addItem(externalSplice, TRANS("Open Splice Sounds"));
            menu.addItem(externalMelodyne, "Melodyne...", !recordTrack_);
            menu.addSeparator();
            menu.addItem(externalLink, "Ableton Link", !recordTrack_, link_ != nullptr);
            if (link_) {
                const auto state = link_->state(linkQuantum());
                menu.addItem(linkTempoItem, TRANS("Link tempo") + "  " + juce::String(state.tempo, 2) + " BPM   /   " +
                             juce::String(int(state.peers)) + " " + TRANS("peers") + "...", !recordTrack_);
            }
            break;
        case 6:
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
    if (item >= studioArrangement && item <= studioVocal) {
        studioModeIndex_ = item - studioArrangement;
        viewPreferences_.getUserSettings()->setValue("aiStudioMode", studioModeIndex_ + 1);
        updatePromptGuide();
        applyAiSelection();
        menuItemsChanged();
        return;
    }
    if (item >= firstThemeId && item < firstThemeId + themeCount) {
        pianoInspector_.theme = static_cast<pianoHarmony::Theme>(item - firstThemeId);
        consoleStyle_.setFinish(finish());
        sendLookAndFeelChange();
        if (auto* config = viewPreferences_.getUserSettings()) {
            config->setValue("workspaceTheme", finish().key);
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
    if (item == musicTransform || item == musicAudition) {
        editSongMusic(item == musicTransform ? ostfstudio::SongMusicEdit::transform : ostfstudio::SongMusicEdit::audition); return;
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
        refreshTheory();
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
        if (auto* config = viewPreferences_.getUserSettings()) config->setValue("timelineLanesWithLyrics", static_cast<int>(timelineLaneMask_));
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
        case exportMusicXml: saveDocument(SaveFormat::musicXml); break;
        case exportChordPro: saveDocument(SaveFormat::chordPro); break;
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
            ensureDeviceSettings().devices();
            showStudioWindow(*deviceSettings_, *this);
            break;
        case showSamplesPane: togglePluginBrowser(true); break;
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

DeviceSettingsWindow& MainComponent::ensureDeviceSettings() {
    if (!deviceSettings_)
        deviceSettings_ = std::make_unique<DeviceSettingsWindow>([this](int count) { mixer_.setActiveOutputCount(count); });
    return *deviceSettings_;
}

PluginBrowser& MainComponent::ensurePluginBrowser() {
    if (!pluginBrowser_) {
        pluginBrowser_ = std::make_unique<PluginBrowser>();
        pluginBrowser_->onTabChanged = [this](bool samples) {
            pluginBrowserButton_.setToggleState(!samples, juce::dontSendNotification);
            sampleBrowserButton_.setToggleState(samples, juce::dontSendNotification);
        };
        addChildComponent(*pluginBrowser_);
    }
    return *pluginBrowser_;
}

void MainComponent::togglePluginBrowser(bool samples) {
    auto& browser = ensurePluginBrowser();
    const bool shown = !browser.isVisible() || !(samples ? sampleBrowserButton_ : pluginBrowserButton_).getToggleState();
    if (shown) {
        browser.showSamples(samples);
        if (!samples && !browser.hasScanCache()) browser.scan();
    } else {
        sampleBrowserButton_.setToggleState(false, juce::dontSendNotification);
        pluginBrowserButton_.setToggleState(false, juce::dontSendNotification);
    }
    browser.setVisible(shown);
    resized();
    repaint();
}

juce::Rectangle<int> MainComponent::getWorkspace() const {
    auto bounds = workspaceBounds(getLocalBounds(), topBasicsHeight());
    if (pluginBrowser_ && pluginBrowser_->isVisible()) bounds.removeFromRight(browserWidthFor(bounds.getWidth(), preferredBrowserWidth_) + columnGripWidth);
    return bounds;
}

int MainComponent::getArrangementHeight(int workspaceHeight) const {
    const int available = std::max(0, workspaceHeight - chordReviewHeight());
    if (!editorVisible_) return available;
    const int navigationRows = stackTimelineNavigation(getWorkspace().getWidth() - 26, workspaceHeight) ? 2 : 1;
    const bool compactClips = available < 320 && clipLane_.isVisible();
    const int minimum = std::min(available, (compactClips ? 2 : 26) + navigationRows * timelineNavigationHeight
                                              + timelineRowHeight() + TrackRowSize::minimum);
    const double fraction = mixerButton_.getToggleState() ? mixerSplitFraction_ : arrangementFraction_;
    const int editorMinimum = (clipLane_.isVisible() ? 160 : 136) + (vocalLane_.isVisible() ? 84 : 0);
    return std::clamp(juce::roundToInt(workspaceHeight * fraction), minimum, std::max(minimum, available - editorMinimum));
}

void MainComponent::scrollBarMoved(juce::ScrollBar* bar, double) {
    clipLane_.repaint();
    if (bar == &arrangementScroll_ && chordReview_.isVisible()) chordReview_.resized();
    chordReview_.repaint();
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
    if (chordReview_.isVisible()) { chordReview_.resized(); chordReview_.repaint(); }
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
        if (project_->chordVoicingTrack != i || (timelineLaneMask_ & laneBit(TimelineLane::voicing)) != 0)
            order.push_back(i);
    const auto rank = [this](size_t i) {
        if (project_->primaryMelodyTrack == i) return std::pair{0, 0};
        if (project_->chordVoicingTrack == i) return std::pair{1, 0};
        if (!canReorderTrack(i)) return std::pair{2, 0};
        const auto position = arrangementOrder_.indexOf(project_->trackIds[int(i)]);
        return std::pair{project_->isGlobalNoteTrack(i) ? 3 : 4, position < 0 ? arrangementOrder_.size() : position};
    };
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return rank(a) < rank(b); });
    return order;
}

bool MainComponent::canReorderTrack(size_t i) const {
    return project_ && i < project_->sourceTrackCount() && project_->primaryMelodyTrack != i &&
        (!project_->isGlobalNoteTrack(i) || project_->mixerGroups[i] == ostfstudio::MixerTrackGroup::vocal);
}

bool MainComponent::canReorderTogether(size_t source, size_t target) const {
    return source != target && canReorderTrack(source) && canReorderTrack(target) &&
        project_->isGlobalNoteTrack(source) == project_->isGlobalNoteTrack(target);
}

juce::Rectangle<int> MainComponent::trackOrderHandle(size_t i) const {
    if (!canReorderTrack(i) || i >= trackRows_.size() || trackRows_[i].isEmpty()) return {};
    auto header = trackRows_[i].withWidth(trackNameWidth);
    return header.removeFromTop(24).removeFromRight(18);
}

std::optional<size_t> MainComponent::adjacentMovableTrack(size_t index, int direction) const {
    if (!canReorderTrack(index)) return {};
    const auto order = displayedTrackOrder();
    const auto it = std::find(order.begin(), order.end(), index);
    if (it == order.end() || (direction < 0 ? it == order.begin() : std::next(it) == order.end())) return {};
    const auto other = *(it + direction);
    if (!canReorderTogether(index, other)) return {};
    return other;
}

void MainComponent::moveTrackRow(size_t index, int direction) {
    if (const auto other = adjacentMovableTrack(index, direction))
        placeTrackRow(index, *other, direction > 0);
}

void MainComponent::placeTrackRow(size_t source, size_t target, bool after) {
    if (!canReorderTogether(source, target)) return;
    auto order = displayedTrackOrder();
    auto from = std::find(order.begin(), order.end(), source);
    auto to = std::find(order.begin(), order.end(), target) + int(after);
    if (from == to || from + 1 == to) return;
    if (from < to) std::rotate(from, from + 1, to);
    else std::rotate(to, from, from + 1);
    arrangementOrder_.clear();
    for (auto i : order)
        if (i < project_->sourceTrackCount()) arrangementOrder_.add(project_->trackIds[int(i)]);
    markSessionEdited();
    resized();
    repaint();
}

bool MainComponent::isInterestedInDragSource(const SourceDetails& details) {
    const auto id = details.description.toString();
    return workspaceMode_ == WorkspaceMode::arrangement && project_ &&
           (dynamic_cast<ImportResultRow*>(details.sourceComponent.get()) || id == generatorDragId ||
            (pluginBrowser_ && (id.startsWith("ostf-vst3:") || (id.startsWith("ostf-media:") && !recordTrack_))));
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
    if (id.startsWith("ostf-media:") && track && !juce::File(id.fromFirstOccurrenceOf(":", false, false)).hasFileExtension("mid;midi;ostf;ostfp")
        && project_->trackKinds[*track] != ostfstudio::TrackKind::audio) track.reset();
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
    if (id.startsWith("ostf-media:")) {
        filesDropped({id.fromFirstOccurrenceOf(":",false,false)}, details.localPosition.x, details.localPosition.y);
        return;
    }
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
                                bool replace, std::shared_ptr<const ostfstudio::OstfpPreview> dropped, bool review) {
    if (!project_ || recordTrack_ || chordReviewBlocksSwitch())
        return;
    bool playing = transportActive_;
    if (playing)
        stopTransport();
    juce::MemoryBlock before;
    if (review && row.index < 0) {
        reviewDroppedChords(row.source,row.index,tick);
        if (playing) startTransport(false);
        return;
    }
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
        if (review) {
            reviewDroppedChords(row.source,row.index,tick);
            if (row.index >= 0 && row.source.trackKinds[row.index] != ostfstudio::TrackKind::audio && !noteHistory_.empty()) {
                noteHistory_.back().reviewBefore = ostfstudio::ChordDecisions{};
                noteHistory_.back().reviewAfter = pendingReview_;
                noteHistory_.back().reviewTrack = track;
            }
        }
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
            juce::ModalCallbackFunction::create([safe, source, generation, track, tick, dropped, review, index = row.index](int answer) {
                if (!answer || !safe || safe->projectGeneration_ != generation) return;
                if (dropped) {
                    ImportResultRow part({}, *dropped, index);
                    safe->placeResult(part, track, tick, true, dropped, review);
                } else if (source) safe->placeResult(*source, track, tick, true);
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
        surfaceWindow_ = std::make_unique<PluginWindow>("Mackie Control");
        surfacePanel_ = new ControllerSurface();
        surfaceWindow_->setContentOwned(surfacePanel_, true);
        surfaceWindow_->centreWithSize(450, 154);
        surfacePanel_->selected = [this](juce::MidiDeviceInfo port) {
            connectController(port);
            if (mackiePort_ == port.identifier)
                viewPreferences_.getUserSettings()->setValue("mackieInput", port.identifier);
        };
        surfacePanel_->selectOutput = [this](const juce::String& id) {
            connectMackieOutput(id);
            if (id.isEmpty() || (mackieOutput_ && mackieOutput_->getIdentifier() == id))
                viewPreferences_.getUserSettings()->setValue("mackieOutput", id);
        };
        surfacePanel_->feedback = [this] { updateMackieFeedback(); };
        auto restore = [this] {
            const auto outputId = viewPreferences_.getUserSettings()->getValue("mackieOutput");
            juce::String availableOutput;
            for (const auto& port : juce::MidiOutput::getAvailableDevices())
                if (port.identifier == outputId) availableOutput = outputId;
            connectMackieOutput(availableOutput);
            const auto saved = viewPreferences_.getUserSettings()->getValue("mackieInput");
            juce::MidiDeviceInfo match;
            for (const auto& port : juce::MidiInput::getAvailableDevices())
                if (port.identifier == saved) match = port;
            if (match.identifier != mackiePort_) connectController(match);
            surfacePanel_->refreshPorts(mackiePort_);
        };
        restore();
        surfaceDevices_ = juce::MidiDeviceListConnection::make(restore);
    }
    if (show) showStudioWindow(*surfaceWindow_, *this);
}

void MainComponent::connectController(juce::MidiDeviceInfo port) {
    if (recordTrack_) { surfacePanel_->refreshPorts(mackiePort_); return; }
    auto& devices = ensureDeviceSettings().midiDevices();
    const bool opened = port.identifier == mackiePort_ ? openedSurfacePort_
        : port.identifier.isNotEmpty() && !devices.isMidiInputDeviceEnabled(port.identifier);
    if (port.identifier.isNotEmpty()) {
        devices.setMidiInputDeviceEnabled(port.identifier, true);
        if (!devices.isMidiInputDeviceEnabled(port.identifier)) {
            surfacePanel_->refreshPorts(mackiePort_);
            surfacePanel_->status.setText(TRANS("Could not open MIDI input."), juce::dontSendNotification);
            return;
        }
    }
    if (openedSurfacePort_ && port.identifier != mackiePort_ &&
        std::none_of(midiRoutes_.begin(), midiRoutes_.end(), [this](const auto& entry) {
            return entry.second.enabled && entry.second.port == mackiePort_;
        })) devices.setMidiInputDeviceEnabled(mackiePort_, false);
    openedSurfacePort_ = opened;
    {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        mackiePort_ = port.identifier;
        surfaceCount_ = 0;
    }
    mackieBank_ = 0;
    surfacePanel_->status.setText(port.name, juce::dontSendNotification);
    surfacePanel_->refreshPorts(mackiePort_);
    attachPadInput(project_ != nullptr || mackiePort_.isNotEmpty());
    if (armedId_.isNotEmpty()) restartInputMonitor();
}

void MainComponent::connectMackieOutput(const juce::String& id) {
    if (!mackieOutput_ || mackieOutput_->getIdentifier() != id) {
        auto output = id.isEmpty() ? nullptr : juce::MidiOutput::openDevice(id);
        if (id.isEmpty() || output) {
            mackieOutput_ = std::move(output);
            mackieFeedback_.fill(-1);
        } else surfacePanel_->status.setText(TRANS("Could not open MIDI output."), juce::dontSendNotification);
    }
    surfacePanel_->refreshOutputs(mackieOutput_ ? mackieOutput_->getIdentifier() : juce::String{});
}

void MainComponent::updateMackieFeedback() {
    if (!mackieOutput_) return;
    size_t entry = 0;
    auto send = [&](int value, const juce::MidiMessage& message) {
        auto& previous = mackieFeedback_[entry++];
        if (previous != value) {
            previous = value;
            mackieOutput_->sendMessageNow(message);
        }
    };
    auto led = [&](int note, bool on) { send(on, juce::MidiMessage::noteOn(1, note, juce::uint8(on ? 127 : 0))); };
    mackieBank_ = std::clamp(mackieBank_, 0, std::max(0, int(trackControls_.size()) - 1));
    for (int i = 0; i < 8; ++i) {
        const auto track = size_t(mackieBank_ + i);
        auto* strip = track < trackControls_.size() ? trackControls_[track].get() : nullptr;
        const int level = strip ? juce::roundToInt(16383 * strip->volume.valueToProportionOfLength(strip->volume.getValue())) : 0;
        send(level, juce::MidiMessage::pitchWheel(i + 1, level));
        const int pan = strip ? 1 + juce::roundToInt(10 * strip->pan.valueToProportionOfLength(strip->pan.getValue())) : 0;
        send(pan, juce::MidiMessage::controllerEvent(1, 0x30 + i, pan));
        led(i, strip && strip->arm.getToggleState());
        led(8 + i, strip && strip->solo.getToggleState());
        led(16 + i, strip && strip->mute.getToggleState());
        led(24 + i, strip && selectedTrack_ == track);
    }
    led(0x5d, !transportActive_);
    led(0x5e, transportActive_);
    led(0x5f, recordTrack_.has_value());
}

void MainComponent::applySurfaceAction(ostfstudio::SurfaceAction a) {
    using A = ostfstudio::SurfaceAction;
    if (!project_) return;
    const int last = std::max(0, int(trackControls_.size()) - 1);
    mackieBank_ = std::min(mackieBank_, last);
    switch (a.kind) {
        case A::rewind: deck_.rewind.triggerClick(); return;
        case A::forward: deck_.forward.triggerClick(); return;
        case A::stop: stopTransport(); return;
        case A::play: startTransport(false); return;
        case A::record: startTransport(true); return;
        case A::bankLeft: mackieBank_ = std::max(0, mackieBank_ - 8); return;
        case A::bankRight: if (mackieBank_ + 8 <= last) mackieBank_ += 8; return;
        case A::channelLeft: mackieBank_ = std::max(0, mackieBank_ - 1); return;
        case A::channelRight: mackieBank_ = std::min(last, mackieBank_ + 1); return;
        default: break;
    }
    const auto track = size_t(mackieBank_ + a.index);
    if (track >= trackControls_.size()) return;
    auto& strip = *trackControls_[track];
    switch (a.kind) {
        case A::fader: strip.volume.setValue(strip.volume.proportionOfLengthToValue(a.value / 16383.0), juce::sendNotificationSync); break;
        case A::relativePan: strip.pan.setValue(strip.pan.getValue() + a.value, juce::sendNotificationSync); break;
        case A::arm: strip.arm.triggerClick(); break;
        case A::solo: strip.solo.triggerClick(); break;
        case A::mute: strip.mute.triggerClick(); break;
        case A::select: if (!recordTrack_) selectTrack(track); break;
        default: break;
    }
}

void MainComponent::cancelPadLearn() {
    learnTarget_ = -1;
    learnedPair_ = -1;
    drumPads_.setLearning(false);
}

void MainComponent::attachPadInput(bool enable) {
    if (enable == padListening_) return;
    if (enable) {
        ensureDeviceSettings().midiDevices().addMidiInputDeviceCallback({}, this);
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
        if (mackiePort_.isNotEmpty() && portId == mackiePort_) {
            const auto action = ostfstudio::mackieAction(message);
            if (action.kind != ostfstudio::SurfaceAction::none && surfaceCount_ < surfaceEvents_.size()) {
                surfaceEvents_[surfaceCount_++] = action;
                padUpdate_.triggerAsyncUpdate();
            }
            return;
        }
        bool matched = false;
        for (auto& monitor : inputMonitors_)
            if (monitor.route.accepts(portId, message.getChannel())) { monitor.received = true; matched = true; }
        if (matched) padUpdate_.triggerAsyncUpdate();
        const bool kitAccepts = drumInputMonitor_ && *drumInputMonitor_ < inputMonitors_.size()
            && inputMonitors_[*drumInputMonitor_].route.accepts(portId, message.getChannel());
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
    {
        const juce::SpinLock::ScopedLockType lock(surfaceLock_);
        for (size_t i = 0; i < std::min(inputMonitors_.size(), received.size()); ++i)
            received[i] = std::exchange(inputMonitors_[i].received, false);
        count = std::exchange(surfaceCount_, 0);
        std::copy_n(surfaceEvents_.begin(), count, events.begin());
    }
    for (size_t i = 0; i < received.size(); ++i)
        if (received[i]) trackControls_[i]->inputLamp.pulse();
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
    if (spliceAuditionActive_ && deviceSettings_) deviceSettings_->devices().removeAudioCallback(&splicePlayer_);
    splicePlayer_.setProcessor(nullptr);
    spliceWindow_.reset();
    attachPadInput(false);
    stopTimer();
    transport_.stop();
    setLookAndFeel(nullptr);
}

void MainComponent::useTrackSinger(size_t index, bool automatic) {
    constexpr auto name = "OSTF Formant Singer";
    auto& track = *trackControls_[index];
    auto& owner = *trackControls_[track.outputTrack.value_or(index)];
    if (owner.instance && owner.instance->getName() == name) {
        if (!automatic) openTrackPluginEditor(index);
        return;
    }
    if (owner.loading || recordTrack_) return;
    if (track.outputTrack || hasSharedParts(index)) {
        statusLabel_.setText(TRANS("Disconnect this track from its shared instrument to use a separate singer."), juce::dontSendNotification);
        return;
    }
    const auto types = ensurePluginBrowser().types();
    if (const auto* type = findInstrument(types,[&](const auto& t) {
            return t.name == name && t.manufacturerName == "OSTF Studio" && t.isInstrument;
        })) {
        if (automatic) track.instrumentAssignment = instrumentReference(*type);
        loadTrackPlugin(index,*type,automatic);
        return;
    }
    track.plugin.setTooltip(TRANS("Install the bundled OSTF Formant Singer VST3 to sing this track."));
    statusLabel_.setText(track.plugin.getTooltip(),juce::dontSendNotification);
}

void MainComponent::editVocalNote(const ostfstudio::PitchedNote& note) {
    if (!project_ || !selectedTrack_ || recordTrack_ || !pianoGrid_.editable) return;
    const auto* data = vocalLane_.lookup(note.id);
    const auto original = data ? *data : ostfstudio::VocalNote{};
    auto* box = new juce::AlertWindow(TRANS("Vocal note"), {}, juce::MessageBoxIconType::NoIcon,this);
    box->addTextEditor("words",original.text,TRANS("Note lyrics"));
    box->addTextEditor("reading",original.pronunciation == original.text ? juce::String{} : original.pronunciation,
                       TRANS("Pronunciation (blank uses note lyrics)"));
    juce::StringArray choices{"", "scoop", "vibrato"};
    juce::StringArray labels{TRANS("Straight"),TRANS("Scoop"),TRANS("Vibrato")};
    if (!choices.contains(original.technique)) { choices.add(original.technique); labels.add(original.technique); }
    box->addComboBox("technique",labels,TRANS("Vocal expression"));
    box->getComboBoxComponent("technique")->setSelectedItemIndex(choices.indexOf(original.technique),juce::dontSendNotification);
    box->addTextEditor("strength",juce::String(original.intensity*100.0,2),TRANS("Intensity (%)"));
    box->addButton(TRANS("Apply"),1,juce::KeyPress(juce::KeyPress::returnKey));
    box->addButton(TRANS("Cancel"),0,juce::KeyPress(juce::KeyPress::escapeKey));
    box->enterModalState(true,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<MainComponent>(this),
        box,choices,track=selectedTrack_,id=note.id,generation=projectGeneration_](int ok) {
        if (!ok || !safe || safe->projectGeneration_ != generation || safe->selectedTrack_ != track || safe->recordTrack_) return;
        const auto text = box->getTextEditorContents("strength").trim();
        const double strength = text.getDoubleValue();
        if (text.isEmpty() || !text.containsOnly("0123456789.") || !text.containsAnyOf("0123456789")
            || text.indexOfChar('.') != text.lastIndexOfChar('.') || strength > 100) {
            safe->statusLabel_.setText(TRANS("Enter an intensity from 0 to 100."),juce::dontSendNotification); return;
        }
        ostfstudio::VocalNote change{box->getTextEditorContents("words"),box->getTextEditorContents("reading"),
            choices[box->getComboBoxComponent("technique")->getSelectedItemIndex()],strength/100.0};
        const bool playing = safe->transportActive_;
        if (playing) safe->stopTransport();
        juce::MemoryBlock before;
        const auto result = safe->project_->editVocalNote(*track,id,change,before);
        if (result.wasOk() && !before.isEmpty()) {
            safe->rememberScoreEdit(std::move(before));
            if (!safe->trackControls_[*track]->instrumentAssignment.isObject() && change.text.isNotEmpty())
                safe->useTrackSinger(*track,true);
        } else if (result.failed()) safe->statusLabel_.setText(juce::translate(result.getErrorMessage()),juce::dontSendNotification);
        safe->finishNoteEdit(playing);
    }),true);
}

void MainComponent::restoreInstrumentAssignments(const ostfstudio::SessionMix& mix) {
    if (std::none_of(mix.instruments.begin(), mix.instruments.end(), [](const auto& item) { return item.isObject(); })) return;
    const auto types = ensurePluginBrowser().types();
    for (std::size_t index = 0; index < std::min(mix.instruments.size(), trackControls_.size()); ++index) {
        const auto& reference = mix.instruments[index];
        if (!reference.isObject()) continue;
        auto& track = *trackControls_[index];
        track.instrumentAssignment = reference;
        if (!track.outputTrack) restoreLocalInstrument(index, types);
    }
}

void MainComponent::restoreLocalInstrument(std::size_t index, const juce::Array<juce::PluginDescription>& types) {
    auto& track = *trackControls_[index];
    if (!track.instrumentAssignment.isObject() || track.instance || track.outputTrack) return;
    if (track.inactiveInstrument) {
        loadTrackPlugin(index, *track.inactiveInstrument, true, track.inactiveInstrumentState);
        return;
    }
    const auto* match = findInstrument(types, [&](const auto& description) {
        return matchesVst3Instrument(track.instrumentAssignment, description);
    });
    if (!match) track.plugin.setButtonText(track.instrumentAssignment["name"].toString() + TRANS(" (missing)"));
    else loadTrackPlugin(index, *match, true, track.inactiveInstrumentState);
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
    if (audioTake && !clipOn(*selectedTrack_) && project_->audio.size() >= project_->maxAudioRegions) {
        statusLabel_.setText(TRANS("Too many audio regions."), juce::dontSendNotification);
        return;
    }
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
    auto& devices = ensureDeviceSettings().devices();
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
    take.controlPort = mackiePort_;
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
    }
    if (recording && !take.cycle.isEmpty() && start >= take.cycle.getEnd()) {
        statusLabel_.setText(TRANS("Place the cursor before the end of the recording loop."), juce::dontSendNotification);
        return;
    }
    if (recording) cancelPadLearn();
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
    for (auto* button : {&deck_.home, &deck_.rewind, &deck_.forward, &deck_.end}) button->setEnabled(!recording);
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
    for (auto* button : {&deck_.home, &deck_.rewind, &deck_.forward, &deck_.end}) button->setEnabled(true);
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
    if (arrangementClipFocus_ && project_ && selectedTrack_ && !selectedGlobal_ && hasKeyboardFocus(false) && mods.isCommandDown() && !mods.isAltDown()) {
        const auto track = *selectedTrack_;
        const auto hit = clipLane_.selected ? clipAt(track, std::llround((*clipLane_.selected - 1) * project_->ticksPerBar)) : std::nullopt;
        if (key.isKeyCode('Z')) { undoPianoEdit(mods.isShiftDown()); return true; }
        if (key.isKeyCode('C')) { clipCommand(ClipOp::copy, hit, track, 0); return true; }
        if (key.isKeyCode('V')) { clipCommand(ClipOp::paste, hit, track, clipInsertionTick_); return true; }
        if (key.isKeyCode('D')) { clipCommand(ClipOp::duplicate, hit, track, 0); return true; }
        if (key.isKeyCode('E')) { clipCommand(ClipOp::split, hit, track, cursorTick_); return true; }
        if (key.isKeyCode('J')) { clipCommand(ClipOp::join, hit, track, 0); return true; }
    }
    if (selectedGlobal_ && hasKeyboardFocus(false)) {
        if (mods.isCommandDown() && !mods.isAltDown()) {
            if (key.isKeyCode('C')) {
                globalCommand(2);
                return true;
            }
            if (key.isKeyCode('E') || key.isKeyCode('J')) {
                editGlobalBoundary(key.isKeyCode('E'));
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
    if (action == TransportKey::end) target = end;
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
    if (scrolled) {
        if (chordReview_.isVisible()) chordReview_.resized();
        repaint();
        return;
    }
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
    project_ = std::move(next);
    reorderSource_.reset();
    reorderTarget_.reset();
    arrangementOrder_ = project_ && project_->savedMix ? project_->savedMix->trackOrder : juce::StringArray{};
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
    draggingMidi_.reset();
    dragPreview_.reset();
    copiedMidi_ = {};
    copiedAudio_.reset();
    selectedAudioId_.clear();
    arrangementClipFocus_ = false;
    ++projectGeneration_;
    pendingReview_.clear();
    chordReview_.setVisible(false);
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
        for (std::size_t i = 0; i < std::min(trackControls_.size(), saved->midiRoutes.size()); ++i) {
            trackControls_[i]->midiChannel = saved->midiRoutes[i].channel;
            trackControls_[i]->outputTrack = saved->midiRoutes[i].destination;
        }
        restoreInstrumentAssignments(*saved);
        for (std::size_t i = 0; i < std::min(trackControls_.size(), saved->midiRoutes.size()); ++i)
            updatePresetLabel(i);
        for (std::size_t i = 0; i < outputControls_.size(); ++i) {
            auto ids = saved->outputEffects[i];
            if (ids.isEmpty() && saved->insertIds[i].isNotEmpty())
                ids.add(saved->insertIds[i]);
            restoreEffects(true, i, ids);
        }
        for (std::size_t i = 0; i < std::min(trackControls_.size(), saved->trackEffects.size()); ++i)
            restoreEffects(false, i, saved->trackEffects[i]);
    }
    for (const auto& id : project_->lyricTrackIds) {
        const auto index = project_->trackIds.indexOf(id);
        if (index >= 0 && !trackControls_[index]->instrumentAssignment.isObject() && !trackControls_[index]->outputTrack)
            useTrackSinger(static_cast<size_t>(index), true);
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
    const bool xml = format == SaveFormat::musicXml;
    const bool chords = format == SaveFormat::chordPro;
    const bool exporting = xml || chords || midi;
    const juce::String extension = xml ? "musicxml" : chords ? "chordpro" : midi ? "mid" : package ? "ostfp" : "ostf";
    auto title = juce::File::createLegalFileName(project_->title).trim();
    if (title.isEmpty()) title = "Untitled";
    const bool upgrading = !exporting && legacySourceFile_ != juce::File{};
    const auto start = upgrading
        ? legacySourceFile_.getSiblingFile(legacySourceFile_.getFileNameWithoutExtension() + "-5.0.0." + extension)
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(title + "." + extension);
    const auto dialogTitle = juce::translate(xml ? "Export MusicXML..." : chords ? "Export ChordPro..." : midi ? "Save as MIDI (SMF)" : package ? "Save as OSTFP" : "Save as OSTF")
        + (upgrading ? " (OSTF " + project_->sourceVersion + " " + juce::String::charToString(0x2192) + " 5.0.0)" : juce::String{});
    chooser_ = std::make_unique<juce::FileChooser>(dialogTitle, start, chords ? "*.chordpro;*.cho" : "*." + extension);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, package, midi, xml, chords, exporting, format,
         extension](const juce::FileChooser& chooser) {
            if (!safe || safe->projectGeneration_ != generation) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension(chords ? "chordpro;cho" : extension)) {
                safe->statusLabel_.setText(TRANS("Choose a .") + extension + TRANS(" filename"), juce::dontSendNotification);
                return;
            }
            if (safe->recordTrack_) safe->stopTransport();
            juce::TemporaryFile temporary(file);
            auto result = juce::Result::fail(TRANS("Could not open a temporary file for saving"));
            {
                juce::FileOutputStream output(temporary.getFile());
                if (output.openedOk() && (xml || chords)) {
                    const auto score = ostfstudio::notationScore(*safe->project_);
                    result = xml ? ostfstudio::writeSongXml(output, score) : ostfstudio::writeChordPro(output, score);
                } else if (output.openedOk() && midi) {
                    std::vector<int> channels;
                    for (const auto& track : safe->trackControls_) channels.push_back(track->midiChannel);
                    result = ostfstudio::writeSongMidi(*safe->project_, format == SaveFormat::midi0
                        ? ostfstudio::SmfFormat::merged : ostfstudio::SmfFormat::multitrack, channels, output);
                } else if (output.openedOk()) {
                    auto mix = safe->mixer_.snapshot();
                    mix.trackOrder = safe->arrangementOrder_;
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
            if (result.wasOk() && !exporting) {
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
    mixer_.setAddOutputAvailability(busAdd_.isEnabled(), returnAdd_.isEnabled());
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
    const bool shared = trackControls_[track]->outputTrack.has_value() || hasSharedParts(track);
    std::array<int,3> current{-1,-1,-1};
    for (const auto& curve : project_->performanceCurves[track]) {
        const auto& k = curve.control;
        if (!curve.enabled || (!shared && k.channel && k.channel != channel)) continue;
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
        [safe = juce::Component::SafePointer<MainComponent>(this), w, names, current, track, tick, channel, shared, generation = projectGeneration_](int choice) {
            if (!safe || choice != 1 || safe->recordTrack_ || safe->projectGeneration_ != generation) return;
            std::array<int,3> values;
            for (int i = 0; i < 3; ++i) values[i] = w->getComboBoxComponent(names[i])->getSelectedItemIndex() - 1;
            if (values == current) return;
            const bool playing = safe->transportActive_;
            if (playing) safe->stopTransport();
            juce::MemoryBlock before;
            auto result = safe->project_->setPatchAt(track, tick, channel, values, before, shared);
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
        menu.addItem(moveRowUp, TRANS("Move track up"), adjacentMovableTrack(index, -1).has_value());
        menu.addItem(moveRowDown, TRANS("Move track down"), adjacentMovableTrack(index, 1).has_value());
        menu.addSeparator();
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
            if (!output && (choice == moveRowUp || choice == moveRowDown)) safe->moveTrackRow(index, choice == moveRowUp ? -1 : 1);
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
            controls.mute.setTitle(name + " mute");
            controls.solo.setTitle(name + " solo");
            controls.volume.setName(name + " volume");
            controls.pan.setName(name + " pan");
            for (size_t i = 0; i < safe->trackControls_.size(); ++i)
                if (safe->trackControls_[i]->outputTrack == index) safe->updatePresetLabel(i);
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
    mix.trackOrder = arrangementOrder_;
    auto& routing = mix.routing;
    auto& count = send ? routing.returnCount : routing.busCount;
    const auto limit = send ? ostfstudio::maxMixerReturns : ostfstudio::maxMixerBuses;
    const auto slotAt = send ? ostfstudio::returnOutput : ostfstudio::busOutput;
    if ((!remove && count >= limit) || (source && (!*source || !routing.hasOutput(*source)
        || send != ostfstudio::isReturnOutput(*source)))) return;
    const auto index = source ? (send ? ostfstudio::outputReturnIndex(*source) : ostfstudio::outputBusIndex(*source)) : count;
    stopTransport();
    resizingAudio_.reset();
    draggingMidi_.reset();
    dragPreview_.reset();
    copiedMidi_ = {};
    copiedAudio_.reset();
    selectedAudioId_.clear();
    arrangementClipFocus_ = false;
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
                               const ImportResultRow* import, juce::int64 tick,
                               std::optional<ostfstudio::SessionMix::MidiRoute> sharedPart) {
    if (!canEditTracks() || chordReviewBlocksSwitch()) return;
    stopTransport();
    std::unique_ptr<ostfstudio::OstfpPreview> updated;
    std::optional<int> newChannel;
    if (sharedPart) newChannel = sharedPart->channel;
    else if (import && kind != ostfstudio::TrackKind::audio && import->index >= 0)
        newChannel = import->source.sourceMidiChannels[size_t(import->index)];
    auto result = project_->editTracks(kind, remove, updated, duplicate, group, newChannel);
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
    mix.trackOrder = arrangementOrder_;
    scoreView_.reset();
    workspaceMode_ = WorkspaceMode::arrangement;
    auto previous = std::move(trackControls_);
    project_ = std::move(updated);
    reorderSource_.reset();
    reorderTarget_.reset();
    scaleReference_ = -1;
    theory_.reset();
    refreshTheory();
    waveforms_.load(project_->audio);
    std::erase_if(melodyneCopies_, [this](const auto& copy) { return !project_->trackIds.contains(copy.first); });
    resizingAudio_.reset();
    draggingMidi_.reset();
    dragPreview_.reset();
    copiedMidi_ = {};
    copiedAudio_.reset();
    selectedAudioId_.clear();
    arrangementClipFocus_ = false;
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
    pendingReview_.clear();
    chordReview_.setVisible(false);
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
        track.inactiveInstrument = std::move(old.inactiveInstrument);
        track.inactiveInstrumentState = std::move(old.inactiveInstrumentState);
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
        if (old.outputTrack) track.plugin.getProperties().set("sharedPart", true);
        updatePresetLabel(i);
        if (old.outputTrack && !track.outputTrack && track.instrumentAssignment.isObject())
            restoreLocalInstrument(i, ensurePluginBrowser().types());
    }
    mixer_.restore(mix);
    if (sharedPart) {
        const auto dest = project_->sourceTrackCount() - 1;
        trackControls_[dest]->midiChannel = sharedPart->channel;
        trackControls_[dest]->outputTrack = sharedPart->destination;
        updatePresetLabel(dest);
        if (sharedPart->destination) updatePresetLabel(*sharedPart->destination);
        markSessionEdited();
    }
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
        copy.inactiveInstrument = src.inactiveInstrument;
        copy.inactiveInstrumentState = src.inactiveInstrumentState;
        copy.plugin.setButtonText(src.plugin.getButtonText());
        updatePresetLabel(dest);
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
        ensureDeviceSettings().devices();
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
            if (item == 3) { safe->menuItemSelected(showDevices, 4); return; }
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
    auto* device = ensureDeviceSettings().devices().getCurrentAudioDevice();
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
            if (item == 2) { safe->menuItemSelected(showDevices, 4); return; }
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
        controls->cue.setTooltip(TRANS("Send this track to Cue before its fader."));
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
        controls->plugin.onClick = [this, i] {
            const auto owner = trackControls_[i]->outputTrack.value_or(i);
            if (trackControls_[owner]->instance && (owner != i || hasSharedParts(i))) openTrackPluginEditor(i);
            else showTrackPluginMenu(i);
        };
        controls->routingMenu.setTitle(name + " / " + TRANS("Instrument routing"));
        controls->routingMenu.setButtonText(TRANS("Sharing") + " " + juce::String::charToString(0x25be));
        controls->routingMenu.setTooltip(TRANS("View or disconnect shared MIDI parts"));
        controls->routingMenu.onClick = [this, i] { showTrackPluginMenu(i); };
        rowOverlay_.addChildComponent(controls->routingMenu);
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

bool MainComponent::hasSharedParts(std::size_t owner) const {
    return std::any_of(trackControls_.begin(), trackControls_.end(),
                       [owner](const auto& part) { return part->outputTrack == owner; });
}

int MainComponent::nextSharedChannel(std::size_t owner) const {
    if (!project_ || owner >= project_->sourceTrackCount()
        || !canEditTracks() || recordTrack_) return 0;
    const auto& host = *trackControls_[owner];
    if (host.outputTrack || !host.playableInstrument || !host.instance || !host.instance->acceptsMidi()
        || midiOutputs_.contains(project_->trackIds[int(owner)])) return 0;
    std::array<bool, 17> used{};
    used[host.midiChannel] = true;
    for (const auto& track : trackControls_)
        if (track->outputTrack == owner) used[track->midiChannel] = true;
    for (int offset = 0; offset < 16; ++offset) {
        const int channel = 1 + (host.midiChannel + offset) % 16;
        if (!used[channel]) return channel;
    }
    return 0;
}

void MainComponent::addSharedPart(std::size_t index) {
    const auto owner = trackControls_[index]->outputTrack.value_or(index);
    const int channel = nextSharedChannel(owner);
    if (!channel || project_->sourceTrackCount() >= project_->maxTracks) return;
    const bool gmDrums = channel == 10 && trackControls_[owner]->instance->getName() == "OSTF GM Sound Module";
    editTracks(gmDrums ? ostfstudio::TrackKind::drum : ostfstudio::TrackKind::pitched, {}, {},
               ostfstudio::MixerTrackGroup::pitched, nullptr, 0, ostfstudio::SessionMix::MidiRoute{channel, owner});
}

void MainComponent::detachSharedPart(std::size_t index) {
    if (!project_ || recordTrack_ || index >= trackControls_.size()) return;
    auto& part = *trackControls_[index];
    if (!part.outputTrack) return;
    routeMidiPart(index, {}, part.midiChannel);
    if (!part.instrumentAssignment.isObject()) statusLabel_.setText(TRANS("Sharing stopped. Choose an instrument for this track."), juce::dontSendNotification);
}

void MainComponent::routeMidiPart(std::size_t index, std::optional<std::size_t> owner, int channel) {
    if (!project_ || recordTrack_ || index >= project_->sourceTrackCount() || channel < 1 || channel > 16) return;
    auto& part = *trackControls_[index];
    if (part.loading || (owner && (*owner == index || *owner >= project_->sourceTrackCount()
        || hasSharedParts(index) || trackControls_[*owner]->outputTrack || !trackControls_[*owner]->playableInstrument))) return;
    stopTransport();
    if (midiOutputs_.erase(project_->trackIds[int(index)])) saveMidiRoutes();
    if (owner && part.instance) {
        part.editor.reset();
        part.inactiveInstrument = part.instance->getPluginDescription();
        part.inactiveInstrumentState.reset();
        part.instance->getStateInformation(part.inactiveInstrumentState);
        part.instance.reset();
        part.playableInstrument = false;
    }
    if (owner != part.outputTrack || channel != part.midiChannel) markSessionEdited();
    const auto previousOwner = part.outputTrack.value_or(index);
    part.outputTrack = owner;
    part.midiChannel = channel;
    for (const auto host : {previousOwner, owner.value_or(index)})
        if (trackControls_[host]->editor && trackControls_[host]->instance)
            trackControls_[host]->editor->setName(instrumentWindowTitle(host));
    refreshMidiRoutes();
    updatePresetLabel(index);
    updatePresetLabel(previousOwner);
    if (owner) updatePresetLabel(*owner);
    resized();
    if (!owner && !part.instance && part.instrumentAssignment.isObject()) restoreLocalInstrument(index, ensurePluginBrowser().types());
}

bool MainComponent::isEmptyMidiTrack(std::size_t i) const {
    if (!project_ || i >= project_->sourceTrackCount() || project_->trackKinds[i] == ostfstudio::TrackKind::audio) return false;
    const auto id = project_->trackIds[int(i)];
    const auto& t = *trackControls_[i];
    const auto name = juce::String::fromUTF8(project_->trackLanes[i].label);
    const auto n = name.getTrailingIntValue();
    if (n < 1 || n > 16 || name != "MIDI Ch " + juce::String(n) || t.loading || t.outputTrack || hasSharedParts(i)
        || t.instance || t.instrumentAssignment.isObject() || !t.effects.slots.empty() || t.inactiveInstrument
        || armedId_ == id || midiOutputs_.contains(id) || project_->panAutomation.contains(id)
        || !clipsOn(i).empty() || !project_->performanceCurves[i].empty()) return false;
    return std::none_of(project_->addedClips.begin(), project_->addedClips.end(), [i](const auto& clip) { return clip.track == i; });
}

void MainComponent::reviewEmptyMidiTracks() {
    if (!canEditTracks() || recordTrack_) return;
    juce::StringArray names, ids;
    for (size_t i = 0; i < project_->sourceTrackCount(); ++i) if (isEmptyMidiTrack(i)) {
        names.add("#" + juce::String(int(i + 1)) + " " + juce::String::fromUTF8(project_->trackLanes[i].label));
        ids.add(project_->trackIds[int(i)]);
    }
    if (ids.isEmpty()) {
        statusLabel_.setText(TRANS("No unused MIDI parts to remove."), juce::dontSendNotification);
        return;
    }
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, TRANS("Remove these empty MIDI parts?"),
        names.joinIntoString("\n"), TRANS("Delete track"), TRANS("Cancel"), this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this), ids, generation = projectGeneration_](int answer) {
            if (!safe || !answer || safe->projectGeneration_ != generation || safe->recordTrack_) return;
            for (int n = ids.size(); --n >= 0;) {
                const auto index = safe->project_->trackIds.indexOf(ids[n]);
                if (index >= 0 && safe->isEmptyMidiTrack(size_t(index))) safe->editTracks(ostfstudio::TrackKind::pitched, size_t(index));
            }
        }));
}

void MainComponent::showTrackPluginMenu(std::size_t index) {
    auto& control = *trackControls_[index];
    juce::PopupMenu menu;
    constexpr int channelId = 100;
    constexpr int outputId = 200;
    constexpr int sharedId = 20000;
    constexpr int reuseId = 600;
    const auto owner = control.outputTrack.value_or(index);
    const int nextChannel = nextSharedChannel(owner);
    constexpr int detachId = 500;
    if (control.outputTrack || hasSharedParts(index)) {
        menu.addSectionHeader(TRANS("Shared instrument") + " #" + juce::String(int(owner + 1)) + "  "
                              + juce::String::fromUTF8(project_->trackLanes[owner].label));
        if (control.outputTrack) {
            menu.addItem(detachId + int(index), TRANS("Stop sharing this instrument"), !recordTrack_ && !control.loading);
        } else {
            juce::PopupMenu parts;
            for (size_t i = 0; i < trackControls_.size(); ++i) {
                const auto& part = *trackControls_[i];
                if (part.outputTrack == index)
                    parts.addItem(detachId + int(i), "Ch " + juce::String(part.midiChannel) + "  " + juce::String::fromUTF8(project_->trackLanes[i].label), !part.loading);
            }
            menu.addSubMenu(TRANS("Stop sharing with track"), parts, !recordTrack_);
            menu.addItem(11, TRANS("Disconnect all parts and keep tracks"), !recordTrack_);
        }
        menu.addSeparator();
    }
    if (nextChannel || control.outputTrack || control.playableInstrument) {
        auto label = TRANS("Add MIDI track sharing this instrument");
        if (nextChannel) label += " / Ch " + juce::String(nextChannel);
        if (!control.outputTrack && !hasSharedParts(index))
            menu.addSectionHeader("#" + juce::String(int(owner + 1)) + " " + juce::String::fromUTF8(project_->trackLanes[owner].label));
        menu.addItem(10, label, nextChannel != 0 && project_->sourceTrackCount() < project_->maxTracks);
        juce::PopupMenu existing;
        for (size_t i = 0; i < project_->sourceTrackCount(); ++i) {
            const auto& part = *trackControls_[i];
            if (i == owner || part.outputTrack || part.loading || hasSharedParts(i) || project_->trackKinds[i] == ostfstudio::TrackKind::audio) continue;
            existing.addItem(reuseId + int(i), "#" + juce::String(int(i + 1)) + " " + juce::String::fromUTF8(project_->trackLanes[i].label));
        }
        if (existing.getNumItems()) menu.addSubMenu(TRANS("Use an existing track") + " / Ch " + juce::String(nextChannel), existing, nextChannel != 0);
        menu.addSeparator();
    }
    menu.addItem(12, TRANS("Review unused MIDI parts..."), canEditTracks() && !recordTrack_);
    juce::PopupMenu channels;
    for (int channel = 1; channel <= 16; ++channel)
        channels.addItem(channelId + channel, juce::String(channel), true, control.midiChannel == channel);
    menu.addSubMenu(TRANS("MIDI channel ") + juce::String(control.midiChannel), channels);
    juce::PopupMenu outputs;
    outputs.addItem(outputId, TRANS("This track"), true, !control.outputTrack);
    const auto sourceLimit = std::min(PreviewTransport::maxTracks, trackControls_.size());
    for (std::size_t i = 0; i < sourceLimit; ++i) {
        const auto& target = *trackControls_[i];
        if (i == index || i >= project_->sourceTrackCount() || target.outputTrack || !target.playableInstrument || !target.instance
            || !target.instance->acceptsMidi() || midiOutputs_.contains(project_->trackIds[int(i)])) continue;
        juce::PopupMenu parts;
        std::array<juce::StringArray, 16> assigned;
        for (std::size_t source = 0; source < sourceLimit; ++source) {
            const auto& route = *trackControls_[source];
            if (source == i || route.outputTrack == i)
                assigned[route.midiChannel - 1].add(juce::String::fromUTF8(project_->trackLanes[source].label));
        }
        for (int part = 1; part <= 16; ++part) {
            auto label = "Ch " + juce::String(part);
            if (!assigned[part - 1].isEmpty()) label += "   " + assigned[part - 1].joinIntoString(", ");
            parts.addItem(sharedId + static_cast<int>(i) * 16 + part - 1, label,
                          true, control.outputTrack == i && control.midiChannel == part);
        }
        outputs.addSubMenu("#" + juce::String(int(i + 1)) + " " + juce::String::fromUTF8(project_->trackLanes[i].label) + " / " + target.instance->getName(), parts);
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
    menu.addSubMenu(TRANS("Instrument destination and channel"), outputs, !hasSharedParts(index) && !control.loading);
    menu.addItem(9, TRANS("Initial MIDI patch..."), index < project_->sourceTrackCount());
    menu.addSeparator();
    menu.addItem(8, TRANS("Link WAV on this track..."), !control.loading && index < project_->sourceTrackCount());
    menu.addItem(1, TRANS("Choose plug-in file..."), !control.loading);
    menu.addItem(3, TRANS("Choose plug-in bundle folder..."), !control.loading);
    menu.addItem(4, TRANS("Load selected VST3"), control.candidatePath.hasFileExtension("vst3") && !control.loading);
    const auto editorTrack = control.outputTrack.value_or(index);
    menu.addItem(5, control.outputTrack ? TRANS("Open shared instrument") : TRANS("Open plug-in editor"),
                 editorTrack < trackControls_.size() && trackControls_[editorTrack]->instance != nullptr);
    menu.addSeparator();
    menu.addItem(2, hasSharedParts(index) ? TRANS("Clear instrument and disconnect its parts") : TRANS("Clear assignment"), (control.pluginPath != juce::File{} || control.instrumentAssignment.isObject()) && !control.loading);
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&control.plugin),
        [self = juce::Component::SafePointer<MainComponent>(this), index, ports, id, generation = projectGeneration_](int selection) {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            if (selection == 1 || selection == 3) self->chooseTrackPlugin(index, selection == 3);
            if (selection == 4) self->scanTrackPlugin(index);
            if (selection == 5) self->openTrackPluginEditor(self->trackControls_[index]->outputTrack.value_or(index));
            if (selection == 8) self->linkAudio(index);
            if (selection == 9) self->showPatch(index, 0);
            if (selection == 10) self->addSharedPart(index);
            if (selection == 11 && !self->recordTrack_)
                for (size_t i = 0; i < self->trackControls_.size(); ++i)
                    if (self->trackControls_[i]->outputTrack == index) self->detachSharedPart(i);
            if (selection == 12) self->reviewEmptyMidiTracks();
            if (selection >= reuseId && selection < reuseId + int(self->project_->sourceTrackCount())) {
                const auto owner = self->trackControls_[index]->outputTrack.value_or(index);
                const int channel = self->nextSharedChannel(owner);
                if (channel) self->routeMidiPart(size_t(selection - reuseId), owner, channel);
            }
            if (selection >= detachId && selection < detachId + int(self->trackControls_.size()))
                self->detachSharedPart(size_t(selection - detachId));
            if (selection > channelId && selection <= channelId + 16) {
                self->stopTransport();
                if (self->trackControls_[index]->midiChannel != selection - channelId) self->markSessionEdited();
                self->trackControls_[index]->midiChannel = selection - channelId;
                self->updatePresetLabel(index);
                const auto host = self->trackControls_[index]->outputTrack.value_or(index);
                if (auto& window = self->trackControls_[host]->editor)
                    window->setName(self->instrumentWindowTitle(host));
            }
            if (selection >= 1000 && selection < sharedId) {
                self->stopTransport();
                if (selection == 1000) self->midiOutputs_.erase(id);
                else if (juce::isPositiveAndBelow(selection - 1001, ports.size())) self->midiOutputs_[id] = ports[selection - 1001];
                self->saveMidiRoutes();
                self->refreshMidiRoutes();
                return;
            }
            if (selection == outputId || selection >= sharedId) {
                const auto next = selection == outputId ? std::nullopt : std::optional<std::size_t>((selection - sharedId) / 16);
                self->routeMidiPart(index, next, next ? 1 + (selection - sharedId) % 16 : self->trackControls_[index]->midiChannel);
            }
            if (selection == 2) {
                self->stopTransport();
                for (size_t i = 0; i < self->trackControls_.size(); ++i)
                    if (self->trackControls_[i]->outputTrack == index) self->detachSharedPart(i);
                auto& strip = *self->trackControls_[index];
                strip.editor.reset();
                strip.instance.reset();
                strip.playableInstrument = false;
                if (!strip.instrumentAssignment.isVoid()) self->markSessionEdited();
                strip.instrumentAssignment = juce::var{};
                strip.inactiveInstrument.reset();
                strip.inactiveInstrumentState.reset();
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
    for (auto& c : clips) if (c.track == track && c.regionId == selectedAudioId_) return &c;
    const auto it = std::find_if(clips.begin(), clips.end(), [track](const auto& clip) { return clip.track == track; });
    return it != clips.end() ? &*it : nullptr;
}

bool MainComponent::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& details,
                                                        juce::StringArray& paths, bool& canMove) {
    const auto id = details.description.toString();
    if (!id.startsWith("ostf-media:")) return false;
    const juce::File file(id.fromFirstOccurrenceOf(":", false, false));
    if (!file.existsAsFile() || !PluginBrowser::supportsSample(file)) return false;
    paths.add(file.getFullPathName());
    canMove = false;
    return true;
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    return project_ && !recordTrack_ && workspaceMode_ == WorkspaceMode::arrangement
           && files.size() == 1 && PluginBrowser::supportsSample(juce::File::getCurrentWorkingDirectory().getChildFile(files[0]));
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y) {
    if (!isInterestedInFileDrag(files) || chordReviewBlocksSwitch()) return;
    auto track = pluginDropTrack({x, y});
    if (audioEditor_.isVisible() && audioEditor_.getBounds().contains(x, y)) track = selectedTrack_;
    const juce::File file(files[0]);
    const bool arrangement = rowOverlay_.getBounds().contains(x, y);
    const auto tickAtDrop = arrangement && x >= rowOverlay_.getX() + trackHeaderWidth
        ? arrangementTickAt(x) : std::max<juce::int64>(0, cursorTick_);
    if (file.hasFileExtension("mid;midi;ostf;ostfp")) {
        if (!arrangement) return;
        std::unique_ptr<ostfstudio::OstfpPreview> source;
        juce::String summary;
        juce::Result r = juce::Result::ok();
        if (file.hasFileExtension("ostf;ostfp")) {
            juce::FileInputStream in(file);
            r = file.hasFileExtension("ostfp") ? ostfstudio::readOstfpPreview(in, source) : ostfstudio::readOstfPreview(in, source);
            if (r.wasOk() && file.hasFileExtension("ostf"))
                for (auto& region : source->audio) region.localFile = file.getSiblingFile(region.path);
        } else r = ostfstudio::prepareMediaImport(file, source, summary);
        if (r.failed()) { statusLabel_.setText(r.getErrorMessage(), juce::dontSendNotification); return; }
        const bool chords = globalLaneAt({x,y}) == TimelineLane::chord;
        auto held = std::shared_ptr<const ostfstudio::OstfpPreview>(std::move(source));
        auto place = [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, held, track, tickAtDrop,
                      name = file.getFileName(), review = file.hasFileExtension("ostf;ostfp")](int partIndex) {
            if (!safe || safe->projectGeneration_ != generation || safe->recordTrack_ || safe->chordReviewBlocksSwitch()) return;
            ImportResultRow part(name, *held, partIndex);
            if (partIndex < 0) safe->placeResult(part, {}, tickAtDrop, false, held, review);
            else if (track && *track < safe->project_->sourceTrackCount() && safe->project_->trackKinds[*track] == held->trackKinds[partIndex])
                safe->placeResult(part, track, tickAtDrop, false, held, review);
            else if (safe->canEditTracks()) {
                const auto index = safe->project_->sourceTrackCount();
                safe->editTracks(held->trackKinds[partIndex], {}, {}, held->mixerGroups[partIndex], &part, tickAtDrop);
                if (review && safe->project_->sourceTrackCount() > index) safe->reviewDroppedChords(*held,partIndex,tickAtDrop);
            }
        };
        if (chords) place(-1);
        else if (held->sourceTrackCount() == 1) place(0);
        else {
            juce::PopupMenu menu;
            menu.addSectionHeader(TRANS("Choose a source track"));
            for (size_t i = 0; i < held->sourceTrackCount(); ++i)
                menu.addItem(int(i + 1), juce::String(int(i + 1)) + "  " + juce::String::fromUTF8(held->trackLanes[i].label));
            if (held->sourceTrackCount()) menu.showMenuAsync({}, [place](int choice) { if (choice) place(choice - 1); });
            else statusLabel_.setText(TRANS("No tracks to place."), juce::dontSendNotification);
        }
        return;
    }
    if (!track || *track >= project_->sourceTrackCount() || project_->trackKinds[*track] != ostfstudio::TrackKind::audio) {
        if (arrangement && canEditTracks()) {
            placeWav(file, project_->sourceTrackCount(), tickAtDrop, false);
            return;
        }
        statusLabel_.setText(TRANS("Drop the sample onto an audio track"), juce::dontSendNotification);
        return;
    }
    const auto local = audioEditor_.getLocalPoint(this, juce::Point<int>(x,y));
    const auto tick = !arrangement && audioEditor_.isVisible() && audioEditor_.recordRuler.getBounds().contains(local.x, audioEditor_.recordRuler.getY())
        ? arrangementTickAt(x) : tickAtDrop;
    placeWav(juce::File(files[0]), *track, tick, false);
}

void MainComponent::placeWav(const juce::File& file, std::size_t index, juce::int64 tick, bool replace) {
    if (!project_ || index > project_->sourceTrackCount() || recordTrack_) return;
    const bool addTrack = index == project_->sourceTrackCount();
    if (addTrack && (!canEditTracks() || index >= project_->maxTracks)) {
        statusLabel_.setText(TRANS("Choose a matching destination track."), juce::dontSendNotification);
        return;
    }
    ostfstudio::ProjectAudio* prior = nullptr;
    auto confirmReplace = [&] {
        juce::PopupMenu options;
        options.addSectionHeader(TRANS("Replace the WAV on this track?"));
        options.addItem(1, TRANS("Replace"));
        options.addItem(2, TRANS("Cancel"));
        options.showMenuAsync(juce::PopupMenu::Options{}.withParentComponent(this),
            [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, index, tick, file](int answer) {
                if (answer == 1 && safe && generation == safe->projectGeneration_) safe->placeWav(file, index, tick, true);
            });
    };
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0 || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024
        || file.getSize() > 16 * 1024 * 1024 || reader->numChannels < 1 || reader->numChannels > 2) {
        statusLabel_.setText(TRANS("Unsupported audio sample") + ": " + file.getFileName(), juce::dontSendNotification);
        return;
    }
    const auto end = project_->playbackEndTick();
    if (tick < 0 || tick >= end) {
        statusLabel_.setText(TRANS("Drop the sample before the song end"), juce::dontSendNotification);
        return;
    }
    ostfstudio::ProjectAudio clip{index, file.getFileName(), file, {}};
    if (!file.hasFileExtension("wav")) {
        if (reader->lengthInSamples * reader->numChannels * 3 + 4096 > 16 * 1024 * 1024) {
            statusLabel_.setText(TRANS("Unsupported audio sample") + ": " + file.getFileName(), juce::dontSendNotification);
            return;
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(clip.embedded.writable(), false);
        auto options = juce::AudioFormatWriterOptions{}.withSampleRate(reader->sampleRate)
            .withNumChannels(int(reader->numChannels)).withBitsPerSample(24);
        auto writer = wav.createWriterFor(stream, options);
        if (!writer || !writer->writeFromAudioReader(*reader,0,reader->lengthInSamples)) {
            statusLabel_.setText(TRANS("Unsupported audio sample") + ": " + file.getFileName(), juce::dontSendNotification);
            return;
        }
        writer.reset();
        clip.path = file.getFileNameWithoutExtension() + ".wav";
        clip.localFile = juce::File{};
    }
    clip.startTick = tick;
    clip.durationTicks = std::clamp<juce::int64>(std::ceil(project_->clock.tickAt(project_->clock.secondsAt(tick)
        + reader->lengthInSamples / reader->sampleRate) - tick), 1, end - tick);
    if (!addTrack) for (auto& other : project_->audio) if (other.track == index &&
        tick < waveforms_.endTick(other, project_->clock) && tick + clip.durationTicks > other.startTick) {
        if (prior) { statusLabel_.setText(TRANS("The destination contains another clip."), juce::dontSendNotification); return; }
        prior = &other;
    }
    if (prior && !replace) { confirmReplace(); return; }
    if (!prior && project_->audio.size() >= project_->maxAudioRegions) { statusLabel_.setText(TRANS("Too many audio regions."), juce::dontSendNotification); return; }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    if (addTrack) {
        editTracks(ostfstudio::TrackKind::audio);
        if (project_->sourceTrackCount() != index + 1) return;
        if (project_->renameTrack(index, file.getFileNameWithoutExtension()).wasOk())
            trackPicker_.changeItemText(int(index + 1), file.getFileNameWithoutExtension());
    }
    const auto placedId = clip.regionId;
    if (prior) *prior = std::move(clip);
    else project_->audio.push_back(std::move(clip));
    markSessionEdited();
    waveforms_.load(project_->audio);
    selectTrack(index);
    selectedAudioId_ = placedId;
    selectEditorClip(1. + double(tick) / project_->ticksPerBar, false);
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
    updatePresetLabel(index);
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
    const auto owner = strip.outputTrack.value_or(index);
    if (strip.outputTrack && restoring) return;
    if (!restoring && (owner != index || hasSharedParts(owner))) {
        const auto& host = *trackControls_[owner];
        if (host.loading) return;
        if (host.instance) {
            const auto current = host.instance->getPluginDescription();
            if (current.pluginFormatName == description.pluginFormatName && current.uniqueId == description.uniqueId &&
                current.manufacturerName == description.manufacturerName && current.name == description.name) {
                updatePresetLabel(index);
                openTrackPluginEditor(owner);
                return;
            }
        }
        if (owner != index) {
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, TRANS("Replace the shared instrument?"),
                TRANS("All tracks routed to this instrument will use") + " " + description.name,
                TRANS("Replace"), TRANS("Cancel"), this, juce::ModalCallbackFunction::create(
                    [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, index, owner, description](int yes) {
                        if (yes && safe && safe->projectGeneration_ == generation && safe->trackControls_[index]->outputTrack == owner)
                            safe->loadTrackPlugin(owner, description);
                    }));
            return;
        }
    }
    strip.loading = true;
    refreshTrackActions();
    strip.plugin.setButtonText(TRANS("Loading..."));
    statusLabel_.setText(TRANS("Loading plug-in: ") + description.name, juce::dontSendNotification);
    auto self = juce::Component::SafePointer<MainComponent>(this);
    const auto generation = projectGeneration_;
    auto installed = [self, index, generation, description, restoring, state = std::move(state)]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
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
        target.instance = std::move(instance);
        target.inactiveInstrument.reset();
        target.inactiveInstrumentState.reset();
        if (!restoring) {
            target.instrumentAssignment = instrumentReference(description);
            self->markSessionEdited();
        }
        target.plugin.setButtonText(target.instance->getName());
        self->updatePresetLabel(index);
        self->resized();
        if (!restoring) self->openTrackPluginEditor(index);
    };
    if (restoring && description.name == "OSTF Formant Singer" && description.manufacturerName == "OSTF Studio") {
        juce::String error;
        auto singer = pluginFormats_.createPluginInstance(description, 44100.0, 512, error);
        installed(std::move(singer), error);
        return;
    }
    juce::Timer::callAfterDelay(65, [self, generation, description, installed = std::move(installed)] {
        if (self && self->projectGeneration_ == generation)
            self->pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512, installed);
    });
}

juce::String MainComponent::instrumentWindowTitle(size_t owner) const {
    const auto& strip = *trackControls_[owner];
    juce::StringArray parts;
    for (size_t i = 0; i < project_->sourceTrackCount(); ++i)
        if (trackControls_[i]->outputTrack.value_or(i) == owner)
            parts.add(juce::String::fromUTF8(project_->trackLanes[i].label) + " (Ch " + juce::String(trackControls_[i]->midiChannel) + ")");
    const auto group = hasSharedParts(owner) ? " #" + juce::String(int(owner + 1)) : juce::String{};
    return strip.instance->getName() + group + " / " + parts.joinIntoString(" + ");
}

void MainComponent::openTrackPluginEditor(std::size_t index) {
    if (!project_ || index >= trackControls_.size()) return;
    index = trackControls_[index]->outputTrack.value_or(index);
    if (index >= trackControls_.size()) return;
    auto& strip = *trackControls_[index];
    auto& instance = strip.instance;
    auto& pluginEditor = strip.editor;
    if (!instance) return;
    if (!pluginEditor) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorAndMakeActive());
        if (!editor) editor = std::make_unique<juce::GenericAudioProcessorEditor>(*instance);
        auto window = std::make_unique<PluginWindow>(instrumentWindowTitle(index));
        if (strip.playableInstrument)
            window->programWatch = std::make_unique<ostfstudio::InstrumentProgramWatch>(*instance,
                [w = window.get()] { return w->isVisible(); },
                [this] { return !transportActive_ && !recordTrack_; },
                [this, index](int channel, int program) { offerInstrumentProgram(index, channel, program); });
        std::unique_ptr<juce::Component> body;
        if (isOstfArranger(instance->getPluginDescription())) body = std::make_unique<GeneratorPanel>(std::move(editor), *this);
        else body = std::move(editor);
        window->setContentOwned(body.release(), true);
        window->centreWithSize(window->getWidth(), window->getHeight());
        if (hasSharedParts(index)) {
            for (size_t i = 0; i < trackControls_.size(); ++i) {
                auto* other = trackControls_[i]->editor.get();
                if (!other || !other->isVisible() || !hasSharedParts(i)) continue;
                const auto existing = other->getScreenBounds();
                const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(existing);
                if (!display) continue;
                const auto area = display->userArea;
                constexpr int gap = 8;
                int x = existing.getRight() + gap;
                if (x + window->getWidth() > area.getRight()) {
                    x = existing.getX() - gap - window->getWidth();
                    if (x < area.getX() && existing.getWidth() + gap + window->getWidth() <= area.getWidth()) {
                        x = area.getRight() - window->getWidth();
                        other->setTopLeftPosition(x - gap - existing.getWidth(), existing.getY());
                    } else if (x < area.getX()) {
                        const auto offset = window->getBounds().withPosition(existing.getPosition().translated(32, 32));
                        window->setTopLeftPosition(offset.constrainedWithin(area).getPosition());
                        break;
                    }
                }
                window->setTopLeftPosition(x, juce::jlimit(area.getY(), juce::jmax(area.getY(), area.getBottom() - window->getHeight()), existing.getY()));
                break;
            }
        }
        window->onClosed = [this, index] { updatePresetLabel(index); };
        pluginEditor = std::move(window);
    }
    showStudioWindow(*pluginEditor, *this);
    statusLabel_.setText(strip.playableInstrument ? TRANS("Instrument ready for track playback")
                                                 : TRANS("Plug-in editor ready / audio routing unavailable for this plug-in"),
                         juce::dontSendNotification);
}

void MainComponent::offerInstrumentProgram(std::size_t owner, int channel, int program) {
    if (!project_ || owner >= project_->sourceTrackCount() || recordTrack_) return;
    juce::StringArray choices;
    std::vector<std::size_t> destinations;
    for (size_t i = 0; i < project_->sourceTrackCount(); ++i) {
        const auto& part = *trackControls_[i];
        if (part.outputTrack.value_or(i) != owner || (channel && part.midiChannel != channel)) continue;
        destinations.push_back(i);
        choices.add("#" + juce::String(int(i + 1)) + " " + juce::String::fromUTF8(project_->trackLanes[i].label)
                    + " / Ch " + juce::String(part.midiChannel));
    }
    if (choices.isEmpty()) return;
    auto dialog = std::make_unique<juce::AlertWindow>(TRANS("Write instrument preset to the track?"),
        TRANS("MIDI program (0-127)") + "  " + juce::String(program), juce::MessageBoxIconType::QuestionIcon, trackControls_[owner]->editor.get());
    dialog->addComboBox("track", choices, TRANS("Track"));
    const auto cursor = juce::jlimit<juce::int64>(0, project_->playbackEndTick() - 1, cursorTick_);
    juce::StringArray positions{TRANS("Song start")};
    if (cursor > 0) positions.add(TRANS("Edit cursor") + " / " + juce::String(cursor));
    dialog->addComboBox("position", positions, TRANS("Position"));
    dialog->addButton(TRANS("Apply"), 1);
    dialog->addButton(TRANS("Cancel"), 0);
    auto* w = dialog.release();
    w->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_, owner,
         plugin = trackControls_[owner]->instance.get(), destinations, channel, program, cursor, w](int answer) {
            if (!safe || answer != 1 || safe->recordTrack_ || safe->projectGeneration_ != generation
                || owner >= safe->trackControls_.size() || plugin != safe->trackControls_[owner]->instance.get()) return;
            const int choice = w->getComboBoxComponent("track")->getSelectedItemIndex();
            if (!juce::isPositiveAndBelow(choice, int(destinations.size()))) return;
            const auto track = destinations[size_t(choice)];
            const auto& part = *safe->trackControls_[track];
            if (part.outputTrack.value_or(track) != owner || (channel && part.midiChannel != channel)) return;
            const auto tick = w->getComboBoxComponent("position")->getSelectedItemIndex() == 1 ? cursor : 0;
            const bool playing = safe->transportActive_;
            if (playing) safe->stopTransport();
            juce::MemoryBlock before;
            const bool routed = part.outputTrack.has_value() || safe->hasSharedParts(owner);
            const auto result = safe->project_->setPatchAt(track, tick, part.midiChannel,
                                                            {-1, -1, program}, before, routed, true);
            if (result.wasOk()) safe->rememberScoreEdit(std::move(before));
            else safe->statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
            safe->finishNoteEdit(playing);
        }), true);
}

void MainComponent::updatePresetLabel(std::size_t index) {
    auto& strip = *trackControls_[index];
    juce::String label;
    const auto owner = strip.outputTrack.value_or(index);
    const auto sharedButtonText = [&] {
        const auto& host = *trackControls_[owner];
        const auto name = host.instance ? host.instance->getName() : host.instrumentAssignment["name"].toString();
        return (name == "OSTF GM Sound Module" ? juce::String("GM #") : juce::String("#"))
            + juce::String(int(owner + 1)) + "  Ch " + juce::String(strip.midiChannel);
    };
    if (strip.outputTrack && owner < trackControls_.size() && owner < project_->trackLanes.size()) {
        label = "#" + juce::String(int(*strip.outputTrack + 1)) + " " + juce::String::fromUTF8(project_->trackLanes[*strip.outputTrack].label) + " / Ch " + juce::String(strip.midiChannel);
        strip.plugin.setButtonText(sharedButtonText());
        strip.plugin.setTooltip(TRANS("Open shared instrument") + " / " + label);
        strip.plugin.getProperties().set("sharedPart", true);
    } else if (strip.plugin.getProperties().remove("sharedPart")) {
        const auto fallback = strip.instrumentAssignment.isObject()
            ? strip.instrumentAssignment["name"].toString() + TRANS(" (missing)") : TRANS("Instrument");
        strip.plugin.setButtonText(strip.instance ? strip.instance->getName() : fallback);
        strip.plugin.setTooltip(TRANS("Assign a local plug-in to this track"));
    }
    if (!strip.outputTrack && strip.instance) {
        auto* instance = strip.instance.get();
        if (hasSharedParts(index)) {
            strip.plugin.setButtonText(sharedButtonText());
            strip.plugin.setTooltip(instrumentWindowTitle(index));
        } else {
            strip.plugin.setButtonText(instance->getName());
            strip.plugin.setTooltip(TRANS("Assign a local plug-in to this track"));
        }
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
        for (const auto& c : project_->audio) if (c.track == track) {
            const auto* audio = &c;
            const auto first = project_->bars.firstBar + double(audio->startTick) / project_->ticksPerBar;
            const auto last = project_->bars.firstBar + waveforms_.endTick(*audio, project_->clock) / project_->ticksPerBar;
            clipLane_.clips.push_back({{first, last}, audio->path.fromLastOccurrenceOf("/", false, false)});
        }
        {
            for (const auto& region : project_->trackLanes[track].regions) {
                const auto bars = region.exactBars.value_or(ostfstudio::TimelineExtent{double(region.bars.firstBar), double(region.bars.endBar)});
                const auto end = resizingMidi_ && region.clipId && resizingMidi_->id == region.clipId
                    ? 1. + double(resizingMidi_->end) / project_->ticksPerBar : bars.end;
                clipLane_.clips.push_back({{bars.start, end}, juce::String::fromUTF8(region.label), double(region.loopTicks) / project_->ticksPerBar});
            }
        }
    }
    if (clipLane_.selected && clipLane_.at(*clipLane_.selected) != clipLane_.selected) clipLane_.selected.reset();
    clipLane_.repaint();
}

void MainComponent::editMidiClip(size_t track, const juce::String& id, ostfstudio::OstfpPreview::MidiClipEdit action, juce::int64 tick) {
    if (!project_ || recordTrack_) return;
    const bool playing = transportActive_;
    if (playing) stopTransport();
    juce::MemoryBlock previous;
    const auto result = project_->editMidiClip(track, id, action, tick, previous);
    if (result.wasOk()) {
        rememberScoreEdit(std::move(previous));
        theory_.reset();
        refreshTheory();
        finishNoteEdit(playing);
        repaint();
    } else {
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        if (playing) startTransport(false);
    }
}

void MainComponent::midiClipMenu(size_t track, double bar) {
    if (!project_ || recordTrack_ || track >= project_->sourceTrackCount()) return;
    const auto here = std::llround((bar - 1) * project_->ticksPerBar);
    const auto cursor = cursorTick_;
    const auto hit = clipAt(track, here);
    selectTrack(track);
    selectEditorClip(hit ? std::optional<double>(1. + double(hit->start) / project_->ticksPerBar) : std::nullopt, false);
    arrangementClipFocus_ = true;
    clipInsertionTick_ = here;
    grabKeyboardFocus();
    juce::PopupMenu menu;
    menu.addItem(int(ClipOp::copy), TRANS("Copy clip"), hit.has_value());
    const bool canPaste = copiedAudio_ ? project_->trackKinds[track] == ostfstudio::TrackKind::audio
        : copiedMidi_.data.isObject() && copiedMidi_.kind == project_->trackKinds[track];
    menu.addItem(int(ClipOp::paste), TRANS("Paste clip here"), canPaste);
    menu.addItem(int(ClipOp::duplicate), TRANS("Duplicate clip"), hit.has_value());
    menu.addItem(int(ClipOp::move), TRANS("Move clip to cursor"), hit.has_value());
    juce::int64 period = 0;
    if (hit) {
        menu.addSeparator();
        menu.addItem(1, TRANS("Split clip here"), here > hit->start && here < hit->end);
        menu.addItem(2, TRANS("Split clip at cursor"), cursor > hit->start && cursor < hit->end);
        bool following = false;
        for (const auto& other : clipsOn(track)) if (other.audio == hit->audio && other.start >= hit->end) following = true;
        menu.addItem(int(ClipOp::join), TRANS("Join with next clip"), following);
        if (!hit->audio) {
            for (const auto& region : project_->trackLanes[track].regions)
                if (region.clipId && hit->id == juce::String(region.clipId)) period = region.loopTicks;
            menu.addSeparator();
            menu.addItem(3, TRANS("Loop clip"), true, period > 0);
            menu.addItem(4, TRANS("Add one repeat"), period > 0);
        }
    }
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<MainComponent>(this), generation = projectGeneration_,
                            track, hit, here, cursor, period](int choice) {
        if (!safe || safe->projectGeneration_ != generation || !choice) return;
        using Edit = ostfstudio::OstfpPreview::MidiClipEdit;
        if (choice == 1 || choice == 2) safe->clipCommand(ClipOp::split, hit, track, choice == 1 ? here : cursor);
        else if (choice == 3 || choice == 4) safe->editMidiClip(track, hit->id, choice == 3 ? Edit::toggleLoop : Edit::resizeLoop, hit->end + period);
        else safe->clipCommand(ClipOp(choice), hit, track, choice == int(ClipOp::move) ? cursor : here);
    });
}

bool MainComponent::beginMidiResize(size_t track, double bar, double tolerance) {
    resizingMidi_.reset();
    if (!project_ || recordTrack_ || track >= project_->sourceTrackCount() || clipOn(track)) return false;
    for (const auto& region : project_->trackLanes[track].regions) {
        if (!region.clipId || !region.exactBars || !region.loopTicks || std::abs(bar - region.exactBars->end) > tolerance) continue;
        const auto end = std::llround((region.exactBars->end - 1) * project_->ticksPerBar);
        const auto start = std::llround((region.exactBars->start - 1) * project_->ticksPerBar);
        resizingMidi_ = MidiResize{track, region.clipId, end, end, start + region.loopTicks, project_->playbackEndTick()};
        return true;
    }
    return false;
}

void MainComponent::dragMidiResize(double bar, bool finish) {
    if (!project_ || !resizingMidi_) return;
    auto& edit = *resizingMidi_;
    if (!finish) edit.end = std::clamp<juce::int64>(std::llround((bar - 1) * project_->ticksPerBar), edit.minEnd, edit.maxEnd);
    if (finish) {
        auto done = edit;
        resizingMidi_.reset();
        if (done.firstEnd != done.end)
            editMidiClip(done.track, done.id, ostfstudio::OstfpPreview::MidiClipEdit::resizeLoop, done.end);
    }
    refreshClipLane();
    repaint();
}

void MainComponent::selectEditorClip(std::optional<double> bar, bool locate) {
    clipLane_.selected = bar;
    if (bar && project_ && selectedTrack_) {
        if (auto hit = clipAt(*selectedTrack_, std::llround((*bar - 1) * project_->ticksPerBar)); hit && hit->audio) {
            selectedAudioId_ = hit->id;
            refreshEditor();
        }
    }
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
    juce::int64 frames = 0, first = 0;
    const auto bytes = ostfstudio::clipWav(*clip, source, project_->clock, frames, &first);
    const auto digest = juce::SHA256(source).toHexString();
    if (bytes.isEmpty() || (clip->sha256.isNotEmpty() && digest != clip->sha256)) {
        statusLabel_.setText(TRANS("The source has changed. Create a new working copy."), juce::dontSendNotification);
        return;
    }
    const auto id = clip->regionId;
    auto it = melodyneCopies_.find(id);
    if (it == melodyneCopies_.end() || it->second.originalHash != digest || it->second.startFrame != first
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
        it = melodyneCopies_.insert_or_assign(id, MelodyneCopy{copy, digest, juce::SHA256(bytes).toHexString(), first, frames, clip->startTick, clip->durationTicks}).first;
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
    if (!clip) return;
    const auto it = melodyneCopies_.find(clip->regionId);
    if (!clip || it == melodyneCopies_.end()) return;
    const auto& copy = it->second;
    juce::int64 frames = 0, first = 0;
    const auto current = ostfstudio::clipWav(*clip, sourceWavBytes(*clip), project_->clock, frames, &first);
    if (copy.startFrame != first || copy.startTick != clip->startTick || copy.durationTicks != clip->durationTicks
        || frames != copy.frames || juce::SHA256(current).toHexString() != copy.workingHash) {
        statusLabel_.setText(TRANS("The source has changed. Create a new working copy."), juce::dontSendNotification);
        return;
    }
    if (juce::SHA256(copy.wav).toHexString() == copy.workingHash) {
        statusLabel_.setText(TRANS("Use Replace Audio in Melodyne, then Apply edited WAV"), juce::dontSendNotification);
        return;
    }
    ostfstudio::ProjectAudio edited;
    const auto check = ostfstudio::prepareEditedWav(*clip, it->second.wav, it->second.originalHash, edited, frames, first);
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

void MainComponent::updatePromptGuide() {
    const auto& mode = studioModes[studioModeIndex_];
    promptGuide_.setExamples(mode.name, juce::translate(mode.first), juce::translate(mode.second), juce::translate(mode.third));
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
        if (project_) displayTransportPosition(cursorTick_);
        lastLinkPlaying_ = false;
    } else {
        const auto tempo = project_ ? project_->clock.bpmAt(std::max<juce::int64>(0, cursorTick_)) * auditionRate_ : 120.0;
        lastLinkPeers_ = -1;
        lastLinkTempo_ = 0;
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
    if (int(state.peers) != lastLinkPeers_) {
        lastLinkPeers_ = int(state.peers);
        statusLabel_.setText(lastLinkPeers_ == 0 ? TRANS("Ableton Link: waiting for peers")
            : "Ableton Link: " + juce::String(lastLinkPeers_) + " " + TRANS("peers"), juce::dontSendNotification);
    }
    if (std::abs(lastLinkTempo_ - state.tempo) > .001) {
        lastLinkTempo_ = state.tempo;
        if (!transportActive_) displayTransportPosition(cursorTick_);
    }
    if (state.playing != lastLinkPlaying_) {
        if (state.playing && !transportActive_ && !canEditTracks()) return;
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
        showStudioWindow(*spliceWindow_, *this);
        updateSplicePlayback();
        return;
    }
    if (spliceLoading_) return;
    auto& browser = ensurePluginBrowser();
    for (const auto& type : browser.types()) {
        if (type.pluginFormatName != "VST3" || !type.name.trim().equalsIgnoreCase("Splice Sounds")) continue;
        auto* device = ensureDeviceSettings().devices().getCurrentAudioDevice();
        spliceLoading_ = true;
        statusLabel_.setText(TRANS("Loading Splice Sounds..."), juce::dontSendNotification);
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
                auto window = std::make_unique<PluginWindow>("Splice Sounds");
                const auto resizable = editor->isResizable();
                window->setResizable(resizable, false);
                window->setContentOwned(editor.release(), true);
                window->centreAroundComponent(safe.getComponent(), window->getWidth(), window->getHeight());
                window->onClosed = [safe] { if (safe) safe->updateSplicePlayback(); };
                safe->spliceWindow_ = std::move(window);
                showStudioWindow(*safe->spliceWindow_, *safe);
                safe->statusLabel_.setText("Splice Sounds", juce::dontSendNotification);
                safe->updateSplicePlayback();
            });
        return;
    }
    statusLabel_.setText(TRANS("Install the Splice Sounds instrument from Splice, then scan your VST3 plugins."), juce::dontSendNotification);
}

void MainComponent::updateSplicePlayback() {
    const bool active = spliceWindow_ && spliceWindow_->isVisible();
    if (active == spliceAuditionActive_) return;
    if (active) deviceSettings_->devices().addAudioCallback(&splicePlayer_);
    else deviceSettings_->devices().removeAudioCallback(&splicePlayer_);
    spliceAuditionActive_ = active;
}

void MainComponent::openEffectEditor(bool output, std::size_t index, std::size_t position) {
    auto& slot = effectChain(output, index).slots[position];
    if (!slot.editor) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(slot.effect->createEditorAndMakeActive());
        if (!editor) editor = std::make_unique<juce::GenericAudioProcessorEditor>(*slot.effect);
        auto window = std::make_unique<PluginWindow>(slot.effect->getName());
        window->setContentOwned(editor.release(), true);
        window->centreWithSize(window->getWidth(), window->getHeight());
        slot.editor = std::move(window);
    }
    showStudioWindow(*slot.editor, *this);
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
        (promptGuide_.isVisible() && promptGuide_.getLocalBounds().contains(promptGuide_.getLocalPoint(this, p))) ||
        !rowOverlay_.getBounds().withTrimmedTop(timelineRowHeight()).contains(p))
        return {};
    int y = rowOverlay_.getY() + timelineRowHeight() - juce::roundToInt(verticalScroll_.getCurrentRangeStart());
    const int height = groupDefaults_[static_cast<std::size_t>(HeightGroup::lanes)];
    auto view = currentArrangement();
    for (const auto& lane : view.globalLanes) {
        if (p.y >= y && p.y < y + height) {
            if (lane.kind == TimelineLane::displayLyrics || lane.kind == TimelineLane::pronunciation) return {};
            return lane.kind;
        }
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

void MainComponent::editGlobalBoundary(bool split) {
    if (!project_ || !selectedGlobal_ || recordTrack_) return;
    if (chordReviewBlocksSwitch()) return;
    juce::Array<juce::var> removed, added;
    auto tick = selectedGlobalTick_;
    if (split && (*selectedGlobal_ == TimelineLane::meter || *selectedGlobal_ == TimelineLane::section))
        tick = std::llround(double(tick)/project_->ticksPerBar)*project_->ticksPerBar;
    auto result = ostfstudio::globalBoundaryEdit(juce::JSON::parse(project_->sourceScore.toString()), *selectedGlobal_,
        tick, project_->durationTicks, split, joinKeepsRight_, removed, added);
    if (result.failed()) statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
    else applyGlobalChange(*selectedGlobal_, removed, added);
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
            editSongInformation();
        return;
    }
    if (command == 2) {
        copiedLane_ = lane;
        copiedEvent_ = event.clone();
        copiedPpq_ = project_->ppq;
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
    reorderSource_.reset();
    reorderTarget_.reset();
    if (rowOverlay_.isVisible() && viewportPan_.begin(*this, event, rowOverlay_.getBounds().withTrimmedLeft(trackHeaderWidth))) return;
    resizingAudio_.reset();
    resizingMidi_.reset();
    draggingMidi_.reset();
    dragPreview_.reset();
    arrangementClipFocus_ = false;
    pendingMidiDrag_.reset();
    const bool wasTempo = selectedGlobal_ == TimelineLane::tempo;
    if (!rowOverlay_.isVisible() || !rowOverlay_.getBounds().contains(event.getPosition())) return;
    if (auto lane = globalLaneAt(event.getPosition())) {
        if (chordReviewBlocksSwitch() || (*lane == TimelineLane::tempo && recordTrack_)) return;
        clipLane_.selected.reset();
        selectedGlobal_ = lane;
        selectedGlobalTick_ = event.x < rowOverlay_.getX() + trackHeaderWidth
            ? std::max<juce::int64>(0, cursorTick_) : arrangementTickAt(event.x);
        if (clipTool_ != ClipTool::select && event.mods.isLeftButtonDown() && !event.mods.isPopupMenu()
            && event.x >= rowOverlay_.getX() + trackHeaderWidth) { editGlobalBoundary(clipTool_ == ClipTool::split); return; }
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
            menu.addItem(5, TRANS("Split here"));
            menu.addItem(6, TRANS("Join with next interval"));
            juce::PopupMenu policy;
            policy.addItem(7, TRANS("Keep earlier value"), true, !joinKeepsRight_);
            policy.addItem(8, TRANS("Keep later value"), true, joinKeepsRight_);
            menu.addSubMenu(TRANS("Value after joining"), policy);
            menu.addItem(4, TRANS("Paste"), copiedLane_ == lane);
            const auto generation = projectGeneration_;
            menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                               [safe = juce::Component::SafePointer<MainComponent>(this), generation](int choice) {
                                   if (safe && safe->projectGeneration_ == generation && choice)
                                   {
                                       if (choice == 5 || choice == 6) safe->editGlobalBoundary(choice == 5);
                                       else if (choice == 7 || choice == 8) {
                                           safe->joinKeepsRight_ = choice == 8;
                                           safe->viewPreferences_.getUserSettings()->setValue("globalJoinKeepsRight", safe->joinKeepsRight_);
                                       } else safe->globalCommand(choice);
                                   }
                               });
        }
        return;
    }
    selectedGlobal_.reset();
    if (wasTempo) refreshEditor();
    if (event.mods.isPopupMenu()) {
        if (auto output = outputDropTrack(event.getPosition())) showRowMenu(true, *output);
        else if (auto track = pluginDropTrack(event.getPosition())) {
            const auto bar = 1. + double(arrangementTickAt(event.x)) / project_->ticksPerBar;
            if (event.x >= rowOverlay_.getX() + trackHeaderWidth && *track < project_->sourceTrackCount()) {
                midiClipMenu(*track, bar);
                return;
            }
            showRowMenu(false, *track);
        }
        return;
    }
    for (std::size_t i = 0; i < trackRows_.size(); ++i) {
        if (trackRows_[i].contains(event.getPosition())) {
            if (event.mods.isLeftButtonDown() && trackOrderHandle(i).contains(event.getPosition())) {
                reorderSource_ = i;
                return;
            }
            if (event.mods.isLeftButtonDown() && trackRows_[i].withWidth(trackNameWidth).contains(event.getPosition())
                && canExportTrack(i))
                pendingMidiDrag_ = i;
            selectTrack(i);
            if (selectedTrack_ != i) { pendingMidiDrag_.reset(); return; }
            const auto timeline = trackRows_[i].withTrimmedLeft(trackHeaderWidth);
            const auto bar = project_->bars.firstBar + double(arrangementTickAt(event.x)) / project_->ticksPerBar;
            if (event.mods.isLeftButtonDown() && timeline.contains(event.getPosition())) {
                arrangementClipFocus_ = true;
                clipInsertionTick_ = arrangementTickAt(event.x);
                grabKeyboardFocus();
                if (clipTool_ != ClipTool::select) {
                    if (i == project_->chordVoicingTrack) {
                        selectedGlobal_ = TimelineLane::chord;
                        selectedGlobalTick_ = clipInsertionTick_;
                        editGlobalBoundary(clipTool_ == ClipTool::split);
                    } else if (auto clip = clipAt(i, clipInsertionTick_))
                        clipCommand(clipTool_ == ClipTool::split ? ClipOp::split : ClipOp::join, clip, i, clipInsertionTick_);
                    return;
                }
            }
            if (event.mods.isLeftButtonDown() && timeline.contains(event.getPosition()) &&
                beginMidiResize(i, bar, (horizontalRange().end - horizontalRange().start) * 7 / std::max(1, timeline.getWidth()))) return;
            selectEditorClip(timeline.contains(event.getPosition()) ? clipLane_.at(bar) : std::nullopt, false);
            auto hit = timeline.contains(event.getPosition()) ? clipAt(i, arrangementTickAt(event.x)) : std::nullopt;
            if (hit && !hit->audio && !recordTrack_ && event.mods.isLeftButtonDown()) {
                draggingMidi_ = hit;
                midiGrabOffset_ = arrangementTickAt(event.x) - hit->start;
                return;
            }
            // A trailing edge can be picked just outside the region.
            if (!hit && timeline.contains(event.getPosition()))
                for (const auto& item : clipsOn(i)) if (item.audio && std::abs(item.end - arrangementTickAt(event.x)) <=
                    std::abs(arrangementTickAt(event.x + 7) - arrangementTickAt(event.x))) { selectedAudioId_ = item.id; break; }
            if (auto* clip = clipOn(i); clip && !recordTrack_ && event.mods.isLeftButtonDown() && timeline.contains(event.getPosition())) {
                const auto& clock = auditionClock_ ? *auditionClock_ : project_->clock;
                const auto range = horizontalRange();
                const double bar = 1 + waveforms_.endTick(*clip, clock) / project_->ticksPerBar;
                const double edge = timeline.getX() + (bar - range.start) / (range.end - range.start) * timeline.getWidth();
                const double left = edge - (waveforms_.endTick(*clip, clock) - clip->startTick) / project_->ticksPerBar
                    / (range.end - range.start) * timeline.getWidth();
                resizingAudioStart_ = !clip->timing.loop && std::abs(event.x - left) <= 7 && std::abs(event.x - left) < std::abs(event.x - edge);
                const bool atEnd = std::abs(event.x - edge) <= 8;
                movingAudio_ = !atEnd && !resizingAudioStart_ && event.x >= left && event.x < edge;
                if ((movingAudio_ || resizingAudioStart_ || atEnd) && waveforms_.available(*clip)) {
                    selectedAudioId_ = clip->regionId;
                    audioGrabOffset_ = arrangementTickAt(event.x) - clip->startTick;
                    resizingAudio_ = i;
                    previewAudioStart_ = clip->startTick;
                    previewAudioLength_ = std::max<juce::int64>(1, std::llround(waveforms_.endTick(*clip, clock)) - clip->startTick);
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
    if (clipTool_ != ClipTool::select) return;
    if (globalLaneAt(p) && selectedGlobal_) {
        globalCommand(1);
        return;
    }
    if (!rowOverlay_.getBounds().withWidth(trackNameWidth).contains(p))
        return;
    if (auto track = pluginDropTrack(p)) {
        if (!trackOrderHandle(*track).contains(p)) renameTrack(*track);
    }
    else if (auto out = outputDropTrack(p))
        renameTrack(*out, true);
}

void MainComponent::mouseDrag(const juce::MouseEvent& event) {
    if (viewportPan_.move(event)) return;
    if (reorderSource_) {
        if (event.getDistanceFromDragStart() < 4) return;
        const auto pos = event.getPosition();
        const auto viewport = rowOverlay_.getBounds().withTrimmedTop(timelineRowHeight());
        if (viewport.contains(pos) && verticalScroll_.isVisible()) {
            if (pos.y < viewport.getY() + 18) verticalScroll_.moveScrollbarInSteps(-1);
            else if (pos.y >= viewport.getBottom() - 18) verticalScroll_.moveScrollbarInSteps(1);
        }
        reorderTarget_.reset();
        if (auto target = pluginDropTrack(pos); target && canReorderTogether(*reorderSource_, *target)) {
            reorderTarget_ = target;
            reorderAfter_ = pos.y >= trackRows_[*target].getCentreY();
        }
        repaint();
        return;
    }
    if (draggingMidi_) {
        const auto track = pluginDropTrack(event.getPosition());
        dragPreview_.reset();
        if (track && project_->trackKinds[*track] == project_->trackKinds[draggingMidi_->track]) {
            const auto start = std::max<juce::int64>(0, arrangementTickAt(event.x, true) - midiGrabOffset_);
            dragPreview_ = ClipHit{*track, draggingMidi_->id, start, start + draggingMidi_->end - draggingMidi_->start, false};
        }
        repaint();
        return;
    }
    if (resizingMidi_) { dragMidiResize(1. + double(arrangementTickAt(event.x, true)) / project_->ticksPerBar, false); return; }
    if (resizingAudio_) {
        if (const auto* clip = clipOn(*resizingAudio_)) {
            const auto& clock = auditionClock_ ? *auditionClock_ : project_->clock;
            const auto end = std::llround(waveforms_.endTick(*clip, clock));
            const auto tick = arrangementTickAt(event.x, true);
            if (movingAudio_) {
                previewAudioStart_ = std::clamp<juce::int64>(tick - audioGrabOffset_, 0,
                    std::max<juce::int64>(0, project_->playbackEndTick() - previewAudioLength_));
            } else if (resizingAudioStart_) {
                previewAudioStart_ = std::clamp<juce::int64>(tick, 0, std::min<juce::int64>(clip->sourceTick(), end - 1));
                previewAudioLength_ = end - previewAudioStart_;
            } else {
                previewAudioLength_ = std::clamp<juce::int64>(tick - clip->startTick, clip->timing.leadingTicks + 1,
                                                               project_->playbackEndTick() - clip->startTick);
            }
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
    if (reorderSource_) {
        const auto source = *reorderSource_;
        reorderSource_.reset();
        reorderTarget_.reset();
        if (event.mouseWasDraggedSinceMouseDown())
            if (auto target = pluginDropTrack(event.getPosition()))
                placeTrackRow(source, *target, event.y >= trackRows_[*target].getCentreY());
        repaint();
        return;
    }
    if (resizingMidi_) { dragMidiResize(1. + double(arrangementTickAt(event.x, true)) / project_->ticksPerBar, true); return; }
    if (draggingMidi_) {
        auto moving = *draggingMidi_;
        dragPreview_.reset();
        draggingMidi_.reset();
        auto destination = pluginDropTrack(event.getPosition());
        if (destination && event.mouseWasDraggedSinceMouseDown())
            clipCommand(ClipOp::move, moving, *destination, std::max<juce::int64>(0, arrangementTickAt(event.x, true) - midiGrabOffset_));
        repaint();
        return;
    }
    if (!resizingAudio_) return;
    auto* clip = clipOn(*resizingAudio_);
    resizingAudio_.reset();
    if (!clip || recordTrack_ || !event.mouseWasDraggedSinceMouseDown()) return;
    const auto destination = pluginDropTrack(event.getPosition());
    if (movingAudio_) {
        if (destination) clipCommand(ClipOp::move, clipAt(clip->track, clip->startTick), *destination, previewAudioStart_);
        repaint();
        return;
    }
    for (const auto& other : clipsOn(clip->track))
        if (other.id != clip->regionId && previewAudioStart_ < other.end && previewAudioStart_ + previewAudioLength_ > other.start) { repaint(); return; }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    auto before = project_->audio;
    {
        const auto& clock = auditionClock_ ? *auditionClock_ : project_->clock;
        clip->resizeBounds(previewAudioStart_, previewAudioStart_ + previewAudioLength_, std::llround(waveforms_.endTick(*clip, clock)));
    }
    rememberAudioEdit(std::move(before));
    markSessionEdited();
    finishNoteEdit(playing);
}

void MainComponent::selectTrack(std::size_t index) {
    if (selectedTrack_ != index && chordReviewBlocksSwitch()) {
        trackPicker_.setSelectedId(selectedTrack_ ? int(*selectedTrack_+1) : 0,juce::dontSendNotification);
        return;
    }
    if (recordTrack_ && *recordTrack_ != index) return;
    if (!project_ || index >= project_->trackLanes.size()) return;
    selectedGlobal_.reset();
    editorVisible_ = true;
    if (selectedTrack_ != index) { clipLane_.selected.reset(); selectedAudioId_.clear(); }
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
    vocalLane_.phrases.clear();
    grid.kitPitches.clear();
    pianoMaxEnds_.clear();
    grid.editable = false;
    if (!project_ || !selectedTrack_) { grid.resetRange(false); return; }
    const auto track = *selectedTrack_;
    grid.notes = project_->pitchedNotes[track];
    if (track < project_->sourceTrackCount()) {
        const auto docs = project_->trackLyricDocuments.find(project_->trackIds[int(track)]);
        for (const auto& phrase : project_->displayPhrases)
            if (docs != project_->trackLyricDocuments.end() ? docs->second.contains(phrase.document)
                : project_->primaryMelodyTrack == track && phrase.document == project_->displayDocument)
                vocalLane_.phrases.push_back(phrase);
    }
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
    if ((!theoryVisible_ && !chordFitVisible_ && !scaleFitVisible_ && pianoColours_.getSelectedId() == plainNotes) || !project_) { theory_.reset(); theoryTracks_.clear(); clipFits_.clear(); return; }
    if (!theory_) {
        theory_ = std::make_unique<ostfstudio::TheoryGuide>(juce::JSON::parse(project_->sourceScore.toString()),
                                                            project_->playbackEndTick());
    }
    theoryTracks_.resize(project_->trackKinds.size());
    clipFits_.resize(project_->trackKinds.size());
    for (std::size_t i = 0; i < theoryTracks_.size(); ++i) refreshTrackTheory(i);
}

void MainComponent::refreshTrackTheory(std::size_t track) {
    if (theory_ && track < theoryTracks_.size() && project_->trackKinds[track] == ostfstudio::TrackKind::pitched)
        theoryTracks_[track] = theory_->analyse(track, project_->pitchedNotes[track], scaleReference_, &clipFits_[track]);
}

const ostfstudio::TheoryGuide::ClipFit& MainComponent::clipFit(size_t track, const char* clipId, size_t chordBegin) const {
    static const ostfstudio::TheoryGuide::ClipFit noNotes;
    const auto& clips = draggedNote_ && track == selectedTrack_ ? previewClipFits_ : clipFits_[track];
    const auto clip = clips.find(juce::String::fromUTF8(clipId));
    if (clip == clips.end()) return noNotes;
    const auto interval = clip->second.find(chordBegin);
    return interval == clip->second.end() ? noNotes : interval->second;
}

juce::Rectangle<int> MainComponent::drawFitBadges(juce::Graphics& g, juce::Rectangle<int> caption,
                                                  const ostfstudio::TheoryGuide::ClipFit& fit) const {
    const int count = int(chordFitVisible_) + int(scaleFitVisible_);
    const int width = std::min(94, caption.getWidth() / std::max(1, count));
    for (bool chord : {false, true}) {
        if (chord ? !chordFitVisible_ : !scaleFitVisible_) continue;
        auto bounds = caption.removeFromRight(width).withHeight(std::min(18, caption.getHeight()));
        if (width >= 10) bounds = bounds.reduced(1, 0);
        if (bounds.isEmpty()) continue;
        const int value = (chord ? fit.chord : fit.scale).percent();
        const juce::Colour accent(value < 0 ? 0xffb5c0cb : value < 50 ? 0xffff91a4 : value < 75 ? 0xffffb06d :
                                  value < 90 ? 0xffffdf77 : 0xff8ce8ce);
        g.setColour(juce::Colour(0xff0b131b));
        g.fillRoundedRectangle(bounds.toFloat(), 3.f);
        g.setColour(accent);
        g.drawRoundedRectangle(bounds.toFloat().reduced(.75f), 2.5f, 1.35f);
        if (width < 21) {
            g.fillRect(bounds.reduced(std::min(2, bounds.getWidth() / 3), 4));
            continue;
        }
        auto label = value < 0 ? juce::String::charToString(0x2014) : juce::String(value);
        if (value >= 0 && width >= 30) label += "%";
        if (width >= 77) label = juce::translate(chord ? "Chord" : "Scale") + " " + label;
        else if (width >= 48) label = juce::String(chord ? "C " : "S ") + label;
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(width < 30 ? 10.f : width < 55 ? 11.f : 13.f, juce::Font::bold));
        g.drawText(label, bounds.reduced(2, 0), juce::Justification::centred);
    }
    return caption;
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
        if (trackOrderHandle(i).contains(p)) return TRANS("Drag to reorder tracks within this group");
        if (trackRows_[i].withWidth(trackNameWidth).contains(p) && !trackRows_[i].isEmpty())
            return juce::String::fromUTF8(project_->trackLanes[i].label) + "  /  " +
                (project_->trackKinds[i] == ostfstudio::TrackKind::drum ? TRANS("Drum / percussion") :
                 project_->trackKinds[i] == ostfstudio::TrackKind::audio ? TRANS("Audio track") : TRANS("Pitched track"));
        const auto row = trackRows_[i].withTrimmedLeft(trackHeaderWidth);
        if (row.isEmpty()) continue;
        if (!row.contains(p) || !theory_ || i >= clipFits_.size() || (!chordFitVisible_ && !scaleFitVisible_) ||
            project_->trackKinds[i] != ostfstudio::TrackKind::pitched) continue;
        const auto bars = horizontalRange();
        const auto bar = bars.start + double(p.x-row.getX()) / row.getWidth() * (bars.end-bars.start);
        const auto cell = theory_->firstCell((bar - 1.) * project_->ticksPerBar);
        if (cell >= theoryTracks_[i].size()) continue;
        for (const auto& region : project_->trackLanes[i].regions) {
            if (!region.exactBars || bar < region.exactBars->start || bar >= region.exactBars->end) continue;
            if (const auto* id = region.clipId ? region.clipId : region.fitId) {
                const auto& fit = clipFit(i, id, theoryTracks_[i][cell].chordBegin);
                juce::StringArray values;
                const int chord = theory_->cells[cell].chord;
                if (chord >= 0) values.add(theory_->chords[size_t(chord)].name);
                auto add = [&](const char* label,int value) {values.add(juce::translate(label) + "  " + (value<0 ? juce::String::charToString(0x2014) : juce::String(value)+"%"));};
                if (chordFitVisible_) add("Chord fit",fit.chord.percent());
                if (scaleFitVisible_) add("Scale fit",fit.scale.percent());
                return values.joinIntoString("\n");
            }
        }
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
    previewFits_ = theory_->analyse(track, notes, scaleReference_, &previewClipFits_);
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
    const auto affected = project_->trackKinds[*selectedTrack_] == ostfstudio::TrackKind::pitched
        ? ostfstudio::changedNoteRanges(project_->pitchedNotes[*selectedTrack_],edits) : std::vector<ostfstudio::ReviewRange>{};
    const auto result = project_->editNotes(*selectedTrack_, edits, before, ids);
    if (result.failed()) {
        if (playing && !liveLoop) startTransport(false);
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        return;
    }
    rememberScoreEdit(std::move(before));
    if (!affected.empty()) {
        auto& revision = noteHistory_.back();
        revision.reviewBefore = pendingReview_;
        revision.reviewTrack = selectedTrack_;
        const ostfstudio::ChordAnalysis harmony(*project_);
        auto changes = harmony.review(affected);
        for (auto& change : changes) {
            auto position = change.range.getStart();
            ostfstudio::ChordDecisions gaps;
            auto appendGap = [&](juce::int64 end) {
                if (end <= position) return;
                auto part = change;
                part.range = {position,end};
                if (part.range != change.range)
                    part.candidates = harmony.rank(part.range);
                part.choice = part.candidates.isEmpty() ? 0 : 1;
                gaps.push_back(std::move(part));
            };
            for (auto& row : pendingReview_) if (row.range.intersects(change.range)) {
                appendGap(row.range.getStart());
                position = row.range.getEnd();
                row.candidates = row.range == change.range ? change.candidates
                    : harmony.rank(row.range);
                if (row.choice != 4) row.choice = row.candidates.isEmpty() ? 0 : 1;
            }
            appendGap(change.range.getEnd());
            pendingReview_.insert(pendingReview_.end(),gaps.begin(),gaps.end());
        }
        std::sort(pendingReview_.begin(),pendingReview_.end(),[](auto& a,auto& b){return a.range.getStart()<b.range.getStart();});
        ostfstudio::rememberReviewedNotes(pendingReview_, revision.before, *selectedTrack_, edits, ids,
                                         project_->pitchedNotes[*selectedTrack_]);
        revision.reviewAfter = pendingReview_;
        refreshChordReview();
    }
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
    if ((!project_->addedClips.empty() && revision.panTrack.isEmpty() && !revision.beforeAudio) || project_->sourceScore != (redo ? revision.before : revision.after)) {
        noteHistory_.clear(); noteHistoryCursor_ = 0; return;
    }
    const bool playing = transportActive_;
    if (playing) stopTransport();
    if (revision.beforeAudio) {
        if (project_->audio != (redo ? *revision.beforeAudio : *revision.afterAudio)) {
            statusLabel_.setText(TRANS("The clips have changed since this edit."), juce::dontSendNotification);
            if (playing) startTransport(false);
            return;
        }
        const auto& score = redo ? revision.after : revision.before;
        if (score != project_->sourceScore && project_->restoreNoteScore(score).failed()) { if (playing) startTransport(false); return; }
        project_->audio = redo ? *revision.afterAudio : *revision.beforeAudio;
        waveforms_.load(project_->audio);
        if (redo) ++noteHistoryCursor_; else --noteHistoryCursor_;
        markSessionEdited();
        finishNoteEdit(playing);
        repaint();
        return;
    }
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
        if (revision.reviewBefore) {
            const auto track = revision.reviewTrack;
            auto restored = redo ? *revision.reviewAfter : *revision.reviewBefore;
            pendingReview_.clear();
            if (!restored.empty() && track && selectedTrack_ != track) selectTrack(*track);
            pendingReview_ = std::move(restored);
            refreshChordReview();
        }
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
    const auto kind = vocalDepthKind(label);
    const bool editable = performanceLane_.isVisible() && (pan || kind.isNotEmpty() || controllerKeys_.contains(label));
    lane.setVisible(editable);
    enterPerformance_.setVisible(editable || (performanceLane_.isVisible() && label == "Velocity" && pianoGrid_.editable));
    enterPerformance_.setEnabled(!recordTrack_);
    if (!editable || !selectedTrack_ || !project_) return;
    if (kind.isNotEmpty()) {
        lane.maximum = 100;
        lane.linear = false;
        lane.bipolar = false;
        for (const auto& note : pianoGrid_.notes) {
            const auto* data = vocalLane_.lookup(note.id);
            if (data && data->technique == kind) lane.points.push_back({note.startTick,data->intensity,note.id});
        }
    } else if (pan) {
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

void MainComponent::enterPerformanceValue() {
    if (!project_ || !selectedTrack_ || recordTrack_) return;
    const auto name = performanceLaneNames_[performanceLane_.getSelectedId()-1];
    const bool velocity = name == "Velocity";
    const bool pan = name == "Pan automation";
    const bool vocal = vocalDepthKind(name).isNotEmpty();
    if (!velocity && !pan && !vocal && !controllerKeys_.contains(name)) return;
    const bool bend = !velocity && !pan && !vocal && controllerKeys_.at(name).type == "pitchBend";
    const int low = velocity ? 1 : pan ? -100 : bend ? -8192 : 0;
    const int high = pan || vocal ? 100 : bend ? 8191 : 127;
    const auto tick = std::clamp<juce::int64>(cursorTick_,0,project_->playbackEndTick()-1);
    int current = low;
    if (velocity) {
        const auto it = std::find_if(pianoGrid_.notes.begin(), pianoGrid_.notes.end(),
            [&](const auto& note){ return pianoGrid_.selection.contains(note.id); });
        if (it == pianoGrid_.notes.end()) {
            statusLabel_.setText(TRANS("Select notes to enter velocity."), juce::dontSendNotification);
            return;
        }
        current = it->velocity.value_or(96);
    } else {
        current = 0;
        for (const auto& p : controllerEditor_.points)
            if (p.tick <= tick) current = low + juce::roundToInt(p.value*(high-low));
    }
    auto* window = new juce::AlertWindow(juce::translate(name), {}, juce::MessageBoxIconType::NoIcon, this);
    if (!velocity) window->addTextEditor("tick", juce::String(tick), TRANS("Position (ticks)"));
    window->addTextEditor("value", juce::String(current),
        TRANS("Value") + " (" + juce::String(low) + " ... " + juce::String(high) + (pan ? " %)" : ")"));
    window->addButton(TRANS("Apply"),1,juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(TRANS("Cancel"),0,juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,juce::ModalCallbackFunction::create(
        [=, safe=juce::Component::SafePointer<MainComponent>(this), track=selectedTrack_, generation=projectGeneration_](int ok) {
            if (!ok || !safe || safe->projectGeneration_ != generation || safe->selectedTrack_ != track || safe->recordTrack_
                || safe->performanceLaneNames_[safe->performanceLane_.getSelectedId()-1] != name) return;
            const auto readInteger = [&](const char* field, juce::int64 min, juce::int64 max, juce::int64& result) {
                const auto text = window->getTextEditorContents(field).trim();
                auto digits = text.startsWithChar('-') ? text.substring(1) : text;
                if (digits.isEmpty() || digits.length() > 18 || !digits.containsOnly("0123456789")) return false;
                result = text.getLargeIntValue();
                return result >= min && result <= max;
            };
            juce::int64 value = 0, at = tick;
            if (!readInteger("value",low,high,value) || (!velocity && !readInteger("tick",0,safe->project_->playbackEndTick()-1,at))) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,juce::translate(name),
                    TRANS("Enter an integer in the displayed range."));
                return;
            }
            if (velocity) safe->pianoGrid_.shapeVelocity(int(value),int(value));
            else safe->editPerformancePoint({},at,double(value-low)/(high-low));
        }),true);
}

void MainComponent::editPerformancePoint(juce::String id, juce::int64 tick, std::optional<double> value) {
    if (!project_ || !selectedTrack_ || recordTrack_) return;
    const auto label = performanceLaneNames_[performanceLane_.getSelectedId() - 1];
    const bool playing = transportActive_;
    if (playing) stopTransport();
    const auto track = *selectedTrack_;
    if (const auto kind = vocalDepthKind(label); kind.isNotEmpty()) {
        const auto& notes = pianoGrid_.notes;
        const auto note = std::find_if(notes.begin(),notes.end(),[&](const auto& n) {
            return id.isNotEmpty() ? n.id == id : n.startTick <= tick && tick < n.endTick;
        });
        if (note == notes.end()) {
            statusLabel_.setText(TRANS("Choose a position inside a sung note."),juce::dontSendNotification);
            if (playing) startTransport(false);
            return;
        }
        const auto* data = vocalLane_.lookup(note->id);
        auto change = data ? *data : ostfstudio::VocalNote{};
        change.technique = value ? kind : juce::String{};
        if (value) change.intensity = *value;
        juce::MemoryBlock before;
        const auto result = project_->editVocalNote(track,note->id,change,before);
        if (result.failed()) statusLabel_.setText(juce::translate(result.getErrorMessage()),juce::dontSendNotification);
        else if (!before.isEmpty()) rememberScoreEdit(std::move(before));
    } else if (label == "Pan automation") {
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
            if (!scoreView_->isVisible() || lead != scoreView_->showsLeadSheet())
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
    attachPadInput(project_ != nullptr || mackiePort_.isNotEmpty());
    updateDrumLoop();
    pianoGrid_.setVisible(piano && selectedTrack_.has_value() && !steps);
    const bool vocalTrack = pianoGrid_.isVisible() && selectedTrack_ && *selectedTrack_ < project_->sourceTrackCount()
        && (project_->isGlobalNoteTrack(*selectedTrack_) || project_->lyricTrackIds.contains(project_->trackIds[int(*selectedTrack_)]))
        && project_->chordVoicingTrack != selectedTrack_;
    vocalLane_.setVisible(vocalTrack);
    if (vocalTrack) {
        vocalLane_.entrance.setEnabled(project_->lyricTrackIds.contains(project_->trackIds[int(*selectedTrack_)]) && !recordTrack_);
        vocalLane_.voice.setEnabled(!recordTrack_);
    }
    updateChordReviewLane();
    pianoRuler_.setVisible(pianoGrid_.isVisible());
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
    chordFitButton_.setEnabled(arranging && project_ != nullptr);
    scaleFitButton_.setEnabled(arranging && project_ != nullptr);
    chordFitButton_.setToggleState(chordFitVisible_, juce::dontSendNotification);
    scaleFitButton_.setToggleState(scaleFitVisible_, juce::dontSendNotification);
    theoryButton_.setEnabled(arranging && project_ != nullptr);
    theoryButton_.setToggleState(theoryVisible_ && arranging, juce::dontSendNotification);
    lanesButton_.setEnabled(arranging);
    trackPicker_.setVisible(arranging && project_ && editorVisible_ && !tempo);
    trackPicker_.setEnabled(!recordTrack_ && project_ && !project_->trackLanes.empty());
    performanceLane_.setVisible(piano && selectedTrack_ && !steps);
    const auto previous = performanceLaneNames_[performanceLane_.getSelectedId() - 1];
    performanceLaneNames_ = juce::StringArray{"Velocity"};
    if (vocalTrack) performanceLaneNames_.addArray({"Scoop depth", "Vibrato depth"});
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
            juce::PopupMenu polyPressure;
            for (int group = 0; group < 128; group += 16) {
                juce::PopupMenu notes;
                for (int note = group; note < group + 16; ++note) add({"polyPressure",note},&notes);
                if (notes.getNumItems()) polyPressure.addSubMenu(juce::String(group) + "-" + juce::String(group+15), notes);
            }
            if (polyPressure.getNumItems()) performanceLane_.getRootMenu()->addSubMenu(TRANS("Polyphonic aftertouch"), polyPressure);
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
    pianoDivider_.setVisible(arranging && project_ && editorVisible_);
    pianoDivider_.setTooltip(showMixer ? TRANS("Drag to resize the arrangement and mixer.") : TRANS("Drag to resize the arrangement and piano roll."));
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
    const int width = toolbarButtonWidth(getWidth());
    const auto place = [&](ViewButton& button) { button.setBounds(row.removeFromLeft(width).reduced(2, 2)); };
    toolbarDividers_.clear();
    const auto divider = [&] {
        const auto gap = row.removeFromLeft(8);
        toolbarDividers_.push_back(gap.withSizeKeepingCentre(1, 19));
    };
    for (auto* button : {&arrangementButton_, &leadButton_, &scoreButton_, &pianoButton_, &mixerButton_}) place(*button);
    divider();
    for (auto* button : {&pluginBrowserButton_, &sampleBrowserButton_, &outputButton_, &lanesButton_, &theoryButton_, &chordFitButton_, &scaleFitButton_}) place(*button);
    divider();
    place(songInfoButton_); place(musicButton_);
    divider();
    place(splitTool_); place(joinTool_);
    if (getWidth() < 964) row = viewBar.removeFromTop(32);
    else divider();
    for (auto* button : {&melodyAdd_, &chorusAdd_, &pitchedAdd_, &drumsAdd_, &audioAdd_}) place(*button);
    divider();
    place(busAdd_); place(returnAdd_);
    divider();
    place(uiSmaller_); place(uiLarger_);
    auto aiRow = viewBar.isEmpty() ? row : viewBar.removeFromTop(32);
    const bool connected = activeAi_ != nullptr;
    modelPicker_.setVisible(connected);
    fetchModels_.setVisible(connected);
    if (connected) {
        fetchModels_.setBounds(aiRow.removeFromRight(98).reduced(2, 3));
        modelPicker_.setBounds(aiRow.removeFromRight(std::clamp(aiRow.getWidth() - 160, 112, 204)).reduced(2, 3));
    }
    aiButton_.setBounds(aiRow.removeFromRight(std::min(180, aiRow.getWidth())).reduced(2, 2));

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
    const int reviewHeight = chordReviewHeight();
    const auto arrangerHeight = getArrangementHeight(editor.getHeight());
    chordReview_.setBounds(editor.withY(editor.getY()+arrangerHeight+(editorVisible_ ? 8 : 0)).withHeight(reviewHeight));
    auto arrangement = editor.withHeight(arrangerHeight).reduced(13);
    if (project_) {
        const bool stacked = stackTimelineNavigation(arrangement.getWidth(), editor.getHeight());
        auto navigation = arrangement.removeFromBottom(timelineNavigationHeight * (stacked ? 2 : 1));
        auto heights = (stacked ? navigation.removeFromTop(timelineNavigationHeight)
                                : navigation.removeFromLeft(std::min(362, navigation.getWidth() / 2))).reduced(0, 2);
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
        for (auto i : order) contentHeight += trackControls_[i]->size.height();
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
        ruler_.headerBackground = verticalScroll_.getCurrentRangeStart() < 0.5
            ? juce::Colours::transparentBlack : juce::Colour(finish().panel);
        ruler_.setBounds(rowOverlay_.getLocalBounds().removeFromTop(timelineRowHeight()));
        ruler_.toFront(false);
        const juce::Font headingFont(juce::FontOptions(15.f, juce::Font::bold));
        const auto headingWidth = [&](const juce::String& label) {
            return juce::roundToInt(std::ceil(juce::GlyphArrangement::getStringWidth(headingFont, label)));
        };
        int labelsWidth = headingWidth(TRANS("BARS"));
        for (const auto& lane : view.globalLanes)
            labelsWidth = std::max(labelsWidth, headingWidth(juce::translate(lane.label)));
        if (theoryVisible_) labelsWidth = std::max({labelsWidth, headingWidth(TRANS("Degree") + " / " + TRANS("Progression")),
                                                    headingWidth(TRANS("Function"))});
        const int guideWidth = std::max(0, trackHeaderWidth - labelsWidth - 20);
        const int remaining = globalHeight - juce::roundToInt(verticalScroll_.getCurrentRangeStart());
        const int guideHeight = std::clamp(remaining, 0, rowOverlay_.getHeight());
        promptGuide_.setBounds(0, 0, guideWidth, guideHeight);
        promptGuide_.setVisible(guideWidth > 0 && guideHeight >= 44);
        promptGuide_.toFront(false);

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
            y += height;
        }
        auto placeGrip = [this](TrackRowSize& size, juce::Rectangle<int> bounds) {
            bounds = rowOverlay_.getLocalArea(this, bounds);
            size.setBounds(bounds.withY(bounds.getBottom()).withHeight(TrackRowSize::grip));
        };
        for (std::size_t i = 0; i < trackControls_.size(); ++i) {
            const bool visible = !trackRows_[i].isEmpty();
            auto& strip = *trackControls_[i];
            const bool shared = strip.outputTrack.has_value() || hasSharedParts(i);
            strip.routingMenu.setVisible(visible && shared && !strip.size.compact());
            auto placeInstrument = [&](juce::Rectangle<int> bounds) {
                if (shared) strip.routingMenu.setBounds(bounds.removeFromRight(juce::jmin(58, bounds.getWidth() * 2 / 5)));
                strip.plugin.setBounds(bounds);
            };
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
                if (canArm) strip.arm.setBounds(row.removeFromLeft(26).reduced(1, 0));
                strip.cue.setBounds(row.removeFromLeft(26).reduced(1, 0));
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
                if (midi && (project_->isGlobalNoteTrack(i) || project_->lyricTrackIds.contains(project_->trackIds[int(i)]))) {
                    strip.plugin.setVisible(true);
                    strip.plugin.setBounds(header.removeFromRight(std::min(130, header.getWidth()*3/4)).reduced(1,0));
                }
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
                placeInstrument(top.reduced(2, 1));
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
            placeInstrument(top.reduced(2, 0));
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
    const int inlineReview = editorVisible_ && pianoGrid_.isVisible() ? reviewHeight : 0;
    editor.removeFromTop(arrangerHeight + 8 + reviewHeight - inlineReview);
    const int clipHeight = clipLane_.isVisible() ? std::min(inlineReview && editor.getHeight() < 180 ? 14 : 24, editor.getHeight() / 5) : 0;
    const PianoLayout pianoLayout(editor.withTrimmedTop(30 + clipHeight), trackHeaderWidth, verticalScroll_.isVisible(),
                                  !pendingReview_.empty() || (pitchedSelection() && pianoColours_.getSelectedId() != plainNotes),
                                  parameterFraction_, drumSteps_.isVisible() ? DrumSteps::labelWidth : 52,inlineReview,vocalLane_.isVisible());
    vocalLane_.gridX = pianoLayout.grid.getX() - pianoLayout.vocals.getX();
    vocalLane_.setBounds(pianoLayout.vocals);
    vocalLane_.resized();
    if (inlineReview > 0) chordReview_.setBounds(editor.withY(pianoLayout.review.getY()).withHeight(pianoLayout.review.getHeight()));
    audioEditor_.setBounds(editor.withTrimmedTop(30 + clipHeight));
    audioEditor_.setTimelineColumns(pianoLayout.ruler.getX() - audioEditor_.getX(), pianoLayout.ruler.getRight() - audioEditor_.getX());
    clipLane_.setBounds(pianoLayout.ruler.withY(editor.getY() + 30).withHeight(clipHeight));
    tempoEditor_.setBounds(editor);
    tempoEditor_.setTimelineColumns(rowOverlay_.getX() + trackHeaderWidth - editor.getX(), rowOverlay_.getRight() - editor.getX());
    pianoGrid_.setBounds(pianoLayout.grid);
    if (chordReview_.isVisible()) { chordReview_.resized(); chordReview_.repaint(); }
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
    if (editorRecord_.isVisible()) editorRecord_.setBounds(toolbar.removeFromRight(88).reduced(1));
    if (cycleButton_.isVisible()) cycleButton_.setBounds(toolbar.removeFromRight(90).reduced(1));
    if (midiDrag_.isVisible() && toolbar.getWidth() < 720) midiDrag_.setBounds({});
    else if (midiDrag_.isVisible()) midiDrag_.setBounds(toolbar.removeFromRight(192));
    const auto pickerWidth = performanceLane_.isVisible() ? juce::jmin(180, toolbar.getWidth() / 2) : 180;
    trackPicker_.setBounds(toolbar.removeFromLeft(pickerWidth));
    toolbar.removeFromLeft(6);
    performanceLane_.setBounds(toolbar.removeFromLeft(std::min(250, toolbar.getWidth() * 2 / 3)).withTrimmedRight(4));
    if (enterPerformance_.isVisible()) enterPerformance_.setBounds(toolbar.removeFromLeft(88).reduced(1));
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
    g.fillAll(juce::Colour(finish().background));
    g.setColour(juce::Colour(finish().panel).darker(.5f));
    g.fillRect(0, menuHeight, getWidth(), viewBarHeight(getWidth()));
    g.setColour(findColour(juce::Label::textColourId).withAlpha(.38f));
    for (auto bounds : toolbarDividers_) g.fillRect(bounds);
    if (workspaceMode_ != WorkspaceMode::arrangement) return;
    auto area = getWorkspace();
    if (!project_) {
        drawArrangement(g, area, currentArrangement());
        return;
    }

    const int reviewHeight = chordReviewHeight();
    auto arrangementArea = area.removeFromTop(getArrangementHeight(area.getHeight()));
    area.removeFromTop(8 + (editorVisible_ && pianoGrid_.isVisible() ? 0 : reviewHeight));
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
    g.setGradientFill(juce::ColourGradient(juce::Colour(finish().panel), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(finish().background).brighter(.12f), 0.0f, static_cast<float>(area.getBottom()), false));
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
        if (pianoInspector_.theme >= pianoHarmony::Theme::forest)
            return colour.interpolatedWith(juce::Colour(finish().panel), .42f);
        return colour;
    };
    auto drawRow = [&](const juce::String& label, int y, std::optional<ViewButton::Icon> icon = {}, bool global = false, int handleWidth = 0) {
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
                         juce::Rectangle<int>(labels.getX() + inset, y, (global ? labels.getWidth() : std::min(trackNameWidth, labels.getWidth())) - inset - 5 - handleWidth, labelHeight),
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

    auto drawLane = [&](const ostfstudio::ArrangementLane& lane, int y, juce::Colour colour, std::optional<ViewButton::Icon> icon = {}, std::optional<size_t> track = {}) {
        const auto grip = track ? trackOrderHandle(*track) : juce::Rectangle<int>{};
        const auto row = drawRow(lane.kind == TimelineLane::none ? juce::String::fromUTF8(lane.label) : juce::translate(lane.label), y, icon, lane.kind != TimelineLane::none, grip.getWidth());
        if (!grip.isEmpty()) {
            g.setColour(juce::Colour(muted));
            for (int dy : {-4, 0, 4})
                for (int dx : {-2, 2})
                    g.fillRect(grip.getCentreX() + dx - 1, grip.getCentreY() + dy - 1, 2, 2);
        }
        g.setColour(juce::Colour(0xff3a4e5e));
        for (int bar = firstBar; bar < visible.end; bar += tickStep) {
            g.drawVerticalLine(positionX(bar, row), static_cast<float>(row.getY()), static_cast<float>(row.getBottom()));
        }

        for (const auto& region : lane.regions) {
            const auto span = region.exactBars.value_or(ostfstudio::TimelineExtent{
                static_cast<double>(region.bars.firstBar), static_cast<double>(region.bars.endBar)});
            auto displaySpan = span;
            if (region.clipId && resizingMidi_ && resizingMidi_->id == region.clipId)
                displaySpan.end = 1. + double(resizingMidi_->end) / project_->ticksPerBar;
            const auto cell = clipBounds(displaySpan, row);
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
            if (region.loopTicks > 0 && project_) {
                const auto step = double(region.loopTicks) / project_->ticksPerBar;
                const auto stride = std::max(1., std::ceil(3. * widthInBars / std::max(1, row.getWidth()) / step));
                for (double n = std::max(stride, std::ceil((visible.start - span.start) / (step * stride)) * stride);
                     span.start + n * step < std::min(displaySpan.end, visible.end); n += stride) {
                    g.setColour(juce::Colour(AudioClipWaveforms::seamColour));
                    g.drawVerticalLine(positionX(span.start + n * step, row), float(bounds.getY()), float(bounds.getBottom()));
                }
                label = juce::String::charToString(0x21bb) + " " + label;
                g.setColour(juce::Colours::white);
            }
            auto caption = bounds.reduced(std::min(4, bounds.getWidth() / 6), 0);
            const auto* id = region.clipId ? region.clipId : region.fitId;
            if (id && track && theory_ && *track < clipFits_.size() && (chordFitVisible_ || scaleFitVisible_) &&
                project_->trackKinds[*track] == ostfstudio::TrackKind::pitched) {
                const auto& cells = theory_->cells;
                const auto& fits = theoryTracks_[*track];
                const auto barTicks = double(project_->ticksPerBar);
                const double start = (std::max(displaySpan.start, visible.start) - 1.) * barTicks;
                const double end = (std::min(displaySpan.end, visible.end) - 1.) * barTicks;
                const auto first = theory_->firstCell(start);
                for (auto i = first; i < fits.size() && cells[i].start < end; i = fits[i].chordEnd) {
                    const auto& ctx = fits[i];
                    const int left = positionX(1. + std::max(start, double(cells[i].start)) / barTicks, row);
                    const int right = positionX(1. + std::min(end, double(cells[ctx.chordEnd - 1].end)) / barTicks, row);
                    const auto part = bounds.getIntersection({left, bounds.getY(), right - left, bounds.getHeight()});
                    const auto free = drawFitBadges(g, part, clipFit(*track, id, ctx.chordBegin));
                    if (i == first) caption.setRight(std::max(caption.getX(), free.getRight()));
                }
            }
            if (rowHeight >= 70 - TrackRowSize::grip) caption = caption.removeFromTop(18);
            g.setFont(juce::FontOptions(13.f));
            g.setColour(juce::Colours::white);
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
                       labels.getWidth());
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
    auto heading = [&](const char* title, bool output = false) {
        auto band = juce::Rectangle<int>(labels.getX(), y, area.getRight() - labels.getX(), arrangementHeadingHeight);
        const auto colour = output ? juce::Colour(0xffe9b777) : juce::Colour(accent);
        g.setColour(juce::Colour(0xff192731).interpolatedWith(colour, 0.13f));
        g.fillRect(band);
        g.setColour(colour);
        g.fillRect(band.removeFromTop(2));
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
            if (!trackRows_[i].intersects(g.getClipBounds())) {
                y += trackControls_[i]->size.height();
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
        drawLane(view.trackLanes[i], y, juce::Colour((blueTheme() ? blueTracks : trackColours)[i % std::size(trackColours)]), icon,i);
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
            for (const auto& item : project_->audio) {
                if (item.track != i) continue;
                const auto* audio = &item;
                const bool adjusting = resizingAudio_ == i && audio->regionId == selectedAudioId_;
                const auto start = 1.0 + static_cast<double>(adjusting ? previewAudioStart_ : audio->startTick) / project_->ticksPerBar;
                const bool missing = !waveforms_.available(*audio);
                const auto& audioClock = auditionClock_ ? *auditionClock_ : project_->clock;
                const auto endBar = adjusting && previewAudioLength_ > 0 ? start + double(previewAudioLength_) / project_->ticksPerBar
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
                        if (adjusting && !audio->timing.loop && !resizingAudioStart_ && audio->durationTicks > 0
                                && previewAudioLength_ > audio->durationTicks) {
                            preview = audio->timing;
                            const auto oldLength = audio->durationTicks - audio->timing.leadingTicks;
                            if (!preview->sourceLimitTicks || oldLength < preview->sourceLimitTicks) preview->sourceLimitTicks = oldLength;
                        }
                        const auto offset = movingAudio_ && adjusting ? previewAudioStart_ - audio->startTick : 0;
                        waveforms_.draw(g, body, *audio, audioClock,
                                        (std::max(start, visible.start) - 1) * project_->ticksPerBar - offset,
                                        (std::min(endBar, visible.end) - 1) * project_->ticksPerBar - offset, preview ? &*preview : nullptr);
                    }
                    g.setColour(juce::Colours::white);
                    const auto label = missing ? TRANS("Missing WAV") : audio->path.fromLastOccurrenceOf("/", false, false);
                    if (selectedTrack_ == i && selectedAudioId_ == audio->regionId && !missing && !audio->timing.loop && start >= visible.start) {
                        g.setColour(juce::Colour(0xffffc479));
                        g.fillRect(clip.getX(), clip.getY() + 1, 2, std::max(1, clip.getHeight() - 2));
                    }
                    if (selectedTrack_ == i && selectedAudioId_ == audio->regionId && !missing && endBar <= visible.end) {
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
        y += project_ ? trackControls_[i]->size.height() : laneHeight;
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
        heading("OUTPUT TRACKS", true);
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
    if (reorderTarget_ && *reorderTarget_ < trackRows_.size()) {
        const auto row = trackRows_[*reorderTarget_];
        g.setColour(juce::Colour(hardwareAmber));
        g.fillRect(row.getX(), reorderAfter_ ? row.getBottom() - 3 : row.getY(), row.getWidth(), 3);
    }
    if (project_ && selectedTrack_ && clipLane_.selected) {
        auto selected = clipAt(*selectedTrack_, std::llround((*clipLane_.selected - 1.) * project_->ticksPerBar));
        for (const auto& hit : {selected, dragPreview_}) {
            if (!hit || hit->track >= trackRows_.size()) continue;
            const auto row = trackRows_[hit->track];
            auto bounds = clipBounds({1. + double(hit->start) / project_->ticksPerBar, 1. + double(hit->end) / project_->ticksPerBar},
                                     area.withY(row.getY()).withHeight(row.getHeight())).reduced(1, 2);
            if (!bounds.isEmpty()) {
                g.setColour(juce::Colour(hardwareAmber));
                g.drawRect(bounds, 1);
            }
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

void MainComponent::drawTheoryRows(juce::Graphics& g, juce::Rectangle<int> bounds, int labelWidth) const {
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
        auto title = header;
        g.drawText(name, title.reduced(5, 0), juce::Justification::centredRight);
        g.setColour(juce::Colour(hardwareAmber));
        juce::ignoreUnused(current);
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
    {
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

}

void MainComponent::drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    if (drumSteps_.isVisible()) return;
    const PianoLayout layout(area.withTrimmedTop(clipLane_.isVisible() ? clipLane_.getHeight() : 0), trackHeaderWidth, verticalScroll_.isVisible(),
                             !pendingReview_.empty() || (pitchedSelection() && pianoColours_.getSelectedId() != plainNotes), parameterFraction_,52,chordReviewHeight(),vocalLane_.isVisible());
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
    if (vocalLane_.isVisible() && vocalLane_.overlay.getToggleState()) {
        g.setColour(juce::Colour(0xffecc681));
        for (int i = 0; i < project_->trackIds.size(); ++i) {
            if (size_t(i) == *selectedTrack_ || !project_->lyricTrackIds.contains(project_->trackIds[i])) continue;
            for (const auto& n : project_->pitchedNotes[size_t(i)]) {
                if (n.startTick >= right) break;
                if (n.endTick <= left) continue;
                g.drawRect(tickX(n.startTick),noteY(n.pitch),std::max(2.f,tickX(n.endTick)-tickX(n.startTick)),
                           std::max(2.f,rowHeight-1.f),1.5f);
            }
        }
    }
    const auto firstNote = std::upper_bound(pianoMaxEnds_.begin(), pianoMaxEnds_.end(), left) - pianoMaxEnds_.begin();
    for (auto it = notes.begin() + firstNote; it != notes.end() && it->startTick < right; ++it) drawNote(*it);
    if (vocalLane_.isVisible()) {
        g.setFont(juce::FontOptions(12.f,juce::Font::bold));
        for (auto it = notes.begin()+firstNote; it != notes.end() && it->startTick < right; ++it) {
            const auto* v = vocalLane_.lookup(it->id);
            if (!v || it->endTick <= left) continue;
            const auto x = juce::roundToInt(tickX(it->startTick));
            const auto w = juce::roundToInt(tickX(it->endTick))-x;
            if (w < 5) continue;
            if (v->technique=="scoop" || v->technique=="vibrato") {
                g.setColour(juce::Colour(0xffffce80));
                vocalLane_.drawContour(g,*v,*it,float(x),float(w),noteY(it->pitch)+rowHeight*.5f,rowHeight);
            }
            if (v->pronunciation.isEmpty()) continue;
            auto box = juce::Rectangle<int>(x,std::max(area.getY(),int(noteY(it->pitch))-14),w,14);
            g.setColour(juce::Colour(0xe51a2c35));
            g.fillRect(box);
            g.setColour(juce::Colours::white);
            g.drawText(v->pronunciation,box.reduced(1,0),juce::Justification::centredLeft);
        }
    }
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
