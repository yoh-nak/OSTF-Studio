#pragma once
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

class MasterSpectrum;

class PreviewTransport final : private juce::AudioIODeviceCallback, private juce::MidiInputCallback,
                               private juce::AudioPlayHead {
public:
    static constexpr std::size_t maxTracks = std::tuple_size_v<decltype(ostfstudio::MeterFrame::tracks)>;
    static constexpr std::size_t outputCount = std::tuple_size_v<decltype(ostfstudio::MeterFrame::outputs)>;
    static_assert(maxTracks >= ostfstudio::OstfpPreview::maxTracks + 1);
    using Chain = std::array<juce::AudioPluginInstance*, ostfstudio::SessionMix::maxEffects>;
    struct Inserts : std::array<Chain, outputCount + maxTracks> {
        std::map<juce::AudioProcessor*, std::shared_ptr<std::atomic<bool>>> bypass;
        bool bypassed(juce::AudioProcessor* device) const {
            const auto it = bypass.find(device);
            return it != bypass.end() && it->second->load();
        }
        std::map<juce::AudioPluginInstance*, size_t> keyTracks;
    };
    struct MidiDestination {
        std::function<void(const juce::MidiBuffer&, double, double)> send;
        std::function<void()> clearPending;
    };
    struct TrackSetup {
        std::optional<std::size_t> outputTrack;
        juce::AudioProcessor* instrument = nullptr; // Owned and rendered by this track.
        int midiChannel = 1;
        std::shared_ptr<MidiDestination> external;
        int monitorInput = -1, monitorWidth = 1;
        float monitorGain = 1.f;
        bool playSource = true;
        bool auditionToMaster = false;
    };
    static bool supportsInstrument(const juce::AudioPluginInstance& instrument);
    static bool supportsInsert(const juce::AudioPluginInstance& effect);
    PreviewTransport();
    bool readMasterSpectrum(std::array<float,31>& bands);
    ~PreviewTransport() override;
    struct LiveSection {
        juce::String id, name;
        juce::Range<juce::int64> ticks;
        int repeats = 1; // Total passes. Zero skips this section.
    };
    juce::Result extendLiveSong(const ostfstudio::OstfpPreview&, const std::vector<LiveSection>&,
                               std::span<const ostfstudio::TakeSelection>, unsigned revision);
    juce::Result setLiveSections(const std::vector<LiveSection>&);
    struct Cue {
        size_t section;
        int repeats;
        std::vector<ostfstudio::TakeSelection> takes;
        unsigned serial=0;
        juce::String sectionId;
        juce::StringArray trackIds;
        juce::Range<juce::int64> sourceRange;
        std::vector<ostfstudio::ScoreCompChoice> comps;
    };
    bool queueLiveSection(size_t index);
    bool enqueueCue(Cue, const ostfstudio::OstfpPreview* score=nullptr);
    std::vector<Cue> pendingCues() const;
    bool hasQueuedSection(size_t index) const;
    bool editCue(size_t position, int count, unsigned serial);
    bool moveCue(unsigned serial,int step);
    std::vector<ostfstudio::TakeSelection> playingCueTakes() const;
    int queuedLiveSection() const { return queuedSection_.load(); }
    void setLiveRepeats(size_t section,int repeats);
    // Message-thread look-ahead, in source-score ticks. A repeated visit keeps its section ID.
    std::vector<LiveSection> upcomingLiveVisits(size_t maximum) const;
    void setMonitorGain(size_t track, double db);
    void setMonitorInput(size_t track,int first,int width,double db);
    void setLiveMidiInput(const ostfstudio::OstfpPreview&,std::optional<size_t>,
                         const ostfstudio::MidiInputRoute&,const std::array<int,128>&);
    int liveRepeats(size_t section) const;
    struct CaptureInput {
        size_t track;
        int first, width;
        double gainDb;
    };
    struct TakeOptions {
        std::vector<CaptureInput> audioTracks;
        std::vector<Cue> cues;
        std::vector<LiveSection> sections;
        std::vector<ostfstudio::TakeSelection> auditions;
        juce::Range<juce::int64> cycle;
        int inputChannels = 0;
        int firstInputBuffer = 0;
        double inputGainDb = 0;
        int countInBars = 0;
        const std::array<int, 128>* drumMapping = nullptr;
        juce::String controlPort;
        ostfstudio::MidiInputRoute midiInput;
    };
    void setInputGain(double db) { captureGain_.store(juce::Decibels::decibelsToGain(float(db))); }
    void setTrackInputGain(size_t track,double db);
    double countInSeconds() const { return double(countRemaining_.load())/std::max(1.,sampleRate_); }
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
    juce::Result updateLoopNotes(const ostfstudio::OstfpPreview&, std::span<const ostfstudio::TakeSelection> auditions = {}, unsigned revision = 0);
    unsigned soundingTakeRevision() const {return soundingRevision_.load();}
    void setLoop(juce::Range<juce::int64> ticks);
    void seekToTick(juce::int64 tick);
    juce::Result preparePlayback(const ostfstudio::OstfpPreview&, std::span<const TrackSetup>, const Inserts&,
                                 double sampleRate, int blockSize, int semitones = 0, double tempoRate = 1, bool monitorOnly = false, std::span<const ostfstudio::TakeSelection> auditions = {});
    void renderBlock(float* const* output, int channels, int samples, const float* const* input = nullptr, int inputs = 0);
    // Call only on a private, prepared offline transport before its first block.
    juce::int64 setRenderTail(int seconds) {
        lastSample_ += juce::int64(sampleRate_ * seconds);
        return lastSample_;
    }
    ostfstudio::MeterFrame takeMeters() noexcept;
    // Buffer indices are the packed active channels supplied by AudioDeviceManager.
    void setOutputPairs(int masterFirst, int cueFirst) noexcept {
        outputPairs_.store((masterFirst + 1) | ((cueFirst + 1) << 8));
    }
    void setListenToCue(bool listen) noexcept { listenToCue_ = listen; }
    void setChannelMix(std::size_t track, const ostfstudio::ChannelMix& mix, bool audible, double cueDb);
    void setOutputMix(const ostfstudio::MixerRouting& mix);
    bool isRunning() const { return running_.load(); }
    Cursor cursor() const;
    juce::int64 positionTick() const;
    juce::int64 inputPerformanceTick() const { return performanceTick_.load(); }

private:
    juce::Optional<PositionInfo> getPosition() const override;
    void processEffects(const Chain& effects, juce::AudioBuffer<float>& audio, int count, const juce::MidiBuffer* patches = nullptr);
    using Timelines = std::array<juce::MidiBuffer, maxTracks>;
    struct PreparedCue {
        Cue cue;
        std::unique_ptr<Timelines> events, chase;
    };
    // The message thread reclaims consumed entries. The render thread only swaps owners.
    std::vector<std::unique_ptr<PreparedCue>> cues_;
    size_t consumedCues_=0;
    std::unique_ptr<PreparedCue> playingCue_, revisedCue_;
    juce::Result prepareCueEvents(PreparedCue&,const ostfstudio::OstfpPreview&) const;
    bool launchFirstCue_=false;
    juce::Result makeMidiTimelines(const ostfstudio::OstfpPreview&, std::span<const TrackSetup>, int, Timelines&, bool patchesOnly = false, std::span<const ostfstudio::TakeSelection> auditions = {}, const ostfstudio::TempoMap* renderClock = nullptr) const;
    void adoptPendingNotes();
    Timelines pendingTimelines_, pendingLoopEvents_;
    bool pendingNotes_ = false;
    unsigned pendingRevision_ = 0;
    std::atomic<unsigned> soundingRevision_{0};
    bool liveWaiting_ = false, liveEntered_ = false, releaseOnStandby_ = false;
    std::vector<TrackSetup> playbackRoutes_;
    int playbackShift_ = 0;
    void mixRenderedTrack(size_t track, juce::AudioBuffer<float>&, int count, int first, double timelineStep);
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
    std::array<std::array<ostfstudio::VolumeUnit, 2>, maxTracks> vu_;
    ostfstudio::MixerVuLevels liveVu_, publishedVu_;
    std::array<std::array<ostfstudio::VolumeUnit, 2>, outputCount> outputVu_;
    std::array<ostfstudio::VolumeUnit, 2> cueVu_;
    ostfstudio::analyzer::Meters masterMeters_;
    int historyCountdown_ = 0;
    std::unique_ptr<MasterSpectrum> masterSpectrum_;
    std::array<ostfstudio::VolumeUnit, 2> masterVu_;
    ostfstudio::MasterReadout scopeFrame_, publishedMaster_;
    juce::SpinLock masterPublishLock_;
    int phaseDecimation_ = 0;
    std::array<ostfstudio::PeakMailbox, outputCount> outputMeters_;
    ostfstudio::PeakMailbox cueMeter_;
    struct InstrumentTrack {
        bool sounding = false;
        juce::AudioProcessor* instance = nullptr;
        std::shared_ptr<MidiDestination> external;
        std::array<bool,16> externalChannels{}, sentChannels{};
        juce::MidiBuffer timeline;
        juce::MidiBuffer seekEvents, loopEvents;
        ostfstudio::PanEnvelope panEnvelope;
        std::vector<ostfstudio::AudioRegionPlayback> regions;
    };
    std::array<InstrumentTrack, maxTracks> instruments_;
    juce::AudioBuffer<float> pluginAudio_, keyScratch_;
    std::array<juce::AudioBuffer<float>,maxTracks> dryTracks_;
    std::array<juce::MidiBuffer,maxTracks> insertPatches_;
    juce::MidiBuffer pluginMidi_, patchMidi_;
    Inserts inserts_ {};
    std::array<juce::AudioBuffer<float>, outputCount> stems_;
    juce::AudioBuffer<float> cueBus_;
    std::atomic<int> outputPairs_{1 | (3 << 8)};
    std::atomic<bool> listenToCue_{false};
    std::array<std::atomic<float>, outputCount> outputGains_ {};
    std::atomic<bool> running_ {false};
    std::atomic<juce::int64> performanceTick_{-1};
    std::atomic<juce::int64> sample_ {0};
    double sampleRate_ = 0.0;
    ostfstudio::TempoMap clock_;
    double playbackTempoRate_ = 1.;
    juce::int64 ppq_ = 1;
    int meterBeats_ = 4, beatUnit_ = 4;
    std::vector<ostfstudio::MeterPoint> meters_;
    juce::int64 lastSample_ = 0;
    juce::int64 scoreTicks_ = 0;
    struct LiveBoundary {
        LiveSection source;
        juce::Range<int> samples;
        int remaining = 0;
    };
    std::vector<LiveBoundary> liveBoundaries_;
    std::vector<Timelines> liveStarts_, stagedStarts_, liveEnds_, stagedEnds_;
    bool liveLinearReplay_ = false;
    size_t liveIndex_ = 0;
    std::atomic<int> queuedSection_{-1};
    const float* const* liveInputs_ = nullptr;
    int liveInputCount_ = 0, liveOffset_ = 0;
    juce::Range<int> loopSamples_;
    bool loopRestart_ = false;
    bool recording_ = false, monitorOnly_ = false;
    double blockTimeMs_ = 0;
    std::optional<size_t> monitorTrack_;
    std::array<bool, 128> inputPitches_{};
    int monitorChannel_ = 1;
    std::array<bool,128> liveHeld_{};
    bool livePedal_=false;
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
    struct CaptureLane { size_t track; int first, width, offset; float gain; bool read=false; };
    std::vector<CaptureLane> captureLanes_;
    int firstInputBuffer_ = 0;
    std::atomic<float> captureGain_{1};
    int captureOrigin_ = 0, captureFirst_ = 0, captureEnd_ = 0;
    std::mutex recordingMutex_;
    std::array<juce::int64, 16 * 128> pressed_ {};
    std::array<int, 16 * 128> pressedVelocity_ {};
    std::vector<ostfstudio::PitchedNote> recorded_;
};
