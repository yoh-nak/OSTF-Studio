#include "ui/MediaImport.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <set>

namespace ostfstudio {
namespace {
using Tick = juce::int64;
using List = juce::Array<juce::var>;
juce::var obj(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields) {
    auto o = std::make_unique<juce::DynamicObject>();
    for (const auto& [key, v] : fields)
        o->setProperty(key, v);
    return juce::var(o.release());
}
struct Part {
    juce::String name;
    int channel = 1;
    List notes;
};
struct ImportSong {
    int ppq = 480, beats = 4, beatType = 4, count = 0;
    Tick length = 1;
    std::map<Tick, double> tempos{{0, 120.0}};
    std::set<int> drums;
    std::vector<Part> parts;
};
juce::Result fail(const char* reason) { return juce::Result::fail(juce::translate(reason)); }

// YIN's first dip in the normalised difference function estimates one fundamental.
// Limit the search to the tested melodic range and reject unvoiced/quiet frames.
int fundamental(const float* samples) {
    constexpr int window = 256, maxLag = 115;
    float power = 0;
    for (int i = 0; i < window; ++i)
        power += samples[i] * samples[i];
    if (power / window < 0.00006f)
        return -1;
    std::array<float, maxLag + 1> difference{};
    float sum = 0;
    for (int lag = 1; lag <= maxLag; ++lag) {
        float distance = 0;
        for (int i = 0; i < window; ++i) {
            const auto delta = samples[i] - samples[i + lag];
            distance += delta * delta;
        }
        sum += distance;
        difference[lag] = sum > 0 ? distance * lag / sum : 1.0f;
        if (lag > 8 && difference[lag - 1] < 0.13f && difference[lag] >= difference[lag - 1]) {
            const float a = difference[lag - 2], b = difference[lag - 1], c = difference[lag];
            const float curve = a - 2 * b + c;
            const double refined = lag - 1 + (curve > 0.000001f ? 0.5f * (a - c) / curve : 0.f);
            return juce::roundToInt(69.0 + 12.0 * std::log2(8000.0 / (refined * 440.0)));
        }
    }
    return -1;
}
juce::Result transcribe(juce::AudioFormatReader& reader, ImportSong& song) {
    if (reader.lengthInSamples / reader.sampleRate > 120.0)
        return fail("Use up to two minutes of isolated melody for transcription.");
    // Block-average to 8 kHz. Keep one short mono analysis buffer regardless of source rate.
    const auto size = static_cast<std::size_t>(std::ceil(reader.lengthInSamples * 8000.0 / reader.sampleRate));
    std::vector<float> mono(size, 0.f);
    juce::AudioBuffer<float> block(static_cast<int>(reader.numChannels), 4096);
    for (Tick pos = 0; pos < reader.lengthInSamples;) {
        const auto count = static_cast<int>(std::min<Tick>(4096, reader.lengthInSamples - pos));
        if (!reader.read(&block, 0, count, pos, true, true))
            return fail("Could not read WAV samples.");
        const int channel =
            reader.numChannels == 2 && block.getRMSLevel(1, 0, count) > block.getRMSLevel(0, 0, count) ? 1 : 0;
        for (int i = 0; i < count; ++i) {
            const auto j = static_cast<std::size_t>((pos + i) * 8000.0 / reader.sampleRate);
            mono[j] += block.getSample(channel, i) * static_cast<float>(8000.0 / reader.sampleRate);
        }
        pos += count;
    }
    Part melody;
    melody.name = TRANS("Transcribed melody");
    int active = -1;
    std::size_t start = 0;
    auto change = [&](int pitch, std::size_t at) {
        if (pitch == active)
            return;
        if (active >= 0 && at >= start + 480) {
            const Tick first = static_cast<Tick>(std::llround(start * 2.0 * song.ppq / 8000));
            const Tick end = static_cast<Tick>(std::llround(at * 2.0 * song.ppq / 8000));
            melody.notes.add(obj({{"id", "draft-" + juce::String(++song.count)},
                                  {"start", first},
                                  {"duration", end - first},
                                  {"pitch", active}}));
        }
        active = pitch;
        start = at;
    };
    for (std::size_t i = 0; i + 371 <= mono.size(); i += 80)
        change(fundamental(mono.data() + i), i + 128);
    change(-1, mono.size());
    if (melody.notes.isEmpty())
        return fail("No stable melody detected. Choose a single-note recording or use WAV reference.");
    song.parts.push_back(std::move(melody));
    return juce::Result::ok();
}

juce::Result readMidi(const juce::File& source, ImportSong& song) {
    juce::FileInputStream input(source);
    juce::MidiFile file;
    int format = -1;
    if (!input.openedOk() || !file.readFrom(input, true, &format) || format > 1 || file.getTimeFormat() <= 0)
        return fail("Choose a PPQ-based MIDI format 0 or 1 file.");
    song.ppq = file.getTimeFormat();
    song.length = std::max<Tick>(1, static_cast<Tick>(file.getLastTimestamp()));
    std::map<Tick, std::pair<int, int>> meters;
    for (int tr = 0; tr < file.getNumTracks(); ++tr) {
        const auto* seq = file.getTrack(tr);
        juce::String name;
        std::map<int, Part> channels;
        for (auto* event : *seq) {
            const auto& msg = event->message;
            const auto at = static_cast<Tick>(msg.getTimeStamp());
            if (msg.isTrackNameEvent())
                name = msg.getTextFromTextMetaEvent();
            if (msg.isTimeSignatureMetaEvent()) {
                int n = 0, d = 0;
                msg.getTimeSignatureInfo(n, d);
                meters[at] = {n, d};
            }
            if (msg.isTempoMetaEvent()) {
                const double bpm = 60.0 / msg.getTempoSecondsPerQuarterNote();
                if (!std::isfinite(bpm) || bpm <= 0 || bpm > 1000)
                    return fail("Unsupported MIDI tempo.");
                song.tempos[at] = bpm;
            }
            if (!msg.isNoteOn())
                continue;
            if (!event->noteOffObject)
                return fail("MIDI note has no matching note-off.");
            const auto end = static_cast<Tick>(event->noteOffObject->message.getTimeStamp());
            if (end <= at)
                return fail("MIDI contains a zero-length note.");
            if (++song.count > 60000)
                return fail("This import exceeds the note limit.");
            const int ch = msg.getChannel(), p = msg.getNoteNumber();
            auto note = obj({{"id", "n-" + juce::String(song.count)},
                             {"start", at},
                             {"duration", end - at},
                             {"velocity", msg.getVelocity()}});
            if (ch == 10) {
                note.getDynamicObject()->setProperty("soundSlotId", "gm-" + juce::String(p));
                song.drums.insert(p);
            } else
                note.getDynamicObject()->setProperty("pitch", p);
            auto& part = channels[ch];
            part.channel = ch;
            part.notes.add(note);
        }
        for (auto& [ch, p] : channels) {
            p.name = name.isNotEmpty() ? name : "MIDI " + juce::String(tr + 1);
            if (channels.size() > 1 || name.isEmpty())
                p.name += " / Ch " + juce::String(ch);
            song.parts.push_back(std::move(p));
        }
    }
    if (song.parts.empty())
        return fail("No MIDI notes found.");
    if (song.parts.size() > OstfpPreview::maxTracks)
        return fail("Studio can open up to fifteen source tracks.");
    if (meters.count(0))
        std::tie(song.beats, song.beatType) = meters.at(0);
    for (const auto& [tick, meter] : meters)
        if (meter != std::pair{song.beats, song.beatType})
            return fail("Changing meter is outside the current Studio import range.");
    return juce::Result::ok();
}
} // namespace

juce::Result prepareMediaImport(const juce::File& source, std::unique_ptr<OstfpPreview>& project, juce::String& summary,
                                bool transcribeMelody) {
    const bool audio = source.hasFileExtension("wav;wave");
    if (!audio && !source.hasFileExtension("mid;midi"))
        return fail("Choose a WAV or MIDI file.");
    if (!source.existsAsFile() || source.getSize() > 16 * 1024 * 1024)
        return fail("Choose a source file of up to 16 MiB.");
    ImportSong song;
    juce::String detail;
    if (audio) {
        juce::WavAudioFormat wav;
        auto stream = source.createInputStream();
        if (!stream)
            return fail("Could not read WAV samples.");
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
        if (!reader || !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0 ||
            reader->numChannels < 1 || reader->numChannels > 2 ||
            reader->lengthInSamples <= 0 || reader->lengthInSamples > 8 * 1024 * 1024)
            return fail("Unsupported WAV for Studio playback.");
        const double seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        song.length = static_cast<Tick>(std::ceil(seconds * 2 * song.ppq));
        song.parts.push_back({source.getFileNameWithoutExtension(), 1, {}});
        detail = juce::String(seconds, 2) + " s / " + juce::String(reader->sampleRate, 0) + " Hz / " +
                 juce::String(reader->numChannels) + " ch\n" +
                 TRANS("WAV reference at bar 1. The guide grid is 120 BPM, 4/4.");
        if (transcribeMelody) {
            if (reader->sampleRate < 8000)
                return fail("Melody transcription requires at least 8 kHz.");
            auto r = transcribe(*reader, song);
            if (r.failed())
                return r;
            detail +=
                "\n" + juce::String(song.count) + " " + TRANS("draft notes. Review the melody in the piano roll.");
        }
    } else {
        auto r = readMidi(source, song);
        if (r.failed())
            return r;
        detail = juce::String(static_cast<int>(song.parts.size())) + " " + TRANS("tracks") + " / " +
                 juce::String(song.count) + " " + TRANS("notes") + "\n" +
                 TRANS("Imports note timing, velocity, channels, tempo and meter. Controller data is omitted.");
    }
    if (song.beats < 1 || song.beatType < 1 || (static_cast<Tick>(song.ppq) * 4 * song.beats) % song.beatType != 0)
        return fail("This meter cannot be represented on the current grid.");
    const Tick bar = static_cast<Tick>(song.ppq) * 4 * song.beats / song.beatType;
    const Tick duration = ((song.length + bar - 1) / bar) * bar;
    if (duration > 256 * bar)
        return fail("Studio can open up to 256 bars.");
    List tracks, slots, tempos;
    for (auto& p : song.parts) {
        auto id = "track-" + juce::String(tracks.size() + 1);
        List clips;
        if (!p.notes.isEmpty())
            clips.add(obj({{"id", id + "-clip"}, {"start", 0}, {"duration", duration}, {"notes", p.notes}}));
        tracks.add(obj({{"id", id},
                        {"name", p.name},
                        {"trackType", p.channel == 10 ? "drum" : "melodic"},
                        {"midi", obj({{"mode", "singleChannel"}, {"channel", p.channel}})},
                        {"clips", clips}}));
    }
    for (int pitch : song.drums)
        slots.add(obj({{"id", "gm-" + juce::String(pitch)},
                       {"name", "GM " + juce::String(pitch)},
                       {"midiMapping", obj({{"standard", "gm1"}, {"note", pitch}})}}));
    for (const auto& [at, bpm] : song.tempos)
        if (at < duration)
            tempos.add(obj({{"at", at}, {"bpm", bpm}}));
    auto score =
        obj({{"format", "OSTF"},
             {"version", "5.0.0"},
             {"ppq", song.ppq},
             {"song", obj({{"title", source.getFileNameWithoutExtension()}})},
             {"timeline",
              obj({{"duration", duration},
                   {"sections", List{obj({{"id", "section-1"},
                                          {"name", source.getFileNameWithoutExtension()},
                                          {"start", 0},
                                          {"duration", duration}})}},
                   {"meterChanges", List{obj({{"at", 0}, {"numerator", song.beats}, {"denominator", song.beatType}})}},
                   {"tempoChanges", tempos}})},
             {"harmony", obj({{"chords", List{}}, {"scales", List{}}})},
             {"tracks", tracks},
             {"soundSlots", slots},
             {"lyrics", obj({{"documents", List{}}, {"phrases", List{}}, {"units", List{}}, {"alignments", List{}}})},
             {"resources", List{}}});
    const auto json = juce::JSON::toString(score, true);
    juce::MemoryInputStream input(json.toRawUTF8(), json.getNumBytesAsUTF8(), false);
    std::unique_ptr<OstfpPreview> next;
    auto r = readOstfPreview(input, next);
    if (r.failed())
        return r;
    if (audio) {
        next->trackKinds[0] = TrackKind::audio;
        next->audio.push_back({0, source.getFileName(), source, {}, 0, song.length});
    }
    project = std::move(next);
    summary = detail;
    return juce::Result::ok();
}
juce::Result saveMediaImport(const OstfpPreview& p, const juce::File& file) {
    juce::TemporaryFile temp(file);
    {
        juce::FileOutputStream out(temp.getFile());
        if (!out.openedOk())
            return out.getStatus();
        SessionMix mix;
        mix.tracks.resize(p.sourceTrackCount());
        for (std::size_t i = 0; i < p.sourceTrackCount(); ++i)
            mix.audioTracks.push_back(p.trackKinds[i] == TrackKind::audio);
        auto result = p.audio.empty() ? writeOstfPreview(p, out) : writeOstfPreview(p, mix, file, out);
        out.flush();
        if (result.failed())
            return result;
        if (out.getStatus().failed())
            return out.getStatus();
    }
    return temp.overwriteTargetFileWithTemporary() ? juce::Result::ok() : fail("Could not save file.");
}
} // namespace ostfstudio
