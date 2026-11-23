#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ostfstudio {

// An SMK-25 II .mkcII file holds one preset. Keep its original image so editing
// a control also round-trips keyboard settings, pad macros and unknown modes.
class Smk25Preset {
public:
    static constexpr int fileSize = 2504;
    enum class Mode { unsupported, note, program, cc };
    struct Control {
        Mode mode = Mode::unsupported;
        int channel = 1, number = 0;
        std::array<int, 3> rgb{};
    };
    using Image = std::array<std::uint8_t, fileSize>;

    static std::optional<Smk25Preset> read(std::span<const std::uint8_t> source) {
        if (source.size() != fileSize) return {};
        Smk25Preset preset;
        std::copy(source.begin(), source.end(), preset.image.begin());
        for (int bank = 0; bank < 2; ++bank)
            for (int slot = 0; slot < 24; ++slot)
                if (preset.control(bank, slot).mode != Mode::unsupported) return preset;
        return {};
    }

    static Smk25Preset studioPreset() {
        Smk25Preset preset;
        auto& bytes = preset.image;
        // Chromatic 25-key map, full velocity, modulation and sustain CCs.
        bytes[4] = 127;
        for (int key = 0; key < 25; ++key) bytes[5 + key] = static_cast<std::uint8_t>(key);
        bytes[2496] = 1;
        bytes[2498] = 127;
        bytes[2501] = 64;
        bytes[2503] = 127;
        for (int b = 0; b < 2; ++b) {
            for (int i = 0; i < 16; ++i) {
                auto* pad = bytes.data() + offset(b, i);
                pad[1] = 9;
                pad[2] = static_cast<std::uint8_t>(36 + b * 16 + i);
                pad[4] = 127;
                pad[5] = 55;
                pad[6] = 180;
                pad[7] = 170;
                pad[8] = 255;
            }
            for (int i = 0; i < 8; ++i) {
                auto* knob = bytes.data() + offset(b, i + 16);
                knob[1] = 2;
                knob[3] = static_cast<std::uint8_t>(16 + b * 8 + i);
                knob[5] = 127;
            }
        }
        return preset;
    }

    // All sixteen pages have distinct MIDI addresses. Keep keyboard, wheels and pedal untouched.
    Smk25Preset forStudioSlot(int slot) const {
        auto result = *this;
        if (slot < 0 || slot >= 8) return result;
        const auto defaults = studioPreset();
        for (int bank = 0; bank < 2; ++bank) {
            for (int i = 0; i < 24; ++i) {
                const auto pos = offset(bank, i);
                std::copy_n(defaults.image.data() + pos, i < 16 ? 74 : 6, result.image.data() + pos);
                auto control = result.control(bank, i);
                control.channel = slot + 9;
                if (i < 16) {
                    if (bank == 0) control.rgb = i < 4 ? std::array{240,100,24} : i < 8 ? std::array{30,170,140} : std::array{70,120,210};
                    else if (i < 8) control.rgb = {40,160,215};
                    else {
                        constexpr std::array<std::array<int,3>,8> commands {{
                            {240,190,30}, {35,110,230}, {220,45,55}, {40,200,110},
                            {185,185,185}, {245,20,35}, {140,90,215}, {235,85,200}
                        }};
                        control.rgb = commands[i - 8];
                    }
                }
                result.edit(bank, i, control);
            }
        }
        return result;
    }

    const Image& bytes() const { return image; }

    // Each bank exposes 16 pads followed by eight knobs.
    Control control(int bank, int slot) const {
        if (!valid(bank, slot)) return {};
        const auto* p = image.data() + offset(bank, slot);
        if (slot >= 16) {
            if (p[0] != 0 || p[1] != 2 || p[2] > 15 || p[3] > 127) return {};
            return {Mode::cc, p[2] + 1, p[3], {}};
        }
        if ((p[0] != 0 && p[0] != 3) || p[1] > 15) return {};
        const auto number = p[p[0] == 3 ? 4 : 2];
        if (number > 127) return {};
        return {p[0] == 3 ? Mode::program : Mode::note, p[1] + 1, number, {p[5], p[6], p[7]}};
    }

    bool edit(int bank, int slot, const Control& next) {
        const auto before = control(bank, slot);
        if (before.mode == Mode::unsupported || next.channel < 1 || next.channel > 16
            || next.number < 0 || next.number > 127) return false;
        auto* p = image.data() + offset(bank, slot);
        if (slot >= 16) {
            if (next.mode != Mode::cc) return false;
            p[2] = static_cast<std::uint8_t>(next.channel - 1);
            p[3] = static_cast<std::uint8_t>(next.number);
        } else {
            if (next.mode != Mode::note && next.mode != Mode::program) return false;
            if (std::any_of(next.rgb.begin(), next.rgb.end(), [](int v) { return v < 0 || v > 255; })) return false;
            if (before.mode != next.mode) {
                p[0] = next.mode == Mode::note ? 0 : 3;
                p[2] = p[3] = 0;
                p[4] = 127;
            }
            p[1] = static_cast<std::uint8_t>(next.channel - 1);
            p[next.mode == Mode::program ? 4 : 2] = static_cast<std::uint8_t>(next.number);
            std::copy(next.rgb.begin(), next.rgb.end(), p + 5);
        }
        return true;
    }

    struct Bindings {
        std::array<int, 16> notes;
        std::array<int, 8> cc;
    };
    // The live surface consumes only notes and absolute CC. Ambiguous input
    // addresses must be resolved in the editor before replacing live bindings.
    std::optional<Bindings> studioBindings(int bank) const {
        if (bank < 0 || bank > 1) return {};
        Bindings result;
        result.notes.fill(-1);
        result.cc.fill(-1);
        for (int slot = 0; slot < 24; ++slot) {
            const auto c = control(bank, slot);
            if (c.mode != Mode::note && c.mode != Mode::cc) continue;
            const auto key = (c.channel - 1) * 128 + c.number;
            auto assign = [key](auto& group, int index) {
                if (std::find(group.begin(), group.end(), key) != group.end()) return false;
                group[index] = key;
                return true;
            };
            if (slot < 16 ? !assign(result.notes, slot) : !assign(result.cc, slot - 16)) return {};
        }
        return result;
    }
private:
    Image image{};
    static bool valid(int bank, int slot) { return bank >= 0 && bank < 2 && slot >= 0 && slot < 24; }
    static int offset(int bank, int slot) {
        if (slot < 16) return 126 + (16 * bank + slot) * 74;
        return 30 + (8 * bank + slot - 16) * 6;
    }
};

// Complete user-edited layout. Negative entries are unassigned; ambiguous MIDI addresses
// cannot identify a bank on input, so validate a new layout before publishing it.
class Smk25Rig {
public:
    std::array<Smk25Preset,8> presets;
    Smk25Rig() {
        const auto base = Smk25Preset::studioPreset();
        for (int slot = 0; slot < 8; ++slot) presets[slot] = base.forStudioSlot(slot);
        rebuild();
    }
    bool rebuild() {
        notes.fill(-1); controllers.fill(-1);
        for (int slot = 0; slot < 8; ++slot) for (int bank = 0; bank < 2; ++bank) {
            const auto map = presets[slot].studioBindings(bank);
            if (!map) return false;
            for (int i = 0; i < 24; ++i) {
                const int key = i < 16 ? map->notes[i] : map->cc[i - 16];
                if (key < 0) continue;
                auto& hit = (i < 16 ? notes : controllers)[size_t(key)];
                if (hit >= 0) return false;
                hit = (slot * 2 + bank) * 24 + i;
            }
        }
        return true;
    }
    int address(bool cc, int channel, int number) const {
        if (channel < 1 || channel > 16 || number < 0 || number > 127) return -1;
        return (cc ? controllers : notes)[size_t((channel - 1) * 128 + number)];
    }
private:
    std::array<int,2048> notes, controllers;
};
}
