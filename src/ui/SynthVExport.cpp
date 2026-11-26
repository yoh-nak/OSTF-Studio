#include "ui/SynthVExport.h"
#include "../LyricAlignment.h"
#include "ui/OstfpPreview.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace ostfstudio {
namespace {
using namespace juce;
using List = Array<var>;
using Tick = juce::int64;
constexpr Tick blicksPerQuarter = 705600000;
constexpr int svpVersion = 113;

var object(std::initializer_list<std::pair<const char*, var>> fields = {}) {
    auto result = std::make_unique<DynamicObject>();
    for (const auto& [key, value] : fields) result->setProperty(key, value);
    return var(result.release());
}

const List& entries(const var& value) {
    static const List empty;
    return value.isArray() ? *value.getArray() : empty;
}

// Convert absolute endpoints independently, so adjacent notes still meet at any PPQ.
Tick blick(Tick tick, Tick ppq) {
    const Tick whole = tick / ppq;
    const Tick fraction = ((tick % ppq) * blicksPerQuarter + ppq / 2) / ppq;
    if (tick < 0 || whole > (std::numeric_limits<Tick>::max() - fraction) / blicksPerQuarter)
        throw std::range_error("Song timing exceeds the SVP range.");
    return whole * blicksPerQuarter + fraction;
}

struct LyricLinks {
    std::map<String, std::set<String>> owners;
    std::map<std::pair<String,String>, String> words; // Voice and note IDs. Empty voice is an unscoped alignment.
    explicit LyricLinks(const var& score, const OstfpPreview& song) {
        std::map<String, String> units;
        LyricStarts unitStarts, onsets;
        for (const auto& unit : entries(score["lyrics"]["units"])) {
            if (unit.hasProperty("start")) unitStarts.emplace(unit["id"].toString(),Tick(unit["start"]));
            auto word = unit["text"].toString().trim();
            if (word.isEmpty()) word = unit["pronunciation"].toString().trim();
            units[unit["id"].toString()] = word;
        }
        std::set<String> active;
        for (const auto& track : song.pitchedNotes)
            for (const auto& note : track) {
                onsets.emplace(note.id,note.startTick);
                if (note.enabled) active.insert(note.id);
            }
        for (const auto& link : entries(score["lyrics"]["alignments"])) {
            const auto voice = link["voiceId"].toString();
            const auto ids = lyricIdsInTimeOrder(entries(link["noteIds"]),onsets);
            const auto unitIds = lyricIdsInTimeOrder(entries(link["unitIds"]),unitStarts);
            std::vector<String> ordered;
            for (size_t i = 0; i < ids.size(); ++i) {
                const auto& id = ids[i];
                if (voice.isNotEmpty()) owners[id].insert(voice);
                if (!active.contains(id)) continue;
                if (ids.size() == unitIds.size()) words[{voice,id}] += units[unitIds[i]];
                else ordered.push_back(id);
            }
            if (!ordered.empty()) {
                for (const auto& unit : unitIds) words[{voice,ordered.front()}] += units[unit];
                for (size_t i = 1; i < ordered.size(); ++i) words[{voice,ordered[i]}] += "-";
            }
        }
        for (const auto& track : entries(score["tracks"]))
            for (const auto& clip : entries(track["clips"]))
                for (const auto& perf : entries(clip["vocalPerformances"]))
                    for (const auto& id : entries(perf["noteIds"])) owners[id.toString()].insert(perf["voiceId"].toString());
    }
    String lyric(const String& voice, const String& note, const String& fallback) const {
        auto it = words.find({voice,note});
        if (it == words.end()) it = words.find({{},note});
        return it != words.end() && it->second.isNotEmpty() ? it->second : fallback;
    }
};

struct Part {
    String id, name, defaultLyric;
    bool lead;
    std::vector<const PitchedNote*> notes;
};

void appendVoice(List& tracks, const Part& part, Tick ppq, const LyricLinks& links) {
    struct Line { Tick end; List notes; };
    std::vector<Line> lines;
    for (const auto* note : part.notes) {
        const auto start = blick(note->startTick, ppq), end = blick(note->endTick, ppq);
        if (end <= start) throw std::range_error("A note is shorter than the SVP time resolution.");
        auto line = std::find_if(lines.begin(), lines.end(), [&](const auto& ch) { return ch.end <= start; });
        if (line == lines.end()) line = lines.emplace(lines.end(), Line{});
        line->notes.add(object({{"onset",start},{"duration",end-start},{"pitch",note->pitch},
                                {"lyrics",links.lyric(part.id,note->id,part.defaultLyric)},{"phonemes",""},{"attributes",object()}}));
        line->end = end;
    }
    int index = 0;
    for (auto& line : lines) {
        const auto uuid = Uuid().toDashedString();
        const auto name = ++index == 1 ? part.name : part.name + " " + String(index);
        const auto group = object({{"name","main"},{"uuid",uuid},{"parameters",object()},{"notes",std::move(line.notes)}});
        const auto ref = object({{"groupID",uuid},{"blickOffset",0},{"pitchOffset",0},{"isInstrumental",false},
                                 {"database",object({{"name",""},{"language",""},{"phoneset",""}})},{"dictionary",""},{"voice",object()}});
        tracks.add(object({{"name",name},{"dispOrder",tracks.size()},{"dispColor",part.lead ? "ff4abbb0" : "ff987db8"},
                           {"renderEnabled",true},{"mixer",object({{"gainDecibel",0},{"pan",0},{"mute",false},{"solo",false},{"display",true}})},
                           {"mainGroup",group},{"mainRef",ref},{"groups",List{}}}));
    }
}

List vocalTracks(const OstfpPreview& song, const var& score, SynthVParts selection) {
    const LyricLinks links(score,song);
    List tracks;
    for (size_t i = 0; i < song.sourceTrackCount(); ++i) {
        if (song.trackKinds[i] != TrackKind::pitched) continue;
        const auto track = score["tracks"][int(i)];
        const auto trackId = song.trackIds[int(i)], name = String::fromUTF8(song.trackLanes[i].label);
        const bool primary = song.primaryMelodyTrack == i;
        std::vector<Part> parts;
        for (const auto& v : entries(score["lyrics"]["voices"])) {
            if (v["trackId"] != trackId) continue;
            const auto role = v["role"].toString().toLowerCase();
            const bool lead = ((primary || !song.primaryMelodyTrack) && (role == "lead" || role == "melody"))
                           || (primary && role.isEmpty() && parts.empty());
            parts.push_back({v["id"].toString(),v.getProperty("name",name).toString(),
                             v["vocalProfile"]["deliveryMode"] == "hum" ? "m" : "la",lead,{}});
        }
        const auto role = track["role"].toString().toLowerCase();
        const bool trackVocal = primary || song.mixerGroups[i] == MixerTrackGroup::vocal
                             || song.declaredVocalTrackIds.contains(trackId) || song.lyricTrackIds.contains(trackId)
                             || role == "chorus" || role == "backing" || role == "backing vocal";
        if (parts.empty() && trackVocal) parts.push_back({{},name,"la",primary,{}});
        if (parts.empty()) continue;
        if (parts.size() == 1 && primary) parts.front().lead = true;
        const size_t voiceCount = parts.size();
        for (const auto& note : song.pitchedNotes[i]) {
            if (!note.enabled || note.fixedTrigger) continue;
            const auto owners = links.owners.find(note.id);
            if (owners == links.owners.end()) {
                if (voiceCount == 1) parts.front().notes.push_back(&note);
                else if (trackVocal) {
                    // Unassigned notes on a shared core track have no declared singer.
                    if (parts.size() == voiceCount) parts.push_back({{},name,"la",primary,{}});
                    parts.back().notes.push_back(&note);
                }
            } else {
                for (auto& part : parts) if (owners->second.contains(part.id)) part.notes.push_back(&note);
            }
        }
        for (auto& part : parts) {
            if (selection != SynthVParts::all && part.lead != (selection == SynthVParts::lead)) continue;
            std::stable_sort(part.notes.begin(), part.notes.end(), [](auto a, auto b) { return a->startTick < b->startTick; });
            appendVoice(tracks,part,song.ppq,links);
        }
    }
    return tracks;
}
}

juce::Result writeSynthV(const OstfpPreview& song, SynthVParts selection, juce::OutputStream& out) try {
    if (song.ppq <= 0 || song.ppq > std::numeric_limits<int>::max() || song.ticksPerBar <= 0)
        return juce::Result::fail("Invalid song timing for SVP export.");
    auto tracks = vocalTracks(song,juce::JSON::parse(song.sourceScore.toString()),selection);
    if (tracks.isEmpty()) return juce::Result::fail("No enabled vocal notes in the selected parts.");
    List tempos, meters;
    std::optional<std::int64_t> at = 0;
    while (at) {
        tempos.add(object({{"position",blick(*at,song.ppq)},{"bpm",song.clock.bpmAt(*at)}}));
        at = song.clock.nextChangeAfter(*at);
    }
    for (const auto& meter : song.meters)
        meters.add(object({{"index",meter.tick / song.ticksPerBar},{"numerator",meter.numerator},{"denominator",meter.denominator}}));
    const auto document = object({{"version",svpVersion},{"time",object({{"meter",meters},{"tempo",tempos}})},
                                  {"tracks",tracks},{"library",List{}},{"renderConfig",object({{"destination","./"},{"filename",juce::File::createLegalFileName(song.title)},
                                   {"numChannels",1},{"bitDepth",16},{"sampleRate",44100},{"aspirationFormat","noAspiration"},{"exportMixDown",true}})}});
    const auto json = juce::JSON::toString(document,true);
    // The version 1 project stream is UTF-8 with a terminating NUL.
    return out.write(json.toRawUTF8(),json.getNumBytesAsUTF8() + 1) ? juce::Result::ok() : juce::Result::fail("Could not write the SVP file.");
} catch (const std::range_error& error) { return juce::Result::fail(error.what()); }
}
