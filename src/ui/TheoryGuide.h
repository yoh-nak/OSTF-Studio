#pragma once

#include "ui/OstfpPreview.h"
#include <set>

namespace ostfstudio {
// Derived display data. Tick intervals are half-open; pitch masks are absolute pitch classes.
class TheoryGuide {
public:
    using Tick = juce::int64;
    struct Scale {
        juce::String name;
        int mask = 0, tonic = -1;
        struct Target { juce::String type, id; };
        std::vector<Target> targets;
    };
    struct Cell {
        Tick start, end;
        int chord = -1, key = -1;
        std::vector<int> scales;
        std::vector<juce::String> sections;
    };
    struct Chord { juce::String id, name; int root, intervals, mask, extensions = 0; };
    struct Function { Tick start, end; int chord, key; juce::String degree, function; };
    struct Progression { Tick start, end; juce::String label; };
    struct Fit {
        double inside = 0, total = 0;
        void add(bool matches, Tick ticks) { total += double(ticks); if (matches) inside += double(ticks); }
        int percent() const { return total == 0 ? -1 : juce::roundToInt(100 * inside / total); }
    };
    struct ClipFit { Fit chord, scale; };
    struct RegionFits { std::vector<Fit> chords, scales; };
    using ChordFits = std::map<std::size_t, ClipFit>; // First analysis cell in each chord interval.
    using ClipFits = std::map<juce::String, ChordFits>;
    struct TrackCell {
        Fit chord, scale;
        std::size_t chordBegin = 0, chordEnd = 0;
        int backgroundScale = -1;
        std::vector<int> noteScoped;
    };
    std::vector<Chord> chords;
    std::vector<Scale> scales;
    std::vector<Cell> cells;
    std::vector<Function> functions;
    std::vector<Progression> progressions;
    std::vector<juce::String> trackIds;

    TheoryGuide(const juce::var& score, Tick end);
    static bool contains(int mask, int pitch) { return (mask & (1 << (pitch % 12))) != 0; }
    static int chordToneMask(const juce::var& chord);
    std::size_t firstCell(double tick) const {
        return static_cast<std::size_t>(std::partition_point(cells.begin(), cells.end(),
            [tick](const Cell& cell) { return double(cell.end) <= tick; }) - cells.begin());
    }
    std::vector<TrackCell> analyse(std::size_t track, const std::vector<PitchedNote>& notes, int reference = -1,
                                   ClipFits* clips = nullptr, RegionFits* regions = nullptr) const;
    int scaleForNote(const TrackCell&, const PitchedNote&) const;
    enum class Tone { unknown, root, guide, fifth, chord, tension, avoid, scale, outside };
    static Tone tone(int pitch, const Chord* chord, int scaleMask);
    static const char* interval(int pitch, int root);
    int chordMask(const Cell& c) const { return c.chord < 0 ? 0 : chords[static_cast<std::size_t>(c.chord)].mask; }
private:
    struct Key { int root; juce::String mode; };
    std::vector<Key> keys;
    int songScale = -1;
    int timedScaleCount = 0;
    std::vector<std::set<juce::String>> disabledClips;
    void buildFunctions();
};
}
