#include "ui/SongMidi.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

namespace ostfstudio {
namespace {
using Message = juce::MidiMessage;
using Tick = juce::int64;
struct Event { Tick tick, order; Message message; };
using Events = std::vector<Event>;

const juce::Array<juce::var>& entries(const juce::var& value) {
    static const juce::Array<juce::var> empty;
    return value.isArray() ? *value.getArray() : empty;
}

int midiValue(const juce::var& value, int maximum = 127) {
    if ((!value.isInt() && !value.isInt64()) || static_cast<Tick>(value) < 0 || static_cast<Tick>(value) > maximum)
        throw std::runtime_error("Invalid MIDI event value");
    return static_cast<int>(value);
}

int channelFor(const juce::var& event, int fallback) {
    const int ch = event.hasProperty("channel") ? midiValue(event["channel"], 16) : fallback;
    if (ch < 1 || ch > 16) throw std::runtime_error("Invalid MIDI channel");
    return ch;
}

Message eventMessage(const juce::var& event, int ch) {
    const auto type = event["type"].toString();
    if (type == "programChange") return Message::programChange(ch, midiValue(event["program"]));
    if (type == "pitchBend") return Message::pitchWheel(ch, midiValue(event["value14"], 16383));
    const auto value = midiValue(event["value"]);
    if (type == "controlChange" || type == "channelMode")
        return Message::controllerEvent(ch, midiValue(event["controller"]), value);
    if (type == "channelPressure") return Message::channelPressureChange(ch, value);
    if (type == "polyPressure") return Message::aftertouchChange(ch, midiValue(event["pitch"]), value);
    throw std::runtime_error("Unsupported OSTF MIDI event");
}

void addTrack(juce::MidiFile& file, Events& events, Tick end) {
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return std::tie(a.tick, a.order) < std::tie(b.tick, b.order);
    });
    juce::MidiMessageSequence sequence;
    for (const auto& e : events) {
        if (e.tick < 0 || e.tick > end) throw std::runtime_error("MIDI event outside the song");
        sequence.addEvent(e.message, static_cast<double>(e.tick));
    }
    sequence.addEvent(Message::endOfTrack(), static_cast<double>(end));
    file.addTrack(sequence);
}
}

juce::Result writeSongMidi(const OstfpPreview& project, SmfFormat format, const std::vector<int>& channels,
                           juce::OutputStream& output) {
    juce::MemoryOutputStream snapshot;
    if (auto saved = writeOstfPreview(project, snapshot); saved.failed()) return saved;
    const auto score = juce::JSON::parse(snapshot.toUTF8());
    const auto timeline = score["timeline"];
    Tick end = project.durationTicks;
    for (const auto& clip : project.addedClips) end = std::max(end, clip.start + clip.length);
    if (project.ppq < 1 || project.ppq > 32767 || end < 1 || end > 0x0fffffff)
        return juce::Result::fail("Song timing exceeds the SMF export range");
    try {
        juce::MidiFile file;
        file.setTicksPerQuarterNote(static_cast<int>(project.ppq));
        Events conductor;
        constexpr Tick meta = std::numeric_limits<Tick>::min();
        conductor.push_back({0, meta, Message::textMetaEvent(3, project.title)});
        for (const auto& meter : entries(timeline["meterChanges"])) {
            const int n = midiValue(meter["numerator"], 255);
            const Tick d = meter["denominator"];
            if (!n || d < 1 || d > 0x40000000 || (d & (d - 1)) != 0)
                throw std::runtime_error("Meter cannot be represented in SMF");
            conductor.push_back({meter["at"], meta, Message::timeSignatureMetaEvent(n, static_cast<int>(d))});
        }
        for (const auto& tempo : entries(timeline["tempoChanges"])) {
            const auto micros = std::round(60000000.0 / static_cast<double>(tempo["bpm"]));
            if (!std::isfinite(micros) || micros < 1 || micros > 0xffffff)
                throw std::runtime_error("Tempo cannot be represented in SMF");
            conductor.push_back({tempo["at"], meta, Message::tempoMetaEvent(static_cast<int>(micros))});
        }
        const bool separate = format == SmfFormat::multitrack;
        if (separate) addTrack(file, conductor, end);
        std::map<juce::String, int> percussion;
        for (const auto& slot : entries(score["soundSlots"])) {
            const auto mapping = slot["midiMapping"];
            if (mapping["standard"] == "gm1") percussion.emplace(slot["id"].toString(), midiValue(mapping["note"]));
        }
        const auto tracks = score["tracks"];
        for (int i = 0; i < tracks.size(); ++i) {
            const auto track = tracks[i];
            const int ch = static_cast<size_t>(i) < channels.size() ? channels[i] : project.sourceMidiChannels.at(i);
            Events part;
            for (const auto& clip : entries(track["clips"])) {
                if (!static_cast<bool>(clip.getProperty("enabled", true))) continue;
                const Tick start = clip["start"];
                for (const auto& e : entries(clip["midiEvents"])) {
                    const Tick offset = e["start"];
                    if (offset < -start || offset > end - start) throw std::runtime_error("MIDI event outside the song");
                    part.push_back({start + offset, e.getProperty("order", 1), eventMessage(e, channelFor(e, ch))});
                }
                for (const auto& note : entries(clip["notes"])) {
                    if (!static_cast<bool>(note.getProperty("enabled", true))) continue;
                    const auto drum = percussion.find(note["soundSlotId"].toString());
                    if (!note.hasProperty("pitch") && drum == percussion.end()) continue;
                    const int pitch = note.hasProperty("pitch") ? midiValue(note["pitch"]) : drum->second;
                    const int channel = channelFor(note, ch);
                    const Tick tick = start + static_cast<Tick>(note["start"]);
                    part.push_back({tick, 2, Message::noteOn(channel, pitch, static_cast<juce::uint8>(midiValue(note.getProperty("velocity", 92))))});
                    part.push_back({tick + static_cast<Tick>(note["duration"]), 0, Message::noteOff(channel, pitch,
                                    static_cast<juce::uint8>(midiValue(note.getProperty("releaseVelocity", 0))))});
                }
            }
            if (separate) {
                if (project.trackKinds.at(i) == TrackKind::audio && part.empty()) continue;
                part.push_back({0, meta, Message::textMetaEvent(3, track["name"].toString())});
                addTrack(file, part, end);
            } else conductor.insert(conductor.end(), part.begin(), part.end());
        }
        if (!separate) addTrack(file, conductor, end);
        if (!file.writeTo(output, separate ? 1 : 0)) return juce::Result::fail("Could not write MIDI file");
        return juce::Result::ok();
    } catch (const std::exception& error) {
        return juce::Result::fail(error.what());
    }
}
}
