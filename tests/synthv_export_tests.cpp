#include "ui/OstfpPreview.h"
#include "ui/SynthVExport.h"
#include <iostream>
#include <set>

namespace {
using namespace juce;
using namespace ostfstudio;

std::unique_ptr<OstfpPreview> open(const File& file) {
    FileInputStream stream(file);
    std::unique_ptr<OstfpPreview> song;
    const auto result = file.hasFileExtension("ostfp") ? readOstfpPreview(stream,song) : readOstfPreview(stream,song);
    if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
    return song;
}

var render(const OstfpPreview& song, SynthVParts parts, File file = {}) {
    MemoryOutputStream bytes;
    if (auto result = writeSynthV(song,parts,bytes); result.failed())
        throw std::runtime_error(result.getErrorMessage().toStdString());
    if (!bytes.getDataSize() || static_cast<const char*>(bytes.getData())[bytes.getDataSize()-1] != '\0')
        throw std::runtime_error("Missing SVP stream terminator");
    if (file != File{} && !file.replaceWithData(bytes.getData(),bytes.getDataSize()))
        throw std::runtime_error("Could not save SVP test output");
    var parsed;
    if (JSON::parse(bytes.toUTF8(),parsed).failed() || int(parsed["version"]) != 113)
        throw std::runtime_error("Wrong SVP encoding or version");
    std::set<String> groups;
    for (const auto& track : *parsed["tracks"].getArray()) {
        const auto group = track["mainGroup"];
        const auto id = group["uuid"].toString();
        if (id.isEmpty() || id != track["mainRef"]["groupID"].toString() || !groups.insert(id).second)
            throw std::runtime_error("Invalid group reference");
        juce::int64 previousEnd = 0;
        for (const auto& note : *group["notes"].getArray()) {
            const juce::int64 start = note["onset"], duration = note["duration"];
            if (start < previousEnd || duration <= 0) throw std::runtime_error("Overlapping SVP notes");
            previousEnd = start + duration;
        }
    }
    return parsed;
}
}

int checkSynthVExport(const File& root, const File& output) try {
    if (output.createDirectory().failed()) return 1;
    auto demo = open(root.getChildFile("examples/demo-songs/paper-lantern-replies.ostfp"));
    const auto source = demo->sourceScore;
    const auto lead = render(*demo,SynthVParts::lead,output.getChildFile("Paper Lantern Replies-lead.svp"))["tracks"];
    const auto chorus = render(*demo,SynthVParts::chorus,output.getChildFile("Paper Lantern Replies-chorus.svp"))["tracks"];
    const auto both = render(*demo,SynthVParts::all,output.getChildFile("Paper Lantern Replies-vocals.svp"));
    if (lead.size()!=1 || chorus.size()!=1 || both["tracks"].size()!=2 || demo->sourceScore!=source) return 2;
    const auto notes = lead[0]["mainGroup"]["notes"];
    const auto backing = chorus[0]["mainGroup"]["notes"];
    if (lead[0]["name"]!="Lead Vocal" || chorus[0]["name"]!="Backing Vocal Harmony" || notes.size()!=194 || backing.size()!=10) return 3;
    if (notes[0]["lyrics"].toString()!=String::charToString(0x3042) || juce::int64(notes[0]["onset"])!=11642400000LL
        || double(both["time"]["tempo"][0]["bpm"])!=78 || int(both["time"]["meter"][0]["numerator"])!=4) return 4;
    if (JSON::toString(both["tracks"][0]["mainGroup"]["notes"])!=JSON::toString(notes)
        || JSON::toString(both["tracks"][1]["mainGroup"]["notes"])!=JSON::toString(backing)) return 5;

    const auto fixtures = root.getChildFile("tests/fixtures/ostf52");
    auto comp = open(fixtures.getChildFile("03-midi-take-comp.ostf"));
    comp->primaryMelodyTrack = 0;
    const auto compNotes = render(*comp,SynthVParts::lead)["tracks"][0]["mainGroup"]["notes"];
    if (compNotes.size()!=2 || int(compNotes[0]["pitch"])!=60 || int(compNotes[1]["pitch"])!=67) return 14;
    // An additional instrumental melody lane is not a backing singer.
    demo->mixerGroups[1] = MixerTrackGroup::melody;
    if (render(*demo,SynthVParts::all)["tracks"].size()!=2) return 15;
    const auto harmony = open(fixtures.getChildFile("30-special-tracks-shared-lyrics.ostf"));
    if (render(*harmony,SynthVParts::chorus)["tracks"].size()!=2) return 6;
    auto shared = open(fixtures.getChildFile("31-special-tracks-shared-core-track.ostf"));
    auto singers = render(*shared,SynthVParts::all)["tracks"];
    const auto sung = singers[0]["mainGroup"]["notes"], hum = singers[1]["mainGroup"]["notes"];
    if (singers.size()!=2 || sung.size()!=2 || hum.size()!=2 || int(sung[0]["pitch"])!=60 || int(hum[0]["pitch"])!=55
        || hum[0]["lyrics"]!="m" || sung[0]["lyrics"]==hum[0]["lyrics"]) return 7;
    if (render(*shared,SynthVParts::lead)["tracks"].size()!=1 || render(*shared,SynthVParts::chorus)["tracks"][0]["name"]!="Humming voice") return 8;

    auto score = JSON::parse(shared->sourceScore.toString());
    auto& alignments = *score["lyrics"].getDynamicObject()->getProperty("alignments").getArray();
    alignments.clear();
    alignments.add(JSON::parse(R"({"voiceId":"voice-singer","unitIds":["unit-so","unit-ra"],"noteIds":["note-singer-2","note-singer-1"]})"));
    auto setScore = [&] {
        const auto text = JSON::toString(score);
        shared->sourceScore = MemoryBlock(text.toRawUTF8(),text.getNumBytesAsUTF8());
    };
    setScore();
    auto grouped = render(*shared,SynthVParts::lead)["tracks"][0]["mainGroup"]["notes"];
    if (grouped[0]["lyrics"]!=sung[0]["lyrics"] || grouped[1]["lyrics"]!=sung[1]["lyrics"]) return 12;
    alignments[0].getDynamicObject()->setProperty("unitIds",Array<var>{"unit-so"});
    setScore();
    grouped = render(*shared,SynthVParts::lead)["tracks"][0]["mainGroup"]["notes"];
    if (grouped[0]["lyrics"]!=sung[0]["lyrics"] || grouped[1]["lyrics"]!="-") return 13;

    // An authored harmony chord must occupy two monophonic SV tracks. Inactive notes never sound.
    auto& harmonyNotes = demo->pitchedNotes[size_t(demo->trackIds.indexOf("backing"))];
    auto extra = harmonyNotes.front();
    extra.pitch += 3;
    extra.id = "extra-part";
    harmonyNotes.push_back(extra);
    demo->clock = TempoMap(demo->ppq);
    demo->clock.append(0,78);
    demo->clock.append(demo->ppq,93);
    demo->meters.push_back({demo->ticksPerBar * 2,8,8});
    auto divided = render(*demo,SynthVParts::chorus);
    if (divided["tracks"].size()!=2 || int(divided["time"]["meter"][1]["index"])!=2
        || juce::int64(divided["time"]["tempo"][1]["position"])!=705600000 || int(divided["time"]["tempo"][1]["bpm"])!=93) return 9;
    for (auto& note : harmonyNotes) note.enabled=false;
    MemoryOutputStream empty;
    if (writeSynthV(*demo,SynthVParts::chorus,empty).wasOk() || empty.getDataSize()!=0) return 10;
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 11; }
