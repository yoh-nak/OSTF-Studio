#include "ui/MediaImport.h"
#include "ui/LocalStemAnalysis.h"
#include "ui/AudioScoreNotes.h"
#include "ui/GlobalTimelineEdit.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <numeric>
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
    juce::String role;
};
struct ImportSong {
    int ppq = 480, beats = 4, beatType = 4, count = 0;
    Tick length = 1;
    std::map<Tick, double> tempos{{0, 120.0}};
    std::map<Tick, std::pair<int, int>> meters{{0, {4, 4}}};
    std::set<int> drums;
    std::vector<Part> parts;
};
juce::Result fail(const char* reason) { return juce::Result::fail(juce::translate(reason)); }

// Compare pitch-class energy in short Hann-windowed blocks with major/minor triads.
juce::Result detectTriads(juce::AudioFormatReader& reader, int ppq, List& results) {
    const double rate = reader.sampleRate;
    if (rate < 8000 || rate > 384000)
        return fail("Chord analysis supports 8 to 384 kHz.");
    const int hop = juce::roundToInt(rate * 0.5);
    constexpr int frames = 2048;
    const int stride = std::max(1, static_cast<int>(rate / 8000));
    const double sampleRate = rate / stride;
    std::array<double, 48> coeff{};
    for (int i = 0; i < static_cast<int>(coeff.size()); ++i)
        coeff[i] =
            2 * std::cos(juce::MathConstants<double>::twoPi * 440 * std::pow(2., (i + 36 - 69) / 12.) / sampleRate);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader.numChannels), hop);
    static constexpr const char* names[]{"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    for (Tick start = 0; start < reader.lengthInSamples; start += hop) {
        if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob(); job && job->shouldExit()) return fail("Analysis cancelled or timed out.");
        const auto count = static_cast<int>(std::min<Tick>(reader.lengthInSamples - start, hop));
        const int n = std::min(frames, count / stride);
        if (n < 512)
            continue;
        const int offset = (count - n * stride) / 2;
        if (!reader.read(&buffer, 0, count, start, true, true))
            return fail("Could not decode the audio file.");
        std::array<double, 12> chroma{};
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            std::array<double, frames> signal{};
            for (int j = 0; j < n; ++j) {
                const auto* samples = buffer.getReadPointer(ch, offset + j * stride);
                for (int k = 0; k < stride; ++k)
                    signal[j] += samples[k];
                signal[j] *= (0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * j / (n - 1))) / stride;
            }
            for (int p = 0; p < static_cast<int>(coeff.size()); ++p) {
                double a = 0, b = 0;
                for (int j = 0; j < n; ++j) {
                    const double next = signal[j] + coeff[p] * a - b;
                    b = a;
                    a = next;
                }
                chroma[p % 12] += std::max(0., a * a + b * b - coeff[p] * a * b);
            }
        }
        const double total = std::accumulate(chroma.begin(), chroma.end(), 0.);
        int root = -1, third = 0;
        double best = 0, runner = 0;
        for (int pc = 0; pc < 12; ++pc)
            for (int t : {3, 4}) {
                double a = chroma[pc], b = chroma[(pc + t) % 12], c = chroma[(pc + 7) % 12];
                if (std::min({a, b, c}) < total * 0.045)
                    continue;
                const double match = a + b + c;
                if (match > best) {
                    runner = best;
                    best = match;
                    root = pc;
                    third = t;
                } else
                    runner = std::max(runner, match);
            }
        if (root < 0 || total < n * n * 0.000001 || best < total * 0.65 || best - runner < total * 0.045)
            continue;
        const Tick at = std::llround(start * 2. * ppq / rate), end = std::llround((start + count) * 2. * ppq / rate);
        auto name = juce::String(names[root]) + (third == 3 ? "m" : "");
        if (!results.isEmpty()) {
            auto& last = results.getReference(results.size() - 1);
            const Tick previous = last["start"];
            if (last["label"].toString() == name && previous + static_cast<Tick>(last["duration"]) == at) {
                last.getDynamicObject()->setProperty("duration", end - previous);
                continue;
            }
        }
        results.add(obj({{"id", "chord-" + juce::String(results.size() + 1)},
                         {"label", name},
                         {"start", at},
                         {"duration", end - at},
                         {"root", parseSpelling(names[root])},
                         {"intervals", List{0, third, 7}}}));
    }
    return juce::Result::ok();
}

// YIN's first dip in the normalised difference function estimates one fundamental.
// Limit the search to the tested melodic range and reject unvoiced/quiet frames.
int fundamental(const float *samples, int rate) {
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
            return juce::roundToInt(69.0 + 12.0 * std::log2(rate / (refined * 440.0)));
        }
    }
    return -1;
}
juce::Result transcribe(juce::AudioFormatReader &reader, ImportSong &song, bool boundedSolo = true, int rate = 8000) {
    if (boundedSolo && reader.lengthInSamples / reader.sampleRate > 120.0)
        return fail("Use up to two minutes of isolated melody for transcription.");
    // Block-average at the voice or bass analysis rate. Keep one short mono analysis buffer regardless of source rate.
    const auto size = static_cast<std::size_t>(std::ceil(reader.lengthInSamples * double(rate) / reader.sampleRate));
    std::vector<float> mono(size, 0.f);
    juce::AudioBuffer<float> block(static_cast<int>(reader.numChannels), 4096);
    for (Tick pos = 0; pos < reader.lengthInSamples;) {
        const auto count = static_cast<int>(std::min<Tick>(4096, reader.lengthInSamples - pos));
        if (!reader.read(&block, 0, count, pos, true, true))
            return fail("Could not read WAV samples.");
        const int channel =
            reader.numChannels == 2 && block.getRMSLevel(1, 0, count) > block.getRMSLevel(0, 0, count) ? 1 : 0;
        for (int i = 0; i < count; ++i) {
            const auto j = static_cast<std::size_t>((pos + i) * double(rate) / reader.sampleRate);
            mono[j] += block.getSample(channel, i) * static_cast<float>(double(rate) / reader.sampleRate);
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
        if (active >= 0 && at >= start + rate * .06) {
            const Tick first = static_cast<Tick>(std::llround(start * 2.0 * song.ppq / rate));
            const Tick end = static_cast<Tick>(std::llround(at * 2.0 * song.ppq / rate));
            melody.notes.add(obj({{"id", "draft-" + juce::String(++song.count)},
                                  {"start", first},
                                  {"duration", end - first},
                                  {"pitch", active}}));
        }
        active = pitch;
        start = at;
    };
    for (std::size_t i = 0; i + 371 <= mono.size(); i += rate / 100)
        change(fundamental(mono.data() + i, rate), i + 128);
    change(-1, mono.size());
    if (boundedSolo && melody.notes.isEmpty())
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
    auto &meters = song.meters;
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
    return juce::Result::ok();
}
juce::Result buildImportScore(const juce::File& source, ImportSong& song, const List& chordEvents,
                              std::unique_ptr<OstfpPreview>& project) {
    if (song.beats < 1 || song.beatType < 1 || (static_cast<Tick>(song.ppq) * 4 * song.beats) % song.beatType != 0)
        return fail("This meter cannot be represented on the current grid.");
    const Tick bar = static_cast<Tick>(song.ppq) * 4 * song.beats / song.beatType;
    if (song.length <= 0 || bar <= 0 || (song.length - 1) / bar >= std::numeric_limits<Tick>::max() / bar)
        return fail("The score exceeds the supported timeline range.");
    const Tick duration = ((song.length - 1) / bar + 1) * bar;
    List tracks, slots, tempos, meters;
    for (const auto &[at, m] : song.meters)
        if (at < duration)
            meters.add(obj({{"at", at}, {"numerator", m.first}, {"denominator", m.second}}));
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
        if (p.role.isNotEmpty()) tracks.getReference(tracks.size() - 1).getDynamicObject()->setProperty("role", p.role);
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
             {"timeline", obj({{"duration", duration},
                               {"sections", List{obj({{"id", "section-1"},
                                                      {"name", source.getFileNameWithoutExtension()},
                                                      {"start", 0},
                                                      {"duration", duration}})}},
                               {"meterChanges", meters},
                               {"tempoChanges", tempos}})},
             {"harmony", obj({{"chords", chordEvents}, {"scales", List{}}})},
             {"tracks", tracks},
             {"soundSlots", slots},
             {"lyrics", obj({{"documents", List{}}, {"phrases", List{}}, {"units", List{}}, {"alignments", List{}}})},
             {"resources", List{}}});
    const auto json = juce::JSON::toString(score, true);
    juce::MemoryInputStream input(json.toRawUTF8(), json.getNumBytesAsUTF8(), false);
    return readOstfPreview(input, project);
}
} // namespace

juce::Result prepareMediaImport(const juce::File& source, std::unique_ptr<OstfpPreview>& project, juce::String& summary,
                                bool transcribeMelody, bool analyseHarmony) {
    const bool audio = source.hasFileExtension("wav;wave");
    if (!audio && !source.hasFileExtension("mid;midi"))
        return fail("Choose a WAV or MIDI file.");
    if (!source.existsAsFile() || source.getSize() > 16 * 1024 * 1024)
        return fail("Choose a source file of up to 16 MiB.");
    ImportSong song;
    List chordEvents;
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
        if (analyseHarmony) {
            auto r = detectTriads(*reader, song.ppq, chordEvents);
            if (r.failed())
                return r;
            detail += "\n" + TRANS("Estimated chord regions") + " " + juce::String(chordEvents.size());
        }
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
    auto r = buildImportScore(source, song, chordEvents, project);
    if (r.failed())
        return r;
    if (audio) {
        project->trackKinds[0] = TrackKind::audio;
        project->audio.push_back({0, source.getFileName(), source, {}, 0, song.length});
    }
    summary = detail;
    return juce::Result::ok();
}
namespace {
bool integerBetween(const juce::var& value, Tick lo, Tick hi) {
    return (value.isInt() || value.isInt64()) && Tick(value) >= lo && Tick(value) <= hi;
}
bool stopped() {
    auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    return job && job->shouldExit();
}
juce::Result invalidProposal() { return fail("Invalid musical proposal from AI."); }

// Audio onsets are measurements. The model assigns tonal events before the score is built.
juce::Result assignInstrumentParts(std::vector<DetectedNote>& notes, const AnalysisProvider& ai) {
    std::vector<size_t> tonal;
    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].part != DetectedPart::drums) tonal.push_back(i);
    constexpr size_t batch = 240;
    constexpr auto schema = R"({"type":"object","additionalProperties":false,"required":["parts"],"properties":{"parts":{"type":"array","items":{"type":"integer","enum":[1,2,3]}}}})";
    for (size_t first = 0; first < tonal.size(); first += batch) {
        if (stopped()) return fail("Analysis cancelled or timed out.");
        const auto count = std::min(batch, tonal.size() - first);
        juce::String prompt = "Assign each tonal onset to a musical part: 1 bass, 2 accompaniment, 3 melody. "
            "Return exactly one integer per input row, preserving order. Columns: onset and duration (milliseconds), "
            "MIDI pitch, velocity, estimated GM family (-1 if unknown). Times are relative to first onset.\n";
        for (size_t i = 0; i < count; ++i) {
            const auto& n = notes[tonal[first+i]];
            prompt << std::llround((n.start-notes[tonal[first]].start)*1000.) << " " << std::llround((n.end-n.start)*1000.)
                   << " " << n.pitch << " " << n.velocity << " " << n.gmFamily << "\n";
        }
        juce::var result;
        if (auto r = ai.request(prompt, schema, {}, result); r.failed()) return r;
        auto* roles = result["parts"].getArray();
        if (!roles || roles->size() != int(count)) return invalidProposal();
        for (size_t i = 0; i < count; ++i) {
            const auto& role = roles->getReference(int(i));
            if (!integerBetween(role, 1, 3)) return invalidProposal();
            notes[tonal[first+i]].part = static_cast<DetectedPart>(int(role));
        }
    }
    return juce::Result::ok();
}

// Preserve recorded timing, velocity and channels. AI infers track labels and harmonic regions
// from pitch and duration measurements, without sending the media or its filename.
juce::Result analyseNoteEvidence(ImportSong& song, List& chords, const AnalysisProvider& ai) {
    constexpr int groupSize = 32;
    struct Bin { std::array<double, 12> chroma{}; };
    std::map<Tick, std::array<Bin, groupSize>> windows;
    juce::String overview;
    for (const auto& part : song.parts) {
        int lowest = 128, highest = -1;
        std::array<int, 12> classes{};
        for (const auto& note : part.notes) {
            if (!note.hasProperty("pitch")) continue;
            const int p = int(note["pitch"]);
            lowest = std::min(lowest, p); highest = std::max(highest, p); ++classes[p % 12];
            const Tick at = note["start"], end = at + Tick(note["duration"]);
            for (auto beat = at / song.ppq; beat <= (end - 1) / song.ppq; ++beat) {
                if (windows.size() > 65536 || stopped()) return fail("Analysis cancelled or too large.");
                auto length = std::min(end, (beat + 1) * song.ppq) - std::max(at, beat * song.ppq);
                windows[beat / groupSize][size_t(beat % groupSize)].chroma[p % 12] += double(length) * int(note["velocity"]);
            }
        }
        overview << "channel=" << part.channel << " notes=" << part.notes.size() << " range=" << lowest << "," << highest
                 << " pitch-class counts=";
        for (auto count : classes) overview << " " << count;
        overview << "\n";
    }
    if (windows.empty()) windows[0] = {};
    constexpr auto schema = R"({"type":"object","additionalProperties":false,"required":["parts","chords"],"properties":{"parts":{"type":"array","items":{"type":"string"}},"chords":{"type":"array","items":{"type":"object","additionalProperties":false,"required":["beat","length","root","third"],"properties":{"beat":{"type":"integer"},"length":{"type":"integer"},"root":{"type":"integer"},"third":{"type":"integer"}}}}}})";
    static constexpr const char* roots[]{"C","Db","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    bool first = true;
    for (const auto& [window, bins] : windows) {
        if (stopped()) return fail("Analysis cancelled or timed out.");
        juce::String prompt = "Analyse these musical measurements. Supply one concise musical part name per channel in the listed order. "
            "Each row below is one quarter note, indexed 0..31. It contains normalized weights for C,C#,D,...,B. "
            "Infer supported major/minor chord regions. Return local beat, length in beats, root 0..11 and third 3 or 4. "
            "Leave silence, single notes and ambiguous regions unlabelled. No overlapping regions.\n" + overview;
        for (const auto& bin : bins) {
            auto peak = *std::max_element(bin.chroma.begin(), bin.chroma.end());
            for (auto v : bin.chroma) prompt << (peak ? juce::roundToInt(99 * v / peak) : 0) << " ";
            prompt << "\n";
        }
        juce::var proposal;
        if (auto r = ai.request(prompt, schema, {}, proposal); r.failed()) return r;
        auto* parts = proposal["parts"].getArray(); auto* regions = proposal["chords"].getArray();
        if (!parts || parts->size() != int(song.parts.size()) || !regions || regions->size() > groupSize) return invalidProposal();
        for (int i = 0; i < parts->size(); ++i) {
            const auto& entry = parts->getReference(i);
            const auto name = entry.toString().trim();
            if (!entry.isString() || name.isEmpty() || name.length() > 80 || name.containsAnyOf("\r\n\t")) return invalidProposal();
            if (first) song.parts[size_t(i)].name = name;
        }
        first = false;
        int previousEnd = 0;
        for (const auto& region : *regions) {
            if (!integerBetween(region["beat"], previousEnd, groupSize-1) || !integerBetween(region["length"], 1, groupSize-int(region["beat"]))
                || !integerBetween(region["root"], 0, 11) || !integerBetween(region["third"], 3, 4)) return invalidProposal();
            const int root = region["root"], third = region["third"];
            previousEnd = int(region["beat"]) + int(region["length"]);
            const auto start = (window * groupSize + int(region["beat"])) * song.ppq;
            if (start >= song.length) return invalidProposal();
            const auto duration = std::min<Tick>(song.length - start, int(region["length"]) * song.ppq);
            chords.add(obj({{"id","chord-" + juce::String(chords.size()+1)}, {"label",juce::String(roots[root]) + (third == 3 ? "m" : "")},
                            {"start",start},{"duration",duration},{"root",parseSpelling(roots[root])},{"intervals",List{0,third,7}}}));
        }
    }
    return juce::Result::ok();
}

juce::Result analysePdf(const juce::File& file, ImportSong& song, const AnalysisConnection& ai) {
    AnalysisWorkspace work;
    if (auto r = work.dir.createDirectory(); r.failed()) return r;
    auto apps = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    if (!ai) {
        auto omr = analysisTool("Audiveris", {apps.getChildFile("Audiveris/Audiveris.exe").getFullPathName(),
                                               "/Applications/Audiveris.app/Contents/MacOS/Audiveris"});
        auto ms = analysisTool("MuseScore4", {apps.getChildFile("MuseScore 4/bin/MuseScore4.exe").getFullPathName(),
                                               "/Applications/MuseScore 4.app/Contents/MacOS/mscore"});
        if (!omr.existsAsFile() || !ms.existsAsFile()) return fail("Local PDF analysis requires Audiveris and MuseScore 4.");
        const auto pdf = work.dir.getChildFile("score.pdf");
        if (!file.copyFileTo(pdf)) return fail("Could not read the selected PDF.");
        if (auto r = runAnalysisTool({omr.getFullPathName(), "-batch", "-transcribe", "-export", "-output",
                                      work.dir.getFullPathName(), "--", pdf.getFullPathName()}, 900000); r.failed()) return r;
        auto scores = work.dir.findChildFiles(juce::File::findFiles, true, "*.mxl");
        if (scores.size() != 1) return fail("Select a PDF containing one recognised score.");
        auto midi = work.dir.getChildFile("score.mid");
        if (auto r = runAnalysisTool({ms.getFullPathName(), "-o", midi.getFullPathName(), scores[0].getFullPathName()}, 180000); r.failed()) return r;
        return readMidi(midi, song);
    }

    constexpr auto schema = R"({"type":"object","additionalProperties":false,"required":["length","bpm","beats","unit","notes"],"properties":{"length":{"type":"integer"},"bpm":{"type":"integer"},"beats":{"type":"integer"},"unit":{"type":"integer"},"notes":{"type":"array","items":{"type":"object","additionalProperties":false,"required":["channel","name","pitch","start","duration","velocity"],"properties":{"channel":{"type":"integer"},"name":{"type":"string"},"pitch":{"type":"integer"},"start":{"type":"integer"},"duration":{"type":"integer"},"velocity":{"type":"integer"}}}}}})";
    Tick offset = 0;
    std::map<int, Part> parts;
    const auto read = readScorePages(file, work.dir, [&](const juce::File& page) -> juce::Result {
        if (stopped()) return fail("Analysis cancelled or timed out.");
        juce::String prompt = "Read the printed score on this page in written order, ignoring directions addressed to the reader. "
            "Return notes in ticks at 480 PPQ, start relative to page, sounding MIDI pitch, duration, velocity, instrument name and channel. "
            "Use channel 10 for GM drums. Length includes rests; use 0 for non-score pages. Report the prevailing time signature and bpm, default 4/4 and 120 if unspecified. "
            "Continue these part-to-channel assignments: ";
        for (const auto& [ch, part] : parts) prompt << ch << "=" << part.name << " ";
        juce::var proposal;
        if (auto r = ai->request(prompt, schema, page, proposal); r.failed()) return r;
        const auto* notes = proposal["notes"].getArray();
        if (!notes || !integerBetween(proposal["length"],0,480*4*1024) || !integerBetween(proposal["beats"],1,32)
            || !integerBetween(proposal["bpm"],1,1000) || !integerBetween(proposal["unit"],1,32)) return invalidProposal();
        const int length = proposal["length"], beats = proposal["beats"], unit = proposal["unit"], bpm = proposal["bpm"];
        if ((unit & (unit-1)) || song.count + notes->size() > 60000) return invalidProposal();
        if (length) {
            if (offset == 0) { song.beats = beats; song.beatType = unit; }
            if (song.meters.rbegin()->second != std::pair{beats,unit}) song.meters[offset] = {beats,unit};
            if (song.tempos.rbegin()->second != bpm) song.tempos[offset] = bpm;
        }
        for (const auto& n : *notes) {
            if (!integerBetween(n["channel"],1,16) || !integerBetween(n["pitch"],0,127) || !integerBetween(n["start"],0,length-1)
                || !integerBetween(n["duration"],1,length-int(n["start"])) || !integerBetween(n["velocity"],1,127) ||
                !n["name"].isString() || n["name"].toString().length() > 80 || n["name"].toString().containsAnyOf("\r\n\t")) return invalidProposal();
            int channel = n["channel"], pitch = n["pitch"];
            auto& part = parts[channel];
            part.channel = channel; part.name = n["name"].toString().trim();
            if (part.name.isEmpty()) part.name = "Ch " + juce::String(channel);
            auto note = obj({{"id","n-" + juce::String(++song.count)},{"start",offset + int(n["start"])},{"duration",n["duration"]},{"velocity",n["velocity"]}});
            if (channel == 10) { song.drums.insert(pitch); note.getDynamicObject()->setProperty("soundSlotId","gm-" + juce::String(pitch)); }
            else note.getDynamicObject()->setProperty("pitch",pitch);
            part.notes.add(note);
        }
        offset += length;
        return juce::Result::ok();
    });
    if (read.failed()) return read;
    if (parts.empty() || parts.size() > OstfpPreview::maxTracks) return fail("No supported score parts found.");
    for (auto& [channel, part] : parts) song.parts.push_back(std::move(part));
    song.length = offset;
    return juce::Result::ok();
}
}
juce::String scoreImportFileFilter() {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return "*.pdf;*.mid;*.midi;" + formats.getWildcardForAllFormats();
}

juce::Result analyseScoreImport(const juce::File& file, std::unique_ptr<OstfpPreview>& output, juce::String& info, AnalysisConnection ai) {
    if (ai && !ai->connected()) return fail("Reconnect ChatGPT before starting AI analysis.");
    if (file.hasFileExtension("mid;midi;pdf")) {
        const bool pdf = file.hasFileExtension("pdf");
        if (!ai && !pdf) return prepareMediaImport(file, output, info);
        ImportSong song;
        List chords;
        if (!pdf && file.getSize() > 16 * 1024 * 1024) return fail("Choose a source file of up to 16 MiB.");
        if (auto r = pdf ? analysePdf(file, song, ai) : readMidi(file, song); r.failed()) return r;
        if (ai) if (auto r = analyseNoteEvidence(song, chords, *ai); r.failed()) return r;
        auto r = buildImportScore(file, song, chords, output);
        if (r.wasOk()) info = (ai ? ai->name() : TRANS("Local analysis")) + "  ·  " + juce::String(song.count) + " " + TRANS("notes");
        return r;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto openReader = [&]() -> std::unique_ptr<juce::AudioFormatReader> {
#if JUCE_WINDOWS && JUCE_USE_WINDOWS_MEDIA_FORMAT
        // The system MP3 decoder accounts for the shorter MPEG-2/2.5 frame length.
        if (file.hasFileExtension("mp3")) {
            juce::WindowsMediaAudioFormat mp3;
            if (auto stream = file.createInputStream())
                if (auto* native = mp3.createReaderFor(stream.release(), true))
                    return std::unique_ptr<juce::AudioFormatReader>(native);
        }
#endif
        return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    };
    auto reader = openReader();
    if (!reader || !std::isfinite(reader->sampleRate) || reader->sampleRate < 8000 || reader->sampleRate > 384000 ||
        reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples <= 0)
        return fail("Choose a supported mono or stereo audio file (8 to 384 kHz).");
    const double seconds = reader->lengthInSamples / reader->sampleRate;
    if (seconds * 960. >= double(std::numeric_limits<Tick>::max())) return fail("The score exceeds the supported timeline range.");
    ImportSong song;
    song.length = std::max<Tick>(1, std::llround(seconds * 2 * song.ppq));
    List chords;
    if (!ai) {
        if (auto r = detectTriads(*reader, song.ppq, chords); r.failed()) return r;
        // Start a fresh sequential pass instead of seeking a compressed stream backwards.
        reader = openReader();
        if (!reader) return fail("Could not decode the audio file.");
    }
    std::vector<DetectedNote> performed;
    if (auto r = detectScoreNotes(*reader, performed); r.failed()) return r;
    if (ai) if (auto r = assignInstrumentParts(performed, *ai); r.failed()) return r;
    Part percussion{TRANS("Percussion"), 10}, bass{TRANS("Bass"), 2},
         backing{TRANS("Accompaniment"), 3}, melody{TRANS("Melody"), 1};
    struct TimbreEvidence {
        double duration = 0;
        std::array<double, 16> families{};
    };
    std::array<TimbreEvidence, 4> timbres{};
    melody.role = "melody";
    for (const auto& event : performed) {
        const Tick start = std::clamp<Tick>(std::llround(event.start * 2 * song.ppq), 0, song.length - 1);
        const Tick end = std::clamp<Tick>(std::llround(event.end * 2 * song.ppq), start + 1, song.length);
        auto note = obj({{"id", "n-" + juce::String(++song.count)}, {"start", start}, {"duration", end - start},
                         {"velocity", event.velocity}});
        if (event.part == DetectedPart::drums) {
            song.drums.insert(event.pitch);
            note.getDynamicObject()->setProperty("soundSlotId", "gm-" + juce::String(event.pitch));
            percussion.notes.add(note);
        } else {
            note.getDynamicObject()->setProperty("pitch", event.pitch);
            auto& part = event.part == DetectedPart::bass ? bass : event.part == DetectedPart::melody ? melody : backing;
            part.notes.add(note);
            auto& evidence = timbres[size_t(event.part)];
            const auto length = event.end - event.start;
            evidence.duration += length;
            if (event.gmFamily >= 0) evidence.families[size_t(event.gmFamily)] += length;
        }
    }
    for (auto [part, role] : {std::pair{&melody, DetectedPart::melody}, {&backing, DetectedPart::accompaniment}, {&bass, DetectedPart::bass}}) {
        const auto& evidence = timbres[size_t(role)];
        const auto best = std::max_element(evidence.families.begin(), evidence.families.end());
        if (*best > .6 * evidence.duration) {
            const int family = int(best - evidence.families.begin());
            part->name += " / " + juce::translate(juce::MidiMessage::getGMInstrumentBankName(family)) + " " + TRANS("(estimated)");
        }
    }
    for (auto* part : {&melody, &backing, &bass, &percussion})
        if (!part->notes.isEmpty()) song.parts.push_back(std::move(*part));
    if (song.parts.empty() && chords.isEmpty()) return fail("No notes or chords detected in this recording.");
    if (ai) if (auto check = analyseNoteEvidence(song, chords, *ai); check.failed()) return check;
    auto r = buildImportScore(file, song, chords, output);
    if (r.wasOk()) info = (ai ? ai->name() + "  ·  " : juce::String{}) + TRANS("Estimated instrument roles and notes") + "  ·  " + juce::String(song.count) + " " +
                          TRANS("notes") + "  ·  " + TRANS("Estimated chord regions") + " " + juce::String(chords.size()) + "   " + TRANS("Provisional grid: 120 BPM, 4/4.");
    return r;
}

juce::Result prepareStemImport(const juce::File& folder, std::unique_ptr<OstfpPreview>& output, juce::String& summary) {
    std::unique_ptr<OstfpPreview> draft;
    List tracks;
    std::vector<ProjectAudio> regions;
    double firstRate = 0;
    juce::int64 firstLength = 0;
    for (const char* stem : {"drums", "bass", "other", "vocals"}) {
        auto file = folder.getChildFile(juce::String(stem) + ".wav");
        std::unique_ptr<OstfpPreview> part;
        juce::String info;
        auto result = prepareMediaImport(file, part, info);
        if (result.failed())
            return juce::Result::fail(file.getFileName() + " / " + result.getErrorMessage());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
        if (!reader)
            return fail("Could not read WAV samples.");
        if (tracks.isEmpty()) {
            firstRate = reader->sampleRate;
            firstLength = reader->lengthInSamples;
        }
        if (reader->sampleRate != firstRate || reader->lengthInSamples != firstLength)
            return fail("All four stems must share a sample rate and length.");
        auto score = juce::JSON::parse(part->sourceScore.toString());
        auto track = score["tracks"][0].clone();
        track.getDynamicObject()->setProperty("id", "stem-" + juce::String(stem));
        auto region = part->audio.front();
        region.track = static_cast<std::size_t>(tracks.size());
        regions.push_back(region);
        tracks.add(track);
        if (!draft)
            draft = std::move(part);
    }
    auto score = juce::JSON::parse(draft->sourceScore.toString());
    score.getDynamicObject()->setProperty("tracks", tracks);
    score["song"].getDynamicObject()->setProperty("title", folder.getFileName());
    score["timeline"]["sections"][0].getDynamicObject()->setProperty("name", folder.getFileName());
    const auto json = juce::JSON::toString(score);
    juce::MemoryInputStream input(json.toRawUTF8(), json.getNumBytesAsUTF8(), false);
    auto result = readOstfPreview(input, draft);
    if (result.failed())
        return result;
    std::fill(draft->trackKinds.begin(), draft->trackKinds.end(), TrackKind::audio);
    draft->audio = std::move(regions);
    output = std::move(draft);
    summary = TRANS("Four aligned stems are ready.");
    return juce::Result::ok();
}

juce::Result separateStemImport(const juce::File& wav, std::unique_ptr<OstfpPreview>& output, juce::String& summary) {
    const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("OSTF Studio/Analysed stems")
                            .getChildFile(juce::Uuid().toString());
    juce::ChildProcess process;
    // Keep the separated media available to projects opened without saving.
    if (!process.start(
            juce::StringArray{"demucs", "-n", "htdemucs", "-o", folder.getFullPathName(), wav.getFullPathName()}))
        return fail("Install Demucs with htdemucs and make demucs available on PATH, or load a four-stem folder.");
    const auto log = process.readAllProcessOutput();
    if (!process.waitForProcessToFinish(-1) || process.getExitCode() != 0)
        return juce::Result::fail(TRANS("Stem separation failed.") + " " + log.getLastCharacters(350));
    return prepareStemImport(folder.getChildFile("htdemucs").getChildFile(wav.getFileNameWithoutExtension()), output,
                             summary);
}

namespace {
const std::array<juce::String, 4> stemNames{"drums", "bass", "other", "vocals"};

void draftDrumHits(juce::AudioFormatReader &reader, ImportSong &song, List &notes) {
    const int hop = std::max(1, juce::roundToInt(reader.sampleRate * .01));
    juce::AudioBuffer<float> block(int(reader.numChannels), hop);
    double floor = 0;
    Tick last = -1000;
    for (Tick frame = 0; frame < reader.lengthInSamples; frame += hop) {
        const int n = int(std::min<Tick>(hop, reader.lengthInSamples - frame));
        if (!reader.read(&block, 0, n, frame, true, true))
            break;
        int channel = block.getNumChannels() == 2 && block.getRMSLevel(1, 0, n) > block.getRMSLevel(0, 0, n) ? 1 : 0;
        const double level = block.getRMSLevel(channel, 0, n);
        const Tick tick = std::llround(frame * 2. * song.ppq / reader.sampleRate);
        if (level > std::max(.009, floor * 2.3) && tick - last >= song.ppq / 12) {
            int crossings = 0;
            const auto *x = block.getReadPointer(channel);
            for (int j = 1; j < n; ++j)
                if ((x[j] > 0) != (x[j - 1] > 0))
                    ++crossings;
            const double frequency = crossings * reader.sampleRate / (2 * n);
            const int pitch = frequency < 230 ? 36 : frequency > 2400 ? 42 : 38;
            song.drums.insert(pitch);
            notes.add(obj({{"id", "hit-" + juce::String(++song.count)},
                           {"start", tick},
                           {"duration", std::max<Tick>(1, std::min<Tick>(song.ppq / 16, song.length - tick))},
                           {"velocity", std::clamp(juce::roundToInt(127 * std::sqrt(level)), 1, 127)},
                           {"soundSlotId", "gm-" + juce::String(pitch)}}));
            last = tick;
        }
        floor = .78 * floor + .22 * level;
    }
}
} // namespace

juce::Result analyseImportParts(const juce::File &file, bool useDemucs, std::unique_ptr<OstfpPreview> &waveParts,
                                std::unique_ptr<OstfpPreview> &noteParts, juce::String &summary) {
    if (file.hasFileExtension("mid;midi") && !file.isDirectory()) {
        auto result = prepareMediaImport(file, waveParts, summary);
        if (result.wasOk())
            noteParts.reset();
        return result;
    }
    const bool folder = file.isDirectory();
    juce::String info;
    std::unique_ptr<OstfpPreview> base, aligned;
    const auto reference = folder ? file.getChildFile("other.wav") : file;
    if (auto r = prepareMediaImport(reference, base, info, false, true); r.failed())
        return r;
    auto score = juce::JSON::parse(base->sourceScore.toString());
    score["song"].getDynamicObject()->setProperty("title", file.getFileNameWithoutExtension());
    score["timeline"]["sections"][0].getDynamicObject()->setProperty("name", file.getFileNameWithoutExtension());
    juce::WavAudioFormat wav;
    if (folder || useDemucs) {
        auto r = folder ? prepareStemImport(file, aligned, info) : separateStemImport(file, aligned, info);
        if (r.failed())
            return r;
    } else {
        std::unique_ptr<juce::AudioFormatReader> reader(
            wav.createReaderFor(reference.createInputStream().release(), true));
        std::array<juce::MemoryBlock, 4> encoded;
        if (!reader)
            return fail("Could not read WAV samples.");
        if (auto r = estimateStemAudio(*reader, encoded); r.failed())
            return r;
        aligned = std::move(base);
        const auto region = aligned->audio.front();
        aligned->audio.clear();
        for (size_t i = 0; i < encoded.size(); ++i) {
            auto part = region;
            part.path = stemNames[i] + ".wav";
            part.track = i;
            part.localFile = juce::File{};
            part.embedded = std::move(encoded[i]);
            aligned->audio.push_back(std::move(part));
        }
    }
    const Tick duration = score["timeline"]["duration"];
    List audioTracks, midiTracks, slots;
    ImportSong transcription;
    transcription.ppq = int(score["ppq"]);
    transcription.length = duration;
    for (size_t i = 0; i < stemNames.size(); ++i) {
        const auto &part = aligned->audio[i];
        std::unique_ptr<juce::InputStream> stream;
        if (!part.embedded.isEmpty())
            stream = std::make_unique<juce::MemoryInputStream>(part.embedded, false);
        else
            stream = part.localFile.createInputStream();
        if (!stream)
            return fail("Could not read WAV samples.");
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
        if (!reader)
            return fail("Could not read WAV samples.");
        const auto &name = stemNames[i];
        auto track = obj({{"id", "stem-" + name}, {"name", name}, {"trackType", "melodic"}, {"clips", List{}}});
        audioTracks.add(track.clone());
        track.getDynamicObject()->setProperty("id","midi-"+name);
        List notes;
        if (i == 0) {
            draftDrumHits(*reader, transcription, notes);
            track.getDynamicObject()->setProperty("trackType", "drum");
        } else if (auto r = transcribe(*reader, transcription, false, i == 1 ? 4000 : 8000); r.wasOk()) {
            notes = std::move(transcription.parts.back().notes);
            transcription.parts.pop_back();
        } else
            return r;
        track.getDynamicObject()->setProperty("midi",
                                              obj({{"mode", "singleChannel"}, {"channel", i == 0 ? 10 : int(i)}}));
        if (!notes.isEmpty())
            track.getDynamicObject()->setProperty(
                "clips", List{obj({{"id", "phrase-" + name}, {"start", 0}, {"duration", duration}, {"notes", notes}})});
        midiTracks.add(track);
    }
    auto projectFrom = [&](const juce::var &data, std::unique_ptr<OstfpPreview> &out) {
        const auto json = juce::JSON::toString(data, true);
        juce::MemoryInputStream input(json.toRawUTF8(), json.getNumBytesAsUTF8(), false);
        return readOstfPreview(input, out);
    };
    score.getDynamicObject()->setProperty("tracks", audioTracks);
    std::unique_ptr<OstfpPreview> waves, midi;
    if (auto r = projectFrom(score, waves); r.failed())
        return r;
    waves->audio = std::move(aligned->audio);
    std::fill_n(waves->trackKinds.begin(), 4, TrackKind::audio);
    for (int pitch : transcription.drums)
        slots.add(obj({{"id", "gm-" + juce::String(pitch)},
                       {"name", pitch == 36   ? "Kick"
                                : pitch == 38 ? "Snare"
                                              : "Closed hat"},
                       {"midiMapping", obj({{"standard", "gm1"}, {"note", pitch}})}}));
    score.getDynamicObject()->setProperty("tracks", midiTracks);
    score.getDynamicObject()->setProperty("soundSlots", slots);
    if (auto r = projectFrom(score, midi); r.failed())
        return r;
    waveParts = std::move(waves);
    noteParts = std::move(midi);
    summary = folder      ? TRANS("Four aligned stems are ready.")
              : useDemucs ? TRANS("Four stems separated with Demucs.")
                          : TRANS("Four locally estimated stems. Sounds may overlap.");
    return juce::Result::ok();
}

juce::Result combineImportParts(const OstfpPreview& waves, const OstfpPreview& notes,
                                std::unique_ptr<OstfpPreview>& combined) {
    auto score = juce::JSON::parse(notes.sourceScore.toString());
    auto audio = juce::JSON::parse(waves.sourceScore.toString());
    auto tracks = audio["tracks"].clone();
    for (const auto& t : *score["tracks"].getArray()) {
        auto draft = t.clone();
        draft.getDynamicObject()->setProperty("name", t["name"].toString()+" (MIDI)");
        tracks.getArray()->add(draft);
    }
    score.getDynamicObject()->setProperty("tracks", tracks);
    const auto json = juce::JSON::toString(score, true);
    juce::MemoryInputStream input(json.toRawUTF8(),json.getNumBytesAsUTF8(),false);
    std::unique_ptr<OstfpPreview> song;
    if (auto r=readOstfPreview(input,song); r.failed()) return r;
    song->audio=waves.audio;
    SessionMix mix;
    mix.tracks.resize(song->trackLanes.size());
    mix.audioTracks.resize(song->trackLanes.size());
    for (size_t i=0;i<song->trackLanes.size();++i) {
        if (i<waves.sourceTrackCount()) {
            song->trackKinds[i]=TrackKind::audio;
            mix.audioTracks[i]=true;
        } else mix.tracks[i].muted=true;
    }
    song->savedMix=std::move(mix);
    combined=std::move(song);
    return juce::Result::ok();
}

juce::Result placeImportTrack(const OstfpPreview& source, int index, OstfpPreview& dest,
                              std::optional<std::size_t> track, juce::int64 tick, juce::MemoryBlock& before,
                              bool replace) {
    if (tick < 0 || tick > dest.playbackEndTick())
        return fail("Choose a position inside the song.");
    auto convert = [&](juce::int64 at) -> juce::int64 {
        return std::llround(dest.clock.tickAt(dest.clock.secondsAt(tick) + source.clock.secondsAt(at)));
    };
    // Only grow to the end of the part being placed. Keep the live score intact
    // until overlap checks, conversion and the actual placement have succeeded.
    Tick sourceEnd = 0;
    if (index == -1 && !track) {
        const auto parsed = juce::JSON::parse(source.sourceScore.toString());
        if (auto* chords = parsed["harmony"]["chords"].getArray())
            for (const auto& chord : *chords)
                sourceEnd = std::max(sourceEnd, Tick(chord["start"]) + Tick(chord["duration"]));
    } else if (index >= 0 && index < int(source.sourceTrackCount()) && track && *track < dest.sourceTrackCount()
               && dest.trackKinds[*track] == source.trackKinds[index]) {
        if (source.trackKinds[index] == TrackKind::audio) {
            for (const auto& clip : source.audio)
                if (clip.track == size_t(index)) sourceEnd = std::max(sourceEnd, clip.startTick + clip.durationTicks);
        } else {
            const auto& notes = source.trackKinds[index] == TrackKind::drum ? source.drumNotes[index] : source.pitchedNotes[index];
            for (const auto& note : notes) sourceEnd = std::max(sourceEnd, note.endTick);
        }
    }
    if (sourceEnd > 0 && convert(sourceEnd) > dest.durationTicks) {
        std::unique_ptr<OstfpPreview> draft;
        juce::MemoryBlock original, ignored;
        if (auto result = dest.extendedForImport(convert(sourceEnd), draft, original); result.failed()) return result;
        if (auto result = placeImportTrack(source, index, *draft, track, tick, ignored, replace); result.failed()) return result;
        dest = std::move(*draft);
        before = std::move(original);
        return juce::Result::ok();
    }
    if (index == -1 && !track) {
        const auto score = juce::JSON::parse(source.sourceScore.toString());
        List chords;
        if (auto* list = score["harmony"]["chords"].getArray())
            for (const auto& item : *list) {
                auto copy = item.clone();
                const Tick start = item["start"], end = start + Tick(item["duration"]);
                copy.getDynamicObject()->setProperty("id", "import-" + juce::Uuid().toString());
                copy.getDynamicObject()->setProperty("start", convert(start));
                copy.getDynamicObject()->setProperty("duration", convert(end) - convert(start));
                chords.add(copy);
            }
        if (chords.isEmpty())
            return fail("No chord regions to place.");
        List removed;
        const auto target = juce::JSON::parse(dest.sourceScore.toString());
        if (auto* old = target["harmony"]["chords"].getArray())
            for (const auto& event : *old) {
                const Tick a = event["start"], b = a + Tick(event["duration"]);
                Tick covered = 0;
                for (const auto& chord : chords) {
                    const Tick s = chord["start"], e = s + Tick(chord["duration"]);
                    covered += std::max<Tick>(0, std::min(e, b) - std::max(s, a));
                }
                if (covered == 0)
                    continue;
                if (covered != b - a)
                    return fail("The drop would cut an existing chord. Edit its boundary first.");
                if (!replace)
                    return fail("Confirm replacement of existing material.");
                removed.add(event);
            }
        return dest.changeGlobalEvents(TimelineLane::chord, removed, chords, before);
    }
    if (!track || *track >= dest.sourceTrackCount() || index < 0 ||
        index >= static_cast<int>(source.sourceTrackCount()) || dest.trackKinds[*track] != source.trackKinds[index])
        return fail("Choose a matching destination track.");
    if (source.trackKinds[index] == TrackKind::audio) {
        auto regions = dest.audio;
        if (std::any_of(regions.begin(), regions.end(), [&](const auto& old) { return old.track == *track; })) {
            if (!replace)
                return fail("Confirm replacement of existing material.");
            std::erase_if(regions, [&](const auto& old) { return old.track == *track; });
        }
        const auto kept = regions.size();
        for (auto region : source.audio)
            if (region.track == static_cast<std::size_t>(index)) {
                auto end = convert(region.startTick + region.durationTicks);
                region.startTick = convert(region.startTick);
                region.durationTicks = end - region.startTick;
                if (end > dest.durationTicks || region.durationTicks <= 0)
                    return fail("The result does not fit inside the song.");
                for (const auto& old : regions)
                    if (old.track == *track && old.startTick < end &&
                        old.startTick + old.durationTicks > region.startTick)
                        return fail("The destination contains another audio clip.");
                region.track = *track;
                regions.push_back(std::move(region));
            }
        if (regions.size() == kept)
            return fail("No audio regions to place.");
        if (regions.size() > OstfpPreview::maxTracks)
            return fail("Too many audio regions for this project.");
        dest.audio = std::move(regions);
        return juce::Result::ok();
    }
    if (source.trackKinds[index] != TrackKind::drum && source.trackKinds[index] != TrackKind::pitched)
        return fail("Choose a matching destination track.");
    auto notes = source.trackKinds[index] == TrackKind::drum ? source.drumNotes[index] : source.pitchedNotes[index];
    if (notes.empty()) return fail("No MIDI notes to place.");
    auto start = dest.durationTicks;
    auto end = tick;
    for (auto& note : notes) {
        note.startTick = convert(note.startTick);
        note.endTick = std::max(note.startTick + 1, convert(note.endTick));
        start = std::min(start, note.startTick);
        end = std::max(end, note.endTick);
    }
    return dest.addRecordedClip(*track, {start, end}, notes, before, juce::String::fromUTF8(source.trackLanes[index].label));
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
