#include "../src/plugins/scale/ScaleDocument.h"
#include <iostream>

int main(int argc, char** argv) {
    using namespace ostfstudio::scale;
    if (argc == 4 && juce::String(argv[1]) == "--workspace-import") {
        Document song;
        const auto source = juce::File(juce::String::fromUTF8(argv[2])).loadFileAsString();
        const auto edited = juce::File(juce::String::fromUTF8(argv[3])).loadFileAsString();
        if (song.load(source).failed()) return 17;
        const auto result = song.importWorkspaceScales(edited, digest(source));
        if (result.failed()) std::cerr << result.getErrorMessage() << '\n';
        return result.wasOk() && song.baseHash == digest(source)
            && song.score["harmony"]["scales"].size() == juce::JSON::parse(edited)["harmony"]["scales"].size() ? 0 : 18;
    }
    if (argc != 2) return 1;
    Document doc;
    const auto input = juce::File(juce::String::fromUTF8(argv[1])).loadFileAsString();
    if (doc.load(input).failed() || catalog().size() != 24) return 2;
    const auto original = doc.score.clone();
    const auto count = original["harmony"]["scales"].size();
    const auto check = [](juce::Result r) {
        if (r.failed()) std::cerr << r.getErrorMessage() << '\n';
        return r.wasOk();
    };
    if (!check(doc.assign(1,2,1))) return 3;
    const auto event = doc.score["harmony"]["scales"][count];
    const auto chord = original["harmony"]["chords"][1];
    if (event["label"].toString() != "D Dorian" || event["targets"][0]["type"].toString() != "chord"
        || event["targets"][0]["id"] != chord["id"] || event["start"] != chord["start"] || event["duration"] != chord["duration"]) return 4;
    if (!check(doc.assign(1,3,0)) || !check(doc.assign(0,0,0))) return 5;
    const auto changed = doc.score["harmony"]["scales"];
    if (changed.size() != count+2 || changed[count]["id"] != event["id"] || int(changed[count]["tonic"]["alter"]) != -1) return 6;
    for (int i=0;i<count;++i)
        if (juce::JSON::toString(changed[i]) != juce::JSON::toString(original["harmony"]["scales"][i])) return 7;
    if (doc.baseHash != digest(input)) return 8;
    const auto exported = doc.text;
    if (doc.assign(30000,0,0).wasOk() || doc.load("{}").wasOk() || doc.text!=exported) return 9;
    if (!check(doc.load(exported))) return 10;
    auto restored=doc.score.clone();
    restored["harmony"].getDynamicObject()->setProperty("scales",original["harmony"]["scales"]);
    if (original.hasProperty("document")) restored.getDynamicObject()->setProperty("document",original["document"]);
    if (juce::JSON::toString(original,true,17)!=juce::JSON::toString(restored,true,17)) return 11;
    auto range = doc.scope(3, 4, false);
    auto repeats = doc.scope(3, 4, true);
    if (range != std::vector<int>{3,4} || repeats.size() <= range.size() || doc.scope(1,0,true).size()) return 12;
    if (!check(doc.assign(repeats,9,5))) return 13;
    const auto committed = doc.text;
    if (doc.assign(std::vector<int>{0,30000},9,5).wasOk() || doc.text != committed) return 14;
    Document workspace;
    if(workspace.load(input).failed() || workspace.importWorkspaceScales(committed,"stale").wasOk()) return 15;
    auto scalesOnly=original.clone(); scalesOnly["harmony"].getDynamicObject()->setProperty("scales",doc.score["harmony"]["scales"]);
    if(!check(workspace.importWorkspaceScales(juce::JSON::toString(scalesOnly),digest(input))) || workspace.baseHash!=digest(input)) return 16;
    juce::TemporaryFile copy(".ostf"), other(".txt");
    if (workspace.saveAs(copy.getFile()).failed() || copy.getFile().loadFileAsString()!=workspace.text) return 17;
    if (!other.getFile().replaceWithText("original") || workspace.saveAs(other.getFile()).wasOk()
        || other.getFile().loadFileAsString()!="original") return 18;
    juce::File returned;
    if (workspace.exportForStudio(copy.getFile().getParentDirectory(),returned).failed()) return 19;
    juce::ScopeGuard cleanup([&]{returned.deleteFile();});
    if (!returned.getFileName().startsWith("studio-scales-"+digest(input)+"-") || returned.loadFileAsString()!=workspace.text) return 20;
    return 0;
}
