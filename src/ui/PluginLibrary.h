#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ui/PluginRoles.h"
#include "ui/StudioPaths.h"

// The Windows native dialog cannot select both single files and VST3 bundle folders.
#if JUCE_WINDOWS
inline constexpr bool useNativeVst3Chooser = false;
#else
inline constexpr bool useNativeVst3Chooser = true;
#endif

inline juce::File pluginDataDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OSTF Studio");
}

inline bool sameVst3Class(const juce::PluginDescription& a, const juce::PluginDescription& b) {
    if (a.pluginFormatName != "VST3" || b.pluginFormatName != "VST3" || a.manufacturerName != b.manufacturerName)
        return false;
    if (a.uniqueId && b.uniqueId) return a.uniqueId == b.uniqueId;
    return a.deprecatedUid && a.deprecatedUid == b.deprecatedUid;
}

inline juce::Array<juce::PluginDescription> pluginCatalog(const juce::Array<juce::PluginDescription>& scanned,
                                                          const juce::File& appDirectory) {
    juce::Array<juce::PluginDescription> result;
    auto priority = [&](const juce::PluginDescription& type) {
        const juce::File file(type.fileOrIdentifier);
        if (!file.exists()) return 0;
        const auto parent = file.getParentDirectory();
        return parent == appDirectory || parent == appDirectory.getChildFile("OSTF Studio Effects") ? 2 : 1;
    };
    for (const auto& type : scanned) {
        if (isOstfGenerator(type) || isOstfArranger(type) || isStudioWorkstation(type)) continue;
        auto* previous = std::find_if(result.begin(), result.end(), [&](const auto& p) { return sameVst3Class(p, type); });
        if (previous == result.end()) result.add(type);
        else if (priority(type) > priority(*previous)) *previous = type;
    }
    return result;
}

inline void registerBundledVst3(juce::KnownPluginList& library, const juce::String& filename) {
    juce::VST3PluginFormat format;
    auto paths = format.getDefaultLocationsToSearch();
    paths.add(studioBundleDirectory(juce::File::getSpecialLocation(juce::File::currentExecutableFile)), 0);
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
        const auto cached = library.getTypeForFile(bundle.getFullPathName());
        const bool oldName = cached && cached->name.startsWith("OSTF ")
            && cached->name != "OSTF Scale Editor" && cached->name != "OSTF Formant Singer";
        library.scanAndAddFile(bundle.getFullPathName(), !oldName, found, format);
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
