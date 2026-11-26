#pragma once
#include "ui/OstfpPreview.h"
#include <algorithm>
#include <cmath>

namespace ostfstudio {
// Observations are in decoded asset frames. Multiple hypotheses can describe the
// same instant, and a replacement render can have a different set of observations.
class AudioModelPitches {
public:
    explicit AudioModelPitches(const juce::var& production = {}) {
        const auto& model = production["audioModel"];
        for (int i=0;i<production["audioRegions"].size();++i) {
            const auto& region=production["audioRegions"][i];
            regionAssets.emplace(region["id"].toString(),region["sourceAssetId"].toString());
        }
        std::map<juce::String,std::pair<juce::String,juce::String>> sources;
        for (int i=0;i<model["sources"].size();++i) {
            const auto& source=model["sources"][i];
            sources.emplace(source["id"].toString(),std::pair{source["assetId"].toString(),source["assetSha256"].toString()});
        }
        for (int i=0;i<model["objects"].size();++i) {
            const auto& note=model["objects"][i];
            const auto& range=note["sourceRange"];
            const auto source=sources.find(range["sourceId"].toString());
            const auto hz=double(note["pitch"]["centerHz"]);
            if (note["kind"]!="note" || source==sources.end() || !std::isfinite(hz) || hz<=0) continue;
            notes[source->second].push_back({juce::int64(range["startFrame"]),juce::int64(range["frameCount"]),hz});
        }
    }

    std::vector<double> frequenciesAt(const ProjectAudio& clip,juce::int64 frame) const {
        std::vector<double> found;
        if (clip.sha256.isEmpty() || frame<clip.startFrame || frame-clip.startFrame>=clip.frameCount) return found;
        const auto region=regionAssets.find(clip.regionId);
        if (region==regionAssets.end()) return found;
        const auto entry=notes.find({region->second,clip.sha256});
        if (entry==notes.end()) return found;
        for (const auto& note : entry->second)
            if (frame>=note.start && frame-note.start<note.frames) found.push_back(note.hz);
        std::sort(found.begin(),found.end());
        found.erase(std::unique(found.begin(),found.end()),found.end());
        return found;
    }
private:
    struct Observation { juce::int64 start,frames; double hz; };
    std::map<juce::String,juce::String> regionAssets;
    std::map<std::pair<juce::String,juce::String>,std::vector<Observation>> notes;
};
}
