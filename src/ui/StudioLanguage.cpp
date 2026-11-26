#include "ui/StudioLanguage.h"
#include <BinaryData.h>

namespace ostfstudio {
void loadStudioLanguage() {
    juce::PropertiesFile settings(viewSettingsOptions());
    const auto language = resolvedLanguage(settings.getValue("language", "system"), juce::SystemStats::getDisplayLanguage());
    juce::LocalisedStrings::setCurrentMappings(language == "ja" ? new juce::LocalisedStrings(
        juce::String::fromUTF8(BinaryData::Japanese_txt, BinaryData::Japanese_txtSize), false) : nullptr);
}
}
