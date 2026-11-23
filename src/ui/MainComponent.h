#pragma once
#include <JuceHeader.h>
#include "ui/DeviceSettingsWindow.h"
#include "ui/MixerPanel.h"
#include "ui/MidiDragButton.h"
#include "ui/OstfpPreview.h"
#include "ui/PluginBrowser.h"
#include "ui/PreviewTransport.h"
#include "ui/ViewButton.h"
#include "ui/ArrangementRuler.h"
#include "ui/AudioClipWaveforms.h"
#include "ui/PianoInspector.h"
#include "ui/PerformanceEditor.h"
#include "ui/DrumSteps.h"
#include "ui/TrackRowSize.h"
#include "ui/ScoreView.h"
#include "ui/TransportPanel.h"
#include "ui/TheoryGuide.h"

class MainComponent final : public juce::Component, public juce::TooltipClient, public juce::DragAndDropContainer,
                            public juce::DragAndDropTarget, private juce::MenuBarModel, private juce::Timer, private juce::ScrollBar::Listener {
public:
    MainComponent();
    ~MainComponent() override;
    bool openDocument(const juce::File& file);
    void displayTransportPosition(juce::int64 tick, double seconds = -1, const juce::ScrollBar* anchoredView = nullptr);
    void paint(juce::Graphics& g) override;
    void resized() override;
    juce::String getTooltip() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    std::optional<std::size_t> pluginDropTrack(juce::Point<int> position) const;
    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void updateHorizontalRange(bool reset = false);
    enum class HeightGroup { all = 1, global, lanes, voicing, melody, vocal, arrangement, pitched, drums, audio,
                             outputs, master, bus, returns, count };
    HeightGroup trackHeightGroup(std::size_t index) const;
    static HeightGroup outputHeightGroup(std::size_t index);
    static bool includesHeightGroup(HeightGroup scope, HeightGroup group);
    void applyGroupHeight();
    void refreshGroupHeight();
    ostfstudio::TimelineExtent horizontalRange() const;
    void startTransport(bool recording);
    void restoreInstrumentAssignments(const ostfstudio::SessionMix& mix);
    void stopTransport();
    void navigateTransport(TransportKey::Action);
    void locateTransport(juce::int64 tick, const juce::ScrollBar* anchoredView = nullptr);
    void panArrangement(double fraction);
    void panTimeline(juce::ScrollBar&, double fraction);
    void showShortcutList();
    void updateTransportMix();
    void togglePluginBrowser();
    juce::Rectangle<int> getWorkspace() const;
    int getArrangementHeight(int workspaceHeight) const;
    void refreshTimelineLanes();
    ostfstudio::ArrangementView currentArrangement() const;
    std::vector<std::size_t> displayedTrackOrder() const;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override;
    void menuItemSelected(int item, int topLevelMenuIndex) override;
    void inspectOSTF();
    void newProject();
    bool openPreview(juce::InputStream& input, const juce::String& filename, bool plainScore = false, const juce::File& sourceFolder = {});
    void editSongInformation();
    void updateDocumentLabel();
    enum class SaveFormat { score, package, midi0, midi1 };
    void saveDocument(SaveFormat);
    juce::PopupMenu timelineMenu() const;
    void selectTrack(std::size_t index);
    void rebuildTrackControls();
    void editTracks(ostfstudio::TrackKind kind, std::optional<std::size_t> remove = {},
                    std::optional<std::size_t> duplicate = {}, ostfstudio::MixerTrackGroup group = ostfstudio::MixerTrackGroup::pitched);
    void renameTrack(std::size_t index, bool output = false);
    void showRowMenu(bool output, std::size_t index);
    void confirmRemoval(bool output, std::size_t index);
    void editOutput(bool isReturn, std::optional<std::size_t> source = {}, bool remove = false);
    void refreshTrackActions();
    void changeInterfaceScale(int step);
    void revealRow(juce::Rectangle<int> row);
    void copyEffectChain(bool output, std::size_t from, std::size_t to);
    bool canEditTracks() const;
    void chooseTrackPlugin(std::size_t index, bool bundleDirectory);
    void showTrackPluginMenu(std::size_t index);
    void scanTrackPlugin(std::size_t index);
    void discoverTrackPlugin(std::size_t index);
    void loadTrackPlugin(std::size_t index, const juce::PluginDescription& description, bool restoring = false, juce::MemoryBlock state = {});
    void openTrackPluginEditor(std::size_t index);
    void linkAudio(std::size_t index);
    void setupOutputTracks();
    struct EffectSlot {
        juce::String id;
        juce::String savedState;
        std::unique_ptr<juce::AudioPluginInstance> effect;
        std::unique_ptr<juce::DocumentWindow> editor;
    };
    struct EffectChain {
        juce::TextButton button {"FX"};
        std::vector<EffectSlot> slots;
        std::size_t loading = 0;
    };
    EffectChain& effectChain(bool output, std::size_t index);
    void updateEffectButton(EffectChain& chain);
    void showEffects(bool output, std::size_t index);
    void appendEffect(bool output, std::size_t index, const juce::PluginDescription& description);
    void loadEffect(bool output, std::size_t index, std::size_t slot, const juce::PluginDescription& description);
    void restoreEffects(bool output, std::size_t index, const juce::StringArray& ids);
    PluginBrowser& ensurePluginBrowser();
    void showTrackMix(std::size_t index);
    void updatePresetLabel(std::size_t index);
    void refreshEditor();
    enum class WorkspaceMode { arrangement, leadSheet, score };
    void setWorkspaceMode(WorkspaceMode);
    WorkspaceMode workspaceMode_ = WorkspaceMode::arrangement;
    bool canExportTrack(std::size_t track) const;
    bool writeTrackMidi(std::size_t track, juce::OutputStream& stream) const;
    void drawArrangement(juce::Graphics& g, juce::Rectangle<int> area, const ostfstudio::ArrangementView& view);
    void drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const;
    void refreshPianoNotes();
    juce::PopupMenu theoryMenu() const;
    juce::PopupMenu musicMenu() const;
    void editSongMusic(ostfstudio::SongMusicEdit::Mode mode);
    void rememberScoreEdit(juce::MemoryBlock before);
    void previewNoteFit(const std::vector<ostfstudio::NoteEdit>&);
    juce::String noteFitReading() const;
    std::vector<ostfstudio::TheoryGuide::TrackCell> previewFits_;
    std::optional<ostfstudio::PitchedNote> inspectedNote_, draggedNote_;
    int auditionShift_ = 0;
    double auditionRate_ = 1;
    std::optional<ostfstudio::TempoMap> auditionClock_;
    void refreshTheory();
    void refreshTrackTheory(std::size_t track);
    int theoryTrackHeight(std::size_t track) const;
    bool pitchedSelection() const;
    void drawTheoryRows(juce::Graphics&, juce::Rectangle<int>, int labelWidth,
                        std::optional<std::size_t> track) const;
    bool theoryVisible_ = false, chordFitVisible_ = true, scaleFitVisible_ = true;
    int scaleReference_ = -1;
    std::unique_ptr<ostfstudio::TheoryGuide> theory_;
    std::vector<std::vector<ostfstudio::TheoryGuide::TrackCell>> theoryTracks_;
    std::vector<juce::int64> pianoMaxEnds_;
    void applyNoteGesture(const std::vector<ostfstudio::NoteEdit>&);
    void undoPianoEdit(bool redo);
    void finishNoteEdit(bool playing);
    PianoNoteGrid pianoGrid_;
    PianoInspector pianoInspector_ {pianoGrid_};
    bool blueTheme() const { return pianoInspector_.theme == pianoHarmony::Theme::studioBlue; }
    juce::Viewport pianoInspectorView_;
    EditorDivider pianoDivider_, columnDivider_ {true};
    int preferredHeaderWidth_ = 487, trackHeaderWidth = 487, trackNameWidth = 172;
    double arrangementFraction_ = 0.6;
    PianoVelocityLane pianoVelocity_ {pianoGrid_};
    juce::ComboBox pianoSnap_, pianoColours_, drumMode_;
    DrumSteps drumSteps_;
    PerformanceEditor controllerEditor_{pianoGrid_};
    std::map<juce::String,ostfstudio::MidiControl> controllerKeys_;
    void refreshControllerLane();
    void editPerformancePoint(juce::String,juce::int64,std::optional<double>);
    void updateDrumLoop();
    juce::Range<juce::int64> activeLoop_;

    juce::Rectangle<int> functionRow_;
    std::optional<std::size_t> pianoTrack_;
    std::size_t pianoGeneration_ = 0;
    struct NoteRevision { juce::MemoryBlock before, after; juce::String panTrack; ostfstudio::PanEnvelope oldPan, newPan; };
    std::deque<NoteRevision> noteHistory_;
    std::size_t noteHistoryCursor_ = 0;
    struct TrackControls {
        LevelMeter meter;
        TrackRowSize size;
        juce::TextButton mute {"M"};
        juce::TextButton solo {"S"};
        juce::Slider volume, pan;
        juce::TextButton plugin {TRANS("Instrument")};
        juce::TextButton mix {TRANS("Mix")};
        EffectChain effects;
        juce::Label preset;
        juce::File pluginPath, candidatePath;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::unique_ptr<juce::DocumentWindow> editor;
        bool loading = false;
        bool playableInstrument = false;
        std::optional<std::size_t> outputTrack;
        int midiChannel = 1;
        juce::var instrumentAssignment;
    };
    juce::MenuBarComponent menuBar_;
    ViewButton pianoButton_ {ViewButton::Icon::piano}, mixerButton_ {ViewButton::Icon::mixer},
               pluginBrowserButton_ {ViewButton::Icon::plugins}, outputButton_ {ViewButton::Icon::outputs},
               songInfoButton_ {ViewButton::Icon::information}, lanesButton_ {ViewButton::Icon::lanes}, arrangementButton_ {ViewButton::Icon::arrangement},
               leadButton_ {ViewButton::Icon::leadSheet}, scoreButton_ {ViewButton::Icon::score};
    ViewButton melodyAdd_ {ViewButton::Icon::melody}, chorusAdd_ {ViewButton::Icon::chorus},
               pitchedAdd_ {ViewButton::Icon::pitched}, drumsAdd_ {ViewButton::Icon::drums},
               audioAdd_ {ViewButton::Icon::audio}, busAdd_ {ViewButton::Icon::bus}, returnAdd_ {ViewButton::Icon::send},
               uiSmaller_ {ViewButton::Icon::zoomOut}, uiLarger_ {ViewButton::Icon::zoomIn};
    int interfaceScale_ = 100;
    ViewButton theoryButton_ {ViewButton::Icon::theory}, musicButton_ {ViewButton::Icon::songMusic};
    juce::Label toolbarHint_;
    juce::TooltipWindow tooltips_ {nullptr, 350};
    bool editorVisible_ = true, mixerSelected_ = false;
    TransportPanel deck_;
    juce::Label documentLabel_;
    juce::String currentFilename_;
    juce::File legacySourceFile_;
    juce::int64 cursorTick_ = -1;
    juce::ComboBox trackPicker_;
    juce::ComboBox performanceLane_;
    juce::Slider arrangementZoom_, groupHeight_;
    juce::ComboBox heightScope_;
    juce::Label heightLabel_, arrangementZoomLabel_;
    std::array<int, static_cast<std::size_t>(HeightGroup::count)> groupDefaults_ {};
    bool mixedHeights_ = false;
    juce::ScrollBar arrangementScroll_ {false};
    juce::Component rowOverlay_;
    ArrangementRuler ruler_, pianoRuler_;
    AudioClipWaveforms waveforms_;
    bool spaceHeld_ = false;
    juce::ScrollBar verticalScroll_ {true};
    juce::StringArray performanceLaneNames_;
    MidiDragButton midiDrag_;
    juce::TemporaryFile trackMidiFile_ {".mid"};
    std::optional<std::size_t> pendingMidiDrag_;
    MixerPanel mixer_;
    std::unique_ptr<DeviceSettingsWindow> deviceSettings_;
    std::unique_ptr<ScoreView> scoreView_;
    std::unique_ptr<juce::DocumentWindow> importWindow_;
    PreviewTransport transport_;
    bool transportActive_ = false;
    std::optional<std::size_t> recordTrack_;
    juce::Label statusLabel_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::FileChooser> pluginChooser_;
    std::unique_ptr<ostfstudio::OstfpPreview> project_;
    juce::ApplicationProperties viewPreferences_;
    unsigned timelineLaneMask_ = 0xff;
    std::vector<ostfstudio::ArrangementLane> displayedGlobalLanes_;
    std::vector<juce::Rectangle<int>> trackRows_;
    juce::AudioPluginFormatManager pluginFormats_;
    std::unique_ptr<PluginBrowser> pluginBrowser_;
    std::vector<std::unique_ptr<TrackControls>> trackControls_;
    struct OutputControl {
        LevelMeter meter;
        TrackRowSize size;
        EffectChain effects;
        juce::Slider volume;
        juce::TextButton mute {"M"};
    };
    std::size_t visibleOutputCount() const;
    std::optional<std::size_t> outputDropTrack(juce::Point<int> position) const;
    std::array<juce::Rectangle<int>, PreviewTransport::outputCount> outputRows_;
    std::optional<std::size_t> outputDropHighlight_;
    std::array<OutputControl, PreviewTransport::outputCount> outputControls_;
    bool showOutputTracks_ = true;
    std::size_t projectGeneration_ = 0;
    std::optional<std::size_t> selectedTrack_;
    std::optional<std::size_t> pluginDropHighlight_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
