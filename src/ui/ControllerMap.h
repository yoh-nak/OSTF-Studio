#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace ostfstudio {
struct SurfaceAction {
    enum Kind { none, fader, relativePan, arm, solo, mute, select, bankLeft, bankRight,
                channelLeft, channelRight, rewind, forward, stop, play, record } kind = none;
    int index = 0, value = 0;
};

inline SurfaceAction mackieAction(const juce::MidiMessage& m) {
    using A = SurfaceAction;
    if (m.isPitchWheel() && m.getChannel() <= 8)
        return {A::fader, m.getChannel() - 1, m.getPitchWheelValue()};
    if (m.getChannel() != 1) return {};
    if (m.isController() && m.getControllerNumber() >= 16 && m.getControllerNumber() < 24) {
        const int value = m.getControllerValue();
        return {A::relativePan, m.getControllerNumber() - 16, (value & 64) ? -(value & 63) : value};
    }
    if (!m.isNoteOn()) return {};
    const int note = m.getNoteNumber();
    if (note < 32) {
        constexpr A::Kind strips[]{A::arm, A::solo, A::mute, A::select};
        return {strips[note / 8], note % 8};
    }
    switch (note) {
        case 0x2e: return {A::bankLeft};
        case 0x2f: return {A::bankRight};
        case 0x30: return {A::channelLeft};
        case 0x31: return {A::channelRight};
        case 0x5b: return {A::rewind};
        case 0x5c: return {A::forward};
        case 0x5d: return {A::stop};
        case 0x5e: return {A::play};
        case 0x5f: return {A::record};
        default: return {};
    }
}
}
