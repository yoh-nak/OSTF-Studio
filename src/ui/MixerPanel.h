#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MixerState.h"
#include "ui/OstfpPreview.h"
#include "ui/LevelMeter.h"
#include "ui/MidiActivityLamp.h"
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
    static ViewButton::Icon trackIcon(const ostfstudio::OstfpPreview&,size_t);
    void setTrackColour(size_t i, juce::Colour colour) {
        if (i < strips_.size() && strips_[i]->colour != colour) { strips_[i]->colour = colour; strips_[i]->repaint(); }
    }
    void pulseInput(size_t i) { if (i < strips_.size()) strips_[i]->lamp.pulse(); }
    void updateInput(size_t i,ostfstudio::StereoPeak p,double now) { if (i < strips_.size()) strips_[i]->inputMeter.update(p,now); }
    struct PluginEntry {
        juce::String name;
        int slot = -1;
        bool available = false;
        bool operator==(const PluginEntry&) const = default;
    };
    void setStripPlugins(bool output, size_t track, int channels, std::vector<PluginEntry> entries);
    std::function<void(bool, size_t, int)> openPlugin;
    std::optional<size_t> dropTargetAt(juce::Point<int>, bool output) const;
    void highlightDrop(std::optional<size_t> track, std::optional<size_t> output);
    std::function<void(size_t, juce::Component*)> chooseInput;
    std::function<void(size_t)> captureMidi;
    std::function<void(size_t,double)> inputGainChanged;
    void displayInputGain(size_t i, double db) {
        if (i < strips_.size()) strips_[i]->inputGain.setValue(db,juce::dontSendNotification);
    }
    juce::String masterDestination() const { return masterDestination_.isEmpty() ? TRANS("Out 1-2") : masterDestination_; }
    std::function<void(bool, juce::Component*)> chooseHardwareOutput;
    void setInputState(size_t track, const juce::String& label, bool enabled, bool captureAvailable);
    void setCaptureAvailable(size_t i, bool enabled) {
        if (i < strips_.size()) strips_[i]->capture.setEnabled(enabled);
    }
    void setHardwareOutputLabels(juce::String master, juce::String cue, bool cueActive);
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

        Send();
        void resized() override;
    };
    struct Strip final : juce::Component {
        Strip(const juce::String& title, const juce::String& group, bool track);
        void resized() override;
        void paint(juce::Graphics& g) override;
        juce::Label name;
        juce::Label classification;
        juce::Slider volume, pan;
        LevelMeter meter {true}, inputMeter;
        MidiActivityLamp lamp;
        ViewButton::Icon icon = ViewButton::Icon::outputs;
        juce::Colour colour{0xffe4ae72};
        juce::TextButton mute {"M"};
        juce::TextButton solo {"S"}, arm {"R"};
        ViewButton cue {ViewButton::Icon::headphones};
        std::unique_ptr<juce::TextButton> listenOnMain;
        juce::Slider inputGain;
        juce::TextButton input, capture{TRANS("Capture MIDI")}, hardware;
        bool highlighted = false;
        void paintOverChildren(juce::Graphics& g) override {
            if (highlighted) { g.setColour(juce::Colour(0xff84d7ca)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 4.f, 2.f); }
        }
        juce::ComboBox output;
        juce::Component sendRack;
        juce::Viewport sendViewport;
        std::vector<std::unique_ptr<Send>> sends;
        juce::Label status;
        juce::Rectangle<int> diagnostics;
        ostfstudio::StereoPeak vu{};
        int meterChannels = 2;
        juce::TextButton instrument{TRANS("Instrument")}, effect{"FX"}, selectEffect{juce::String::charToString(0x25be)};
        std::vector<PluginEntry> entries;
        int effectSlot = -1;
        unsigned pluginRevision = 0;
        std::function<void(int)> openSlot;
        void updatePluginButtons();
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
    juce::String masterDestination_, cueDestination_;
    bool cueDestinationActive_ = false;
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
