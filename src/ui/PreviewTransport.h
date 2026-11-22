#pragma once

#include "ui/OstfpPreview.h"
#include "core/MixerState.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <mutex>

class PreviewTransport final : private juce::AudioIODeviceCallback, private juce::MidiInputCallback {
public:
    ~PreviewTransport() override;
    juce::Result start(juce::AudioDeviceManager& devices, const ostfstudio::OstfpPreview& project, bool record);
    std::vector<ostfstudio::PitchedNote> stop();
    void setChannelGain(std::size_t track, ostfstudio::MixGains gains);
    bool isRunning() const { return running_.load(); }
    juce::int64 positionTick() const;

private:
    struct Tone { juce::int64 first, end; double frequency; std::size_t track; };
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override { running_ = false; }
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    juce::AudioDeviceManager* devices_ = nullptr;
    std::vector<Tone> tones_;
    struct ChannelGains { std::atomic<float> master {0.0f}, cue {0.0f}; };
    std::array<ChannelGains, 8> gains_ {};
    std::atomic<bool> running_ {false};
    std::atomic<juce::int64> sample_ {0};
    double sampleRate_ = 0.0;
    double samplesPerTick_ = 0.0;
    juce::int64 lastSample_ = 0;
    juce::int64 scoreTicks_ = 0;
    bool recording_ = false;
    std::mutex recordingMutex_;
    std::array<juce::int64, 16 * 128> pressed_ {};
    std::vector<ostfstudio::PitchedNote> recorded_;
};
