#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace ostfstudio {
enum class SurfaceModel { none, smk25ii, mixer };
inline SurfaceModel surfaceModel(juce::String name) {
    auto compact = name.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
    if ((compact.contains("smk25ii") || compact.contains("smk252")) && !compact.contains("daw"))
        return SurfaceModel::smk25ii;
    if (compact.contains("smcmixer"))
        return SurfaceModel::mixer;
    return SurfaceModel::none;
}
struct SurfaceAction {
    enum Kind { none, pad, knob, fader, relativePan, learned } kind = none;
    int index = 0, value = 0;
};
struct ControllerMap {
    juce::String port;
    SurfaceModel model = SurfaceModel::none;
    std::array<int, 16> notes{}, pitches{};
    std::array<int, 8> cc{};
    ControllerMap() {
        for (int i = 0; i < 16; ++i)
            notes[i] = 9 * 128 + 36 + i; // Studio pad preset, channel 10
        for (int i = 0; i < 8; ++i)
            cc[i] = 16 + i; // Studio knob preset, channel 1
        pitches.fill(-1);
    }
    int padIndex(const juce::MidiMessage &m) const {
        if (model != SurfaceModel::smk25ii || !(m.isNoteOn() || m.isNoteOff()))
            return -1;
        auto it = std::find(notes.begin(), notes.end(), (m.getChannel() - 1) * 128 + m.getNoteNumber());
        return it == notes.end() ? -1 : int(it - notes.begin());
    }
    SurfaceAction action(const juce::MidiMessage &m) const {
        if (model == SurfaceModel::mixer) {
            if (m.isPitchWheel() && m.getChannel() <= 8)
                return {SurfaceAction::fader, m.getChannel() - 1, m.getPitchWheelValue()};
            if (m.isController() && m.getChannel() == 1 && m.getControllerNumber() >= 16 &&
                m.getControllerNumber() < 24) {
                const int v = m.getControllerValue();
                return {SurfaceAction::relativePan, m.getControllerNumber() - 16, (v & 64) ? -(v & 63) : v};
            }
        }
        if (model == SurfaceModel::smk25ii) {
            if (m.isNoteOn()) {
                const auto index = padIndex(m);
                if (index >= 0)
                    return {SurfaceAction::pad, index, pitches[index]};
            }
            if (m.isController()) {
                const auto key = (m.getChannel() - 1) * 128 + m.getControllerNumber();
                const auto it = std::find(cc.begin(), cc.end(), key);
                if (it != cc.end())
                    return {SurfaceAction::knob, int(it - cc.begin()), m.getControllerValue()};
            }
        }
        return {};
    }
    void bind(int control, int key) {
        auto assign = [&](auto &keys, int index) {
            for (auto &existing : keys)
                if (existing == key)
                    existing = -1;
            keys[index] = key;
        };
        if (control >= 0 && control < 16)
            assign(notes, control);
        else if (control >= 16 && control < 24)
            assign(cc, control - 16);
    }
};
} // namespace ostfstudio
