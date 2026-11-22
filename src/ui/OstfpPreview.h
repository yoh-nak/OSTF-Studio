#pragma once

#include "ui/ArrangementView.h"
#include "core/MixerState.h"
#include <juce_core/juce_core.h>
#include <deque>
#include <vector>

namespace ostfstudio {
enum class MixerTrackGroup { voicing, melody, vocal, pitched, other };

struct PitchedNote {
    juce::int64 startTick;
    juce::int64 endTick;
    int pitch;
    std::optional<int> velocity;
    bool enabled = true;
};

struct PerformanceCurve {
    struct Point { juce::int64 tick; double value; };
    juce::String label;
    std::vector<Point> points;
    bool isCurve = false;
    bool stepped = false;
    bool cents = false;
};

struct SessionMix {
    struct MidiRoute { int channel = 1; std::optional<std::size_t> destination; };
    std::vector<ChannelMix> tracks;
    std::vector<MidiRoute> midiRoutes;
    std::vector<juce::var> instruments;
    MixerRouting routing;
    static constexpr std::size_t maxEffects = 8;
    std::array<juce::String, 3 + maxMixerReturns> insertIds;
    std::array<juce::StringArray, 3 + maxMixerReturns> outputEffects;
    std::vector<juce::StringArray> trackEffects;
    std::vector<bool> audioTracks;
};

struct ProjectAudio {
    std::size_t track = 0;
    juce::String path;
    juce::File localFile;
    juce::MemoryBlock embedded;
};

enum class TrackKind { pitched, drum, audio };

struct OstfpPreview {
    static constexpr std::size_t maxTracks = 8;
    juce::String title;
    BarSpan bars {1, 1};
    juce::int64 ticksPerBar = 0;
    juce::int64 ppq = 0;
    double previewBpm = 0.0; // A non-zero value denotes one fixed source tempo.
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
    juce::Result editTracks(TrackKind kind, std::optional<std::size_t> remove, std::unique_ptr<OstfpPreview>& result) const;
    bool insertGeneratorPattern(std::size_t track, int bar);
    bool addSessionClip(std::size_t track, juce::String name, std::vector<PitchedNote> notes);
};

juce::Result readOstfPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result);
juce::Result readOstfpPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result);
juce::Result writeOstfPreview(const OstfpPreview& project, juce::OutputStream& output);
juce::Result writeOstfPreview(const OstfpPreview& project, const SessionMix& mix, juce::OutputStream& output);
juce::Result writeOstfPreview(const OstfpPreview& project, const SessionMix& mix, const juce::File& destination,
                             juce::OutputStream& output);
juce::Result writeOstfpPreview(const OstfpPreview& project, const SessionMix& mix, juce::OutputStream& output);
}
