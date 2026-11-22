#include "ui/OstfpPreview.h"
#include "StockEffects.h"
#include "core/MidiGeneratorPattern.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ostfstudio {
namespace {
constexpr char mediaType[] = "application/vnd.ostf-project+zip";
constexpr int maxBytes = 4 * 1024 * 1024;
constexpr int maxAudioBytes = 16 * 1024 * 1024;
constexpr int maxPackageBytes = 48 * 1024 * 1024;
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

std::unique_ptr<OstfpPreview> projectScore(const juce::var& score) {
    require(score["format"] == "OSTF" && score["version"] == "5.0.0", "Expected an OSTF 5.0.0 score");
    const auto timeline = score["timeline"];
    const auto ppq = integer(score["ppq"]);
    const auto duration = integer(timeline["duration"]);
    const auto meters = timeline["meterChanges"];
    const auto& meterEntries = array(meters);
    require(meterEntries.size() == 1 && integer(meterEntries[0]["at"]) == 0, "Preview requires one meter starting at tick zero");
    const auto numerator = integer(meterEntries[0]["numerator"]);
    const auto denominator = integer(meterEntries[0]["denominator"]);
    require(ppq > 0 && ppq <= std::numeric_limits<int>::max() && numerator > 0 && numerator <= std::numeric_limits<int>::max()
            && denominator > 0 && duration > 0, "Invalid OSTF meter or duration");
    require(ppq <= std::numeric_limits<juce::int64>::max() / 4 / numerator, "OSTF meter exceeds preview tick range");
    const auto ticksPerWholeBars = ppq * numerator * 4;
    require(ticksPerWholeBars % denominator == 0, "Preview requires an integral number of ticks per bar");
    const auto ticksPerBar = ticksPerWholeBars / denominator;
    require(ticksPerBar > 0 && duration % ticksPerBar == 0 && duration / ticksPerBar <= 256,
            "Preview requires a whole-bar score of at most 256 bars");

    auto out = std::make_unique<OstfpPreview>();
    out->ticksPerBar = ticksPerBar;
    out->ppq = ppq;
    out->title = score["song"]["title"].toString();
    require(out->title.isNotEmpty(), "Missing song title");
    out->bars.endBar = static_cast<int>(duration / ticksPerBar) + 1;
    auto bar = [&](juce::int64 tick) {
        require(tick >= 0 && tick <= duration && tick % ticksPerBar == 0, "Preview requires bar-aligned regions");
        return static_cast<int>(tick / ticksPerBar) + 1;
    };
    auto addLane = [&](std::vector<ArrangementLane>& lanes, juce::String title, std::vector<ArrangementRegion> regions,
                       TimelineLane kind = TimelineLane::none) {
        out->regions.push_back(std::move(regions));
        lanes.push_back({out->keepLabel(std::move(title)), out->regions.back(), kind});
    };
    auto intervals = [&](const juce::var& value, const char* labelField, bool countNotes = false, bool exact = false) {
        std::vector<ArrangementRegion> result;
        if (value.isVoid()) return result;
        for (const auto& event : array(value)) {
            const auto start = integer(event["start"]);
            const auto length = integer(event["duration"]);
            require(start >= 0 && start < duration && length > 0 && length <= duration - start, "Invalid OSTF region duration");
            const auto finish = start + length;
            const auto firstBar = exact ? static_cast<int>(start / ticksPerBar) + 1 : bar(start);
            const auto endBar = exact ? static_cast<int>((finish - 1) / ticksPerBar) + 2 : bar(finish);
            std::optional<TimelineExtent> position;
            if (exact) position = TimelineExtent{1.0 + static_cast<double>(start) / ticksPerBar, 1.0 + static_cast<double>(finish) / ticksPerBar};
            auto label = countNotes ? event["name"].toString() : juce::String{};
            if (label.isEmpty()) label = event[labelField].toString();
            const auto notes = countNotes ? event["notes"] : juce::var{};
            result.push_back({{firstBar, endBar}, out->keepLabel(label.isNotEmpty() ? label : event["id"].toString()),
                              notes.isArray() ? array(notes).size() : 0, position});
        }
        return result;
    };

    addLane(out->globalLanes, "SECTION", intervals(timeline["sections"], "name"), TimelineLane::section);
    const auto chords = score["harmony"]["chords"];
    const auto chordRegions = intervals(chords, "label", false, true);
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
                                    0, chordRegions[static_cast<std::size_t>(i)].exactBars});
        }
    }
    std::stable_sort(voicingNotes.begin(), voicingNotes.end(), [](const auto& a, const auto& b) { return a.startTick < b.startTick; });
    out->chordVoicings = static_cast<int>(voicings.size());

    std::vector<ArrangementRegion> keys;
    auto key = score["song"]["key"]["label"].toString();
    int keyStart = 1;
    if (const auto keyChanges = timeline["keyChanges"]; keyChanges.isArray()) {
        for (const auto& change : array(keyChanges)) {
            const auto tick = integer(change["at"]);
            if (tick <= 0 || tick >= duration || tick % ticksPerBar != 0) continue;
            const auto nextBar = bar(tick);
            if (nextBar <= keyStart) continue;
            if (key.isNotEmpty()) keys.push_back({{keyStart, nextBar}, out->keepLabel(key)});
            keyStart = nextBar;
            key = change["key"]["label"].toString();
            if (key.isEmpty()) key = change["label"].toString();
        }
    }
    if (key.isNotEmpty()) keys.push_back({{keyStart, out->bars.endBar}, out->keepLabel(key)});
    if (!keys.empty()) addLane(out->globalLanes, "KEY", std::move(keys), TimelineLane::key);

    std::vector<ArrangementRegion> scales;
    if (const auto timed = score["harmony"]["scales"]; timed.isArray()) {
        for (const auto& entry : array(timed)) {
            const auto start = integer(entry["start"]);
            const auto length = integer(entry["duration"]);
            if (start < 0 || start >= duration || length <= 0 || length > duration - start
                || start % ticksPerBar != 0 || length % ticksPerBar != 0) continue;
            auto label = entry["label"].toString();
            if (label.isEmpty()) label = entry["id"].toString();
            scales.push_back({{bar(start), bar(start + length)}, out->keepLabel(std::move(label))});
        }
    }
    if (scales.empty()) {
        const auto scale = score["song"]["scale"]["label"].toString();
        if (scale.isNotEmpty()) scales.push_back({out->bars, out->keepLabel(scale)});
    }
    if (!scales.empty()) addLane(out->globalLanes, "SCALE", std::move(scales), TimelineLane::scale);
    addLane(out->globalLanes, "METER", {{out->bars, out->keepLabel(juce::String(numerator) + "/" + juce::String(denominator))}}, TimelineLane::meter);

    const auto tempo = timeline["tempoChanges"];
    const auto& changes = array(tempo);
    if (changes.size() == 1 && integer(changes[0]["at"]) == 0)
        out->previewBpm = static_cast<double>(changes[0]["bpm"]);
    std::vector<ArrangementRegion> tempos;
    for (int i = 0; i < changes.size(); ++i) {
        const auto first = bar(integer(changes[i]["at"]));
        const auto last = i + 1 < changes.size() ? bar(integer(changes[i + 1]["at"])) : out->bars.endBar;
        require(first < last, "Tempo changes must be ordered within the score");
        tempos.push_back({{first, last}, out->keepLabel(changes[i]["bpm"].toString())});
    }
    addLane(out->globalLanes, "BPM", std::move(tempos), TimelineLane::tempo);

    const auto trackData = score["tracks"];
    const auto& tracks = array(trackData);
    require(static_cast<std::size_t>(tracks.size()) <= OstfpPreview::maxTracks, "Preview supports at most eight arrangement tracks");
    std::vector<std::pair<juce::String, int>> drumMappings;
    if (const auto slots = score["soundSlots"]; slots.isArray()) {
        for (const auto& slot : array(slots)) {
            const auto mapping = slot["midiMapping"];
            if (mapping["standard"] == "gm1" && mapping["note"].isInt()) {
                const auto pitch = static_cast<int>(mapping["note"]);
                if (pitch >= 0 && pitch <= 127) drumMappings.emplace_back(slot["id"].toString(), pitch);
            }
        }
    }
    for (const auto& track : tracks) {
        addLane(out->trackLanes, track["name"].toString(), intervals(track["clips"], "id", true, true));
        out->trackKinds.push_back(track["trackType"] == "drum" ? TrackKind::drum : TrackKind::pitched);
        const auto role = track["role"].toString();
        auto& group = out->mixerGroups.emplace_back(
                               role.equalsIgnoreCase("vocal") || role.equalsIgnoreCase("vocals") ? MixerTrackGroup::vocal
                             : role.equalsIgnoreCase("melody") || role.equalsIgnoreCase("lead") ? MixerTrackGroup::melody
                             : MixerTrackGroup::other);
        auto& notes = out->pitchedNotes.emplace_back();
        auto& drums = out->drumNotes.emplace_back();
        auto& programs = out->programChanges.emplace_back();
        const auto midiSetup = track["midi"];
        const auto sourceChannel = static_cast<int>(midiSetup["channel"]);
        out->sourceMidiChannels.push_back(midiSetup["mode"] == "singleChannel" && sourceChannel >= 1 && sourceChannel <= 16
                                          ? sourceChannel : (track["trackType"] == "drum" ? 10 : 1));
        auto& curves = out->performanceCurves.emplace_back();
        const auto clips = track["clips"];
        if (clips.isVoid()) continue;
        for (const auto& clip : array(clips)) {
            const auto clipStart = integer(clip["start"]);
            const auto clipLength = integer(clip["duration"]);
            const auto events = clip["notes"];
            if (events.isArray()) for (const auto& event : array(events)) {
                const auto slot = event["soundSlotId"].toString();
                const auto drum = std::find_if(drumMappings.begin(), drumMappings.end(), [&](const auto& mapping) { return mapping.first == slot; });
                const bool pitched = !event["pitch"].isVoid();
                if (!pitched && drum == drumMappings.end()) continue;
                const auto pitch = pitched ? integer(event["pitch"]) : drum->second;
                const auto local = integer(event["start"]);
                const auto length = integer(event["duration"]);
                require(pitch >= 0 && pitch <= 127 && local >= 0 && local < clipLength && length > 0
                        && length <= duration - clipStart - local, "Invalid OSTF pitched note");
                std::optional<int> velocity;
                if (!event["velocity"].isVoid()) {
                    const auto value = integer(event["velocity"]);
                    require(value >= 1 && value <= 127, "Invalid OSTF note velocity");
                    velocity = static_cast<int>(value);
                }
                auto& target = pitched ? notes : drums;
                target.push_back({clipStart + local, clipStart + local + length, static_cast<int>(pitch), velocity,
                                  event["enabled"].isVoid() || static_cast<bool>(event["enabled"])});
            }
            const auto midi = clip["midiEvents"];
            if (midi.isArray()) for (const auto& event : array(midi)) {
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
                    programs.push_back({clipStart + time, static_cast<int>(number), channel});
                    continue;
                }
                juce::String label;
                if (type == "controlChange") label = "CC " + event["controller"].toString();
                else if (type == "channelPressure") label = "Channel Aftertouch";
                else if (type == "polyPressure") label = "Poly Aftertouch / note " + event["pitch"].toString();
                else continue;
                if (!event["channel"].isVoid()) label += " / ch " + event["channel"].toString();
                const auto time = integer(event["start"]);
                const auto value = integer(event["value"]);
                if (time < -clipStart || time >= duration - clipStart || value < 0 || value > 127) continue;
                auto lane = std::find_if(curves.begin(), curves.end(), [&](const auto& c) { return !c.isCurve && c.label == label; });
                if (lane == curves.end()) lane = curves.insert(curves.end(), {label, {}});
                lane->points.push_back({clipStart + time, static_cast<double>(value) / 127.0});
            }
            const auto vocal = clip["vocalPerformances"];
            if (vocal.isArray()) for (const auto& voice : array(vocal)) {
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
    if (!voicingNotes.empty()) {
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
    return out;
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto value = std::make_unique<juce::DynamicObject>();
    for (const auto& [key, entry] : fields) value->setProperty(key, entry);
    return juce::var(value.release());
}

juce::MemoryBlock jsonBytes(const juce::var& value) {
    const auto text = juce::JSON::toString(value) + "\n";
    return {text.toRawUTF8(), text.getNumBytesAsUTF8()};
}

juce::MemoryBlock savedScore(const OstfpPreview& project) {
    require(!project.sourceScore.isEmpty(), "No OSTF score to save");
    if (project.addedClips.empty()) return project.sourceScore;
    auto score = parseObject(project.sourceScore);
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
        document->setProperty("revisionId", project.addedClips.back().id);
        document->setProperty("parents", parents);
        document->setProperty("modifiedBy", object({{"name", "OSTF Studio"}}));
    }
    return jsonBytes(score);
}

juce::String busId(int bus) { return bus < 0 ? "master" : "bus-" + juce::String(bus + 1); }
juce::String returnId(std::size_t index) { return "return-" + juce::String(static_cast<int>(index + 1)); }
int returnIndex(const juce::String& id) {
    for (std::size_t index = 0; index < maxMixerReturns; ++index)
        if (id == returnId(index)) return static_cast<int>(index);
    return -1;
}

juce::var mixChannel(const juce::String& id, const char* kind, double gain) {
    return object({{"id", id}, {"kind", kind}, {"audioChannels", 2}, {"gainDb", gain}, {"pan", 0.0},
                   {"mute", false}, {"solo", false}, {"soloSafe", false}, {"deviceIds", juce::Array<juce::var>{}}});
}

juce::var productionMix(const juce::var& score, const SessionMix& mix) {
    const auto scoreTracks = score["tracks"];
    const auto& tracks = array(scoreTracks);
    const auto sourceCount = static_cast<std::size_t>(tracks.size());
    require(mix.tracks.size() == sourceCount || mix.tracks.size() == sourceCount + 1, "Mixer does not match the score");
    juce::var voicingMix;
    juce::var voicingRoute;
    juce::Array<juce::var> channels, routes, devices;
    auto states = object({});
    auto attachEffects = [&](juce::var& channel, bool output, std::size_t index, const juce::StringArray& chain) {
        auto ids = array(channel["deviceIds"]);
        for (int slot = 0; slot < chain.size(); ++slot) {
            for (const auto& fx : stockEffects) {
                if (fx.chainId() != chain[slot]) continue;
                const auto id = SessionMix::effectSlot(output, index, slot);
                ids.add(id);
                devices.add(object({{"id", id}, {"role", "effect"}, {"enabled", true}, {"bypass", false},
                    {"onUnavailable", "reportAndPreserve"}, {"plugin", object({{"format", "vst3"}, {"vendor", "OSTF Studio"},
                        {"name", fx.name}, {"pluginId", fx.classId()}})}}));
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
    for (int bus = 0; bus < 2; ++bus) {
        auto channel = mixChannel(busId(bus), "bus", mix.routing.busDb[static_cast<std::size_t>(bus)]);
        attachEffects(channel, true, bus + 1, mix.outputEffects[bus + 1]);
        channel.getDynamicObject()->setProperty("mute", mix.routing.outputMuted[static_cast<std::size_t>(bus + 1)]);
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
            local.getDynamicObject()->setProperty("audioTrack", true);
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
    hostData.add(object({{"namespace", mixNamespace}, {"version", "1.0"},
                        {"data", object({{"tracks", routes}, {"cueDb", mix.routing.cueDb}, {"inserts", inserts}, {"outputEffects", outputEffects}, {"stockEffectStates", states}, {"chordVoicingMix", voicingMix}, {"chordVoicingRoute", voicingRoute}})}}));
    return object({{"channels", channels}, {"devices", devices}, {"hostExtensions", hostData}});
}

SessionMix readMix(const juce::var& score, const juce::var& production) {
    const auto host = production["hostExtensions"];
    const auto extensions = host.isVoid() ? juce::Array<juce::var>{} : array(host);
    require(extensions.isEmpty() || (extensions.size() == 1 && extensions[0]["namespace"] == mixNamespace
            && extensions[0]["version"] == "1.0"),
            "This production document is outside the Studio mixer preview");
    for (const auto* field : {"assets", "automation", "audioRegions", "sidechainConnections"})
        require(production[field].isVoid() || array(production[field]).isEmpty(), "Unsupported OSTFP production content");
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
    if (voicingMix.isObject()) channels.add(voicingMix);
    // Legacy Studio chains can also contain cached host IDs for third-party plug-ins.
    // Translate state keys after merging those chains, so mixed chains keep slot order.
    std::map<juce::String, juce::StringArray> effectDeviceIds;
    for (const auto& entry : channels) {
        const auto id = entry["id"].toString();
        const bool outputChannel = entry["kind"] != "track";
        const int track = entry == voicingMix ? static_cast<int>(voicingIndex) : trackIndex(entry["trackId"]);
        const int output = id == "master" ? 0 : id == "bus-1" ? 1 : id == "bus-2" ? 2
            : returnIndex(id) >= 0 ? 3 + returnIndex(id) : -1;
        const int channelIndex = outputChannel ? output : track;
        require(channelIndex >= 0, "Unsupported production channel");
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
        } else if (id == "bus-1" || id == "bus-2") {
            const auto bus = id == "bus-1" ? 0u : 1u;
            mix.routing.busDb[bus] = gain(entry["gainDb"]);
            mix.routing.outputMuted[1 + bus] = static_cast<bool>(entry["mute"]);
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
            require(output == "master" || output == "bus-1" || output == "bus-2", "Unsupported OSTFP track output");
            settings.outputBus = output == "master" ? -1 : (output == "bus-1" ? 0 : 1);
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
        mix.tracks[index].cue = static_cast<bool>(route["cue"]);
        mix.midiRoutes[index].channel = static_cast<int>(midiChannel);
        if (!route["destinationTrackId"].isVoid()) {
            const auto target = trackIndex(route["destinationTrackId"]);
            require(target >= 0 && target != i, "Invalid OSTFP MIDI destination");
            mix.midiRoutes[index].destination = static_cast<std::size_t>(target);
        }
    }
    auto restoreStates = [&](bool output, std::size_t channel, const juce::StringArray& chain) {
        const auto it = effectDeviceIds.find(SessionMix::effectSlot(output, channel, 0));
        const bool voicing = !output && channel == voicingIndex;
        if (it == effectDeviceIds.end() && !voicing) return;
        int device = 0;
        for (int slot = 0; slot < chain.size(); ++slot)
            for (const auto& fx : stockEffects) if (chain[slot] == fx.chainId()) {
                const auto key = SessionMix::effectSlot(output, channel, slot);
                const juce::Identifier deviceId(voicing ? key : it->second[device++]);
                const auto state = local["stockEffectStates"][deviceId];
                if (state.isString()) mix.stockEffectStates[key] = state.toString();
                break;
            }
    };
    for (std::size_t i = 0; i < mix.trackEffects.size(); ++i) restoreStates(false, i, mix.trackEffects[i]);
    for (std::size_t i = 0; i < mix.outputEffects.size(); ++i) restoreStates(true, i, mix.outputEffects[i]);
    return mix;
}

std::optional<SessionMix> readEmbeddedMix(const juce::var& score) {
    const auto settings = score["extensions"][mixNamespace];
    if (settings.isVoid()) return std::nullopt;
    require(settings["version"] == "1.0", "Unsupported OSTF Studio settings version");
    return readMix(score, settings["data"]);
}

juce::MemoryBlock scoreWithMix(const OstfpPreview& project, const SessionMix& mix,
                               const juce::Array<juce::var>& references = {}) {
    auto score = parseObject(savedScore(project));
    auto extensions = score["extensions"];
    if (!extensions.isObject()) extensions = object({});
    auto settings = productionMix(score, mix);
    if (!references.isEmpty()) settings.getDynamicObject()->setProperty("audio", references);
    extensions.getDynamicObject()->setProperty(mixNamespace, object({
        {"version", "1.0"}, {"status", "current"}, {"dependencies", object({{"scope", "document"}})},
        {"data", settings}}));
    score.getDynamicObject()->setProperty("extensions", extensions);
    return jsonBytes(score);
}

void readAudioReferences(const juce::var& score, OstfpPreview& project) {
    const auto references = score["extensions"][mixNamespace]["data"]["audio"];
    if (!references.isArray()) return;
    const auto& records = array(references);
    require(records.size() <= 8, "Too many referenced audio files");
    for (const auto& ref : records) {
        const auto track = integer(ref["track"]);
        const auto path = ref["path"].toString();
        const auto parts = juce::StringArray::fromTokens(path, "/", "");
        bool relative = !path.startsWithChar('/') && !path.containsAnyOf("\\:\r\n") && !path.contains("//");
        for (const auto& part : parts) relative = relative && part.isNotEmpty() && part != "." && part != "..";
        require(track >= 0 && static_cast<std::size_t>(track) < project.trackLanes.size()
                && relative && path.endsWithIgnoreCase(".wav"), "Unsupported Studio audio reference");
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
}

const char* OstfpPreview::keepLabel(juce::String label) {
    labels.push_back(std::move(label));
    return labels.back().toRawUTF8();
}

juce::Result OstfpPreview::editTracks(TrackKind kind, std::optional<std::size_t> remove,
                                     std::unique_ptr<OstfpPreview>& result) const {
    try {
        const auto before = savedScore(*this);
        auto score = parseObject(before);
        auto tracks = score["tracks"];
        auto& entries = *tracks.getArray();
        auto extensions = score["extensions"];
        if (auto* values = extensions.getDynamicObject()) values->removeProperty(mixNamespace);
        const auto revision = "studio-" + juce::Uuid().toString();
        if (remove) {
            require(*remove < static_cast<std::size_t>(entries.size()), "Invalid track");
            const auto removed = entries[static_cast<int>(*remove)];
            juce::StringArray ids;
            ids.add(removed["id"].toString());
            if (const auto clips = removed["clips"]; clips.isArray())
                for (const auto& clip : *clips.getArray()) ids.add(clip["id"].toString());
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
            require(static_cast<std::size_t>(entries.size()) < maxTracks, "Preview supports up to eight tracks");
            const auto name = kind == TrackKind::audio ? "Audio" : kind == TrackKind::drum ? "Drums" : "Pitched";
            auto track = object({{"id", revision + "-track"}, {"name", name + juce::String(" ") + juce::String(entries.size() + 1)},
                                 {"clips", juce::Array<juce::var>{}}});
            if (kind != TrackKind::audio) track.getDynamicObject()->setProperty("trackType", kind == TrackKind::drum ? "drum" : "melodic");
            entries.add(track);
            score.getDynamicObject()->setProperty("tracks", tracks);
        }
        if (auto* document = score["document"].getDynamicObject()) {
            juce::Array<juce::var> parents;
            parents.add(object({{"documentId", document->getProperty("id")},
                                {"revisionId", document->getProperty("revisionId")}, {"sha256", juce::SHA256(before).toHexString()}}));
            document->setProperty("parents", parents);
            document->setProperty("revisionId", revision);
            document->setProperty("modifiedBy", object({{"name", "OSTF Studio"}}));
        }
        auto next = projectScore(score);
        next->sourceScore = jsonBytes(score);
        next->trackKinds = trackKinds;
        if (remove) next->trackKinds.erase(next->trackKinds.begin() + static_cast<std::ptrdiff_t>(*remove));
        else next->trackKinds.insert(next->trackKinds.begin() + static_cast<std::ptrdiff_t>(sourceTrackCount()), kind);
        for (const auto& item : audio) {
            if (remove && item.track == *remove) continue;
            next->audio.push_back(item);
            if (remove && item.track > *remove) --next->audio.back().track;
        }
        result = std::move(next);
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}

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
    trackRegions.push_back({{bars.firstBar + static_cast<int>(first), bars.firstBar + static_cast<int>(last)},
                            keepLabel(name), static_cast<int>(notes.size())});
    trackLanes[track].regions = trackRegions;
    auto& trackNotes = pitchedNotes[track];
    trackNotes.insert(trackNotes.end(), notes.begin(), notes.end());
    std::stable_sort(trackNotes.begin(), trackNotes.end(), [](const auto& a, const auto& b) { return a.startTick < b.startTick; });
    addedClips.push_back({track, "studio-" + juce::Uuid().toString(), std::move(name), first * ticksPerBar,
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
        auto next = projectScore(score);
        next->savedMix = readEmbeddedMix(score);
        readAudioReferences(score, *next);
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
        require(zip.getNumEntries() >= 3 && zip.getNumEntries() <= 12
                && zip.getEntry(0)->filename == "mimetype" && zip.getEntry(1)->filename == "manifest.json",
                "Preview supports an OSTFP score and optional Studio mixer document");
        const auto mime = readMember(zip, 0);
        require(mime.getSize() == sizeof mediaType - 1 && std::memcmp(mime.getData(), mediaType, mime.getSize()) == 0,
                "Incorrect OSTFP media type");
        const auto manifest = parseObject(readMember(zip, 1));
        require(manifest["packageFormat"] == "OSTFP" && manifest["packageVersion"] == "1.0.0", "Unsupported OSTFP manifest version");
        const auto documentData = manifest["documents"];
        const auto& documents = array(documentData);
        const auto assetData = manifest["assets"];
        const auto& assets = array(assetData);
        require(documents.size() <= 1 && assets.size() <= 8 && zip.getNumEntries() == 3 + documents.size() + assets.size()
                && array(manifest["bindings"]).isEmpty()
                && (manifest["externalAssets"].isVoid() || array(manifest["externalAssets"]).isEmpty()),
                "Unsupported OSTFP documents or assets");
        const auto descriptor = manifest["score"];
        const auto path = descriptor["path"].toString();
        const auto scoreIndex = zip.getIndexOfFileName(path);
        require(descriptor["format"] == "OSTF" && descriptor["version"] == "5.0.0" && safeScorePath(path)
                && scoreIndex >= 2, "Invalid or unsupported OSTFP score entry");
        auto scoreBytes = readMember(zip, scoreIndex);
        const auto hash = juce::SHA256(scoreBytes).toHexString();
        require(hash == descriptor["sha256"].toString(), "OSTFP score SHA-256 mismatch");
        const auto score = parseObject(scoreBytes);
        auto next = projectScore(score);
        readAudioReferences(score, *next);
        if (!documents.isEmpty()) {
            const auto document = documents[0];
            const auto documentPath = document["path"].toString();
            const auto index = zip.getIndexOfFileName(documentPath);
            require(document["role"] == "productionSession" && document["family"] == "OSTF-Production-Session"
                    && document["version"] == "0.3.0" && safeScorePath(documentPath) && index >= 2 && index != scoreIndex,
                    "Unsupported OSTFP companion document");
            const auto bytes = readMember(zip, index);
            require(juce::SHA256(bytes).toHexString() == document["sha256"].toString(), "OSTFP production document SHA-256 mismatch");
            const auto production = parseObject(bytes);
            require(production["profile"] == "OSTF-Production-Session" && production["profileVersion"] == "0.3.0"
                    && production["score"]["formatVersion"] == "5.0.0" && production["score"]["sha256"] == hash,
                    "OSTFP production profile or score hash mismatch");
            next->savedMix = readMix(score, production);
        } else {
            next->savedMix = readEmbeddedMix(score);
        }
        require(assets.size() == static_cast<int>(next->audio.size()), "OSTFP audio inventory mismatch");
        juce::StringArray loadedAssets;
        std::size_t loadedBytes = 0;
        for (auto& audio : next->audio) {
            require(safeScorePath(audio.path) && loadedAssets.addIfNotAlreadyThere(audio.path), "Invalid OSTFP audio path");
            const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) {
                return item["path"] == audio.path && item["purpose"] == "audioSource";
            });
            const auto index = zip.getIndexOfFileName(audio.path);
            require(asset != assets.end() && index >= 2 && index != scoreIndex, "Missing OSTFP audio asset");
            audio.embedded = readMember(zip, index, maxAudioBytes);
            loadedBytes += audio.embedded.getSize();
            require(loadedBytes <= maxPackageBytes - 2 * maxBytes
                    && integer((*asset)["sizeBytes"]) == static_cast<juce::int64>(audio.embedded.getSize())
                    && juce::SHA256(audio.embedded).toHexString() == (*asset)["sha256"].toString(), "OSTFP audio asset hash or size mismatch");
        }
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
        const auto bytes = savedScore(project);
        require(bytes.getSize() <= maxBytes && output.write(bytes.getData(), bytes.getSize()), "Could not write OSTF score");
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}

juce::Result writeOstfpPreview(const OstfpPreview& project, const SessionMix& mix, juce::OutputStream& output) {
    try {
        juce::Array<juce::var> references, assets;
        std::vector<juce::MemoryBlock> media;
        std::size_t total = 0;
        for (const auto& audio : project.audio) {
            media.push_back(audioBytes(audio));
            const auto& bytes = media.back();
            total += bytes.getSize();
            require(total <= maxPackageBytes - 3 * maxBytes && media.size() <= 8, "OSTFP audio exceeds preview package capacity");
            const auto id = "audio-" + juce::String(static_cast<int>(media.size()));
            const auto path = "assets/" + id + ".wav";
            references.add(object({{"track", static_cast<int>(audio.track)}, {"path", path}}));
            assets.add(object({{"id", id}, {"path", path}, {"purpose", "audioSource"}, {"mediaType", "audio/wav"},
                               {"sizeBytes", static_cast<juce::int64>(bytes.getSize())},
                               {"sha256", juce::SHA256(bytes).toHexString()}}));
        }
        const auto score = references.isEmpty() ? savedScore(project) : scoreWithMix(project, mix, references);
        const auto hash = juce::SHA256(score).toHexString();
        auto document = productionMix(parseObject(score), mix);
        auto* profile = document.getDynamicObject();
        profile->setProperty("profile", "OSTF-Production-Session");
        profile->setProperty("profileVersion", "0.3.0");
        profile->setProperty("score", object({{"formatVersion", "5.0.0"}, {"sha256", hash}}));
        const auto production = jsonBytes(document);
        require(score.getSize() <= maxBytes && production.getSize() <= maxBytes, "OSTFP score or mixer document is too large");
        auto manifest = project.sourceManifest.isObject() ? project.sourceManifest.clone() : object({});
        auto* fields = manifest.getDynamicObject();
        fields->setProperty("packageFormat", "OSTFP");
        fields->setProperty("packageVersion", "1.0.0");
        if (manifest["projectId"].toString().isEmpty())
            fields->setProperty("projectId", "studio-" + juce::Uuid().toString());
        fields->setProperty("title", project.title);
        constexpr char scorePath[] = "score/song.ostf";
        fields->setProperty("score", object({{"path", scorePath}, {"format", "OSTF"}, {"version", "5.0.0"}, {"sha256", hash}}));
        juce::Array<juce::var> documents;
        documents.add(object({{"id", "studio-mixer"}, {"role", "productionSession"}, {"path", mixDocument},
                              {"family", "OSTF-Production-Session"}, {"version", "0.3.0"},
                              {"sha256", juce::SHA256(production).toHexString()}}));
        fields->setProperty("documents", documents);
        fields->setProperty("assets", assets);
        fields->setProperty("bindings", juce::Array<juce::var>{});
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
        juce::Array<juce::var> references;
        if (!project.audio.empty()) {
            require(destination != juce::File{}, "An OSTF file destination is needed to copy referenced audio");
            const auto folder = destination.getSiblingFile(destination.getFileNameWithoutExtension() + ".media");
            require(folder.createDirectory().wasOk(), "Could not create the OSTF media folder");
            for (const auto& audio : project.audio) {
                const auto bytes = audioBytes(audio);
                const auto name = juce::SHA256(bytes).toHexString() + ".wav";
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
                references.add(object({{"track", static_cast<int>(audio.track)}, {"path", folder.getFileName() + "/" + name}}));
            }
        }
        const auto bytes = scoreWithMix(project, mix, references);
        require(bytes.getSize() <= maxBytes && output.write(bytes.getData(), bytes.getSize()), "Could not write OSTF score");
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}
}
