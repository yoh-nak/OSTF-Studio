#include "ui/Notation.h"
#include <algorithm>
#include <map>

namespace ostfstudio {
using Tick = juce::int64;
namespace {
constexpr const char* steps[]{"C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"};
constexpr int degree[]{0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
constexpr int alter[]{0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
const std::vector<int>& tuning(StaffStyle style) {
    static const std::vector<int> guitar{64, 59, 55, 50, 45, 40}, bass{43, 38, 33, 28}, none;
    return style == StaffStyle::guitarTab ? guitar : style == StaffStyle::bassTab ? bass : none;
}
int octave(int pitch) { return pitch / 12 - 1; }
int diatonic(int pitch) { return 7 * octave(pitch) + degree[pitch % 12]; }
// Common drum-kit positions expressed as treble-staff display pitches.
int drumPosition(int pitch) {
    switch (pitch) {
    case 35:
    case 36:
        return 65;
    case 38:
    case 39:
    case 40:
        return 72;
    case 41:
    case 43:
        return 69;
    case 45:
    case 47:
        return 74;
    case 48:
    case 50:
        return 76;
    case 44:
        return 62;
    case 42:
    case 46:
        return 79;
    case 49:
    case 52:
    case 55:
    case 57:
        return 81;
    default:
        return 77;
    }
}
bool crossHead(int pitch) { return pitch == 42 || pitch == 44 || pitch == 46 || (pitch >= 49 && pitch != 50); }
const juce::Array<juce::var>& items(const juce::var& value) {
    static const juce::Array<juce::var> empty;
    return value.isArray() ? *value.getArray() : empty;
}
} // namespace

const char* staffName(StaffStyle s) {
    switch (s) {
    case StaffStyle::treble:
        return "Treble clef";
    case StaffStyle::alto:
        return "Alto clef";
    case StaffStyle::bass:
        return "Bass clef";
    case StaffStyle::grand:
        return "Grand staff";
    case StaffStyle::guitarTab:
        return "Guitar TAB";
    case StaffStyle::bassTab:
        return "Bass TAB";
    case StaffStyle::percussion:
        return "Drum staff";
    }
    return "";
}
NotationScore notationScore(const OstfpPreview& project) {
    NotationScore s;
    s.title = project.title;
    s.ppq = project.ppq;
    s.barTicks = project.ticksPerBar;
    s.bars = project.bars.endBar - project.bars.firstBar;
    const auto doc = juce::JSON::parse(project.sourceScore.toString());
    const auto timeline = doc["timeline"];
    const auto meter = timeline["meterChanges"][0];
    s.beats = static_cast<int>(meter["numerator"]);
    s.beatType = static_cast<int>(meter["denominator"]);
    s.key = doc["song"]["key"]["label"].toString();
    auto marks = [](const juce::var& list, const char* label) {
        std::vector<SheetMark> result;
        for (const auto& item : items(list)) {
            if (item["start"].isVoid())
                continue;
            const Tick start = item["start"];
            result.push_back({start, start + static_cast<Tick>(item["duration"]), item[label].toString()});
        }
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
        return result;
    };
    s.sections = marks(timeline["sections"], "name");
    s.chords = marks(doc["harmony"]["chords"], "label");
    std::map<juce::String, std::pair<Tick, Tick>> noteTimes;
    for (const auto& track : items(doc["tracks"]))
        for (const auto& clip : items(track["clips"]))
            for (const auto& n : items(clip["notes"])) {
                const Tick start = static_cast<Tick>(clip["start"]) + static_cast<Tick>(n["start"]);
                noteTimes[n["id"].toString()] = {start, start + static_cast<Tick>(n["duration"])};
            }
    std::map<juce::String, SheetMark> alignments;
    for (const auto& a : items(doc["lyrics"]["alignments"]))
        for (const auto& id : items(a["unitIds"])) {
            auto& value =
                alignments.try_emplace(id.toString(), SheetMark{-1, 0, {}, a["voiceId"].toString()}).first->second;
            for (const auto& note : items(a["noteIds"])) {
                const auto it = noteTimes.find(note.toString());
                if (it == noteTimes.end())
                    continue;
                value.at = value.at < 0 ? it->second.first : std::min(value.at, it->second.first);
                value.end = std::max(value.end, it->second.second);
            }
        }
    for (const auto& unit : items(doc["lyrics"]["units"])) {
        const auto it = alignments.find(unit["id"].toString());
        auto mark = it == alignments.end() ? SheetMark{-1, 0, {}, {}} : it->second;
        if (!unit["start"].isVoid()) {
            mark.at = static_cast<Tick>(unit["start"]);
            mark.end = mark.at + static_cast<Tick>(unit["duration"]);
        }
        mark.text = unit["text"].toString();
        if (mark.at >= 0 && mark.text.isNotEmpty())
            s.lyrics.push_back(std::move(mark));
    }
    std::stable_sort(s.lyrics.begin(), s.lyrics.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
    for (const auto& tempo : items(timeline["tempoChanges"]))
        s.tempos.push_back({static_cast<Tick>(tempo["at"]), 0, tempo["bpm"].toString()});
    for (std::size_t i = 0; i < project.trackLanes.size(); ++i) {
        if (project.trackKinds[i] == TrackKind::audio)
            continue;
        ScorePart p;
        p.id = i < project.sourceTrackCount() ? doc["tracks"][static_cast<int>(i)]["id"].toString() : "/chord-voicing";
        p.name = juce::String::fromUTF8(project.trackLanes[i].label);
        const bool drum = project.trackKinds[i] == TrackKind::drum;
        if (drum)
            p.preferred = StaffStyle::percussion;
        else if (p.name.containsIgnoreCase("bass"))
            p.preferred = StaffStyle::bass;
        else if (p.name.containsIgnoreCase("piano") || i == project.chordVoicingTrack)
            p.preferred = StaffStyle::grand;
        p.notes = project.pitchedNotes[i];
        const auto& drums = project.drumNotes[i];
        p.notes.insert(p.notes.end(), drums.begin(), drums.end());
        std::erase_if(p.notes, [](const auto& n) { return !n.enabled || n.endTick <= n.startTick; });
        std::stable_sort(p.notes.begin(), p.notes.end(), [](const auto& a, const auto& b) {
            return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch > b.pitch;
        });
        s.parts.push_back(std::move(p));
    }
    return s;
}

MeasureNotes partMeasures(const NotationScore& s, int index, StaffStyle style) {
    MeasureNotes result(s.bars);
    if (index < 0 || index >= static_cast<int>(s.parts.size()) || s.barTicks <= 0)
        return result;
    std::vector<Tick> voices;
    const auto& strings = tuning(style);
    std::vector<Tick> busy(strings.size(), 0);
    for (const auto& n : s.parts[index].notes) {
        auto v = std::find_if(voices.begin(), voices.end(), [&](Tick end) { return end <= n.startTick; });
        const int voice = static_cast<int>(v - voices.begin()) + 1;
        if (v == voices.end())
            voices.push_back(n.endTick);
        else
            *v = n.endTick;
        int string = 0, fret = -1;
        for (std::size_t i = 0; i < strings.size(); ++i) {
            const int candidate = n.pitch - strings[i];
            if (busy[i] <= n.startTick && candidate >= 0 && candidate <= 24) {
                string = static_cast<int>(i + 1);
                fret = candidate;
                busy[i] = n.endTick;
                break;
            }
        }
        for (auto pos = n.startTick; pos < n.endTick && pos < s.barTicks * s.bars;) {
            const int bar = static_cast<int>(pos / s.barTicks);
            const auto end = std::min(n.endTick, (bar + 1) * s.barTicks);
            result[bar].push_back(
                {pos % s.barTicks, end - pos, n.pitch, voice, string, fret, pos != n.startTick, end != n.endTick});
            pos = end;
        }
    }
    return result;
}

namespace {
juce::XmlElement* field(juce::XmlElement& e, const char* tag, const juce::String& value) {
    auto* child = e.createNewChildElement(tag);
    child->addTextElement(value);
    return child;
}
void clef(juce::XmlElement& attr, const char* sign, int line, int number = 0) {
    auto* el = attr.createNewChildElement("clef");
    if (number)
        el->setAttribute("number", number);
    field(*el, "sign", sign);
    field(*el, "line", juce::String(line));
}
void rest(juce::XmlElement& measure, Tick ticks, int voice) {
    if (ticks <= 0)
        return;
    auto* n = measure.createNewChildElement("note");
    n->createNewChildElement("rest");
    field(*n, "duration", juce::String(ticks));
    field(*n, "voice", juce::String(voice));
}
} // namespace
juce::Result writePartXml(juce::OutputStream& stream, const NotationScore& s, int index, StaffStyle style,
                          const MeasureNotes& bars) {
    if (index < 0 || index >= static_cast<int>(s.parts.size()))
        return juce::Result::fail("Select a note track");
    juce::XmlElement root("score-partwise");
    root.setAttribute("version", "4.0");
    field(*root.createNewChildElement("work"), "work-title", s.title);
    auto* desc = root.createNewChildElement("part-list")->createNewChildElement("score-part");
    desc->setAttribute("id", "P1");
    field(*desc, "part-name", s.parts[index].name);
    const bool drum = style == StaffStyle::percussion;
    std::map<int, juce::String> ids;
    if (drum) {
        for (const auto& n : s.parts[index].notes)
            ids[n.pitch] = "I" + juce::String(n.pitch);
        for (const auto& [pitch, id] : ids) {
            auto* inst = desc->createNewChildElement("score-instrument");
            inst->setAttribute("id", id);
            field(*inst, "instrument-name", "GM " + juce::String(pitch));
        }
        for (const auto& [pitch, id] : ids) {
            auto* midi = desc->createNewChildElement("midi-instrument");
            midi->setAttribute("id", id);
            field(*midi, "midi-channel", "10");
            field(*midi, "midi-unpitched", juce::String(pitch + 1));
        }
    }
    auto* part = root.createNewChildElement("part");
    part->setAttribute("id", "P1");
    for (int b = 0; b < static_cast<int>(bars.size()); ++b) {
        auto* m = part->createNewChildElement("measure");
        m->setAttribute("number", b + 1);
        if (b == 0) {
            auto* a = m->createNewChildElement("attributes");
            field(*a, "divisions", juce::String(s.ppq));
            auto* time = a->createNewChildElement("time");
            field(*time, "beats", juce::String(s.beats));
            field(*time, "beat-type", juce::String(s.beatType));
            const auto& strings = tuning(style);
            if (style == StaffStyle::grand) {
                field(*a, "staves", "2");
                clef(*a, "G", 2, 1);
                clef(*a, "F", 4, 2);
            } else if (!strings.empty()) {
                clef(*a, "TAB", static_cast<int>(strings.size()) - 1);
                auto* details = a->createNewChildElement("staff-details");
                field(*details, "staff-lines", juce::String(static_cast<int>(strings.size())));
                for (int i = 0; i < static_cast<int>(strings.size()); ++i) {
                    auto* t = details->createNewChildElement("staff-tuning");
                    t->setAttribute("line", i + 1);
                    const int p = strings[strings.size() - 1 - i];
                    field(*t, "tuning-step", steps[p % 12]);
                    field(*t, "tuning-octave", juce::String(octave(p)));
                }
            } else if (drum)
                clef(*a, "percussion", 3);
            else if (style == StaffStyle::alto)
                clef(*a, "C", 3);
            else if (style == StaffStyle::bass)
                clef(*a, "F", 4);
            else
                clef(*a, "G", 2);
        }
        std::map<int, std::vector<ScoreNote>> voices;
        const Tick measureStart = b * s.barTicks;
        for (const auto& tempo : s.tempos)
            if (tempo.at >= measureStart && tempo.at < measureStart + s.barTicks) {
                auto* direction = m->createNewChildElement("direction");
                auto* mark = direction->createNewChildElement("direction-type")->createNewChildElement("metronome");
                field(*mark, "beat-unit", "quarter");
                field(*mark, "per-minute", tempo.text);
                if (tempo.at != measureStart)
                    field(*direction, "offset", juce::String(tempo.at - measureStart));
                direction->createNewChildElement("sound")->setAttribute("tempo", tempo.text);
            }
        for (const auto& section : s.sections)
            if (section.at == measureStart) {
                auto* direction = m->createNewChildElement("direction");
                field(*direction->createNewChildElement("direction-type"), "rehearsal", section.text);
            }
        for (const auto& n : bars[b])
            voices[n.voice].push_back(n);
        if (voices.empty())
            rest(*m, s.barTicks, 1);
        bool rewind = false;
        for (const auto& [voice, events] : voices) {
            if (rewind)
                field(*m->createNewChildElement("backup"), "duration", juce::String(s.barTicks));
            Tick at = 0;
            for (const auto& e : events) {
                rest(*m, e.at - at, voice);
                auto* n = m->createNewChildElement("note");
                const int p = drum ? drumPosition(e.pitch) : e.pitch;
                auto* tone = n->createNewChildElement(drum ? "unpitched" : "pitch");
                field(*tone, drum ? "display-step" : "step", steps[p % 12]);
                if (!drum && alter[p % 12])
                    field(*tone, "alter", "1");
                field(*tone, drum ? "display-octave" : "octave", juce::String(octave(p)));
                field(*n, "duration", juce::String(e.length));
                auto ties = [&](juce::XmlElement& parent, const char* tag) {
                    if (e.tiedFrom)
                        parent.createNewChildElement(tag)->setAttribute("type", "stop");
                    if (e.tiedTo)
                        parent.createNewChildElement(tag)->setAttribute("type", "start");
                };
                ties(*n, "tie");
                if (drum)
                    n->createNewChildElement("instrument")->setAttribute("id", ids[e.pitch]);
                field(*n, "voice", juce::String(voice));
                if (drum && crossHead(e.pitch))
                    field(*n, "notehead", "x");
                if (style == StaffStyle::grand)
                    field(*n, "staff", e.pitch < 60 ? "2" : "1");
                if (e.tiedFrom || e.tiedTo || e.string) {
                    auto* notation = n->createNewChildElement("notations");
                    ties(*notation, "tied");
                    if (e.string) {
                        auto* tech = notation->createNewChildElement("technical");
                        field(*tech, "string", juce::String(e.string));
                        field(*tech, "fret", juce::String(e.fret));
                    }
                }
                at = e.at + e.length;
            }
            rest(*m, s.barTicks - at, voice);
            rewind = true;
        }
    }
    const auto xml = root.toString();
    return stream.write(xml.toRawUTF8(), xml.getNumBytesAsUTF8()) ? juce::Result::ok()
                                                                  : juce::Result::fail("MusicXML write failed");
}

namespace {
constexpr float paperWidth = 595, paperHeight = 842;
const juce::Colour ink(0xff192728), soft(0xff50625f), accent(0xff276356);
void text(juce::Graphics& g, const juce::String& s, float x, float y, float w, float h, float size, bool bold = false,
          int lines = 1) {
    g.setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText(s, juce::Rectangle<float>(x, y, w, h).toNearestInt(), juce::Justification::centredLeft, lines,
                     0.7f);
}
void staffLines(juce::Graphics& g, float x, float right, float y, int count) {
    g.setColour(soft);
    for (int l = 0; l < count; ++l)
        g.drawLine(x, y + l * 5, right, y + l * 5, 0.65f);
}
void drawClef(juce::Graphics& g, StaffStyle style, float x, float top) {
    g.setColour(ink);
    juce::Path path;
    if (style == StaffStyle::treble) {
        path.startNewSubPath(7, 19);
        path.cubicTo(-5, 16, 1, 8, 7, 1);
        path.cubicTo(15, -11, 8, -16, 7, -6);
        path.cubicTo(6, 0, 11, 20, 11, 26);
        path.cubicTo(11, 33, 3, 32, 5, 27);
        path.startNewSubPath(6, 18);
        path.cubicTo(-1, 10, 14, 4, 15, 14);
        path.cubicTo(16, 21, 7, 23, 3, 18);
    } else if (style == StaffStyle::bass) {
        path.startNewSubPath(1, 19);
        path.cubicTo(18, 12, 12, -2, 5, 1);
        path.cubicTo(0, 2, 1, 9, 5, 6);
        g.fillEllipse(x + 15, top + 2, 2.5f, 2.5f);
        g.fillEllipse(x + 15, top + 8, 2.5f, 2.5f);
    } else if (style == StaffStyle::alto) {
        g.fillRect(x, top, 2.5f, 20.f);
        g.fillRect(x + 4, top, 1.f, 20.f);
        path.startNewSubPath(7, 1);
        path.cubicTo(19, -3, 18, 12, 7, 10);
        path.cubicTo(18, 8, 19, 24, 7, 19);
    } else if (style == StaffStyle::percussion) {
        g.fillRect(x + 3, top + 5, 2.8f, 10.f);
        g.fillRect(x + 9, top + 5, 2.8f, 10.f);
    } else {
        text(g, "TAB", x - 1, top, 20, 20, 9, true);
    }
    g.strokePath(path, juce::PathStrokeType(1.7f), juce::AffineTransform::translation(x, top));
}
void drawNotes(juce::Graphics& g, const std::vector<ScoreNote>& events, const NotationScore& s, StaffStyle style,
               float left, float right, float top) {
    const bool tab = !tuning(style).empty();
    if (events.empty()) {
        g.setColour(ink);
        g.fillRect((left + right) / 2, top + 5, 7.f, 2.6f);
        if (style == StaffStyle::grand)
            g.fillRect((left + right) / 2, top + 56, 7.f, 2.6f);
    }
    std::map<int, int> accidentals;
    for (const auto& n : events) {
        const float x = left + 8 + static_cast<float>(double(n.at) / s.barTicks) * (right - left - 16);
        g.setColour(ink);
        if (tab) {
            const float y = top + 5 * (n.string ? n.string - 1 : static_cast<int>(tuning(style).size()) + 1);
            g.setColour(juce::Colours::white);
            g.fillRect(x - 2, y - 4, 12.f, 8.f);
            g.setColour(ink);
            text(g, n.string ? juce::String(n.fret) : "?", x - 1, y - 5, 12, 10, 9, true);
            continue;
        }
        const auto clefStyle =
            style == StaffStyle::grand ? (n.pitch < 60 ? StaffStyle::bass : StaffStyle::treble) : style;
        const float staffTop = top + (style == StaffStyle::grand && n.pitch < 60 ? 51 : 0);
        const int bottomPitch = clefStyle == StaffStyle::bass ? 43 : clefStyle == StaffStyle::alto ? 53 : 64;
        const int p = style == StaffStyle::percussion ? drumPosition(n.pitch) : n.pitch;
        const int offset = diatonic(p) - diatonic(bottomPitch);
        const float trueY = staffTop + 20 - offset * 2.5f;
        // Keep extreme pitches legible on paper by showing their explicit octave displacement.
        float y = trueY;
        int octaves = 0;
        while (y < staffTop - 13) {
            y += 17.5f;
            ++octaves;
        }
        while (y > staffTop + 34) {
            y -= 17.5f;
            --octaves;
        }
        if (octaves)
            text(g, juce::String(steps[p % 12]) + (alter[p % 12] ? "#" : "") + juce::String(octave(p)), x - 3, y + 5,
                 17, 9, 7);
        for (float line = staffTop - 5; line >= y - .2f; line -= 5)
            g.drawLine(x - 4, line, x + 5, line, .7f);
        for (float line = staffTop + 25; line <= y + .2f; line += 5)
            g.drawLine(x - 4, line, x + 5, line, .7f);
        const double quarters = static_cast<double>(n.length) / s.ppq;
        double closest = 10000, base = 1;
        bool dotted = false;
        for (double unit : {4., 2., 1., .5, .25, .125, .0625})
            for (bool dot : {false, true}) {
                const auto distance = std::abs(std::log2(quarters / (unit * (dot ? 1.5 : 1.))));
                if (distance < closest) {
                    closest = distance;
                    base = unit;
                    dotted = dot;
                }
            }
        if (style == StaffStyle::percussion && crossHead(n.pitch)) {
            g.drawLine(x - 2.5f, y - 2.5f, x + 2.5f, y + 2.5f, 1);
            g.drawLine(x - 2.5f, y + 2.5f, x + 2.5f, y - 2.5f, 1);
        } else if (base >= 2)
            g.drawEllipse(x - 3, y - 2, 6, 4, 1.1f);
        else
            g.fillEllipse(x - 3, y - 2, 6, 4);
        if (dotted)
            g.fillEllipse(x + 4.f, y - 2, 1.6f, 1.6f);
        if (style != StaffStyle::percussion) {
            auto& prior = accidentals[diatonic(p)];
            if (prior != alter[p % 12] && !n.tiedFrom) {
                if (alter[p % 12])
                    text(g, "#", x - 9, y - 5, 6, 10, 9);
                else {
                    g.drawLine(x - 8, y - 5, x - 8, y + 2, .7f);
                    g.drawLine(x - 5, y - 2, x - 5, y + 5, .7f);
                    g.drawLine(x - 8, y - 1, x - 5, y - 2, .8f);
                    g.drawLine(x - 8, y + 2, x - 5, y + 1, .8f);
                }
            }
            prior = alter[p % 12];
        }
        if (base < 4) {
            const bool down = y < staffTop + 10;
            const float stemX = x + (down ? -2.5f : 2.5f), end = y + (down ? 13.f : -13.f);
            g.drawLine(stemX, y, stemX, end, .8f);
            for (int flag = 0; flag < 4 && base < 1.0 / (1 << flag); ++flag) {
                const float tail = end + (down ? -1 : 1) * flag * 3;
                g.drawLine(stemX, tail, stemX + 4, tail + (down ? -4 : 4), 1);
            }
        }
        if (n.tiedTo || n.tiedFrom) {
            juce::Path tie;
            const float x1 = n.tiedFrom ? left : x, x2 = n.tiedTo ? right - 2 : x;
            tie.startNewSubPath(x1, y + 3);
            tie.quadraticTo((x1 + x2) / 2, y + 9, x2, y + 3);
            g.strokePath(tie, juce::PathStrokeType(.75f));
        }
    }
}
} // namespace
int barsPerPage(bool lead, StaffStyle style, const SheetOptions& options) {
    return lead ? 8 * juce::jlimit(1, 8, options.barsPerLine) : style == StaffStyle::grand ? 8 : 10;
}
int sheetPages(const NotationScore& s, bool lead, StaffStyle style, const SheetOptions& options) {
    const int count = barsPerPage(lead, style, options);
    return std::max(1, (s.bars + count - 1) / count);
}
void drawSheetPage(juce::Graphics& g, const NotationScore& s, int part, StaffStyle style, const MeasureNotes& notes,
                   bool lead, int page, const SheetOptions& options, Tick playingTick) {
    g.fillAll(juce::Colours::white);
    g.setColour(accent);
    const auto heading = lead ? TRANS("Lead sheet") : (part >= 0 ? s.parts[part].name : TRANS("Score"));
    text(g, heading, 33, 27, 530, 17, 13, true);
    g.setColour(ink);
    text(g, s.title, 32, 50, 531, 37, 29, true);
    juce::StringArray meta;
    meta.add(juce::String(s.beats) + "/" + juce::String(s.beatType));
    if (s.key.isNotEmpty())
        meta.add(s.key);
    if (!s.tempos.empty())
        meta.add(s.tempos.front().text + " BPM");
    if (!lead)
        meta.add(juce::translate(staffName(style)));
    g.setColour(soft);
    text(g, meta.joinIntoString("    /    "), 34, 89, 527, 19, 12);
    g.drawLine(33, 117, 562, 117, 1);
    const int cols = lead ? juce::jlimit(1, 8, options.barsPerLine) : 2, per = barsPerPage(lead, style, options),
              first = page * per;
    const float rowH = 640.f / (per / cols);
    for (int r = 0; r < per / cols && first + r * cols < s.bars; ++r) {
        const float y = 134 + r * rowH, startX = lead ? 33.f : 58.f, width = (562 - startX) / cols;
        const float top = y + (style == StaffStyle::grand ? 40 : 51);
        const int lines = tuning(style).empty() ? 5 : static_cast<int>(tuning(style).size());
        if (!lead) {
            const float right = startX + std::min(cols, s.bars - first - r * cols) * width;
            staffLines(g, 33, right, top, lines);
            drawClef(g, style == StaffStyle::grand ? StaffStyle::treble : style, 34, top);
            if (style == StaffStyle::grand) {
                staffLines(g, 33, right, top + 51, 5);
                drawClef(g, StaffStyle::bass, 34, top + 51);
            }
        }
        for (int c = 0; c < cols; ++c) {
            const int bar = first + r * cols + c;
            if (bar >= s.bars)
                break;
            const float x = startX + c * width;
            const Tick from = bar * s.barTicks, to = from + s.barTicks;
            if (playingTick >= from && playingTick < to) {
                g.setColour(juce::Colour(0xffdaf3e9));
                g.fillRect(x + 1, y, width - 2, lead ? rowH - 7 : 17.f);
            }
            juce::StringArray labels;
            for (const auto& section : s.sections)
                if (section.at >= from && section.at < to)
                    labels.add(section.text);
            for (const auto& tempo : s.tempos)
                if (tempo.at > 0 && tempo.at >= from && tempo.at < to)
                    labels.add(tempo.text + " BPM");
            g.setColour(accent);
            text(g, juce::String(bar + 1) + "   " + labels.joinIntoString(" / "), x + 4, y, width - 8, 16, 10, true);
            if (lead) {
                g.setColour(juce::Colour(0xffcad4d1));
                g.drawLine(x, y + 20, x, y + rowH - 7, .7f);
                g.drawLine(x, y + 19, x + width, y + 19, .7f);
                for (const auto& chord : s.chords) {
                    if (chord.end <= from || chord.at >= to)
                        continue;
                    const float a = static_cast<float>(double(std::max(from, chord.at) - from) / s.barTicks);
                    const float b = static_cast<float>(double(std::min(to, chord.end) - from) / s.barTicks);
                    if (chord.at <= playingTick && playingTick < chord.end && playingTick >= from && playingTick < to) {
                        g.setColour(juce::Colour(0xff9bdbc6));
                        g.fillRect(x + width * a, y + 21, width * (b - a), 22.f);
                    }
                    g.setColour(ink);
                    text(g, chord.text, x + width * a + 3, y + 21, std::max(1.f, width * (b - a) - 4), 22, 16, true);
                }
                juce::StringArray words;
                for (const auto& unit : s.lyrics)
                    if (unit.at >= from && unit.at < to &&
                        (options.lyricVoice.isEmpty() || options.lyricVoice == unit.voice))
                        words.add(unit.text);
                g.setColour(soft);
                text(g, words.joinIntoString(" "), x + 4, y + 43, width - 8, rowH - 51, 11, false, 2);
            } else {
                g.setColour(ink);
                g.drawLine(x + width, top, x + width, top + (style == StaffStyle::grand ? 71 : 5 * (lines - 1)), .8f);
                if (bar < static_cast<int>(notes.size()))
                    drawNotes(g, notes[bar], s, style, x, x + width, top);
            }
        }
    }
    g.setColour(soft);
    g.drawLine(33, 790, 562, 790, .8f);
    text(g, "OSTF Studio", 34, 796, 200, 17, 10);
    text(g, juce::String(page + 1) + " / " + juce::String(sheetPages(s, lead, style, options)), 510, 796, 55, 17, 11);
}

juce::Result writeSheetPdf(juce::OutputStream& out, const NotationScore& s, int part, StaffStyle style,
                           const MeasureNotes& notes, bool lead, const SheetOptions& options) {
    // Each A4 page embeds a lossless grayscale rendering of the same graphics as the view.
    // This includes local font fallback for Japanese, without requiring a PDF viewer font.
    bool ok = true;
    auto ascii = [&](const juce::String& str) { ok &= out.write(str.toRawUTF8(), str.getNumBytesAsUTF8()); };
    std::vector<juce::int64> offsets{0};
    auto object = [&] {
        offsets.push_back(out.getPosition());
        ascii(juce::String(static_cast<int>(offsets.size() - 1)) + " 0 obj\n");
    };
    const int pages = sheetPages(s, lead, style, options);
    ascii("%PDF-1.4\n");
    object();
    ascii("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    juce::String kids;
    for (int i = 0; i < pages; ++i)
        kids << (3 + i * 3) << " 0 R ";
    object();
    ascii("<< /Type /Pages /Count " + juce::String(pages) + " /Kids [" + kids + "] >>\nendobj\n");
    for (int page = 0; page < pages && ok; ++page) {
        constexpr int scale = 3, w = 595 * scale, h = 842 * scale;
        juce::Image image(juce::Image::RGB, w, h, true, juce::SoftwareImageType{});
        {
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
            drawSheetPage(g, s, part, style, notes, lead, page, options);
        }
        juce::MemoryOutputStream bytes;
        {
            juce::GZIPCompressorOutputStream z(bytes, 6, 15);
            juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
            std::vector<juce::uint8> scan(w);
            for (int y = 0; y < h; ++y) {
                const auto* pixel = pixels.getLinePointer(y);
                for (int x = 0; x < w; ++x, pixel += pixels.pixelStride) {
                    const auto* rgb = reinterpret_cast<const juce::PixelRGB*>(pixel);
                    scan[x] = static_cast<juce::uint8>(
                        (77 * rgb->getRed() + 150 * rgb->getGreen() + 29 * rgb->getBlue()) / 256);
                }
                if (!z.write(scan.data(), scan.size()))
                    return juce::Result::fail("PDF compression failed");
            }
        }
        const int id = 3 + page * 3;
        object();
        ascii("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /XObject << /Im " +
              juce::String(id + 2) + " 0 R >> >> /Contents " + juce::String(id + 1) + " 0 R >>\nendobj\n");
        const juce::String paint = "q 595 0 0 842 0 0 cm /Im Do Q\n";
        object();
        ascii("<< /Length " + juce::String(paint.length()) + " >>\nstream\n" + paint + "endstream\nendobj\n");
        object();
        ascii("<< /Type /XObject /Subtype /Image /Width " + juce::String(w) + " /Height " + juce::String(h) +
              " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length " +
              juce::String(static_cast<juce::int64>(bytes.getDataSize())) + " >>\nstream\n");
        ok &= out.write(bytes.getData(), bytes.getDataSize());
        ascii("\nendstream\nendobj\n");
    }
    const auto xref = out.getPosition();
    ascii("xref\n0 " + juce::String(static_cast<int>(offsets.size())) + "\n0000000000 65535 f \n");
    for (std::size_t i = 1; i < offsets.size(); ++i)
        ascii(juce::String(offsets[i]).paddedLeft('0', 10) + " 00000 n \n");
    ascii("trailer\n<< /Size " + juce::String(static_cast<int>(offsets.size())) + " /Root 1 0 R >>\nstartxref\n" +
          juce::String(xref) + "\n%%EOF\n");
    return ok ? juce::Result::ok() : juce::Result::fail("PDF write failed");
}
} // namespace ostfstudio
