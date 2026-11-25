#include "ScaleDocument.h"
#include "../../core/ScoreSchema.h"
#include <juce_cryptography/juce_cryptography.h>
#include <jsoncons/json.hpp>

namespace ostfstudio::scale {
const char* const tonicNames[] = {"C","Db","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
const std::vector<Scale>& catalog() {
    // Scale names and pitch collections from the supplied OSTF Scale Editor v1.0.
    static const std::vector<Scale> scales {
    {"Ionian / Major", {0,2,4,5,7,9,11}},
    {"Dorian", {0,2,3,5,7,9,10}},
    {"Phrygian", {0,1,3,5,7,8,10}},
    {"Lydian", {0,2,4,6,7,9,11}},
    {"Mixolydian", {0,2,4,5,7,9,10}},
    {"Aeolian / Natural minor", {0,2,3,5,7,8,10}},
    {"Locrian", {0,1,3,5,6,8,10}},
    {"Melodic minor", {0,2,3,5,7,9,11}},
    {"Dorian b2", {0,1,3,5,7,9,10}},
    {"Lydian augmented", {0,2,4,6,8,9,11}},
    {"Lydian dominant", {0,2,4,6,7,9,10}},
    {"Mixolydian b6", {0,2,4,5,7,8,10}},
    {"Locrian natural 2", {0,2,3,5,6,8,10}},
    {"Altered", {0,1,3,4,6,8,10}},
    {"Harmonic minor", {0,2,3,5,7,8,11}},
    {"Phrygian dominant", {0,1,4,5,7,8,10}},
    {"Half-whole diminished", {0,1,3,4,6,7,9,10}},
    {"Whole-half diminished", {0,2,3,5,6,8,9,11}},
    {"Whole tone", {0,2,4,6,8,10}},
    {"Major pentatonic", {0,2,4,7,9}},
    {"Minor pentatonic", {0,3,5,7,10}},
    {"Minor blues", {0,3,5,6,7,10}},
    {"Dominant bebop", {0,2,4,5,7,9,10,11}},
    {"Major bebop", {0,2,4,5,7,8,9,11}},
    };
    return scales;
}
juce::String digest(const juce::String& text) {
    return juce::SHA256(text.toRawUTF8(),text.getNumBytesAsUTF8()).toHexString();
}
juce::Result Document::importWorkspaceScales(const juce::String& edited, const juce::String& openedHash) {
    if (openedHash.isEmpty() || digest(text) != openedHash)
        return juce::Result::fail("The Studio score changed. Open a fresh full editor before importing.");
    if (edited.getNumBytesAsUTF8() > maxBytes) return juce::Result::fail("Scale export exceeds 24 MiB.");
    try {
        using Json = jsoncons::ojson;
        const auto options = jsoncons::json_options{}.lossless_number(true).bignum_format(jsoncons::bignum_format_kind::raw);
        auto original=Json::parse(text.toStdString(),options);
        const auto proposal=Json::parse(edited.toStdString(),options);
        const auto version=score["version"].toString().toStdString();
        validateScoreSchema(version,edited.toStdString());
        auto compare=proposal;
        compare.at("harmony")["scales"]=original.at("harmony").at("scales");
        // The browser may reserialize harmless decimal numbers. Only import the scale list.
        std::string normalized; compare.dump(normalized,options);
        if (juce::JSON::toString(juce::JSON::parse(juce::String::fromUTF8(normalized.data(),int(normalized.size()))),true,17)
            != juce::JSON::toString(score,true,17))
            return juce::Result::fail("This export contains changes outside the open song's harmony scales.");
        original.at("harmony")["scales"]=proposal.at("harmony").at("scales");
        if (original.contains("document")) {
            auto& meta=original.at("document");
            Json parent; parent["documentId"]=meta.at("id"); parent["revisionId"]=meta.at("revisionId"); parent["sha256"]=openedHash.toStdString();
            meta["parents"]=Json::array(); meta["parents"].push_back(parent);
            meta["revisionId"]=("scale-"+juce::Uuid().toString()).toStdString();
            meta["modifiedBy"]=Json::parse(R"({"name":"OSTF Scale Editor"})");
        }
        std::string bytes; original.dump(bytes,options);
        validateScoreSchema(version,bytes);
        text=juce::String::fromUTF8(bytes.data(),int(bytes.size())); score=juce::JSON::parse(text);
        return juce::Result::ok();
    } catch(const std::exception& e) {return juce::Result::fail(e.what());}
}
juce::Result Document::load(const juce::String& utf8) {
    if (utf8.isEmpty() || utf8.getNumBytesAsUTF8() > maxBytes) return juce::Result::fail("Expected an OSTF file up to 24 MiB.");
    try {
        auto parsed = juce::JSON::parse(utf8);
        if ((parsed["version"] != "5.0.0" && parsed["version"] != "5.1.0") || parsed["format"].toString() != "OSTF")
            return juce::Result::fail("Open an OSTF 5.0 or 5.1 score.");
        validateScoreSchema(parsed["version"].toString().toStdString(),utf8.toStdString());
        score = parsed;
        text = utf8;
        baseHash = digest(text);
        return juce::Result::ok();
    } catch (const std::exception& e) { return juce::Result::fail(juce::String::fromUTF8(e.what())); }
}
juce::Result Document::assign(int chordIndex, int tonic, int mode) {
    return assign(std::vector<int>{chordIndex}, tonic, mode);
}
std::vector<int> Document::scope(int first, int last, bool repeats) const {
    const auto chords = score["harmony"]["chords"];
    if (first < 0 || first > last || last >= chords.size()) return {};
    std::vector<int> selected;
    const int length = last - first + 1;
    for (int start = repeats ? 0 : first; start <= (repeats ? chords.size() - length : first); ++start) {
        bool matches = true;
        for (int i = 0; i < length && matches; ++i) {
            auto a = chords[first+i], b = chords[start+i];
            matches = a["label"] == b["label"] && a["duration"] == b["duration"]
                && a["root"]["step"] == b["root"]["step"] && a["root"]["alter"] == b["root"]["alter"]
                && a["intervals"] == b["intervals"] && a["bass"]["step"] == b["bass"]["step"] && a["bass"]["alter"] == b["bass"]["alter"]
                && juce::int64(a["start"]) - juce::int64(chords[first]["start"])
                    == juce::int64(b["start"]) - juce::int64(chords[start]["start"]);
        }
        if (matches) for (int i=0; i<length; ++i)
            if (selected.empty() || selected.back() < start+i) selected.push_back(start+i);
    }
    return selected;
}
juce::Result Document::assign(const std::vector<int>& indices, int tonic, int mode) {
    std::vector<Assignment> plan;
    for(int chord:indices) plan.push_back({chord,tonic,mode});
    return assignPlan(plan);
}
juce::Result Document::assignPlan(const std::vector<Assignment>& plan) {
    const auto chords=score["harmony"]["chords"];
    if (plan.empty() || std::any_of(plan.begin(),plan.end(),[&](const auto& a){
            return a.chord<0 || a.chord>=chords.size() || a.tonic<0 || a.tonic>=12 || a.mode<0 || a.mode>=int(catalog().size());
        }))
        return juce::Result::fail("Select a chord, tonic and scale.");
    try {
        using Json = jsoncons::ojson;
        const auto options = jsoncons::json_options{}.lossless_number(true).bignum_format(jsoncons::bignum_format_kind::raw);
        auto doc = Json::parse(text.toStdString(),options);
        auto& harmony = doc.at("harmony");
        auto& scales = harmony.at("scales");
        for (const auto& [chordIndex,tonic,mode] : plan) {
            const auto& chord = harmony.at("chords").at(chordIndex);
            auto target = Json::parse(R"([{"type":"chord","id":""}])");
            target[0]["id"] = chord.at("id");
            Json* assigned = nullptr;
            for (auto& item : scales.array_range()) {
                if (!item.contains("targets") || item.at("targets") != target || item.at("start") != chord.at("start")
                        || item.at("duration") != chord.at("duration")) continue;
                if (assigned) return juce::Result::fail("This chord already has several targeted scales.");
                assigned = &item;
            }
            if (!assigned) {
                scales.push_back(Json());
                assigned = &scales.at(scales.size()-1);
                (*assigned)["id"] = ("scale-" + juce::Uuid().toString()).toStdString();
                (*assigned)["targets"] = target;
                (*assigned)["start"] = chord.at("start");
                (*assigned)["duration"] = chord.at("duration");
            }
            const std::string name = tonicNames[tonic];
            (*assigned)["tonic"]["step"] = name.substr(0,1);
            (*assigned)["tonic"]["alter"] = name.size() == 1 ? 0 : (name[1] == 'b' ? -1 : 1);
            (*assigned)["label"] = name + " " + catalog()[mode].name;
            (*assigned)["intervals"] = catalog()[mode].intervals;
        }
        if (doc.contains("document")) {
            auto& meta = doc.at("document");
            Json parent;
            parent["documentId"] = meta.at("id");
            parent["revisionId"] = meta.at("revisionId");
            parent["sha256"] = digest(text).toStdString();
            meta["parents"] = Json::array();
            meta["parents"].push_back(parent);
            meta["revisionId"] = ("scale-" + juce::Uuid().toString()).toStdString();
            meta["modifiedBy"] = Json::parse(R"({"name":"OSTF Scale Editor"})");
        }
        std::string bytes;
        doc.dump(bytes,options);
        bytes.push_back('\n');
        validateScoreSchema(score["version"].toString().toStdString(),bytes);
        auto result = juce::String::fromUTF8(bytes.data(),int(bytes.size()));
        score = juce::JSON::parse(result);
        text = std::move(result);
        return juce::Result::ok();
    } catch (const std::exception& e) { return juce::Result::fail(juce::String::fromUTF8(e.what())); }
}
}
