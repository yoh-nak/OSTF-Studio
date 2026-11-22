#pragma once

#include "core/MixerState.h"
#include "ui/OstfpPreview.h"
#include "ui/LevelMeter.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class MixerPanel final : public juce::Component {
public:
    MixerPanel();
    void setTracks(const ostfstudio::OstfpPreview& project);
    void resized() override;
    void updateMeters(const ostfstudio::MeterFrame& frame, double now);
    void clearMeters();
    const ostfstudio::ChannelMix& channel(std::size_t index) const { return channels_[index]; }
    bool isAudible(std::size_t index) const { return ostfstudio::channelIsAudible(channels_, index); }
    ostfstudio::MixGains gains(std::size_t index) const { return ostfstudio::mixGains(channels_, index, routing_); }
    void setOutputMuted(std::size_t index, bool muted);
    double outputVolume(std::size_t index) const;
    void setOutputVolume(std::size_t index, double db);
    void setVolumeDb(std::size_t index, double db);
    void setPan(std::size_t index, float pan);
    void setOutputBus(std::size_t index, int bus);
    void setSendLevel(std::size_t index, std::size_t target, float level);
    std::size_t returnCount() const { return routing_.returnCount; }
    const ostfstudio::MixerRouting& routing() const { return routing_; }
    void setMuted(std::size_t index, bool muted);
    void setSoloed(std::size_t index, bool soloed);
    ostfstudio::SessionMix snapshot() const;
    void restore(const ostfstudio::SessionMix& mix);
    std::function<void(std::size_t)> channelChanged;
    std::function<void()> routingChanged;
    std::function<void()> returnsChanged;

private:
    struct Strip final : juce::Component {
        Strip(const juce::String& title, const juce::String& group, bool track);
        void resized() override;
        void paint(juce::Graphics& g) override;
        juce::Label name;
        juce::Label classification;
        juce::Slider volume, pan;
        LevelMeter meter {true};
        juce::ToggleButton mute {"M"};
        juce::ToggleButton solo {"S"};
        juce::ToggleButton cue {"C"};
        juce::ComboBox output, returnTarget;
        juce::Slider sendLevel;
        juce::Label status;
        const bool isTrack;
    };
    void addReturn();
    void updateReturnOptions();
    void refreshAudibility();

    juce::Label caption_ {"", ""};
    juce::TextButton addReturnButton_ {TRANS("+ Return")};
    juce::Component rack_;
    juce::Viewport viewport_;
    juce::Component outputRack_;
    juce::Viewport outputViewport_;
    std::vector<ostfstudio::ChannelMix> channels_;
    ostfstudio::MixerRouting routing_;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::vector<std::unique_ptr<Strip>> buses_, returns_;
    std::unique_ptr<Strip> master_, cue_;
    std::vector<std::size_t> trackOrder_;
};
