#pragma once
#include "ui/AudioTrackInput.h"
#include "core/ClipTiming.h"

#include "ui/ArrangementView.h"
#include "core/MixerState.h"
#include "core/TempoMap.h"
#include "ui/SongMusic.h"
#include "ui/StudioMidiTakes.h"
#include <juce_core/juce_core.h>
#include <deque>
#include <map>
#include <vector>

namespace ostfstudio {
enum class MixerTrackGroup { voicing, melody, vocal, pitched, other };

struct PitchedNote {
    juce::int64 startTick;
    juce::int64 endTick;
    int pitch;
    std::optional<int> velocity;
    bool enabled = true;
    juce::String id;
    bool fixedTrigger = false;
    juce::String clipId, slotId;
};

struct NoteEdit {
    enum Action { insert, change, duplicate, erase } action;
    PitchedNote note;
};

struct VocalNote {
    juce::String text, pronunciation, technique;
    double intensity = 0.5;
};

// The original score event retains its ID, articulation and note-scoped data during chord review.
struct ReviewNoteOrigin {
    juce::String clipId;
    juce::var event;
    int index = 0;
    juce::StringArray createdClips;
};
using ReviewNoteOrigins = std::map<juce::String, ReviewNoteOrigin>;

struct MidiControl {
    juce::String type;
    int number = 0, channel = 0;
    int maximum() const { return type == "pitchBend" ? 16383 : 127; }
    juce::String label() const {
        juce::String name = type == "programChange" ? juce::String("Program Change") : type == "controlChange" ? "CC " + juce::String(number)
            : type == "pitchBend" ? juce::String("Pitch Bend") : type == "channelPressure" ? juce::String("Channel Aftertouch")
            : "Poly Aftertouch / note " + juce::String(number);
        return channel ? name + " / ch " + juce::String(channel) : name;
    }
};
struct MeterPoint { juce::int64 tick; int numerator, denominator; };
inline MeterPoint activeMeter(const std::vector<MeterPoint>& meters, double tick, MeterPoint initial) {
    const auto next = std::upper_bound(meters.begin(), meters.end(), tick, [](double t, const auto& m){ return t < m.tick; });
    return next == meters.begin() ? initial : *std::prev(next);
}
struct DrumSlot { juce::String id, name; int pitch; };
struct PerformanceCurve {
    struct Point { juce::int64 tick; double value; juce::String id; juce::int64 order = 1; size_t ordinal = 0; };
    juce::String label;
    std::vector<Point> points;
    bool isCurve = false;
    bool stepped = false;
    bool cents = false;
    MidiControl control;
    bool enabled = true;
};

struct PanEnvelope {
    juce::String id;
    bool linear = true;
    std::vector<PerformanceCurve::Point> points;
    double valueAt(double tick, double base) const {
        const auto next = std::upper_bound(points.begin(), points.end(), tick, [](double t, const auto& p){ return t < p.tick; });
        if (next == points.begin()) return base;
        const auto& prev = *std::prev(next);
        if (!linear || next == points.end()) return prev.value;
        return prev.value + (next->value - prev.value) * ((tick - prev.tick) / (next->tick - prev.tick));
    }
};
using PanAutomation = std::map<juce::String, PanEnvelope>;
struct SessionMix {
    struct DeviceState {
        bool enabled = true, bypass = false;
        bool active() const { return enabled && !bypass; }
        bool operator==(const DeviceState&) const = default;
    };
    std::map<juce::String, DeviceState> deviceStates;
    DeviceState deviceState(const juce::String& slot) const {
        const auto it = deviceStates.find(slot);
        return it == deviceStates.end() ? DeviceState{} : it->second;
    }
    static juce::String instrumentSlot(std::size_t track) { return "instrument-" + juce::String(int(track) + 1); }
    juce::StringArray trackOrder;
    struct MidiRoute { int channel = 1; std::optional<std::size_t> destination; };
    std::vector<ChannelMix> tracks;
    std::vector<MidiRoute> midiRoutes;
    std::vector<juce::var> instruments;
    MixerRouting routing;
    std::array<juce::String, mixerOutputCapacity> outputNames;
    static constexpr std::size_t maxEffects = 8;
    std::array<juce::String, mixerOutputCapacity> insertIds;
    std::array<juce::StringArray, mixerOutputCapacity> outputEffects;
    std::vector<juce::StringArray> trackEffects;
    // Normalized to the displayed channel and slot when reading a production document.
    using ParameterValues = std::map<juce::String, double>;
    std::map<juce::String, ParameterValues> insertParameters;
    std::map<juce::String, juce::String> stockEffectStates, sidechainTracks;
    static juce::String effectSlot(bool output, std::size_t channel, int slot) {
        const auto n = static_cast<int>(channel);
        const auto id = !output ? "track-" + juce::String(n + 1)
            : n == 0 ? juce::String("master") : isReturnOutput(channel) ? "return-" + juce::String(n - 2)
            : "bus-" + juce::String(static_cast<int>(outputBusIndex(channel) + 1));
        return id + "-fx-" + juce::String(slot + 1);
    }
    juce::Range<juce::int64> recordCycle;
    bool cycleEnabled = false;
    std::map<juce::String, int> captureChannels;
    std::map<juce::String, AudioTrackInput> audioInputs;
    std::vector<bool> audioTracks;
};

// A duplicated region and its undo entries can share an immutable source recording.
class RegionAudioBytes {
public:
    RegionAudioBytes() = default;
    RegionAudioBytes(juce::MemoryBlock bytes) : data(std::make_shared<juce::MemoryBlock>(std::move(bytes))) {}
    const juce::MemoryBlock& read() const { static const juce::MemoryBlock empty; return data ? *data : empty; }
    operator const juce::MemoryBlock&() const { return read(); }
    juce::MemoryBlock& writable() {
        if (!data) data = std::make_shared<juce::MemoryBlock>();
        else if (data.use_count() > 1) data = std::make_shared<juce::MemoryBlock>(*data);
        return *data;
    }
    size_t getSize() const { return read().getSize(); }
    bool isEmpty() const { return read().isEmpty(); }
    void* getData() { return writable().getData(); }
    const void* getData() const { return read().getData(); }
    void reset() { data.reset(); }
    bool operator==(const RegionAudioBytes& other) const { return data == other.data || read() == other.read(); }
    bool operator==(const juce::MemoryBlock& bytes) const { return read() == bytes; }
private:
    std::shared_ptr<juce::MemoryBlock> data;
};

struct ProjectAudio {
    std::size_t track = 0;
    juce::String path;
    juce::File localFile;
    RegionAudioBytes embedded;
    juce::int64 startTick = 0, durationTicks = 0, startFrame = 0, frameCount = 0;
    juce::int64 fadeInTicks = 0, fadeOutTicks = 0;
    double gainDb = 0;
    bool muted = false;
    juce::String sha256;
    int sampleRate = 0, channels = 0;
    juce::int64 assetFrames = 0;
    ClipTiming timing;
    juce::String regionId = "region-" + juce::Uuid().toString();
    bool operator==(const ProjectAudio&) const = default;
    juce::String mediaKey() const {
        return embedded.isEmpty() ? localFile.getFullPathName() : "memory:" + juce::String::toHexString(reinterpret_cast<juce::pointer_sized_uint>(embedded.getData()));
    }
    juce::int64 sourceTick() const { return startTick + timing.leadingTicks; }
    void resizeBounds(juce::int64 start, juce::int64 end, juce::int64 previousEnd) {
        const auto onset = sourceTick();
        if (!timing.loop && end > previousEnd && previousEnd > onset) {
            const auto audible = previousEnd - onset;
            if (!timing.sourceLimitTicks || audible < timing.sourceLimitTicks) timing.sourceLimitTicks = audible;
        }
        startTick = start;
        timing.leadingTicks = onset - start;
        durationTicks = end - start;
    }
};

enum class TrackKind { pitched, drum, audio };

struct SongCredit {
    juce::String id, contributorId, role, name;
};
struct SongInformation {
    juce::String title, subtitle, description;
    std::vector<SongCredit> credits;
    std::optional<juce::StringArray> genres, moods;
};

struct ScoreCompChoice {
    juce::String group, comp, track;
    juce::Range<juce::int64> ticks;
    bool active;
    juce::String label;
};
// Original Production-only channels have no Core track owner. These rows are
// projected for monitoring; their IDs and audio never become authored Core tracks.
struct ImportedProduction {
    juce::var viewTracks{juce::Array<juce::var>{}};
    juce::MemoryBlock manifestBytes, productionBytes;
    std::map<juce::String, RegionAudioBytes> members;
    std::vector<ProjectAudio> originalAudio;
    juce::MemoryBlock mixerSnapshot;
    std::map<juce::String, juce::String> deviceIdsBySlot;
    std::map<juce::String, SessionMix::DeviceState> originalDeviceStates;
};
struct OstfpPreview {
    std::shared_ptr<const ImportedProduction> importedProduction;
    std::vector<ScoreCompChoice> compChoices;
    juce::Result addScoreTake(size_t track,juce::Range<juce::int64>,juce::MemoryBlock& before);
    juce::Result compPlayback(const std::vector<ScoreCompChoice>&, std::unique_ptr<OstfpPreview>&) const;
    juce::Result selectScoreComps(const std::vector<ScoreCompChoice>&, juce::MemoryBlock& before);
    juce::Result selectScoreComp(const ScoreCompChoice& choice, juce::MemoryBlock& before);
    juce::int64 playbackEndTick() const { return (bars.endBar - 1) * ticksPerBar; }
    static constexpr std::size_t maxTracks = 15; // One transport channel is reserved for global chord voicing.
    juce::String title;
    juce::String sourceVersion, documentId;
    bool usesUnsupportedCompSelection = false;
    juce::int64 durationTicks = 0;
    BarSpan bars {1, 1};
    juce::int64 ticksPerBar = 0;
    juce::int64 ppq = 0;
    double previewBpm = 0.0; // A non-zero value denotes one fixed source tempo.
    TempoMap clock;
    int meterBeats = 4, beatUnit = 4;
    std::vector<MeterPoint> meters;
    std::vector<std::pair<juce::int64,int>> camelotKeys;
    juce::String defaultScale;
    juce::StringArray trackIds;
    std::map<juce::String,StudioMidiTakes> midiTakes;
    const StudioMidiTakes& takes(size_t track) const;
    juce::Result addMidiTake(size_t track, juce::MemoryBlock& before);
    juce::Result adoptMidiTakes(const std::vector<TakeSelection>&, juce::MemoryBlock& before);
    std::map<juce::String, juce::uint32> trackColoursById;
    juce::uint32 trackColour(std::size_t index) const;
    juce::Result setTrackColour(std::size_t index, juce::uint32 rgb, juce::MemoryBlock& previous);
    PanAutomation panAutomation;
    std::vector<std::vector<DrumSlot>> slotsByTrack;
    std::vector<std::vector<PitchedNote>> slotHits;
    juce::String contextAt(TimelineLane kind, juce::int64 tick) const;
    SongInformation songInformation() const;
    juce::Result renameTrack(std::size_t index, const juce::String& name);
    juce::Result applySongDetails(const SongInformation&, const SongMusicEdit&, juce::MemoryBlock& previous);
    juce::Result applySongInformation(const SongInformation&, juce::MemoryBlock* previous = nullptr);
    juce::Result drawTempo(juce::int64 start, double from, juce::int64 end, double to, juce::MemoryBlock& previous);
    juce::Result appendSection(juce::Range<juce::int64> range, const juce::String& name, bool duplicate, juce::MemoryBlock& previous);
    juce::Result importScaleAssignments(const juce::MemoryBlock& returned, juce::MemoryBlock& before);
    juce::Result changeGlobalEvents(TimelineLane lane, const juce::Array<juce::var>& remove,
                                    const juce::Array<juce::var>& insert, juce::MemoryBlock& before);
    juce::Result applySongMusic(const SongMusicEdit&, juce::MemoryBlock& before);
    juce::Result checkAudition(int shift, double rate) const;
    std::vector<ArrangementLane> globalLanes, trackLanes;
    const char* displayLyricAt(size_t track, juce::int64 tick) const;
    std::map<size_t, std::vector<ArrangementLane>> partLyricLanes;
    std::vector<std::vector<PitchedNote>> pitchedNotes, drumNotes;
    std::map<juce::String, VocalNote> vocalNotes;
    struct DisplayPhrase { juce::int64 start, end; juce::String text, reading, document; };
    juce::String displayDocument;
    std::map<juce::String, juce::StringArray> trackLyricDocuments;
    std::vector<DisplayPhrase> displayPhrases;
    bool partDisplaysDocument(size_t track, const juce::String& document) const;
    juce::StringArray lyricTrackIds, declaredVocalTrackIds, specialTrackNames;
    juce::Result editVocalNote(size_t track, const juce::String& noteId, const VocalNote&, juce::MemoryBlock& before);
    struct ProgramChange { juce::int64 tick; int program; std::optional<int> channel; juce::int64 order = 1; size_t ordinal = 0; };
    std::vector<std::vector<ProgramChange>> programChanges;
    std::vector<int> sourceMidiChannels;
    std::vector<std::vector<PerformanceCurve>> performanceCurves;
    std::vector<MixerTrackGroup> mixerGroups;
    std::vector<TrackKind> trackKinds;
    int chordVoicings = 0;
    std::optional<std::size_t> chordVoicingTrack, primaryMelodyTrack;
    std::size_t sourceTrackCount() const { return chordVoicingTrack.value_or(trackLanes.size()); }
    bool supportsScoreTakes() const { return sourceVersion == "5.1.0" || sourceVersion == "5.2.0"; }
    bool isGlobalNoteTrack(std::size_t index) const {
        return chordVoicingTrack == index || isMelodyOrLyricTrack(index);
    }
    bool isMelodyOrLyricTrack(std::size_t index) const {
        const auto group = mixerGroups[index];
        return group == MixerTrackGroup::melody || group == MixerTrackGroup::vocal
            || declaredVocalTrackIds.contains(trackIds[int(index)]);
    }
    std::deque<std::vector<ArrangementRegion>> regions;
    std::vector<std::vector<ArrangementRegion>> editedTrackRegions;
    std::deque<juce::String> labels;
    juce::MemoryBlock sourceScore;
    juce::var sourceManifest;
    std::optional<SessionMix> savedMix;
    std::vector<ProjectAudio> audio;
    struct AddedClip {
        std::size_t track;
        juce::String id, name;
        juce::int64 start, length;
        std::vector<PitchedNote> notes;
    };
    std::vector<AddedClip> addedClips;

    [[nodiscard]] ArrangementView view() const { return {bars, globalLanes, trackLanes}; }
    const char* keepLabel(juce::String label);
    juce::Result editTracks(TrackKind kind, std::optional<std::size_t> remove, std::unique_ptr<OstfpPreview>& result,
                            std::optional<std::size_t> duplicate = {}, MixerTrackGroup group = MixerTrackGroup::pitched,
                            std::optional<int> newPartChannel = {}) const;
    juce::Result extendedForImport(juce::int64 end, std::unique_ptr<OstfpPreview>& draft, juce::MemoryBlock& original) const;
    bool insertGeneratorPattern(std::size_t track, int bar);
    juce::Result editDrumPad(size_t track, int row, std::optional<int> pitch, juce::MemoryBlock &before,
                             std::optional<int> moveTo = {});
    juce::Result addRecordedClip(size_t track, juce::Range<juce::int64>, const std::vector<PitchedNote>&, juce::MemoryBlock& before, const juce::String& name = "Recorded MIDI", const juce::Array<juce::var>& midiEvents = {}, int editingTake = 0);
    struct MidiClipCopy { juce::var data; juce::int64 period = 0; TrackKind kind = TrackKind::pitched; };
    MidiClipCopy copyMidiClip(size_t track, const juce::String& id) const;
    juce::Result placeMidiClip(const MidiClipCopy&, size_t destination, juce::int64 tick,
                               std::optional<size_t> moveFrom, const juce::String& joinTo, juce::MemoryBlock& before);
    static constexpr size_t maxAudioRegions = 256;
    enum class MidiClipEdit { split, toggleLoop, resizeLoop };
    juce::Result editMidiClip(size_t track, const juce::String& id, MidiClipEdit edit, juce::int64 tick, juce::MemoryBlock& before);
    bool addSessionClip(std::size_t track, juce::String name, std::vector<PitchedNote> notes);
    juce::Result restoreReviewedNotes(std::size_t track, const ReviewNoteOrigins&, juce::MemoryBlock& before);
    juce::Result editNotes(std::size_t track, const std::vector<NoteEdit>& edits, juce::MemoryBlock& before,
                           juce::StringArray& resultingIds, juce::Range<juce::int64> newClipRange = {}, int editingTake = 0);
    juce::Result setPatchAt(std::size_t track, juce::int64 tick, int channel, std::array<int,3> values,
                            juce::MemoryBlock& before, bool sharedPart = false, bool programOnly = false);
    juce::Result editController(std::size_t track, const MidiControl&, const juce::String& id, juce::int64 tick,
                                std::optional<int> value, juce::MemoryBlock& before);
    juce::Result restoreNoteScore(const juce::MemoryBlock& bytes);
};

juce::Result readOstfPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result);
juce::Result readOstfpPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result);
juce::Result writeOstfPreview(const OstfpPreview& project, juce::OutputStream& output);
juce::Result writeOstfPreview(const OstfpPreview& project, const SessionMix& mix, juce::OutputStream& output);
juce::Result writeOstfPreview(const OstfpPreview& project, const SessionMix& mix, const juce::File& destination,
                             juce::OutputStream& output);
juce::Result writeOstfpPreview(const OstfpPreview& project, const SessionMix& mix, juce::OutputStream& output);
}
