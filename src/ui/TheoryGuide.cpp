#include "ui/TheoryGuide.h"
#include <algorithm>
#include <map>

namespace ostfstudio {
namespace {
int pitchClass(const juce::var& value) {
    auto step = value["step"].toString();
    constexpr int semitones[] {0, 2, 4, 5, 7, 9, 11};
    const int index = juce::String("CDEFGAB").indexOf(step);
    if (step.length() != 1 || index < 0) return -1;
    const int alter = static_cast<int>(value["alter"]) % 12;
    return (semitones[index] + alter + 12) % 12;
}
int pitchMask(const juce::var& value, const char* rootField) {
    const int root = pitchClass(value[rootField]);
    const auto intervals = value["intervals"];
    int mask = 0;
    if (root >= 0 && intervals.isArray())
        for (const auto& interval : *intervals.getArray())
            mask |= 1 << ((root + static_cast<int>(interval) % 12 + 12) % 12);
    return mask;
}
template<class Visitor> void each(const juce::var& items, Visitor visit) {
    if (items.isArray()) for (const auto& item : *items.getArray()) visit(item);
}
int combine(int a, int b) { return a == -1 || a == b ? b : -2; }
}

int TheoryGuide::chordToneMask(const juce::var& chord) {
    const int tones = pitchMask(chord, "root"), bass = pitchClass(chord["bass"]);
    return tones && bass >= 0 ? tones | (1 << bass) : tones;
}

TheoryGuide::TheoryGuide(const juce::var& score, Tick end) {
    enum Kind { chord, scale, section };
    struct Edge { Kind kind; int index; bool start; };
    std::map<Tick, std::vector<Edge>> edges;
    edges[0]; edges[end];
    auto span = [&](const juce::var& event, Kind kind, int index) {
        const auto start = static_cast<Tick>(event["start"]), length = static_cast<Tick>(event["duration"]);
        if (start < 0 || start >= end || length <= 0) return;
        edges[start].push_back({kind, index, true});
        edges[start + std::min(length, end - start)].push_back({kind, index, false});
    };
    const auto harmony = score["harmony"], timeline = score["timeline"], song = score["song"];
    each(harmony["chords"], [&](const auto& entry) {
        span(entry, chord, static_cast<int>(chords.size()));
        const auto intervals = pitchMask(entry, "root");
        const auto mask = chordToneMask(entry);
        int extensions = 0;
        const int root = pitchClass(entry["root"]);
        each(entry["intervals"], [&](const auto& interval) {
            if (root >= 0 && int(interval) >= 12) extensions |= 1 << ((root + int(interval) % 12) % 12);
        });
        chords.push_back({entry["id"].toString(), entry["label"].toString(), pitchClass(entry["root"]), intervals, mask, extensions});
    });
    std::vector<juce::String> sectionIds;
    each(timeline["sections"], [&](const auto& entry) {
        span(entry, section, static_cast<int>(sectionIds.size()));
        sectionIds.push_back(entry["id"].toString());
    });
    auto addScale = [&](const juce::var& definition, const juce::var& range, const juce::String& track) {
        Scale s {definition.getProperty("label", definition["id"]).toString(), pitchMask(definition, "tonic"), pitchClass(definition["tonic"])};
        if (track.isNotEmpty()) {
            s.name = track + " / " + (s.name.isNotEmpty() ? s.name : range["id"].toString());
            s.targets.push_back({"track", track});
        }
        each(definition["targets"], [&](const auto& target) {
            s.targets.push_back({target["type"].toString(), target["id"].toString()});
        });
        const auto index = static_cast<int>(scales.size());
        scales.push_back(std::move(s));
        if (range.isObject()) span(range, scale, index);
        return index;
    };
    if (song["scale"].isObject()) songScale = addScale(song["scale"], {}, {});
    each(harmony["scales"], [&](const auto& entry) {
        const auto start = static_cast<Tick>(entry["start"]), length = static_cast<Tick>(entry["duration"]);
        if (start < 0 || start >= end || length <= 0 || length > end - start) return;
        addScale(entry, entry, {});
        ++timedScaleCount;
    });
    each(score["tracks"], [&](const auto& track) {
        const auto id = track["id"].toString();
        trackIds.push_back(id);
        auto& disabled = disabledClips.emplace_back();
        each(track["clips"], [&](const auto& clip) {
            if (!bool(clip.getProperty("enabled", true))) disabled.insert(clip["id"].toString());
        });
        each(track["directions"], [&](const auto& dir) {
            const auto source = dir["parameters"]["pitchSource"];
            if (dir["type"].toString() == "improvisation" && source["type"].toString() == "explicitScale")
                addScale(source["scale"], dir, id);
        });
    });
    std::map<Tick, int> keyAt;
    auto addKey = [&](Tick at, const juce::var& k) {
        if (at < 0 || at >= end) return;
        edges[at];
        keyAt[at] = static_cast<int>(keys.size());
        keys.push_back({k["state"].toString() == "tonal" ? pitchClass(k["tonic"]) : -1, k["mode"].toString()});
    };
    addKey(0, song["key"]);
    each(timeline["keyChanges"], [&](const auto& e) { addKey(static_cast<Tick>(e["at"]), e["key"]); });
    std::array<std::set<int>, 3> active;
    int key = -1;
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        for (auto edge : it->second) {
            if (edge.start) active[edge.kind].insert(edge.index);
            else active[edge.kind].erase(edge.index);
        }
        if (auto k = keyAt.find(it->first); k != keyAt.end()) key = k->second;
        const auto next = std::next(it);
        if (next == edges.end()) break;
        Cell c {it->first, next->first, active[chord].size() == 1 ? *active[chord].begin() : -1, key};
        c.scales.assign(active[scale].begin(), active[scale].end());
        for (int i : active[section]) c.sections.push_back(sectionIds[i]);
        cells.push_back(std::move(c));
    }
    buildFunctions();
}

void TheoryGuide::buildFunctions() {
    for (const auto& c : cells) {
        if (c.chord < 0) continue;
        if (!functions.empty() && functions.back().end == c.start && functions.back().chord == c.chord && functions.back().key == c.key) {
            functions.back().end = c.end;
            continue;
        }
        auto& f = functions.emplace_back(Function{c.start, c.end, c.chord, c.key, {}, {}});
        if (c.key < 0) continue;
        const auto& k = keys[c.key];
        const auto& h = chords[c.chord];
        if (k.root < 0 || h.root < 0 || (k.mode != "major" && k.mode != "minor")) continue;
        const bool minor = k.mode == "minor";
        const auto degree = (h.root + 12 - k.root) % 12;
        const auto has = [&](int interval) { return contains(h.intervals & ~h.extensions, h.root + interval); };
        const bool maj = has(4) && !has(3) && has(7), min = has(3) && !has(4) && has(7);
        if (degree == 0 && (minor ? min : maj)) { f.degree = minor ? "i" : "I"; f.function = "Tonic"; }
        else if (degree == 2 && ((!minor && min) || (minor && has(3) && has(6) && !has(7)))) {
            f.degree = minor ? "ii" + juce::String::charToString(has(10) ? 0x00f8 : 0x00b0) : juce::String("ii");
            f.function = "Subdominant";
        } else if (degree == 5 && (minor ? min : maj)) { f.degree = minor ? "iv" : "IV"; f.function = "Subdominant"; }
        else if (degree == 7 && maj) { f.degree = "V"; f.function = "Dominant"; }
        else if (!minor && min && (degree == 4 || degree == 9)) {
            f.degree = degree == 4 ? "iii" : "vi";
            f.function = "Tonic substitute";
        }
        else if (minor && maj && (degree == 3 || degree == 8 || degree == 10)) {
            f.degree = degree == 3 ? "III" : degree == 8 ? "VI" : "VII";
            f.function = "Diatonic";
        }
    }
    // An actual adjacent target supplies the evidence for an applied dominant.
    for (std::size_t i = 1; i < functions.size(); ++i) {
        auto& from = functions[i - 1];
        const auto& to = functions[i];
        const auto& h = chords[from.chord];
        if (from.key == to.key && from.end == to.start && (to.degree == "I" || to.degree == "i")
            && h.root >= 0 && (h.root - chords[to.chord].root + 12) % 12 == 1
            && contains(h.intervals, h.root + 4) && contains(h.intervals, h.root + 10)
            && !contains(h.intervals, h.root + 11)) {
            from.degree = "subV";
            from.function = "Dominant substitute";
        }
        if (from.key != to.key || from.end != to.start || h.root < 0 || to.degree.isEmpty()
            || to.degree == "I" || to.degree == "i" || to.degree.contains("/") || from.degree == "V"
            || (h.root - chords[to.chord].root + 12) % 12 != 7) continue;
        const auto has = [&](int n) { return contains(h.intervals, h.root + n); };
        if (has(4) && has(7) && has(10) && !has(11)) {
            from.degree = "V/" + to.degree;
            from.function = "Secondary dominant";
        }
    }
    auto addProgression = [&](Tick start, Tick end, const juce::String& name) {
        // A resolution can immediately begin the next cadence. Give that beat to the new phrase.
        if (!progressions.empty() && progressions.back().end > start) {
            if (progressions.back().start >= start) progressions.pop_back();
            else progressions.back().end = start;
        }
        progressions.push_back({start, end, name});
    };
    for (std::size_t i = 1; i < functions.size(); ++i) {
        const auto& v = functions[i-1]; const auto& tonic = functions[i];
        if (v.function == "Secondary dominant" && v.end == tonic.start && v.key == tonic.key) {
            juce::String label = "V/" + tonic.degree;
            Tick start = v.start;
            if (i >= 2) {
                const auto& pre = functions[i-2];
                const auto& h = chords[pre.chord];
                const int targetRoot = chords[tonic.chord].root;
                const int structural = h.intervals & ~h.extensions;
                if (pre.key == v.key && pre.end == v.start && h.root >= 0 && (h.root + 12 - targetRoot) % 12 == 2
                    && contains(structural, h.root + 3) && !contains(structural, h.root + 4)
                    && (contains(structural, h.root + 6) || contains(structural, h.root + 7))) {
                    start = pre.start;
                    label = "(ii-V)/" + tonic.degree;
                }
            }
            addProgression(start, tonic.end, label);
            continue;
        }
        if (v.end != tonic.start || v.key != tonic.key) continue;
        if ((v.degree.equalsIgnoreCase("IV") && tonic.degree.equalsIgnoreCase("I"))
            || (v.degree == "V" && tonic.degree.equalsIgnoreCase("VI"))) {
            addProgression(v.start, tonic.end, v.degree + "-" + tonic.degree);
            continue;
        }
        if ((v.degree != "V" && v.degree != "subV") || (tonic.degree != "I" && tonic.degree != "i") || v.end != tonic.start || v.key != tonic.key) continue;
        if (i >= 2) {
            const auto& ii = functions[i-2];
            if ((ii.degree.startsWith("ii") || ii.degree.equalsIgnoreCase("IV")) && ii.end == v.start && ii.key == v.key) {
                auto start = ii.start;
                auto label = ii.degree + "-" + v.degree + "-" + tonic.degree;
                if (i >= 4 && ii.degree == "ii") {
                    const auto& opening = functions[i-4];
                    const auto& sixth = functions[i-3];
                    if ((opening.degree == "I" || opening.degree == "iii") && sixth.degree == "vi"
                        && opening.key == ii.key && sixth.key == ii.key
                        && opening.end == sixth.start && sixth.end == ii.start) {
                        start = opening.start;
                        label = opening.degree + "-vi-" + label;
                    }
                }
                addProgression(start, tonic.end, label);
                continue;
            }
        }
        addProgression(v.start, tonic.end, v.degree + "-" + tonic.degree);
    }
}

const char* TheoryGuide::interval(int pitch, int root) {
    static constexpr const char* names[] {"1", "b2", "2", "b3", "3", "4", "#4/b5", "5", "b6", "6", "b7", "7"};
    return root < 0 ? "" : names[(pitch - root + 12) % 12];
}

TheoryGuide::Tone TheoryGuide::tone(int pitch, const Chord* chord, int scaleMask) {
    if (chord && contains(chord->mask, pitch)) {
        const auto degree = (pitch - chord->root + 12) % 12;
        if (degree == 0) return Tone::root;
        if (contains(chord->extensions, pitch)) return Tone::tension;
        if (degree == 7) return Tone::fifth;
        // A #9 can coexist with the major third. It is a chord colour, not a minor third.
        if ((degree == 3 && !contains(chord->intervals, chord->root + 4)) || degree == 4
            || degree == 10 || degree == 11) return Tone::guide;
        return Tone::chord;
    }
    if (contains(scaleMask, pitch)) {
        if (!chord || !chord->intervals) return Tone::scale;
        // A sustained minor ninth above a structural chord tone is an avoid candidate.
        // Explicit extensions and suspended chord members were handled above.
        const int structural = chord->intervals & ~chord->extensions;
        return contains(structural, pitch + 11) ? Tone::avoid : Tone::tension;
    }
    return (chord && chord->mask) || scaleMask ? Tone::outside : Tone::unknown;
}

int TheoryGuide::scaleForNote(const TrackCell& cell, const PitchedNote& note) const {
    int index = cell.backgroundScale;
    for (auto i : cell.noteScoped)
        for (const auto& target : scales[i].targets)
            if ((target.type == "note" && target.id == note.id) || (target.type == "clip" && target.id == note.clipId)) {
                index = combine(index == songScale ? -1 : index, i);
                break;
            }
    return index;
}

std::vector<TheoryGuide::TrackCell> TheoryGuide::analyse(std::size_t track, const std::vector<PitchedNote>& notes,
                                                        int reference, ClipFits* clips, RegionFits* regions) const {
    if (clips) clips->clear();
    if (regions) {
        regions->chords.assign(chords.size(), {});
        regions->scales.assign(timedScaleCount ? timedScaleCount : int(songScale >= 0), {});
    }
    std::vector<TrackCell> result(cells.size());
    const auto id = track < trackIds.size() ? trackIds[track] : juce::String{};
    std::set<juce::String> noteIds, clipIds;
    const bool scoped = std::any_of(scales.begin(), scales.end(), [](const auto& scale) {
        return std::any_of(scale.targets.begin(), scale.targets.end(), [](const auto& t) { return t.type == "clip" || t.type == "note"; });
    });
    if (scoped) for (const auto& note : notes) {
        noteIds.insert(note.id);
        clipIds.insert(note.clipId);
    }
    std::size_t chordBegin = 0;
    for (std::size_t j = 0; j < cells.size(); ++j) {
        const auto& c = cells[j];
        auto& dest = result[j];
        if (j && cells[j - 1].chord != c.chord) chordBegin = j;
        dest.chordBegin = chordBegin;
        for (auto i : c.scales) {
            if (reference >= 0 && reference != i) continue;
            const auto& targets = scales[i].targets;
            bool whole = targets.empty(), individual = false;
            for (const auto& t : targets) {
                whole |= (t.type == "track" && t.id == id)
                      || (t.type == "chord" && c.chord >= 0 && t.id == chords[c.chord].id)
                      || (t.type == "section" && std::find(c.sections.begin(), c.sections.end(), t.id) != c.sections.end());
                individual |= (t.type == "note" && noteIds.contains(t.id)) || (t.type == "clip" && clipIds.contains(t.id));
            }
            if (whole) dest.backgroundScale = combine(dest.backgroundScale, i);
            else if (individual) dest.noteScoped.push_back(i);
        }
        if (dest.backgroundScale == -1 && (reference < 0 || reference == songScale)) dest.backgroundScale = songScale;
    }
    for (const auto& n : notes) {
        if (!n.enabled || n.fixedTrigger || n.pitch < 0 || n.pitch > 127
            || (track < disabledClips.size() && disabledClips[track].contains(n.clipId))) continue;
        auto* sourceClip = clips && n.clipId.isNotEmpty() ? &(*clips)[n.clipId] : nullptr;
        for (auto i = firstCell(double(n.startTick)); i < cells.size() && cells[i].start < n.endTick; ++i) {
            const auto& c = cells[i];
            auto* intervals = sourceClip;
            if (!intervals && clips && track >= trackIds.size() && c.chord >= 0)
                intervals = &(*clips)[chords[size_t(c.chord)].id];
            auto* clip = intervals ? &(*intervals)[result[i].chordBegin] : nullptr;
            const auto length = std::min(n.endTick, c.end) - std::max(n.startTick, c.start);
            if (const int mask = chordMask(c)) {
                const bool inside = contains(mask, n.pitch);
                result[i].chord.add(inside, length);
                if (regions) regions->chords[size_t(c.chord)].add(inside, length);
                if (clip) clip->chord.add(inside, length);
            }
            const auto scale = scaleForNote(result[i], n);
            if (regions) {
                // Each scale on the global lane is evaluated in its own right, including overlapping events.
                auto addScaleFit = [&](int index, int region) {
                    const auto& s = scales[size_t(index)];
                    if (!s.mask) return;
                    const bool applies = s.targets.empty() || std::any_of(s.targets.begin(), s.targets.end(), [&](const auto& target) {
                        return (target.type == "track" && target.id == id) || (target.type == "clip" && target.id == n.clipId)
                            || (target.type == "note" && target.id == n.id)
                            || (target.type == "chord" && c.chord >= 0 && target.id == chords[size_t(c.chord)].id)
                            || (target.type == "section" && std::find(c.sections.begin(), c.sections.end(), target.id) != c.sections.end());
                    });
                    if (applies) regions->scales[size_t(region)].add(contains(s.mask, n.pitch), length);
                };
                if (timedScaleCount == 0 && songScale >= 0) addScaleFit(songScale, 0);
                for (int index : c.scales) {
                    const int region = index - int(songScale >= 0);
                    if (juce::isPositiveAndBelow(region, timedScaleCount)) addScaleFit(index, region);
                }
            }
            if (scale >= 0 && scales[scale].mask) {
                const bool inside = contains(scales[scale].mask, n.pitch);
                result[i].scale.add(inside, length);
                if (clip) clip->scale.add(inside, length);
            }
        }
    }
    // A scale or key boundary can split a cell inside a chord. The chord lane
    // always reports the complete chord's denominator, independently of that split.
    for (std::size_t first = 0; first < cells.size();) {
        auto end = first + 1;
        Fit total = result[first].chord;
        while (end < cells.size() && cells[end].chord == cells[first].chord) {
            total.inside += result[end].chord.inside;
            total.total += result[end].chord.total;
            ++end;
        }
        for (auto i = first; i < end; ++i) {
            result[i].chord = total;
            result[i].chordEnd = end;
        }
        first = end;
    }
    return result;
}
}
