#pragma once

#include "ui/ArrangementView.h"

namespace ostfstudio::demo {
using Region = ArrangementRegion;
using Lane = ArrangementLane;
inline constexpr BarSpan bars {1, 33};

inline constexpr Region sections[] {{{1, 9}, "Intro"}, {{9, 17}, "Verse"}, {{17, 25}, "Build"}, {{25, 33}, "Drop"}};
inline constexpr Region chords[] {{{1, 9}, "Am"}, {{9, 13}, "F"}, {{13, 17}, "G"}, {{17, 21}, "Am"}, {{21, 25}, "G"}, {{25, 33}, "Am"}};
inline constexpr Region keys[] {{{1, 17}, "A Aeolian"}, {{17, 25}, "A Dorian"}, {{25, 33}, "A Aeolian"}};
inline constexpr Region meters[] {{{1, 17}, "4/4"}, {{17, 25}, "3/4"}, {{25, 33}, "4/4"}};
inline constexpr Region tempos[] {{{1, 17}, "120"}, {{17, 25}, "124"}, {{25, 33}, "128"}};
inline constexpr Region phrases[] {{{1, 5}, "A"}, {{5, 9}, "A'"}, {{9, 17}, "B"}, {{17, 25}, "C"}, {{25, 33}, "D"}};

inline constexpr Region drums[] {{{1, 9}, "Intro beat"}, {{9, 17}, "Verse groove"}, {{17, 25}, "Riser beat"}, {{25, 33}, "Full kit"}};
inline constexpr Region bass[] {{{5, 17}, "Bassline"}, {{21, 25}, "Pickup"}, {{25, 33}, "Drop bass"}};
inline constexpr Region synth[] {{{1, 9}, "Pad"}, {{13, 25}, "Arpeggio"}, {{25, 33}, "Lead"}};
inline constexpr Region vocals[] {{{9, 17}, "Verse vocal"}, {{25, 33}, "Hook"}};
inline constexpr Region audio[] {{{1, 5}, "Ambience"}, {{21, 25}, "Riser"}, {{29, 33}, "Tail"}};

inline constexpr Lane globalLanes[] {
    {"SECTION", sections, TimelineLane::section}, {"CHORD", chords, TimelineLane::chord}, {"KEY / SCALE", keys, TimelineLane::key},
    {"METER", meters, TimelineLane::meter}, {"BPM", tempos, TimelineLane::tempo}, {"PHRASING", phrases}
};

inline constexpr Lane trackLanes[] {
    {"DRUMS", drums}, {"BASS", bass}, {"SYNTH", synth}, {"VOCAL", vocals}, {"AUDIO", audio}
};

inline constexpr ArrangementView arrangement {bars, globalLanes, trackLanes};
}
