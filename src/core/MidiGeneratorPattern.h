#pragma once
#include <array>

namespace ostfstudio {
inline constexpr char midiGeneratorName[] = "OSTF MIDI Generator";
inline constexpr std::array generatorPitches {60, 64, 67, 64};
inline constexpr int generatorVelocity = 92;

constexpr int generatorPitchAtBeat(long long beat) {
    constexpr auto count = static_cast<long long>(generatorPitches.size());
    return generatorPitches[(beat % count + count) % count];
}
}
