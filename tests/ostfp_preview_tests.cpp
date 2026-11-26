#include "ui/AudioModelPitches.h"
#include "ui/AudioPitchAnalysis.h"
#include "ui/GlobalTimelineEdit.h"
#include "ui/OstfpPreview.h"
#include "ui/AudioRegionEdits.h"
#include "StockEffects.h"
#include "ui/SongMidi.h"
#include "ui/StudioLanguage.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <iostream>
#include <string_view>

static int checkGallery(const juce::File& directory) {
    for (bool packed : {false, true}) {
        juce::FileInputStream input(directory.getChildFile(packed ? "blue-hour-waltz-shared-gm.ostfp" : "blue-hour-waltz-shared-gm.ostf"));
        std::unique_ptr<ostfstudio::OstfpPreview> shared;
        auto opened = packed ? ostfstudio::readOstfpPreview(input, shared) : ostfstudio::readOstfPreview(input, shared);
        if (opened.failed() || !shared->savedMix || shared->sourceTrackCount() != 7) return 94;
        const auto& mix = *shared->savedMix;
        const int owner = shared->trackIds.indexOf("keys");
        if (owner < 0) return 95;
        juce::MemoryBlock beforeColour;
        if(shared->setTrackColour(size_t(owner),0xff7351a0,beforeColour).failed() || beforeColour.isEmpty())return 151;
        juce::MemoryOutputStream written;
        auto saved = packed ? ostfstudio::writeOstfpPreview(*shared, mix, written) : ostfstudio::writeOstfPreview(*shared, mix, written);
        if (saved.failed()) return 96;
        juce::MemoryInputStream bytes(written.getData(), written.getDataSize(), false);
        std::unique_ptr<ostfstudio::OstfpPreview> reopened;
        auto again = packed ? ostfstudio::readOstfpPreview(bytes, reopened) : ostfstudio::readOstfPreview(bytes, reopened);
        if (again.failed() || !reopened->savedMix) return 97;
        if(reopened->trackColour(size_t(owner))!=0xff7351a0) return 152;
        for (const auto* song : {shared.get(), reopened.get()}) {
            const auto& session = *song->savedMix;
            for (size_t i = 0; i < song->sourceTrackCount(); ++i) {
                const auto& route = session.midiRoutes[i];
                if (route.channel != song->sourceMidiChannels[i]) return 98;
                if (i == size_t(owner)) {
                    if (route.destination || session.instruments[i]["name"].toString() != "OSTF GM Sound Module") return 98;
                } else if (route.destination != size_t(owner) || session.instruments[i].isObject()) return 98;
            }
        }
        // Two instances of the very same VST3 retain independent destinations.
        auto twoGroups = mix;
        const size_t second = owner == 0 ? 1 : 0;
        const size_t member = 6;
        twoGroups.instruments[second] = mix.instruments[size_t(owner)];
        twoGroups.midiRoutes[second].destination.reset();
        twoGroups.midiRoutes[member].destination = second;
        juce::MemoryOutputStream two;
        if ((packed ? ostfstudio::writeOstfpPreview(*shared, twoGroups, two) : ostfstudio::writeOstfPreview(*shared, twoGroups, two)).failed()) return 130;
        juce::MemoryInputStream inputTwo(two.getData(), two.getDataSize(), false);
        std::unique_ptr<ostfstudio::OstfpPreview> restored;
        if ((packed ? ostfstudio::readOstfpPreview(inputTwo, restored) : ostfstudio::readOstfPreview(inputTwo, restored)).failed()
            || !restored->savedMix || restored->savedMix->midiRoutes[member].destination != second
            || restored->savedMix->midiRoutes[second].destination || restored->savedMix->midiRoutes[size_t(owner)].destination
            || restored->savedMix->instruments[second]["name"] != mix.instruments[size_t(owner)]["name"]) return 131;
        const auto bass = shared->trackIds.indexOf("bass");
        juce::MemoryBlock before;
        if (bass < 0 || shared->setPatchAt(bass, 0, 15, {-1,-1,35}, before, true).failed()
            || shared->programChanges[bass].size() != 1 || shared->programChanges[bass].front().program != 35) return 99;
    }
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
    {
        using Project = ostfstudio::OstfpPreview;
        std::unique_ptr<Project> midi;
        if (fresh->editTracks(ostfstudio::TrackKind::pitched, {}, midi).failed()
            || !midi->addSessionClip(0, "Loop test", {{60, 180, 62, 81}})) return 80;
        const juce::String id(midi->trackLanes[0].regions.back().clipId);
        juce::MemoryBlock before;
        auto edit = [&](const juce::String& clip, Project::MidiClipEdit action, juce::int64 tick) {
            const auto result = midi->editMidiClip(0, clip, action, tick, before);
            if (result.failed()) std::cerr << result.getErrorMessage() << '\n';
            return result.wasOk();
        };
        if (!edit(id, Project::MidiClipEdit::split, 120) || midi->pitchedNotes[0].size() != 2) return 81;
        const auto& halves = midi->pitchedNotes[0];
        if (halves[0].endTick != 120 || halves[1].startTick != 120 || halves[1].endTick != 180) return 82;
        if (!edit(id, Project::MidiClipEdit::toggleLoop, 0) || !edit(id, Project::MidiClipEdit::resizeLoop, 210)
            || !edit(id, Project::MidiClipEdit::resizeLoop, 240)) return 83;
        auto match = std::find_if(midi->pitchedNotes[0].begin(), midi->pitchedNotes[0].end(),
            [](const auto& n) { return n.startTick == 180 && n.endTick == 240 && n.pitch == 62; });
        if (match == midi->pitchedNotes[0].end()) return 84;
        if (midi->restoreNoteScore(before).failed() || midi->trackLanes[0].regions[0].loopTicks != 120) return 85;
        const auto vocalId = midi->pitchedNotes[0].front().id;
        ostfstudio::VocalNote lyric{juce::String::fromUTF8("空"), juce::String::fromUTF8("そら"), "scoop", .4};
        const auto result = midi->editVocalNote(0, vocalId, lyric, before);
        if (result.failed() || !midi->lyricTrackIds.contains(midi->trackIds[0])
            || midi->vocalNotes.at(vocalId).pronunciation != lyric.pronunciation) {
            std::cerr << "First sung note: " << result.getErrorMessage() << '\n';
            return 92;
        }
    }
    struct Expectation { const char* file; int bars; int tracks; double bpm; };
    constexpr Expectation cases[] {
        {"neon-platforms.ostfp", 76, 8, 106.0},
        {"blue-hour-waltz.ostfp", 128, 7, 132.0},
        {"relay-circuit.ostfp", 96, 12, 128.0},
        {"paper-lantern-replies.ostfp", 56, 9, 78.0},
        {"offset-bridges.ostfp", 80, 8, 0.0},
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
        if (juce::String(expected.file) == "blue-hour-waltz.ostfp") {
            using ostfstudio::TimelineLane;
            const auto scaleLane = [&] {
                return std::find_if(project->globalLanes.begin(), project->globalLanes.end(),
                                    [](const auto& lane){return lane.kind==TimelineLane::scale;});
            };
            const auto lane = scaleLane();
            if (lane==project->globalLanes.end() || lane==project->globalLanes.begin()
                || std::next(lane)==project->globalLanes.end() || std::prev(lane)->kind!=TimelineLane::key
                || std::next(lane)->kind!=TimelineLane::meter || !lane->regions.empty()) return 131;
            const auto bar=project->ticksPerBar;
            auto source=juce::JSON::parse(project->sourceScore.toString());
            auto range=ostfstudio::emptyScaleRange(source,bar+1,bar,project->durationTicks);
            if (range!=juce::Range<juce::int64>(bar,2*bar)) return 132;
            auto scale=juce::JSON::parse(R"({"id":"dorian-test","tonic":{"step":"D","alter":0},"intervals":[0,2,3,5,7,9,10]})");
            scale.getDynamicObject()->setProperty("start",bar+120);
            scale.getDynamicObject()->setProperty("duration",bar-240);
            juce::MemoryBlock before;
            if (project->changeGlobalEvents(TimelineLane::scale,{}, {scale},before).failed()
                || project->contextAt(TimelineLane::scale,bar+120)!="dorian-test") return 133;
            source=juce::JSON::parse(project->sourceScore.toString());
            auto songScale=scale.clone();
            songScale.getDynamicObject()->removeProperty("start");
            songScale.getDynamicObject()->removeProperty("duration");
            songScale.getDynamicObject()->setProperty("id","reference-scale");
            source["song"].getDynamicObject()->setProperty("scale",songScale);
            const auto withDefault=juce::JSON::toString(source);
            juce::MemoryInputStream input(withDefault.toRawUTF8(),withDefault.getNumBytesAsUTF8(),false);
            std::unique_ptr<ostfstudio::OstfpPreview> inherited;
            if (ostfstudio::readOstfPreview(input,inherited).failed()) return 135;
            const auto row=std::find_if(inherited->globalLanes.begin(),inherited->globalLanes.end(),
                [](const auto& item){return item.kind==TimelineLane::scale;});
            if (row==inherited->globalLanes.end() || row->regions.size()!=3
                || juce::String(row->regions.front().label)!="reference-scale" || juce::String(row->regions.back().label)!="reference-scale"
                || inherited->contextAt(TimelineLane::scale,bar+120)!="dorian-test") return 136;
            const auto verifyBoundary = [&](bool split, bool keepRight) {
                juce::Array<juce::var> removed,added;
                juce::MemoryBlock undo;
                if (ostfstudio::globalBoundaryEdit(source,TimelineLane::scale,bar,inherited->durationTicks,
                        split,keepRight,removed,added).failed()
                    || inherited->changeGlobalEvents(TimelineLane::scale,removed,added,undo).failed()) return false;
                const auto beforeOverride=inherited->contextAt(TimelineLane::scale,bar);
                const auto duringOverride=inherited->contextAt(TimelineLane::scale,bar+120);
                return beforeOverride==(keepRight?"dorian-test":"reference-scale")
                    && duringOverride==((split||keepRight)?"dorian-test":"reference-scale")
                    && inherited->restoreNoteScore(undo).wasOk();
            };
            if (!verifyBoundary(true,false) || !verifyBoundary(false,false) || !verifyBoundary(false,true)) return 137;
            if (ostfstudio::emptyScaleRange(source,bar+119,bar,project->durationTicks)!=juce::Range<juce::int64>(bar,bar+120)
                || !ostfstudio::emptyScaleRange(source,bar+120,bar,project->durationTicks).isEmpty()
                || ostfstudio::emptyScaleRange(source,2*bar-120,bar,project->durationTicks)!=juce::Range<juce::int64>(2*bar-120,2*bar)
                || project->restoreNoteScore(before).failed() || scaleLane()==project->globalLanes.end()
                || !scaleLane()->regions.empty()) return 134;
        }
        if (std::string_view(expected.file) == "paper-lantern-replies.ostfp") {
            if (project->lyricTrackIds.size()!=2 || project->vocalNotes.size()!=204 || project->primaryMelodyTrack!=0) return 86;
            const auto harmony = std::find_if(project->globalLanes.begin(), project->globalLanes.end(),
                [](const auto& lane) { return lane.kind == ostfstudio::TimelineLane::chord; });
            if (harmony == project->globalLanes.end()) return 91;
            const auto matchesChords = [&](const auto& lane) {
                if (lane.regions.size() != harmony->regions.size()) return false;
                for (size_t i = 0; i < lane.regions.size(); ++i) {
                    const auto a = lane.regions[i].exactBars, b = harmony->regions[i].exactBars;
                    if (!a || !b || a->start != b->start || a->end != b->end) return false;
                }
                return true;
            };
            const auto& leadLanes = project->partLyricLanes.at(0);
            for (auto kind : {ostfstudio::TimelineLane::displayLyrics, ostfstudio::TimelineLane::pronunciation}) {
                const auto lane = std::find_if(leadLanes.begin(), leadLanes.end(),
                    [kind](const auto& entry) { return entry.kind == kind; });
                const bool sheet = kind == ostfstudio::TimelineLane::displayLyrics;
                if (lane == leadLanes.end() || (sheet ? !matchesChords(*lane) : lane->regions.size() != 194)) return 91;
                const auto first = std::find_if(lane->regions.begin(), lane->regions.end(), [](const auto& cell) { return cell.label[0] != 0; });
                if (first == lane->regions.end() || first->exactBars->start != (sheet ? 5.0 : 5.125)
                    || juce::String::fromUTF8(first->label) != juce::String::fromUTF8(sheet ? "雨の音が" : "あ")) return 91;
            }
            const bool printedLine=std::any_of(leadLanes.front().regions.begin(),leadLanes.front().regions.end(),[](const auto& cell) {
                return juce::String::fromUTF8(cell.label)==juce::String::fromUTF8("If the street lights all go out");
            });
            if (!printedLine) return 91;
            const auto& backingLanes = project->partLyricLanes.at(project->trackIds.indexOf("backing"));
            if (backingLanes.size() != 2 || backingLanes[0].kind != ostfstudio::TimelineLane::displayLyrics
                || backingLanes[1].kind != ostfstudio::TimelineLane::pronunciation
                || !matchesChords(backingLanes[0]) || backingLanes[1].regions.size() != 10) return 92;
            for (const auto& reading : backingLanes[1].regions) {
                const auto at = reading.exactBars->start;
                const auto cells = backingLanes[0].regions;
                const auto display = std::find_if(cells.begin(), cells.end(), [at](const auto& cell) {
                    return cell.exactBars->start <= at && at < cell.exactBars->end;
                });
                if (display == cells.end() || juce::String::fromUTF8(display->label) != "Ah"
                    || juce::String::fromUTF8(reading.label) != juce::String::fromUTF8("アー")
                    || display->exactBars->start >= at) return 92;
            }
            const auto id = project->pitchedNotes[0].front().id;
            const auto original = project->vocalNotes.at(id);
            auto changed = original;
            changed.text = juce::String::fromUTF8("灯");
            changed.pronunciation = juce::String::fromUTF8("ひ");
            changed.technique = "vibrato"; changed.intensity = .75;
            juce::MemoryBlock before;
            if (project->editVocalNote(0,id,changed,before).failed()) return 87;
            const auto& current = project->vocalNotes.at(id);
            if (project->displayPhrases.front().text != juce::String::fromUTF8("雨の音が")
                || current.text != changed.text || current.pronunciation != changed.pronunciation
                || current.technique != changed.technique || current.intensity != .75) return 88;
            juce::MemoryOutputStream out;
            if (writeOstfpPreview(*project,*project->savedMix,out).failed()) return 89;
            juce::MemoryInputStream saved(out.getData(),out.getDataSize(),false);
            std::unique_ptr<ostfstudio::OstfpPreview> reloaded;
            if (readOstfpPreview(saved,reloaded).failed() || reloaded->vocalNotes.at(id).pronunciation != changed.pronunciation
                || project->restoreNoteScore(before).failed() || project->vocalNotes.at(id).text != original.text) return 90;
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
                || authored.outputEffects[1] != juce::StringArray{ostfstudio::stockEffects[14].chainId()}
                || authored.outputEffects[3] != juce::StringArray{ostfstudio::stockEffects[0].chainId()}) return 1;
            std::array<int, 2> members{};
            int roomSends = 0;
            for (const auto& strip : authored.tracks) {
                if (strip.outputBus >= 0) ++members.at(static_cast<std::size_t>(strip.outputBus));
                if (strip.returnSends[0] > 0 && strip.returnSends[0] < 1) ++roomSends;
            }
            const bool twoGmDemo = std::string_view(expected.file) == "neon-platforms.ostfp";
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
                juce::String expectedInstrument = "OSTF GM Sound Module";
                if (std::string_view(expected.file) == "first-light-vector.ostfp") {
                    const auto id = project->trackIds[int(track)];
                    if (id == "arp") expectedInstrument = "OSTF FM Synth";
                    if (id == "sub") expectedInstrument = "OSTF Analog Synth";
                    if (id == "drums") expectedInstrument = "OSTF Drum Machine";
                    if (id == "pad") expectedInstrument = "OSTF Wavetable Synth";
                    if (expectedInstrument != "OSTF GM Sound Module" && (events.size() != 2 || events.back().program != 2)) return 1;
                }
                const auto& part = project->trackIds[int(track)];
                const std::string_view file(expected.file);
                if ((twoGmDemo && part == "keys") || (file == "blue-hour-waltz.ostfp" && part == "vibes")
                    || (file == "paper-lantern-replies.ostfp" && (part == "keys" || part == "answer"))
                    || (file == "copper-mile.ostfp" && part == "organ") || (file == "offset-bridges.ostfp" && part == "pulse")
                    || (file == "relay-circuit.ostfp" && part == "arp")) expectedInstrument = "OSTF FM Synth";
                if ((twoGmDemo && part == "pad") || (file == "relay-circuit.ostfp" && (part == "atmos" || part == "chord"))
                    || ((file == "paper-lantern-replies.ostfp" || file == "offset-bridges.ostfp") && part == "strings")) expectedInstrument = "OSTF Wavetable Synth";
                if (file == "relay-circuit.ostfp" && part == "sub") expectedInstrument = "OSTF Analog Synth";
                if ((twoGmDemo || file == "relay-circuit.ostfp") && part == "drums") expectedInstrument = "OSTF Drum Machine";
                if (twoGmDemo) {
                    const auto owner = part == "comp" ? size_t(project->trackIds.indexOf("bass"))
                        : (part == "brass" || part == "strings") ? size_t(project->trackIds.indexOf("hook")) : track;
                    const auto& route = authored.midiRoutes[track];
                    const auto& plain = fromScore->savedMix->midiRoutes[track];
                    if (route.destination.value_or(track) != owner || route.channel != project->sourceMidiChannels[track]
                        || plain.destination != route.destination || plain.channel != route.channel || events.empty()
                        || plugin.isObject() != (track == owner) || fromScore->savedMix->instruments[track].isObject() != (track == owner)) return 1;
                    if (track != owner) continue;
                }
                if (std::string_view(expected.file) == "paper-lantern-replies.ostfp" &&
                    (project->trackIds[int(track)] == "voice-track" || project->trackIds[int(track)] == "backing"))
                    expectedInstrument = "OSTF Formant Singer";
                if (plugin["format"] != "vst3" || plugin["name"].toString() != expectedInstrument || events.empty()
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
            mix.trackOrder.add(project->trackIds[1]);
            mix.trackOrder.add(project->trackIds[0]);
            mix.tracks.resize(index + 1);
            mix.midiRoutes.resize(index + 1);
            mix.instruments = project->savedMix->instruments;
            mix.tracks[index].pan = 0.35f;
            mix.tracks[index].muted = true;
            mix.stockEffectStates[ostfstudio::SessionMix::effectSlot(true, 3, 0)] = "test-state";
            mix.trackEffects.resize(index + 1);
            mix.trackEffects[index].add(ostfstudio::stockEffects[2].chainId());
            mix.stockEffectStates[ostfstudio::SessionMix::effectSlot(false, index, 0)] = "voicing-state";
            mix.deviceStates[ostfstudio::SessionMix::effectSlot(true,3,0)] = {false,false};
            mix.deviceStates[ostfstudio::SessionMix::effectSlot(false,index,0)] = {true,true};
            for (size_t track=0; track<index; ++track)
                if (mix.instruments[track].isObject()) {
                    mix.deviceStates[ostfstudio::SessionMix::instrumentSlot(track)] = {true,true};
                    break;
                }
            juce::MemoryOutputStream plain;
            juce::TemporaryFile destination("ostf");
            const auto file = destination.getFile();
            const auto savedPlain = ostfstudio::writeOstfPreview(*project, mix, file, plain);
            if (!project->audio.empty()) file.getSiblingFile(file.getFileNameWithoutExtension() + ".media").deleteRecursively();
            if (savedPlain.failed()) return 1;
            juce::MemoryInputStream plainInput(plain.getData(), plain.getDataSize(), false);
            if (ostfstudio::readOstfPreview(plainInput, fromScore).failed() || !fromScore->savedMix
                || fromScore->savedMix->trackOrder != mix.trackOrder
                || fromScore->savedMix->deviceStates != mix.deviceStates
                || fromScore->savedMix->stockEffectStates != mix.stockEffectStates
                || fromScore->savedMix->trackEffects != mix.trackEffects) return 1;
            juce::MemoryOutputStream saved;
            if (ostfstudio::writeOstfpPreview(*project, mix, saved).failed()) return 1;
            juce::MemoryInputStream roundtrip(saved.getData(), saved.getDataSize(), false);
            std::unique_ptr<ostfstudio::OstfpPreview> reopened;
            if (ostfstudio::readOstfpPreview(roundtrip, reopened).failed() || !reopened->savedMix
                || reopened->sourceTrackCount() != index || reopened->sourceScore != project->sourceScore
                || reopened->savedMix->deviceStates != mix.deviceStates
                || reopened->savedMix->instruments.front()["pluginId"] != mix.instruments.front()["pluginId"]
                || reopened->savedMix->tracks.size() != index + 1 || !reopened->savedMix->tracks[index].muted
                || reopened->savedMix->tracks[index].pan != 0.35f
                || reopened->savedMix->trackOrder != mix.trackOrder
                || reopened->savedMix->outputNames != mix.outputNames
                || reopened->savedMix->insertParameters != mix.insertParameters
                || reopened->savedMix->outputEffects != mix.outputEffects
                || reopened->savedMix->stockEffectStates != mix.stockEffectStates) return 1;
            if (std::string_view(expected.file) == "first-light-vector.ostfp") {
                for (const auto* copy : {project.get(), fromScore.get(), reopened.get()}) {
                    const auto& sweep = copy->audio.front();
                    const auto& timing = sweep.timing;
                    if (!timing.loop || timing.cycleTicks != 3840 || sweep.durationTicks != 3 * timing.cycleTicks
                        || timing.markers.size() != 2) return 1;
                    for (int pass = 0; pass < 3; ++pass)
                        if (timing.frameAt(0, (pass + .25) * timing.cycleTicks, 800) != 100
                            || timing.frameAt(0, (pass + .625) * timing.cycleTicks, 800) != 600) return 1;
                }
            }
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
            const auto firstPressureTick = notes.front().startTick + std::min<juce::int64>(200, (notes.front().endTick - notes.front().startTick) / 3);
            if (notes.size() < 160 || project->trackLanes.front().regions.size() != 6
                || project->trackLanes.front().regions.front().bars.firstBar != 5
                || cc == curves.end() || cc->points.size() < 40
                || cc->points.front().tick != notes.front().startTick
                || channel == curves.end() || channel->points.front().tick != firstPressureTick
                || poly == curves.end() || poly->points.front().tick != firstPressureTick
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
        juce::MemoryBlock before;
        if (project->setPatchAt(0, 0, 1, {2, 9, 127}, before).failed()) return 70;
        auto lookup = [&](const char* type, int number) -> const ostfstudio::PerformanceCurve* {
            for (const auto& curve : project->performanceCurves[0]) if (curve.control.type == type && curve.control.number == number) return &curve;
            return nullptr;
        };
        juce::MemoryBlock priorPreset;
        if (project->setPatchAt(0, 0, 1, {-1, -1, 27}, priorPreset, false, true).failed()) return 132;
        for (auto [cc, value] : {std::pair{0, 2}, std::pair{32, 9}}) {
            const auto* curve = lookup("controlChange", cc);
            if (!curve || !std::any_of(curve->points.begin(), curve->points.end(), [value](const auto& p) {
                return p.tick == 0 && std::lround(p.value * 127) == value;
            })) return 133;
        }
        if (project->restoreNoteScore(priorPreset).failed()) return 134;
        auto* patch = lookup("programChange", 0);
        const auto* bank = lookup("controlChange", 32);
        if (!patch || !bank || !std::any_of(bank->points.begin(), bank->points.end(), [](const auto& p) { return p.tick == 0 && std::lround(p.value * 127) == 9; })) return 71;
        juce::String point;
        for (const auto& p : patch->points) if (p.tick == 0 && p.value == 1) point = p.id;
        juce::MemoryBlock prior;
        if (point.isEmpty() || project->editController(0, patch->control, point, 100, 19, prior).failed()) return 72;
        const auto changes = project->programChanges[0];
        if (!std::any_of(changes.begin(), changes.end(), [](const auto& e) { return e.tick == 100 && e.program == 19; })
            || project->restoreNoteScore(before).failed()) return 73;
        if (project->durationTicks != 3360 || project->bars.endBar != 3 || project->pitchedNotes[0].size() != 2
            || project->pitchedNotes[0][0].startTick != 1800 || project->programChanges[0][0].tick != 1800) return 10;
        juce::MemoryOutputStream midi;
        if (writeSongMidi(*project, SmfFormat::multitrack, {}, midi).failed()) return 11;
        // 3.5 cannot declare the 4.0 MIDI stream even though Studio understands it in 4.0.
        for (const auto* unsupported : {"3.5.0", "3.4.0", "4.0.1", "5.1.1"}) {
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

static bool checkMultiClipTake(juce::var score, const juce::String& version) {
    using namespace ostfstudio;
    auto fail=[](int line){ std::cerr<<"Multi-clip take check failed at line "<<line<<"\n"; return false; };
    auto* root=score.getDynamicObject();
    root->setProperty("version",version);
    root->removeProperty("takeGroups");
    auto track=score["tracks"][0];
    const auto templateClip=track["clips"][0];
    auto makeClip=[&](int start,const juce::String& id,int pitch) {
        auto clip=templateClip.clone();
        auto* c=clip.getDynamicObject();
        c->setProperty("id",id);
        c->setProperty("enabled",true);
        c->setProperty("start",start);
        c->setProperty("duration",480);
        auto note=clip["notes"][0];
        note.getDynamicObject()->setProperty("id",id+"-note");
        note.getDynamicObject()->setProperty("pitch",pitch);
        c->setProperty("notes",juce::Array<juce::var>{note});
        return clip;
    };
    auto early=makeClip(240,"early",60), late=makeClip(1920,"late",65);
    track.getDynamicObject()->setProperty("clips",juce::Array<juce::var>{late,early});
    auto open=[&] (std::unique_ptr<OstfpPreview>& project) {
        auto text=juce::JSON::toString(score);
        juce::MemoryInputStream in(text.toRawUTF8(),text.getNumBytesAsUTF8(),false);
        auto result=readOstfPreview(in,project);
        if (result.failed()) std::cerr<<result.getErrorMessage()<<"\n";
        return result.wasOk();
    };
    const juce::Range<juce::int64> ticks(0,3840);
    juce::MemoryBlock before;
    std::unique_ptr<OstfpPreview> project, preview;
    if (!open(project)) return fail(__LINE__);
    const auto added=project->addScoreTake(0,ticks,before);
    if (added.failed()) std::cerr<<added.getErrorMessage()<<"\n";
    if (added.failed() || project->compChoices.size()!=3) return fail(__LINE__);
    const auto original=project->sourceScore;
    auto group=juce::JSON::parse(original.toString())["takeGroups"][0];
    auto active=group["comps"][0];
    if (active["outputClipIds"].size()!=2 || active["outputClipIds"][0]!="early" || active["outputClipIds"][1]!="late"
        || active["compRegions"].size()!=5 || active["compRegions"][2]["kind"]!="silence" || int(active["compRegions"][2]["durationTicks"])!=1200
        || active["noteLinks"].size()!=2 || group["activeCompId"]!=active["id"]) return fail(__LINE__);
    const auto choices=project->compChoices;
    if (project->compPlayback({choices[1]},preview).failed() || !preview->pitchedNotes[0].empty() || original!=project->sourceScore) return fail(__LINE__);
    auto notes=project->pitchedNotes[0];
    if (notes.size()!=2) return fail(__LINE__);
    auto changed=notes.front();
    changed.pitch=72;
    juce::StringArray ids;
    if (project->editNotes(0,{{NoteEdit::change,changed}},before,ids,ticks).failed()) return fail(__LINE__);
    auto edited=juce::JSON::parse(project->sourceScore.toString());
    active=edited["takeGroups"][0]["comps"][0];
    if (active["noteLinks"].size()!=2) return fail(__LINE__);
    for (const auto& link : *active["noteLinks"].getArray()) {
        bool matched=false;
        for (const auto& clip : *edited["tracks"][0]["clips"].getArray()) {
            if (bool(clip["enabled"]) || clip["notes"].size()!=1 || clip["notes"][0]["id"]!=link["sourceNoteId"]) continue;
            const auto outputId=link["outputNoteId"].toString();
            const auto expected=std::find_if(project->pitchedNotes[0].begin(),project->pitchedNotes[0].end(),[&](const auto& n){return n.id==outputId;});
            matched=expected!=project->pitchedNotes[0].end() && expected->pitch==int(clip["notes"][0]["pitch"]);
        }
        if (!matched) return fail(__LINE__);
    }
    if (project->selectScoreComp(choices[1],before).failed() || !project->pitchedNotes[0].empty()
        || project->selectScoreComp(choices[0],before).failed() || project->pitchedNotes[0].size()!=2) return fail(__LINE__);
    late.getDynamicObject()->setProperty("start",480);
    if (!open(project)) return fail(__LINE__);
    const auto untouched=project->sourceScore;
    return project->addScoreTake(0,ticks,before).failed() && project->sourceScore==untouched;
}
static int checkOstf52(const juce::File& fixtures) {
    using namespace ostfstudio;
    const auto files=fixtures.findChildFiles(juce::File::findFiles,false,"*.ostf");
    if (files.size()<5) return 1;
    for (const auto& file : files) {
        juce::FileInputStream in(file); std::unique_ptr<OstfpPreview> p;
        const auto opened=readOstfPreview(in,p);
        if(opened.failed()) {std::cerr<<file.getFileName()<<": "<<opened.getErrorMessage()<<"\n";return 2;}
        if(!p->supportsScoreTakes()) return 3;
        juce::MemoryOutputStream out;
        if(writeOstfPreview(*p,out).failed() || out.toString()!=file.loadFileAsString()) return 4;
        if(file.getFileName().startsWith("31-") && (p->declaredVocalTrackIds.size()!=1 || p->specialTrackNames.size()!=4)) return 5;
        if(file.getFileName().startsWith("03-")) {
            for (const auto version : {"5.1.0","5.2.0"})
                if (!checkMultiClipTake(juce::JSON::parse(file.loadFileAsString()),version)) { std::cerr<<"Multi-clip comp "<<version<<" failed\n"; return 23; }
            juce::MemoryOutputStream packed;
            SessionMix mix;
            mix.tracks.resize(p->trackLanes.size());
            mix.midiRoutes.resize(mix.tracks.size());
            const auto saved = writeOstfpPreview(*p,mix,packed);
            if (saved.failed()) {std::cerr << saved.getErrorMessage() << "\n"; return 15;}
            juce::MemoryInputStream zip(packed.getData(),packed.getDataSize(),false);
            std::unique_ptr<OstfpPreview> unpacked;
            const auto loaded = readOstfpPreview(zip,unpacked);
            if (loaded.failed()) {std::cerr << loaded.getErrorMessage() << "\n"; return 16;}
            if (unpacked->sourceVersion != "5.2.0" || unpacked->compChoices.size() != p->compChoices.size()) return 17;
            if(p->compChoices.empty()) return 6;
            const auto choices=p->compChoices;
            const auto alternative=std::find_if(choices.begin(),choices.end(),[](const auto& c){return !c.active && c.comp.isNotEmpty();});
            if(alternative==choices.end()) return 7;
            juce::MemoryBlock undo;
            if(p->selectScoreComps({*alternative},undo).failed() || p->sourceVersion!="5.2.0") return 8;
        }
    }
    const auto packages = fixtures.findChildFiles(juce::File::findFiles, false, "*.ostfp");
    if (packages.size() < 16) {
        std::cerr << "Expected the 16 official OSTF 5.2 package fixtures in " << fixtures.getFullPathName() << '\n';
        return 1;
    }
    for(const auto& package:packages) {
        juce::FileInputStream in(package); std::unique_ptr<OstfpPreview> project;
        const auto opened=readOstfpPreview(in,project);
        const auto name = package.getFileName();
        if (name.startsWith("04-") || name.startsWith("06-")) {
            const auto expected = name.startsWith("04-") ? "Unsupported sidechain routing" : "Unknown device or unsupported state asset";
            if (opened.getErrorMessage()!=expected || project) return 18;
            continue;
        }
        if(opened.failed()) {std::cerr<<package.getFileName()<<": "<<opened.getErrorMessage()<<"\n";return 10;}
        if(!project->importedProduction) return 11;
        if (name.startsWith("10-audio-")) {
            const auto& source = *project->importedProduction;
            auto manifest = juce::JSON::parse(source.manifestBytes.toString());
            auto session = juce::JSON::parse(source.productionBytes.toString());
            const auto& fx = ostfstudio::stockEffects[0];
            auto device = juce::JSON::parse(R"({"id":"foreign-fx","role":"effect","enabled":true,"bypass":false,"onUnavailable":"reportAndPreserve","plugin":{"format":"vst3","vendor":"OSTF Studio"}})");
            device["plugin"].getDynamicObject()->setProperty("name",fx.displayName());
            device["plugin"].getDynamicObject()->setProperty("pluginId",fx.classId());
            session.getDynamicObject()->setProperty("devices",juce::Array<juce::var>{device});
            session["channels"][0].getDynamicObject()->setProperty("deviceIds",juce::Array<juce::var>{"foreign-fx"});
            auto toBytes=[](const juce::var& value) {
                const auto json=juce::JSON::toString(value);
                return juce::MemoryBlock(json.toRawUTF8(),json.getNumBytesAsUTF8());
            };
            const auto production = toBytes(session);
            auto companion=manifest["documents"][0];
            companion.getDynamicObject()->setProperty("sha256",juce::SHA256(production).toHexString());
            const auto manifestData=toBytes(manifest);
            juce::ZipFile original(package); juce::ZipFile::Builder builder;
            for(int i=0; i<original.getNumEntries(); ++i) {
                const auto path=original.getEntry(i)->filename;
                std::unique_ptr<juce::InputStream> stream(original.createStreamForEntry(i));
                if (path=="manifest.json") stream=std::make_unique<juce::MemoryInputStream>(manifestData,false);
                if (path==companion["path"].toString()) stream=std::make_unique<juce::MemoryInputStream>(production,false);
                builder.addEntry(std::move(stream),path=="mimetype"?0:9,path,{});
            }
            juce::MemoryOutputStream inputZip,editedZip;
            if(!builder.writeToStream(inputZip,nullptr)) return 33;
            juce::MemoryInputStream before(inputZip.getData(), inputZip.getDataSize(), false);
            std::unique_ptr<OstfpPreview> test;
            if(const auto result=readOstfpPreview(before,test); result.failed()) {
                std::cerr << "Read imported device fixture: " << result.getErrorMessage() << '\n'; return 34;
            }
            auto mix=*test->savedMix;
            mix.deviceStates[SessionMix::effectSlot(true,0,0)]={true,true};
            if(const auto result=writeOstfpPreview(*test,mix,editedZip); result.failed()) {
                std::cerr << "Save imported bypass: " << result.getErrorMessage() << '\n'; return 35;
            }
            juce::MemoryInputStream after(editedZip.getData(), editedZip.getDataSize(), false);
            if(readOstfpPreview(after,test).failed() || test->savedMix->deviceStates!=mix.deviceStates) return 36;
            const auto savedDevice=juce::JSON::parse(test->importedProduction->productionBytes.toString())["devices"][0];
            if(savedDevice["id"]!="foreign-fx" || !bool(savedDevice["bypass"])) return 37;
        }
        const auto active=std::count_if(project->audio.begin(),project->audio.end(),[](const auto& audio){return !audio.muted;});
        if(package.getFileName().startsWith("01-") ? active!=0 : active<1) return 12;
        if(package.getFileName().startsWith("02-") && active!=2) return 12;
        juce::MemoryOutputStream copy; const auto saved=writeOstfpPreview(*project,project->savedMix.value_or(SessionMix{}),copy);
        if(saved.failed()){std::cerr<<saved.getErrorMessage()<<"\n";return 13;}
        juce::MemoryInputStream second(copy.getData(),copy.getDataSize(),false); std::unique_ptr<OstfpPreview> reopened;
        if(readOstfpPreview(second,reopened).failed() || reopened->sourceScore!=project->sourceScore
           || reopened->importedProduction->productionBytes!=project->importedProduction->productionBytes
           || reopened->importedProduction->members!=project->importedProduction->members) return 14;
        if (name.startsWith("11-")) {
            AudioModelPitches pitches(juce::JSON::parse(project->importedProduction->productionBytes.toString()));
            if (project->audio.size()!=2) return 24;
            for (const auto& clip : project->audio) {
                const auto expected = clip.regionId=="r-original" ? 440. : 880.;
                if (pitches.frequenciesAt(clip,0)!=std::vector<double>{expected}
                    || pitches.frequenciesAt(clip,47999)!=std::vector<double>{expected}
                    || !pitches.frequenciesAt(clip,48000).empty()) return 25;
                auto replacement=clip;
                replacement.sha256="different-asset";
                if (!pitches.frequenciesAt(replacement,24000).empty()) return 26;
                auto trim=clip;
                trim.startFrame=12000; trim.frameCount=12000;
                if (!pitches.frequenciesAt(trim,11999).empty() || pitches.frequenciesAt(trim,12000).size()!=1
                    || !pitches.frequenciesAt(trim,24000).empty()) return 27;
            }
            auto& clip=project->audio.front();
            const auto base=pitches.frequenciesAt(clip,12000).front();
            const auto guide=analysePitchGuide(clip,std::atomic_bool{false});
            if (guide.empty() || std::abs(guide.front().hz-base)>4.) return 28;
            clip.timing.cycleTicks=clip.durationTicks;
            clip.timing.preservePitch=true;
            clip.timing.markers={{.65,.5}};
            clip.timing.pitches={{0.,.5,3}};
            juce::MemoryOutputStream changed;
            if (const auto result=writeOstfpPreview(*project,*project->savedMix,changed); result.failed()) {
                std::cerr<<result.getErrorMessage()<<"\n"; return 29;
            }
            juce::MemoryInputStream input(changed.getData(),changed.getDataSize(),false);
            if (const auto result=readOstfpPreview(input,reopened); result.failed()) {std::cerr<<result.getErrorMessage()<<"\n";return 30;}
            if (reopened->audio!=project->audio || reopened->importedProduction->members!=project->importedProduction->members) return 31;
            auto audible=reopened->audio.front(); audible.muted=false; audible.gainDb=0;
            AudioRegionPlayback play;
            if (play.load(audible,project->clock,48000,96000).failed()) return 32;
            const int length=play.wavEnd-play.wavStart;
            juce::AudioBuffer<float> sound(2,length); sound.clear();
            play.mixInto(sound,length,play.wavStart,1.,48000,project->clock);
            auto hzAt=[&](double t) {
                const int a=int(t*length),n=length/10;
                int edges=0;
                for(int i=a+1;i<a+n;++i) if(sound.getSample(0,i-1)<=0 && sound.getSample(0,i)>0) ++edges;
                return 48000.*edges/n;
            };
            const auto firstHz=hzAt(.30),lastHz=hzAt(.80);
            std::cout<<"Warped region: "<<length<<" frames, pitched / unchanged intervals: "<<firstHz<<" / "<<lastHz<<" Hz\n";
            if (std::abs(firstHz-base*std::exp2(3./12))>12 || std::abs(lastHz-base)>12) return 33;
            auto info=project->songInformation();
            info.title="Edited score with original audio";
            if (project->applySongInformation(info).failed()) return 19;
            juce::MemoryOutputStream edited;
            if (writeOstfpPreview(*project,*project->savedMix,edited).failed()) return 20;
            juce::MemoryInputStream archive(edited.getData(),edited.getDataSize(),false);
            if (readOstfpPreview(archive,reopened).failed() || reopened->title!=info.title
                || reopened->importedProduction->members!=project->importedProduction->members) return 21;
        }
    }
    auto invalid=juce::JSON::parse(fixtures.getChildFile("10-notation-minimal.ostf"));
    invalid["notation"].getDynamicObject()->setProperty("profileVersion","99.0");
    const auto json=juce::JSON::toString(invalid);
    juce::MemoryInputStream bad(json.toRawUTF8(),json.getNumBytesAsUTF8(),false); std::unique_ptr<OstfpPreview> rejected;
    return readOstfPreview(bad,rejected).failed()?0:9;
}

int checkSynthVExport(const juce::File& root, const juce::File& output);
int main(int argc, char** argv) {
    if (argc==4 && std::string_view(argv[1])=="--svp")
        return checkSynthVExport(juce::File(argv[2]),juce::File(argv[3]));
    if (argc==3 && std::string_view(argv[1])=="--ostf52") return checkOstf52(juce::File::createFileWithoutCheckingPath(argv[2]));
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
    {
        std::unique_ptr<ostfstudio::OstfpPreview> withPart;
        if (project->editTracks(ostfstudio::TrackKind::pitched, {}, withPart, {}, ostfstudio::MixerTrackGroup::pitched, 16).failed())
            return 91;
        const auto added = withPart->sourceTrackCount() - 1;
        if (withPart->sourceMidiChannels[added] != 16 || !withPart->pitchedNotes[added].empty()
            || juce::String::fromUTF8(withPart->trackLanes[added].label) != "MIDI Ch 16") return 92;
        if (project->editTracks(ostfstudio::TrackKind::pitched, {}, withPart, {}, ostfstudio::MixerTrackGroup::pitched, 17).wasOk())
            return 93;
    }
    if (view.bars.endBar != 17 || view.globalLanes.size() != 6 || view.trackLanes.size() != 3
        || project->partLyricLanes.at(0).size() != 2) return 1;
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
    for(const auto& fx:ostfstudio::stockEffects) if(juce::String(fx.name)=="OSTF Unmask") {
        mix.outputEffects[0].add(fx.chainId());
        mix.sidechainTracks[ostfstudio::SessionMix::effectSlot(true,0,0)]=project->trackIds[0];
    }
    for (const auto& fx : ostfstudio::stockEffects) if (fx.displayName()=="Bus Compressor") {
        const int slot = mix.outputEffects[0].size();
        mix.outputEffects[0].add(fx.chainId());
        mix.sidechainTracks[ostfstudio::SessionMix::effectSlot(true,0,slot)] = project->trackIds[1];
    }
    mix.sidechainTracks[ostfstudio::SessionMix::effectSlot(true,ostfstudio::busOutput(3),0)] = project->trackIds[2];
    mix.deviceStates[ostfstudio::SessionMix::effectSlot(true,ostfstudio::busOutput(3),0)] = {true,true};
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
    if (saved.deviceStates != mix.deviceStates) return 1;
    if (saved.sidechainTracks.empty() || saved.sidechainTracks != mix.sidechainTracks) return 1;
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
    project->audio.front().timing = {true, 240, {{0.3, 0.6}}};
    project->audio.front().durationTicks = 960;
    mix.audioTracks.resize(project->sourceTrackCount(), false);
    mix.audioTracks[0] = true;
    mix.captureChannels[project->trackIds[0]] = 2;
    mix.audioInputs[project->trackIds[0]] = {4, -7.5};
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
    for (auto* reopened : {external.get(), bundled.get()}) {
        const auto& route = reopened->savedMix->audioInputs.at(project->trackIds[0]);
        if (route.first != 4 || route.gainDb != -7.5 || reopened->savedMix->captureChannels != mix.captureChannels) return 1;
        const auto& clip = reopened->audio.front();
        if (clip.durationTicks != 960 || !clip.timing.loop || clip.timing.cycleTicks != 240
            || clip.timing.markers.size() != 1 || clip.timing.markers[0].source != 0.6
            || clip.timing.markers[0].position != 0.3) return 1;
    }
    // Two regions may share an embedded source and have independent warp phases.
    auto second = bundled->audio.front();
    second.regionId = "region-" + juce::Uuid().toString();
    second.startTick += second.durationTicks;
    second.timing.phaseOffset = 0.375;
    bundled->audio.push_back(second);
    juce::MemoryOutputStream repeated;
    if (ostfstudio::writeOstfpPreview(*bundled, mix, repeated).failed()) return 1;
    juce::MemoryInputStream repeatIn(repeated.getData(), repeated.getDataSize(), false);
    std::unique_ptr<ostfstudio::OstfpPreview> roundTrip;
    const auto reopenedRegions = ostfstudio::readOstfpPreview(repeatIn, roundTrip);
    if (reopenedRegions.failed() || roundTrip->audio.size() != 2) {
        std::cerr << "Multi-region OSTFP round trip: " << reopenedRegions.getErrorMessage() << '\n';
        return 1;
    }
    const auto& a = roundTrip->audio[0];
    const auto& b = roundTrip->audio[1];
    if (a.regionId != bundled->audio.front().regionId || b.regionId != second.regionId || a.track != b.track
        || b.timing != second.timing || a.embedded.getData() != b.embedded.getData() || a.path != b.path) return 1;
    // Consolidating independent clips keeps both sounds and the gap between them.
    auto constantClip = [&](int tick, juce::uint8 highByte) {
        auto data = wav.getMemoryBlock();
        auto* samples = static_cast<juce::uint8*>(data.getData()) + 44;
        samples[1] = samples[3] = highByte;
        ostfstudio::ProjectAudio region{0, "tone.wav", {}, std::move(data), tick, 8};
        region.timing.cycleTicks = 8;
        return region;
    };
    auto leftTone = constantClip(0, 0x20), rightTone = constantClip(12, 0xc0);
    ostfstudio::ProjectAudio rendered;
    if (ostfstudio::consolidateRegions(leftTone, rightTone, 20, project->clock, rendered).failed()
        || rendered.durationTicks != 20 || rendered.regionId != leftTone.regionId) return 1;
    auto renderReader = ostfstudio::wavReader(rendered.embedded);
    if (!renderReader || renderReader->numChannels != 2) return 1;
    juce::AudioBuffer<float> sample(2, 1);
    for (auto [tick, expected] : {std::pair{4, .25f}, std::pair{10, 0.f}, std::pair{16, -.5f}}) {
        if (!renderReader->read(&sample, 0, 1, std::llround(project->clock.secondsAt(tick) * renderReader->sampleRate), true, true)
            || std::abs(sample.getSample(0, 0) - expected) > .001f) return 1;
    }
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
