#pragma once

#include "ui/ArrangementView.h"
#include <juce_core/juce_core.h>
#include <deque>
#include <vector>

namespace ostfstudio {
enum class MixerTrackGroup { voicing, vocal, pitched, other };

struct PitchedNote {
    juce::int64 startTick;
    juce::int64 endTick;
    int pitch;
};

struct OstfpPreview {
    juce::String title;
    BarSpan bars {1, 1};
    juce::int64 ticksPerBar = 0;
    juce::int64 ppq = 0;
    double previewBpm = 0.0; // A non-zero value denotes one fixed source tempo.
    std::vector<ArrangementLane> globalLanes, trackLanes;
    std::vector<std::vector<PitchedNote>> pitchedNotes;
    std::vector<MixerTrackGroup> mixerGroups;
    int chordVoicings = 0;
    std::deque<std::vector<ArrangementRegion>> regions;
    std::deque<juce::String> labels;

    [[nodiscard]] ArrangementView view() const { return {bars, globalLanes, trackLanes}; }
    const char* keepLabel(juce::String label);
};

juce::Result readOstfpPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result);
}
