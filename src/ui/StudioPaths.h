#pragma once
#include <juce_core/juce_core.h>

// On Windows the binary inside a VST3 bundle can itself end in ".vst3".
inline juce::File studioBundleDirectory(juce::File module) {
    for (auto dir = module; dir != dir.getParentDirectory(); dir = dir.getParentDirectory())
        if (dir.isDirectory() && dir.hasFileExtension("vst3")) return dir.getParentDirectory();
    return module.getParentDirectory();
}

inline juce::File studioToolsExecutable() {
    const auto module = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#if OSTF_STUDIO_HOSTED
    const auto directory = studioBundleDirectory(module);
    #if JUCE_WINDOWS
    const auto sibling = directory.getChildFile("OSTF Studio.exe");
    if (sibling.existsAsFile()) return sibling;
    const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    if (local.isNotEmpty()) return juce::File(local).getChildFile("Programs/OSTF Studio/OSTF Studio.exe");
    #elif JUCE_MAC
    const auto path = "OSTF Studio.app/Contents/MacOS/OSTF Studio";
    const auto sibling = directory.getChildFile(path);
    if (sibling.existsAsFile()) return sibling;
    return juce::File("/Applications").getChildFile(path);
    #endif
    return {};
#else
    return module;
#endif
}
