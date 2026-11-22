#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

inline juce::File pluginDataDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OSTF Studio");
}

inline void registerBundledVst3(juce::KnownPluginList& library, const juce::String& filename) {
    juce::VST3PluginFormat format;
    auto paths = format.getDefaultLocationsToSearch();
    paths.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory(), 0);
#if JUCE_WINDOWS
    const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    if (local.isNotEmpty()) paths.add(juce::File(local).getChildFile("Programs/Common/VST3"), 1);
#endif
    for (int i = 0; i < paths.getNumPaths(); ++i) {
        const auto bundle = paths[i].getChildFile(filename);
        if (!bundle.isDirectory()) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        library.scanAndAddFile(bundle.getFullPathName(), true, found, format);
        break;
    }
}
