#include "core/Smk25BleProtocol.h"
#include <fstream>
#include <set>
#include <iostream>
#include <vector>

using Preset = ostfstudio::Smk25Preset;

namespace {
using namespace ostfstudio::smkble;

bool testTransfer() {
    const auto original = Preset::studioPreset().bytes();
    auto memory = original;
    auto desired = original;
    desired[100] ^= 1;
    desired[101] ^= 1;
    desired[500] ^= 2;
    const auto base = 7 * Preset::fileSize;
    int commits = 0, chunks = 0;
    if (frame(0x11, {}) != Bytes{0,0x59,0x11,0,0,0,0xff}
        || frame(0x23, memoryRequest(0, 1000)) != Bytes{0,0x59,0x23,8,0,0,5,0,0,0,0,0xe8,3,0,0x0f}) return false;
    Exchange wire = [&](const Bytes& msg) {
        if (msg[2] == 0x11) return frame(0x11, {'S','M','K','2','5','I','I','_','0','0','1',0});
        if (msg.size() < 15 || msg[6] != 5) throw std::runtime_error("request shape");
        auto addr = little(std::span(msg).subspan(7, 4));
        auto size = little(std::span(msg).subspan(11, 3));
        if (msg[2] == 0x22 && size == 0 && addr == 0) { ++commits; return frame(0, {0}); }
        if (addr < base || addr + size > base + memory.size()) throw std::runtime_error("wrong preset slot");
        auto at = memory.begin() + (addr - base);
        if (msg[2] == 0x23) {
            Bytes out(msg.begin() + 6, msg.begin() + 14);
            out.insert(out.end(), at, at + size);
            return frame(0x23, out);
        }
        if (msg[2] != 0x22 || msg.size() != size + 15 || size > 5) throw std::runtime_error("write shape");
        std::copy_n(msg.begin() + 14, size, at);
        ++chunks;
        return frame(0, {0});
    };
    identify(wire);
    auto previous = readPreset(wire, 7);
    if (previous != original) return false;
    writePreset(wire, 7, previous, desired, 5);
    if (memory != desired || commits != 1 || chunks != 2) return false;
    writePreset(wire, 7, memory, desired, 5);
    if (commits != 1 || chunks != 2) return false;
    auto rejected = [](auto fn, Failure expected) {
        try { fn(); } catch (const TransferError& e) { return expected == e.reason; }
        return false;
    };
    if (!rejected([&] { identify([](const auto&) { return frame(0x11, {'S','M','K','2','5','_','1'}); }); }, Failure::model)) return false;
    if (!rejected([&] { readPreset([](auto) { return Bytes{0,89,35,0,0,0,0}; },0); }, Failure::protocol)) return false;
    if (!rejected([&] { readPreset(wire,8); }, Failure::protocol)) return false;
    auto mismatched = desired;
    mismatched[99] ^= 1;
    if (!rejected([&] { writePreset([&](const Bytes& msg) {
        return msg[2] == 0x22 ? frame(0,{0}) : wire(msg);
    },7,desired,mismatched,5); }, Failure::verify)) return false;
    return rejected([&] { writePreset([](const auto&) {return frame(0,{1});},7,desired,mismatched,5); }, Failure::protocol);
}
}

int main(int argc, char** argv) {
    if (!testTransfer()) return 18;
    if (argc > 1) {
        // Optional interop check using users' existing preset exports, read-only.
        for (int i = 1; i < argc; ++i) {
            std::ifstream f(argv[i], std::ios::binary);
            std::vector<std::uint8_t> source{std::istreambuf_iterator<char>(f), {}};
            auto p = Preset::read(source);
            if (!p || !std::equal(source.begin(), source.end(), p->bytes().begin())) return 1;
            for (int b = 0; b < 2; ++b) {
                for (int slot = 0; slot < 24; ++slot) {
                    auto c = p->control(b, slot);
                    if (c.mode == Preset::Mode::unsupported) continue;
                    if (!p->edit(b, slot, c)) return 2;
                }
            }
            if (!std::equal(source.begin(), source.end(), p->bytes().begin())) return 3;
        }
        std::cout << argc - 1 << " preset exports round-tripped\n";
        return 0;
    }
    const auto starter = Preset::studioPreset();
    std::set<int> notes, knobs;
    auto custom = starter.bytes();
    custom[12] = 42; custom.back() = 82;
    auto base = Preset::read(custom).value();
    auto duplicate = base.control(0, 16);
    duplicate.number = 20;
    if (!base.edit(0, 16, duplicate)) return 25;
    for (int page = 0; page < 8; ++page) {
        const auto rig = base.forStudioSlot(page);
        for (int bank = 0; bank < 2; ++bank)
            for (int knob = 0; knob < 8; ++knob) {
                const auto cc = rig.control(bank, 16 + knob);
                if (cc.number != 16 + 8 * bank + knob || cc.channel != 9 + page) return 26;
            }
        if (rig.bytes()[12] != 42 || rig.bytes().back() != 82 || rig.forStudioSlot(page).bytes() != rig.bytes()) return 19;
        for (int bank = 0; bank < 2; ++bank) {
            const auto map = rig.studioBindings(bank).value();
            for (int key : map.notes) if (!notes.insert(key).second) return 20;
            for (int key : map.cc) if (!knobs.insert(key).second) return 21;
        }
        if (rig.control(1, 9).rgb == rig.control(1, 10).rgb) return 22;
    }
    for (int b = 0; b < 2; ++b) {
        const auto bank = starter.studioBindings(b);
        if (!bank || bank->notes[15] != 9 * 128 + 51 + b * 16 || bank->cc[7] != 23 + b * 8
            || !Preset::read(starter.bytes())) return 17;
    }
    ostfstudio::Smk25Rig rig;
    auto customPad = rig.presets[7].control(1, 15);
    customPad.channel = 2; customPad.number = 120;
    if (!rig.presets[7].edit(1,15,customPad) || !rig.rebuild() || rig.address(false,2,120) != 15 * 24 + 15 ||
        rig.address(false,16,67) >= 0 || rig.address(true,16,31) != 15 * 24 + 23) return 23;
    customPad.channel = 9; customPad.number = 36;
    if (!rig.presets[7].edit(1,15,customPad) || rig.rebuild()) return 24;
    Preset::Image image;
    image.fill(0xee); // Opaque bytes must survive edits.
    auto knob = 30 + 8 * 6;
    image[knob] = 0; image[knob + 1] = 2; image[knob + 2] = 3; image[knob + 3] = 51;
    auto pad = 126 + 31 * 74;
    image[pad] = 3; image[pad + 1] = 15; image[pad + 4] = 126;
    auto document = Preset::read(image);
    if (!document || Preset::read(std::span(image).first(2503))) return 4;
    auto p = document->control(1, 15);
    if (p.mode != Preset::Mode::program || p.channel != 16 || p.number != 126) return 5;
    auto k = document->control(1, 16);
    if (k.mode != Preset::Mode::cc || k.channel != 4 || k.number != 51) return 6;
    p.channel = 5; p.number = 32; p.rgb = {12, 34, 56};
    if (!document->edit(1, 15, p)) return 7;
    auto expected = image;
    expected[pad + 1] = 4; expected[pad + 4] = 32;
    expected[pad + 5] = 12; expected[pad + 6] = 34; expected[pad + 7] = 56;
    if (document->bytes() != expected) return 8;
    k.channel = 9; k.number = 90;
    if (!document->edit(1, 16, k)) return 9;
    expected[knob + 2] = 8; expected[knob + 3] = 90;
    if (document->bytes() != expected) return 10;
    k.number = 128;
    if (document->edit(1, 16, k) || document->edit(-1, 16, p) || document->edit(0, 24, p)
        || document->edit(0, 0, p) || document->bytes() != expected) return 11;
    auto live = document->studioBindings(1);
    if (!live || live->cc[0] != 8 * 128 + 90 || live->notes[15] != -1 || live->cc[1] != -1) return 12;
    p.mode = Preset::Mode::note; p.number = 66;
    if (!document->edit(1, 15, p)) return 13;
    live = document->studioBindings(1);
    if (!live || live->notes[15] != 4 * 128 + 66 || document->bytes()[pad + 4] != 127 || document->bytes()[pad + 9] != 0xee) return 14;
    expected = document->bytes();
    std::copy_n(expected.begin() + pad, 74, expected.begin() + pad - 74);
    if (Preset::read(expected)->studioBindings(1)) return 15;
    expected.fill(255);
    if (Preset::read(expected)) return 16;
    return 0;
}
