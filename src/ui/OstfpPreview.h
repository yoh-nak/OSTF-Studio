#pragma once

#include "ui/ArrangementView.h"
#include "core/MixerState.h"
#include "core/TempoMap.h"
#include "ui/SongMusic.h"
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

struct MidiControl {
    juce::String type;
    int number = 0, channel = 0;
    int maximum() const { return type == "pitchBend" ? 16383 : 127; }
    juce::String label() const {
        juce::String name = type == "controlChange" ? "CC " + juce::String(number)
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
    struct Point { juce::int64 tick; double value; juce::String id; };
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
    std::map<juce::String, juce::String> stockEffectStates;
    static juce::String effectSlot(bool output, std::size_t channel, int slot) {
        const auto n = static_cast<int>(channel);
        const auto id = !output ? "track-" + juce::String(n + 1)
            : n == 0 ? juce::String("master") : isReturnOutput(channel) ? "return-" + juce::String(n - 2)
            : "bus-" + juce::String(static_cast<int>(outputBusIndex(channel) + 1));
        return id + "-fx-" + juce::String(slot + 1);
    }
    std::vector<bool> audioTracks;
};

struct ProjectAudio {
    std::size_t track = 0;
    juce::String path;
    juce::File localFile;
    juce::MemoryBlock embedded;
    juce::int64 startTick = 0, durationTicks = 0, startFrame = 0, frameCount = 0;
    double gainDb = 0;
    bool muted = false;
    juce::String sha256;
    int sampleRate = 0, channels = 0;
    juce::int64 assetFrames = 0;
};

enum class TrackKind { pitched, drum, audio };

struct SongCredit {
    juce::String id, contributorId, role, name;
};
struct SongInformation {
    juce::String title, subtitle, description;
    std::vector<SongCredit> credits;
};

struct OstfpPreview {
    juce::int64 playbackEndTick() const { return (bars.endBar - 1) * ticksPerBar; }
    static constexpr std::size_t maxTracks = 15; // One transport channel is reserved for global chord voicing.
    juce::String title;
    juce::String sourceVersion;
    juce::int64 durationTicks = 0;
    BarSpan bars {1, 1};
    juce::int64 ticksPerBar = 0;
    juce::int64 ppq = 0;
    double previewBpm = 0.0; // A non-zero value denotes one fixed source tempo.
    TempoMap clock;
    int meterBeats = 4, beatUnit = 4;
    std::vector<MeterPoint> meters;
    juce::String defaultScale;
    juce::StringArray trackIds;
    PanAutomation panAutomation;
    std::vector<std::vector<DrumSlot>> slotsByTrack;
    std::vector<std::vector<PitchedNote>> slotHits;
    juce::String contextAt(TimelineLane kind, juce::int64 tick) const;
    SongInformation songInformation() const;
    juce::Result renameTrack(std::size_t index, const juce::String& name);
    juce::Result applySongInformation(const SongInformation&);
    juce::Result applySongMusic(const SongMusicEdit&, juce::MemoryBlock& before);
    juce::Result checkAudition(int shift, double rate) const;
    std::vector<ArrangementLane> globalLanes, trackLanes;
    std::vector<std::vector<PitchedNote>> pitchedNotes, drumNotes;
    struct ProgramChange { juce::int64 tick; int program; std::optional<int> channel; };
    std::vector<std::vector<ProgramChange>> programChanges;
    std::vector<int> sourceMidiChannels;
    std::vector<std::vector<PerformanceCurve>> performanceCurves;
    std::vector<MixerTrackGroup> mixerGroups;
    std::vector<TrackKind> trackKinds;
    int chordVoicings = 0;
    std::optional<std::size_t> chordVoicingTrack;
    std::size_t sourceTrackCount() const { return chordVoicingTrack.value_or(trackLanes.size()); }
    bool isGlobalNoteTrack(std::size_t index) const {
        const auto group = mixerGroups[index];
        return chordVoicingTrack == index || group == MixerTrackGroup::melody || group == MixerTrackGroup::vocal;
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
                            std::optional<std::size_t> duplicate = {}, MixerTrackGroup group = MixerTrackGroup::pitched) const;
    bool insertGeneratorPattern(std::size_t track, int bar);
    bool addSessionClip(std::size_t track, juce::String name, std::vector<PitchedNote> notes);
    juce::Result editNotes(std::size_t track, const std::vector<NoteEdit>& edits, juce::MemoryBlock& before,
                           juce::StringArray& resultingIds);
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
