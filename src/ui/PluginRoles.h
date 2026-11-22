#pragma once

#include "core/MidiGeneratorPattern.h"
#include <juce_audio_processors/juce_audio_processors.h>

inline bool isOstfGenerator(const juce::PluginDescription& plugin) {
    return plugin.pluginFormatName == "VST3" && plugin.name == ostfstudio::midiGeneratorName
        && (plugin.manufacturerName == "OSTF Studio" || plugin.manufacturerName == "OSTF");
}

inline bool isOstfArranger(const juce::PluginDescription& plugin) {
    return plugin.pluginFormatName == "VST3" && plugin.name == "OSTF Arranger"
        && plugin.manufacturerName == "OSTF Studio";
}
