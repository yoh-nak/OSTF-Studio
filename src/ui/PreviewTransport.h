#pragma once
#include "ui/LinkSession.h"
#include "ui/AudioTrackInput.h"
#include "ui/ControllerMap.h"
#include "ui/MidiInputRoute.h"

#include "ui/AudioRegionPlayback.h"
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
    struct MidiDestination {
        std::function<void(const juce::MidiBuffer&, double, double)> send;
        std::function<void()> clearPending;
    };
    struct TrackSetup {
        std::optional<std::size_t> outputTrack;
        juce::AudioProcessor* instrument = nullptr; // Owned and rendered by this track.
        int midiChannel = 1;
        std::shared_ptr<MidiDestination> external;
    };
    static bool supportsInstrument(const juce::AudioPluginInstance& instrument);
    static bool supportsInsert(const juce::AudioPluginInstance& effect);
    ~PreviewTransport() override;
    struct TakeOptions {
        juce::Range<juce::int64> cycle;
        int inputChannels = 0;
        int firstInputBuffer = 0;
        double inputGainDb = 0;
        int countInBars = 0;
        ostfstudio::LinkSession* link = nullptr;
        double linkQuantum = 4, linkTempo = 0;
        bool announceLinkStart = true;
        const std::array<int, 128>* drumMapping = nullptr;
        juce::String controlPort;
        ostfstudio::MidiInputRoute midiInput;
    };
    void setInputGain(double db) { captureGain_.store(juce::Decibels::decibelsToGain(float(db))); }
    std::atomic<bool> metronome{false};
    juce::Range<juce::int64> recordedRange() const;
    bool countingIn() const { return countRemaining_.load() > 0; }
    juce::Result prepareTake(const TakeOptions&);
    std::optional<ostfstudio::ProjectAudio> finishAudioTake(size_t track);
    juce::Result start(juce::AudioDeviceManager &devices, const ostfstudio::OstfpPreview &project, bool record,
                       std::span<const TrackSetup> tracks, const Inserts &inserts = {}, juce::int64 startTick = 0,
                       int semitones = 0, double tempoRate = 1, std::optional<size_t> inputTrack = {}, const TakeOptions* options = nullptr, bool monitorOnly = false);
    // Arm after preparePlayback and before attaching device callbacks or rendering audio.
    void prepareRecording(const ostfstudio::OstfpPreview &, std::span<const TrackSetup>,
                          std::optional<size_t> inputTrack, bool capture = true);
    void inputNote(const juce::MidiMessage &message);
    void inputFromDevice(const juce::MidiMessage&, const juce::String& port = {});
    struct Cursor { juce::int64 tick; double seconds; };
    std::vector<ostfstudio::PitchedNote> stop(Cursor* stoppedAt = nullptr);
    juce::Result updateLoopNotes(const ostfstudio::OstfpPreview&);
    void setLoop(juce::Range<juce::int64> ticks);
    void seekToTick(juce::int64 tick);
    juce::Result preparePlayback(const ostfstudio::OstfpPreview&, std::span<const TrackSetup>, const Inserts&,
                                 double sampleRate, int blockSize, int semitones = 0, double tempoRate = 1, bool monitorOnly = false, double linkTempo = 0);
    void renderBlock(float* const* output, int channels, int samples, const float* const* input = nullptr, int inputs = 0);
    ostfstudio::MeterFrame takeMeters() noexcept;
    void setListenToCue(bool listen) noexcept { listenToCue_ = listen; }
    void setChannelMix(std::size_t track, const ostfstudio::ChannelMix& mix, bool audible, double cueDb);
    void setOutputMix(const ostfstudio::MixerRouting& mix);
    bool isRunning() const { return running_.load(); }
    Cursor cursor() const;
    juce::int64 positionTick() const;

private:
    juce::Optional<PositionInfo> getPosition() const override;
    void processEffects(const Chain& effects, juce::AudioBuffer<float>& audio, int count, const juce::MidiBuffer* patches = nullptr);
    using Timelines = std::array<juce::MidiBuffer, maxTracks>;
    juce::Result makeMidiTimelines(const ostfstudio::OstfpPreview&, std::span<const TrackSetup>, int, Timelines&, bool patchesOnly = false) const;
    void adoptPendingNotes();
    Timelines pendingTimelines_, pendingLoopEvents_;
    bool pendingNotes_ = false;
    std::vector<TrackSetup> playbackRoutes_;
    int playbackShift_ = 0;
    void renderInstruments(float* const* output, int channels, int count, int first, int end, bool listenToCue);
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override { running_ = false; }
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    juce::AudioDeviceManager* devices_ = nullptr;
    struct ChannelGains {
        std::atomic<float> level {0.0f}, cue {0.0f}, pan {0.0f};
        std::atomic<bool> audible{true};
        std::atomic<int> bus {0};
        std::array<std::atomic<float>, ostfstudio::maxMixerReturns> sends {};
    };
    std::array<ChannelGains, maxTracks> gains_ {};
    std::array<ostfstudio::PeakMailbox, maxTracks> trackMeters_;
    std::array<ostfstudio::PeakMailbox, outputCount> outputMeters_;
    ostfstudio::PeakMailbox cueMeter_;
    struct InstrumentTrack {
        juce::AudioProcessor* instance = nullptr;
        std::shared_ptr<MidiDestination> external;
        std::array<bool,16> externalChannels{}, sentChannels{};
        juce::MidiBuffer timeline;
        juce::MidiBuffer seekEvents, loopEvents;
        ostfstudio::PanEnvelope panEnvelope;
        std::vector<ostfstudio::AudioRegionPlayback> regions;
    };
    std::array<InstrumentTrack, maxTracks> instruments_;
    juce::AudioBuffer<float> pluginAudio_;
    juce::MidiBuffer pluginMidi_, patchMidi_;
    Inserts inserts_ {};
    std::array<juce::AudioBuffer<float>, outputCount> stems_;
    juce::AudioBuffer<float> cueBus_;
    std::atomic<bool> listenToCue_{false};
    std::array<std::atomic<float>, outputCount> outputGains_ {};
    std::atomic<bool> running_ {false};
    std::atomic<juce::int64> sample_ {0};
    double sampleRate_ = 0.0;
    ostfstudio::TempoMap clock_;
    ostfstudio::LinkSession* launchClock_ = nullptr;
    std::int64_t launchMicros_ = 0;
    ostfstudio::LinkSession* playbackLink_ = nullptr;
    double linkBaseTempo_ = 0, linkQuantum_ = 4, linkStartBeat_ = 0, linkSubsample_ = 0;
    std::atomic<double> linkBpm_{0};
    double outputLatencyMicros_ = 0;
    juce::int64 ppq_ = 1;
    int meterBeats_ = 4, beatUnit_ = 4;
    std::vector<ostfstudio::MeterPoint> meters_;
    juce::int64 lastSample_ = 0;
    juce::int64 scoreTicks_ = 0;
    juce::Range<int> loopSamples_;
    bool loopRestart_ = false;
    bool recording_ = false, monitorOnly_ = false;
    double blockTimeMs_ = 0;
    std::optional<size_t> monitorTrack_;
    std::array<bool, 128> inputPitches_{};
    int monitorChannel_ = 1;
    juce::SpinLock liveLock_;
    juce::MidiBuffer liveMessages_;
    int queuedLiveMessages_ = 0;
    void closeRecordedNote(size_t key, juce::int64 elapsed);
    void mixClick(float* const*, int channels, int offset, int samples, int age, bool accent) const;
    std::vector<std::pair<int, bool>> beatSamples_;
    std::atomic<juce::int64> elapsed_{0}, countRemaining_{0};
    juce::int64 takeStart_ = 0, countLength_ = 0, countBeat_ = 1, countBar_ = 1;
    std::array<int, 128> inputMap_{};
    bool mapInput_ = false;
    juce::String controlPort_;
    ostfstudio::MidiInputRoute midiInput_;
    juce::AudioBuffer<float> captured_;
    int firstInputBuffer_ = 0;
    std::atomic<float> captureGain_{1};
    int captureOrigin_ = 0, captureFirst_ = 0, captureEnd_ = 0;
    std::mutex recordingMutex_;
    std::array<juce::int64, 16 * 128> pressed_ {};
    std::array<int, 16 * 128> pressedVelocity_ {};
    std::vector<ostfstudio::PitchedNote> recorded_;
};
