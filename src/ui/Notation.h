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
    juce::String noteId;
};
struct ScorePart {
    juce::String id, name;
    StaffStyle preferred = StaffStyle::treble;
    std::vector<PitchedNote> notes;
    int midiChannel = 0;
};
struct SheetMark {
    juce::int64 at, end;
    juce::String text, voice, pronunciation, trackId, noteId;
};
enum class LyricDisplay { text, pronunciation, both };
struct SheetOptions {
    int barsPerLine = 4;
    juce::String lyricVoice;
    bool showChords = true, showLyrics = true, showScales = false;
    LyricDisplay lyricDisplay = LyricDisplay::text;
    int marginMm = 12;
    std::vector<int> pageBreakBars; // One-based first bars of explicitly chosen pages.
    bool showSections = true, showBarNumbers = true;
};
struct NotationScore {
    juce::String title, key;
    int bars = 0, beats = 4, beatType = 4;
    juce::int64 ppq = 0, barTicks = 0;
    std::vector<SheetMark> sections, chords, scales, lyrics, tempos, keys;
    std::vector<OstfpPreview::DisplayPhrase> displayPhrases;
    juce::String primaryDocument;
    std::map<juce::String, juce::String> voiceDocuments;
    std::vector<ScorePart> parts;
    std::vector<MeterPoint> meters;
    juce::StringArray unalignedPhrases;
};
using MeasureNotes = std::vector<std::vector<ScoreNote>>;
NotationScore notationScore(const OstfpPreview&);
MeasureNotes partMeasures(const NotationScore&, int part, StaffStyle);
int barsPerPage(bool lead, StaffStyle, const SheetOptions&);
std::vector<int> sheetPageStarts(const NotationScore&, bool lead, StaffStyle, const SheetOptions&);
void drawSheetPage(juce::Graphics&, const NotationScore&, int part, StaffStyle, const MeasureNotes&, bool lead,
                   int page, const SheetOptions&);
int sheetPages(const NotationScore&, bool lead, StaffStyle, const SheetOptions& = {});
void drawReadingGrid(juce::Graphics&, juce::Rectangle<int>, const NotationScore&, StaffStyle, const MeasureNotes&,
                     bool lead, int firstBar, int columns, int rows, const SheetOptions&, juce::int64 playingTick);
juce::Result writeSheetPdf(juce::OutputStream&, const NotationScore&, int part, StaffStyle, const MeasureNotes&,
                           bool lead, const SheetOptions& = {});
juce::Result writeSongXml(juce::OutputStream&, const NotationScore&);
juce::Result writeChordPro(juce::OutputStream&, const NotationScore&);
juce::Result writePartXml(juce::OutputStream&, const NotationScore&, int part, StaffStyle, const MeasureNotes&);
} // namespace ostfstudio
