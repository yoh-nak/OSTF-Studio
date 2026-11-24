#pragma once
#include "ui/LocalStemAnalysis.h"
#include "ui/StemTranscription.h"
#include "ui/SongMusic.h"

namespace ostfstudio::stems {
inline const std::array<juce::String, 4> names{"Drums", "Bass", "Other", "Vocals"};
inline constexpr auto chordTrackName = "OSTF Stem Separator Chords";

inline void addNote(juce::MidiMessageSequence& events, int channel, int pitch, int velocity,
                    juce::int64 start, juce::int64 duration) {
    events.addEvent(juce::MidiMessage::noteOn(channel, pitch, juce::uint8(velocity)), double(start));
    events.addEvent(juce::MidiMessage::noteOff(channel, pitch), double(start + duration));
}
inline bool writeMidi(const juce::File& file, const juce::String& name, juce::MidiMessageSequence& notes, int ppq) {
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::textMetaEvent(3, name));
    track.addEvent(juce::MidiMessage::tempoMetaEvent(500000));
    track.addSequence(notes, 0);
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(track);
    auto out = file.createOutputStream();
    if (!out || !midi.writeTo(*out, 0)) return false;
    out->flush();
    return out->getStatus().wasOk();
}

// The caller supplies a fresh directory and publishes it only after all writes succeed.
inline juce::Result exportParts(juce::AudioFormatReader& source, const juce::File& dir) {
    if (!std::isfinite(source.sampleRate) || source.sampleRate < 8000 || source.sampleRate > 384000)
        return juce::Result::fail("Use audio between 8 and 384 kHz.");
    std::array<juce::MemoryBlock, 4> audio;
    if (auto r = estimateStemAudio(source, audio); r.failed()) return r;
    scoreimport::ImportSong song;
    song.length = std::max<juce::int64>(1, std::llround(source.lengthInSamples * 2. * song.ppq / source.sampleRate));
    juce::WavAudioFormat wav;
    for (size_t i = 0; i < names.size(); ++i) {
        if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob(); job && job->shouldExit())
            return juce::Result::fail("Cancelled");
        if (!dir.getChildFile(names[i] + ".wav").replaceWithData(audio[i].getData(), audio[i].getSize()))
            return juce::Result::fail("Could not write stem audio.");
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::MemoryInputStream(audio[i], false), true));
        if (!reader) return juce::Result::fail("Could not read separated audio.");
        scoreimport::List notes;
        if (i == 0) scoreimport::draftDrumHits(*reader, song, notes);
        else {
            if (auto r = scoreimport::transcribe(*reader, song, false, i == 1 ? 4000 : 8000); r.failed()) return r;
            notes = std::move(song.parts.back().notes);
            song.parts.pop_back();
        }
        juce::MidiMessageSequence seq;
        for (const auto& n : notes) {
            const int pitch = i == 0 ? n["soundSlotId"].toString().fromLastOccurrenceOf("-",false,false).getIntValue() : int(n["pitch"]);
            addNote(seq, i == 0 ? 10 : int(i), pitch, int(n.getProperty("velocity", 90)), n["start"], n["duration"]);
        }
        if (!notes.isEmpty() && !writeMidi(dir.getChildFile(names[i] + ".mid"), names[i], seq, song.ppq))
            return juce::Result::fail("Could not write MIDI.");
    }
    scoreimport::List chords;
    if (auto r = scoreimport::detectTriads(source, song.ppq, chords); r.failed()) return r;
    juce::MidiMessageSequence seq;
    for (const auto& chord : chords) {
        const auto label = chord["label"].toString();
        seq.addEvent(juce::MidiMessage::textMetaEvent(6, label), double(chord["start"]));
        const auto root = 60 + pitchClassOf(chord["root"]);
        for (const auto& interval : *chord["intervals"].getArray())
            addNote(seq, 1, root + int(interval), 90, chord["start"], chord["duration"]);
    }
    if (!chords.isEmpty() && !writeMidi(dir.getChildFile("Chords.mid"), chordTrackName, seq, song.ppq))
        return juce::Result::fail("Could not write chord MIDI.");
    return juce::Result::ok();
}
}
