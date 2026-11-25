#include "ui/OstfpPreview.h"
#include "ui/GlobalTimelineEdit.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "StockEffects.h"
#include "core/MidiGeneratorPattern.h"
#include "core/ScoreSchema.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace ostfstudio {
namespace {
constexpr char mediaType[] = "application/vnd.ostf-project+zip";
const std::array<DrumSlot, 16> defaultPads{{{{}, "Kick", 36},
                                            {{}, "Snare", 38},
                                            {{}, "Closed hat", 42},
                                            {{}, "Open hat", 46},
                                            {{}, "Clap", 39},
                                            {{}, "Low tom", 45},
                                            {{}, "Side stick", 37},
                                            {{}, "Pedal hat", 44},
                                            {{}, "Low floor tom", 41},
                                            {{}, "High floor tom", 43},
                                            {{}, "Low-mid tom", 47},
                                            {{}, "High-mid tom", 48},
                                            {{}, "High tom", 50},
                                            {{}, "Crash", 49},
                                            {{}, "Ride", 51},
                                            {{}, "Ride bell", 53}}};
constexpr int maxBytes = 4 * 1024 * 1024;
constexpr int maxAudioBytes = 16 * 1024 * 1024;
// Allow four full-size WAV stems plus score, session and ZIP overhead.
constexpr int maxPackageBytes = 4 * maxAudioBytes + 4 * maxBytes;
constexpr char loopNamespace[] = "org.ostf.studio.midi-loops";
constexpr char mixNamespace[] = "org.ostf.studio";
constexpr char mixDocument[] = "session/studio.production.json";

void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

juce::MemoryBlock readMember(juce::ZipFile& zip, int index, int limit = maxBytes) {
    const auto* entry = zip.getEntry(index);
    require(entry && !entry->isSymbolicLink && entry->uncompressedSize >= 0 && entry->uncompressedSize <= limit,
            "Invalid or oversized OSTFP member");
    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
    require(stream != nullptr, "Cannot read OSTFP member");
    juce::MemoryBlock bytes;
    const auto size = stream->readIntoMemoryBlock(bytes, entry->uncompressedSize + 1);
    require(size == static_cast<std::size_t>(entry->uncompressedSize), "OSTFP member size mismatch");
    return bytes;
}

juce::var parseObject(const juce::MemoryBlock& bytes) {
    juce::var value;
    require(juce::JSON::parse(bytes.toString(), value).wasOk() && value.isObject(), "Invalid OSTFP JSON object");
    return value;
}

const juce::Array<juce::var>& array(const juce::var& value) {
    const auto* entries = value.getArray();
    require(entries != nullptr, "Expected an OSTF array");
    return *entries;
}

juce::int64 integer(const juce::var& value) {
    require(value.isInt() || value.isInt64(), "Expected an integer OSTF position or meter");
    return static_cast<juce::int64>(value);
}

bool safeScorePath(const juce::String& path) {
    const auto parts = juce::StringArray::fromTokens(path, "/", "");
    if (path.isEmpty() || path.startsWithChar('/') || path.endsWithChar('/') || path.contains("//")) return false;
    for (const auto& part : parts) {
        if (part == "." || part == ".." || !part.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"))
            return false;
    }
    return !path.equalsIgnoreCase("mimetype") && !path.equalsIgnoreCase("manifest.json");
}

void validateVersion(const juce::String& version, const juce::MemoryBlock& bytes) {
    validateScoreSchema(version.toStdString(), {static_cast<const char*>(bytes.getData()), bytes.getSize()});
}

juce::int64 scoreExtent(const juce::var& score) {
    // Nominal duration is optional. A clip is a placement, not a gate on its notes.
    const auto timeline = score["timeline"];
    juce::int64 end = integer(timeline.getProperty("duration", 0));
    require(end >= 0, "Invalid OSTF duration");
    auto addTicks = [](juce::int64 origin, juce::int64 offset) {
        require(origin >= 0 && offset >= -origin && offset <= std::numeric_limits<juce::int64>::max() - origin,
                "OSTF event outside the score tick range");
        return origin + offset;
    };
    auto include = [&](const juce::var& entries, const char* time, juce::int64 origin = 0) {
        if (entries.isVoid()) return;
        for (const auto& item : array(entries)) {
            const auto start = addTicks(origin, integer(item[time]));
            const auto length = integer(item.getProperty("duration", 1));
            require(length >= 0, "Invalid OSTF event duration");
            end = std::max(end, addTicks(start, length));
        }
    };
    include(timeline["sections"], "start");
    for (const auto* key : {"tempoChanges", "meterChanges", "keyChanges"}) include(timeline[key], "at");
    include(score["harmony"]["chords"], "start");
    include(score["harmony"]["scales"], "start");
    for (const auto& track : array(score["tracks"])) {
        const auto clips = track["clips"];
        include(clips, "start");
        if (clips.isArray()) for (const auto& clip : array(clips)) {
            const auto start = integer(clip["start"]);
            include(clip["notes"], "start", start);
            include(clip["midiEvents"], "start", start);
        }
    }
    return end;
}

void readCompChoices(const juce::var& score, OstfpPreview& project) {
    const auto groups = score["takeGroups"];
    if (!groups.isArray()) return;
    std::set<juce::String> groupIds, claimed;
    std::map<juce::String,std::vector<juce::Range<juce::int64>>> trackRanges;
    for (const auto& group : array(groups)) {
        const auto id=group["id"].toString(), track=group["trackId"].toString();
        require(groupIds.insert(id).second, "Duplicate MIDI take group ID.");
        const auto start=integer(group["startTick"]), length=integer(group["durationTicks"]);
        const juce::Range<juce::int64> range(start,start+length);
        auto& ownedRanges=trackRanges[track];
        for(const auto& other:ownedRanges) require(!other.intersects(range),"Overlapping take groups on one track.");
        ownedRanges.push_back(range);
        std::map<juce::String,juce::var> clips;
        for (const auto& lane : array(score["tracks"])) if (lane["id"] == track)
            for (const auto& clip : array(lane["clips"])) clips.emplace(clip["id"].toString(),clip);
        std::map<juce::String,juce::String> sources;
        std::set<juce::String> ownedClips;
        auto claim=[&](const juce::var& ref, bool enabled) {
            const auto key=ref.toString(); const auto it=clips.find(key);
            require(it!=clips.end() && claimed.insert(key).second, "Unknown or multiply owned take clip.");
            ownedClips.insert(key);
            const auto clip=it->second;
            const auto tick=integer(clip["start"]), dur=integer(clip["duration"]);
            require(tick>=start && dur<=range.getEnd()-tick && bool(clip.getProperty("enabled",true))==enabled,
                    "Take clip range or playback flag contradicts its comp.");
            return clip;
        };
        for (const auto& take : array(group["takes"]))
            for (const auto& ref : array(take["clipIds"])) {
                claim(ref,false);
                sources.emplace(ref.toString(),take["id"].toString());
            }
        std::set<juce::String> comps;
        const auto active=group["activeCompId"].toString();
        for (const auto& comp : array(group["comps"])) {
            const auto compId=comp["id"].toString();
            require(comps.insert(compId).second, "Duplicate comp ID.");
            auto next=start;
            for (const auto& region : array(comp["compRegions"])) {
                require(integer(region["startTick"])==next, "Comp regions must cover the group without gaps or overlaps.");
                next+=integer(region["durationTicks"]);
                if (region["kind"]=="source") {
                    const auto ref=region["clipId"].toString();
                    require(sources.contains(ref) && sources.at(ref)==region["takeId"].toString(), "Comp region refers to another take.");
                    const auto c=clips.at(ref);
                    require(integer(c["start"])<=integer(region["startTick"]) && integer(c["start"])+integer(c["duration"])>=next, "Region exceeds selected take clip.");
                }
            }
            require(next==range.getEnd(),"Incomplete comp map.");
            for (const auto& ref : array(comp["outputClipIds"])) claim(ref,active==compId);
            project.compChoices.push_back({id,compId,track,range,active==compId,comp.getProperty("name",compId).toString()});
        }
        require(active.isEmpty() || comps.contains(active),"Unknown active comp.");
        const auto policy = group["liveSelection"]["mode"].toString();
        if (policy == "cycle" || policy == "random" || policy == "weighted") project.usesUnsupportedCompSelection = true;
        if (group["visitSelections"].isArray())
            for (const auto& visit : array(group["visitSelections"]))
                if (visit["compId"].toString() != active) project.usesUnsupportedCompSelection = true;
        for(const auto& [key,c]:clips) {
            const auto a=integer(c["start"]);
            if(range.intersects({a,a+integer(c["duration"])})) require(ownedClips.contains(key),"Unmanaged clip overlaps a take group.");
        }
        project.compChoices.push_back({id,{},track,range,active.isEmpty(), "Silence"});
    }
}

std::unique_ptr<OstfpPreview> projectScore(const juce::var& score, bool retainVoicingRow = false) {
    require(score["format"] == "OSTF", "Expected an OSTF score");
    const auto timeline = score["timeline"];
    const auto ppq = integer(score["ppq"]);
    const auto duration = scoreExtent(score);
    const auto meters = timeline["meterChanges"];
    const auto& meterEntries = array(meters);
    require(!meterEntries.isEmpty() && integer(meterEntries[0]["at"]) == 0, "Preview requires an initial meter at tick zero");
    const auto numerator = integer(meterEntries[0]["numerator"]);
    const auto denominator = integer(meterEntries[0]["denominator"]);
    require(ppq > 0 && ppq <= std::numeric_limits<int>::max() && numerator > 0 && numerator <= std::numeric_limits<int>::max()
            && denominator > 0 && duration > 0, "Invalid OSTF meter or duration");
    require(ppq <= std::numeric_limits<juce::int64>::max() / 4 / numerator, "OSTF meter exceeds preview tick range");
    const auto ticksPerWholeBars = ppq * numerator * 4;
    require(ticksPerWholeBars % denominator == 0, "Preview requires an integral number of ticks per bar");
    const auto ticksPerBar = ticksPerWholeBars / denominator;
    require(ticksPerBar > 0, "Invalid OSTF bar length");
    const auto completeBars = (duration - 1) / ticksPerBar;
    require(completeBars <= std::numeric_limits<int>::max() - 2
            && completeBars < std::numeric_limits<juce::int64>::max() / ticksPerBar,
            "The score exceeds the supported timeline range.");

    auto out = std::make_unique<OstfpPreview>();
    out->sourceVersion = score["version"].toString();
    readCompChoices(score,*out);
    out->documentId = score["document"]["id"].toString();
    out->durationTicks = duration;
    out->ticksPerBar = ticksPerBar;
    out->ppq = ppq;
    require(denominator <= std::numeric_limits<int>::max(), "Unsupported meter denominator");
    out->meterBeats = static_cast<int>(numerator);
    out->beatUnit = static_cast<int>(denominator);
    out->clock = TempoMap(ppq);
    out->title = score["song"]["title"].toString();
    if (out->title.isEmpty()) out->title = juce::translate("Untitled");
    out->bars.endBar = static_cast<int>(completeBars) + 2;
    auto addLane = [&](std::vector<ArrangementLane>& lanes, juce::String title, std::vector<ArrangementRegion> regions,
                       TimelineLane kind = TimelineLane::none) {
        out->regions.push_back(std::move(regions));
        lanes.push_back({out->keepLabel(std::move(title)), out->regions.back(), kind});
    };
    std::map<std::pair<juce::String,juce::String>,juce::uint32> colours;
    if (auto* values=score["presentation"]["appearance"]["objectColors"].getArray())
        for (const auto& item:*values) {
            const auto hex=item["color"].toString();
            if(hex.length()==7 && hex.startsWithChar('#')) colours[{item["target"]["type"].toString(),item["target"]["id"].toString()}]=
                0xff000000u | juce::uint32(hex.substring(1).getHexValue32());
        }
    auto eventColour=[&](const juce::String& type,const juce::var& id) -> juce::uint32 {
        const auto found=colours.find({type,id.toString()}); return found==colours.end()?0:found->second;
    };
    auto intervals = [&](const juce::var& value, const char* labelField, bool countNotes = false, const char* colourType=nullptr) {
        std::vector<ArrangementRegion> result;
        if (value.isVoid()) return result;
        for (const auto& event : array(value)) {
            if (countNotes && !bool(event.getProperty("enabled",true))) continue;
            const auto start = integer(event["start"]);
            const auto length = integer(event["duration"]);
            require(start >= 0 && start < duration && length > 0 && length <= duration - start, "Invalid OSTF region duration");
            const auto finish = start + length;
            const auto firstBar = static_cast<int>(start / ticksPerBar) + 1;
            const auto endBar = static_cast<int>((finish - 1) / ticksPerBar) + 2;
            const TimelineExtent position{1.0 + static_cast<double>(start) / ticksPerBar, 1.0 + static_cast<double>(finish) / ticksPerBar};
            auto label = countNotes ? event["name"].toString() : juce::String{};
            if (label.isEmpty()) label = event[labelField].toString();
            const auto notes = countNotes ? event["notes"] : juce::var{};
            result.push_back({{firstBar, endBar}, out->keepLabel(label.isNotEmpty() ? label : event["id"].toString()),
                              notes.isArray() ? array(notes).size() : 0, position});
            if(colourType) result.back().colour=eventColour(colourType,event["id"]);
            if (countNotes) {
                const auto id = event["id"].toString();
                result.back().clipId = out->keepLabel(id);
                const auto extension = score["extensions"][loopNamespace];
                if (extension["version"] == "1.0" && extension["data"].hasProperty(id)) {
                    const auto period = integer(extension["data"].getProperty(id, 0));
                    if (period > 0 && period <= length) result.back().loopTicks = period;
                }
            }
        }
        return result;
    };

    addLane(out->globalLanes, "SECTION", intervals(timeline["sections"], "name",false,"section"), TimelineLane::section);
    const auto chords = score["harmony"]["chords"];
    const auto chordRegions = intervals(chords, "label",false,"chord");
    addLane(out->globalLanes, "CHORD", chordRegions, TimelineLane::chord);
    std::vector<ArrangementRegion> voicings;
    std::vector<PitchedNote> voicingNotes;
    if (chords.isArray()) {
        const auto& entries = array(chords);
        for (int i = 0; i < entries.size(); ++i) {
            const auto voicing = entries[i]["voicing"];
            const auto pitches = voicing["pitches"];
            if (!pitches.isArray()) continue;
            const auto velocities = voicing["velocities"];
            juce::StringArray values;
            const auto start = integer(entries[i]["start"]);
            const auto end = start + integer(entries[i]["duration"]);
            const auto& notes = array(pitches);
            for (int voice = 0; voice < notes.size(); ++voice) {
                const auto pitch = integer(notes[voice]);
                require(pitch >= 0 && pitch <= 127, "Invalid chord voicing pitch");
                std::optional<int> velocity;
                if (velocities.isArray() && voice < velocities.size() && !velocities[voice].isVoid()) {
                    const auto value = integer(velocities[voice]);
                    require(value >= 1 && value <= 127, "Invalid chord voicing velocity");
                    velocity = static_cast<int>(value);
                }
                constexpr const char* pitchClasses[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                values.add(juce::String(pitchClasses[pitch % 12]) + juce::String(pitch / 12 - 1));
                voicingNotes.push_back({start, end, static_cast<int>(pitch), velocity});
            }
            if (!values.isEmpty())
                voicings.push_back({chordRegions[static_cast<std::size_t>(i)].bars, out->keepLabel(values.joinIntoString("  ")),
                                    0, chordRegions[static_cast<std::size_t>(i)].exactBars, false, nullptr, 0, out->keepLabel(entries[i]["id"].toString())});
        }
    }
    std::stable_sort(voicingNotes.begin(), voicingNotes.end(), [](const auto& a, const auto& b) { return a.startTick < b.startTick; });
    out->chordVoicings = static_cast<int>(voicings.size());

    auto region = [&](juce::int64 start, juce::int64 end, const juce::String& label) -> ArrangementRegion {
        return {{static_cast<int>(start / ticksPerBar + 1), static_cast<int>((end - 1) / ticksPerBar + 2)},
                out->keepLabel(label), 0, TimelineExtent{1.0 + double(start) / ticksPerBar, 1.0 + double(end) / ticksPerBar}};
    };
    auto keyName = [](const juce::var& value) {
        auto label = value["label"].toString();
        if (label.isNotEmpty()) return label;
        if (value["state"].toString() != "tonal") return value["state"].toString();
        const auto tonic = value["tonic"];
        const int alter = std::clamp(static_cast<int>(tonic["alter"]), -2, 2);
        return tonic["step"].toString() + juce::String::repeatedString(alter < 0 ? "b" : "#", std::min(2, std::abs(alter)))
            + " " + value["mode"].toString();
    };
    std::vector<ArrangementRegion> keys;
    auto key = keyName(score["song"]["key"]);
    out->camelotKeys.emplace_back(0,camelotCode(score["song"]["key"]));
    juce::int64 keyStart = 0;
    if (const auto keyChanges = timeline["keyChanges"]; keyChanges.isArray()) {
        for (const auto& change : array(keyChanges)) {
            const auto tick = integer(change["at"]);
            if (tick < keyStart || tick >= duration) continue;
            if (tick > keyStart && key.isNotEmpty()) keys.push_back(region(keyStart, tick, key));
            keyStart = tick;
            key = keyName(change["key"]);
            out->camelotKeys.emplace_back(tick,camelotCode(change["key"]));
            if (key.isEmpty()) key = change["label"].toString();
        }
    }
    if (key.isNotEmpty()) keys.push_back(region(keyStart, duration, key));
    if (!keys.empty()) addLane(out->globalLanes, "KEY", std::move(keys), TimelineLane::key);

    std::vector<ArrangementRegion> scales;
    const auto songScale = score["song"]["scale"];
    out->defaultScale = songScale.getProperty("label", songScale["id"]).toString();
    if (const auto timed = score["harmony"]["scales"]; timed.isArray()) {
        for (const auto& entry : array(timed)) {
            const auto start = integer(entry["start"]);
            const auto length = integer(entry["duration"]);
            if (start < 0 || start >= duration || length <= 0 || length > duration - start) continue;
            auto label = entry["label"].toString();
            if (label.isEmpty()) label = entry["id"].toString();
            auto item = region(start, start + length, label);
            item.colour=eventColour("scale",entry["id"]);
            item.isScoped = entry.hasProperty("targets");
            scales.push_back(item);
        }
    }
    if (scales.empty()) {
        const auto scale = score["song"]["scale"]["label"].toString();
        if (scale.isNotEmpty()) scales.push_back({out->bars, out->keepLabel(scale)});
    }
    if (!scales.empty()) addLane(out->globalLanes, "SCALE", std::move(scales), TimelineLane::scale);
    std::vector<ArrangementRegion> meterRegions;
    for (int i = 0; i < meterEntries.size(); ++i) {
        const auto& change = meterEntries[i];
        const auto at = integer(change["at"]), n = integer(change["numerator"]), d = integer(change["denominator"]);
        const auto end = i + 1 < meterEntries.size() ? integer(meterEntries[i+1]["at"]) : duration;
        require(n > 0 && n <= INT_MAX && d > 0 && d <= INT_MAX && at >= 0 && at < end && end <= duration, "Invalid meter change.");
        const bool sameMeter=i>0 && integer(meterEntries[i-1]["numerator"])==n && integer(meterEntries[i-1]["denominator"])==d;
        require(n * denominator == numerator * d && (sameMeter || at % ticksPerBar == 0),
                "Preview requires meter changes to preserve bar length at bar boundaries.");
        // A cut creates an editable boundary. Starting a new metric clock there would move the downbeat.
        if (!sameMeter || at % ticksPerBar == 0) out->meters.push_back({at,int(n),int(d)});
        meterRegions.push_back(region(at, end, juce::String(n) + "/" + juce::String(d)));
    }
    addLane(out->globalLanes, "METER", std::move(meterRegions), TimelineLane::meter);

    const auto tempo = timeline["tempoChanges"];
    const auto& changes = array(tempo);
    if (changes.size() == 1 && integer(changes[0]["at"]) == 0)
        out->previewBpm = static_cast<double>(changes[0]["bpm"]);
    std::vector<ArrangementRegion> tempos;
    for (int i = 0; i < changes.size(); ++i) {
        const auto first = integer(changes[i]["at"]);
        const auto last = i + 1 < changes.size() ? integer(changes[i + 1]["at"]) : duration;
        require(first < last && last <= duration && out->clock.append(first, static_cast<double>(changes[i]["bpm"])),
                "Invalid OSTF tempo map");
        tempos.push_back(region(first, last, changes[i]["bpm"].toString()));
    }
    addLane(out->globalLanes, "BPM", std::move(tempos), TimelineLane::tempo);

    const auto trackData = score["tracks"];
    const auto& tracks = array(trackData);
    require(static_cast<std::size_t>(tracks.size()) <= OstfpPreview::maxTracks, "Preview supports at most fifteen source tracks");
    std::vector<std::pair<juce::String, int>> drumMappings;
    if (const auto slots = score["soundSlots"]; slots.isArray()) {
        for (const auto& slot : array(slots)) {
            const auto mapping = slot["midiMapping"];
            if ((mapping["standard"] == "gm1" || mapping["standard"] == "custom") && mapping["note"].isInt()) {
                const auto pitch = static_cast<int>(mapping["note"]);
                if (pitch >= 0 && pitch <= 127) drumMappings.emplace_back(slot["id"].toString(), pitch);
            }
        }
    }
    std::map<juce::String, juce::var> lyricUnits;
    const auto lyrics = score["lyrics"];
    if (lyrics["units"].isArray())
        for (const auto& unit : array(lyrics["units"])) lyricUnits.emplace(unit["id"].toString(), unit);
    if (lyrics["alignments"].isArray())
        for (const auto& alignment : array(lyrics["alignments"])) {
            for (const auto& note : array(alignment["noteIds"])) {
                auto& label = out->vocalNotes[note.toString()];
                for (const auto& id : array(alignment["unitIds"])) {
                    const auto unit = lyricUnits.find(id.toString());
                    if (unit == lyricUnits.end()) continue;
                    const auto text = unit->second["text"].toString();
                    const auto reading = unit->second["pronunciation"].toString().trim();
                    label.text += text;
                    label.pronunciation += reading.isNotEmpty() ? reading : text;
                }
            }
        }
    for (const auto& track : tracks) {
        addLane(out->trackLanes, track["name"].toString(), intervals(track["clips"], "id", true));
        out->trackKinds.push_back(track["trackType"] == "drum" ? TrackKind::drum : TrackKind::pitched);
        const auto role = track["role"].toString();
        const auto primary = score["song"]["primaryMelodyTrackId"].toString();
        if ((!primary.isEmpty() && track["id"].toString() == primary) || (primary.isEmpty() && !out->primaryMelodyTrack && (role.equalsIgnoreCase("melody") || role.equalsIgnoreCase("lead"))))
            out->primaryMelodyTrack = out->trackIds.size();
        auto& group = out->mixerGroups.emplace_back(
                               role.equalsIgnoreCase("vocal") || role.equalsIgnoreCase("vocals") ? MixerTrackGroup::vocal
                             : role.equalsIgnoreCase("melody") || role.equalsIgnoreCase("lead") ? MixerTrackGroup::melody
                             : MixerTrackGroup::other);
        if (out->primaryMelodyTrack == static_cast<size_t>(out->trackIds.size())) group = MixerTrackGroup::melody;
        auto& notes = out->pitchedNotes.emplace_back();
        auto& drums = out->drumNotes.emplace_back();
        auto& programs = out->programChanges.emplace_back();
        const auto midiSetup = track["midi"];
        const auto sourceChannel = static_cast<int>(midiSetup["channel"]);
        out->sourceMidiChannels.push_back(midiSetup["mode"] == "singleChannel" && sourceChannel >= 1 && sourceChannel <= 16
                                          ? sourceChannel : (track["trackType"] == "drum" ? 10 : 1));
        auto& curves = out->performanceCurves.emplace_back();
        const auto clips = track["clips"];
        out->trackIds.add(track["id"].toString());
        const auto takeData=score["extensions"][StudioMidiTakes::extension];
        if (takeData["version"]=="1.0" && takeData["data"].hasProperty(track["id"].toString()))
            out->midiTakes.emplace(track["id"].toString(),StudioMidiTakes::decode(takeData["data"].getProperty(track["id"].toString(), {})));
        auto& slotList = out->slotsByTrack.emplace_back();
        auto& allHits = out->slotHits.emplace_back();
        if (track["trackType"] == "drum" && score["soundSlots"].isArray()) {
            juce::var members;
            if (track.hasProperty("drumKitId") && score["drumKits"].isArray())
                for (const auto& kit : array(score["drumKits"])) if (kit["id"] == track["drumKitId"]) members = kit["soundSlotIds"];
            const auto appendSlot = [&](const juce::var &s) {
                const auto m = s["midiMapping"];
                const int pitch = (m["standard"] == "gm1" || m["standard"] == "custom") ? int(m["note"]) : -1;
                slotList.push_back({s["id"].toString(), s.getProperty("name", s["id"]).toString(), pitch});
            };
            if (members.isArray()) {
                for (const auto &id : array(members))
                    for (const auto &s : array(score["soundSlots"]))
                        if (s["id"] == id) {
                            appendSlot(s);
                            break;
                        }
            } else
                for (const auto &s : array(score["soundSlots"]))
                    appendSlot(s);
        }
        if (track["trackType"] == "drum" && !track.hasProperty("drumKitId")) {
            for (const auto &suggested : defaultPads)
                if (slotList.size() < 16 && std::none_of(slotList.begin(), slotList.end(),
                                                         [&](const auto &s) { return s.pitch == suggested.pitch; }))
                    slotList.push_back(suggested);
        }
        if (clips.isVoid()) continue;
        size_t ordinal = 0;
        for (const auto& clip : array(clips)) {
            if (!bool(clip.getProperty("enabled", true))) continue;
            const auto clipStart = integer(clip["start"]);
            const auto events = clip["notes"];
            if (events.isArray()) for (const auto& event : array(events)) {
                const auto slot = event["soundSlotId"].toString();
                const auto drum = std::find_if(drumMappings.begin(), drumMappings.end(), [&](const auto& mapping) { return mapping.first == slot; });
                const bool pitched = !event["pitch"].isVoid();

                const auto pitch = pitched ? integer(event["pitch"]) : drum == drumMappings.end() ? -1 : drum->second;
                const auto local = integer(event["start"]);
                const auto length = integer(event["duration"]);
                require(pitch >= (pitched ? 0 : -1) && pitch <= 127 && local >= -clipStart && local < duration - clipStart && length > 0
                        && length <= duration - clipStart - local, "Invalid OSTF pitched note");
                std::optional<int> velocity;
                if (!event["velocity"].isVoid()) {
                    const auto value = integer(event["velocity"]);
                    require(value >= 1 && value <= 127, "Invalid OSTF note velocity");
                    velocity = static_cast<int>(value);
                }
                PitchedNote note {clipStart + local, clipStart + local + length, static_cast<int>(pitch), velocity,
                                  event["enabled"].isVoid() || static_cast<bool>(event["enabled"]), event["id"].toString(),
                                  event["pitchRole"].toString() == "fixedTrigger", clip["id"].toString(), slot};
                if (pitched && out->vocalNotes.contains(note.id)) out->lyricTrackIds.addIfNotAlreadyThere(track["id"].toString());
                if (!pitched) allHits.push_back(note);
                if (pitch >= 0) (pitched ? notes : drums).push_back(note);
            }
            const auto midi = clip["midiEvents"];
            if (midi.isArray()) for (const auto& event : array(midi)) {
                ++ordinal;
                const auto type = event["type"].toString();
                if (type == "programChange") {
                    const auto time = integer(event["start"]);
                    const auto number = integer(event["program"]);
                    require(time >= -clipStart && time < duration - clipStart && number >= 0 && number <= 127,
                            "Invalid OSTF MIDI program change");
                    std::optional<int> channel;
                    if (!event["channel"].isVoid()) {
                        const auto value = integer(event["channel"]);
                        require(value >= 1 && value <= 16, "Invalid OSTF MIDI channel");
                        channel = static_cast<int>(value);
                    }
                    if (bool(clip.getProperty("enabled", true))) programs.push_back({clipStart + time, static_cast<int>(number), channel, integer(event.getProperty("order", 1)), ordinal});
                }
                if (type != "programChange" && type != "pitchBend" && type != "controlChange" && type != "channelPressure" && type != "polyPressure") continue;
                MidiControl key{type, int(event[type == "controlChange" ? "controller" : "pitch"]), int(event["channel"])};
                auto label = key.label();
                const auto time = integer(event["start"]);
                const int value = int(event[type == "programChange" ? "program" : type == "pitchBend" ? "value14" : "value"]);
                if (time < -clipStart || time >= duration - clipStart || value < 0 || value > key.maximum()) continue;
                const bool enabled = bool(clip.getProperty("enabled", true));
                auto lane = std::find_if(curves.begin(), curves.end(), [&](const auto& c) { return c.label == label && c.enabled == enabled; });
                if (lane == curves.end()) lane = curves.insert(curves.end(), {label, {}, false, true, false, key, enabled});
                lane->points.push_back({clipStart + time, double(value) / key.maximum(), event["id"].toString(), integer(event.getProperty("order", 1)), ordinal});
            }
            const auto vocal = clip["vocalPerformances"];
            if (vocal.isArray()) for (const auto& voice : array(vocal)) {
                if (voice["techniques"].isArray()) for (const auto& tech : array(voice["techniques"])) {
                    const auto id = tech["targetNoteId"].toString();
                    if (id.isEmpty()) continue;
                    auto& label = out->vocalNotes[id];
                    const auto kind = tech["kind"].toString();
                    if (label.technique.isEmpty() || kind == "scoop" || kind == "vibrato") {
                        label.technique = kind;
                        label.intensity = double(tech.getProperty("intensity", 0.5));
                    }
                }
                const auto voiceLabel = "Vocal / " + voice["voiceId"].toString() + " / ";
                for (const auto* field : {"pitchCurves", "expressionCurves"}) {
                    const auto data = voice[field];
                    if (!data.isArray()) continue;
                    const bool pitch = juce::String(field) == "pitchCurves";
                    for (const auto& curve : array(data)) {
                        PerformanceCurve line{voiceLabel + (pitch ? juce::String("Pitch (cents)") : curve["parameter"].toString()),
                                              {}, true, curve["interpolation"].toString() == "step", pitch};
                        const auto points = curve["points"];
                        if (!points.isArray()) continue;
                        for (const auto& point : array(points)) {
                            const auto time = integer(point["at"]);
                            const auto value = static_cast<double>(point["value"]);
                            if (time >= -clipStart && time < duration - clipStart && std::isfinite(value)
                                && (pitch || (value >= 0.0 && value <= 1.0)))
                                line.points.push_back({clipStart + time, value});
                        }
                        if (!line.points.empty()) curves.push_back(std::move(line));
                    }
                }
            }
        }
        for (auto& curve : curves)
            std::stable_sort(curve.points.begin(), curve.points.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
        if (group == MixerTrackGroup::other && !notes.empty()) group = MixerTrackGroup::pitched;
    }
    // The lyric document can be displayed even when no singing instrument or notes exist.
    out->displayDocument = score["song"]["primaryLyricDocumentId"].toString();
    if (lyrics["voices"].isArray())
        for (const auto& voice : array(lyrics["voices"]))
            out->trackLyricDocuments[voice["trackId"].toString()].addIfNotAlreadyThere(voice["documentId"].toString());
    if (out->displayDocument.isEmpty() && out->primaryMelodyTrack) {
        const auto docs = out->trackLyricDocuments.find(out->trackIds[int(*out->primaryMelodyTrack)]);
        if (docs != out->trackLyricDocuments.end()) out->displayDocument = docs->second[0];
    }
    if (out->displayDocument.isEmpty() && lyrics["documents"].size())
        out->displayDocument = lyrics["documents"][0]["id"].toString();
    std::map<juce::String, juce::Range<juce::int64>> noteRanges, phraseRanges;
    for (const auto& part : out->pitchedNotes)
        for (const auto& note : part)
            if (out->vocalNotes.contains(note.id)) noteRanges[note.id] = {note.startTick,note.endTick};
    auto extendPhrase = [&](const juce::var& unit, juce::Range<juce::int64> range) {
        auto& span = phraseRanges[unit["phraseId"].toString()];
        span = span.isEmpty() ? range : span.getUnionWith(range);
    };
    for (const auto& [id, unit] : lyricUnits)
        if (unit.hasProperty("start"))
            extendPhrase(unit, juce::Range<juce::int64>::withStartAndLength(integer(unit["start"]), integer(unit["duration"])));
    if (lyrics["alignments"].isArray())
        for (const auto& alignment : array(lyrics["alignments"]))
            for (const auto& unitId : array(alignment["unitIds"])) {
                const auto unit = lyricUnits.find(unitId.toString());
                if (unit == lyricUnits.end() || unit->second.hasProperty("start")) continue;
                for (const auto& id : array(alignment["noteIds"]))
                    if (auto n = noteRanges.find(id.toString()); n != noteRanges.end()) extendPhrase(unit->second,n->second);
            }
    if (lyrics["phrases"].isArray())
        for (const auto& phrase : array(lyrics["phrases"])) {
            auto span = phraseRanges[phrase["id"].toString()];
            if (phrase.hasProperty("start"))
                span = juce::Range<juce::int64>::withStartAndLength(integer(phrase["start"]), integer(phrase["duration"]));
            if (!span.isEmpty() && phrase["text"].toString().isNotEmpty())
                out->displayPhrases.push_back({span.getStart(),span.getEnd(),phrase["text"].toString(),
                                               phrase["pronunciation"].toString(),phrase["documentId"].toString()});
        }
    std::stable_sort(out->displayPhrases.begin(),out->displayPhrases.end(),[](const auto& a,const auto& b){return a.start < b.start;});
    // The written lyric is a lead-sheet cell. A late vocal entrance changes its
    // pronunciation timing, while its text still belongs to the enclosing chord.
    std::vector<juce::Range<juce::int64>> chordSpans;
    if (chords.isArray()) for (const auto& chord : array(chords)) {
        chordSpans.push_back(juce::Range<juce::int64>::withStartAndLength(integer(chord["start"]), integer(chord["duration"])));
    }
    std::stable_sort(chordSpans.begin(), chordSpans.end(), [](auto a, auto b) { return a.getStart() < b.getStart(); });
    auto writtenRegions = [&](auto includesDocument) {
        std::vector<ArrangementRegion> cells;
        std::vector<juce::StringArray> captions(chordSpans.size());
        for (const auto& phrase : out->displayPhrases) {
            if (!includesDocument(phrase.document)) continue;
            auto next = std::upper_bound(chordSpans.begin(), chordSpans.end(), phrase.start,
                [](auto tick, auto span) { return tick < span.getStart(); });
            if (next != chordSpans.begin() && std::prev(next)->contains(phrase.start)) {
                captions[static_cast<size_t>(std::prev(next) - chordSpans.begin())].add(phrase.text);
            } else {
                const auto end = next == chordSpans.end() ? phrase.end : std::min(phrase.end, next->getStart());
                cells.push_back(region(phrase.start, end, phrase.text));
            }
        }
        for (size_t i = 0; i < chordSpans.size(); ++i)
            cells.push_back(region(chordSpans[i].getStart(), chordSpans[i].getEnd(), captions[i].joinIntoString("  ")));
        std::stable_sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) {
            return a.exactBars->start < b.exactBars->start;
        });
        return cells;
    };
    bool primaryDocumentShown = false;
    for (size_t track = 0; track < out->trackLanes.size(); ++track) {
        const auto& id = out->trackIds[int(track)];
        if (!out->isGlobalNoteTrack(track) && !out->trackLyricDocuments.contains(id)
            && !out->lyricTrackIds.contains(id)) continue;
        std::vector<ArrangementRegion> readings;
        auto words = writtenRegions([&](const auto& doc) { return out->partDisplaysDocument(track, doc); });
        for (const auto& note : out->pitchedNotes[track]) {
            const auto found = out->vocalNotes.find(note.id);
            if (found == out->vocalNotes.end() || found->second.pronunciation.isEmpty()) continue;
            readings.push_back(region(note.startTick, note.endTick, found->second.pronunciation));
        }
        auto& lanes = out->partLyricLanes[track];
        addLane(lanes, "Display lyrics", std::move(words), TimelineLane::displayLyrics);
        addLane(lanes, "Pronunciation", std::move(readings), TimelineLane::pronunciation);
        primaryDocumentShown |= out->partDisplaysDocument(track, out->displayDocument);
    }
    if (!primaryDocumentShown && out->displayDocument.isNotEmpty()) {
        auto words = writtenRegions([&](const auto& doc) { return doc == out->displayDocument; });
        if (!words.empty()) addLane(out->globalLanes, "Display lyrics", std::move(words), TimelineLane::displayLyrics);
    }
    if (!voicingNotes.empty() || retainVoicingRow) {
        out->chordVoicingTrack = out->trackLanes.size();
        addLane(out->trackLanes, "Chord Voicing", std::move(voicings), TimelineLane::voicing);
        out->pitchedNotes.push_back(std::move(voicingNotes));
        out->drumNotes.emplace_back();
        out->programChanges.emplace_back();
        out->sourceMidiChannels.push_back(1);
        out->performanceCurves.emplace_back();
        out->trackKinds.push_back(TrackKind::pitched);
        out->mixerGroups.push_back(MixerTrackGroup::voicing);
    }
    for(const auto& [target,rgb]:colours) if(target.first=="track") out->trackColoursById[target.second]=rgb;
    return out;
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto value = std::make_unique<juce::DynamicObject>();
    for (const auto& [key, entry] : fields) value->setProperty(key, entry);
    return juce::var(value.release());
}

juce::MemoryBlock jsonBytes(const juce::var& value) {
    const auto text = juce::JSON::toString(value, false, 17) + "\n";
    return {text.toRawUTF8(), text.getNumBytesAsUTF8()};
}

// Mirror edits to a whole-take output into its independent source. More complex
// maps can switch between their saved outputs without rewriting their source data.
bool syncCompNotes(const juce::var& previous, juce::var edited) {
    if (!previous["takeGroups"].isArray()) return false;
    bool changed=false;
    for (const auto& group : array(edited["takeGroups"])) {
        auto clipsFor=[&](const juce::var& song) {
            std::map<juce::String,juce::var> result;
            for (const auto& track : array(song["tracks"])) if(track["id"]==group["trackId"])
                for (const auto& clip : array(track["clips"])) result.emplace(clip["id"].toString(),clip);
            return result;
        };
        auto old=clipsFor(previous), clips=clipsFor(edited);
        std::map<juce::String,int> uses;
        for(const auto& comp:array(group["comps"])) for(const auto& r:array(comp["compRegions"]))
            if(r["kind"]=="source") ++uses[r["clipId"].toString()];
        for(const auto& [id,count]:uses)
            require(old.contains(id) && clips.contains(id) && juce::JSON::toString(old.at(id))==juce::JSON::toString(clips.at(id)),
                    "Edit the selected comp output to keep the original take intact.");
        for(auto comp:array(group["comps"])) for(const auto& id:array(comp["outputClipIds"])) {
            const auto key=id.toString();
            require(old.contains(key) && clips.contains(key),"Removing a comp output requires editing its take group.");
            const auto out=clips.at(key);
            if(juce::JSON::toString(out)==juce::JSON::toString(old.at(key))) continue;
            juce::var region;
            for(const auto& r:array(comp["compRegions"])) if (r["kind"]=="source") {
                require(!region.isObject(),"This edited output combines multiple source intervals."); region=r;
            }
            const auto src=region["clipId"].toString();
            require(group["activeCompId"]==comp["id"] && comp["outputClipIds"].size()==1 && uses[src]==1
                && integer(region["startTick"])==integer(out["start"]) && integer(region["durationTicks"])==integer(out["duration"]),
                "This operation would invalidate an authored comp map.");
            auto source=clips.at(src);
            require(source["start"]==out["start"] && source["duration"]==out["duration"] && !source.hasProperty("vocalPerformances"),
                    "This comp requires coordinated source performance edits.");
            auto unedited=out.clone();
            unedited.getDynamicObject()->setProperty("notes",old.at(key)["notes"]);
            require(juce::JSON::toString(unedited)==juce::JSON::toString(old.at(key)),"Edit only the notes of this whole take.");
            std::map<juce::String,juce::String> links;
            for(int i=0;i<comp["noteLinks"].size();++i) {
                const auto link=comp["noteLinks"][i];
                links.emplace(link["outputNoteId"].toString(),link["sourceNoteId"].toString());
            }
            require(size_t(old.at(key)["notes"].size())==links.size(),"This comp lacks a one-to-one note map.");
            juce::Array<juce::var> notes,newLinks;
            for(const auto& note:array(out["notes"])) {
                const auto outputId=note["id"].toString();
                const auto sourceId=links.contains(outputId)?links.at(outputId):"event-"+juce::Uuid().toString();
                const auto onset=integer(note["start"]);
                require(onset>=0 && onset+integer(note["duration"])<=integer(out["duration"]), "A whole-take note crosses the group boundary.");
                auto n=note.clone(); n.getDynamicObject()->setProperty("id",sourceId); notes.add(n);
                newLinks.add(object({{"sourceNoteId",sourceId},{"outputNoteId",outputId}}));
            }
            source.getDynamicObject()->setProperty("notes",notes);
            if(newLinks.isEmpty()) comp.getDynamicObject()->removeProperty("noteLinks");
            else comp.getDynamicObject()->setProperty("noteLinks",newLinks);
            changed=true;
        }
    }
    return changed;
}
juce::MemoryBlock editedScoreBytes(const juce::MemoryBlock& original, const juce::var& edited,
                                   std::initializer_list<std::string_view> fields) {
    bool updateMap=false;
    if (edited["takeGroups"].size() && std::find(fields.begin(),fields.end(),"tracks")!=fields.end() && std::find(fields.begin(),fields.end(),"takeGroups")==fields.end())
        updateMap=syncCompNotes(parseObject(original),edited);
    const auto changes = jsonBytes(edited);
    auto bytes = applyScoreFields({static_cast<const char*>(original.getData()), original.getSize()},
                                        {static_cast<const char*>(changes.getData()), changes.getSize()}, fields);
    if(updateMap) bytes=applyScoreFields(bytes,{static_cast<const char*>(changes.getData()),changes.getSize()},{"takeGroups"});
    return {bytes.data(), bytes.size()};
}

juce::MemoryBlock savedScore(const OstfpPreview& project, bool writing = false) {
    require(!project.sourceScore.isEmpty(), "No OSTF score to save");
    const bool upgrade = writing && project.sourceVersion != "5.0.0" && project.sourceVersion != "5.1.0";
    if (project.addedClips.empty() && !upgrade) return project.sourceScore;
    auto score = parseObject(project.sourceScore);
    if (upgrade) score.getDynamicObject()->setProperty("version", "5.0.0");
    auto tracks = score["tracks"];
    for (const auto& edit : project.addedClips) {
        auto* track = (*tracks.getArray())[static_cast<int>(edit.track)].getDynamicObject();
        auto clips = track->getProperty("clips");
        if (!clips.isArray()) clips = juce::var(juce::Array<juce::var>{});
        juce::Array<juce::var> notes;
        for (const auto& n : edit.notes) {
            auto note = object({{"id", edit.id + "-note-" + juce::String(notes.size() + 1)},
                                {"start", n.startTick - edit.start}, {"duration", n.endTick - n.startTick}, {"pitch", n.pitch}});
            if (n.velocity) note.getDynamicObject()->setProperty("velocity", *n.velocity);
            notes.add(std::move(note));
        }
        clips.getArray()->add(object({{"id", edit.id}, {"name", edit.name}, {"start", edit.start},
                                      {"duration", edit.length}, {"notes", notes}}));
        track->setProperty("clips", clips);
    }
    if (auto* document = score["document"].getDynamicObject()) {
        juce::Array<juce::var> parents;
        parents.add(object({{"documentId", document->getProperty("id")},
                            {"revisionId", document->getProperty("revisionId")},
                            {"sha256", juce::SHA256(project.sourceScore).toHexString()}}));
        document->setProperty("revisionId", project.addedClips.empty() ? "studio-" + juce::Uuid().toString() : project.addedClips.back().id);
        document->setProperty("parents", parents);
        document->setProperty("modifiedBy", object({{"name", "OSTF Studio"}}));
    }
    auto bytes = project.addedClips.empty() ? editedScoreBytes(project.sourceScore, score, {"version", "document"})
                                            : editedScoreBytes(project.sourceScore, score, {"version", "document", "tracks"});
    if (upgrade) validateVersion(parseObject(bytes)["version"].toString(), bytes);
    return bytes;
}

juce::String busId(int bus) { return bus < 0 ? "master" : "bus-" + juce::String(bus + 1); }
juce::String returnId(std::size_t index) { return "return-" + juce::String(static_cast<int>(index + 1)); }
int busIndex(const juce::String& id) {
    for (int i = 0; i < static_cast<int>(maxMixerBuses); ++i)
        if (id == busId(i)) return i;
    return -1;
}
int returnIndex(const juce::String& id) {
    for (std::size_t index = 0; index < maxMixerReturns; ++index)
        if (id == returnId(index)) return static_cast<int>(index);
    return -1;
}

juce::var mixChannel(const juce::String& id, const char* kind, double gain) {
    return object({{"id", id}, {"kind", kind}, {"audioChannels", 2}, {"gainDb", gain}, {"pan", 0.0},
                   {"mute", false}, {"solo", false}, {"soloSafe", false}, {"deviceIds", juce::Array<juce::var>{}}});
}

juce::var productionMix(const juce::var& score, const SessionMix& mix, const PanAutomation& envelopes) {
    const auto scoreTracks = score["tracks"];
    const auto& tracks = array(scoreTracks);
    const auto sourceCount = static_cast<std::size_t>(tracks.size());
    require(mix.tracks.size() == sourceCount || mix.tracks.size() == sourceCount + 1, "Mixer does not match the score");
    juce::var voicingMix;
    juce::var voicingRoute;
    juce::Array<juce::var> channels, routes, devices, sidechains;
    juce::Array<juce::var> automation;
    auto states = object({});
    auto externalKeys = object({});
    auto attachEffects = [&](juce::var& channel, bool output, std::size_t index, const juce::StringArray& chain) {
        if (output && mix.outputNames[index].isNotEmpty()) channel.getDynamicObject()->setProperty("name", mix.outputNames[index]);
        auto ids = array(channel["deviceIds"]);
        for (int slot = 0; slot < chain.size(); ++slot) {
            const auto slotId = SessionMix::effectSlot(output,index,slot);
            if (auto source = mix.sidechainTracks.find(slotId); source != mix.sidechainTracks.end()
                && std::none_of(std::begin(stockEffects),std::end(stockEffects),[&](const auto& fx){return fx.chainId()==chain[slot];}))
                externalKeys.getDynamicObject()->setProperty(slotId, source->second);
            for (const auto& fx : stockEffects) {
                if (fx.chainId() != chain[slot]) continue;
                const auto id = SessionMix::effectSlot(output, index, slot);
                ids.add(id);
                if (const auto values = mix.insertParameters.find(id); values != mix.insertParameters.end())
                    for (const auto& [parameter, value] : values->second)
                        automation.add(object({{"id", id + "-p-" + parameter}, {"target", object({{"kind","device"},{"deviceId",id},{"parameterId",parameter}})},
                            {"valueType","number"},{"interpolation","step"},{"points",juce::Array<juce::var>{object({{"atTick",0},{"value",value}})}}}));
                if (const auto it=mix.sidechainTracks.find(id); it!=mix.sidechainTracks.end()) {
                    const auto source=std::find_if(tracks.begin(),tracks.end(),[&](const auto& t){return t["id"].toString()==it->second;});
                    if (source!=tracks.end())
                        sidechains.add(object({{"id","key-"+id},{"sourceChannelId","track-"+juce::String(int(source-tracks.begin())+1)},
                            {"targetDeviceId",id},{"targetPortId","vst3-aux-1"},{"tapPoint","preFader"},
                            {"followMute",false},{"gainDb",0.0},{"enabled",true}}));
                }
                devices.add(object({{"id", id}, {"role", "effect"}, {"enabled", true}, {"bypass", false},
                    {"onUnavailable", "reportAndPreserve"}, {"plugin", object({{"format", "vst3"}, {"vendor", "OSTF Studio"},
                        {"name", fx.displayName()}, {"pluginId", fx.classId()}})}}));
                if (const auto state = mix.stockEffectStates.find(id); state != mix.stockEffectStates.end())
                    states.getDynamicObject()->setProperty(id, state->second);
                break;
            }
        }
        channel.getDynamicObject()->setProperty("deviceIds", ids);
    };
    auto master = mixChannel("master", "master", mix.routing.masterDb);
    attachEffects(master, true, 0, mix.outputEffects[0]);
    master.getDynamicObject()->setProperty("mute", mix.routing.outputMuted[0]);
    channels.add(master);
    for (int bus = 0; bus < static_cast<int>(mix.routing.busCount); ++bus) {
        const auto slot = busOutput(static_cast<std::size_t>(bus));
        auto channel = mixChannel(busId(bus), "bus", mix.routing.busDb[static_cast<std::size_t>(bus)]);
        attachEffects(channel, true, slot, mix.outputEffects[slot]);
        channel.getDynamicObject()->setProperty("mute", mix.routing.outputMuted[slot]);
        channel.getDynamicObject()->setProperty("outputChannelId", "master");
        channels.add(std::move(channel));
    }
    for (std::size_t i = 0; i < mix.routing.returnCount && i < maxMixerReturns; ++i) {
        auto channel = mixChannel(returnId(i), "bus", mix.routing.returnDb[i]);
        attachEffects(channel, true, 3 + i, mix.outputEffects[3 + i]);
        channel.getDynamicObject()->setProperty("mute", mix.routing.outputMuted[3 + i]);
        channel.getDynamicObject()->setProperty("outputChannelId", "master");
        channels.add(std::move(channel));
    }
    for (int i = 0; i < static_cast<int>(mix.tracks.size()); ++i) {
        const bool isVoicing = i == tracks.size();
        const auto id = isVoicing ? juce::String("studio-chord-voicing") : tracks[i]["id"].toString();
        require(id.isNotEmpty(), "A track is missing its OSTF ID");
        const auto index = static_cast<std::size_t>(i);
        const auto& settings = mix.tracks[index];
        const auto midi = index < mix.midiRoutes.size() ? mix.midiRoutes[index] : SessionMix::MidiRoute{};
        auto channel = mixChannel("track-" + juce::String(i + 1), "track", settings.volumeDb);
        auto* model = channel.getDynamicObject();
        model->setProperty("trackId", id);
        model->setProperty("pan", static_cast<double>(settings.pan));
        model->setProperty("mute", settings.muted);
        model->setProperty("solo", settings.soloed);
        model->setProperty("outputChannelId", busId(settings.outputBus));
        juce::Array<juce::var> sends;
        for (std::size_t r = 0; r < mix.routing.returnCount && r < maxMixerReturns; ++r)
            if (settings.returnSends[r] > 0.0f)
                sends.add(object({{"id", "send-" + juce::String(i + 1) + "-" + juce::String(static_cast<int>(r + 1))},
                                  {"targetChannelId", returnId(r)}, {"gainDb", 20.0 * std::log10(settings.returnSends[r])},
                                  {"preFader", false}, {"enabled", true}}));
        if (!isVoicing && index < mix.instruments.size() && mix.instruments[index].isObject()) {
            const auto device = "instrument-" + juce::String(i + 1);
            model->setProperty("deviceIds", juce::Array<juce::var>{device});
            devices.add(object({{"id", device}, {"role", "instrument"}, {"plugin", mix.instruments[index]},
                                {"enabled", true}, {"bypass", false}, {"onUnavailable", "reportAndPreserve"}}));
        }
        model->setProperty("sends", sends);
        if (index < mix.trackEffects.size()) {
            if (!isVoicing) attachEffects(channel, false, index, mix.trackEffects[index]);
            else for (int slot = 0; slot < mix.trackEffects[index].size(); ++slot) {
                const auto key = SessionMix::effectSlot(false, index, slot);
                if (const auto state = mix.stockEffectStates.find(key); state != mix.stockEffectStates.end())
                    states.getDynamicObject()->setProperty(key, state->second);
            }
        }
        if (isVoicing) voicingMix = channel;
        else channels.add(std::move(channel));
        auto local = object({{"trackId", id}, {"midiChannel", midi.channel}, {"cue", settings.cue}});
        if (midi.destination && *midi.destination < sourceCount)
            local.getDynamicObject()->setProperty("destinationTrackId", tracks[static_cast<int>(*midi.destination)]["id"]);
        if (index < mix.audioTracks.size() && mix.audioTracks[index])
        {
            local.getDynamicObject()->setProperty("audioTrack", true);
            if (auto route = mix.audioInputs.find(id); route != mix.audioInputs.end()) {
                local.getDynamicObject()->setProperty("inputFirstChannel", route->second.first);
                local.getDynamicObject()->setProperty("inputGainDb", route->second.gainDb);
            }
            if (const auto found = mix.captureChannels.find(id); found != mix.captureChannels.end())
                local.getDynamicObject()->setProperty("captureChannels", found->second);
        }
        if (index < mix.trackEffects.size()) {
            juce::Array<juce::var> fx;
            for (const auto& id : mix.trackEffects[index]) fx.add(id);
            local.getDynamicObject()->setProperty("effects", fx);
        }
        if (isVoicing) voicingRoute = local;
        else routes.add(std::move(local));
    }
    juce::Array<juce::var> hostData, inserts;
    for (const auto& id : mix.insertIds) inserts.add(id);
    juce::Array<juce::var> outputEffects;
    for (const auto& chain : mix.outputEffects) {
        juce::Array<juce::var> fx;
        for (const auto& id : chain) fx.add(id);
        outputEffects.add(fx);
    }
    juce::Array<juce::var> order;
    for (const auto& id : mix.trackOrder) order.add(id);
    hostData.add(object({{"namespace", mixNamespace}, {"version", "1.0"},
                        {"data", object({{"tracks", routes}, {"trackOrder", order}, {"cueDb", mix.routing.cueDb}, {"inserts", inserts}, {"outputEffects", outputEffects}, {"stockEffectStates", states}, {"chordVoicingMix", voicingMix}, {"chordVoicingRoute", voicingRoute}})}}));
    if (externalKeys.getDynamicObject()->getProperties().size() > 0)
        hostData[0]["data"].getDynamicObject()->setProperty("externalSidechainTracks",externalKeys);
    if (!mix.recordCycle.isEmpty())
        hostData[0]["data"].getDynamicObject()->setProperty("recordCycle", object({
            {"start", mix.recordCycle.getStart()}, {"end", mix.recordCycle.getEnd()}, {"enabled", mix.cycleEnabled}}));
    auto document = object({{"channels", channels}, {"devices", devices}, {"hostExtensions", hostData}});
    if (!sidechains.isEmpty()) document.getDynamicObject()->setProperty("sidechainConnections",sidechains);
    for (const auto& c : channels) {
        const auto it = envelopes.find(c["trackId"].toString());
        if (it == envelopes.end() || it->second.points.empty()) continue;
        juce::Array<juce::var> points;
        for (const auto& p : it->second.points) points.add(object({{"atTick", p.tick}, {"value", p.value}}));
        automation.add(object({{"id", it->second.id}, {"target", object({{"kind","channel"},{"channelId",c["id"]},{"parameter","pan"}})},
            {"valueType","number"}, {"interpolation",it->second.linear ? "linear" : "step"}, {"points",points}}));
    }
    if (!automation.isEmpty()) document.getDynamicObject()->setProperty("automation", automation);
    return document;
}

SessionMix readMix(const juce::var& score, const juce::var& production, PanAutomation& envelopes) {
    const auto host = production["hostExtensions"];
    const auto extensions = host.isVoid() ? juce::Array<juce::var>{} : array(host);
    require(extensions.isEmpty() || (extensions.size() == 1 && extensions[0]["namespace"] == mixNamespace
            && extensions[0]["version"] == "1.0"),
            "This production document is outside the Studio mixer preview");
    std::map<juce::String,juce::String> deviceKeys;
    const auto sourceTracks = score["tracks"];
    const auto& tracks = array(sourceTracks);
    auto trackIndex = [&](const juce::var& id) {
        for (int i = 0; i < tracks.size(); ++i)
            if (tracks[i]["id"] == id) return i;
        return -1;
    };
    const auto local = extensions.isEmpty() ? juce::var{} : extensions[0]["data"];
    const auto voicingMix = local["chordVoicingMix"];
    const auto voicingRoute = local["chordVoicingRoute"];
    const auto voicingIndex = static_cast<std::size_t>(tracks.size());
    auto gain = [](const juce::var& value) {
        const auto v = static_cast<double>(value);
        require(std::isfinite(v), "Invalid mixer gain");
        return std::clamp(v, -60.0, 6.0);
    };
    SessionMix mix;
    if (const auto cycle = local["recordCycle"]; cycle.isObject()) {
        const auto first = integer(cycle["start"]), last = integer(cycle["end"]);
        require(first >= 0 && last > first, "Invalid Studio recording range.");
        mix.recordCycle = {first, last};
        mix.cycleEnabled = bool(cycle["enabled"]);
    }
    mix.routing.busCount = mix.routing.returnCount = minMixerOutputsPerKind;
    mix.tracks.resize(voicingIndex + static_cast<std::size_t>(voicingMix.isObject()));
    mix.midiRoutes.resize(mix.tracks.size());
    mix.instruments.resize(mix.tracks.size());
    mix.trackEffects.resize(mix.tracks.size());
    for (int i = 0; i < tracks.size(); ++i) {
        const auto midi = tracks[i]["midi"];
        const auto channel = static_cast<int>(midi["channel"]);
        mix.midiRoutes[static_cast<std::size_t>(i)].channel =
            midi["mode"] == "singleChannel" && channel >= 1 && channel <= 16
            ? channel : (tracks[i]["trackType"] == "drum" ? 10 : 1);
    }
    const auto deviceData = production["devices"];
    const auto devices = deviceData.isVoid() ? juce::Array<juce::var>{} : array(deviceData);
    const auto channelData = production["channels"];
    auto channels = array(channelData);
    if (!production["sidechainConnections"].isVoid()) for(const auto& connection : array(production["sidechainConnections"])) {
        auto source=std::find_if(channels.begin(),channels.end(),[&](const auto& c){return c["id"]==connection["sourceChannelId"];});
        auto device=std::find_if(devices.begin(),devices.end(),[&](const auto& d){return d["id"]==connection["targetDeviceId"];});
        require(source!=channels.end() && (*source)["kind"]=="track" && trackIndex((*source)["trackId"])>=0
            && device!=devices.end() && (*device)["role"]=="effect" && connection["targetPortId"]=="vst3-aux-1"
            && connection["tapPoint"]=="preFader" && !bool(connection["followMute"]) && bool(connection["enabled"])
            && double(connection["gainDb"])==0., "Unsupported sidechain routing");
        require(deviceKeys.emplace((*device)["id"].toString(),(*source)["trackId"].toString()).second,"Multiple detector sources");
    }
    if (voicingMix.isObject()) channels.add(voicingMix);
    // Legacy Studio chains can also contain cached host IDs for third-party plug-ins.
    // Translate state keys after merging those chains, so mixed chains keep slot order.
    std::map<juce::String, juce::StringArray> effectDeviceIds;
    for (const auto& entry : channels) {
        const auto id = entry["id"].toString();
        const bool outputChannel = entry["kind"] != "track";
        const int track = entry == voicingMix ? static_cast<int>(voicingIndex) : trackIndex(entry["trackId"]);
        const int bus = busIndex(id);
        const int output = id == "master" ? 0 : bus >= 0 ? static_cast<int>(busOutput(bus))
            : returnIndex(id) >= 0 ? 3 + returnIndex(id) : -1;
        const int channelIndex = outputChannel ? output : track;
        require(channelIndex >= 0, "Unsupported production channel");
        if (outputChannel) mix.outputNames[static_cast<std::size_t>(channelIndex)] = entry["name"].toString();
        auto& fx = outputChannel ? mix.outputEffects[channelIndex] : mix.trackEffects[channelIndex];
        for (const auto& deviceId : array(entry["deviceIds"])) {
            const auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& device) { return device["id"] == deviceId; });
            require(found != devices.end() && (*found)["stateAssetId"].isVoid(), "Unknown device or unsupported state asset");
            const auto& device = *found;
            if (device["role"] == "instrument") {
                require(!outputChannel && fx.isEmpty() && !mix.instruments[track].isObject(), "Unsupported instrument order");
                if (static_cast<bool>(device["enabled"]) && !static_cast<bool>(device["bypass"]))
                    mix.instruments[track] = device["plugin"];
                continue;
            }
            require(device["role"] == "effect" && static_cast<bool>(device["enabled"]) && !static_cast<bool>(device["bypass"]),
                    "Unsupported production device or bypass state");
            const auto ref = device["plugin"];
            const auto stock = std::find_if(std::begin(stockEffects), std::end(stockEffects), [&](const auto& item) {
                return ref["format"] == "vst3" && ref["pluginId"].toString().equalsIgnoreCase(item.classId());
            });
            require(stock != std::end(stockEffects) && fx.size() < static_cast<int>(SessionMix::maxEffects),
                    "Unsupported Production Session effect or chain length");
            fx.add(stock->chainId());
            effectDeviceIds[SessionMix::effectSlot(outputChannel, channelIndex, 0)].add(deviceId.toString());
        }
        if (id == "master") {
            mix.routing.masterDb = gain(entry["gainDb"]);
            mix.routing.outputMuted[0] = static_cast<bool>(entry["mute"]);
        } else if (bus >= 0) {
            mix.routing.busCount = std::max(mix.routing.busCount, static_cast<std::size_t>(bus + 1));
            mix.routing.busDb[bus] = gain(entry["gainDb"]);
            mix.routing.outputMuted[busOutput(bus)] = static_cast<bool>(entry["mute"]);
        }
        else if (id.startsWith("return-")) {
            const auto index = returnIndex(id);
            require(index >= 0, "Invalid OSTFP return");
            mix.routing.returnDb[static_cast<std::size_t>(index)] = gain(entry["gainDb"]);
            mix.routing.outputMuted[3 + static_cast<std::size_t>(index)] = static_cast<bool>(entry["mute"]);
            mix.routing.returnCount = std::max(mix.routing.returnCount, static_cast<std::size_t>(index + 1));
        } else {
            const auto i = entry == voicingMix ? static_cast<int>(voicingIndex) : trackIndex(entry["trackId"]);
            require(i >= 0 && entry["kind"] == "track", "Unsupported OSTFP mixer track");
            auto& settings = mix.tracks[static_cast<std::size_t>(i)];
            settings.volumeDb = gain(entry["gainDb"]);
            const auto pan = static_cast<float>(entry["pan"]);
            require(std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f, "Invalid OSTFP pan");
            settings.pan = pan;
            settings.muted = static_cast<bool>(entry["mute"]);
            settings.soloed = static_cast<bool>(entry["solo"]);
            const auto output = entry["outputChannelId"].toString();
            require(output == "master" || busIndex(output) >= 0, "Unsupported OSTFP track output");
            settings.outputBus = busIndex(output);
            const auto sends = entry["sends"];
            if (!sends.isVoid()) for (const auto& send : array(sends)) {
                const auto r = returnIndex(send["targetChannelId"].toString());
                require(r >= 0 && !static_cast<bool>(send["preFader"]), "Unsupported OSTFP send");
                if (static_cast<bool>(send["enabled"]))
                    settings.returnSends[static_cast<std::size_t>(r)] = std::min(1.0f, gainFromDb(gain(send["gainDb"])));
            }
        }
    }
    const auto insertData = local["inserts"];
    if (insertData.isArray()) {
        const auto& ids = array(insertData);
        for (int i = 0; i < std::min(ids.size(), static_cast<int>(mix.insertIds.size())); ++i)
            mix.insertIds[static_cast<std::size_t>(i)] = ids[i].toString();
    }
    auto readEffects = [](const juce::var& value, juce::StringArray& result) {
        juce::StringArray legacy;
        if (const auto* values = value.getArray())
            for (const auto& entry : *values)
                if (entry.isString() && entry.toString().isNotEmpty()
                    && legacy.size() < static_cast<int>(SessionMix::maxEffects)) legacy.add(entry.toString());
        if (legacy.isEmpty()) return;
        if (!result.isEmpty()) {
            juce::StringArray stock;
            for (const auto& id : legacy)
                for (const auto& item : stockEffects) if (id == item.chainId()) { stock.add(id); break; }
            require(stock == result, "Conflicting production and Studio effect order");
        }
        result = std::move(legacy);
    };
    const auto outputData = local["outputEffects"];
    if (const auto* outputs = outputData.getArray())
        for (int i = 0; i < std::min(outputs->size(), static_cast<int>(mix.outputEffects.size())); ++i)
            readEffects((*outputs)[i], mix.outputEffects[static_cast<std::size_t>(i)]);
    mix.trackEffects.resize(mix.tracks.size());
    mix.audioTracks.resize(mix.tracks.size());
    if (const auto* ids = local["trackOrder"].getArray())
        for (const auto& id : *ids) if (id.isString()) mix.trackOrder.addIfNotAlreadyThere(id.toString());
    mix.routing.cueDb = gain(local["cueDb"]);
    const auto routeData = local["tracks"];
    auto routes = routeData.isVoid() ? juce::Array<juce::var>{} : array(routeData);
    if (voicingMix.isObject() && voicingRoute.isObject()) routes.add(voicingRoute);
    for (const auto& route : routes) {
        const auto i = route == voicingRoute ? static_cast<int>(voicingIndex) : trackIndex(route["trackId"]);
        const auto midiChannel = integer(route["midiChannel"]);
        require(i >= 0 && midiChannel >= 1 && midiChannel <= 16, "Invalid OSTFP MIDI route");
        const auto index = static_cast<std::size_t>(i);
        readEffects(route["effects"], mix.trackEffects[index]);
        mix.audioTracks[index] = static_cast<bool>(route["audioTrack"]);
        if (mix.audioTracks[index]) {
            auto first = integer(route.getProperty("inputFirstChannel", -1));
            double db = double(route.getProperty("inputGainDb", 0.0));
            require(first >= -1 && first < 64 && std::isfinite(db) && db >= -60 && db <= 24, "Invalid recording input.");
            mix.audioInputs[route["trackId"].toString()] = {int(first), db};
        }
        if (mix.audioTracks[index] && route.hasProperty("captureChannels")) {
            const auto channels = integer(route["captureChannels"]);
            require(channels == 1 || channels == 2, "Invalid capture channel count.");
            mix.captureChannels[route["trackId"].toString()] = int(channels);
        }
        mix.tracks[index].cue = static_cast<bool>(route["cue"]);
        mix.midiRoutes[index].channel = static_cast<int>(midiChannel);
        if (!route["destinationTrackId"].isVoid()) {
            const auto target = trackIndex(route["destinationTrackId"]);
            require(target >= 0 && target != i, "Invalid OSTFP MIDI destination");
            mix.midiRoutes[index].destination = static_cast<std::size_t>(target);
        }
    }
    std::map<juce::String,juce::String> slotByDevice;
    auto restoreStates = [&](bool output, std::size_t channel, const juce::StringArray& chain) {
        const auto it = effectDeviceIds.find(SessionMix::effectSlot(output, channel, 0));
        const bool voicing = !output && channel == voicingIndex;
        if (it == effectDeviceIds.end() && !voicing) return;
        int device = 0;
        for (int slot = 0; slot < chain.size(); ++slot)
            for (const auto& fx : stockEffects) if (chain[slot] == fx.chainId()) {
                const auto key = SessionMix::effectSlot(output, channel, slot);
                const juce::Identifier deviceId(voicing ? key : it->second[device++]);
                slotByDevice[deviceId.toString()] = key;
                if(auto source=deviceKeys.find(deviceId.toString()); source!=deviceKeys.end()) mix.sidechainTracks[key]=source->second;
                const auto state = local["stockEffectStates"][deviceId];
                if (state.isString()) mix.stockEffectStates[key] = state.toString();
                break;
            }
    };
    for (std::size_t i = 0; i < mix.trackEffects.size(); ++i) restoreStates(false, i, mix.trackEffects[i]);
    for (std::size_t i = 0; i < mix.outputEffects.size(); ++i) restoreStates(true, i, mix.outputEffects[i]);
    if (auto* keys = local["externalSidechainTracks"].getDynamicObject()) {
        auto readKey = [&](bool output, size_t channel, const juce::StringArray& chain) {
            for (int i=0;i<chain.size();++i) {
                const auto id=SessionMix::effectSlot(output,channel,i);
                const auto source=keys->getProperty(id).toString();
                if (source.isNotEmpty() && trackIndex(source)>=0 && !mix.sidechainTracks.contains(id)) mix.sidechainTracks[id]=source;
            }
        };
        for (size_t i=0;i<mix.trackEffects.size();++i) readKey(false,i,mix.trackEffects[i]);
        for (size_t i=0;i<mix.outputEffects.size();++i) readKey(true,i,mix.outputEffects[i]);
    }
    if (production["automation"].isArray()) for (const auto& a : array(production["automation"])) {
        const auto t = a["target"];
        if (t["kind"] == "device") {
            const auto slot = slotByDevice.find(t["deviceId"].toString());
            const auto points = a["points"];
            const auto parameter = t["parameterId"].toString();
            require(slot != slotByDevice.end() && parameter.isNotEmpty() && a["valueType"] == "number" &&
                    points.size() == 1 && integer(points[0]["atTick"]) == 0, "Only initial stock insert parameters are supported.");
            const auto value = double(points[0]["value"]);
            require(value >= 0 && value <= 1 && mix.insertParameters[slot->second].emplace(parameter,value).second, "Invalid or duplicate plug-in parameter.");
            continue;
        }
        require(t["kind"] == "channel" && t["parameter"] == "pan" && a["valueType"] == "number"
            && (a["interpolation"] == "linear" || a["interpolation"] == "step"), "Only track pan automation is supported.");
        juce::String trackId;
        for (const auto& c : channels) if (c["kind"] == "track" && c["id"] == t["channelId"]) trackId = c["trackId"].toString();
        require(trackIndex(trackId) >= 0 && !envelopes.contains(trackId), "Invalid automation target.");
        auto& env = envelopes[trackId];
        env.id = a["id"].toString(); env.linear = a["interpolation"] == "linear";
        for (const auto& p : array(a["points"])) {
            const auto tick = integer(p["atTick"]); const double value = double(p["value"]);
            require(tick >= 0 && (env.points.empty() || tick > env.points.back().tick) && std::isfinite(value) && std::abs(value) <= 1, "Invalid automation point.");
            env.points.push_back({tick, value});
        }
    }
    return mix;
}

std::optional<SessionMix> readEmbeddedMix(const juce::var& score, PanAutomation& envelopes) {
    const auto settings = score["extensions"][mixNamespace];
    if (settings.isVoid()) return std::nullopt;
    require(settings["version"] == "1.0", "Unsupported OSTF Studio settings version");
    return readMix(score, settings["data"], envelopes);
}

// Linear re-pitch fit has a standard representation. Loops and non-linear timing are Studio edits.
void storeClipTiming(juce::var& production, const OstfpPreview& project) {
    auto host = production["hostExtensions"][0]["data"];
    auto edits = object({});
    for (const auto& clip : project.audio) {
        const auto& timing = clip.timing;
        require(timing.valid(), "Invalid audio clip timing");
        if (!timing.phaseOffset && !timing.loop && !timing.leadingTicks && !timing.sourceLimitTicks && timing.markers.empty()
            && (!timing.cycleTicks || timing.cycleTicks == clip.durationTicks)) continue;
        juce::Array<juce::var> points;
        for (auto p : timing.markers) points.add(object({{"position", p.position}, {"source", p.source}}));
        edits.getDynamicObject()->setProperty(clip.regionId,
            object({{"loop", timing.loop}, {"phaseOffset", timing.phaseOffset}, {"cycleTicks", juce::int64(timing.cycleTicks)}, {"markers", points},
                    {"leadingTicks", juce::int64(timing.leadingTicks)}, {"sourceLimitTicks", juce::int64(timing.sourceLimitTicks)}}));
    }
    if (edits.getDynamicObject()->getProperties().size()) host.getDynamicObject()->setProperty("clipTiming", edits);
}

juce::MemoryBlock scoreWithMix(const OstfpPreview& project, const SessionMix& mix,
                               const juce::Array<juce::var>& assets, const juce::Array<juce::var>& regions) {
    const auto core = savedScore(project, true);
    auto score = parseObject(core);
    auto extensions = score["extensions"];
    if (!extensions.isObject()) extensions = object({});
    auto settings = productionMix(score, mix, project.panAutomation);
    storeClipTiming(settings, project);
    if (!assets.isEmpty()) {
        settings.getDynamicObject()->setProperty("assets", assets);
        settings.getDynamicObject()->setProperty("audioRegions", regions);
    }
    extensions.getDynamicObject()->setProperty(mixNamespace, object({
        {"version", "1.0"}, {"status", "current"}, {"dependencies", object({{"scope", "document"}})},
        {"data", settings}}));
    score.getDynamicObject()->setProperty("extensions", extensions);
    auto bytes = editedScoreBytes(core, score, {"extensions"});
    if (project.sourceVersion != "5.0.0") validateVersion(parseObject(bytes)["version"].toString(), bytes);
    return bytes;
}

bool relativeWavPath(const juce::String& path) {
    const auto parts = juce::StringArray::fromTokens(path, "/", "");
    return path.endsWithIgnoreCase(".wav") && !path.startsWithChar('/') && !path.containsAnyOf("\\:\r\n")
        && !path.contains("//") && !parts.contains(".") && !parts.contains("..");
}

void readAudioReferences(const juce::var& score, const juce::var& production, OstfpPreview& project,
                         const juce::var& manifest = {}, const juce::String& documentPath = {}) {
    const auto bindings = manifest.getProperty("bindings", juce::Array<juce::var>{});
    const auto assets = production.getProperty("assets", juce::Array<juce::var>{});
    const auto regions = production.getProperty("audioRegions", juce::Array<juce::var>{});
    const auto references = production["audio"];
    if (!array(assets).isEmpty() || !array(regions).isEmpty() || !array(bindings).isEmpty()) {
        const auto& descriptors = array(assets);
        const auto& clips = array(regions);
        require(descriptors.size() <= static_cast<int>(OstfpPreview::maxAudioRegions) && clips.size() <= static_cast<int>(OstfpPreview::maxAudioRegions)
                && references.isVoid(), "Too many audio assets or regions");
        require(!manifest.isObject() || array(bindings).size() == descriptors.size(), "Missing production asset bindings");
        juce::StringArray usedIds, regionIds;
        const auto channels = production["channels"];
        const auto tracks = score["tracks"];
        for (const auto& region : clips) {
            const auto assetId = region["sourceAssetId"].toString();
            const auto findId = [](const auto& list, const auto& id) {
                return std::find_if(list.begin(), list.end(), [&](const auto& item) { return item["id"] == id; });
            };
            const auto asset = findId(descriptors, assetId);
            const auto channel = findId(array(channels), region["channelId"]);
            require(asset != descriptors.end() && assetId.isNotEmpty()
                    && channel != array(channels).end() && (*channel)["kind"] == "track", "Unresolved audio region");
            const auto track = findId(array(tracks), (*channel)["trackId"]);
            require(track != array(tracks).end(), "Unknown audio region track");
            ProjectAudio item;
            item.track = static_cast<std::size_t>(track - array(tracks).begin());
            usedIds.addIfNotAlreadyThere(assetId);
            item.regionId = region["id"].toString();
            require(item.regionId.isNotEmpty() && regionIds.addIfNotAlreadyThere(item.regionId), "Duplicate or empty audio region ID");
            const auto& a = *asset;
            item.path = a["uri"].toString();
            item.sha256 = a["sha256"].toString();
            const auto rate = integer(a["sampleRateHz"]), channelsInFile = integer(a["audioChannels"]);
            item.assetFrames = integer(a["frameCount"]);
            require(a["purpose"] == "audioSource" && a["mediaType"] == "audio/wav" && item.sha256.length() == 64
                    && rate >= 1 && rate <= 768000 && channelsInFile >= 1 && channelsInFile <= 2
                    && item.assetFrames > 0 && item.assetFrames <= 8 * 1024 * 1024, "Invalid WAV asset descriptor");
            item.sampleRate = static_cast<int>(rate);
            item.channels = static_cast<int>(channelsInFile);
            item.startTick = integer(region["startTick"]); item.durationTicks = integer(region["durationTicks"]);
            item.startFrame = integer(region["sourceStartFrame"]); item.frameCount = integer(region["sourceFrameCount"]);
            item.gainDb = static_cast<double>(region["gainDb"]); item.muted = static_cast<bool>(region["mute"]);
            const bool fit = region["playbackMode"] == "fit" && region["pitchPolicy"] == "followRate";
            if (fit) item.timing.cycleTicks = item.durationTicks;
            const auto host = production["hostExtensions"];
            const auto timing = host.isArray() && host.size() > 0
                ? host[0]["data"]["clipTiming"][juce::Identifier(region["id"].toString())] : juce::var{};
            if (!timing.isVoid()) {
                require(timing["loop"].isBool() && timing["markers"].isArray(), "Invalid Studio audio timing");
                item.timing.leadingTicks = integer(timing.getProperty("leadingTicks", 0));
                item.timing.sourceLimitTicks = integer(timing.getProperty("sourceLimitTicks", 0));
                require(item.startTick >= 0 && item.startTick <= project.playbackEndTick()
                        && item.timing.leadingTicks <= project.playbackEndTick() - item.startTick
                        && item.timing.sourceLimitTicks <= project.playbackEndTick(), "Invalid audio padding");
                item.timing.phaseOffset = double(timing.getProperty("phaseOffset", 0.));
                item.timing.loop = static_cast<bool>(timing["loop"]);
                item.timing.cycleTicks = integer(timing["cycleTicks"]);
                require(array(timing["markers"]).size() <= 128, "Too many warp markers");
                for (const auto& p : array(timing["markers"]))
                    item.timing.markers.push_back({double(p["position"]), double(p["source"])});
                require(item.timing.valid(), "Invalid Studio warp markers");
            }
            require((fit || region["playbackMode"] == "native") && integer(region.getProperty("fadeInTicks", 0)) == 0
                    && integer(region.getProperty("fadeOutTicks", 0)) == 0, "Unsupported audio region stretch or fade");
            require(item.startTick >= 0 && item.durationTicks > 0 && item.durationTicks <= project.playbackEndTick() - item.startTick
                    && item.startFrame >= 0 && item.frameCount > 0 && item.frameCount <= item.assetFrames - item.startFrame
                    && std::isfinite(item.gainDb), "Invalid audio region range");
            if (manifest.isObject()) {
                const auto& links = array(bindings);
                const auto link = std::find_if(links.begin(), links.end(), [&](const auto& b) {
                    return b["documentPath"] == documentPath && b["resourceKind"] == "productionAsset" && b["resourceId"] == assetId;
                });
                require(link != links.end(), "Missing production audio binding");
                const auto entries = manifest["assets"];
                const auto member = findId(array(entries), (*link)["assetId"]);
                require(member != array(entries).end() && (*member)["sha256"] == item.sha256 && (*member)["mediaType"] == a["mediaType"]
                        && (*member)["purpose"] == a["purpose"] && (!a.hasProperty("byteLength") || integer(a["byteLength"]) == integer((*member)["sizeBytes"])),
                        "Inconsistent production audio binding");
                item.path = (*member)["path"].toString();
            }
            require(relativeWavPath(item.path), "Unsupported audio file path");
            if (project.savedMix && !project.savedMix->instruments[item.track].isObject() && array((*track)["clips"]).isEmpty())
                project.savedMix->audioTracks[item.track] = true;
            project.audio.push_back(std::move(item));
        }
        require(usedIds.size() == descriptors.size(), "Unused or duplicate production audio asset");
        return;
    }
    if (!references.isArray()) return;
    const auto& records = array(references);
    require(records.size() <= static_cast<int>(OstfpPreview::maxTracks), "Too many referenced audio files");
    for (const auto& ref : records) {
        const auto track = integer(ref["track"]);
        const auto path = ref["path"].toString();
        require(track >= 0 && static_cast<std::size_t>(track) < project.sourceTrackCount()
                && relativeWavPath(path), "Unsupported Studio audio reference");
        project.audio.push_back({static_cast<std::size_t>(track), path, {}, {}});
    }
}

juce::MemoryBlock audioBytes(const ProjectAudio& audio) {
    if (!audio.embedded.isEmpty()) return audio.embedded;
    const auto length = audio.localFile.getSize();
    require(length > 0 && length <= maxAudioBytes, "Missing audio or WAV exceeds the preview size limit");
    juce::MemoryBlock bytes;
    require(audio.localFile.loadFileAsData(bytes) && bytes.getSize() == static_cast<std::size_t>(length), "Could not read a referenced WAV");
    return bytes;
}

void reviseDocument(juce::var& score, const juce::MemoryBlock& before, const juce::String& revision) {
    if (auto* document = score["document"].getDynamicObject()) {
        document->setProperty("parents", juce::Array<juce::var>{object({{"documentId", document->getProperty("id")},
            {"revisionId", document->getProperty("revisionId")}, {"sha256", juce::SHA256(before).toHexString()}})});
        document->setProperty("revisionId", revision);
        document->setProperty("modifiedBy", object({{"name", "OSTF Studio"}}));
    }
}

void describeWav(const OstfpPreview& project, const ProjectAudio& audio, const juce::MemoryBlock& bytes,
                 const juce::String& path, const juce::String& hash, juce::Array<juce::var>& assets, juce::Array<juce::var>& regions) {
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(bytes, false), true));
    require(reader && reader->lengthInSamples > 0 && reader->lengthInSamples <= 8 * 1024 * 1024 && reader->numChannels >= 1
            && reader->numChannels <= 2 && reader->sampleRate >= 1 && reader->sampleRate <= 768000, "Unsupported WAV format");
    require(audio.sha256.isEmpty() || (hash == audio.sha256 && reader->sampleRate == audio.sampleRate
            && static_cast<int>(reader->numChannels) == audio.channels && reader->lengthInSamples == audio.assetFrames),
            "WAV differs from its production asset descriptor");
    const auto number = juce::String(static_cast<int>(audio.track + 1));
    const auto id = "source-" + hash;
    const auto frames = audio.frameCount > 0 ? audio.frameCount : reader->lengthInSamples - audio.startFrame;
    const auto length = audio.durationTicks > 0 ? audio.durationTicks : std::clamp<juce::int64>(
        static_cast<juce::int64>(std::ceil(project.clock.tickAt(project.clock.secondsAt(audio.startTick) + frames / reader->sampleRate) - audio.startTick)), 1, project.playbackEndTick() - audio.startTick);
    if (std::none_of(assets.begin(), assets.end(), [&](const auto& a) { return a["id"] == id; }))
        assets.add(object({{"id", id}, {"purpose", "audioSource"}, {"uri", path}, {"mediaType", "audio/wav"},
        {"sha256", hash}, {"byteLength", static_cast<juce::int64>(bytes.getSize())},
        {"sampleRateHz", static_cast<int>(reader->sampleRate)}, {"audioChannels", static_cast<int>(reader->numChannels)},
        {"frameCount", reader->lengthInSamples}}));
    regions.add(object({{"id", audio.regionId}, {"channelId", "track-" + number}, {"sourceAssetId", id},
        {"startTick", audio.startTick}, {"durationTicks", length}, {"sourceStartFrame", audio.startFrame},
        {"sourceFrameCount", frames}, {"playbackMode", "native"}, {"gainDb", audio.gainDb}, {"mute", audio.muted}}));
    if (!audio.timing.phaseOffset && audio.timing.cycleTicks == length && !audio.timing.loop && !audio.timing.leadingTicks && !audio.timing.sourceLimitTicks) {
        regions.getLast().getDynamicObject()->setProperty("playbackMode", "fit");
        regions.getLast().getDynamicObject()->setProperty("pitchPolicy", "followRate");
    }
}
}

bool OstfpPreview::partDisplaysDocument(size_t track, const juce::String& document) const {
    const auto docs = trackLyricDocuments.find(trackIds[int(track)]);
    return docs != trackLyricDocuments.end() ? docs->second.contains(document)
                                             : primaryMelodyTrack == track && document == displayDocument;
}

const char* OstfpPreview::displayLyricAt(size_t track, juce::int64 tick) const {
    const auto part = partLyricLanes.find(track);
    if (part == partLyricLanes.end() || ticksPerBar <= 0 || tick < 0) return "";
    const double bar = 1.0 + double(tick) / ticksPerBar;
    for (const auto& lane : part->second) {
        if (lane.kind != TimelineLane::displayLyrics) continue;
        const auto next = std::upper_bound(lane.regions.begin(), lane.regions.end(), bar,
            [](double at, const ArrangementRegion& cell) { return at < cell.exactBars->start; });
        if (next != lane.regions.begin()) {
            const auto& cell = *std::prev(next);
            if (bar < cell.exactBars->end && cell.label) return cell.label;
        }
    }
    return "";
}

juce::String OstfpPreview::contextAt(TimelineLane kind, juce::int64 tick) const {
    if (ticksPerBar <= 0) return {};
    const double at = 1.0 + double(tick) / ticksPerBar;
    juce::StringArray labelsAtTick;
    for (const auto& lane : globalLanes) {
        if (lane.kind != kind) continue;
        for (const auto& item : lane.regions) {
            if (item.isScoped) continue;
            const auto begin = item.exactBars ? item.exactBars->start : item.bars.firstBar;
            const auto end = item.exactBars ? item.exactBars->end : item.bars.endBar;
            if (at >= begin && at < end) labelsAtTick.addIfNotAlreadyThere(juce::String::fromUTF8(item.label));
        }
    }
    if (labelsAtTick.isEmpty() && kind == TimelineLane::scale && at < bars.endBar) return defaultScale;
    return labelsAtTick.joinIntoString(" / ");
}

SongInformation OstfpPreview::songInformation() const {
    const auto score = parseObject(sourceScore);
    const auto song = score["song"];
    SongInformation info{song["title"].toString(), song["subtitle"].toString(), song["description"].toString(), {}};
    auto tagNames = [&](const char* key) {
        juce::StringArray result;
        if (const auto* tags = song[key].getArray())
            for (const auto& tag : *tags)
                result.add(tag["name"].toString());
        return result;
    };
    info.genres = tagNames("genres");
    info.moods = tagNames("moods");
    std::map<juce::String, juce::String> names;
    if (const auto* people = score["contributors"].getArray())
        for (const auto& p : *people) names[p["id"].toString()] = p["name"].toString();
    if (const auto* credits = song["credits"].getArray())
        for (const auto& c : *credits) {
            auto who = c["contributorId"].toString();
            info.credits.push_back({c["id"].toString(), who, c["role"].toString(), names[who]});
        }
    return info;
}

juce::Result OstfpPreview::applySongDetails(const SongInformation& info, const SongMusicEdit& music, juce::MemoryBlock& previous) try {
    const auto original = savedScore(*this);
    auto draft = projectScore(parseObject(original), chordVoicingTrack.has_value());
    draft->sourceScore = original;
    draft->sourceVersion = sourceVersion;
    auto result = draft->applySongInformation(info);
    juce::MemoryBlock beforeMusic;
    if (result.wasOk()) result = draft->applySongMusic(music, beforeMusic);
    if (result.failed() || original == draft->sourceScore) return result;
    result = restoreNoteScore(draft->sourceScore);
    if (result.wasOk()) previous = original;
    return result;
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::applySongInformation(const SongInformation& info, juce::MemoryBlock* previous) try {
    if (info.title.trim().isEmpty()) return juce::Result::fail("Enter a song title.");
    const auto beforeEdit = savedScore(*this);
    auto score = parseObject(beforeEdit);
    auto* song = score["song"].getDynamicObject();
    song->setProperty("title", info.title);
    auto optionalText = [&](const char* key, const juce::String& value) {
        if (value.isNotEmpty()) song->setProperty(key, value);
        else if (song->getProperty(key).toString().isNotEmpty()) song->removeProperty(key);
    };
    optionalText("subtitle", info.subtitle);
    optionalText("description", info.description);
    auto updateTags = [&](const char* key, const std::optional<juce::StringArray>& names) {
        if (!names)
            return;
        const auto original = song->getProperty(key);
        juce::Array<juce::var> result;
        std::vector<bool> used(original.size());
        std::vector<int> matches(names->size(), -1);
        for (int n = 0; n < names->size(); ++n)
            for (int i = 0; i < original.size(); ++i)
                if (!used[i] && original[i]["name"].toString() == (*names)[n]) {
                    used[i] = true;
                    matches[n] = i;
                    break;
                }
        for (int n = 0; n < names->size(); ++n) {
            const auto& name = (*names)[n];
            require(name.trim().isNotEmpty(), "Enter a tag name.");
            int i = matches[n];
            if (i < 0 && n < original.size() && !used[n]) {
                i = n;
                used[n] = true;
            }
            auto tag =
                i >= 0 ? original[i].clone() : object({{"id", juce::String(key) + "-" + juce::Uuid().toString()}});
            tag.getDynamicObject()->setProperty("name", name);
            result.add(tag);
        }
        if (original.isArray() || !result.isEmpty())
            song->setProperty(key, result);
    };
    updateTags("genres", info.genres);
    updateTags("moods", info.moods);
    auto people = score.getProperty("contributors", juce::Array<juce::var>{});
    if (!people.isArray()) return juce::Result::fail("Invalid contributor list.");
    auto originals = song->getProperty("credits");
    juce::Array<juce::var> credits;
    std::map<juce::String, juce::String> editedNames;
    for (const auto& row : info.credits) {
        if (row.name.trim().isEmpty() || row.role.isEmpty() || !row.role.substring(0, 1).containsOnly("abcdefghijklmnopqrstuvwxyz")
            || !row.role.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789.-"))
            return juce::Result::fail("Each credit needs a name and a lowercase role such as composer.");
        juce::var person;
        for (const auto& p : *people.getArray()) {
            if (row.contributorId.isNotEmpty() ? p["id"].toString() == row.contributorId : p["name"].toString() == row.name) {
                person = p;
                break;
            }
        }
        if (!person.isObject()) {
            person = object({{"id", "person-" + juce::Uuid().toString()}, {"name", row.name}});
            people.getArray()->add(person);
        }
        const auto who = person["id"].toString();
        if (editedNames.contains(who) && editedNames[who] != row.name)
            return juce::Result::fail("Use the same name for a shared contributor.");
        editedNames[who] = row.name;
        person.getDynamicObject()->setProperty("name", row.name);
        juce::var credit;
        if (originals.isArray() && row.id.isNotEmpty())
            for (const auto& c : *originals.getArray()) if (c["id"].toString() == row.id) { credit = c; break; }
        if (!credit.isObject()) credit = object({{"id", "credit-" + juce::Uuid().toString()}});
        credit.getDynamicObject()->setProperty("contributorId", who);
        credit.getDynamicObject()->setProperty("role", row.role);
        credits.add(credit);
    }
    if (score.hasProperty("contributors") || !people.getArray()->isEmpty()) score.getDynamicObject()->setProperty("contributors", people);
    if (originals.isArray() || !credits.isEmpty()) song->setProperty("credits", credits);
    if (juce::JSON::toString(score) == juce::JSON::toString(parseObject(beforeEdit))) return juce::Result::ok();
    reviseDocument(score, beforeEdit, "metadata-" + juce::Uuid().toString());
    auto bytes = editedScoreBytes(beforeEdit, score, {"song", "contributors", "document"});
    if (bytes.getSize() > maxBytes) return juce::Result::fail("OSTF score is too large.");
    sourceScore = std::move(bytes);
    addedClips.clear();
    if (previous) *previous = beforeEdit;
    title = info.title;
    return juce::Result::ok();
} catch (const std::exception& error) {
    return juce::Result::fail(juce::String::fromUTF8(error.what()));
}

juce::uint32 OstfpPreview::trackColour(std::size_t index) const {
    constexpr juce::uint32 palette[]{0xff65b8ce,0xffbb86c1,0xff70ba9a,0xff9299d5,0xffd79a74,0xff78a9d5,
                                     0xffbdac69,0xffcd8799,0xff79bfb7,0xffa8a1cf,0xff82ad78,0xffca9774};
    if (index == chordVoicingTrack) return 0xffa6b3c3;
    if (auto it = trackColoursById.find(trackIds[int(index)]); it != trackColoursById.end()) return it->second;
    return palette[index % std::size(palette)];
}

juce::Result OstfpPreview::setTrackColour(std::size_t index, juce::uint32 rgb, juce::MemoryBlock& previous) try {
    require(index < sourceTrackCount(), "Select a score track.");
    const auto before = savedScore(*this);
    auto score = parseObject(before);
    auto presentation = score["presentation"];
    if (!presentation.isObject())
        presentation = object({{"status","current"},{"dependencies",object({{"scope","none"}})},
                               {"roleDefinitions",juce::Array<juce::var>{}},{"sectionRoleAssignments",object({})}});
    auto appearance = presentation["appearance"];
    if (!appearance.isObject()) appearance = object({{"roleColors",object({})},{"customRoleColors",object({})}});
    auto colours = appearance["objectColors"];
    if (!colours.isArray()) colours = juce::Array<juce::var>{};
    auto target = object({{"type","track"},{"id",trackIds[int(index)]}});
    const auto hex = "#" + juce::String::toHexString(int(rgb & 0xffffff)).paddedLeft('0',6).toUpperCase();
    juce::var item;
    for (auto& entry : *colours.getArray())
        if (entry["target"]["type"] == "track" && entry["target"]["id"] == target["id"]) { item = entry; break; }
    if (!item.isObject()) { item = object({{"target",target}}); colours.getArray()->add(item); }
    item.getDynamicObject()->setProperty("color",hex);
    appearance.getDynamicObject()->setProperty("objectColors",colours);
    presentation.getDynamicObject()->setProperty("appearance",appearance);
    score.getDynamicObject()->setProperty("presentation",presentation);
    reviseDocument(score,before,"colour-" + juce::Uuid().toString());
    auto bytes = editedScoreBytes(before,score,{"presentation","document"});
    require(bytes.getSize() <= maxBytes,"OSTF score is too large.");
    previous = before;
    sourceScore = std::move(bytes);
    addedClips.clear();
    trackColoursById[trackIds[int(index)]] = rgb | 0xff000000u;
    return juce::Result::ok();
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::renameTrack(std::size_t index, const juce::String& input) try {
    const auto name = input.trim();
    require(index < sourceTrackCount(), "Select an editable track.");
    require(name.isNotEmpty(), "Enter a track name.");
    if (name == juce::String::fromUTF8(trackLanes[index].label)) return juce::Result::ok();
    const auto before = savedScore(*this);
    auto score = parseObject(before);
    score["tracks"][static_cast<int>(index)].getDynamicObject()->setProperty("name", name);
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    auto bytes = editedScoreBytes(before, score, {"tracks", "document"});
    require(bytes.getSize() <= maxBytes, "OSTF score is too large.");
    sourceScore = std::move(bytes);
    addedClips.clear();
    trackLanes[index].label = keepLabel(name);
    return juce::Result::ok();
} catch (const std::exception& error) {
    return juce::Result::fail(juce::String::fromUTF8(error.what()));
}

const char* OstfpPreview::keepLabel(juce::String label) {
    labels.push_back(std::move(label));
    return labels.back().toRawUTF8();
}

namespace {
juce::var arrayProperty(juce::var parent, const char *key);
juce::String drumSlot(juce::var score, juce::var track, int pitch);

juce::var createKit(juce::var score, juce::var track, const std::vector<DrumSlot> &pads) {
    juce::Array<juce::var> members;
    for (const auto &s : pads)
        members.add(s.id.isEmpty() ? drumSlot(score, track, s.pitch) : s.id);
    const auto id = "kit-" + juce::Uuid().toString();
    auto kit = object({{"id", id}, {"name", track["name"]}, {"soundSlotIds", members}});
    arrayProperty(score, "drumKits").getArray()->add(kit);
    track.getDynamicObject()->setProperty("drumKitId", id);
    return kit;
}

// Pin the previously visible memberships before changing the global slot registry.
void materialiseKits(juce::var score, const OstfpPreview &project) {
    for (size_t i = 0; i < project.sourceTrackCount(); ++i) {
        auto t = score["tracks"][int(i)];
        if (t["trackType"] == "drum" && !t.hasProperty("drumKitId"))
            createKit(score, t, project.slotsByTrack[i]);
    }
}

using IdCopies = std::map<juce::String, juce::String>;

void collectOwnedIds(const juce::var& value, IdCopies& ids, bool makeCopies = false) {
    if (auto* obj = value.getDynamicObject()) {
        if (auto id = value["id"].toString(); id.isNotEmpty())
            ids.try_emplace(id, makeCopies ? "copy-" + juce::Uuid().toString() : juce::String{});
        for (const auto& field : obj->getProperties()) collectOwnedIds(field.value, ids, makeCopies);
    } else if (auto* items = value.getArray())
        for (const auto& item : *items) collectOwnedIds(item, ids, makeCopies);
}

void remapIds(juce::var& value, const juce::String& field, const IdCopies& ids) {
    if (auto* obj = value.getDynamicObject()) {
        auto& properties = obj->getProperties();
        for (int i = 0; i < properties.size(); ++i)
            remapIds(*properties.getVarPointerAt(i), properties.getName(i).toString(), ids);
    } else if (auto* items = value.getArray()) {
        for (auto& item : *items) remapIds(item, field, ids);
    } else if (value.isString() && (field == "id" || field.endsWith("Id") || field.endsWith("Ids"))) {
        if (const auto it = ids.find(value.toString()); it != ids.end()) value = it->second;
    }
}

void writeTrackTakes(juce::var score, juce::var track, StudioMidiTakes& takes);

void copyTakeNotes(StudioMidiTakes& target, const StudioMidiTakes& source, const IdCopies& ids) {
    for (const auto& [oldId, newId] : ids) {
        if (const auto owner = source.owners.find(oldId); owner != source.owners.end())
            target.owners[newId] = owner->second;
        if (source.suppressed.contains(oldId)) target.suppressed.insert(newId);
    }
}

juce::var copyScoreTrack(const juce::var& original, juce::var lyrics, IdCopies& ids) {
    collectOwnedIds(original, ids, true);
    // Lyrics and text units are shared. Each singing part owns its voice and note alignments.
    auto copyRelated = [&](const char* key, auto matches) {
        auto* object = lyrics.getDynamicObject();
        if (!object || !lyrics[key].isArray()) return;
        auto list = lyrics[key];
        const auto count = list.getArray()->size();
        for (int i = 0; i < count; ++i) {
            auto item = (*list.getArray())[i].clone();
            if (!matches(item)) continue;
            collectOwnedIds(item, ids, true);
            remapIds(item, {}, ids);
            list.getArray()->add(item);
        }
        object->setProperty(key, list);
    };
    copyRelated("voices", [&](const auto& voice) { return voice["trackId"] == original["id"]; });
    copyRelated("alignments", [&](const auto& alignment) {
        auto notes = alignment["noteIds"];
        return notes.isArray() && !notes.getArray()->isEmpty() && std::all_of(notes.getArray()->begin(),
            notes.getArray()->end(), [&](const auto& note) { return ids.contains(note.toString()); });
    });
    auto copy = original.clone();
    remapIds(copy, {}, ids);
    copy.getDynamicObject()->setProperty("name", original["name"].toString() + TRANS(" (copy)"));
    return copy;
}
}

juce::Result OstfpPreview::editTracks(TrackKind kind, std::optional<std::size_t> remove,
                                     std::unique_ptr<OstfpPreview>& result, std::optional<std::size_t> duplicate,
                                     MixerTrackGroup group, std::optional<int> newPartChannel) const {
    try {
        const auto before = savedScore(*this);
        auto score = parseObject(before);
        auto tracks = score["tracks"];
        auto& entries = *tracks.getArray();
        auto extensions = score["extensions"];
        if (auto* values = extensions.getDynamicObject()) values->removeProperty(mixNamespace);
        const auto revision = "studio-" + juce::Uuid().toString();
        if (duplicate) {
            require(!remove && *duplicate < sourceTrackCount() && sourceTrackCount() < maxTracks, "Cannot duplicate this track");
            IdCopies ids;
            auto copy = copyScoreTrack(entries[static_cast<int>(*duplicate)], score["lyrics"], ids);
            const auto& sourceTakes = takes(*duplicate);
            if (sourceTakes.count > 1) {
                StudioMidiTakes cloned;
                cloned.count = sourceTakes.count;
                cloned.spans = sourceTakes.spans;
                copyTakeNotes(cloned, sourceTakes, ids);
                writeTrackTakes(score, copy, cloned);
            }
            if (auto* colours=score["presentation"]["appearance"]["objectColors"].getArray()) {
                juce::var clone;
                for(const auto& c:*colours) if(c["target"]["type"]=="track" && c["target"]["id"]==entries[int(*duplicate)]["id"]) {
                    clone=c.clone(); clone["target"].getDynamicObject()->setProperty("id",copy["id"]); break;
                }
                if(clone.isObject()) colours->add(clone);
            }
            entries.add(std::move(copy));
            score.getDynamicObject()->setProperty("tracks", tracks);
            kind = trackKinds[*duplicate];
        } else if (remove) {
            require(*remove < static_cast<std::size_t>(entries.size()), "Invalid track");
            const auto removed = entries[static_cast<int>(*remove)];
            if (auto* data = extensions[StudioMidiTakes::extension]["data"].getDynamicObject()) data->removeProperty(removed["id"].toString());
            IdCopies ids;
            collectOwnedIds(removed, ids);
            auto lyrics = score["lyrics"];
            auto removeRelated = [&](const char* key, auto matches) {
                if (!lyrics.isObject() || !lyrics[key].isArray()) return;
                auto list = lyrics[key];
                for (int i = list.getArray()->size(); --i >= 0;) {
                    const auto item = (*list.getArray())[i];
                    if (!matches(item)) continue;
                    collectOwnedIds(item, ids);
                    list.getArray()->remove(i);
                }
                lyrics.getDynamicObject()->setProperty(key, list);
            };
            removeRelated("voices", [&](const auto& item) { return item["trackId"] == removed["id"]; });
            removeRelated("alignments", [&](const auto& item) {
                auto notes = item["noteIds"];
                return notes.isArray() && !notes.getArray()->isEmpty() && std::all_of(notes.getArray()->begin(),
                    notes.getArray()->end(), [&](const auto& note) { return ids.contains(note.toString()); });
            });
            if (auto* colours=score["presentation"]["appearance"]["objectColors"].getArray())
                for(int i=colours->size();--i>=0;)
                    if((*colours)[i]["target"]["type"]=="track" && (*colours)[i]["target"]["id"]==removed["id"]) colours->remove(i);
            entries.remove(static_cast<int>(*remove));
            score.getDynamicObject()->setProperty("tracks", tracks);
            const auto referencesRemoved = [&ids](const auto& self, const juce::var& value) -> bool {
                if (value.isString()) return ids.contains(value.toString());
                if (const auto* list = value.getArray())
                    for (const auto& entry : *list) if (self(self, entry)) return true;
                if (const auto* object = value.getDynamicObject())
                    for (const auto& property : object->getProperties())
                        if (ids.contains(property.name.toString()) || self(self, property.value)) return true;
                return false;
            };
            require(!referencesRemoved(referencesRemoved, score), "Track or clip is referenced elsewhere in the score");
        } else {
            require(static_cast<std::size_t>(entries.size()) < maxTracks, "Preview supports at most fifteen source tracks");
            const auto name = group == MixerTrackGroup::melody ? "Melody" : group == MixerTrackGroup::vocal ? "Chorus"
                            : kind == TrackKind::audio ? "Audio" : kind == TrackKind::drum ? "Drums" : "Pitched";
            auto track = object({{"id", revision + "-track"}, {"name", name + juce::String(" ") + juce::String(entries.size() + 1)},
                                 {"clips", juce::Array<juce::var>{}}});
            if (kind != TrackKind::audio) track.getDynamicObject()->setProperty("trackType", kind == TrackKind::drum ? "drum" : "melodic");
            if (group == MixerTrackGroup::melody || group == MixerTrackGroup::vocal)
                track.getDynamicObject()->setProperty("role", group == MixerTrackGroup::melody ? "melody" : "vocal");
            if (kind == TrackKind::drum) {
                materialiseKits(score, *this);
                createKit(score, track, {defaultPads.begin(), defaultPads.end()});
                track.getDynamicObject()->setProperty("midi", object({{"mode", "singleChannel"}, {"channel", 10}}));
            }
            if (newPartChannel) {
                require(kind != TrackKind::audio && *newPartChannel >= 1 && *newPartChannel <= 16, "Invalid MIDI part channel");
                track.getDynamicObject()->setProperty("name", "MIDI Ch " + juce::String(*newPartChannel));
                track.getDynamicObject()->setProperty("midi", object({{"mode", "singleChannel"}, {"channel", *newPartChannel}}));
            }
            entries.add(track);
            score.getDynamicObject()->setProperty("tracks", tracks);
        }
        reviseDocument(score, before, revision);
        auto next = projectScore(score);
        next->sourceScore =
            editedScoreBytes(before, score, {"tracks", "document", "extensions", "lyrics", "soundSlots", "drumKits", "presentation", StudioMidiTakes::extension});
        require(next->sourceScore.getSize() <= maxBytes, "OSTF score is too large.");
        next->sourceManifest = sourceManifest;
        next->panAutomation = panAutomation;
        if (remove) next->panAutomation.erase(trackIds[int(*remove)]);
        if (duplicate && panAutomation.contains(trackIds[int(*duplicate)])) {
            auto& env = next->panAutomation[next->trackIds[int(sourceTrackCount())]];
            env = panAutomation.at(trackIds[int(*duplicate)]); env.id = "pan-" + juce::Uuid().toString();
        }
        next->trackKinds = trackKinds;
        if (remove) next->trackKinds.erase(next->trackKinds.begin() + static_cast<std::ptrdiff_t>(*remove));
        else next->trackKinds.insert(next->trackKinds.begin() + static_cast<std::ptrdiff_t>(sourceTrackCount()), kind);
        for (const auto& item : audio) {
            if (remove && item.track == *remove) continue;
            next->audio.push_back(item);
            if (remove && item.track > *remove) --next->audio.back().track;
            if (duplicate && item.track == *duplicate) {
                next->audio.push_back(item);
                next->audio.back().track = sourceTrackCount();
                next->audio.back().regionId = "region-" + juce::Uuid().toString();
            }
        }
        require(next->audio.size() <= maxAudioRegions, "Too many audio regions.");
        result = std::move(next);
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}

namespace {
juce::var arrayProperty(juce::var parent, const char* key) {
    if (!parent[key].isArray()) parent.getDynamicObject()->setProperty(key, juce::Array<juce::var>{});
    return parent[key];
}

bool referencesNote(const juce::var& value, const std::set<juce::String>& ids, bool referenceField = false) {
    if (auto* items = value.getArray()) {
        for (const auto& item : *items) if (referencesNote(item, ids, referenceField)) return true;
    } else if (auto* obj = value.getDynamicObject()) {
        for (const auto& property : obj->getProperties()) {
            const auto key = property.name.toString();
            if (key==StudioMidiTakes::extension) continue;
            if (ids.contains(key) || referencesNote(property.value, ids, key == "id" || key.endsWith("Id") || key.endsWith("Ids"))) return true;
        }
    } else if (referenceField && value.isString()) return ids.contains(value.toString());
    return false;
}

juce::String drumSlot(juce::var score, juce::var track, int pitch) {
    auto slots = arrayProperty(score, "soundSlots");
    juce::var members;
    if (track.hasProperty("drumKitId")) {
        for (const auto& kit : array(score["drumKits"]))
            if (kit["id"] == track["drumKitId"]) members = kit["soundSlotIds"];
        require(members.isArray(), "Missing drum kit.");
    }
    for (const auto& slot : array(slots)) {
        if (slot["midiMapping"].hasProperty("note") && int(slot["midiMapping"]["note"]) == pitch
            && (!members.isArray() || members.getArray()->contains(slot["id"]))) return slot["id"].toString();
    }
    const auto id = "slot-" + juce::Uuid().toString();
    auto pad = std::find_if(defaultPads.begin(), defaultPads.end(), [&](const auto &p) { return p.pitch == pitch; });
    const auto name = pad == defaultPads.end() ? "MIDI " + juce::String(pitch) : pad->name;
    slots.getArray()->add(object({{"id", id}, {"name", name},
                                 {"midiMapping", object({{"standard", pitch >= 35 && pitch <= 81 ? "gm1" : "custom"}, {"note", pitch}})}}));
    if (members.isArray()) members.getArray()->add(id);
    return id;
}
}

juce::Result OstfpPreview::editDrumPad(size_t index, int row, std::optional<int> pitch, juce::MemoryBlock &before,
                                      std::optional<int> moveTo) try {
    require(index < sourceTrackCount() && trackKinds[index] == TrackKind::drum, "Select a drum track.");
    const auto &pads = slotsByTrack.at(index);
    require(row >= -1 && row < int(pads.size()) && (row >= 0 || pitch), "Choose a pad.");
    if (moveTo) {
        require(row >= 0 && *moveTo >= 0 && *moveTo < int(pads.size()), "Choose a pad.");
    } else if (pitch) {
        require(*pitch >= 0 && *pitch <= 127, "Choose MIDI note 0-127.");
        for (int i = 0; i < int(pads.size()); ++i)
            require(i == row || pads[i].pitch != *pitch, "That MIDI note is already assigned.");
    } else {
        const auto &used = slotHits.at(index);
        require(std::none_of(used.begin(), used.end(), [&](const auto &h) { return h.slotId == pads[row].id; }),
                "This pad has hits. Move or remove them first.");
    }
    before = savedScore(*this);
    auto score = parseObject(before);
    materialiseKits(score, *this);
    auto track = score["tracks"][int(index)], kits = arrayProperty(score, "drumKits");
    juce::var kit;
    for (const auto &k : array(kits))
        if (k["id"] == track["drumKitId"])
            kit = k;
    require(kit.isObject(), "Missing drum kit.");
    if (std::count_if(array(score["tracks"]).begin(), array(score["tracks"]).end(),
                      [&](const auto &t) { return t["drumKitId"] == kit["id"]; }) > 1) {
        kit = kit.clone();
        kit.getDynamicObject()->setProperty("id", "kit-" + juce::Uuid().toString());
        track.getDynamicObject()->setProperty("drumKitId", kit["id"]);
        kits.getArray()->add(kit);
    }
    auto members = kit["soundSlotIds"];
    if (moveTo) {
        members.getArray()->move(row, *moveTo);
    } else if (row < 0) {
        drumSlot(score, track, *pitch);
    } else if (!pitch) {
        members.getArray()->remove(row);
    } else {
        const auto old = members[row].toString();
        auto slots = arrayProperty(score, "soundSlots");
        juce::var updated;
        for (const auto &s : array(slots))
            if (s["id"] == old)
                updated = s.clone();
        require(updated.isObject(), "Unknown drum slot.");
        const auto id = "slot-" + juce::Uuid().toString();
        updated.getDynamicObject()->setProperty("id", id);
        updated.getDynamicObject()->setProperty("midiMapping", object({{"standard", "custom"}, {"note", *pitch}}));
        slots.getArray()->add(updated);
        members.getArray()->set(row, id);
        for (const auto &clip : array(track["clips"]))
            if (clip["notes"].isArray())
                for (const auto &n : array(clip["notes"]))
                    if (n["soundSlotId"] == old)
                        n.getDynamicObject()->setProperty("soundSlotId", id);
    }
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    return restoreNoteScore(editedScoreBytes(before, score, {"tracks", "drumKits", "soundSlots", "document"}));
} catch (const std::exception &e) {
    return juce::Result::fail(e.what());
}

juce::Result OstfpPreview::checkAudition(int shift, double rate) const {
    if (!std::isfinite(rate) || rate <= 0 || !clock.scaled(rate))
        return juce::Result::fail("A tempo change would exceed the OSTF tempo range.");
    for (std::size_t i = 0; shift && i < pitchedNotes.size(); ++i) {
        if (i < trackKinds.size() && trackKinds[i] != TrackKind::pitched) continue;
        for (const auto& n : pitchedNotes[i])
            if (!n.fixedTrigger && (n.pitch + shift < 0 || n.pitch + shift > 127))
                return juce::Result::fail("A transposed pitch would fall outside MIDI notes 0-127.");
    }
    return juce::Result::ok();
}

juce::Result OstfpPreview::applySongMusic(const SongMusicEdit& edit, juce::MemoryBlock& before) try {
    require(edit.mode != SongMusicEdit::audition, "Playback overrides belong to the transport.");
    before = savedScore(*this);
    const auto base = savedScore(*this, true);
    auto score = parseObject(base);
    auto song = score["song"], timeline = score["timeline"];
    auto tempos = timeline["tempoChanges"];
    auto set = [](const juce::var& value, const juce::Identifier& key, const juce::var& v) {
        value.getDynamicObject()->setProperty(key, v);
    };
    if (edit.mode == SongMusicEdit::defaults) {
        if (!edit.key.isVoid()) set(song, "key", edit.key);
        if (edit.removeScale) song.getDynamicObject()->removeProperty("scale");
        else if (!edit.scale.isVoid()) set(song, "scale", edit.scale);
        if (edit.bpm != 0) {
            require(std::isfinite(edit.bpm) && edit.bpm > 0 && edit.bpm <= 1000, "Tempo must be above zero and at most 1000 BPM.");
            if (tempos.size() == 0) tempos.getArray()->add(object({{"at", 0}, {"bpm", edit.bpm}}));
            else set(tempos[0], "bpm", edit.bpm);
        }
        if (edit.numerator != 0) {
            auto meter = timeline["meterChanges"][0];
            if (int(meter["numerator"]) != edit.numerator) meter.getDynamicObject()->removeProperty("beatGroups");
            set(meter, "numerator", edit.numerator);
            set(meter, "denominator", edit.denominator);
        }
    } else {
        const int shift = edit.semitones;
        require(shift >= -127 && shift <= 127 && std::isfinite(edit.rate) && edit.rate > 0, "Invalid transposition or tempo multiplier.");
        if (edit.rate != 1) for (const auto& tempo : array(tempos)) {
            const double bpm = double(tempo["bpm"]) * edit.rate;
            require(bpm > 0 && bpm <= 1000, "A tempo change would exceed the OSTF tempo range.");
            set(tempo, "bpm", bpm);
        }
        if (shift != 0) {
            auto each = [](const juce::var& entries, auto visit) {
                if (auto* list = entries.getArray()) for (auto& item : *list) visit(item);
            };
            auto midi = [shift](const juce::var& pitch) {
                int p = int(pitch) + shift;
                require(p >= 0 && p <= 127, "A transposed pitch would fall outside MIDI notes 0-127.");
                return p;
            };
            auto absolute = [&](const juce::var& item, const char* name) {
                if (item.hasProperty(name)) set(item, name, midi(item[name]));
            };
            auto transposeClass = [&](const juce::var& item, const char* name) {
                const auto pc = pitchClassOf(item[name]);
                if (pc >= 0 && shift % 12 != 0) set(item, name, spelledClass((pc + shift + 132) % 12));
            };
            auto labelled = [&](const juce::var& item, const char* name) {
                if (pitchClassOf(item[name]) < 0) return;
                transposeClass(item, name);
                if (item.hasProperty("label")) set(item, "label", shiftedLabel(item["label"].toString(), shift));
            };
            auto note = [&](const juce::var& n) {
                if (n["pitchRole"] != "fixedTrigger") absolute(n, "pitch");
            };
            labelled(song["key"], "tonic");
            labelled(song["scale"], "tonic");
            each(timeline["keyChanges"], [&](const auto& change) {
                labelled(change["key"], "tonic");
                if (change.hasProperty("label")) set(change, "label", shiftedLabel(change["label"].toString(), shift));
            });
            each(score["harmony"]["scales"], [&](const auto& scale) { labelled(scale, "tonic"); });
            each(score["harmony"]["chords"], [&](const auto& chord) {
                labelled(chord, "root");
                transposeClass(chord, "bass");
                each(chord["voicing"]["pitches"], [&](juce::var& p) { p = midi(p); });
            });
            for (int i = 0; i < score["tracks"].size(); ++i) {
                const auto track = score["tracks"][i];
                if (trackKinds.at(i) != TrackKind::pitched) continue;
                each(track["clips"], [&](const auto& clip) {
                    each(clip["notes"], note);
                    each(clip["midiEvents"], [&](const auto& e) { if (e["type"] == "polyPressure") absolute(e, "pitch"); });
                });
                each(track["directions"], [&](const auto& direction) {
                    const auto params = direction["parameters"];
                    absolute(params, "lowPitch");
                    absolute(params, "highPitch");
                    labelled(params["pitchSource"]["scale"], "tonic");
                });
            }
            each(score["relativePhrases"]["assignments"], [&](const auto& assignment) { absolute(assignment, "anchorPitch"); });
            each(score["phraseLibrary"]["templates"], [&](const auto& t) { each(t["notes"], note); });
        }
    }
    if (jsonBytes(score) == jsonBytes(parseObject(base))) { before.reset(); return juce::Result::ok(); }
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    const auto bytes = edit.mode == SongMusicEdit::defaults ? editedScoreBytes(base, score, {"song", "timeline", "document"})
        : edit.semitones == 0 ? editedScoreBytes(base, score, {"timeline", "document"})
        : editedScoreBytes(base, score, {"song", "timeline", "tracks", "harmony", "relativePhrases", "phraseLibrary", "takeGroups", "document"});
    validateVersion(parseObject(bytes)["version"].toString(), bytes);
    return restoreNoteScore(bytes);
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::drawTempo(juce::int64 start, double from, juce::int64 end, double to, juce::MemoryBlock& previous) try {
    if (end < start) { std::swap(start, end); std::swap(from, to); }
    require(start >= 0 && end < durationTicks && std::isfinite(from) && std::isfinite(to)
            && std::min(from, to) > 0 && std::max(from, to) <= 1000, "Invalid tempo range.");
    const auto original = savedScore(*this);
    const auto base = savedScore(*this, true);
    auto score = parseObject(base);
    auto timeline = score["timeline"];
    const auto existing = timeline["tempoChanges"];
    juce::Array<juce::var> events;
    const auto& old = array(existing);
    auto it = old.begin();
    while (it != old.end() && integer((*it)["at"]) < start) events.add(*it++);
    while (it != old.end() && integer((*it)["at"]) <= end) ++it;
    events.add(object({{"at", start}, {"bpm", start == end ? to : from}}));
    if (start < end && from != to) {
        // OSTF 5 stores step changes. Bound the number of samples for a song-length gesture.
        const auto span = end - start;
        const auto segments = std::min<juce::int64>({span, 4096, std::max<juce::int64>(
            std::llround(std::ceil(double(span) * 32 / ppq)), std::llround(std::ceil(std::abs(to - from) * 2)))});
        for (juce::int64 i = 1; i <= segments; ++i) {
            const auto at = start + std::llround(double(span) * i / segments);
            events.add(object({{"at", juce::int64(at)}, {"bpm", i == segments ? to : from + (to - from) * (at - start) / span}}));
        }
    }
    for (; it != old.end(); ++it) events.add(*it);
    if (juce::JSON::toString(events, true, 17) == juce::JSON::toString(existing, true, 17)) return juce::Result::ok();
    timeline.getDynamicObject()->setProperty("tempoChanges", events);
    reviseDocument(score, original, "tempo-" + juce::Uuid().toString());
    const auto bytes = editedScoreBytes(base, score, {"timeline", "document"});
    validateVersion(parseObject(bytes)["version"].toString(), bytes);
    const auto result = restoreNoteScore(bytes);
    if (result.wasOk()) previous = original;
    return result;
} catch (const std::exception& error) { return juce::Result::fail(error.what()); }

juce::Result OstfpPreview::appendSection(juce::Range<juce::int64> range, const juce::String& name,
                                         bool duplicate, juce::MemoryBlock& previous) try {
    const auto destination = playbackEndTick(), length = range.getLength();
    require(length > 0 && destination <= 0x0fffffff - length && name.trim().isNotEmpty(), "Invalid section range.");
    const auto entries = [](const juce::var& v) -> const juce::Array<juce::var>& {
        static const juce::Array<juce::var> empty;
        return v.getArray() ? *v.getArray() : empty;
    };
    const auto original = savedScore(*this), base = savedScore(*this,true);
    auto score = parseObject(base);
    auto timeline = score["timeline"];
    timeline.getDynamicObject()->setProperty("duration",destination + length);
    IdCopies ids;
    struct Copy { juce::var parent; const char* property; juce::var item; };
    std::vector<Copy> copies;
    std::set<juce::String> slicedClips;
    auto stage = [&](juce::var parent, const char* property, juce::var item) {
        // Scale targets are references and must never allocate IDs for existing tracks.
        auto targets = item["targets"];
        if (targets.isArray()) item.getDynamicObject()->removeProperty("targets");
        collectOwnedIds(item,ids,true);
        if (targets.isArray()) item.getDynamicObject()->setProperty("targets",targets);
        copies.push_back({parent,property,item});
    };
    auto section = duplicate ? globalEventAt(score,TimelineLane::section,range.getStart()).clone() : juce::var{};
    if (!section.isObject()) section = object({{"id","section-" + juce::Uuid().toString()}});
    section.getDynamicObject()->setProperty("start",destination);
    section.getDynamicObject()->setProperty("duration",length);
    section.getDynamicObject()->setProperty("name",name);
    section.getDynamicObject()->removeProperty("localizedNames");
    stage(timeline,"sections",section);
    if (duplicate) {
        require(range.getStart() >= 0 && range.getEnd() <= destination,"Invalid section range.");
        auto intersect = [&](const juce::var& item) {
            const auto at = integer(item["start"]);
            return range.getIntersectionWith({at,at+integer(item["duration"])});
        };
        auto spans = [&](juce::var group, const char* property) {
            for (const auto& entry : entries(group[property])) {
                const auto part = intersect(entry);
                if (part.isEmpty()) continue;
                auto copy = entry.clone();
                copy.getDynamicObject()->setProperty("start",destination+part.getStart()-range.getStart());
                copy.getDynamicObject()->setProperty("duration",part.getLength());
                stage(group,property,copy);
            }
        };
        for (auto lane : {TimelineLane::tempo,TimelineLane::meter,TimelineLane::key}) {
            const auto property = globalSource(lane).events;
            auto initial = globalEventAt(score,lane,range.getStart());
            if (!initial.isObject() && lane == TimelineLane::key && score["song"]["key"].isObject())
                initial = object({{"id","key-"+juce::Uuid().toString()},{"key",score["song"]["key"]}});
            auto point = [&](juce::var item,juce::int64 at) {
                auto copy=item.clone(); copy.getDynamicObject()->setProperty("at",at); stage(timeline,property,copy);
            };
            if (initial.isObject()) point(initial,destination);
            for (const auto& item : entries(timeline[property])) {
                const auto at = integer(item["at"]);
                if (at > range.getStart() && at < range.getEnd()) point(item,destination+at-range.getStart());
            }
        }
        for (const auto* property : {"chords","scales"}) spans(score["harmony"],property);
        for (const auto* property : {"phrases","units"}) for (const auto& entry : entries(score["lyrics"][property])) {
            auto copy = entry.clone();
            if (entry.hasProperty("start")) {
                const auto at = integer(entry["start"]);
                if (entry.hasProperty("duration")) {
                    const auto part = intersect(entry);
                    if (part.isEmpty()) continue;
                    copy.getDynamicObject()->setProperty("start",destination+part.getStart()-range.getStart());
                    copy.getDynamicObject()->setProperty("duration",part.getLength());
                } else {
                    if (!range.contains(at)) continue;
                    copy.getDynamicObject()->setProperty("start",destination+at-range.getStart());
                }
            } else if (!ids.contains(entry["phraseId"].toString())) {
                continue;
            }
            stage(score["lyrics"],property,copy);
        }
        for (const auto& track : entries(score["tracks"])) for (const auto& clip : entries(track["clips"])) {
            const auto part = intersect(clip);
            if (part.isEmpty()) continue;
            const auto origin = integer(clip["start"]);
            auto copy = clip.clone();
            copy.getDynamicObject()->setProperty("start",destination+part.getStart()-range.getStart());
            copy.getDynamicObject()->setProperty("duration",part.getLength());
            if (part.getStart()!=origin || part.getLength()!=integer(clip["duration"])) {
                slicedClips.insert(clip["id"].toString());
                // Loop repetitions already exist as score events. A partial copy is a finite slice.
                // Only notes beginning inside this cell belong to it. Keep their full sustains.
                for (const auto* property : {"notes","midiEvents"}) if (auto* list=copy[property].getArray())
                    for (int i=list->size(); --i>=0;) {
                        auto ev=list->getReference(i);
                        const auto at=origin+integer(ev["start"]);
                        if (!part.contains(at)) list->remove(i);
                        else ev.getDynamicObject()->setProperty("start",at-part.getStart());
                    }
                auto retained = [&](const juce::var& id) {
                    for (const auto& note : entries(copy["notes"])) if(note["id"]==id) return true;
                    return false;
                };
                for (auto perf : entries(copy["vocalPerformances"])) {
                    if (auto* list=perf["noteIds"].getArray())
                        for (int i=list->size(); --i>=0;) if (!retained(list->getReference(i))) list->remove(i);
                    if (perf["noteIds"].isArray() && perf["noteIds"].size()==0) perf.getDynamicObject()->removeProperty("noteIds");
                    for (const auto* property : {"pitchCurves","expressionCurves","techniques","breathEvents","phonemeEvents"})
                        if (auto* list=perf[property].getArray()) for (int i=list->size(); --i>=0;) {
                            auto ev=list->getReference(i);
                            const auto target=ev.getProperty("targetNoteId",ev["noteId"]);
                            if (!target.isVoid() && !retained(target)) { list->remove(i); continue; }
                            if (ev.hasProperty("start")) {
                                const auto at=origin+integer(ev["start"]);
                                if (target.isVoid() && !part.contains(at)) {list->remove(i); continue;}
                                ev.getDynamicObject()->setProperty("start",at-part.getStart());
                            }
                            for (auto p : entries(ev["points"])) if(p.hasProperty("at"))
                                p.getDynamicObject()->setProperty("at",origin+integer(p["at"])-part.getStart());
                        }
                }
            }
            stage(track,"clips",copy);
        }
        for (const auto& entry : entries(score["lyrics"]["alignments"])) {
            auto copy = entry.clone();
            bool complete = true;
            for (const auto* prop : {"noteIds","unitIds"}) for (const auto& id : entries(copy[prop]))
                if (!ids.contains(id.toString())) complete = false;
            if (complete) stage(score["lyrics"],"alignments",copy);
        }
    }
    for (auto& copy : copies) {
        auto periods=score["extensions"][loopNamespace]["data"].getDynamicObject();
        if (periods && std::string_view(copy.property)=="clips") {
            const auto oldId=copy.item["id"].toString();
            const auto period=periods->getProperty(oldId);
            if (!period.isVoid() && !slicedClips.contains(oldId)) periods->setProperty(ids.at(oldId),period);
        }
        remapIds(copy.item,{},ids);
        arrayProperty(copy.parent,copy.property).getArray()->add(copy.item);
    }
    if (duplicate) for (size_t i = 0; i < sourceTrackCount(); ++i) {
        const auto& sourceTakes = takes(i);
        if (sourceTakes.count == 1) continue;
        auto copied = sourceTakes;
        copyTakeNotes(copied, sourceTakes, ids);
        for (const auto& span : sourceTakes.spans) {
            auto overlap = span.ticks.getIntersectionWith(range);
            if (!overlap.isEmpty()) copied.adopt(overlap + (destination - range.getStart()), span.adopted);
        }
        writeTrackTakes(score, score["tracks"][int(i)], copied);
    }
    reviseDocument(score,original,"section-"+juce::Uuid().toString());
    auto bytes = editedScoreBytes(base,score,{"timeline","harmony","tracks","lyrics",loopNamespace,StudioMidiTakes::extension,"document"});
    validateVersion(parseObject(bytes)["version"].toString(),bytes);
    auto result=restoreNoteScore(bytes);
    if(result.wasOk()) previous=original;
    return result;
} catch (const std::exception& e) {return juce::Result::fail(e.what());}

juce::Result OstfpPreview::changeGlobalEvents(TimelineLane lane, const juce::Array<juce::var>& remove,
                                              const juce::Array<juce::var>& insert, juce::MemoryBlock& before) try {
    const auto source = globalSource(lane);
    require(*source.events, "Choose an editable global lane.");
    const auto old = savedScore(*this);
    const auto base = savedScore(*this, true);
    auto score = parseObject(base);
    auto group = score[source.group];
    if (!group.isObject()) {
        group = object({});
        score.getDynamicObject()->setProperty(source.group, group);
    }
    auto events = group.getProperty(source.events, juce::Array<juce::var>{});
    auto* list = events.getArray();
    require(list != nullptr, "Invalid global lane.");
    auto same = [](const juce::var& a, const juce::var& b) {
        return juce::JSON::toString(a, true, 17) == juce::JSON::toString(b, true, 17);
    };
    const auto key = source.span ? "start" : "at";
    for (const auto& item : remove) {
        int i = 0;
        while (i < list->size() && !same((*list)[i], item))
            ++i;
        require(i < list->size(), "The source event changed. Select it again.");
        list->remove(i);
    }
    for (const auto& item : insert) {
        const auto at = integer(item[key]);
        const auto len = source.span ? integer(item["duration"]) : 1;
        require(at >= 0 && len > 0 && at < durationTicks && len <= durationTicks - at,
                "The event does not fit inside the song.");
        for (const auto& other : *list) {
            const auto start = integer(other[key]), end = start + (source.span ? integer(other["duration"]) : 1);
            if (lane == TimelineLane::scale && !same(other["targets"], item["targets"]))
                continue;
            require(at >= end || at + len <= start, "The destination contains another event.");
        }
        list->add(item.clone());
    }
    std::stable_sort(list->begin(), list->end(),
                     [&](const auto& a, const auto& b) { return integer(a[key]) < integer(b[key]); });
    group.getDynamicObject()->setProperty(source.events, events);
    if (lane == TimelineLane::meter || lane == TimelineLane::tempo)
        require(!list->isEmpty() && integer(list->getReference(0)[key]) == 0,
                "The initial tempo or meter must stay at tick zero.");
    // Keep references to both halves of a section or chord cut.
    if (remove.size()==1 && insert.size()==2 && insert[0]["id"]==remove[0]["id"] &&
        (lane==TimelineLane::section || lane==TimelineLane::chord)) {
        const auto oldId=remove[0]["id"], newId=insert[1]["id"];
        if (lane==TimelineLane::section) if (auto* plans=score["performancePlans"].getArray())
            for (const auto& plan:*plans) if (auto* visits=plan["visits"].getArray())
                for (const auto& visit:*visits) if (auto* ids=visit["sourceSectionIds"].getArray())
                    for (int i=ids->size(); --i>=0;) if (ids->getReference(i)==oldId) ids->insert(i+1,newId);
        if (auto* scales=score["harmony"]["scales"].getArray()) for (auto& scale:*scales)
            if (auto* targets=scale["targets"].getArray()) for (int i=targets->size(); --i>=0;) {
                const auto target=targets->getReference(i);
                if (target["type"]==(lane==TimelineLane::section ? "section" : "chord") && target["id"]==oldId) {
                    auto right=target.clone(); right.getDynamicObject()->setProperty("id",newId);
                    targets->insert(i+1,right);
                }
            }
    }
    // Keep any authored reference to a removed ID valid. A value edit retains the ID.
    const auto json = juce::JSON::toString(score, true, 17);
    for (const auto& item : remove) {
        const auto id = item["id"].toString();
        if (id.isEmpty())
            continue;
        const bool retained =
            std::any_of(list->begin(), list->end(), [&](const auto& e) { return e["id"].toString() == id; });
        require(retained || !json.contains(juce::JSON::toString(juce::var(id), true)),
                "Another score object refers to this event.");
    }
    reviseDocument(score, old, "timeline-" + juce::Uuid().toString());
    auto bytes = editedScoreBytes(base, score, {"timeline", "harmony", "performancePlans", "document"});
    validateVersion(parseObject(bytes)["version"].toString(), bytes);
    auto result = restoreNoteScore(bytes);
    if (result.wasOk())
        before = old;
    return result;
} catch (const std::exception& e) {
    return juce::Result::fail(e.what());
}

juce::Result OstfpPreview::extendedForImport(juce::int64 end, std::unique_ptr<OstfpPreview>& draft,
                                            juce::MemoryBlock& original) const try {
    original = savedScore(*this);
    auto score = parseObject(original);
    score["timeline"].getDynamicObject()->setProperty("duration", end);
    reviseDocument(score, original, "import-" + juce::Uuid().toString());
    auto bytes = editedScoreBytes(original, score, {"timeline", "document"});
    auto next = projectScore(score, chordVoicingTrack.has_value());
    next->sourceScore = std::move(bytes);
    next->sourceManifest = sourceManifest;
    next->trackKinds = trackKinds;
    next->panAutomation = panAutomation;
    next->savedMix = savedMix;
    next->audio = audio;
    draft = std::move(next);
    return juce::Result::ok();
} catch (const std::exception& error) { return juce::Result::fail(error.what()); }

OstfpPreview::MidiClipCopy OstfpPreview::copyMidiClip(size_t track, const juce::String& id) const {
    if (track >= sourceTrackCount() || trackKinds[track] == TrackKind::audio) return {};
    const auto score = parseObject(savedScore(*this));
    for (const auto& clip : array(score["tracks"][int(track)]["clips"])) {
        if (clip["id"] == id)
            return {clip.clone(), integer(score["extensions"][loopNamespace]["data"].getProperty(id, 0)), trackKinds[track]};
    }
    return {};
}

juce::Result OstfpPreview::placeMidiClip(const MidiClipCopy& payload, size_t destination, juce::int64 tick,
                                         std::optional<size_t> moveFrom, const juce::String& joinTo, juce::MemoryBlock& before) try {
    require(payload.data.isObject() && destination < sourceTrackCount() && trackKinds[destination] == payload.kind,
            "Choose a matching destination track.");
    const auto original = savedScore(*this);
    const auto base = savedScore(*this, true);
    auto score = parseObject(base);
    auto tracks = score["tracks"];
    auto dest = arrayProperty(tracks[int(destination)], "clips");
    auto clip = payload.data.clone();
    const auto id = clip["id"].toString();
    const auto length = integer(clip["duration"]);
    require(tick >= 0 && length > 0 && tick <= 0x0fffffff - length, "Invalid clip range.");
    require(!clip.hasProperty("vocalPerformances") || (moveFrom == destination && joinTo.isEmpty()),
            "This clip contains linked vocal performances.");
    auto extensions = score.getProperty("extensions", object({}));
    auto settings = extensions.getProperty(loopNamespace, object({{"version", "1.0"}, {"status", "current"},
        {"dependencies", object({{"scope", "document"}})}, {"data", object({})}}));
    require(settings["version"] == "1.0" && settings["data"].isObject(), "Unknown MIDI loop settings version.");
    auto* periods = settings["data"].getDynamicObject();
    if (moveFrom) {
        require(*moveFrom < sourceTrackCount(), "Select a MIDI clip.");
        auto source = tracks[int(*moveFrom)]["clips"];
        const auto pos = std::find_if(array(source).begin(), array(source).end(), [&](const auto& c) { return c["id"] == id; });
        require(pos != array(source).end(), "Select a MIDI clip.");
        source.getArray()->remove(int(pos - array(source).begin()));
    } else {
        IdCopies ids;
        collectOwnedIds(clip, ids, true);
        remapIds(clip, {}, ids);
    }
    if (payload.kind == TrackKind::drum && clip["notes"].isArray()) {
        const auto& pads = slotsByTrack.at(destination);
        for (auto note : array(clip["notes"])) {
            const auto slot = note["soundSlotId"];
            if (!slot.isString() || std::any_of(pads.begin(), pads.end(), [&](const auto& pad) { return pad.id == slot.toString(); })) continue;
            auto slots = score["soundSlots"];
            auto found = std::find_if(array(slots).begin(), array(slots).end(), [&](const auto& s) { return s["id"] == slot; });
            require(found != array(slots).end() && (*found)["midiMapping"].hasProperty("note"), "Choose a matching destination track.");
            const int pitch = int((*found)["midiMapping"]["note"]);
            note.getDynamicObject()->setProperty("soundSlotId", drumSlot(score, tracks[int(destination)], pitch));
        }
    }
    juce::var left;
    for (const auto& existing : array(dest)) {
        if (joinTo.isNotEmpty() && existing["id"] == joinTo) { left = existing; continue; }
        const auto start = integer(existing["start"]);
        require(tick + length <= start || tick >= start + integer(existing["duration"]), "The destination contains another clip.");
    }
    for (const auto key : {"notes", "midiEvents"}) if (clip[key].isArray())
        for (const auto& item : array(clip[key])) require(tick + integer(item["start"]) >= 0, "Invalid clip range.");
    if (joinTo.isNotEmpty()) {
        require(moveFrom == destination && left.isObject() && !left.hasProperty("vocalPerformances")
                && bool(left.getProperty("enabled", true)) == bool(clip.getProperty("enabled", true)), "These clips cannot be joined.");
        const auto offset = tick - integer(left["start"]);
        require(offset >= integer(left["duration"]), "These clips cannot be joined.");
        for (const auto key : {"notes", "midiEvents"}) if (clip[key].isArray()) {
            auto list = arrayProperty(left, key);
            for (auto item : array(clip[key])) {
                item.getDynamicObject()->setProperty("start", integer(item["start"]) + offset);
                list.getArray()->add(item);
            }
        }
        left.getDynamicObject()->setProperty("duration", offset + length);
        periods->removeProperty(joinTo);
        periods->removeProperty(id);
        require(!referencesNote(score, {id}), "Another score object refers to this clip.");
    } else {
        clip.getDynamicObject()->setProperty("start", tick);
        dest.getArray()->add(clip);
        if (payload.period > 0) periods->setProperty(clip["id"].toString(), payload.period);
    }
    if (periods->getProperties().size()) extensions.getDynamicObject()->setProperty(loopNamespace, settings);
    else extensions.getDynamicObject()->removeProperty(loopNamespace);
    if (extensions.getDynamicObject()->getProperties().size() || score.hasProperty("extensions"))
        score.getDynamicObject()->setProperty("extensions", extensions);
    if (tick + length > durationTicks) score["timeline"].getDynamicObject()->setProperty("duration", tick + length);
    reviseDocument(score, original, "clip-" + juce::Uuid().toString());
    auto bytes = editedScoreBytes(base, score, {"tracks", "timeline", "soundSlots", "drumKits", loopNamespace, "document"});
    require(bytes.getSize() <= maxBytes, "OSTF score is too large.");
    validateVersion(parseObject(bytes)["version"].toString(), bytes);
    auto result = restoreNoteScore(bytes);
    if (result.wasOk()) before = original;
    return result;
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::editMidiClip(size_t track, const juce::String& id, MidiClipEdit edit,
                                          juce::int64 tick, juce::MemoryBlock& before) try {
    require(track < sourceTrackCount() && trackKinds[track] != TrackKind::audio, "Select a MIDI clip.");
    const auto original = savedScore(*this);
    const auto base = savedScore(*this, true);
    auto score = parseObject(base);
    auto clips = score["tracks"][int(track)]["clips"];
    juce::var clip;
    for (const auto& c : array(clips)) if (c["id"] == id) { clip = c; break; }
    require(clip.isObject(), "Select a MIDI clip.");
    const auto start = integer(clip["start"]), length = integer(clip["duration"]);
    auto extensions = score["extensions"];
    if (!extensions.isObject()) extensions = object({});
    auto settings = extensions[loopNamespace];
    require(settings.isVoid() || settings["version"] == "1.0", "Unknown MIDI loop settings version.");
    if (settings.isVoid()) settings = object({{"version", "1.0"}, {"status", "current"},
        {"dependencies", object({{"scope", "document"}})}, {"data", object({})}});
    auto periods = settings["data"];
    require(periods.isObject(), "Invalid MIDI loop settings.");
    const auto period = integer(periods.getProperty(id, 0));
    require(!clip.hasProperty("vocalPerformances") || (edit == MidiClipEdit::toggleLoop && period > 0),
            "This clip contains linked vocal performances.");
    auto fresh = [](const char* prefix) { return juce::String(prefix) + juce::Uuid().toString(); };
    std::set<juce::String> removedIds;
    if (edit == MidiClipEdit::split) {
        const auto split = tick - start;
        require(split > 0 && split < length, "Choose a point inside the clip.");
        auto right = clip.clone();
        right.getDynamicObject()->setProperty("id", fresh("clip-"));
        right.getDynamicObject()->setProperty("start", tick);
        right.getDynamicObject()->setProperty("duration", length - split);
        clip.getDynamicObject()->setProperty("duration", split);
        for (const auto key : {"notes", "midiEvents"}) {
            if (!clip.hasProperty(key)) continue;
            juce::Array<juce::var> leftEvents, rightEvents;
            for (auto event : array(clip[key])) {
                const auto at = integer(event["start"]);
                const auto gate = integer(event.getProperty("duration", 0));
                if (at < split) leftEvents.add(event);
                if (at >= split || at + gate > split) {
                    auto moved = event.clone();
                    moved.getDynamicObject()->setProperty("start", std::max<juce::int64>(0, at - split));
                    if (at < split) {
                        moved.getDynamicObject()->setProperty("id", fresh("note-"));
                        moved.getDynamicObject()->setProperty("duration", at + gate - split);
                        event.getDynamicObject()->setProperty("duration", split - at);
                    }
                    rightEvents.add(moved);
                }
            }
            clip.getDynamicObject()->setProperty(key, leftEvents);
            right.getDynamicObject()->setProperty(key, rightEvents);
        }
        clips.getArray()->add(right);
        periods.getDynamicObject()->removeProperty(id);
    } else if (edit == MidiClipEdit::toggleLoop) {
        if (period > 0) periods.getDynamicObject()->removeProperty(id);
        else {
            for (const auto key : {"notes", "midiEvents"}) if (clip[key].isArray())
                for (const auto& e : array(clip[key])) {
                    const auto at = integer(e["start"]);
                    require(at >= 0 && at < length && integer(e.getProperty("duration", 0)) <= length - at,
                            "Split or trim events outside the clip before looping.");
                }
            periods.getDynamicObject()->setProperty(id, length);
        }
    } else {
        const auto size = tick - start;
        require(period > 0 && size >= period && tick <= std::numeric_limits<int>::max() * double(ticksPerBar),
                "Keep at least one complete MIDI cycle.");
        for (const auto key : {"notes", "midiEvents"}) {
            if (!clip[key].isArray()) continue;
            const bool notes = juce::String(key) == "notes";
            juce::Array<juce::var> output, pattern;
            std::multimap<juce::int64, juce::var> clippedNotes;
            for (auto event : array(clip[key])) {
                const auto at = integer(event["start"]);
                if (at < period && at >= 0) pattern.add(event);
                if (at >= size) { removedIds.insert(event["id"].toString()); continue; }
                if (notes) {
                    const auto end = at + integer(event["duration"]);
                    if (end > size) event.getDynamicObject()->setProperty("duration", size - at);
                    if (end == length && at >= period) clippedNotes.emplace(at, event);
                }
                output.add(event);
            }
            if (size > length && !pattern.isEmpty()) {
                const auto cycles = (size - 1) / period - length / period + 1;
                require(cycles <= 100000 / pattern.size(), "Too many MIDI events in this loop.");
                for (auto origin = length / period * period; origin < size; origin += period) {
                    for (const auto& source : pattern) {
                        const auto at = origin + integer(source["start"]);
                        if (at >= size) continue;
                        const auto gate = notes ? std::min(integer(source["duration"]), size - at) : 0;
                        if (at < length) {
                            if (!notes || at + gate <= length) continue;
                            const auto matches = clippedNotes.equal_range(at);
                            for (auto it = matches.first; it != matches.second; ++it) {
                                auto e = it->second;
                                if (e["pitch"] == source["pitch"] && e["soundSlotId"] == source["soundSlotId"] && e["channel"] == source["channel"]) {
                                    e.getDynamicObject()->setProperty("duration", gate);
                                    clippedNotes.erase(it);
                                    break;
                                }
                            }
                            continue;
                        }
                        auto copy = source.clone();
                        copy.getDynamicObject()->setProperty("id", fresh(notes ? "note-" : "midi-"));
                        copy.getDynamicObject()->setProperty("start", at);
                        if (notes) copy.getDynamicObject()->setProperty("duration", gate);
                        output.add(copy);
                    }
                }
            }
            clip.getDynamicObject()->setProperty(key, output);
        }
        clip.getDynamicObject()->setProperty("duration", size);
        if (tick > durationTicks) score["timeline"].getDynamicObject()->setProperty("duration", tick);
    }
    require(removedIds.empty() || !referencesNote(score, removedIds), "A linked MIDI event cannot be removed.");
    if (periods.getDynamicObject()->getProperties().size()) extensions.getDynamicObject()->setProperty(loopNamespace, settings);
    else extensions.getDynamicObject()->removeProperty(loopNamespace);
    if (extensions.getDynamicObject()->getProperties().size() || score.hasProperty("extensions"))
        score.getDynamicObject()->setProperty("extensions", extensions);
    reviseDocument(score, original, fresh("studio-"));
    auto bytes = editedScoreBytes(base, score, {"tracks", "timeline", "soundSlots", "drumKits", loopNamespace, "document"});
    require(bytes.getSize() <= maxBytes, "OSTF score is too large.");
    validateVersion(parseObject(bytes)["version"].toString(), bytes);
    auto result = restoreNoteScore(bytes);
    if (result.wasOk()) before = original;
    return result;
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::restoreNoteScore(const juce::MemoryBlock& bytes) try {
    require(bytes.getSize() <= maxBytes, "OSTF score is too large.");
    auto next = projectScore(parseObject(bytes), chordVoicingTrack.has_value());
    require(next->sourceTrackCount() == sourceTrackCount() && next->chordVoicingTrack == chordVoicingTrack, "Track layout changed.");
    next->sourceScore = bytes;
    next->sourceManifest = sourceManifest;
    next->trackKinds = trackKinds;
    next->panAutomation = std::move(panAutomation);
    next->savedMix = std::move(savedMix);
    next->audio = std::move(audio);
    *this = std::move(*next);
    return juce::Result::ok();
} catch (const std::exception& error) { return juce::Result::fail(error.what()); }

namespace {
juce::var editableClip(juce::var clips, juce::int64 tick, juce::int64 bar, juce::int64 limit, bool enabledOnly = false,
                       juce::Range<juce::int64> preferred = {}) {
    auto start = tick / bar * bar;
    auto end = std::min(limit, start + bar);
    preferred = preferred.getIntersectionWith({0,limit});
    if (preferred.contains(tick)) { start = preferred.getStart(); end = preferred.getEnd(); }
    for (const auto& c : array(clips)) {
        if (enabledOnly && !bool(c.getProperty("enabled", true))) continue;
        const auto a = integer(c["start"]), b = a + integer(c["duration"]);
        if (a <= tick && tick < b) return c;
        if (b <= tick) start = std::max(start, b);
        if (a > tick) end = std::min(end, a);
    }
    auto c = object({{"id", "clip-" + juce::Uuid().toString()}, {"name", "MIDI"}, {"start", start}, {"duration", end - start}});
    clips.getArray()->add(c);
    return c;
}
}

juce::Result OstfpPreview::setPatchAt(std::size_t track, juce::int64 tick, int channel, std::array<int,3> values,
                                      juce::MemoryBlock& before, bool sharedPart, bool programOnly) try {
    require(track < sourceTrackCount() && tick >= 0 && tick < playbackEndTick() && channel >= 1 && channel <= 16, "Invalid patch destination.");
    require(std::all_of(values.begin(), values.end(), [](int v) { return v >= -1 && v <= 127; }), "Invalid MIDI patch.");
    before = savedScore(*this);
    auto score = parseObject(before), owner = score["tracks"][int(track)], clips = arrayProperty(owner, "clips");
    for (int field = programOnly ? 2 : 0; field < 3; ++field) {
        const int cc = field == 0 ? 0 : 32;
        const auto type = field == 2 ? "programChange" : "controlChange";
        juce::var destination, previous;
        for (auto clip : array(clips)) {
            if (!bool(clip.getProperty("enabled", true))) continue;
            auto events = clip["midiEvents"];
            if (auto* list = events.getArray()) for (int i = list->size(); --i >= 0;) {
                const auto e = (*list)[i];
                if (integer(clip["start"]) + integer(e["start"]) == tick && e["type"] == type
                    && (field == 2 || int(e["controller"]) == cc)
                    && (sharedPart || int(e.getProperty("channel", channel)) == channel)) {
                    destination = clip;
                    previous = e;
                    list->remove(i);
                }
            }
        }
        if (values[field] < 0) continue;
        if (!destination.isObject()) destination = editableClip(clips, tick, ticksPerBar, playbackEndTick(), true);
        auto event = previous.isObject() ? previous : object({{"id", "midi-" + juce::Uuid().toString()}, {"type", type}});
        event.getDynamicObject()->setProperty("start", tick - integer(destination["start"]));
        event.getDynamicObject()->setProperty("order", field == 2 ? 1 : field - 2);
        if (owner["midi"]["mode"].isString() && owner["midi"]["mode"] != "singleChannel")
            event.getDynamicObject()->setProperty("channel", channel);
        if (field < 2) event.getDynamicObject()->setProperty("controller", cc);
        event.getDynamicObject()->setProperty(field == 2 ? "program" : "value", values[field]);
        arrayProperty(destination, "midiEvents").getArray()->add(event);
    }
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    return restoreNoteScore(editedScoreBytes(before, score, {"tracks", "document"}));
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::editController(std::size_t track, const MidiControl& key, const juce::String& id,
                                          juce::int64 tick, std::optional<int> value, juce::MemoryBlock& before) try {
    require(track < sourceTrackCount() && trackKinds[track] != TrackKind::audio && tick >= 0 && tick < playbackEndTick(), "Invalid controller position.");
    require(key.type == "programChange" || key.type == "controlChange" || key.type == "pitchBend" || key.type == "polyPressure" || key.type == "channelPressure", "Unknown controller.");
    require(key.number >= 0 && key.number <= (key.type == "controlChange" ? 119 : 127) && key.channel >= 0 && key.channel <= 16
            && (!value || (*value >= 0 && *value <= key.maximum())), "Invalid MIDI value.");
    before = savedScore(*this);
    auto score = parseObject(before), owner = score["tracks"][int(track)], clips = arrayProperty(owner, "clips");
    juce::var event, source;
    if (id.isNotEmpty()) {
        for (const auto& c : array(clips)) if (c["midiEvents"].isArray())
            for (const auto& e : array(c["midiEvents"])) if (e["id"] == id) { event = e; source = c; }
        require(event.isObject() && event["type"] == key.type, "The selected event has changed.");
        auto list = source["midiEvents"];
        list.getArray()->removeFirstMatchingValue(event);
    }
    if (value) {
        if (!event.isObject()) {
            event = object({{"id", "midi-" + juce::Uuid().toString()}, {"type", key.type}});
            auto* e = event.getDynamicObject();
            if (key.type == "controlChange") {
                e->setProperty("controller", key.number);
                if (key.number == 0 || key.number == 32) e->setProperty("order", key.number == 0 ? -2 : -1);
            }
            if (key.type == "polyPressure") e->setProperty("pitch", key.number);
            if (key.channel || (owner["midi"]["mode"].isString() && owner["midi"]["mode"] != "singleChannel")) e->setProperty("channel", key.channel ? key.channel : sourceMidiChannels[track]);
        }
        auto dest = source;
        if (!dest.isObject() || tick < integer(dest["start"]) || tick >= integer(dest["start"]) + integer(dest["duration"]))
            dest = editableClip(clips, tick, ticksPerBar, playbackEndTick());
        event.getDynamicObject()->setProperty("start", tick - integer(dest["start"]));
        event.getDynamicObject()->setProperty(key.type == "programChange" ? "program" : key.type == "pitchBend" ? "value14" : "value", *value);
        auto events = arrayProperty(dest, "midiEvents");
        events.getArray()->add(event);
        std::stable_sort(events.getArray()->begin(), events.getArray()->end(), [](const auto& a, const auto& b){return integer(a["start"]) < integer(b["start"]);});
    }
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    return restoreNoteScore(editedScoreBytes(before, score, {"tracks", "document"}));
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::editVocalNote(size_t track, const juce::String& id, const VocalNote& edit,
                                         juce::MemoryBlock& before) try {
    require(track < sourceTrackCount() && trackKinds[track] == TrackKind::pitched, "Select a melody note.");
    const auto& notes = pitchedNotes[track];
    const auto pos = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == id; });
    require(pos != notes.end() && std::isfinite(edit.intensity) && edit.intensity >= 0 && edit.intensity <= 1,
            "Invalid vocal note.");
    const auto found = vocalNotes.find(id);
    const VocalNote original = found == vocalNotes.end() ? VocalNote{} : found->second;
    const auto reading = edit.pronunciation.trim().isEmpty() ? edit.text : edit.pronunciation.trim();
    const bool wordsChanged = original.text != edit.text || original.pronunciation != reading;
    const bool techniqueChanged = original.technique != edit.technique
        || (edit.technique.isNotEmpty() && std::abs(edit.intensity - original.intensity) > 0.00001);
    if (!wordsChanged && !techniqueChanged) return juce::Result::ok();
    before = savedScore(*this);
    auto score = parseObject(before);
    if (!score["lyrics"].isObject()) score.getDynamicObject()->setProperty("lyrics", object({}));
    auto lyrics = score["lyrics"];
    for (const auto* key : {"documents","phrases","units","alignments"}) arrayProperty(lyrics, key);
    auto lookup = [](juce::var list, const juce::var& key) -> juce::var {
        if (list.isArray()) for (auto item : *list.getArray()) if (item["id"] == key) return item;
        return {};
    };
    auto newId = [] { return "vocal-" + juce::Uuid().toString(); };
    auto clip = lookup(score["tracks"][int(track)]["clips"], pos->clipId);
    require(clip.isObject(), "Select a melody clip.");
    juce::var alignment;
    for (const auto& item : array(lyrics["alignments"])) {
        if (!array(item["noteIds"]).contains(id)) continue;
        require(!alignment.isObject(), "This note has several lyric alignments.");
        alignment = item;
    }
    auto unit = alignment.isObject() ? lookup(lyrics["units"], alignment["unitIds"][0]) : juce::var{};
    auto phrase = lookup(lyrics["phrases"], unit["phraseId"]);
    auto documentId = phrase["documentId"].toString();
    auto voice = lookup(lyrics["voices"], alignment["voiceId"]);
    if (!voice.isObject() && lyrics["voices"].isArray()) for (const auto& candidate : array(lyrics["voices"])) {
        if (candidate["trackId"].toString() == trackIds[int(track)]
            && (documentId.isEmpty() || candidate["documentId"].toString() == documentId)) { voice = candidate; break; }
    }
    if (documentId.isEmpty()) documentId = voice["documentId"].toString();
    if (documentId.isEmpty()) {
        documentId = newId();
        arrayProperty(lyrics, "documents").getArray()->add(object({{"id",documentId}}));
    }
    if (!voice.isObject()) {
        voice = object({{"id", newId()}, {"trackId", trackIds[int(track)]}, {"documentId", documentId}});
        arrayProperty(lyrics, "voices").getArray()->add(voice);
    }
    const auto performances = clip["vocalPerformances"];
    juce::var performance;
    if (performances.isArray()) for (auto item : array(performances))
        if (item["voiceId"] == voice["id"]) { performance = item; break; }
    if (wordsChanged) {
        if (!alignment.isObject()) {
            phrase = object({{"id",newId()}, {"documentId",documentId}, {"text",edit.text}});
            arrayProperty(lyrics,"phrases").getArray()->add(phrase);
            unit = object({{"id",newId()}, {"phraseId",phrase["id"]}, {"text",edit.text}});
            arrayProperty(lyrics,"units").getArray()->add(unit);
            alignment = object({{"id",newId()}, {"voiceId",voice["id"]}, {"unitIds",juce::Array<juce::var>{unit["id"]}},
                                {"noteIds",juce::Array<juce::var>{id}}});
            arrayProperty(lyrics,"alignments").getArray()->add(alignment);
            if (performance["noteIds"].isArray()) performance["noteIds"].getArray()->addIfNotAlreadyThere(id);
        } else {
            const bool shared = std::count_if(array(lyrics["alignments"]).begin(), array(lyrics["alignments"]).end(),
                [&](const auto& other) { return array(other["unitIds"]).contains(unit["id"]); }) > 1;
            require(unit.isObject() && !shared && array(alignment["noteIds"]).size() == 1 && array(alignment["unitIds"]).size() == 1,
                    "This lyric is shared by multiple notes or units.");
            unit.getDynamicObject()->setProperty("text", edit.text);
        }
        if (reading != edit.text) unit.getDynamicObject()->setProperty("pronunciation", reading);
        else unit.getDynamicObject()->removeProperty("pronunciation");
    }
    if (techniqueChanged) {
        require(edit.technique.isEmpty() || edit.technique == "vibrato" || edit.technique == "scoop"
                || edit.technique == original.technique, "Unsupported vocal technique.");
        if (!performance.isObject()) {
            performance = object({{"id",newId()}, {"voiceId",voice["id"]}});
            arrayProperty(clip,"vocalPerformances").getArray()->add(performance);
        }
        if (!alignment.hasProperty("voiceId") && alignment.isObject())
            alignment.getDynamicObject()->setProperty("voiceId", voice["id"]);
        if (!alignment.isObject() && !performance["noteIds"].isArray()) {
            auto owned = arrayProperty(performance, "noteIds");
            for (const auto& a : array(lyrics["alignments"])) if (a["voiceId"] == voice["id"])
                for (const auto& n : array(a["noteIds"])) if (lookup(clip["notes"],n).isObject()) owned.getArray()->addIfNotAlreadyThere(n);
        }
        if (performance["noteIds"].isArray()) performance["noteIds"].getArray()->addIfNotAlreadyThere(id);
        auto list = arrayProperty(performance, "techniques");
        auto* events = list.getArray();
        int index = events->size();
        while (--index >= 0 && !((*events)[index]["targetNoteId"] == id && (*events)[index]["kind"] == original.technique)) {}
        if (edit.technique.isEmpty()) { if (index >= 0) events->remove(index); }
        else {
            auto event = index >= 0 ? (*events)[index] : object({{"id",newId()}, {"targetNoteId",id},
                {"start",pos->startTick - integer(clip["start"])}, {"duration",pos->endTick-pos->startTick}});
            event.getDynamicObject()->setProperty("kind",edit.technique);
            event.getDynamicObject()->setProperty("intensity",edit.intensity);
            if (index < 0) events->add(event);
        }
    }
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    return restoreNoteScore(editedScoreBytes(before, score, {"tracks","lyrics","document"}));
} catch (const std::exception& error) { return juce::Result::fail(error.what()); }

const StudioMidiTakes& OstfpPreview::takes(size_t index) const {
    static const StudioMidiTakes initial;
    const auto it=midiTakes.find(trackIds[int(index)]);
    return it==midiTakes.end() ? initial : it->second;
}

namespace {
void writeTrackTakes(juce::var score, juce::var track, StudioMidiTakes& takes) {
    takes.project(track);
    auto extensions=score.getProperty("extensions",object({}));
    auto ext=extensions.getProperty(StudioMidiTakes::extension,object({{"version","1.0"},{"status","current"},
            {"dependencies",object({{"scope","document"}})},{"data",object({})}}));
    require(ext["version"]=="1.0" && ext["data"].isObject(),"Unsupported Studio take data.");
    ext["data"].getDynamicObject()->setProperty(track["id"].toString(),takes.encode());
    extensions.getDynamicObject()->setProperty(StudioMidiTakes::extension,ext);
    score.getDynamicObject()->setProperty("extensions",extensions);
}
}
namespace {
void appendWholeTake(juce::var group, juce::var clips, juce::Range<juce::int64> ticks, juce::var output, bool active) {
    auto source=output.clone();
    const auto uuid=juce::Uuid().toString();
    const auto takeId="take-"+uuid, compId="comp-"+uuid;
    source.getDynamicObject()->setProperty("id","source-"+uuid);
    source.getDynamicObject()->setProperty("enabled",false);
    output.getDynamicObject()->setProperty("enabled",active);
    juce::Array<juce::var> links;
    for (const auto key : {"notes","midiEvents"}) if (source[key].isArray())
        for (auto e : array(source[key])) {
            const auto old=e["id"].toString(), fresh="event-"+juce::Uuid().toString();
            e.getDynamicObject()->setProperty("id",fresh);
            if (juce::String(key)=="notes") links.add(object({{"sourceNoteId",fresh},{"outputNoteId",old}}));
        }
    clips.getArray()->add(source);
    const auto first=integer(output["start"]), dur=integer(output["duration"]);
    juce::Array<juce::var> regions;
    auto silence=[&](juce::int64 a,juce::int64 b) {if (a<b) regions.add(object({{"id","r-"+juce::Uuid().toString()},
                    {"kind","silence"},{"startTick",a},{"durationTicks",b-a}}));};
    silence(ticks.getStart(),first);
    regions.add(object({{"id","r-"+uuid},{"kind","source"},{"startTick",first},{"durationTicks",dur},
                        {"takeId",takeId},{"clipId",source["id"]}}));
    silence(first+dur,ticks.getEnd());
    const int ordinal=group["takes"].size();
    arrayProperty(group,"takes").getArray()->add(object({{"id",takeId},{"kind","take"},{"laneOrder",ordinal},{"clipIds",juce::Array<juce::var>{source["id"]}}}));
    auto comp=object({{"id",compId},{"name","Take "+juce::String(ordinal+1)},{"noteBoundary","authored"},{"compRegions",regions},
                      {"outputClipIds",juce::Array<juce::var>{output["id"]}}});
    if(!links.isEmpty()) comp.getDynamicObject()->setProperty("noteLinks",links);
    arrayProperty(group,"comps").getArray()->add(comp);
    if(active) group.getDynamicObject()->setProperty("activeCompId",compId);

}
}

juce::Result OstfpPreview::addScoreTake(size_t trackIndex, juce::Range<juce::int64> ticks, juce::MemoryBlock& before) try {
    require(trackIndex<sourceTrackCount() && !ticks.isEmpty(),"Select a MIDI track and section.");
    const auto base=savedScore(*this,true);
    auto score=parseObject(base);
    require(score["version"]=="5.1.0","Native comp authoring requires OSTF 5.1.");
    auto track=score["tracks"][int(trackIndex)];
    auto clips=arrayProperty(track,"clips");
    auto groups=arrayProperty(score,"takeGroups");
    juce::var group;
    for (auto item : array(groups)) if (item["trackId"]==track["id"]) {
        const auto start=integer(item["startTick"]);
        const juce::Range<juce::int64> existing(start,start+integer(item["durationTicks"]));
        if (!existing.intersects(ticks)) continue;
        require(existing==ticks,"Select the exact bounds of this take group.");
        group=item;
    }
    auto blank=[&] {return object({{"id","clip-"+juce::Uuid().toString()},{"start",ticks.getStart()},
                        {"duration",ticks.getLength()},{"notes",juce::Array<juce::var>{}}});};
    if (!group.isObject()) {
        juce::var first;
        for (const auto& clip : array(clips)) {
            const auto at=integer(clip["start"]), end=at+integer(clip["duration"]);
            if(!ticks.intersects({at,end})) continue;
            require(!first.isObject() && at>=ticks.getStart() && end<=ticks.getEnd() && bool(clip.getProperty("enabled",true)),
                    "Creating a group here requires one contained clip or an empty section.");
            require(!clip.hasProperty("vocalPerformances") && !clip.hasProperty("clipChoices"),
                    "This clip has linked performances that require a dedicated comp map.");
            for (const auto& prop : clip.getDynamicObject()->getProperties())
                require(juce::StringArray{"id","name","start","duration","notes","midiEvents","enabled"}.contains(prop.name.toString()),
                        "This clip has extra data that requires an authored take map.");
            for (int i=0;i<clip["notes"].size();++i) {
                const auto n=clip["notes"][i]; const auto onset=integer(n["start"]);
                require(onset>=0 && onset+integer(n["duration"])<=end-at && !vocalNotes.contains(n["id"].toString()),"Linked or crossing notes require an authored take map.");
            }
            first=clip;
        }
        if(!first.isObject()) {first=blank(); clips.getArray()->add(first);}
        group=object({{"id","group-"+juce::Uuid().toString()},{"trackId",track["id"]},{"startTick",ticks.getStart()},{"durationTicks",ticks.getLength()}});
        for (const auto& s : array(score["timeline"]["sections"])) if(integer(s["start"])==ticks.getStart() && integer(s["duration"])==ticks.getLength())
            group.getDynamicObject()->setProperty("sectionId",s["id"]);
        appendWholeTake(group,clips,ticks,first,true); groups.getArray()->add(group);
    }
    auto second=blank(); clips.getArray()->add(second); appendWholeTake(group,clips,ticks,second,false);
    reviseDocument(score,base,"take-"+juce::Uuid().toString());
    const auto bytes=editedScoreBytes(base,score,{"takeGroups","tracks","document"});
    validateVersion("5.1.0",bytes);
    const auto r=restoreNoteScore(bytes); if(r.wasOk()) before=base;
    return r;
} catch(const std::exception& e){return juce::Result::fail(e.what());}

namespace {
void applyCompChoice(juce::var& score, const ScoreCompChoice& choice) {
    bool found=false;
    for (auto group : array(score["takeGroups"])) if (group["id"]==choice.group && group["trackId"]==choice.track) {
        std::map<juce::String,bool> enabled;
        found=choice.comp.isEmpty();
        for (const auto& comp : array(group["comps"])) {
            const bool select=comp["id"]==choice.comp;
            found=found || select;
            for (const auto& id : array(comp["outputClipIds"])) enabled[id.toString()]=select;
        }
        require(found,"Unknown comp selection.");
        group.getDynamicObject()->setProperty("activeCompId",choice.comp.isEmpty() ? juce::var{} : juce::var(choice.comp));
        for (const auto& lane : array(score["tracks"])) if (lane["id"]==choice.track)
            for (auto clip : array(lane["clips"]))
                if (auto it=enabled.find(clip["id"].toString()); it!=enabled.end()) clip.getDynamicObject()->setProperty("enabled",it->second);
        break;
    }
    require(found,"Unknown take group.");
}
}

juce::Result OstfpPreview::compPlayback(const std::vector<ScoreCompChoice>& choices, std::unique_ptr<OstfpPreview>& playback) const try {
    auto score = parseObject(savedScore(*this));
    for (const auto& choice : choices) applyCompChoice(score, choice);
    auto projected = projectScore(score, chordVoicingTrack.has_value());
    projected->sourceScore = jsonBytes(score);
    projected->trackKinds = trackKinds;
    projected->panAutomation = panAutomation;
    playback = std::move(projected);
    return juce::Result::ok();
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

juce::Result OstfpPreview::selectScoreComp(const ScoreCompChoice& choice, juce::MemoryBlock& before) {
    return selectScoreComps({choice}, before);
}

juce::Result OstfpPreview::selectScoreComps(const std::vector<ScoreCompChoice>& choices, juce::MemoryBlock& before) try {
    const auto base = savedScore(*this, true);
    auto score = parseObject(base);
    for (const auto& choice : choices) applyCompChoice(score, choice);
    reviseDocument(score,base,"comp-"+juce::Uuid().toString());
    const auto bytes=editedScoreBytes(base,score,{"tracks","takeGroups","document"});
    validateVersion("5.1.0",bytes);
    const auto result=restoreNoteScore(bytes);
    if(result.wasOk()) before=base;
    return result;
} catch(const std::exception& e){return juce::Result::fail(e.what());}

juce::Result OstfpPreview::addMidiTake(size_t index, juce::MemoryBlock& before) try {
    require(index<sourceTrackCount() && trackKinds[index]!=TrackKind::audio,"Select a MIDI track.");
    const auto original=savedScore(*this), base=savedScore(*this,true);
    auto score=parseObject(base);
    auto state=takes(index);
    require(state.count<999,"Take limit reached.");
    ++state.count;
    writeTrackTakes(score,score["tracks"][int(index)],state);
    reviseDocument(score,original,"take-"+juce::Uuid().toString());
    auto bytes=editedScoreBytes(base,score,{StudioMidiTakes::extension,"tracks","document"});
    validateVersion(parseObject(bytes)["version"].toString(),bytes);
    const auto result=restoreNoteScore(bytes);
    if(result.wasOk()) before=original;
    return result;
} catch(const std::exception& e){return juce::Result::fail(e.what());}

juce::Result OstfpPreview::adoptMidiTakes(const std::vector<TakeSelection>& choices,juce::MemoryBlock& before) try {
    const auto original=savedScore(*this), base=savedScore(*this,true);
    auto score=parseObject(base);
    std::map<size_t,StudioMidiTakes> changed;
    for (const auto& c:choices) {
        require(c.track<sourceTrackCount() && trackKinds[c.track]!=TrackKind::audio && !c.ticks.isEmpty()
            && c.ticks.getStart()>=0 && c.ticks.getEnd()<=playbackEndTick() && c.number>0 && c.number<=takes(c.track).count,"Invalid take selection.");
        changed.try_emplace(c.track,takes(c.track)).first->second.adopt(c.ticks,c.number);
    }
    for (auto& [index,state]:changed) writeTrackTakes(score,score["tracks"][int(index)],state);
    reviseDocument(score,original,"take-"+juce::Uuid().toString());
    const auto bytes=editedScoreBytes(base,score,{"tracks",StudioMidiTakes::extension,"document"});
    validateVersion(parseObject(bytes)["version"].toString(),bytes);
    const auto result=restoreNoteScore(bytes);
    if(result.wasOk()) before=original;
    return result;
} catch(const std::exception& e){return juce::Result::fail(e.what());}

juce::Result OstfpPreview::editNotes(std::size_t index, const std::vector<NoteEdit>& edits,
                                     juce::MemoryBlock& before, juce::StringArray& resultingIds,
                                     juce::Range<juce::int64> newClipRange,int editingTake) try {
    require(index < sourceTrackCount() && trackKinds[index] != TrackKind::audio && !edits.empty(), "Select a MIDI track.");
    before = savedScore(*this);
    auto score = parseObject(before);
    if (trackKinds[index] == TrackKind::drum)
        materialiseKits(score, *this);
    auto track = score["tracks"][static_cast<int>(index)];
    auto clips = arrayProperty(track, "clips");
    auto takeState=takes(index);
    require(editingTake>=0 && editingTake<=takeState.count,"Unknown take.");
    juce::StringArray ids;
    std::set<juce::String> deleted;
    std::map<juce::String, std::pair<juce::var, juce::var>> existing;
    for (const auto& owner : array(clips)) if (owner["notes"].isArray())
        for (const auto& n : array(owner["notes"])) existing.emplace(n["id"].toString(), std::make_pair(owner, n));
    for (const auto& edit : edits) {
        const auto& n = edit.note;
        juce::var clip, event;
        if (edit.action != NoteEdit::insert) {
            auto found = existing.find(n.id);
            require(found != existing.end(), "The selected note has changed.");
            clip = found->second.first;
            event = found->second.second;
            if (edit.action != NoteEdit::duplicate) existing.erase(found);
        }
        if (edit.action == NoteEdit::erase) {
            auto notes = clip["notes"];
            notes.getArray()->removeFirstMatchingValue(event);
            deleted.insert(n.id);
            continue;
        }
        require(n.startTick >= 0 && n.endTick > n.startTick && n.endTick <= playbackEndTick()
                && (n.pitch >= 0 || (trackKinds[index] == TrackKind::drum && n.slotId.isNotEmpty())) && n.pitch <= 127 && (!n.velocity || (*n.velocity >= 1 && *n.velocity <= 127)), "Note outside the editable range.");
        const bool fresh = edit.action == NoteEdit::insert;
        const auto oldStart = fresh ? 0 : integer(clip["start"]) + integer(event["start"]);
        const bool crossing = !fresh && (n.startTick < integer(clip["start"])
                              || n.startTick >= integer(clip["start"]) + integer(clip["duration"]));
        const bool transfer = edit.action == NoteEdit::change && oldStart != n.startTick && crossing;
        if (fresh || edit.action == NoteEdit::duplicate || transfer) {
            if (transfer) {
                auto oldEvents = clip["notes"];
                oldEvents.getArray()->removeFirstMatchingValue(event);
                require(!referencesNote(score, {n.id}), "A linked note must stay in its clip.");
            } else {
                event = fresh ? object({}) : event.clone();
                event.getDynamicObject()->setProperty("id", "note-" + juce::Uuid().toString());
                const int number=fresh ? (editingTake && (newClipRange.isEmpty() || newClipRange.contains(n.startTick)) ? editingTake : takeState.adoptedAt(n.startTick)) : takeState.takeOf(n.id);
                if (number>1) takeState.owners[event["id"].toString()]=number;
                if (!fresh && takeState.suppressed.contains(n.id)) event.getDynamicObject()->setProperty("enabled",true);
            }
            if (fresh || crossing) clip = editableClip(clips, n.startTick, ticksPerBar, playbackEndTick(), !compChoices.empty(), newClipRange);
            if (fresh && track["midi"]["mode"].isString() && track["midi"]["mode"] != "singleChannel")
                event.getDynamicObject()->setProperty("channel", sourceMidiChannels[index]);
            arrayProperty(clip, "notes").getArray()->add(event);
        }
        auto* obj = event.getDynamicObject();
        obj->setProperty("start", n.startTick - integer(clip["start"]));
        obj->setProperty("duration", n.endTick - n.startTick);
        if (event.hasProperty("soundSlotId") || (fresh && trackKinds[index] == TrackKind::drum)) {
            // Keep the actual slot ID when multiple percussion articulations share a key.
            const auto& drums = drumNotes[index];
            auto old = std::find_if(drums.begin(), drums.end(), [&](const auto& d) { return d.id == n.id; });
            if (fresh && n.slotId.isNotEmpty()) {
                const auto& slots = slotsByTrack.at(index);
                require(std::any_of(slots.begin(), slots.end(), [&](const auto& s){return s.id == n.slotId;}), "Unknown drum slot.");
                obj->setProperty("soundSlotId", n.slotId);
            } else if (fresh || old == drums.end() || old->pitch != n.pitch)
                obj->setProperty("soundSlotId", drumSlot(score, track, n.pitch));
        } else obj->setProperty("pitch", n.pitch);
        if (n.velocity) {
            obj->setProperty("velocity", *n.velocity);
            const int deviation = int(event["velocityDeviation"]);
            if (event.hasProperty("velocityDeviation"))
                obj->setProperty("velocityDeviation", std::clamp(deviation, 1 - *n.velocity, 127 - *n.velocity));
        }
        ids.add(event["id"].toString());
    }
    if (!deleted.empty()) require(!referencesNote(score, deleted), "This note is linked to lyrics or other score data.");
    if (takeState.count>1) writeTrackTakes(score,track,takeState);
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    auto data = editedScoreBytes(before, score, {"tracks", "soundSlots", "drumKits", "document",StudioMidiTakes::extension});
    const auto result = restoreNoteScore(data);
    if (result.wasOk()) resultingIds = std::move(ids);
    return result;
} catch (const std::exception& error) { return juce::Result::fail(error.what()); }

juce::Result OstfpPreview::restoreReviewedNotes(std::size_t track, const ReviewNoteOrigins& origins,
                                              juce::MemoryBlock& before) try {
    require(track < sourceTrackCount() && !origins.empty(), "Select a MIDI track.");
    before = savedScore(*this);
    auto score = parseObject(before);
    auto clips = score["tracks"][int(track)]["clips"];
    require(clips.isArray(), "The clips have changed since this edit.");
    std::map<juce::String, juce::var> owners;
    std::set<juce::String> created, removed;
    std::vector<const ReviewNoteOrigin*> restore;
    for (const auto& [id, origin] : origins) {
        if (origin.event.isObject()) restore.push_back(&origin);
        else removed.insert(id);
        created.insert(origin.createdClips.begin(), origin.createdClips.end());
    }
    for (auto& clip : *clips.getArray()) {
        owners.emplace(clip["id"].toString(), clip);
        auto notes = clip["notes"];
        if (auto* list = notes.getArray())
            for (int i = list->size(); --i >= 0;)
                if (origins.contains(list->getReference(i)["id"].toString())) list->remove(i);
    }
    std::stable_sort(restore.begin(), restore.end(), [](auto* a, auto* b) { return a->index < b->index; });
    for (const auto* origin : restore) {
        auto owner = owners.find(origin->clipId);
        require(owner != owners.end(), "The clips have changed since this edit.");
        auto events = arrayProperty(owner->second, "notes");
        events.getArray()->insert(std::min(origin->index, events.size()), origin->event.clone());
    }
    for (int i = clips.size(); --i >= 0;) {
        const auto& clip = clips.getArray()->getReference(i);
        const auto id = clip["id"].toString();
        if (created.contains(id) && clip["notes"].size() == 0 && clip["midiEvents"].size() == 0) {
            clips.getArray()->remove(i);
            removed.insert(id);
        }
    }
    require(removed.empty() || !referencesNote(score, removed), "A linked MIDI event cannot be removed.");
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    return restoreNoteScore(editedScoreBytes(before, score, {"tracks", "document"}));
} catch (const std::exception& e) { return juce::Result::fail(e.what()); }

bool OstfpPreview::insertGeneratorPattern(std::size_t track, int bar) {
    if (track >= sourceTrackCount() || trackKinds[track] != TrackKind::pitched || track >= pitchedNotes.size() || track >= trackLanes.size() || bar < bars.firstBar || bar >= bars.endBar
        || ppq < 2 || ticksPerBar <= 0) return false;
    const auto length = static_cast<juce::int64>(generatorPitches.size()) * ppq;
    if (length > (bars.endBar - bar) * ticksPerBar) return false;
    auto tick = (bar - bars.firstBar) * ticksPerBar;
    std::vector<PitchedNote> notes;
    for (const auto pitch : generatorPitches) {
        notes.push_back({tick, tick + ppq / 2, pitch, generatorVelocity});
        tick += ppq;
    }
    return addSessionClip(track, "Generated", std::move(notes));
}

juce::Result OstfpPreview::addRecordedClip(size_t index, juce::Range<juce::int64> range,
                                         const std::vector<PitchedNote>& notes, juce::MemoryBlock& before, const juce::String& name,
                                         const juce::Array<juce::var>& midiEvents,int editingTake) try {
    require(index < sourceTrackCount() && trackKinds[index] != TrackKind::audio && (!notes.empty() || !midiEvents.isEmpty()), "No MIDI notes captured");
    before = savedScore(*this);
    auto score = parseObject(before);
    const bool drum = trackKinds[index] == TrackKind::drum;
    if (drum) materialiseKits(score, *this);
    auto track = score["tracks"][int(index)];
    juce::Array<juce::var> events;
    auto takeState=takes(index);
    require(editingTake>=0 && editingTake<=takeState.count,"Unknown take.");
    for (const auto& note : notes) {
        require(note.startTick >= 0 && note.startTick < note.endTick && note.endTick <= playbackEndTick(), "Note outside the editable range.");
        range = range.getUnionWith({note.startTick, note.endTick});
    }
    for (const auto& note : notes) {
        auto event = object({{"id", "note-" + juce::Uuid().toString()}, {"start", note.startTick - range.getStart()},
                             {"duration", note.endTick - note.startTick}, {"velocity", note.velocity.value_or(96)}});
        if (drum) event.getDynamicObject()->setProperty("soundSlotId", drumSlot(score, track, note.pitch));
        else event.getDynamicObject()->setProperty("pitch", note.pitch);
        const int number=editingTake ? editingTake : takeState.adoptedAt(note.startTick);
        if (number>1) takeState.owners[event["id"].toString()]=number;
        if (track["midi"]["mode"].isString() && track["midi"]["mode"] != "singleChannel")
            event.getDynamicObject()->setProperty("channel", sourceMidiChannels[index]);
        events.add(event);
    }
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return integer(a["start"]) < integer(b["start"]); });
    auto clip = object({{"id", "take-" + juce::Uuid().toString()}, {"name", name}, {"start", range.getStart()},
                        {"duration", range.getLength()}, {"notes", events}});
    juce::Array<juce::var> controls;
    for (const auto& event : midiEvents) {
        const auto at = integer(event["start"]);
        require(range.contains(at) && at < playbackEndTick(), "MIDI event outside the editable range.");
        auto copy = event.clone();
        auto* e = copy.getDynamicObject();
        e->setProperty("id", "midi-" + juce::Uuid().toString());
        e->setProperty("start", at - range.getStart());
        if (track["midi"]["mode"] == "singleChannel") e->removeProperty("channel");
        else if (!copy.hasProperty("channel")) e->setProperty("channel", sourceMidiChannels[index]);
        controls.add(copy);
    }
    if (!controls.isEmpty()) clip.getDynamicObject()->setProperty("midiEvents", controls);
    auto trackClips=arrayProperty(track, "clips");
    trackClips.getArray()->add(clip);
    if(score["takeGroups"].isArray()) for(auto group:array(score["takeGroups"])) {
        if(group["trackId"]!=track["id"]) continue;
        const auto start=integer(group["startTick"]);
        const juce::Range<juce::int64> owned(start,start+integer(group["durationTicks"]));
        if(!owned.intersects(range)) continue;
        require(owned.contains(range),"Split the recording at the take-group boundary.");
        appendWholeTake(group,trackClips,owned,clip,false);
        reviseDocument(score,before,"record-"+juce::Uuid().toString());
        const auto bytes=editedScoreBytes(before,score,{"tracks","drumKits","soundSlots","takeGroups","document"});
        validateVersion("5.1.0",bytes);
        return restoreNoteScore(bytes);
    }
    if(takeState.count>1) writeTrackTakes(score,track,takeState);
    reviseDocument(score, before, "studio-" + juce::Uuid().toString());
    return restoreNoteScore(editedScoreBytes(before, score, {"tracks", "drumKits", "soundSlots", "document",StudioMidiTakes::extension}));
} catch (const std::exception& error) { return juce::Result::fail(error.what()); }

bool OstfpPreview::addSessionClip(std::size_t track, juce::String name, std::vector<PitchedNote> notes) {
    if (track >= sourceTrackCount() || notes.empty() || ticksPerBar <= 0) return false;
    auto firstTick = notes.front().startTick;
    auto lastTick = notes.front().endTick;
    const auto endTick = (bars.endBar - bars.firstBar) * ticksPerBar;
    for (const auto& note : notes) {
        if (note.startTick < 0 || note.endTick > endTick || note.startTick >= note.endTick
            || note.pitch < 0 || note.pitch > 127 || (note.velocity && (*note.velocity < 1 || *note.velocity > 127)))
            return false;
        firstTick = std::min(firstTick, note.startTick);
        lastTick = std::max(lastTick, note.endTick);
    }
    const auto first = firstTick / ticksPerBar;
    const auto last = (lastTick - 1) / ticksPerBar + 1;
    if (editedTrackRegions.empty()) editedTrackRegions.resize(trackLanes.size());
    auto& trackRegions = editedTrackRegions[track];
    if (trackRegions.empty() && !trackLanes[track].regions.empty())
        trackRegions.assign(trackLanes[track].regions.begin(), trackLanes[track].regions.end());
    const auto clipId = "studio-" + juce::Uuid().toString();
    trackRegions.push_back({{bars.firstBar + static_cast<int>(first), bars.firstBar + static_cast<int>(last)},
                            keepLabel(name), static_cast<int>(notes.size()),
                            TimelineExtent{double(bars.firstBar + first), double(bars.firstBar + last)}});
    trackRegions.back().clipId = keepLabel(clipId);
    trackLanes[track].regions = trackRegions;
    auto& trackNotes = pitchedNotes[track];
    for (std::size_t i = 0; i < notes.size(); ++i) {
        notes[i].id = clipId + "-note-" + juce::String(static_cast<int>(i + 1));
        notes[i].clipId = clipId;
    }
    trackNotes.insert(trackNotes.end(), notes.begin(), notes.end());
    std::stable_sort(trackNotes.begin(), trackNotes.end(), [](const auto& a, const auto& b) { return a.startTick < b.startTick; });
    addedClips.push_back({track, clipId, std::move(name), first * ticksPerBar,
                          (last - first) * ticksPerBar, std::move(notes)});
    return true;
}

juce::Result readOstfPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result) {
    try {
        const auto length = input.getTotalLength();
        require(length > 0 && length <= maxBytes && input.setPosition(0), "Invalid or oversized OSTF score");
        juce::MemoryBlock bytes;
        require(input.readIntoMemoryBlock(bytes, maxBytes + 1) == static_cast<std::size_t>(length), "Could not read OSTF score");
        const auto score = parseObject(bytes);
        if (score["version"] != "5.0.0") validateVersion(score["version"].toString(), bytes);
        auto next = projectScore(score);
        next->savedMix = readEmbeddedMix(score, next->panAutomation);
        readAudioReferences(score, score["extensions"][mixNamespace]["data"], *next);
        if (next->savedMix)
            for (std::size_t i = 0; i < next->trackKinds.size(); ++i)
                if (i < next->sourceTrackCount() && i < next->savedMix->audioTracks.size() && next->savedMix->audioTracks[i])
                    next->trackKinds[i] = TrackKind::audio;
        next->sourceScore = std::move(bytes);
        result = std::move(next);
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}

juce::Result readOstfpPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result) {
    try {
        require(input.getTotalLength() > 0 && input.getTotalLength() <= maxPackageBytes && input.setPosition(0), "Invalid or oversized OSTFP file");
        unsigned char header[10] {};
        require(input.read(header, sizeof header) == sizeof header && juce::ByteOrder::littleEndianInt(header) == 0x04034b50
                && (header[6] & 1) == 0 && juce::ByteOrder::littleEndianShort(header + 8) == 0 && input.setPosition(0),
                "Expected a stored OSTFP mimetype header");
        juce::ZipFile zip(input);
        require(zip.getNumEntries() >= 3 && zip.getNumEntries() <= 4 + static_cast<int>(OstfpPreview::maxAudioRegions)
                && zip.getEntry(0)->filename == "mimetype" && zip.getEntry(1)->filename == "manifest.json",
                "Preview supports an OSTFP score and optional Studio mixer document");
        const auto mime = readMember(zip, 0);
        require(mime.getSize() == sizeof mediaType - 1 && std::memcmp(mime.getData(), mediaType, mime.getSize()) == 0,
                "Incorrect OSTFP media type");
        const auto manifestBytes = readMember(zip, 1);
        const auto manifest = parseObject(manifestBytes);
        const bool manifest11 = manifest["packageVersion"] == "1.1.0";
        if (manifest11) validateVersion("package-manifest-1.1", manifestBytes);
        require(manifest["packageFormat"] == "OSTFP" && (manifest11 || manifest["packageVersion"] == "1.0.0"), "Unsupported OSTFP manifest version");
        const auto documentData = manifest["documents"];
        const auto& documents = array(documentData);
        const auto assetData = manifest["assets"];
        const auto& assets = array(assetData);
        require(documents.size() <= 1 && assets.size() <= static_cast<int>(OstfpPreview::maxAudioRegions) && zip.getNumEntries() == 3 + documents.size() + assets.size()
                && (manifest["externalAssets"].isVoid() || array(manifest["externalAssets"]).isEmpty()),
                "Unsupported OSTFP documents or assets");
        const auto descriptor = manifest["score"];
        const auto path = descriptor["path"].toString();
        const auto scoreIndex = zip.getIndexOfFileName(path);
        require(descriptor["format"] == "OSTF" && ((manifest11 && descriptor["version"] == "5.1.0") || descriptor["version"] == "5.0.0" || descriptor["version"] == "4.3.0") && safeScorePath(path)
                && scoreIndex >= 2, "Invalid or unsupported OSTFP score entry");
        auto scoreBytes = readMember(zip, scoreIndex);
        const auto hash = juce::SHA256(scoreBytes).toHexString();
        require(hash == descriptor["sha256"].toString(), "OSTFP score SHA-256 mismatch");
        const auto score = parseObject(scoreBytes);
        require(score["version"] == descriptor["version"], "OSTFP manifest and score versions differ");
        const bool is51 = score["version"] == "5.1.0";
        if (is51) validateVersion("5.1.0", scoreBytes);
        if (score["version"] == "4.3.0") {
            require(documents.isEmpty(), "Import the standalone 4.3 score; historical session documents require separate migration");
            validateVersion("4.3.0", scoreBytes);
        }
        auto next = projectScore(score);
        if (!documents.isEmpty()) {
            const auto document = documents[0];
            const auto documentPath = document["path"].toString();
            const auto index = zip.getIndexOfFileName(documentPath);
            require(document["role"] == "productionSession" && document["family"] == "OSTF-Production-Session"
                    && document["version"] == (is51 ? "0.4.0" : "0.3.0") && safeScorePath(documentPath) && index >= 2 && index != scoreIndex,
                    "Unsupported OSTFP companion document");
            const auto bytes = readMember(zip, index);
            require(juce::SHA256(bytes).toHexString() == document["sha256"].toString(), "OSTFP production document SHA-256 mismatch");
            if (is51) validateVersion("production-session-0.4", bytes);
            const auto production = parseObject(bytes);
            for (const auto key : {"documentId","revisionId"})
                require(!production["score"].hasProperty(key) || production["score"][key] == score["document"][juce::String(key) == "documentId" ? "id" : "revisionId"], "Production score identity mismatch");
            require(production["audioTakeGroups"].size() == 0, "This editor cannot yet preserve audio take groups.");
            require(production["profile"] == "OSTF-Production-Session" && production["profileVersion"] == document["version"]
                    && production["score"]["formatVersion"] == score["version"] && production["score"]["sha256"] == hash,
                    "OSTFP production profile or score hash mismatch");
            next->savedMix = readMix(score, production, next->panAutomation);
            const auto source = production.hasProperty("assets") || production.hasProperty("audioRegions")
                ? production : score["extensions"][mixNamespace]["data"];
            readAudioReferences(score, source, *next, manifest, documentPath);
        } else {
            next->savedMix = readEmbeddedMix(score, next->panAutomation);
            readAudioReferences(score, score["extensions"][mixNamespace]["data"], *next, manifest);
        }

        std::map<juce::String, RegionAudioBytes> loadedAssets;
        std::size_t loadedBytes = 0;
        for (auto& audio : next->audio) {
            require(safeScorePath(audio.path), "Invalid OSTFP audio path");
            const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) {
                return item["path"] == audio.path && item["purpose"] == "audioSource";
            });
            const auto index = zip.getIndexOfFileName(audio.path);
            require(asset != assets.end() && index >= 2 && index != scoreIndex, "Missing OSTFP audio asset");
            if (auto it = loadedAssets.find(audio.path); it != loadedAssets.end()) {
                audio.embedded = it->second;
                continue;
            }
            audio.embedded = readMember(zip, index, maxAudioBytes);
            loadedBytes += audio.embedded.getSize();
            require(loadedBytes <= maxPackageBytes - 2 * maxBytes
                    && integer((*asset)["sizeBytes"]) == static_cast<juce::int64>(audio.embedded.getSize())
                    && juce::SHA256(audio.embedded).toHexString() == (*asset)["sha256"].toString(), "OSTFP audio asset hash or size mismatch");
            loadedAssets.emplace(audio.path, audio.embedded);
        }
        require(loadedAssets.size() == assets.size(), "OSTFP audio inventory mismatch");
        if (next->savedMix)
            for (std::size_t i = 0; i < next->trackKinds.size(); ++i)
                if (i < next->sourceTrackCount() && i < next->savedMix->audioTracks.size() && next->savedMix->audioTracks[i])
                    next->trackKinds[i] = TrackKind::audio;
        next->sourceScore = std::move(scoreBytes);
        next->sourceManifest = manifest;
        result = std::move(next);
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}

juce::Result writeOstfPreview(const OstfpPreview& project, juce::OutputStream& output) {
    try {
        const auto bytes = savedScore(project, true);
        require(bytes.getSize() <= maxBytes && output.write(bytes.getData(), bytes.getSize()), "Could not write OSTF score");
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}

juce::Result writeOstfpPreview(const OstfpPreview& project, const SessionMix& mix, juce::OutputStream& output) {
    try {
        juce::Array<juce::var> sources, regions, bindings, assets;
        std::vector<juce::MemoryBlock> media;
        std::size_t total = 0;
        for (const auto& audio : project.audio) {
            auto bytes = audioBytes(audio);
            const auto digest = juce::SHA256(bytes).toHexString();
            auto existing = std::find_if(assets.begin(), assets.end(), [&](const auto& a) { return a["sha256"] == digest; });
            if (existing != assets.end()) {
                describeWav(project, audio, bytes, (*existing)["path"].toString(), digest, sources, regions);
                continue;
            }
            total += bytes.getSize();
            require(total <= maxPackageBytes - 3 * maxBytes && project.audio.size() <= OstfpPreview::maxAudioRegions, "OSTFP audio exceeds preview package capacity");
            const auto id = "audio-" + juce::String(static_cast<int>(media.size() + 1));
            const auto path = "assets/" + id + ".wav";
            describeWav(project, audio, bytes, path, digest, sources, regions);
            bindings.add(object({{"documentPath", mixDocument}, {"resourceKind", "productionAsset"},
                                 {"resourceId", sources.getLast()["id"]}, {"assetId", id}}));
            assets.add(object({{"id", id}, {"path", path}, {"purpose", "audioSource"}, {"mediaType", "audio/wav"},
                               {"sizeBytes", static_cast<juce::int64>(bytes.getSize())},
                               {"sha256", digest}}));
            media.push_back(std::move(bytes));
        }
        const auto score = savedScore(project, true);
        const auto hash = juce::SHA256(score).toHexString();
        const auto core = parseObject(score);
        auto document = productionMix(core, mix, project.panAutomation);
        storeClipTiming(document, project);
        auto* profile = document.getDynamicObject();
        if (!sources.isEmpty() || core["extensions"][mixNamespace]["data"].hasProperty("assets")
            || core["extensions"][mixNamespace]["data"].hasProperty("audio")) {
            profile->setProperty("assets", sources);
            profile->setProperty("audioRegions", regions);
        }
        profile->setProperty("profile", "OSTF-Production-Session");
        const bool is51 = core["version"] == "5.1.0";
        const auto productionVersion = is51 ? "0.4.0" : "0.3.0";
        profile->setProperty("profileVersion", productionVersion);
        profile->setProperty("score", object({{"formatVersion", core["version"]}, {"sha256", hash}}));
        const auto production = jsonBytes(document);
        require(score.getSize() <= maxBytes && production.getSize() <= maxBytes, "OSTFP score or mixer document is too large");
        auto manifest = project.sourceManifest.isObject() ? project.sourceManifest.clone() : object({});
        auto* fields = manifest.getDynamicObject();
        fields->setProperty("packageFormat", "OSTFP");
        fields->setProperty("packageVersion", is51 || manifest["packageVersion"] == "1.1.0" ? "1.1.0" : "1.0.0");
        if (manifest["projectId"].toString().isEmpty())
            fields->setProperty("projectId", "studio-" + juce::Uuid().toString());
        fields->setProperty("title", project.title);
        constexpr char scorePath[] = "score/song.ostf";
        fields->setProperty("score", object({{"path", scorePath}, {"format", "OSTF"}, {"version", core["version"]}, {"sha256", hash}}));
        juce::Array<juce::var> documents;
        documents.add(object({{"id", "studio-mixer"}, {"role", "productionSession"}, {"path", mixDocument},
                              {"family", "OSTF-Production-Session"}, {"version", productionVersion},
                              {"sha256", juce::SHA256(production).toHexString()}}));
        fields->setProperty("documents", documents);
        fields->setProperty("assets", assets);
        fields->setProperty("bindings", bindings);
        const auto manifestBytes = jsonBytes(manifest);
        require(manifestBytes.getSize() <= maxBytes, "OSTFP manifest is too large");
        juce::ZipFile::Builder archive;
        archive.addEntry(std::make_unique<juce::MemoryInputStream>(mediaType, sizeof mediaType - 1, false), 0, "mimetype", {});
        archive.addEntry(std::make_unique<juce::MemoryInputStream>(manifestBytes, false), 9, "manifest.json", {});
        archive.addEntry(std::make_unique<juce::MemoryInputStream>(score, false), 9, scorePath, {});
        archive.addEntry(std::make_unique<juce::MemoryInputStream>(production, false), 9, mixDocument, {});
        for (std::size_t i = 0; i < media.size(); ++i)
            archive.addEntry(std::make_unique<juce::MemoryInputStream>(media[i], false), 0,
                             assets[static_cast<int>(i)]["path"].toString(), {});
        require(archive.writeToStream(output, nullptr), "Could not write OSTFP package");
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}

juce::Result writeOstfPreview(const OstfpPreview& project, const SessionMix& mix, juce::OutputStream& output) {
    return writeOstfPreview(project, mix, {}, output);
}

juce::Result writeOstfPreview(const OstfpPreview& project, const SessionMix& mix, const juce::File& destination,
                             juce::OutputStream& output) {
    try {
        juce::Array<juce::var> sources, regions;
        if (!project.audio.empty()) {
            require(destination != juce::File{}, "An OSTF file destination is needed to copy referenced audio");
            const auto folder = destination.getSiblingFile(destination.getFileNameWithoutExtension() + ".media");
            require(folder.createDirectory().wasOk(), "Could not create the OSTF media folder");
            for (const auto& audio : project.audio) {
                const auto bytes = audioBytes(audio);
                const auto digest = juce::SHA256(bytes).toHexString();
                const auto name = digest + ".wav";
                const auto copy = folder.getChildFile(name);
                if (!copy.existsAsFile()) {
                    juce::TemporaryFile temporary(copy);
                    require(temporary.getFile().replaceWithData(bytes.getData(), bytes.getSize())
                            && temporary.overwriteTargetFileWithTemporary(), "Could not copy a WAV to the OSTF media folder");
                } else {
                    juce::FileInputStream existing(copy);
                    require(existing.openedOk() && existing.getTotalLength() == static_cast<juce::int64>(bytes.getSize())
                            && juce::SHA256(existing) == juce::SHA256(bytes), "An existing OSTF media file has different content");
                }
                describeWav(project, audio, bytes, folder.getFileName() + "/" + name, digest, sources, regions);
            }
        }
        const auto bytes = scoreWithMix(project, mix, sources, regions);
        require(bytes.getSize() <= maxBytes && output.write(bytes.getData(), bytes.getSize()), "Could not write OSTF score");
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}
}
