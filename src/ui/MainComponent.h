#pragma once
#include "ui/ControlTargetButton.h"
#include "ui/EditorClipLane.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <JuceHeader.h>
#include "ui/DeviceSettingsWindow.h"
#include "ui/MixerPanel.h"
#include "ui/MidiDragButton.h"
#include "ui/OstfpPreview.h"
#include "ui/PluginBrowser.h"
#include "ui/PreviewTransport.h"
#include "ui/ViewButton.h"
#include "ui/ArrangementRuler.h"
#include "ui/AudioClipEditor.h"
#include "ui/TempoEditor.h"
#include "ui/PianoInspector.h"
#include "ui/PerformanceEditor.h"
#include "ui/DrumSteps.h"
#include "ui/DrumPads.h"
#include "ui/ControllerSurface.h"
#include "ui/TrackRowSize.h"
#include "ui/ScoreView.h"
#include "ui/TransportPanel.h"
#include "ui/SongBasicsPanel.h"
#include "ui/TheoryGuide.h"

class SmkPresetEditor;

class MainComponent final : public juce::Component, public juce::TooltipClient, public juce::DragAndDropContainer,
                            public juce::FileDragAndDropTarget, public juce::DragAndDropTarget, private juce::MenuBarModel, private juce::MidiInputCallback, private juce::Timer, private juce::ScrollBar::Listener {
public:
    MainComponent();
    ~MainComponent() override;
    bool openDocument(const juce::File& file);
    void openImportedSong(std::unique_ptr<ostfstudio::OstfpPreview>);
    void displayTransportPosition(juce::int64 tick, double seconds = -1, const juce::ScrollBar* anchoredView = nullptr);
    void paint(juce::Graphics& g) override;
    void resized() override;
    juce::String getTooltip() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    std::optional<std::size_t> pluginDropTrack(juce::Point<int> position) const;
    void timerCallback() override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    struct PadUpdate final : juce::AsyncUpdater {
        explicit PadUpdate(MainComponent& owner) : editor(owner) {}
        MainComponent& editor;
        void handleAsyncUpdate() override { editor.showPadInput(); }
    } padUpdate_{*this};
    void showPadInput();
    void attachPadInput(bool);
    void openController(bool show);
    void connectController(juce::MidiDeviceInfo);
    void refreshController();
    juce::AudioPluginInstance* surfacePlugin(const juce::String& target = {}) const;
    juce::String activeControlTarget(const juce::String& port, int page) const;
    void chooseControlTarget(juce::Button&, const juce::String&, const juce::String&);
    void assignControlTarget(const juce::MidiDeviceInfo&, const juce::String&, const juce::String&);
    void syncControlInputs();
    void remapEffectTargets(bool output, size_t index, int from, int to);
    juce::String controlProjectId_;
    ostfstudio::ControllerMap controllerProfile(const juce::MidiDeviceInfo&);
    juce::String pluginTarget(bool output, size_t track, int slot) const;
    juce::String targetCaption(const juce::String&) const;
    void refreshControlButtons();
    ControlTargetButton paneControl_;
    std::map<juce::String, ostfstudio::ControllerMap> otherSurfaces_;
    std::map<juce::String, int> lastControlPages_;
    std::map<juce::String, juce::String> controlPortNames_;
    ostfstudio::ControlTargetMap controlTargets_;
    size_t rigMixerBank(int page) const;
    int rigTarget_ = 0;
    void applySurfaceAction(ostfstudio::SurfaceAction);
    void saveControllerBindings();
    SmkPresetEditor* presetEditor_ = nullptr;
    std::unique_ptr<juce::DocumentWindow> surfaceWindow_;
    ControllerSurface* surfacePanel_ = nullptr;
    juce::MidiDeviceListConnection surfaceDevices_;
    juce::SpinLock surfaceLock_;
    ostfstudio::ControllerMap surfaceMap_;
    std::array<ostfstudio::SurfaceAction, 128> surfaceEvents_{};
    size_t surfaceCount_ = 0;
    int surfaceLearn_ = -1;
    bool surfacePan_ = false, openedSurfacePort_ = false;
    void flashPad(int pitch);
    void cancelPadLearn();
    std::array<int, 128> padMapping_{};
    std::array<std::atomic<bool>, 128> padPressed_{};
    std::atomic<int> learnTarget_{-1}, learnedPair_{-1};
    bool padListening_ = false;
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
    void startTransport(bool recording, bool replaceConfirmed = false, bool monitorOnly = false);
    void restartInputMonitor();
    bool monitorActive_ = false, monitorPending_ = false;
    std::map<juce::String, juce::MidiDeviceInfo> midiOutputs_;
    juce::Range<juce::int64> editorCycle_;
    juce::TextButton editorRecord_{TRANS("Record")}, cycleButton_{TRANS("Loop range")};
    std::map<juce::String, int> captureChannels_;
    std::map<juce::String, ostfstudio::AudioTrackInput> audioInputs_;
    void showAudioInput(std::size_t track);
    void updateInputMeters();
    bool hasAudioInputs_ = false;
    void restoreInstrumentAssignments(const ostfstudio::SessionMix& mix);
    void stopTransport();
    void navigateTransport(TransportKey::Action);
    void locateTransport(juce::int64 tick, const juce::ScrollBar* anchoredView = nullptr);
    TimelinePan viewportPan_;
    void panArrangement(double fraction);
    void panTimeline(juce::ScrollBar&, double fraction);
    void showShortcutList();
    void updateTransportMix();
    void togglePluginBrowser();
    juce::Rectangle<int> getWorkspace() const;
    int getArrangementHeight(int workspaceHeight) const;
    void refreshTimelineLanes();
    std::optional<ostfstudio::TimelineLane> globalLaneAt(juce::Point<int>) const;
    juce::int64 arrangementTickAt(int x, bool endBoundary = false) const;
    void globalCommand(int command);
    void placeResult(ImportResultRow&, std::optional<std::size_t> track, juce::int64 tick, bool replace);
    void applyGlobalChange(ostfstudio::TimelineLane, juce::Array<juce::var> remove, juce::Array<juce::var> insert);
    std::optional<ostfstudio::TimelineLane> selectedGlobal_, copiedLane_;
    juce::int64 selectedGlobalTick_ = 0, copiedPpq_ = 0;
    juce::var copiedEvent_;
    ostfstudio::ArrangementView currentArrangement() const;
    std::vector<std::size_t> displayedTrackOrder() const;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override;
    void menuItemSelected(int item, int topLevelMenuIndex) override;
    void inspectOSTF();
    void newProject();
    void adoptProject(std::unique_ptr<ostfstudio::OstfpPreview>, const juce::String&, bool, const juce::File&);
    bool openPreview(juce::InputStream& input, const juce::String& filename, bool plainScore = false, const juce::File& sourceFolder = {});
    void editSongInformation();
    void updateDocumentLabel();
    enum class SaveFormat { score, package, midi0, midi1 };
    void saveDocument(SaveFormat);
    juce::PopupMenu timelineMenu() const;
    void selectTrack(std::size_t index);
    void rebuildTrackControls();
    void showTrackInput(std::size_t);
    void setRecordArm(std::size_t, bool);
    void refreshMidiRoutes();
    void saveMidiRoutes();
    juce::String armedId_, midiDocument_;
    std::map<juce::String, ostfstudio::MidiInputRoute> midiRoutes_;
    struct InputMonitor { ostfstudio::MidiInputRoute route; bool received = false; };
    std::vector<InputMonitor> inputMonitors_;
    std::optional<std::size_t> drumInputMonitor_;
    int surfaceInputStatus_ = -1, surfaceInputData_ = 0;
    void editTracks(ostfstudio::TrackKind kind, std::optional<std::size_t> remove = {},
                    std::optional<std::size_t> duplicate = {}, ostfstudio::MixerTrackGroup group = ostfstudio::MixerTrackGroup::pitched,
                    const ImportResultRow* import = nullptr, juce::int64 tick = 0);
    juce::TextEditor renameEditor_{TRANS("Track name")};
    std::size_t renameRow_{};
    bool renameOutput_{};
    void renameTrack(std::size_t index, bool output = false);
    void showPatch(std::size_t track, juce::int64 tick);
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
    void loadEffect(bool output, std::size_t index, std::size_t slot, const juce::PluginDescription& description, bool openEditor = false);
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
    EditorDivider pianoDivider_, columnDivider_{true}, arrangementColumnDivider_{true}, browserDivider_{true}, parameterDivider_;
    juce::ComboBox pitchZoom_;
    double parameterFraction_ = 0;
    int parameterBottom_ = 0, parameterHeight_ = 0;
    int preferredHeaderWidth_ = 487, trackHeaderWidth = 487, trackNameWidth = 172;
    int preferredBrowserWidth_ = 230;
    double arrangementFraction_ = 0.6;
    PianoVelocityLane pianoVelocity_ {pianoGrid_};
    juce::ComboBox pianoSnap_, pianoColours_, drumMode_;
    DrumSteps drumSteps_;
    DrumPads drumPads_;
    juce::String padTrack_;
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
        LevelMeter meter, recordingMeter;
        ViewButton cue{ViewButton::Icon::headphones};
        juce::Slider inputGain;
        MidiActivityLamp inputLamp;
        juce::TextButton arm{"R"}, input{"IN"};
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
    SongBasicsPanel basics_;
    int topBasicsHeight() const;
    int timelineRowHeight() const;
    void editSongField(SongBasicsPanel::Field, const juce::String&, int);
    void markSessionEdited();
    void refreshWindowTitle();
    void parentHierarchyChanged() override;
    // Plug-ins may notify on the audio thread. Update the document caption on the message thread.
    struct StockEffectChanges final : private juce::AudioProcessorListener, private juce::Timer {
        void observe(juce::AudioProcessor& processor) {
            processor.addListener(this);
            if (!isTimerRunning()) startTimerHz(5);
        }
        void reset() { pending.store(false); }
        std::function<void()> changed;
    private:
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { pending.store(true); }
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
            if (details.nonParameterStateChanged) pending.store(true);
        }
        void timerCallback() override { if (pending.exchange(false) && changed) changed(); }
        std::atomic<bool> pending{false};
    } stockEffectChanges_;
    juce::MemoryBlock savedScore_;
    bool sessionEdited_ = false;
    juce::String currentFilename_;
    juce::File legacySourceFile_;
    juce::int64 cursorTick_ = -1;
    juce::ComboBox trackPicker_;
    juce::ComboBox performanceLane_;
    juce::Slider arrangementZoom_, groupHeight_;
    struct HeightMenuStyle : juce::LookAndFeel_V4 {
        juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(16.f); }
        juce::Font getPopupMenuFont() override { return juce::FontOptions(16.f); }
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override {
            return LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label).withStandardItemHeight(30).withMinimumWidth(210);
        }
    } heightMenuStyle_;
    juce::ComboBox heightScope_;
    juce::Label heightLabel_, arrangementZoomLabel_;
    std::array<int, static_cast<std::size_t>(HeightGroup::count)> groupDefaults_ {};
    bool mixedHeights_ = false;
    juce::ScrollBar arrangementScroll_ {false};
    juce::Component rowOverlay_;
    ArrangementRuler ruler_, pianoRuler_;
    ostfstudio::ProjectAudio* clipOn(std::size_t track) const;
    void placeWav(const juce::File&, std::size_t track, juce::int64 tick, bool replace);
    void openSplice();
    void toggleLink();
    void pollLink();
    void editLinkTempo();
    double linkQuantum() const;
    std::unique_ptr<ostfstudio::LinkSession> link_;
    bool lastLinkPlaying_ = false, followingLink_ = false;
    void aiConnectionMenu();
    void displayAiState(const juce::String&, juce::uint32);
    juce::String aiModel_;
    std::array<juce::TextButton, 3> aiStudios_;
    int studioModeIndex_ = 0;
    juce::ComboBox modelPicker_;
    juce::TextButton fetchModels_{TRANS("Refresh models")};
    void showModelChoices();
    ostfstudio::AiStudioMode aiMode() const {
        return static_cast<ostfstudio::AiStudioMode>(studioModeIndex_);
    }
    void applyAiSelection();
    void refreshAiModels();
    bool refreshingModels_ = false;
    juce::StringArray aiModels_, aiModelNames_;
    enum class AccountAction { restore, login, logout };
    void updateAiConnection(AccountAction);
    juce::URL signInUrl_;
    bool signInPending_ = false;
    ostfstudio::AnalysisConnection activeAi_;
    struct ConnectionButton final : juce::TextButton {
        void paintButton(juce::Graphics& g, bool hover, bool down) override {
            auto b = getLocalBounds().toFloat().reduced(1);
            auto colour = findColour(buttonColourId);
            g.setColour(down ? colour.darker(.15f) : hover ? colour.brighter(.10f) : colour);
            g.fillRoundedRectangle(b, 7);
            g.setColour(colour.brighter(.48f));
            g.drawRoundedRectangle(b, 7, 1.5f);
            auto light = b.removeFromLeft(26).withSizeKeepingCentre(8, 8);
            g.setColour(juce::Colours::white);
            g.fillEllipse(light);
            g.setFont(juce::FontOptions(15.f, juce::Font::bold));
            g.drawFittedText(getButtonText(), b.toNearestInt().withTrimmedRight(6), juce::Justification::centredLeft, 2);
        }
    } aiButton_;
    juce::ThreadPool aiConnectionWorker_{1};
    bool spliceLoading_ = false;
    std::unique_ptr<juce::AudioPluginInstance> spliceInstance_;
    juce::AudioProcessorPlayer splicePlayer_;
    std::unique_ptr<juce::DocumentWindow> spliceWindow_;
    EditorClipLane clipLane_;
    void refreshClipLane();
    void selectEditorClip(std::optional<double> bar, bool locate);
    void editMelodyneCopy();
    void applyMelodyneCopy();
    struct MelodyneCopy {
        juce::File wav;
        juce::String originalHash, workingHash;
        juce::int64 startFrame, frames, startTick, durationTicks;
    };
    std::map<juce::String, MelodyneCopy> melodyneCopies_;
    void openEffectEditor(bool output, std::size_t index, std::size_t slot);
    AudioClipWaveforms waveforms_;
    AudioClipEditor audioEditor_{waveforms_};
    TempoEditor tempoEditor_;
    std::optional<std::size_t> resizingAudio_;
    juce::int64 previewAudioLength_ = 0;
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
    juce::Label importTrackDrop_;
    std::optional<std::size_t> pluginDropHighlight_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
