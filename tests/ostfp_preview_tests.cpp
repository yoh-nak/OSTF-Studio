#include "ui/OstfpPreview.h"
#include <algorithm>
#include <iostream>
#include <string_view>

static int checkGallery(const juce::File& directory) {
    struct Expectation { const char* file; int bars; int tracks; double bpm; };
    constexpr Expectation cases[] {
        {"neon-platforms.ostfp", 8, 4, 106.0},
        {"blue-hour-waltz.ostfp", 8, 3, 132.0},
        {"relay-circuit.ostfp", 12, 3, 128.0},
        {"paper-lantern-replies.ostfp", 8, 5, 78.0},
        {"offset-bridges.ostfp", 0, 0, 0.0},
    };
    for (const auto& expected : cases) {
        juce::FileInputStream input(directory.getChildFile(expected.file));
        std::unique_ptr<ostfstudio::OstfpPreview> project;
        const auto result = ostfstudio::readOstfpPreview(input, project);
        const bool matches = expected.bars == 0
            ? result.getErrorMessage() == "Preview requires one meter starting at tick zero"
            : result.wasOk() && project->bars.endBar == expected.bars + 1
              && project->trackLanes.size() == static_cast<std::size_t>(expected.tracks)
              && project->previewBpm == expected.bpm && !project->pitchedNotes.front().empty()
              && project->chordVoicings == expected.bars;
        if (!matches) {
            std::cerr << expected.file << ": unexpected preview result: " << result.getErrorMessage() << '\n';
            return 1;
        }
        if (std::string_view(expected.file) == "paper-lantern-replies.ostfp") {
            const auto& notes = project->pitchedNotes.front();
            const auto& curves = project->performanceCurves.front();
            const auto lane = [&](const juce::String& label) {
                return std::find_if(curves.begin(), curves.end(), [&](const auto& curve) { return curve.label == label; });
            };
            const auto cc = lane("CC 11");
            const auto channel = lane("Channel Aftertouch");
            const auto poly = lane("Poly Aftertouch / note 69");
            const auto pitch = lane("Vocal / voice / Pitch (cents)");
            if (notes.front().velocity != 88 || project->trackLanes.front().regions.front().noteCount != 12
                || curves.size() != 9 || cc == curves.end() || cc->points.size() != 2
                || cc->points.front().tick != notes.front().startTick
                || channel == curves.end() || channel->points.front().tick != notes.front().startTick + 720
                || poly == curves.end() || poly->points.front().tick != notes.front().startTick + 480
                || pitch == curves.end() || !pitch->cents || pitch->points.front().value != -30.0
                || project->trackLanes[3].regions.size() != 1 || project->trackLanes[3].regions.front().bars.firstBar != 5)
                return 1;
            for (const auto* parameter : {"dynamics", "breathiness", "tension", "brightness", "voicing"}) {
                const auto expression = lane(juce::String("Vocal / voice / ") + parameter);
                if (expression == curves.end() || expression->points.size() != 3) return 1;
            }
        }
        std::cout << expected.file << (expected.bars == 0 ? ": mixed meter rejected as expected" : ": preview opened") << '\n';
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--gallery")
        return checkGallery(juce::File::createFileWithoutCheckingPath(argv[2]));
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
