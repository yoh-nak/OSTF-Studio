#pragma once
#include <JuceHeader.h>
#include "ui/DeviceSettingsWindow.h"
#include "ui/MixerPanel.h"
#include "ui/OstfpPreview.h"
#include "ui/PluginBrowser.h"
#include "ui/PreviewTransport.h"

class MainComponent final : public juce::Component, public juce::DragAndDropContainer,
                            public juce::DragAndDropTarget, private juce::MenuBarModel, private juce::Timer {
public:
    MainComponent();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
private:
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void timerCallback() override;
    void startTransport(bool recording);
    void stopTransport();
    void updateTransportMix();
    void togglePluginBrowser();
    juce::Rectangle<int> getWorkspace() const;
    int getArrangementHeight(int workspaceHeight) const;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override;
    void menuItemSelected(int item, int topLevelMenuIndex) override;
    void inspectOSTF();
    void openDemo();
    void openPreview(juce::InputStream& input);
    void selectTrack(std::size_t index);
    void rebuildTrackControls();
    void chooseTrackPlugin(std::size_t index, bool bundleDirectory);
    void showTrackPluginMenu(std::size_t index);
    void scanTrackPlugin(std::size_t index);
    void discoverTrackPlugin(std::size_t index);
    void loadTrackPlugin(std::size_t index, const juce::PluginDescription& description);
    void openTrackPluginEditor(std::size_t index);
    void refreshEditor();
    void drawArrangement(juce::Graphics& g, juce::Rectangle<int> area, const ostfstudio::ArrangementView& view);
    void drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const;
    struct TrackControls {
        juce::TextButton mute {"M"};
        juce::TextButton solo {"S"};
        juce::Slider volume;
        juce::TextButton plugin {"Plug-in"};
        juce::File pluginPath;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::unique_ptr<juce::DocumentWindow> editor;
        bool loading = false;
    };
    juce::MenuBarComponent menuBar_;
    juce::TextButton pianoButton_ {"Piano Roll"};
    juce::TextButton mixerButton_ {"Mixer"};
    juce::TextButton pluginBrowserButton_ {"Plug-ins"};
    juce::TextButton playButton_ {"Play"}, stopButton_ {"Stop"}, recordButton_ {"Record MIDI"};
    juce::Label transportPosition_;
    juce::ComboBox trackPicker_;
    MixerPanel mixer_;
    std::unique_ptr<DeviceSettingsWindow> deviceSettings_;
    PreviewTransport transport_;
    bool transportActive_ = false;
    std::optional<std::size_t> recordTrack_;
    juce::Label statusLabel_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::FileChooser> pluginChooser_;
    std::unique_ptr<ostfstudio::OstfpPreview> project_;
    std::vector<juce::Rectangle<int>> trackRows_;
    juce::AudioPluginFormatManager pluginFormats_;
    std::unique_ptr<PluginBrowser> pluginBrowser_;
    std::vector<std::unique_ptr<TrackControls>> trackControls_;
    std::size_t projectGeneration_ = 0;
    std::optional<std::size_t> selectedTrack_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
