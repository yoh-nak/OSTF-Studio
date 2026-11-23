#include "ui/MainComponent.h"

using namespace ostfstudio;

namespace {
int modelChecks() {
    // C major, mid-measure ii-V-I, F held over V-I. Scales and keys have independent boundaries.
    auto score = juce::JSON::parse(R"({
      "song":{"key":{"state":"tonal","tonic":{"step":"C"},"mode":"major"}},
      "tracks":[{"id":"solo"},{"id":"keys"}],
      "harmony":{"chords":[
        {"id":"a","label":"Dm7","root":{"step":"D"},"intervals":[0,3,7,10],"start":0,"duration":10},
        {"id":"b","label":"G7","root":{"step":"G"},"intervals":[0,4,7,10],"start":10,"duration":10},
        {"id":"c","label":"Cmaj7","root":{"step":"C"},"intervals":[0,4,7,11],"start":20,"duration":10},
        {"id":"d","label":"open","start":35,"duration":5}],
      "scales":[{"id":"major","tonic":{"step":"C"},"intervals":[0,2,4,5,7,9,11],"start":0,"duration":30},
        {"id":"local","tonic":{"step":"A"},"intervals":[0,3,5,7,10],"start":22,"duration":2,
         "targets":[{"type":"track","id":"solo"}]}]}})");
    TheoryGuide guide(score, 40);
    if (guide.functions.size() != 4 || guide.progressions.size() != 1 || guide.progressions[0].label != "ii-V-I  /  V-I"
        || guide.functions[0].function != "Subdominant" || guide.functions[1].function != "Dominant"
        || guide.functions[2].function != "Tonic" || guide.functions[3].degree.isNotEmpty()) return 1;
    std::vector<PitchedNote> notes {{16, 24, 65}, {24, 28, 64}, {20, 30, 60, {}, false}, {20, 30, 60, {}, true, "switch", true}};
    const auto fit = guide.analyse(0, notes), other = guide.analyse(1, notes);
    const auto v = guide.firstCell(16), tonic = guide.firstCell(20), solo = guide.firstCell(22);
    if (fit[v].chord.percent() != 100 || fit[tonic].chord.percent() != 50 || fit[tonic].chord.total != 8
        || fit[tonic].scale.percent() != 100 || fit[v].scale.percent() != 100 || fit[solo].backgroundScale != -2
        || fit[solo].scale.percent() != -1 || other[solo].scale.percent() != 100) return 2;
    const auto local = guide.analyse(0, notes, 1);
    if (local[solo].scale.percent() != 0 || local[v].scale.percent() != -1 || local[solo].chord.percent() != 50) return 3;
    if (guide.chordMask(guide.cells[guide.firstCell(32)]) || guide.chordMask(guide.cells[guide.firstCell(36)])
        || fit[0].chord.percent() != -1) return 4;

    auto& chords = *score["harmony"]["chords"].getArray();
    chords[1].getDynamicObject()->setProperty("start", 11);
    chords[1].getDynamicObject()->setProperty("duration", 9);
    const TheoryGuide gap(score, 40);
    if (gap.progressions.size() != 1 || gap.progressions.front().label != "V-I") return 5;
    score.getDynamicObject()->setProperty("timeline", juce::JSON::parse(R"({"keyChanges":[
        {"at":20,"key":{"state":"tonal","tonic":{"step":"F"},"mode":"major"}}]})"));
    if (!TheoryGuide(score, 40).progressions.empty()) return 6;

    auto scale = (*score["harmony"]["scales"].getArray())[1];
    for (auto type : {"note", "clip", "chord", "section"}) {
        const juce::String kind(type);
        const juce::String id = kind == "chord" ? "c" : "target";
        auto target = juce::JSON::parse("{\"type\":\"" + kind + "\",\"id\":\"" + id + "\"}");
        scale.getDynamicObject()->setProperty("targets", juce::Array<juce::var>{target});
        score["timeline"].getDynamicObject()->setProperty("sections", juce::JSON::parse(R"([{"id":"target","start":22,"duration":1}])"));
        TheoryGuide targeted(score, 40);
        const std::vector<PitchedNote> candidates {{22,24,65,{},true,"target",false,"target"}, {22,24,69,{},true,"else",false,"else"}};
        const auto data = targeted.analyse(0,candidates,1);
        const auto& bucket = data[targeted.firstCell(22)];
        if (bucket.scale.percent() != (kind == "note" || kind == "clip" ? 0 : 50)) return 7;
        if (kind == "section" && data[targeted.firstCell(23)].scale.percent() != -1) return 8;
    }
    auto minor = juce::JSON::parse(R"({
      "song":{"key":{"state":"tonal","tonic":{"step":"A"},"mode":"minor"}},
      "harmony":{"chords":[
        {"id":"a","root":{"step":"B"},"intervals":[0,3,6,10],"start":0,"duration":1},
        {"id":"b","root":{"step":"E"},"intervals":[0,4,7,10],"start":1,"duration":1},
        {"id":"c","root":{"step":"A"},"intervals":[0,3,7],"start":2,"duration":1}]},
      "tracks":[{"id":"guitar","directions":[{"id":"solo","type":"improvisation","start":1,"duration":1,
        "parameters":{"pitchSource":{"type":"explicitScale","scale":{"tonic":{"step":"A"},"intervals":[0,3,5,7,10]}}}}]}]})");
    TheoryGuide cadence(minor, 3);
    if (cadence.progressions.size() != 1 || !cadence.progressions[0].label.endsWith("V-i")) return 9;
    const std::vector<PitchedNote> soloNotes {{0,3,61}};
    if (cadence.analyse(0, soloNotes)[1].scale.percent() != 0 || cadence.analyse(1, soloNotes)[1].scale.percent() != -1) return 21;
    minor["song"]["key"].getDynamicObject()->setProperty("mode", "dorian");
    if (!TheoryGuide(minor, 3).progressions.empty()) return 22;
    return 0;
}

struct Command { int id; bool checked; };
Command find(juce::MenuBarModel& model, const juce::String& label) {
    const auto menu = model.getMenuForIndex(1, TRANS("View"));
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next()) if (it.getItem().text == label) return {it.getItem().itemID, it.getItem().isTicked};
    return {};
}
}

int checkTheoryGuide(const juce::File& demo) {
    if (int failed = modelChecks()) return failed;
    juce::FileInputStream file(demo);
    std::unique_ptr<OstfpPreview> project;
    if (readOstfPreview(file, project).failed()) return 10;
    TheoryGuide guide(juce::JSON::parse(project->sourceScore.toString()), project->playbackEndTick());
    auto keys = std::find(guide.trackIds.begin(), guide.trackIds.end(), "keys") - guide.trackIds.begin();
    if (keys >= static_cast<std::ptrdiff_t>(guide.trackIds.size()) || !project->chordVoicingTrack) return 11;
    const auto at = guide.firstCell(13.0 * project->ticksPerBar);
    const auto band = guide.analyse(keys, project->pitchedNotes[keys])[at];
    if (band.chord.total != 13176 || band.chord.percent() != 100 || band.scale.inside != 9904 || band.scale.percent() != 75) return 12;
    const auto voicing = *project->chordVoicingTrack;
    if (guide.analyse(voicing, project->pitchedNotes[voicing])[at].chord.total <= 0) return 13;

    MainComponent main;
    main.setVisible(true); main.setSize(1440, 1000);
    if (!main.openDocument(demo)) return 14;
    auto* grid = dynamic_cast<PianoNoteGrid*>(main.findChildWithID("piano-note-grid"));
    juce::MenuBarModel* model = nullptr;
    for (auto* child : main.getChildren()) if (auto* bar = dynamic_cast<juce::MenuBarComponent*>(child)) model = bar->getModel();
    if (!grid || !model) return 15;
    auto set = [&](const char* text, bool enabled) {
        const auto cmd = find(*model, juce::translate(text));
        if (cmd.id && cmd.checked != enabled) model->menuItemSelected(cmd.id, 1);
        return cmd.id != 0;
    };
    const auto show = find(*model, TRANS("Show Theory Guide")), chord = find(*model, TRANS("Chord tone fit")),
               scale = find(*model, TRANS("Scale fit"));
    juce::ScopeGuard restore([&] {
        set("Show Theory Guide", true); set("Chord tone fit", chord.checked);
        set("Scale fit", scale.checked); set("Show Theory Guide", show.checked);
    });
    if (!set("Show Theory Guide", false)) return 16;
    const int normal = grid->getHeight();
    set("Show Theory Guide", true); set("Chord tone fit", true); set("Scale fit", true);
    const int both = grid->getHeight();
    if (both <= 0 || both >= normal) return 17;
    const auto snapshot = main.createComponentSnapshot(main.getLocalBounds());
    int chordPixels = 0;
    for (int y = grid->getY(); y < grid->getBottom(); ++y)
        for (int x = grid->getX(); x < grid->getRight(); ++x)
            if (snapshot.getPixelAt(x,y).getARGB() == 0xff60c9b4) ++chordPixels;
    if (!chordPixels) return 18;
    set("Scale fit", false);
    if (grid->getHeight() <= both || !find(*model, TRANS("Chord tone fit")).checked) return 19;
    set("Chord tone fit", false); set("Scale fit", true);
    if (grid->getHeight() <= both || find(*model, TRANS("Chord tone fit")).checked) return 20;
    return 0;
}
