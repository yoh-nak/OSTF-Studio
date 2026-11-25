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

inline bool matchesVst3Instrument(const juce::var& reference, const juce::PluginDescription& description) {
    if (reference["format"] != "vst3" || !description.isInstrument || description.pluginFormatName != "VST3") return false;
    const auto requestedName = reference["name"].toString();
    if (description.name != requestedName && !(description.manufacturerName == "OSTF Studio"
        && (requestedName == "OSTF " + description.name || description.name == "OSTF " + requestedName))) return false;
    const auto vendor = reference["vendor"].toString();
    if (vendor.isNotEmpty() && description.manufacturerName != vendor) return false;
    const auto id = reference["pluginId"].toString();
    if (reference["namespace"] == "org.juce.vst3.uid")
        return id.equalsIgnoreCase(juce::String::toHexString(description.uniqueId));
    if (!reference["namespace"].isVoid() || id.length() != 32 || !id.containsOnly("0123456789abcdefABCDEF")) return false;
    // JUCE hashes the four normalized 32-bit words of a VST3 component class ID.
    std::uint32_t hash = 0;
    for (int i = 0; i < 32; i += 8)
        hash = hash * 31u + static_cast<std::uint32_t>(id.substring(i, i + 8).getHexValue32());
    return hash == static_cast<std::uint32_t>(description.uniqueId);
}
