#pragma once
#include "ui/OstfpPreview.h"
#include "ui/AnalysisService.h"

namespace ostfstudio {
juce::File findAudiveris();
juce::String scoreImportFileFilter();
juce::Result analyseScoreImport(const juce::File&, std::unique_ptr<OstfpPreview>&, juce::String& summary,
                               AnalysisConnection provider = {});
// Builds a new project in memory. Opening and saving the current song are independent.
juce::Result prepareMediaImport(const juce::File& source, std::unique_ptr<OstfpPreview>& project, juce::String& summary,
                                bool transcribeMelody = false, bool analyseHarmony = false);
juce::Result prepareStemImport(const juce::File& directory, std::unique_ptr<OstfpPreview>&, juce::String& summary);
juce::Result analyseImportParts(const juce::File &, bool useDemucs, std::unique_ptr<OstfpPreview> &,
                                std::unique_ptr<OstfpPreview> &noteParts, juce::String &summary);
juce::Result separateStemImport(const juce::File& wav, std::unique_ptr<OstfpPreview>&, juce::String& summary);
juce::Result combineImportParts(const OstfpPreview& waves,const OstfpPreview& notes,std::unique_ptr<OstfpPreview>& combined);
juce::Result placeImportTrack(const OstfpPreview& source, int index, OstfpPreview& destination,
                              std::optional<std::size_t> track, juce::int64 tick, juce::MemoryBlock& before,
                              bool replace = false);
juce::Result saveMediaImport(const OstfpPreview&, const juce::File& destination);
} // namespace ostfstudio
