#include "ui/MainComponent.h"
#include "ui/FitColour.h"

using namespace ostfstudio;

namespace {
int modelChecks() {
    // C major, mid-measure ii-V-I, F held over V-I. Scales and keys have independent boundaries.
    auto score = juce::JSON::parse(R"({
      "song":{"key":{"state":"tonal","tonic":{"step":"C"},"mode":"major"}},
      "tracks":[{"id":"solo","clips":[{"id":"off","enabled":false}]},{"id":"keys"}],
      "harmony":{"chords":[
        {"id":"a","label":"Dm7","root":{"step":"D"},"intervals":[0,3,7,10],"start":0,"duration":10},
        {"id":"b","label":"G7","root":{"step":"G"},"intervals":[0,4,7,10],"start":10,"duration":10},
        {"id":"c","label":"Cmaj7","root":{"step":"C"},"intervals":[0,4,7,11],"start":20,"duration":10},
        {"id":"d","label":"open","start":35,"duration":5}],
      "scales":[{"id":"major","tonic":{"step":"C"},"intervals":[0,2,4,5,7,9,11],"start":0,"duration":30},
        {"id":"local","tonic":{"step":"A"},"intervals":[0,3,5,7,10],"start":22,"duration":2,
         "targets":[{"type":"track","id":"solo"}]}]}})");
    TheoryGuide guide(score, 40);
    TheoryGuide::RegionFits lead, keys;
    guide.analyse(0, {{0,20,62}, {0,5,61}, {22,24,65}, {0,20,61,{},true,"note-in-off-clip",false,"off"},
                      {0,20,61,{},false}, {0,20,61,{},true,{},true}}, -1, nullptr, &lead);
    guide.analyse(1, {{0,5,65}, {22,24,61}}, 1, nullptr, &keys);
    TheoryGuide::Fit ensemble {lead.chords[0].inside + keys.chords[0].inside, lead.chords[0].total + keys.chords[0].total};
    if (ensemble.percent() != 75 || lead.chords[1].total != 10 || lead.chords[1].percent() != 100 ||
        lead.scales[0].total != 27 || lead.scales[0].inside != 22 || keys.scales[0].total != 7 ||
        keys.scales[0].inside != 5 || lead.scales[1].percent() != 0 || keys.scales[1].percent() != -1) return 73;
    auto fallback = score.clone();
    fallback["song"].getDynamicObject()->setProperty("scale", fallback["harmony"]["scales"][0]);
    fallback["harmony"].getDynamicObject()->removeProperty("scales");
    TheoryGuide(fallback, 40).analyse(0, {{0,8,60}, {0,2,61}}, -1, nullptr, &lead);
    if (lead.scales.size() != 1 || lead.scales[0].percent() != 80 || lead.chords[1].percent() != -1) return 74;
    if (guide.functions.size() != 4 || guide.progressions.size() != 1 || guide.progressions[0].label != "ii-V-I"
        || guide.functions[0].function != "Subdominant" || guide.functions[1].function != "Dominant"
        || guide.functions[2].function != "Tonic" || guide.functions[3].degree.isNotEmpty()) return 1;
    std::vector<PitchedNote> notes {{16, 24, 65}, {24, 28, 64}, {20, 30, 60, {}, false}, {20, 30, 60, {}, true, "switch", true}};
    notes[0].clipId="sustain"; notes[1].clipId="answer"; notes[2].clipId="muted";
    TheoryGuide::ClipFits badges;
    const auto fit = guide.analyse(0, notes, -1, &badges), other = guide.analyse(1, notes);
    const auto v = guide.firstCell(16), tonic = guide.firstCell(20), solo = guide.firstCell(22);
    if (fit[tonic].localChord.percent() != 0 || fit[solo].localChord.percent() != 0
        || fit[guide.firstCell(24)].localChord.percent() != 100 || fit[guide.firstCell(32)].localChord.percent() != -1)
        return 75;
    const auto& sustain = badges.at("sustain");
    const auto& dominantFit = sustain.at(fit[v].chordBegin);
    const auto& tonicFit = sustain.at(fit[tonic].chordBegin);
    const auto& answer = badges.at("answer").at(fit[tonic].chordBegin);
    if (badges.size() != 2 || sustain.size() != 2 || fit[tonic].chordBegin != fit[solo].chordBegin ||
        dominantFit.chord.total != 4 || dominantFit.chord.percent() != 100 || tonicFit.chord.total != 4 ||
        tonicFit.chord.percent() != 0 || answer.chord.total != 4 || answer.chord.percent() != 100 ||
        dominantFit.scale.percent() != 100 || tonicFit.scale.percent() != 100) return 51;
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
    using Tone = TheoryGuide::Tone;
    const auto& g7 = guide.chords[1];
    const auto& cmaj = guide.chords[2];
    const auto ionian = guide.scales[0].mask;
    // The same sustained F is the seventh of G7, then an avoid candidate over Cmaj7.
    if (TheoryGuide::tone(65, &g7, ionian) != Tone::guide || TheoryGuide::tone(65, &cmaj, ionian) != Tone::avoid
        || juce::String(TheoryGuide::interval(65, g7.root)) != "b7" || juce::String(TheoryGuide::interval(65, cmaj.root)) != "4"
        || TheoryGuide::tone(60, &cmaj, ionian) != Tone::root || TheoryGuide::tone(67, &cmaj, ionian) != Tone::fifth
        || TheoryGuide::tone(61, &cmaj, ionian) != Tone::outside || TheoryGuide::tone(61, nullptr, 0) != Tone::unknown) return 23;
    if (TheoryGuide::tone(62, &cmaj, ionian) != Tone::tension || TheoryGuide::tone(65, nullptr, ionian) != Tone::scale
        || TheoryGuide::tone(62, &cmaj, 0) != Tone::outside) return 44;
    auto substitution = juce::JSON::parse(R"({"song":{"key":{"state":"tonal","tonic":{"step":"C"},"mode":"major"}},
        "harmony":{"chords":[{"root":{"step":"D"},"intervals":[0,3,7,10],"start":0,"duration":4},
        {"root":{"step":"D","alter":-1},"intervals":[0,4,7,10],"start":4,"duration":4},
        {"root":{"step":"C"},"intervals":[0,4,7],"start":8,"duration":4}]}})");
    const TheoryGuide sub(substitution, 12);
    if (sub.progressions.size() != 1 || sub.progressions[0].label != "ii-subV-I"
        || sub.functions[1].function != "Dominant substitute") return 45;
    auto subChord = (*substitution["harmony"]["chords"].getArray())[1];
    subChord.getDynamicObject()->setProperty("intervals", juce::JSON::parse("[0,4,7,11]"));
    if (!TheoryGuide(substitution, 12).progressions.empty()) return 46;
    subChord.getDynamicObject()->setProperty("root", juce::JSON::parse(R"({"step":"G"})"));
    subChord.getDynamicObject()->setProperty("intervals", juce::JSON::parse("[0,4,7,10]"));
    auto predominant = (*substitution["harmony"]["chords"].getArray())[0];
    predominant.getDynamicObject()->setProperty("root", juce::JSON::parse(R"({"step":"F"})"));
    predominant.getDynamicObject()->setProperty("intervals", juce::JSON::parse("[0,4,7]"));
    const TheoryGuide iv(substitution, 12);
    if (iv.progressions.size() != 1 || iv.progressions.front().label != "IV-V-I") return 47;
    auto applied = juce::JSON::parse(R"({"song":{"key":{"state":"tonal","tonic":{"step":"C"},"mode":"major"}},
        "harmony":{"chords":[{"root":{"step":"A"},"intervals":[0,4,7,10,15],"start":0,"duration":4},
        {"root":{"step":"D"},"intervals":[0,3,7,10],"start":4,"duration":4},
        {"root":{"step":"A"},"intervals":[0,3,7],"start":8,"duration":4}]}})");
    const TheoryGuide intoTwo(applied, 12);
    if (intoTwo.progressions.size() != 1 || intoTwo.progressions[0].label != "V/ii" || intoTwo.functions[0].degree != "V/ii" || intoTwo.functions[2].degree != "vi"
        || TheoryGuide::tone(60, &intoTwo.chords[0], ionian) != Tone::tension) return 24;
    auto localChain = applied.clone();
    auto chainChords = localChain["harmony"]["chords"];
    for (auto& entry : *chainChords.getArray())
        entry.getDynamicObject()->setProperty("start", int(entry["start"]) + 4);
    chainChords.getArray()->insert(0, juce::JSON::parse(R"({"root":{"step":"E"},"intervals":[0,3,7,10],"start":0,"duration":4})"));
    const TheoryGuide localCadence(localChain, 16);
    if (localCadence.progressions.empty() || localCadence.progressions.front().label != "(ii-V)/ii") return 57;
    (*applied["harmony"]["chords"].getArray())[1].getDynamicObject()->setProperty("start", 5);
    if (TheoryGuide(applied, 12).functions.front().degree == "V/ii") return 25;
    auto cadenceScore = substitution.clone();
    auto events = cadenceScore["harmony"]["chords"];
    events.getArray()->remove(1);
    (*events.getArray())[1].getDynamicObject()->setProperty("start", 4);
    const TheoryGuide plagal(cadenceScore, 8);
    if (plagal.progressions.size() != 1 || plagal.progressions[0].label != "IV-I") return 58;
    auto firstChord = (*events.getArray())[0].getDynamicObject();
    auto secondChord = (*events.getArray())[1].getDynamicObject();
    firstChord->setProperty("root", juce::JSON::parse(R"({"step":"G"})"));
    secondChord->setProperty("root", juce::JSON::parse(R"({"step":"A"})"));
    secondChord->setProperty("intervals", juce::JSON::parse("[0,3,7]"));
    const TheoryGuide deceptive(cadenceScore, 8);
    if (deceptive.progressions.size() != 1 || deceptive.progressions[0].label != "V-vi") return 59;
    secondChord->setProperty("start", 5);
    if (!TheoryGuide(cadenceScore, 9).progressions.empty()) return 60;
    auto turnaround = juce::JSON::parse(R"({"song":{"key":{"state":"tonal","tonic":{"step":"C"},"mode":"major"}},
        "harmony":{"chords":[{"root":{"step":"C"},"intervals":[0,4,7],"start":0,"duration":1},
        {"root":{"step":"A"},"intervals":[0,3,7],"start":1,"duration":1},
        {"root":{"step":"D"},"intervals":[0,3,7],"start":2,"duration":1},
        {"root":{"step":"G"},"intervals":[0,4,7,10],"start":3,"duration":1},
        {"root":{"step":"C"},"intervals":[0,4,7],"start":4,"duration":1}]}})");
    const TheoryGuide longGuide(turnaround, 5);
    if (longGuide.progressions.size() != 1 || longGuide.progressions.front().label != "I-vi-ii-V-I") return 61;
    return 0;
}

struct Command { int id; bool checked; };
int songMusicChecks(const juce::File& demo) {
    if (shiftedLabel("Dbmaj7/F", 2) != "Ebmaj7/G" || shiftedLabel("Blues", 2) != "Blues" ||
        shiftedLabel("Dorian", -2) != "Dorian") return 45;
    auto score = juce::JSON::parse(demo);
    auto& tempos = *score["timeline"]["tempoChanges"].getArray();
    tempos.add(juce::JSON::parse(R"({"at":960,"bpm":80})"));
    const auto note = score["tracks"][0]["clips"][0]["notes"][0];
    note.getDynamicObject()->setProperty("pitchRole", "fixedTrigger");
    const auto fixedId = note["id"].toString();
    const auto utf8 = juce::JSON::toString(score);
    juce::MemoryInputStream input(utf8.toRawUTF8(), utf8.getNumBytesAsUTF8(), false);
    std::unique_ptr<OstfpPreview> song;
    if (readOstfPreview(input, song).failed()) return 30;
    const auto original = song->sourceScore;
    const auto pitches = song->pitchedNotes;
    const auto drum = song->drumNotes;
    if (song->checkAudition(2, .5).failed() || song->clock.scaled(.5)->bpmAt(960) != 40
        || song->sourceScore != original || song->checkAudition(127, 1).wasOk()) return 31;
    SongMusicEdit transform; transform.mode = SongMusicEdit::transform; transform.semitones = 2; transform.rate = .5;
    juce::MemoryBlock before;
    if (song->applySongMusic(transform, before).failed() || before != original || song->clock.bpmAt(0) != 53
        || song->clock.bpmAt(960) != 40 || song->contextAt(TimelineLane::chord, 0) != "Dmaj7"
        || song->contextAt(TimelineLane::key, 0) != "D major" || song->contextAt(TimelineLane::scale, 0) != "D Ionian") return 32;
    for (std::size_t t = 0; t < pitches.size(); ++t) {
        for (std::size_t i = 0; i < pitches[t].size(); ++i)
            if (song->pitchedNotes[t][i].pitch != pitches[t][i].pitch + (pitches[t][i].id == fixedId || song->trackKinds[t] == TrackKind::drum ? 0 : 2)) return 33;
        for (std::size_t i = 0; i < drum[t].size(); ++i)
            if (song->drumNotes[t][i].pitch != drum[t][i].pitch) return 34;
    }
    const auto transposed = song->sourceScore;
    transform.semitones = 127;
    if (song->applySongMusic(transform, before).wasOk() || song->sourceScore != transposed) return 35;
    SongMusicEdit base; base.key = juce::JSON::parse(R"({"state":"tonal","tonic":{"step":"F","alter":0},"mode":"minor"})");
    base.scale = juce::JSON::parse(R"({"id":"fm","tonic":{"step":"F","alter":0},"intervals":[0,3,5,7,10],"label":"F minor pentatonic"})");
    base.numerator = 6; base.denominator = 8; base.bpm = 90;
    if (song->applySongMusic(base, before).failed() || before != transposed || song->clock.bpmAt(0) != 90
        || song->clock.bpmAt(960) != 40 || song->beatUnit != 8 || song->meterBeats != 6 || song->contextAt(TimelineLane::key, 0) != "F minor"
        || song->defaultScale != "F minor pentatonic" || song->contextAt(TimelineLane::scale, 0) != "D Ionian") return 36;
    juce::MemoryOutputStream out;
    if (writeOstfPreview(*song, out).failed()) return 37;
    juce::MemoryInputStream saved(out.getData(), out.getDataSize(), false);
    std::unique_ptr<OstfpPreview> reopened;
    if (readOstfPreview(saved, reopened).failed() || reopened->meterBeats != 6 || reopened->clock.bpmAt(960) != 40
        || reopened->defaultScale != song->defaultScale || song->restoreNoteScore(original).failed() || song->sourceScore != original) return 38;
    return 0;
}

Command find(juce::MenuBarModel& model, const juce::String& label, int menuIndex = 1) {
    const auto menu = model.getMenuForIndex(menuIndex, {});
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next()) if (it.getItem().text == label) return {it.getItem().itemID, it.getItem().isTicked};
    return {};
}
}

int checkTheoryGuide(const juce::File& demo) {
    if (int failed = modelChecks()) return failed;
    if (int failed = songMusicChecks(demo)) return failed;
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
    main.setVisible(true); main.setSize(1440, 1280);
    if (!main.openDocument(demo)) return 14;
    auto* grid = dynamic_cast<PianoNoteGrid*>(main.findChildWithID("piano-note-grid"));
    juce::MenuBarModel* model = nullptr;
    for (auto* child : main.getChildren()) if (auto* bar = dynamic_cast<juce::MenuBarComponent*>(child)) model = bar->getModel();
    auto* colours = dynamic_cast<juce::ComboBox*>(main.findChildWithID("piano-note-colours"));
    if (!grid || !model || !colours || !colours->isVisible()) return 15;
    auto set = [&](const char* text, bool enabled) {
        const auto cmd = find(*model, juce::translate(text));
        if (cmd.id && cmd.checked != enabled) model->menuItemSelected(cmd.id, 1);
        return cmd.id != 0;
    };
    const auto show = find(*model, TRANS("Show Theory Guide")), chord = find(*model, TRANS("Chord fit")),
               scale = find(*model, TRANS("Scale fit"));
    const auto blue = find(*model, TRANS("Studio Blue"), 5), harmonic = find(*model, TRANS("Harmonic colours"), 5);
    const auto graphite = find(*model, TRANS("Graphite"), 5), aubergine = find(*model, TRANS("Aubergine"), 5);
    const std::array themes{blue, harmonic, graphite, aubergine, find(*model, TRANS("Forest Console"), 5),
                            find(*model, TRANS("Midnight Indigo"), 5), find(*model, TRANS("Copper Console"), 5)};
    const auto active = std::find_if(themes.begin(), themes.end(), [](const auto& t) { return t.checked; });
    if (active == themes.end() || std::any_of(themes.begin(), themes.end(), [](const auto& t) { return !t.id; }))
        return 44;
    auto* pitchZoom = dynamic_cast<juce::ComboBox*>(main.findChildWithID("piano-pitch-zoom"));
    auto* divider = main.findChildWithID("piano-parameter-divider");
    if (!pitchZoom || !divider || !divider->isVisible())
        return 45;
    const auto storedRows = pitchZoom->getSelectedId();
    pitchZoom->setSelectedId(12, juce::sendNotificationSync);
    const bool hasOctave = grid->high - grid->low + 1 == 12;
    pitchZoom->setSelectedId(storedRows, juce::sendNotificationSync);
    if (!hasOctave)
        return 46;
    const auto savedMode = colours->getSelectedId();
    juce::ScopeGuard restore([&] {
        model->menuItemSelected(active->id, 4);
        colours->setSelectedId(savedMode, juce::sendNotificationSync);
        set("Show Theory Guide", true);
        set("Chord fit", chord.checked);
        set("Scale fit", scale.checked);
        set("Show Theory Guide", show.checked);
    });
    for (std::size_t i = 0; i < themes.size(); ++i) {
        model->menuItemSelected(themes[i].id, 4);
        const auto& finish = ostfstudio::console::finishes[i];
        if (!find(*model, juce::translate(finish.name), 5).checked
            || main.findColour(ostfstudio::console::LookAndFeel::accentColourId) != juce::Colour(finish.accent)) return 47;
    }
    const auto preview = juce::SystemStats::getEnvironmentVariable("OSTF_CONSOLE_MIXER_PNG", {});
    if (preview.isNotEmpty()) {
        MixerPanel mixer;
        mixer.setLookAndFeel(&main.getLookAndFeel());
        mixer.setTracks(*project);
        if (project->savedMix) mixer.restore(*project->savedMix);
        while (mixer.returnCount() < ostfstudio::maxMixerReturns) mixer.addReturn();
        mixer.setSize(2260, 680);
        ostfstudio::MeterFrame signal;
        signal.master.vu = {.78f,1.10f};
        signal.master.levels.momentary = -16.2;
        signal.master.levels.shortTerm = -17.7;
        signal.master.levels.integrated = -18.6;
        signal.master.levels.rms = {.16f,.18f};
        signal.master.levels.peak = {.64f,.78f};
        signal.master.levels.maxTruePeak = {.83f,.86f};
        signal.master.levels.correlation = .72;
        signal.master.levels.hasCorrelation = true;
        signal.master.historyCount = unsigned(signal.master.shortHistory.size());
        for (size_t i=0; i<signal.master.shortHistory.size(); ++i)
            signal.master.shortHistory[i] = float(-18. + 3.*std::sin(i*.15) + 1.5*std::sin(i*.67));
        std::array<float,31> spectrum{};
        for (size_t i=0; i<spectrum.size(); ++i)
            spectrum[i] = float(.28*std::exp(-.12*i)*(.7+.3*std::cos(.8*i)));
        mixer.setSpectrum(spectrum);
        for (size_t i=0; i<signal.vu.size(); ++i) signal.vu[i] = {.32f + .13f * (i % 6), .42f + .16f * (i % 5)};
        for (size_t i=0; i<signal.master.scope.size(); ++i) {
            const double t = .10*i;
            signal.master.scope[i] = {float(.38*std::sin(t)), float(.33*std::sin(t+.7))};
        }
        signal.outputs[0] = {.65f, .81f};
        mixer.updateMeters(signal, 1.);
        signal.outputs[0] = {.09f, .14f};
        mixer.updateMeters(signal, 1.65);
        juce::FileOutputStream out{juce::File(preview)};
        if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed()
            || !juce::PNGImageFormat().writeImageToStream(mixer.createComponentSnapshot(mixer.getLocalBounds(), true, 1.25f), out))
            return 48;
    }
    if (!set("Show Theory Guide", false)) return 16;
    colours->setSelectedId(2, juce::sendNotificationSync);
    const int normal = grid->getHeight();
    set("Show Theory Guide", true); set("Chord fit", true); set("Scale fit", true);
    const int both = grid->getHeight();
    if (both <= 0 || both != normal) return 17;
    colours->setSelectedId(4, juce::sendNotificationSync);
    for (auto* child : main.getChildren()) {
        if (auto* picker = dynamic_cast<juce::ComboBox*>(child); picker && picker->getTitle() == TRANS("Arrangement track"))
            picker->setSelectedId(static_cast<int>(keys + 1), juce::sendNotificationSync);
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider && slider->getTitle() == TRANS("Timeline horizontal zoom"))
            slider->setValue(0.68, juce::sendNotificationSync);
    }
    main.displayTransportPosition(13 * project->ticksPerBar);
    if (auto* scroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-horizontal-scroll")))
            scroll->setCurrentRangeStart(13, juce::sendNotificationSync);
    auto* vertical = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-vertical-scroll"));
    if (!vertical) return 62;
    const auto chordLane = find(*model, TRANS("Chords")), scaleLane = find(*model, TRANS("Scales"));
    juce::ScopeGuard restoreLanes([&] { set("Chords", chordLane.checked); set("Scales", scaleLane.checked); });
    set("Chords", true); set("Scales", true);
    vertical->setCurrentRangeStart(0, juce::sendNotificationSync);
    // Each View switch must put its fit colours on the rendered timeline.
    const auto badgePixels = [&] {
        const auto picture = main.createComponentSnapshot(main.getLocalBounds());
        int pixels = 0;
        for (auto fill : {fitColour(0), fitColour(50), fitColour(75), fitColour(90), fitColour(100), juce::Colour(0xff303f49)}) {
            for (int y = 0; y < grid->getY(); y += 2)
                for (int x = grid->getX(); x < grid->getRight(); x += 2)
                    pixels += picture.getPixelAt(x, y) == fill;
        }
        return pixels;
    };
    set("Chord fit", false); set("Scale fit", false);
    const auto noBadges = badgePixels();
    set("Chord fit", true);
    if (badgePixels() <= noBadges) return 75;
    set("Chord fit", false); set("Scale fit", true);
    if (badgePixels() <= noBadges) return 76;
    set("Chord fit", true);
    vertical->setCurrentRangeStart(320, juce::sendNotificationSync);
    const juce::String muteName = juce::String::fromUTF8(project->trackLanes[keys].label) + " mute";
    juce::Component* trackMute = nullptr;
    for (auto* child : main.getChildren())
        for (auto* control : child->getChildren())
            if (control->getTitle() == muteName) trackMute = control;
    if (!trackMute) return 63;
    model->menuItemSelected(harmonic.id, 4);
    const auto multicolour = main.createComponentSnapshot(main.getLocalBounds());
    model->menuItemSelected(blue.id, 4);
    const auto snapshot = main.createComponentSnapshot(main.getLocalBounds());
    if (!find(*model, TRANS("Studio Blue"), 5).checked) return 45;
    int different = 0;
    for (int y = grid->getY(); y < grid->getBottom(); y += 3)
        for (int x = grid->getX(); x < grid->getRight(); x += 3)
            different += snapshot.getPixelAt(x, y) != multicolour.getPixelAt(x, y);
    if (different < 20) return 46;
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable("OSTF_THEORY_SMOKE_PNG", {});
    if (snapshotPath.isNotEmpty()) {
        juce::FileOutputStream out {juce::File(snapshotPath)};
        if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed()
            || !juce::PNGImageFormat().writeImageToStream(snapshot, out)) return 27;
    }
    int chordPixels = 0;
    for (int y = grid->getY(); y < grid->getBottom(); ++y)
        for (int x = grid->getX(); x < grid->getRight(); ++x)
            if (snapshot.getPixelAt(x,y).getARGB() == 0xff086250) ++chordPixels;
    if (!chordPixels) return 18;
    const int accompanimentHeight = grid->getHeight();
    set("Scale fit", false);
    if (grid->getHeight() != accompanimentHeight || !find(*model, TRANS("Chord fit")).checked) return 19;
    set("Chord fit", false);
    // The pre-chorus keys clip covers four visible chords. Each must draw its own badges.
    const auto plain = main.createComponentSnapshot(main.getLocalBounds());
    const auto times = grid->tickRange();
    const auto top = main.getLocalArea(trackMute, trackMute->getLocalBounds()).getY();
    int checked = 0;
    for (const auto& harmony : guide.functions) {
        if (harmony.start < times.getStart() || harmony.end > times.getEnd()) continue;
        const auto x = grid->getX() + juce::roundToInt(grid->getWidth() * (harmony.end - times.getStart()) / times.getLength()) - 12;
        int changed = 0;
        for (int dx = 0; dx < 8; ++dx)
            for (int dy = 3; dy < 14; ++dy)
                changed += plain.getPixelAt(x + dx, top + dy) != snapshot.getPixelAt(x + dx, top + dy);
        if (changed < 24) return 64;
        ++checked;
    }
    if (checked < 3) return 65;
    set("Chord fit", false); set("Scale fit", true);
    if (grid->getHeight() != accompanimentHeight || find(*model, TRANS("Chord fit")).checked) return 20;
    set("Show Theory Guide", false);
    const PitchedNote c {0, 10, 60};
    for (int mode = 1; mode <= 4; ++mode) {
        colours->setSelectedId(mode, juce::sendNotificationSync);
        const auto hint = grid->theoryHint(0, c);
        if (hint.contains("Cmaj7") != (mode != 1)
            || hint.contains("Ionian") != (mode == 3 || mode == 4)
            || (mode == 1 ? grid->getHeight() <= accompanimentHeight : grid->getHeight() != accompanimentHeight)) return 26;
    }
    auto* guideButton = dynamic_cast<ViewButton*>(main.findChildWithID("theory-guide"));
    auto* musicButton = dynamic_cast<ViewButton*>(main.findChildWithID("song-settings"));
    if (!guideButton || !musicButton || guideButton->getTooltip().isEmpty() || musicButton->getBounds().isEmpty()) return 39;
    if (find(*model, TRANS("Song defaults..."), 0).id != 0
        || !find(*model, TRANS("Transpose or change tempo of score..."), 0).id
        || musicButton->getTooltip() != TRANS("Transpose and tempo")) return 49;
    guideButton->onClick();
    if (!guideButton->getToggleState() || !find(*model, TRANS("Show Theory Guide")).checked) return 40;
    // Drag C to C-sharp over the opening Cmaj7. Inspect the actual readout before release and after committing.
    for (auto* child : main.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider && slider->getTitle() == TRANS("Timeline horizontal zoom"))
            slider->setValue(1, juce::sendNotificationSync);
    auto* scroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-horizontal-scroll"));
    scroll->setCurrentRangeStart(1, juce::sendNotificationSync);
    grid->commit({{NoteEdit::insert, {0, project->ppq, 84, 96, true, {}}}});
    auto changed = std::find_if(grid->notes.begin(), grid->notes.end(), [&](const auto& n){ return grid->selection.contains(n.id); });
    if (changed == grid->notes.end() || !grid->getDescription().contains("100%")) return 41;
    const auto point = grid->box(*changed).getCentre();
    const auto end = point.translated(0, -float(grid->getHeight()) / (grid->high - grid->low + 1));
    auto mouse = [&](juce::Point<float> p) {
        const auto t = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys::leftButtonModifier,
            1, 0, 0, 0, 0, grid, grid, t, point, t, 1, p != point);
    };
    grid->mouseDown(mouse(point)); grid->mouseDrag(mouse(end));
    const auto outside = grid->getDescription();
    if (outside.isEmpty() || !outside.contains("0%") || outside.contains("100%")) return 42;
    grid->mouseUp(mouse(end));
    if (grid->getDescription() != outside || std::none_of(grid->notes.begin(), grid->notes.end(),
        [&](const auto& n){ return grid->selection.contains(n.id) && n.pitch == 85; })) return 43;
    return 0;
}
