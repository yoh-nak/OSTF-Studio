#pragma once
#include "ui/ArrangementView.h"
#include "ui/SongMusic.h"
#include <map>

namespace ostfstudio {
struct GlobalLaneSource {
    const char* group;
    const char* events;
    bool span;
};
inline GlobalLaneSource globalSource(TimelineLane lane) {
    switch (lane) {
    case TimelineLane::section:
        return {"timeline", "sections", true};
    case TimelineLane::chord:
        return {"harmony", "chords", true};
    case TimelineLane::scale:
        return {"harmony", "scales", true};
    case TimelineLane::tempo:
        return {"timeline", "tempoChanges", false};
    case TimelineLane::meter:
        return {"timeline", "meterChanges", false};
    case TimelineLane::key:
        return {"timeline", "keyChanges", false};
    default:
        return {"", "", false};
    }
}
inline juce::var globalEventAt(const juce::var& score, TimelineLane lane, juce::int64 tick) {
    const auto source = globalSource(lane);
    const auto events = score[source.group][source.events];
    juce::var hit;
    if (auto* list = events.getArray())
        for (const auto& item : *list) {
            const auto at = static_cast<juce::int64>(item[source.span ? "start" : "at"]);
            if (tick >= at && (!source.span || tick - at < static_cast<juce::int64>(item["duration"])))
                hit = item;
        }
    return hit;
}
inline juce::String pitchText(const juce::var& note) {
    const auto alter = std::clamp(int(note["alter"]), -2, 2);
    return note["step"].toString() + juce::String::repeatedString(alter < 0 ? "b" : "#", std::abs(alter));
}
inline juce::var parseSpelling(juce::String text) {
    text = text.trim();
    if (text.isEmpty() || !text.substring(0, 1).toUpperCase().containsOnly("ABCDEFG"))
        return {};
    const auto suffix = text.substring(1);
    if (!(suffix.isEmpty() || suffix == "b" || suffix == "bb" || suffix == "#" || suffix == "##"))
        return {};
    auto* o = new juce::DynamicObject;
    o->setProperty("step", text.substring(0, 1).toUpperCase());
    o->setProperty("alter", suffix.startsWith("b") ? -suffix.length() : suffix.length());
    return juce::var(o);
}
inline juce::String intervalsText(const juce::var& event) {
    juce::StringArray result;
    if (const auto* list = event["intervals"].getArray())
        for (auto& i : *list)
            result.add(i.toString());
    return result.joinIntoString(" ");
}
inline juce::Result editChordSymbol(juce::var& event, juce::String symbol) {
    const auto slash = symbol.indexOfChar('/');
    auto chord = slash < 0 ? symbol : symbol.substring(0, slash);
    int n = 1;
    while (n < chord.length() && (chord[n] == 'b' || chord[n] == '#'))
        ++n;
    auto root = parseSpelling(chord.substring(0, n));
    auto bass = slash < 0 ? juce::var() : parseSpelling(symbol.substring(slash + 1));
    static const std::map<juce::String, juce::Array<juce::var>> types{{"", {0, 4, 7}},
                                                                      {"maj", {0, 4, 7}},
                                                                      {"m", {0, 3, 7}},
                                                                      {"min", {0, 3, 7}},
                                                                      {"5", {0, 7}},
                                                                      {"dim", {0, 3, 6}},
                                                                      {"aug", {0, 4, 8}},
                                                                      {"sus2", {0, 2, 7}},
                                                                      {"sus4", {0, 5, 7}},
                                                                      {"6", {0, 4, 7, 9}},
                                                                      {"m6", {0, 3, 7, 9}},
                                                                      {"7", {0, 4, 7, 10}},
                                                                      {"maj7", {0, 4, 7, 11}},
                                                                      {"m7", {0, 3, 7, 10}},
                                                                      {"m7b5", {0, 3, 6, 10}},
                                                                      {"dim7", {0, 3, 6, 9}},
                                                                      {"9", {0, 4, 7, 10, 14}},
                                                                      {"maj9", {0, 4, 7, 11, 14}},
                                                                      {"m9", {0, 3, 7, 10, 14}},
                                                                      {"add9", {0, 4, 7, 14}},
                                                                      {"11", {0, 4, 7, 10, 14, 17}},
                                                                      {"13", {0, 4, 7, 10, 14, 21}},
                                                                      {"7(b9)", {0, 4, 7, 10, 13}}};
    const auto type = types.find(chord.substring(n));
    if (root.isVoid() || (slash >= 0 && bass.isVoid()) || type == types.end())
        return juce::Result::fail("Enter a supported chord symbol such as Dm7 or G7.");
    auto* o = event.getDynamicObject();
    o->setProperty("label", symbol);
    o->setProperty("root", root);
    o->setProperty("intervals", type->second);
    if (bass.isVoid())
        o->removeProperty("bass");
    else
        o->setProperty("bass", bass);
    return juce::Result::ok();
}
} // namespace ostfstudio
