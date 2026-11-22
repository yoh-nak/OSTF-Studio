#include "ui/StudioLanguage.h"
#include <BinaryData.h>

namespace ostfstudio {
juce::PropertiesFile::Options viewSettingsOptions() {
    juce::PropertiesFile::Options options;
    options.applicationName = "OSTF Studio View";
    options.filenameSuffix = ".settings";
    options.folderName = "OSTF Studio";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

void loadStudioLanguage() {
    juce::PropertiesFile settings(viewSettingsOptions());
    const auto language = resolvedLanguage(settings.getValue("language", "system"), juce::SystemStats::getDisplayLanguage());
    juce::LocalisedStrings::setCurrentMappings(language == "ja" ? new juce::LocalisedStrings(
        juce::String::fromUTF8(BinaryData::Japanese_txt, BinaryData::Japanese_txtSize), false) : nullptr);
}
}
