#pragma once
#include "core/Smk25Preset.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ostfstudio {
enum class MixerShortcut { volume, pan, mute, solo, select };
inline juce::String mixerShortcut(const juce::String& description, size_t track, MixerShortcut action) {
    if (track >= 64) return description;
    const int page = int(track / 8), strip = int(track % 8);
    const bool bankB = action == MixerShortcut::pan || action == MixerShortcut::select;
    const bool knob = action == MixerShortcut::pan || action == MixerShortcut::volume;
    const int control = strip + (knob ? 16 : action == MixerShortcut::solo ? 8 : 0);
    static const Smk25Rig rig;
    const auto message = rig.presets[page].control(bankB ? 1 : 0, control);
    return description + "\n" + TRANS("MIDI shortcut") + "  SMK-25 II  P" + juce::String(page + 1)
        + (bankB ? " B" : " A") + "  /  Ch " + juce::String(message.channel)
        + (knob ? "  CC " : "  Note ") + juce::String(message.number);
}

inline juce::String mixerPadShortcut(const juce::String& description, int pad, bool selectedTrack = false) {
    const auto qualifier = selectedTrack ? "  /  " + TRANS("Selected track") : juce::String{};
    return description + "\n" + TRANS("MIDI shortcut") + "  SMK-25 II  P1-P8 B  /  Ch 9-16  Note "
        + juce::String(52 + pad) + qualifier;
}
}
