#pragma once
#include "ui/OstfpPreview.h"

namespace ostfstudio {
// Builds a new project in memory. Opening and saving the current song are independent.
juce::Result prepareMediaImport(const juce::File& source, std::unique_ptr<OstfpPreview>& project, juce::String& summary,
                                bool transcribeMelody = false, bool analyseHarmony = false);
juce::Result prepareStemImport(const juce::File& directory, std::unique_ptr<OstfpPreview>&, juce::String& summary);
juce::Result separateStemImport(const juce::File& wav, std::unique_ptr<OstfpPreview>&, juce::String& summary);
juce::Result placeImportTrack(const OstfpPreview& source, int index, OstfpPreview& destination,
                              std::optional<std::size_t> track, juce::int64 tick, juce::MemoryBlock& before,
                              bool replace = false);
juce::Result saveMediaImport(const OstfpPreview&, const juce::File& destination);
} // namespace ostfstudio
