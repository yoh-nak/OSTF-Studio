#include "ui/OstfpPreview.h"
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    juce::FileInputStream input(juce::File::createFileWithoutCheckingPath(argv[1]));
    std::unique_ptr<ostfstudio::OstfpPreview> project;
    const auto result = ostfstudio::readOstfpPreview(input, project);
    if (result.failed()) {
        std::cerr << result.getErrorMessage() << '\n';
        return 1;
    }
    const auto view = project->view();
    if (view.bars.endBar != 17 || view.globalLanes.size() != 5 || view.trackLanes.size() != 3) return 1;
    if (std::string_view(view.globalLanes[0].regions[0].label) != "Intro"
        || view.globalLanes[1].regions.size() != 16
        || std::string_view(view.trackLanes[0].label) != "Lead Guitar"
        || view.trackLanes[0].regions[0].bars.firstBar != 11
        || view.trackLanes[0].regions[0].bars.endBar != 15) return 1;

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
}
