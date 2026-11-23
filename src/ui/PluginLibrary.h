#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

// The Windows native dialog cannot select both single files and VST3 bundle folders.
#if JUCE_WINDOWS
inline constexpr bool useNativeVst3Chooser = false;
#else
inline constexpr bool useNativeVst3Chooser = true;
#endif

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
    const auto common = juce::SystemStats::getEnvironmentVariable("CommonProgramFiles", {});
    if (common.isNotEmpty()) paths.add(juce::File(common).getChildFile("VST3/OSTF Studio"), 1);
#endif
    for (int i = 0; i < paths.getNumPaths(); ++i) {
        auto bundle = paths[i].getChildFile(filename);
        if (!bundle.isDirectory()) bundle = paths[i].getChildFile("OSTF Studio Effects").getChildFile(filename);
        if (!bundle.isDirectory()) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        library.scanAndAddFile(bundle.getFullPathName(), true, found, format);
#if JUCE_WINDOWS
        if (!library.getTypeForFile(bundle.getFullPathName())) continue;
        if (local.isNotEmpty()) {
            const auto legacyRoot = juce::File(local).getChildFile("Programs/Common/VST3");
            for (const auto& old : library.getTypes()) {
                if (old.manufacturerName != "OSTF Studio" || old.pluginFormatName != "VST3") continue;
                const juce::File oldFile(old.fileOrIdentifier);
                if (oldFile != bundle && (oldFile == legacyRoot.getChildFile(filename)
                    || oldFile == legacyRoot.getChildFile("OSTF Studio Effects").getChildFile(filename)))
                    library.removeType(old);
            }
        }
#endif
        break;
    }
}
