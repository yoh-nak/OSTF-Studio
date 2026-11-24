#pragma once
#include <array>
#include <juce_core/juce_core.h>

namespace ostfstudio::musicxml {
// Written tonics for key signatures from seven flats through seven sharps.
inline const std::array<const char *, 15> &tonics(bool minor) {
    static constexpr std::array<const char *, 15> majorKeys{"Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C",
                                                            "G",  "D",  "A",  "E",  "B",  "F#", "C#"};
    static constexpr std::array<const char *, 15> minorKeys{"Ab", "Eb", "Bb", "F",  "C",  "G",  "D", "A",
                                                            "E",  "B",  "F#", "C#", "G#", "D#", "A#"};
    return minor ? minorKeys : majorKeys;
}
} // namespace ostfstudio::musicxml
