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
static juce::String revisedScales(const std::string& merged,const juce::String& parentHash) {
    using Json=jsoncons::ojson;
    const auto options=jsoncons::json_options{}.lossless_number(true).bignum_format(jsoncons::bignum_format_kind::raw);
    auto original=Json::parse(merged,options);
    if (!original.contains("document")) return juce::String::fromUTF8(merged.data(),int(merged.size()));
    auto& meta=original.at("document");
    Json parent; parent["documentId"]=meta.at("id"); parent["revisionId"]=meta.at("revisionId"); parent["sha256"]=parentHash.toStdString();
    meta["parents"]=Json::array(); meta["parents"].push_back(parent);
    meta["revisionId"]=("scale-"+juce::Uuid().toString()).toStdString();
    meta["modifiedBy"]=Json::parse(R"({"name":"OSTF Scale Editor"})");
    std::string bytes; original.dump(bytes,options);
    return juce::String::fromUTF8(bytes.data(),int(bytes.size()));
}
juce::Result Document::importWorkspaceScales(const juce::String& edited, const juce::String& openedHash) {
    if (openedHash.isEmpty() || digest(text) != openedHash)
        return juce::Result::fail("The Studio score changed. Open a fresh full editor before importing.");
    if (edited.getNumBytesAsUTF8() > maxBytes) return juce::Result::fail("Scale export exceeds 24 MiB.");
    try {
        const auto version=score["version"].toString().toStdString();
        validateScoreSchema(version,edited.toStdString());
        const auto merged=mergeScaleAssignments(text.toStdString(),edited.toStdString());
        const auto mergedText=juce::String::fromUTF8(merged.data(),int(merged.size()));
        auto normalized = [](const juce::String& value) {
            auto doc = juce::JSON::parse(value);
            // Setting and then clearing a track's first assignment leaves an empty JS array.
            if (const auto* tracks = doc["tracks"].getArray())
                for (const auto& track : *tracks)
                    if (track["directions"].isArray() && track["directions"].size()==0)
                        track.getDynamicObject()->removeProperty("directions");
            return juce::JSON::toString(doc,true,17);
        };
        if (normalized(mergedText) != normalized(edited))
            return juce::Result::fail("This export contains changes outside the open song's scale assignments.");
        if (mergedText == text) return juce::Result::ok();
        const auto revision=revisedScales(merged,openedHash);
        validateScoreSchema(version,revision.toStdString());
        text=revision; score=juce::JSON::parse(text);
        return juce::Result::ok();
    } catch(const std::exception& e) {return juce::Result::fail(e.what());}
}
juce::Result Document::load(const juce::String& utf8) {
    if (utf8.isEmpty() || utf8.getNumBytesAsUTF8() > maxBytes) return juce::Result::fail("Expected an OSTF file up to 24 MiB.");
    try {
        auto parsed = juce::JSON::parse(utf8);
        if ((parsed["version"] != "5.0.0" && parsed["version"] != "5.1.0" && parsed["version"] != "5.2.0") || parsed["format"].toString() != "OSTF")
            return juce::Result::fail("Open an OSTF 5.0, 5.1 or 5.2 score.");
        validateScoreSchema(parsed["version"].toString().toStdString(),utf8.toStdString());
        score = parsed;
        text = utf8;
        baseHash = digest(text);
        return juce::Result::ok();
    } catch (const std::exception& e) { return juce::Result::fail(juce::String::fromUTF8(e.what())); }
}
static juce::Result writeScore(const juce::File& file,const juce::String& text,const juce::String& expectedHash={}) {
    if (!file.hasFileExtension("ostf")) return juce::Result::fail("Choose a filename ending in .ostf.");
    if (text.isEmpty()) return juce::Result::fail("Open a score first.");
    juce::TemporaryFile temp(file);
    if (!temp.getFile().replaceWithText(text,false,false,nullptr))
        return juce::Result::fail("Unable to write the edited score.");
    if (expectedHash.isNotEmpty()) {
        juce::FileInputStream stream(file);
        if (!stream.openedOk() || stream.getTotalLength()>maxBytes || juce::SHA256(stream).toHexString()!=expectedHash
                || stream.getStatus().failed())
            return juce::Result::fail("The linked file changed or moved. Reopen it or use Save As to keep your edits.");
    }
    if (!temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Unable to save the edited score.");
    return juce::Result::ok();
}
juce::Result Document::saveAs(const juce::File& file) const {return writeScore(file,text);}
juce::Result LinkedScoreFile::read(const juce::File& source, juce::String& text) {
    juce::FileInputStream stream(source);
    if (!stream.openedOk()) return juce::Result::fail("Could not open this OSTF file.");
    juce::MemoryBlock bytes;
    if (stream.readIntoMemoryBlock(bytes,maxBytes+1) > maxBytes)
        return juce::Result::fail("Score exceeds 24 MiB.");
    if (stream.getStatus().failed() || bytes.getSize()==0)
        return juce::Result::fail("Could not read this OSTF file.");
    text=juce::String::createStringFromData(bytes.getData(),int(bytes.getSize()));
    openedText=text;
    diskHash=juce::SHA256(bytes).toHexString();
    file=source;
    return juce::Result::ok();
}
juce::Result LinkedScoreFile::write(const Document& doc, const juce::File& target, bool workspaceScales) {
    if (target==juce::File{}) return juce::Result::fail("Choose a destination using Save As.");
    juce::InterProcessLock lock("OSTFScaleSave-"+digest(target.getFullPathName().toLowerCase()));
    if (!lock.enter(1000)) return juce::Result::fail("Another editor is saving this file. Try again.");
    juce::ScopeGuard unlock([&]{lock.exit();});
    Document output=doc;
    if (workspaceScales && openedText.isNotEmpty()) {
        // The visual editor changes scale assignments. Preserve unrelated original
        // JSON numbers which JavaScript cannot round-trip at full precision.
        try {
            const auto original=openedText.toStdString();
            const auto merged=mergeScaleAssignments(original,doc.text.toStdString());
            const auto valid=output.load(merged==original ? openedText : revisedScales(merged,diskHash));
            if (valid.failed()) return valid;
        } catch (const std::exception& e) {return juce::Result::fail(e.what());}
    }
    const auto result=writeScore(target,output.text,target==file ? diskHash : juce::String{});
    if (result.wasOk()) { file=target; openedText=output.text; diskHash=digest(openedText); }
    return result;
}
juce::Result Document::exportForStudio(const juce::File& directory, juce::File& written) const {
    written = juce::File{};
    if (auto result=directory.createDirectory(); result.failed()) return result;
    const auto file=directory.getChildFile("studio-scales-"+baseHash+"-"+juce::Uuid().toString()+".ostf");
    auto result=saveAs(file);
    if (result.wasOk()) written=file;
    return result;
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
