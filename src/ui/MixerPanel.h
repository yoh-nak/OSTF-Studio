#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MixerState.h"
#include "ui/OstfpPreview.h"
#include "ui/LevelMeter.h"
#include "ui/ViewButton.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class MixerPanel final : public juce::Component {
public:
    MixerPanel();
    void addReturn();
    void addBus();
    void setAddOutputAvailability(bool canAddBus, bool canAddReturn);
    juce::String outputName(std::size_t slot) const;
    void renameOutput(std::size_t slot, const juce::String& name);
    static juce::String busName(std::size_t index);
    void setTracks(const ostfstudio::OstfpPreview& project);
    void renameTrack(std::size_t index, const juce::String& name);
    void resized() override;
    void updateMeters(const ostfstudio::MeterFrame& frame, double now);
    void clearMeters();
    struct PluginEntry {
        juce::String name;
        int slot = -1;
        bool available = false;
        bool operator==(const PluginEntry&) const = default;
    };
    void setStripPlugins(bool output, size_t track, int channels, std::vector<PluginEntry> entries);
    std::function<void(bool, size_t, int)> openPlugin;
    bool showingAnalysis() const;
    void setSpectrum(const std::array<float,31>& bands);
    void setRecordState(std::size_t track, bool armed, bool enabled);
    std::function<void(std::size_t, bool)> recordArmChanged;
    size_t trackCount() const { return channels_.size(); }
    const ostfstudio::ChannelMix& channel(std::size_t index) const { return channels_[index]; }
    bool isAudible(std::size_t index) const { return ostfstudio::channelIsAudible(channels_, index); }
    ostfstudio::MixGains gains(std::size_t index) const { return ostfstudio::mixGains(channels_, index, routing_); }
    void setOutputMuted(std::size_t index, bool muted);
    double outputVolume(std::size_t index) const;
    void setOutputVolume(std::size_t index, double db);
    void setVolumeDb(std::size_t index, double db);
    void displayPan(std::size_t index, float pan);
    void setPan(std::size_t index, float pan);
    void setOutputBus(std::size_t index, int bus);
    void setSendLevel(std::size_t index, std::size_t target, float level);
    std::size_t returnCount() const { return routing_.returnCount; }
    const ostfstudio::MixerRouting& routing() const { return routing_; }
    void setCue(std::size_t index, bool enabled);
    void setActiveOutputCount(int count);
    void setMuted(std::size_t index, bool muted);
    void setSoloed(std::size_t index, bool soloed);
    ostfstudio::SessionMix snapshot() const;
    void restore(const ostfstudio::SessionMix& mix);
    std::function<void(std::size_t)> channelChanged;
    std::function<void()> routingChanged;
    std::function<void(bool)> cueMonitorChanged;
    std::function<void(bool isReturn)> requestAddOutput;
    std::function<void(bool, std::size_t)> rowMenu;

private:
    struct Send final : juce::Component {
        juce::Label name;
        juce::Slider knob;
        ostfstudio::StereoPeak vu{};
        juce::Rectangle<int> dial;
        Send();
        void paint(juce::Graphics&) override;
        void resized() override;
    };
    struct Strip final : juce::Component {
        Strip(const juce::String& title, const juce::String& group, bool track);
        void resized() override;
        void paint(juce::Graphics& g) override;
        juce::Label name;
        juce::Label classification;
        juce::Slider volume, pan;
        LevelMeter meter {true};
        juce::TextButton mute {"M"};
        juce::TextButton solo {"S"}, arm {"R"};
        ViewButton cue {ViewButton::Icon::headphones};
        std::unique_ptr<juce::TextButton> listenOnMain;
        juce::ComboBox output;
        juce::Component sendRack;
        juce::Viewport sendViewport;
        std::vector<std::unique_ptr<Send>> sends;
        juce::Label status;
        juce::Rectangle<int> diagnostics;
        ostfstudio::StereoPeak vu{};
        int meterChannels = 2;
        juce::Component pluginRack;
        juce::Viewport pluginViewport;
        std::vector<PluginEntry> entries;
        std::vector<std::unique_ptr<juce::TextButton>> pluginRows;
        juce::TextButton analysisButton{TRANS("Meter details")};
        std::array<float,31> bands{};
        bool isMaster = false;
        ostfstudio::MasterReadout analysis;
        void mouseDown(const juce::MouseEvent& event) override {
            if (event.mods.isPopupMenu() && contextMenu) contextMenu();
        }
        std::function<void()> contextMenu;
        const bool isTrack;
    };
    void updateReturnOptions();
    void refreshAudibility();
    void updateCueStatus();
    int activeOutputCount_ = -1;
    bool listeningOnMain_ = false;

    juce::Label caption_ {"", ""};
    juce::TextButton addBusButton_ {TRANS("+ Bus")};
    juce::TextButton addReturnButton_ {TRANS("+ Return")};
    juce::Component rack_;
    juce::Viewport viewport_, masterViewport_;
    juce::Component outputRack_;
    juce::Viewport outputViewport_;
    std::vector<ostfstudio::ChannelMix> channels_;
    ostfstudio::MixerRouting routing_;
    std::array<juce::String, ostfstudio::mixerOutputCapacity> outputNames_;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::vector<std::unique_ptr<Strip>> buses_, returns_;
    std::unique_ptr<Strip> master_, cue_;
    std::vector<std::size_t> trackOrder_;
};
