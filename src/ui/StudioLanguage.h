#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace ostfstudio {
inline juce::String resolvedLanguage(const juce::String& preference, const juce::String& osLanguage) {
    const auto tag = (preference == "system" ? osLanguage : preference).trim().toLowerCase().replaceCharacter('_', '-');
    return tag.upToFirstOccurrenceOf("-", false, false) == "ja" ? "ja" : "en";
}
inline juce::PropertiesFile::Options viewSettingsOptions() {
    juce::PropertiesFile::Options options;
    options.applicationName = "OSTF Studio View";
    options.filenameSuffix = ".settings";
    options.folderName = "OSTF Studio";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}
void loadStudioLanguage();
}
