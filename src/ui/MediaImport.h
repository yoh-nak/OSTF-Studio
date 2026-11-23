#pragma once
#include "ui/OstfpPreview.h"

namespace ostfstudio {
// Builds a new project in memory. Opening and saving the current song are independent.
juce::Result prepareMediaImport(const juce::File& source, std::unique_ptr<OstfpPreview>& project, juce::String& summary,
                                bool transcribeMelody = false);
juce::Result saveMediaImport(const OstfpPreview&, const juce::File& destination);
} // namespace ostfstudio
