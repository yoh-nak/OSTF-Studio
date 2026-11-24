#include "ui/GlobalTimelineEdit.h"
#include "ui/MusicXmlKey.h"
#include "ui/OstfpPreview.h"
#include "ui/ScoreImportData.h"
#include <cmath>
#include <tuple>

namespace ostfstudio::scoreimport {
namespace {
using Xml = juce::XmlElement;
using String = juce::String;
constexpr int maxBytes = 16 * 1024 * 1024;
juce::Result bad(const char *message) { return juce::Result::fail(juce::translate(message)); }
String child(const Xml &el, const char *name) { return el.getChildElementAllSubText(name, {}).trim(); }

std::unique_ptr<Xml> parseStream(juce::InputStream *stream, int limit) {
    if (!stream)
        return {};
    juce::MemoryBlock bytes;
    stream->readIntoMemoryBlock(bytes, limit + 1);
    if (bytes.isEmpty() || bytes.getSize() > static_cast<size_t>(limit))
        return {};
    auto text = String::createStringFromData(bytes.getData(), static_cast<int>(bytes.getSize()));
    if (text.containsIgnoreCase("<!ENTITY"))
        return {};
    return juce::XmlDocument::parse(text);
}
std::unique_ptr<Xml> openScore(const juce::File &file) {
    if (file.getSize() > maxBytes)
        return {};
    if (!file.hasFileExtension("mxl"))
        return parseStream(file.createInputStream().get(), maxBytes);
    juce::ZipFile zip(file);
    const auto entry = [&](const String &path, int limit) -> std::unique_ptr<Xml> {
        const auto n = zip.getIndexOfFileName(path);
        if (n < 0 || zip.getEntry(n)->uncompressedSize > limit)
            return {};
        std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(n));
        return parseStream(in.get(), limit);
    };
    const auto container = entry("META-INF/container.xml", 65536);
    const auto *roots = container ? container->getChildByName("rootfiles") : nullptr;
    const auto *root = roots ? roots->getChildByName("rootfile") : nullptr;
    return root ? entry(root->getStringAttribute("full-path"), maxBytes) : nullptr;
}
String spelling(const Xml &el, const char *step, const char *alteration) {
    const auto letter = child(el, step);
    const auto alter = child(el, alteration).getDoubleValue();
    if (letter.length() != 1 || !String("ABCDEFG").contains(letter) || std::abs(alter) > 2 ||
        alter != std::floor(alter))
        return {};
    return letter + String::repeatedString(alter < 0 ? "b" : "#", static_cast<int>(std::abs(alter)));
}
String chordSymbol(const Xml &h) {
    const auto *root = h.getChildByName("root");
    const auto *kind = h.getChildByName("kind");
    if (!root || !kind)
        return {};
    const auto base = spelling(*root, "root-step", "root-alter");
    if (base.isEmpty())
        return {};
    static const std::map<String, String> suffixes{{"major", ""},
                                                   {"minor", "m"},
                                                   {"dominant", "7"},
                                                   {"major-seventh", "maj7"},
                                                   {"minor-seventh", "m7"},
                                                   {"diminished", "dim"},
                                                   {"diminished-seventh", "dim7"},
                                                   {"half-diminished", "m7b5"},
                                                   {"augmented", "aug"},
                                                   {"suspended-fourth", "sus4"},
                                                   {"suspended-second", "sus2"},
                                                   {"major-sixth", "6"},
                                                   {"minor-sixth", "m6"},
                                                   {"dominant-ninth", "9"},
                                                   {"major-ninth", "maj9"},
                                                   {"minor-ninth", "m9"},
                                                   {"dominant-11th", "11"},
                                                   {"dominant-13th", "13"},
                                                   {"power", "5"}};
    const auto name = kind->getAllSubText().trim();
    const auto it = suffixes.find(name);
    auto symbol = base + (it != suffixes.end() ? it->second : kind->getStringAttribute("text", name));
    if (auto *bass = h.getChildByName("bass"))
        symbol += "/" + spelling(*bass, "bass-step", "bass-alter");
    return symbol;
}
struct Reader {
    ImportSong &song;
    std::map<Tick, String> harmony;
    int writtenNotes = 0;

    juce::Result part(const Xml &partXml, const Xml &description) {
        Part part;
        part.name = child(description, "part-name");
        if (part.name.isEmpty())
            part.name = partXml.getStringAttribute("id");
        part.channel = static_cast<int>(song.parts.size()) + 1;
        if (part.channel >= 10)
            ++part.channel;
        std::map<String, int> percussion;
        for (auto *inst : description.getChildWithTagNameIterator("midi-instrument")) {
            const int channel = child(*inst, "midi-channel").getIntValue();
            if (channel >= 1 && channel <= 16)
                part.channel = channel;
            auto pitch = child(*inst, "midi-unpitched").getIntValue();
            if (pitch >= 1 && pitch <= 128)
                percussion[inst->getStringAttribute("id")] = pitch - 1;
        }
        double divisions = 1, barStart = 0;
        int beats = song.beats, unit = song.beatType, transpose = 0;
        std::map<std::tuple<String, String, int>, int> ties;
        const auto partId = "xml-" + String(song.parts.size() + 1);
        bool hasLyrics = false;
        const auto ticks = [&](double value) { return value * song.ppq / divisions; };
        for (auto *measure : partXml.getChildWithTagNameIterator("measure")) {
            double cursor = 0, furthest = 0, lastStart = 0;
            for (auto *el : measure->getChildIterator()) {
                const auto tag = el->getTagName();
                if (tag == "attributes") {
                    if (el->getChildByName("divisions")) {
                        divisions = child(*el, "divisions").getDoubleValue();
                        if (!(divisions > 0 && divisions <= 1000000))
                            return bad("Invalid MusicXML divisions.");
                    }
                    if (auto *time = el->getChildByName("time"); time && time->getChildByName("beats")) {
                        const auto value = child(*time, "beats");
                        beats = value.getIntValue();
                        unit = child(*time, "beat-type").getIntValue();
                        if (!value.containsOnly("0123456789") || beats < 1 || beats > 32 || unit < 1 || unit > 32 ||
                            (unit & (unit - 1)))
                            return bad("Unsupported MusicXML time signature.");
                        song.meters[std::llround(barStart + cursor)] = {beats, unit};
                    }
                    if (auto *key = el->getChildByName("key")) {
                        const auto mode = child(*key, "mode");
                        const int fifths = child(*key, "fifths").getIntValue();
                        if (key->getChildByName("fifths") && (mode == "major" || mode == "minor") && fifths >= -7 &&
                            fifths <= 7) {
                            const String tonic = musicxml::tonics(mode == "minor")[fifths + 7];
                            song.keys.try_emplace(std::llround(barStart + cursor),
                                                  obj({{"state", "tonal"},
                                                       {"tonic", parseSpelling(tonic)},
                                                       {"mode", mode},
                                                       {"label", tonic + " " + mode}}));
                        }
                    }
                    if (auto *t = el->getChildByName("transpose"))
                        transpose =
                            child(*t, "chromatic").getIntValue() + 12 * child(*t, "octave-change").getIntValue();
                } else if (tag == "direction" || tag == "sound") {
                    const auto *sound = tag == "sound" ? el : el->getChildByName("sound");
                    double bpm = sound ? sound->getDoubleAttribute("tempo") : 0;
                    if (bpm == 0)
                        if (auto *type = el->getChildByName("direction-type"))
                            if (auto *met = type->getChildByName("metronome")) {
                                const String beat = child(*met, "beat-unit");
                                const double factor = beat == "half"      ? 2
                                                      : beat == "quarter" ? 1
                                                      : beat == "eighth"  ? .5
                                                      : beat == "whole"   ? 4
                                                                          : 0;
                                bpm = child(*met, "per-minute").getDoubleValue() * factor *
                                      (met->getChildByName("beat-unit-dot") ? 1.5 : 1.);
                            }
                    if (bpm > 0 && bpm <= 1000)
                        song.tempos[std::max<Tick>(
                            0, std::llround(barStart + cursor + ticks(child(*el, "offset").getDoubleValue())))] = bpm;
                } else if (tag == "harmony") {
                    const auto symbol = chordSymbol(*el);
                    if (symbol.isNotEmpty())
                        harmony.try_emplace(
                            std::max<Tick>(
                                0, std::llround(barStart + cursor + ticks(child(*el, "offset").getDoubleValue()))),
                            symbol);
                } else if (tag == "forward" || tag == "backup" || tag == "note") {
                    if (el->getChildByName("grace"))
                        continue;
                    const auto duration = ticks(child(*el, "duration").getDoubleValue());
                    if (!(duration > 0 && duration <= 10000000))
                        return bad("Invalid MusicXML duration.");
                    const bool chord = el->getChildByName("chord") != nullptr;
                    const double start = chord ? lastStart : cursor;
                    if (tag == "backup")
                        cursor -= duration;
                    else if (!chord) {
                        lastStart = cursor;
                        cursor += duration;
                    }
                    if (cursor < -.01 || barStart + cursor > 100000000)
                        return bad("Invalid MusicXML measure position.");
                    furthest = std::max(furthest, tag == "note" ? start + duration : cursor);
                    if (tag != "note" || el->getChildByName("rest"))
                        continue;
                    if (++writtenNotes > 60000)
                        return bad("Too many MusicXML notes.");
                    int pitch = -1;
                    const bool drum = el->getChildByName("unpitched") != nullptr;
                    if (auto *p = el->getChildByName("pitch")) {
                        const auto step = child(*p, "step"), oct = child(*p, "octave");
                        const auto alter = child(*p, "alter").getDoubleValue();
                        constexpr int semitones[]{0, 2, 4, 5, 7, 9, 11};
                        const int index = String("CDEFGAB").indexOf(step);
                        if (step.length() == 1 && index >= 0 && oct.isNotEmpty() && alter == std::floor(alter) &&
                            std::abs(alter) <= 12)
                            pitch =
                                12 * (oct.getIntValue() + 1) + semitones[index] + static_cast<int>(alter) + transpose;
                    } else if (drum) {
                        const auto *inst = el->getChildByName("instrument");
                        const auto match = percussion.find(inst ? inst->getStringAttribute("id") : String{});
                        if (match != percussion.end())
                            pitch = match->second;
                        else if (percussion.size() == 1)
                            pitch = percussion.begin()->second;
                        part.channel = 10;
                    }
                    if (pitch < 0 || pitch > 127)
                        return bad("MusicXML contains a pitch or percussion mapping that Studio cannot represent.");
                    const Tick at = std::llround(barStart + start),
                               end = std::max<Tick>(at + 1, std::llround(barStart + start + duration));
                    const auto key = std::make_tuple(child(*el, "voice"), child(*el, "staff"), pitch);
                    bool stop = false, begin = false;
                    for (auto *t : el->getChildWithTagNameIterator("tie")) {
                        stop |= t->getStringAttribute("type") == "stop";
                        begin |= t->getStringAttribute("type") == "start";
                    }
                    auto pending = ties.find(key);
                    int index = part.notes.size();
                    if (stop && pending != ties.end()) {
                        index = pending->second;
                        auto &n = part.notes.getReference(index);
                        n.getDynamicObject()->setProperty("duration", end - static_cast<Tick>(n["start"]));
                    } else {
                        auto n = obj({{"id", "n-" + String(++song.count)},
                                      {"start", at},
                                      {"duration", end - at},
                                      {"velocity", 80}});
                        if (drum) {
                            n.getDynamicObject()->setProperty("soundSlotId", "gm-" + String(pitch));
                            song.drums.insert(pitch);
                        } else
                            n.getDynamicObject()->setProperty("pitch", pitch);
                        part.notes.add(n);
                    }
                    if (begin)
                        ties[key] = index;
                    else
                        ties.erase(key);
                    if (auto *lyric = el->getChildByName("lyric")) {
                        auto text = child(*lyric, "text");
                        if (text.isNotEmpty()) {
                            if (!hasLyrics) {
                                song.documents.add(obj({{"id", partId}, {"title", part.name}}));
                                hasLyrics = true;
                            }
                            auto id = String(song.units.size() + 1);
                            song.phrases.add(obj({{"id", "p-" + id},
                                                  {"documentId", partId},
                                                  {"text", text},
                                                  {"start", at},
                                                  {"duration", end - at}}));
                            song.units.add(obj({{"id", "u-" + id},
                                                {"phraseId", "p-" + id},
                                                {"text", text},
                                                {"start", at},
                                                {"duration", end - at}}));
                            song.alignments.add(obj({{"id", "a-" + id},
                                                     {"unitIds", List{"u-" + id}},
                                                     {"noteIds", List{part.notes[index]["id"]}}}));
                        }
                    }
                }
            }
            const double nominal = 4. * song.ppq * beats / unit;
            barStart += measure->getStringAttribute("implicit") == "yes" && furthest > 0 ? furthest
                                                                                         : std::max(nominal, furthest);
            if (barStart > 100000000)
                return bad("MusicXML exceeds the supported score length.");
        }
        song.length = std::max<Tick>(song.length, std::llround(barStart));
        if (part.channel == 10 && std::any_of(part.notes.begin(), part.notes.end(),
                                              [](const juce::var &n) { return n.hasProperty("pitch"); }))
            return bad(
                "Studio requires pitched notes and percussion on separate parts outside channel 10 for pitched notes.");
        song.parts.push_back(std::move(part));
        return juce::Result::ok();
    }
};
} // namespace

juce::Result readMusicXml(const juce::File &file, ImportSong &song, List &chords) {
    auto xml = openScore(file);
    if (!xml || !xml->hasTagName("score-partwise"))
        return bad("Choose a partwise MusicXML score (.musicxml, .xml or .mxl).");
    auto *parts = xml->getChildByName("part-list");
    if (!parts)
        return bad("MusicXML part list is missing.");
    if (auto *work = xml->getChildByName("work"))
        song.title = child(*work, "work-title");
    if (song.title.isEmpty())
        song.title = child(*xml, "movement-title");
    Reader reader{song};
    for (auto *part : xml->getChildWithTagNameIterator("part")) {
        if (song.parts.size() >= OstfpPreview::maxTracks)
            return bad("Studio can open up to fifteen source tracks.");
        const auto *description = parts->getChildByAttribute("id", part->getStringAttribute("id"));
        if (!description)
            return bad("Missing MusicXML part description.");
        if (auto r = reader.part(*part, *description); r.failed())
            return r;
    }
    std::tie(song.beats, song.beatType) = song.meters.at(0);
    for (auto it = reader.harmony.begin(); it != reader.harmony.end(); ++it) {
        const auto next = std::next(it);
        const auto end = next == reader.harmony.end() ? song.length : std::min(song.length, next->first);
        if (end <= it->first)
            continue;
        auto chord = obj({{"id", "chord-" + String(chords.size() + 1)},
                          {"start", it->first},
                          {"duration", end - it->first},
                          {"label", it->second}});
        editChordSymbol(chord, it->second);
        chords.add(chord);
    }
    if (song.parts.empty() || (song.count == 0 && chords.isEmpty()))
        return bad("No supported score parts found.");
    return juce::Result::ok();
}
} // namespace ostfstudio::scoreimport
