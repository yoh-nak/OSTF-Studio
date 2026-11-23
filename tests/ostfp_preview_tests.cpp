#include "ui/OstfpPreview.h"
#include "StockEffects.h"
#include "ui/SongMidi.h"
#include "ui/StudioLanguage.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <iostream>
#include <string_view>

static int checkGallery(const juce::File& directory) {
    const auto templateFile = directory.getParentDirectory().getParentDirectory().getChildFile("assets/NewProject.ostf");
    juce::FileInputStream templateStream(templateFile);
    std::unique_ptr<ostfstudio::OstfpPreview> fresh;
    const auto opened = ostfstudio::readOstfPreview(templateStream, fresh);
    if (opened.failed() || !fresh->trackLanes.empty() || fresh->previewBpm != 120 || fresh->bars.endBar != 33) {
        std::cerr << "Unexpected blank template: " << templateFile.getFullPathName() << " " << opened.getErrorMessage() << '\n';
        return 2;
    }
    for (bool package : {false, true}) {
        juce::MemoryOutputStream written;
        const ostfstudio::SessionMix defaultMix;
        const auto save = package ? ostfstudio::writeOstfpPreview(*fresh, defaultMix, written)
                                  : ostfstudio::writeOstfPreview(*fresh, defaultMix, written);
        juce::MemoryInputStream input(written.getData(), written.getDataSize(), false);
        std::unique_ptr<ostfstudio::OstfpPreview> reopened;
        const auto read = package ? ostfstudio::readOstfpPreview(input, reopened)
                                  : ostfstudio::readOstfPreview(input, reopened);
        if (save.failed() || read.failed() || !reopened->trackLanes.empty() || reopened->title != fresh->title) {
            std::cerr << (package ? "OSTFP" : "OSTF") << " blank round trip: "
                      << save.getErrorMessage() << " / " << read.getErrorMessage() << '\n';
            return 2;
        }
    }
    struct Expectation { const char* file; int bars; int tracks; double bpm; };
    constexpr Expectation cases[] {
        {"neon-platforms.ostfp", 76, 8, 106.0},
        {"blue-hour-waltz.ostfp", 128, 7, 132.0},
        {"relay-circuit.ostfp", 96, 12, 128.0},
        {"paper-lantern-replies.ostfp", 56, 9, 78.0},
        {"offset-bridges.ostfp", 0, 0, 0.0},
        {"copper-mile.ostfp", 64, 6, 0.0},
        {"first-light-vector.ostfp", 96, 11, 138.0},
    };
    for (const auto& expected : cases) {
        juce::FileInputStream input(directory.getChildFile(expected.file));
        std::unique_ptr<ostfstudio::OstfpPreview> project;
        const auto result = ostfstudio::readOstfpPreview(input, project);
        const bool matches = expected.bars == 0
            ? result.getErrorMessage() == "Preview requires meter changes to preserve bar length at bar boundaries."
            : result.wasOk() && project->bars.endBar == expected.bars + 1
              && project->sourceTrackCount() == static_cast<std::size_t>(expected.tracks)
              && project->chordVoicingTrack == static_cast<std::size_t>(expected.tracks)
              && project->previewBpm == expected.bpm && !project->pitchedNotes.front().empty()
              && project->chordVoicings > 0
              && std::count_if(project->trackLanes.begin(), project->trackLanes.end(),
                               [](const auto& lane) { return lane.regions.size() < 2; }) == static_cast<int>(project->audio.size());
        if (!matches) {
            std::cerr << expected.file << ": unexpected preview result: " << result.getErrorMessage() << '\n';
            return 1;
        }
        if (std::string_view(expected.file) == "copper-mile.ostfp") {
            const auto& env = project->panAutomation.at("lead");
            if (project->contextAt(ostfstudio::TimelineLane::meter,153600) != "2/2"
                || project->clock.bpmAt(153600) != 104 || project->clock.bpmAt(168960) != 116
                || env.points.size() != 5 || env.valueAt(138240,0) < .6) return 1;
        }
        if (expected.bars != 0) {
            const auto index = *project->chordVoicingTrack;
            if (std::count(project->mixerGroups.begin(), project->mixerGroups.end(),
                           ostfstudio::MixerTrackGroup::voicing) != 1) return 1;
            for (std::size_t i = 0; i < project->sourceTrackCount(); ++i) {
                if (juce::String(project->trackLanes[i].label).containsIgnoreCase("voicing")) return 1;
            }
            if (std::string_view(expected.file) == "neon-platforms.ostfp") {
                auto legacy = juce::JSON::parse(project->sourceScore.toString());
                auto tracks = legacy["tracks"];
                auto* piano = (*tracks.getArray())[1].getDynamicObject();
                piano->setProperty("name", "Keys Voicing");
                piano->setProperty("role", "voicing");
                const auto json = juce::JSON::toString(legacy);
                juce::MemoryInputStream source(json.toRawUTF8(), json.getNumBytesAsUTF8(), false);
                std::unique_ptr<ostfstudio::OstfpPreview> old;
                if (ostfstudio::readOstfPreview(source, old).failed() || old->isGlobalNoteTrack(1)
                    || old->pitchedNotes[1].empty() || !old->isGlobalNoteTrack(*old->chordVoicingTrack)) return 1;
            }
            const auto& voicing = project->pitchedNotes[index];
            if (!project->isGlobalNoteTrack(index) || !project->isGlobalNoteTrack(0)
                || voicing.empty() || voicing.front().startTick != 0 || !voicing.front().velocity
                || !juce::String(project->trackLanes[index].regions.front().label).containsAnyOf("ABCDEFG")
                || project->addSessionClip(index, "Should not become a Core track", {{0, 120, 60, 80}})) return 1;
            if (!project->savedMix || project->savedMix->instruments.size() != project->sourceTrackCount()) return 1;
            const auto& authored = *project->savedMix;
            juce::FileInputStream standalone(directory.getChildFile(expected.file).withFileExtension("ostf"));
            std::unique_ptr<ostfstudio::OstfpPreview> fromScore;
            if (ostfstudio::readOstfPreview(standalone, fromScore).failed() || !fromScore->savedMix
                || fromScore->savedMix->trackEffects != authored.trackEffects
                || fromScore->savedMix->outputEffects != authored.outputEffects) return 1;
            if (authored.outputEffects[0] != juce::StringArray{ostfstudio::stockEffects[4].chainId()}
                || authored.outputEffects[1] != juce::StringArray{ostfstudio::stockEffects[3].chainId()}
                || authored.outputEffects[3] != juce::StringArray{ostfstudio::stockEffects[0].chainId()}) return 1;
            std::array<int, 2> members{};
            int roomSends = 0;
            for (const auto& strip : authored.tracks) {
                if (strip.outputBus >= 0) ++members.at(static_cast<std::size_t>(strip.outputBus));
                if (strip.returnSends[0] > 0 && strip.returnSends[0] < 1) ++roomSends;
            }
            if (members[0] < 2 || members[1] < 2 || roomSends < 2
                || std::none_of(authored.trackEffects.begin(), authored.trackEffects.end(),
                                [](const auto& chain) { return chain.size() >= 2; })) return 1;
            for (std::size_t track = 0; track < project->sourceTrackCount(); ++track) {
                const auto& plugin = project->savedMix->instruments[track];
                if (project->trackKinds[track] == ostfstudio::TrackKind::audio) {
                    const auto onTrack = [track](const auto& r) { return r.track == track; };
                    const auto wav = std::find_if(project->audio.begin(), project->audio.end(), onTrack);
                    const auto plain = std::find_if(fromScore->audio.begin(), fromScore->audio.end(), onTrack);
                    if (plugin.isObject() || wav == project->audio.end() || plain == fromScore->audio.end()
                        || authored.trackEffects[track].size() != 2 || authored.tracks[track].outputBus < 0
                        || authored.tracks[track].returnSends[0] <= 0 || fromScore->trackKinds[track] != ostfstudio::TrackKind::audio)
                        return 1;
                    juce::MemoryBlock external;
                    if (wav->startTick != plain->startTick || wav->durationTicks <= 0 || wav->durationTicks != plain->durationTicks
                        || wav->frameCount != plain->frameCount || wav->frameCount <= 0 || wav->embedded.isEmpty()
                        || !directory.getChildFile(plain->path).loadFileAsData(external) || external != wav->embedded) return 1;
                    continue;
                }
                const auto& events = project->programChanges[track];
                if (plugin["format"] != "vst3" || plugin["name"] != "OSTF GM Sound Module" || events.empty()
                    || events.front().channel != project->sourceMidiChannels[track]
                    || project->savedMix->midiRoutes[track].channel != project->sourceMidiChannels[track]) return 1;
            }
            const auto regions = project->trackLanes[index].regions;
            if (std::none_of(regions.begin(), regions.end(), [](const auto& region) {
                    return region.exactBars && region.exactBars->end - region.exactBars->start < 1.0;
                }) || std::none_of(regions.begin(), regions.end(), [](const auto& region) {
                    return region.exactBars && region.exactBars->end - region.exactBars->start > 1.0;
                })) return 1;
            auto mix = authored;
            mix.tracks.resize(index + 1);
            mix.midiRoutes.resize(index + 1);
            mix.instruments = project->savedMix->instruments;
            mix.tracks[index].pan = 0.35f;
            mix.tracks[index].muted = true;
            mix.stockEffectStates[ostfstudio::SessionMix::effectSlot(true, 3, 0)] = "test-state";
            mix.trackEffects.resize(index + 1);
            mix.trackEffects[index].add(ostfstudio::stockEffects[2].chainId());
            mix.stockEffectStates[ostfstudio::SessionMix::effectSlot(false, index, 0)] = "voicing-state";
            juce::MemoryOutputStream plain;
            juce::TemporaryFile destination("ostf");
            const auto file = destination.getFile();
            const auto savedPlain = ostfstudio::writeOstfPreview(*project, mix, file, plain);
            if (!project->audio.empty()) file.getSiblingFile(file.getFileNameWithoutExtension() + ".media").deleteRecursively();
            if (savedPlain.failed()) return 1;
            juce::MemoryInputStream plainInput(plain.getData(), plain.getDataSize(), false);
            if (ostfstudio::readOstfPreview(plainInput, fromScore).failed() || !fromScore->savedMix
                || fromScore->savedMix->stockEffectStates != mix.stockEffectStates
                || fromScore->savedMix->trackEffects != mix.trackEffects) return 1;
            juce::MemoryOutputStream saved;
            if (ostfstudio::writeOstfpPreview(*project, mix, saved).failed()) return 1;
            juce::MemoryInputStream roundtrip(saved.getData(), saved.getDataSize(), false);
            std::unique_ptr<ostfstudio::OstfpPreview> reopened;
            if (ostfstudio::readOstfpPreview(roundtrip, reopened).failed() || !reopened->savedMix
                || reopened->sourceTrackCount() != index || reopened->sourceScore != project->sourceScore
                || reopened->savedMix->instruments.front()["pluginId"] != mix.instruments.front()["pluginId"]
                || reopened->savedMix->tracks.size() != index + 1 || !reopened->savedMix->tracks[index].muted
                || reopened->savedMix->tracks[index].pan != 0.35f
                || reopened->savedMix->outputNames != mix.outputNames
                || reopened->savedMix->outputEffects != mix.outputEffects
                || reopened->savedMix->stockEffectStates != mix.stockEffectStates) return 1;
            for (std::size_t i = 0; i < index; ++i)
                if (reopened->savedMix->trackEffects[i] != mix.trackEffects[i]
                    || reopened->savedMix->tracks[i].outputBus != mix.tracks[i].outputBus
                    || std::abs(reopened->savedMix->tracks[i].returnSends[0] - mix.tracks[i].returnSends[0]) > 0.00001f
                    || reopened->savedMix->tracks[i].pan != mix.tracks[i].pan) return 1;
        }
        if (std::string_view(expected.file) == "paper-lantern-replies.ostfp") {
            const auto& notes = project->pitchedNotes.front();
            const auto& curves = project->performanceCurves.front();
            const auto lane = [&](const juce::String& label) {
                return std::find_if(curves.begin(), curves.end(), [&](const auto& curve) { return curve.label == label; });
            };
            const auto cc = lane("CC 11");
            const auto channel = lane("Channel Aftertouch");
            const auto poly = lane("Poly Aftertouch / note " + juce::String(notes.front().pitch));
            const auto pitch = lane("Vocal / voice / Pitch (cents)");
            if (notes.size() < 160 || project->trackLanes.front().regions.size() != 6
                || project->trackLanes.front().regions.front().bars.firstBar != 5
                || cc == curves.end() || cc->points.size() < 40
                || cc->points.front().tick != notes.front().startTick
                || channel == curves.end() || channel->points.front().tick != notes.front().startTick + 200
                || poly == curves.end() || poly->points.front().tick != notes.front().startTick + 200
                || pitch == curves.end() || !pitch->cents || pitch->points.front().value != -24.0
                || project->trackLanes[7].regions.size() != 2 || project->pitchedNotes[7].size() != 10)
                return 1;
            for (const auto* parameter : {"dynamics", "breathiness", "tension", "brightness", "voicing"}) {
                const auto label = juce::String("Vocal / voice / ") + parameter;
                if (std::count_if(curves.begin(), curves.end(), [&](const auto& curve) {
                    return curve.label == label && curve.points.size() >= 6;
                }) != 6) return 1;
            }
            const auto score = juce::JSON::parse(project->sourceScore.toString());
            const auto lyrics = score["lyrics"];
            const auto units = lyrics["units"];
            const auto alignments = lyrics["alignments"];
            if (units.size() != static_cast<int>(notes.size() + project->pitchedNotes[7].size())
                || alignments.size() != units.size() || lyrics["voices"].size() != 2) return 1;
            std::unique_ptr<ostfstudio::OstfpPreview> doubled, restored;
            if (project->editTracks(ostfstudio::TrackKind::pitched, {}, doubled, 0).failed()) return 1;
            const auto duplicated = juce::JSON::parse(doubled->sourceScore.toString());
            const auto copyIndex = project->sourceTrackCount();
            const auto newTrack = duplicated["tracks"][static_cast<int>(copyIndex)];
            const auto voice = duplicated["lyrics"]["voices"][2];
            if (voice["trackId"] != newTrack["id"] || voice["id"] == lyrics["voices"][0]["id"]
                || newTrack["clips"][0]["vocalPerformances"][0]["voiceId"] != voice["id"]
                || duplicated["lyrics"]["units"].size() != units.size()
                || duplicated["lyrics"]["alignments"].size() != alignments.size() + static_cast<int>(notes.size())
                || doubled->editTracks(ostfstudio::TrackKind::pitched, copyIndex, restored).failed()) return 1;
            const auto audioIndex = restored->audio.front().track;
            const auto newName = juce::String::charToString(0x706f) + juce::String::charToString(0x308a);
            if (restored->renameTrack(audioIndex, "   ").wasOk() || restored->renameTrack(audioIndex, "  " + newName + "  ").failed()) return 1;
            const auto restoredScore = juce::JSON::parse(restored->sourceScore.toString());
            if (restoredScore["tracks"][static_cast<int>(audioIndex)]["name"].toString() != newName
                || restoredScore["tracks"][static_cast<int>(audioIndex)]["id"] != "foley"
                || juce::String::fromUTF8(restored->trackLanes[audioIndex].label) != newName) return 1;
            if (juce::JSON::toString(restoredScore["lyrics"]) != juce::JSON::toString(lyrics)) return 1;
        }
        std::cout << expected.file << (expected.bars == 0 ? ": mixed meter rejected as expected" : ": preview opened") << '\n';
    }
    return 0;
}

static int checkSmf(const juce::File& demo) {
    juce::FileInputStream input(demo);
    std::unique_ptr<ostfstudio::OstfpPreview> project;
    if (ostfstudio::readOstfPreview(input, project).failed()) return 1;
    auto channels = project->sourceMidiChannels;
    channels[0] = 15;
    if (!project->addSessionClip(0, "MIDI export test", {{37, 175, 94, 63}})) return 2;
    for (const auto format : {ostfstudio::SmfFormat::merged, ostfstudio::SmfFormat::multitrack}) {
        juce::MemoryOutputStream output;
        const auto result = ostfstudio::writeSongMidi(*project, format, channels, output);
        if (result.failed()) { std::cerr << result.getErrorMessage() << '\n'; return 3; }
        juce::MemoryInputStream data(output.getData(), output.getDataSize(), false);
        juce::MidiFile file;
        int type = -1;
        if (!file.readFrom(data, false, &type) || type != static_cast<int>(format)
            || file.getTimeFormat() != project->ppq || file.getNumTracks() != (type == 0 ? 1 : 9)
            || file.getLastTimestamp() != (project->bars.endBar - 1) * project->ticksPerBar) return 4;
        bool tempo = false, meter = false, on = false, off = false, drums = false, expression = false, program = false;
        for (int i = 0; i < file.getNumTracks(); ++i) {
            const auto& track = *file.getTrack(i);
            for (const auto* event : track) {
                const auto& m = event->message;
                if (i == 0 && m.isTempoMetaEvent()) tempo |= std::abs(m.getTempoSecondsPerQuarterNote() - 60.0 / 78) < 1e-6;
                if (i == 0 && m.isTimeSignatureMetaEvent()) {
                    int n, d; m.getTimeSignatureInfo(n, d); meter |= n == 4 && d == 4;
                }
                if (type == 1 && i == 0 && !m.isMetaEvent()) return 5;
                if ((i == 1 || type == 0) && m.isNoteOnOrOff() && m.getChannel() == 15 && m.getNoteNumber() == 94) {
                    on |= m.isNoteOn() && m.getTimeStamp() == 37 && m.getVelocity() == 63;
                    off |= m.isNoteOff() && m.getTimeStamp() == 175;
                }
                drums |= m.isNoteOn() && m.getChannel() == 10;
                expression |= m.isController() && m.getControllerNumber() == 11;
                program |= m.isProgramChange();
            }
        }
        if (!(tempo && meter && on && off && drums && expression && program)) return 6;
    }
    project->ppq = 32768;
    juce::MemoryOutputStream rejected;
    return ostfstudio::writeSongMidi(*project, ostfstudio::SmfFormat::merged, {}, rejected).failed()
        && rejected.getDataSize() == 0 ? 0 : 7;
}

// Also accepts external official examples so the same checks can run without redistributing songs.
static int legacyRoundTrip(const juce::File& file) {
    using namespace ostfstudio;
    juce::FileInputStream input(file);
    std::unique_ptr<OstfpPreview> project;
    const auto opened = readOstfPreview(input, project);
    if (opened.failed()) { std::cerr << opened.getErrorMessage() << '\n'; return 1; }
    juce::MemoryBlock original;
    if (!file.loadFileAsData(original) || original != project->sourceScore) return 2;
    auto expected = juce::JSON::parse(original.toString());
    if (project->sourceVersion != expected["version"].toString() || project->sourceVersion == "5.0.0") return 3;

    SessionMix mix;
    mix.tracks.resize(project->sourceTrackCount());
    for (int mode = 0; mode < 3; ++mode) {
        juce::MemoryOutputStream output;
        const auto saved = mode == 0 ? writeOstfPreview(*project, output)
                         : mode == 1 ? writeOstfPreview(*project, mix, output) : writeOstfpPreview(*project, mix, output);
        if (saved.failed()) { std::cerr << saved.getErrorMessage() << '\n'; return 4; }
        juce::MemoryInputStream data(output.getData(), output.getDataSize(), false);
        std::unique_ptr<OstfpPreview> next;
        const auto read = mode == 2 ? readOstfpPreview(data, next) : readOstfPreview(data, next);
        if (read.failed()) { std::cerr << read.getErrorMessage() << '\n'; return 5; }
        auto actual = juce::JSON::parse(next->sourceScore.toString());
        if (file.getFileName() == "4.0.0.ostf" && !next->sourceScore.toString().contains("0.1234567890123456789012345")) return 13;
        if (actual["version"] != "5.0.0" || next->bars.endBar != project->bars.endBar
            || next->sourceTrackCount() != project->sourceTrackCount()) return 6;
        actual.getDynamicObject()->setProperty("version", expected["version"]);
        if (expected.hasProperty("document")) {
            const auto doc = actual["document"];
            if (doc["id"] != expected["document"]["id"] || doc["revisionId"] == expected["document"]["revisionId"]
                || doc["parents"][0]["revisionId"] != expected["document"]["revisionId"]
                || doc["parents"][0]["sha256"] != juce::SHA256(original).toHexString()) return 7;
            actual.getDynamicObject()->setProperty("document", expected["document"]);
        }
        if (mode == 1) {
            auto ext = actual["extensions"];
            ext.getDynamicObject()->removeProperty("org.ostf.studio");
            if (!expected.hasProperty("extensions")) actual.getDynamicObject()->removeProperty("extensions");
        }
        if (juce::JSON::toString(actual, true, 17) != juce::JSON::toString(expected, true, 17)) return 8;
        if (project->sourceScore != original) return 9;
    }
    if (file.getFileName() == "4.0.0.ostf") {
        if (project->durationTicks != 3360 || project->bars.endBar != 3 || project->pitchedNotes[0].size() != 2
            || project->pitchedNotes[0][0].startTick != 1800 || project->programChanges[0][0].tick != 1800) return 10;
        juce::MemoryOutputStream midi;
        if (writeSongMidi(*project, SmfFormat::multitrack, {}, midi).failed()) return 11;
        // 3.5 cannot declare the 4.0 MIDI stream even though Studio understands it in 4.0.
        for (const auto* unsupported : {"3.5.0", "3.4.0", "4.0.1", "5.1.0"}) {
            expected.getDynamicObject()->setProperty("version", unsupported);
            const auto text = juce::JSON::toString(expected);
            juce::MemoryInputStream wrong(text.toRawUTF8(), text.getNumBytesAsUTF8(), false);
            const auto* previous = project.get();
            if (readOstfPreview(wrong, project).wasOk() || previous != project.get()) return 12;
        }
    }
    if (file.getFileName() == "4.3.0.ostf") {
        juce::FileInputStream packed(file.withFileExtension(".ostfp"));
        std::unique_ptr<OstfpPreview> historical;
        if (readOstfpPreview(packed, historical).failed() || historical->sourceVersion != "4.3.0") return 14;
        juce::MemoryOutputStream upgraded;
        if (writeOstfPreview(*historical, upgraded).failed() || juce::JSON::parse(upgraded.toUTF8())["version"] != "5.0.0") return 15;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--legacy")
        return legacyRoundTrip(juce::File::createFileWithoutCheckingPath(argv[2]));
    if (argc == 3 && std::string_view(argv[1]) == "--smf")
        return checkSmf(juce::File::createFileWithoutCheckingPath(argv[2]));
    if (argc == 3 && std::string_view(argv[1]) == "--languages") {
        using ostfstudio::resolvedLanguage;
        juce::LocalisedStrings japanese(juce::File::createFileWithoutCheckingPath(argv[2]), false);
        return resolvedLanguage("system", "ja-JP") == "ja" && resolvedLanguage("system", "JA_jp") == "ja"
            && resolvedLanguage("system", "fr-CA") == "en" && resolvedLanguage("system", "") == "en"
            && resolvedLanguage("ja", "de") == "ja" && resolvedLanguage("en", "ja") == "en"
            && japanese.translate("File") != "File" && japanese.translate("Save as MIDI (SMF)") != "Save as MIDI (SMF)"
            && japanese.translate("Unknown string") == "Unknown string" ? 0 : 1;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--gallery")
        return checkGallery(juce::File(juce::String::fromUTF8(argv[2])));
    if (argc != 2 && argc != 3) return 1;
    juce::FileInputStream input(juce::File::createFileWithoutCheckingPath(argv[1]));
    std::unique_ptr<ostfstudio::OstfpPreview> project;
    const auto result = ostfstudio::readOstfpPreview(input, project);
    if (result.failed()) {
        std::cerr << result.getErrorMessage() << '\n';
        return 1;
    }
    const auto view = project->view();
    if (view.bars.endBar != 17 || view.globalLanes.size() != 6 || view.trackLanes.size() != 3) return 1;
    if (std::string_view(view.globalLanes[0].regions[0].label) != "Intro"
        || view.globalLanes[1].regions.size() != 16
        || std::string_view(view.trackLanes[0].label) != "Lead Guitar"
        || view.trackLanes[0].regions[0].bars.firstBar != 11
        || view.trackLanes[0].regions[0].bars.endBar != 15) return 1;
    if (view.globalLanes[0].kind != ostfstudio::TimelineLane::section
        || view.globalLanes[1].kind != ostfstudio::TimelineLane::chord
        || view.globalLanes[3].kind != ostfstudio::TimelineLane::scale
        || std::string_view(view.globalLanes[3].regions[0].label) != "A natural minor") return 1;

    const auto& notes = project->pitchedNotes;
    using enum ostfstudio::MixerTrackGroup;
    if (project->mixerGroups != std::vector {pitched, pitched, other}) return 1;
    if (project->ppq != 960 || project->previewBpm != 122.0 || project->ticksPerBar != 3840
        || notes.size() != 3 || notes[0].size() != 2
        || notes[0][0].startTick != 38400 || notes[0][0].endTick != 39360 || notes[0][0].pitch != 69
        || notes[0][1].startTick != 39360 || notes[0][1].pitch != 72
        || notes[1].size() != 1 || notes[1][0].startTick != 0 || notes[1][0].pitch != 45
        || !notes[2].empty()) return 1;

    const char invalid[] = "Not a ZIP";
    juce::MemoryInputStream badInput(invalid, sizeof invalid - 1, false);
    const auto* previousProject = project.get();
    if (ostfstudio::readOstfpPreview(badInput, project).wasOk() || project.get() != previousProject) return 1;

    if (!input.setPosition(0)) return 1;
    juce::ZipFile source(input);
    juce::ZipFile::Builder altered;
    altered.addEntry(std::unique_ptr<juce::InputStream>(source.createStreamForEntry(0)), 0, "mimetype", {});
    std::unique_ptr<juce::InputStream> manifest(source.createStreamForEntry(1));
    auto json = juce::JSON::parse(manifest->readEntireStreamAsString());
    json["score"].getDynamicObject()->setProperty("sha256", juce::String::repeatedString("0", 64));
    const auto changedManifest = juce::JSON::toString(json);
    altered.addEntry(std::make_unique<juce::MemoryInputStream>(changedManifest.toRawUTF8(), changedManifest.getNumBytesAsUTF8(), true),
                     9, "manifest.json", {});
    altered.addEntry(std::unique_ptr<juce::InputStream>(source.createStreamForEntry(2)), 9, source.getEntry(2)->filename, {});
    juce::MemoryOutputStream archive;
    if (!altered.writeToStream(archive, nullptr)) return 1;
    juce::MemoryInputStream mismatch(archive.getData(), archive.getDataSize(), false);
    const auto failure = ostfstudio::readOstfpPreview(mismatch, project);
    if (failure.getErrorMessage() != "OSTFP score SHA-256 mismatch" || project.get() != previousProject) return 1;

    juce::MemoryOutputStream unchanged;
    if (ostfstudio::writeOstfPreview(*project, unchanged).failed()
        || unchanged.getMemoryBlock() != project->sourceScore) return 1;
    if (project->insertGeneratorPattern(0, 17) || notes[0].size() != 2
        || !project->insertGeneratorPattern(0, 2) || notes[0].size() != 6
        || notes[0][0].startTick != 3840 || notes[0][0].endTick != 4320 || notes[0][0].pitch != 60
        || notes[0][4].startTick != 38400 || notes[1].size() != 1
        || project->trackLanes[0].regions.size() != 2
        || project->trackLanes[0].regions.back().bars.firstBar != 2
        || project->trackLanes[0].regions.back().bars.endBar != 3) return 1;
    if (!project->addSessionClip(1, "Recorded MIDI", {{480, 720, 59, 77}})) return 1;

    juce::MemoryOutputStream ostf, ostfp;
    auto mix = ostfstudio::SessionMix{};
    mix.tracks.resize(3);
    mix.midiRoutes.resize(3);
    mix.routing.busCount = 4;
    mix.routing.busDb[3] = -4.5;
    mix.tracks[2].outputBus = 3;
    mix.outputEffects[ostfstudio::busOutput(3)].add("VST3|Test|Bus D|23");
    mix.routing.masterDb = -6.0;
    mix.routing.returnCount = 2;
    mix.routing.returnDb[1] = -8.0;
    mix.tracks[0].volumeDb = -9.0;
    mix.tracks[0].pan = 0.5f;
    mix.tracks[0].outputBus = 1;
    mix.tracks[0].returnSends[1] = 0.42f;
    mix.midiRoutes[1] = {10, 0};
    if (ostfstudio::writeOstfPreview(*project, ostf).failed()
        || ostfstudio::writeOstfpPreview(*project, mix, ostfp).failed()) return 1;
    std::unique_ptr<ostfstudio::OstfpPreview> scoreOnly, package;
    juce::MemoryInputStream scoreInput(ostf.getData(), ostf.getDataSize(), false);
    juce::MemoryInputStream packageInput(ostfp.getData(), ostfp.getDataSize(), false);
    if (ostfstudio::readOstfPreview(scoreInput, scoreOnly).failed() || scoreOnly->savedMix
        || ostfstudio::readOstfpPreview(packageInput, package).failed() || !package->savedMix
        || scoreOnly->sourceScore != package->sourceScore || scoreOnly->pitchedNotes[0].size() != 6
        || scoreOnly->pitchedNotes[1].size() != 2 || scoreOnly->pitchedNotes[1][1].velocity != 77) return 1;
    const auto& saved = *package->savedMix;
    if (saved.routing.busCount != 4 || saved.routing.busDb[3] != -4.5 || saved.tracks[2].outputBus != 3
        || saved.outputEffects[ostfstudio::busOutput(3)] != mix.outputEffects[ostfstudio::busOutput(3)]) return 1;
    if (saved.tracks[0].volumeDb != -9.0 || saved.tracks[0].pan != 0.5f || saved.tracks[0].outputBus != 1
        || std::abs(saved.tracks[0].returnSends[1] - 0.42f) > 0.001f || saved.routing.masterDb != -6.0
        || saved.routing.returnCount != 2 || saved.routing.returnDb[1] != -8.0
        || saved.midiRoutes[1].channel != 10 || saved.midiRoutes[1].destination != 0) return 1;
    mix.insertIds[0] = "VST3|Studio test|Stereo effect|123";
    mix.outputEffects[1].add("VST3|Studio test|Bus effect|456");
    mix.trackEffects.resize(3);
    mix.trackEffects[0].add("VST3|Studio test|First effect|789");
    mix.trackEffects[0].add("VST3|Studio test|Second effect|101");
    mix.routing.outputMuted[1] = true;
    juce::TemporaryFile destination(".ostf");
    juce::MemoryOutputStream wav;
    wav.write("RIFF", 4);
    wav.writeInt(40);
    wav.write("WAVEfmt ", 8);
    wav.writeInt(16);
    wav.writeShort(1);
    wav.writeShort(1);
    wav.writeInt(48000);
    wav.writeInt(96000);
    wav.writeShort(2);
    wav.writeShort(16);
    wav.write("data", 4);
    wav.writeInt(4);
    wav.writeInt(0);
    project->audio.push_back({0, "test.wav", {}, wav.getMemoryBlock()});
    juce::MemoryOutputStream withMedia, bundledMedia;
    const auto scoreFile = destination.getFile();
    if (ostfstudio::writeOstfPreview(*project, mix, scoreFile, withMedia).failed()
        || ostfstudio::writeOstfpPreview(*project, mix, bundledMedia).failed()) return 1;
    juce::MemoryInputStream externalInput(withMedia.getData(), withMedia.getDataSize(), false);
    juce::MemoryInputStream bundledInput(bundledMedia.getData(), bundledMedia.getDataSize(), false);
    std::unique_ptr<ostfstudio::OstfpPreview> external, bundled;
    if (ostfstudio::readOstfPreview(externalInput, external).failed()
        || ostfstudio::readOstfpPreview(bundledInput, bundled).failed()
        || !external->savedMix || !bundled->savedMix
        || external->savedMix->insertIds != mix.insertIds || bundled->savedMix->insertIds != mix.insertIds
        || external->savedMix->outputEffects != mix.outputEffects || bundled->savedMix->outputEffects != mix.outputEffects
        || external->savedMix->trackEffects != mix.trackEffects || bundled->savedMix->trackEffects != mix.trackEffects
        || !external->savedMix->routing.outputMuted[1] || !bundled->savedMix->routing.outputMuted[1]
        || external->audio.size() != 1 || bundled->audio.size() != 1
        || bundled->audio.front().embedded != wav.getMemoryBlock()) return 1;
    const auto relativePath = external->audio.front().path;
    if (!relativePath.startsWith(scoreFile.getFileNameWithoutExtension() + ".media/")) return 1;
    const auto copiedWav = scoreFile.getSiblingFile(relativePath);
    juce::MemoryBlock copiedBytes;
    if (!copiedWav.loadFileAsData(copiedBytes) || copiedBytes != wav.getMemoryBlock()) return 1;
    if (!copiedWav.deleteFile() || !copiedWav.getParentDirectory().deleteFile()) return 1;
    using ostfstudio::TrackKind;
    std::unique_ptr<ostfstudio::OstfpPreview> withPitched, drums, audio, removed;
    if (project->editTracks(TrackKind::pitched, {}, withPitched).failed()
        || withPitched->pitchedNotes[0].size() != project->pitchedNotes[0].size()
        || withPitched->editTracks(TrackKind::drum, {}, drums).failed()
        || drums->editTracks(TrackKind::audio, {}, audio).failed()
        || audio->trackKinds[3] != TrackKind::pitched || audio->trackKinds[4] != TrackKind::drum
        || audio->trackKinds[5] != TrackKind::audio || audio->insertGeneratorPattern(4, 1)
        || audio->editTracks(TrackKind::pitched, 3, removed).failed()
        || removed->trackKinds[3] != TrackKind::drum || removed->trackKinds[4] != TrackKind::audio
        || removed->audio.size() != 1 || removed->audio[0].track != 0) return 1;
    std::unique_ptr<ostfstudio::OstfpPreview> melodyProject, chorus, copied;
    if (project->editTracks(TrackKind::pitched, {}, melodyProject, {}, ostfstudio::MixerTrackGroup::melody).failed()
        || !melodyProject->isGlobalNoteTrack(3) || melodyProject->mixerGroups[3] != ostfstudio::MixerTrackGroup::melody
        || melodyProject->editTracks(TrackKind::pitched, {}, chorus, {}, ostfstudio::MixerTrackGroup::vocal).failed()
        || chorus->mixerGroups[4] != ostfstudio::MixerTrackGroup::vocal
        || chorus->editTracks(TrackKind::pitched, {}, copied, 0).failed()
        || copied->audio.back().track != 5 || copied->audio.back().embedded != project->audio[0].embedded
        || copied->pitchedNotes[5].size() != project->pitchedNotes[0].size()) return 1;
    const auto copyJson = juce::JSON::parse(copied->sourceScore.toString());
    auto originalTrack = copyJson["tracks"][0], copy = copyJson["tracks"][5];
    if (originalTrack["id"] == copy["id"] || originalTrack["clips"][0]["id"] == copy["clips"][0]["id"]) return 1;
    while (copied->sourceTrackCount() < ostfstudio::OstfpPreview::maxTracks) {
        std::unique_ptr<ostfstudio::OstfpPreview> next;
        if (copied->editTracks(TrackKind::pitched, {}, next).failed()) return 1;
        copied.swap(next);
    }
    std::unique_ptr<ostfstudio::OstfpPreview> overflow;
    if (copied->sourceTrackCount() != 15 || copied->editTracks(TrackKind::pitched, {}, overflow).wasOk() || overflow) return 1;
    removed->audio.clear();
    mix.tracks.resize(5);
    mix.audioTracks = {false, false, false, false, true};
    juce::MemoryOutputStream edited;
    if (ostfstudio::writeOstfPreview(*removed, mix, edited).failed()) return 1;
    juce::MemoryInputStream editedInput(edited.getData(), edited.getDataSize(), false);
    std::unique_ptr<ostfstudio::OstfpPreview> reopened;
    if (ostfstudio::readOstfPreview(editedInput, reopened).failed()
        || reopened->trackKinds[3] != TrackKind::drum || reopened->trackKinds[4] != TrackKind::audio) return 1;
    if (argc == 3) {
        const auto directory = juce::File::createFileWithoutCheckingPath(argv[2]);
        if (!directory.getChildFile("edited-tracks.ostf").replaceWithData(edited.getData(), edited.getDataSize())) return 1;
        if (!directory.getChildFile("roundtrip.ostf").replaceWithData(ostf.getData(), ostf.getDataSize())
            || !directory.getChildFile("roundtrip.ostfp").replaceWithData(ostfp.getData(), ostfp.getDataSize())) return 1;
    }
}
