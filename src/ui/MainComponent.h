#pragma once
#include <JuceHeader.h>
#include "ui/DeviceSettingsWindow.h"
#include "ui/MixerPanel.h"
#include "ui/MidiDragButton.h"
#include "ui/OstfpPreview.h"
#include "ui/PluginBrowser.h"
#include "ui/PreviewTransport.h"
#include "ui/ViewButton.h"
#include "ui/TrackRowSize.h"
#include "ui/ScoreView.h"

class MainComponent final : public juce::Component, public juce::DragAndDropContainer,
                            public juce::DragAndDropTarget, private juce::MenuBarModel, private juce::Timer, private juce::ScrollBar::Listener {
public:
    MainComponent();
    ~MainComponent() override;
    bool openDocument(const juce::File& file);
    void displayTransportPosition(juce::int64 tick);
    void paint(juce::Graphics& g) override;
    void resized() override;
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
    void updateHorizontalRange(bool piano, bool reset = false);
    ostfstudio::TimelineExtent horizontalRange(bool piano) const;
    void startTransport(bool recording);
    void restoreInstrumentAssignments(const ostfstudio::SessionMix& mix);
    void stopTransport();
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
    void openDemo();
    bool openPreview(juce::InputStream& input, bool plainScore = false, const juce::File& sourceFolder = {});
    enum class SaveFormat { score, package, midi0, midi1 };
    void saveDocument(SaveFormat);
    juce::PopupMenu timelineMenu() const;
    void selectTrack(std::size_t index);
    void rebuildTrackControls();
    void editTracks(ostfstudio::TrackKind kind, std::optional<std::size_t> remove = {});
    bool canEditTracks() const;
    void chooseTrackPlugin(std::size_t index, bool bundleDirectory);
    void showTrackPluginMenu(std::size_t index);
    void scanTrackPlugin(std::size_t index);
    void discoverTrackPlugin(std::size_t index);
    void loadTrackPlugin(std::size_t index, const juce::PluginDescription& description, bool restoring = false);
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
               lanesButton_ {ViewButton::Icon::lanes}, arrangementButton_ {ViewButton::Icon::arrangement},
               leadButton_ {ViewButton::Icon::leadSheet}, scoreButton_ {ViewButton::Icon::score};
    juce::TooltipWindow tooltips_ {this};
    bool editorVisible_ = true, mixerSelected_ = false;
    juce::TextButton playButton_ {TRANS("Play")}, stopButton_ {TRANS("Stop")}, recordButton_ {TRANS("Record MIDI")};
    juce::Label transportPosition_;
    juce::ComboBox trackPicker_;
    juce::ComboBox performanceLane_;
    juce::ComboBox arrangementZoom_, pianoZoom_;
    juce::ScrollBar arrangementScroll_ {false}, pianoScroll_ {false};
    juce::Component rowOverlay_;
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
