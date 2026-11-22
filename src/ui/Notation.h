#pragma once
#include "ui/OstfpPreview.h"
#include <juce_graphics/juce_graphics.h>

namespace ostfstudio {
enum class StaffStyle { treble = 1, alto, bass, grand, guitarTab, bassTab, percussion };
const char* staffName(StaffStyle);
struct ScoreNote {
    juce::int64 at, length;
    int pitch, voice, string = 0, fret = -1;
    bool tiedFrom, tiedTo;
};
struct ScorePart {
    juce::String id, name;
    StaffStyle preferred = StaffStyle::treble;
    std::vector<PitchedNote> notes;
};
struct SheetMark {
    juce::int64 at, end;
    juce::String text, voice;
};
struct SheetOptions {
    int barsPerLine = 4;
    juce::String lyricVoice;
};
struct NotationScore {
    juce::String title, key;
    int bars = 0, beats = 4, beatType = 4;
    juce::int64 ppq = 0, barTicks = 0;
    std::vector<SheetMark> sections, chords, lyrics, tempos;
    std::vector<ScorePart> parts;
};
using MeasureNotes = std::vector<std::vector<ScoreNote>>;
NotationScore notationScore(const OstfpPreview&);
MeasureNotes partMeasures(const NotationScore&, int part, StaffStyle);
int barsPerPage(bool lead, StaffStyle, const SheetOptions&);
int sheetPages(const NotationScore&, bool lead, StaffStyle, const SheetOptions& = {});
void drawSheetPage(juce::Graphics&, const NotationScore&, int part, StaffStyle, const MeasureNotes&, bool lead,
                   int page, const SheetOptions& = {}, juce::int64 playingTick = -1);
juce::Result writeSheetPdf(juce::OutputStream&, const NotationScore&, int part, StaffStyle, const MeasureNotes&,
                           bool lead, const SheetOptions& = {});
juce::Result writePartXml(juce::OutputStream&, const NotationScore&, int part, StaffStyle, const MeasureNotes&);
} // namespace ostfstudio
