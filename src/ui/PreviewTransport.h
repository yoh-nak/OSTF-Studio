#pragma once

#include "ui/OstfpPreview.h"
#include "core/MixerState.h"
#include "ui/AudioMeters.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <mutex>

class PreviewTransport final : private juce::AudioIODeviceCallback, private juce::MidiInputCallback,
                               private juce::AudioPlayHead {
public:
    static constexpr std::size_t maxTracks = std::tuple_size_v<decltype(ostfstudio::MeterFrame::tracks)>;
    static constexpr std::size_t outputCount = std::tuple_size_v<decltype(ostfstudio::MeterFrame::outputs)>;
    static_assert(maxTracks >= ostfstudio::OstfpPreview::maxTracks + 1);
    using Chain = std::array<juce::AudioPluginInstance*, ostfstudio::SessionMix::maxEffects>;
    using Inserts = std::array<Chain, outputCount + maxTracks>;
    struct TrackSetup {
        std::optional<std::size_t> outputTrack;
        juce::AudioPluginInstance* instrument = nullptr; // Owned and rendered by this track.
        int midiChannel = 1;
    };
    static bool supportsInstrument(const juce::AudioPluginInstance& instrument);
    static bool supportsInsert(const juce::AudioPluginInstance& effect);
    ~PreviewTransport() override;
    juce::Result start(juce::AudioDeviceManager& devices, const ostfstudio::OstfpPreview& project,
                       bool record, std::span<const TrackSetup> tracks, const Inserts& inserts = {}, juce::int64 startTick = 0);
    struct Cursor { juce::int64 tick; double seconds; };
    std::vector<ostfstudio::PitchedNote> stop(Cursor* stoppedAt = nullptr);
    void seekToTick(juce::int64 tick);
    juce::Result preparePlayback(const ostfstudio::OstfpPreview&, std::span<const TrackSetup>, const Inserts&,
                                 double sampleRate, int blockSize);
    void renderBlock(float* const* output, int channels, int samples);
    ostfstudio::MeterFrame takeMeters() noexcept;
    void setChannelMix(std::size_t track, const ostfstudio::ChannelMix& mix, bool audible, double cueDb);
    void setOutputMix(const ostfstudio::MixerRouting& mix);
    bool isRunning() const { return running_.load(); }
    Cursor cursor() const;
    juce::int64 positionTick() const;

private:
    juce::Optional<PositionInfo> getPosition() const override;
    void processEffects(const Chain& effects, juce::AudioBuffer<float>& audio, int count);
    void renderInstruments(float* const* output, int channels, int count, int first, int end);
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override { running_ = false; }
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    juce::AudioDeviceManager* devices_ = nullptr;
    struct ChannelGains {
        std::atomic<float> level {0.0f}, cue {0.0f}, pan {0.0f};
        std::atomic<int> bus {0};
        std::array<std::atomic<float>, ostfstudio::maxMixerReturns> sends {};
    };
    std::array<ChannelGains, maxTracks> gains_ {};
    std::array<ostfstudio::PeakMailbox, maxTracks> trackMeters_;
    std::array<ostfstudio::PeakMailbox, outputCount> outputMeters_;
    ostfstudio::PeakMailbox cueMeter_;
    struct InstrumentTrack {
        juce::AudioPluginInstance* instance = nullptr;
        juce::MidiBuffer timeline;
        juce::MidiBuffer seekEvents;
        juce::AudioBuffer<float> wav;
        double wavRate = 0.0;
        int wavStart = 0, wavEnd = 0;
        float wavGain = 1.0f;
    };
    std::array<InstrumentTrack, maxTracks> instruments_;
    juce::AudioBuffer<float> pluginAudio_;
    juce::MidiBuffer pluginMidi_;
    Inserts inserts_ {};
    std::array<juce::AudioBuffer<float>, outputCount> stems_;
    std::array<std::atomic<float>, outputCount> outputGains_ {};
    std::atomic<bool> running_ {false};
    std::atomic<juce::int64> sample_ {0};
    double sampleRate_ = 0.0;
    ostfstudio::TempoMap clock_;
    juce::int64 ppq_ = 1;
    int meterBeats_ = 4, beatUnit_ = 4;
    juce::int64 lastSample_ = 0;
    juce::int64 scoreTicks_ = 0;
    bool recording_ = false;
    std::mutex recordingMutex_;
    std::array<juce::int64, 16 * 128> pressed_ {};
    std::array<int, 16 * 128> pressedVelocity_ {};
    std::vector<ostfstudio::PitchedNote> recorded_;
};
