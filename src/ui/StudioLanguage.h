#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace ostfstudio {
inline juce::String resolvedLanguage(const juce::String& preference, const juce::String& osLanguage) {
    const auto tag = (preference == "system" ? osLanguage : preference).trim().toLowerCase().replaceCharacter('_', '-');
    return tag.upToFirstOccurrenceOf("-", false, false) == "ja" ? "ja" : "en";
}
juce::PropertiesFile::Options viewSettingsOptions();
void loadStudioLanguage();
}
