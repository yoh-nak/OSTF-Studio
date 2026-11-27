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
            if (lane == TimelineLane::scale && item.hasProperty("targets")) continue;
            const auto at = static_cast<juce::int64>(item[source.span ? "start" : "at"]);
            if (tick >= at && (!hit.isObject() || at >= static_cast<juce::int64>(hit[source.span ? "start" : "at"]))
                && (!source.span || tick - at < static_cast<juce::int64>(item["duration"])))
                hit = item;
        }
    return hit;
}
inline juce::Range<juce::int64> sharedScaleGap(const juce::var& score, juce::int64 tick,
                                               juce::Range<juce::int64> limits) {
    if (!limits.contains(tick)) return {};
    auto start = limits.getStart(), end = limits.getEnd();
    const auto scales = score["harmony"]["scales"];
    if (const auto* entries = scales.getArray()) for (const auto& scale : *entries) {
        if (scale.hasProperty("targets")) continue;
        const juce::int64 a = scale["start"];
        const auto b = a + juce::int64(scale["duration"]);
        if (a <= tick && tick < b) return {};
        if (b <= tick) start = std::max(start, b);
        if (a > tick) end = std::min(end, a);
    }
    return {start, end};
}
inline juce::Range<juce::int64> emptyScaleRange(const juce::var& score, juce::int64 tick,
                                                juce::int64 barLength, juce::int64 songEnd) {
    if (tick < 0 || tick >= songEnd || barLength <= 0) return {};
    const auto bar = tick - tick % barLength;
    return sharedScaleGap(score, tick, {bar, bar + std::min(barLength, songEnd - bar)});
}
inline juce::int64 snapGlobalTick(TimelineLane lane, juce::int64 tick, juce::int64 origin,
                                  juce::int64 ppq, int numerator, int denominator) {
    if (lane == TimelineLane::tempo) return tick;
    const auto beat = std::max<juce::int64>(1, ppq * 4 / std::max(1, denominator));
    const auto grid = lane == TimelineLane::section || lane == TimelineLane::meter ? beat * std::max(1, numerator)
        : lane == TimelineLane::key ? beat : std::max<juce::int64>(1, beat / 2);
    return origin + std::llround(double(tick - origin) / grid) * grid;
}

// A shared edge belongs to both spans. Keep both IDs so score references survive a drag.
inline juce::Result moveGlobalBoundary(const juce::var& score, TimelineLane lane, juce::int64 boundary,
                                       juce::int64 target, juce::int64 endTick,
                                       juce::Array<juce::var>& remove, juce::Array<juce::var>& insert) {
    const auto source = globalSource(lane);
    const auto key = source.span ? "start" : "at";
    const auto events = score[source.group][source.events];
    juce::var left, right;
    auto next = endTick;
    if (const auto* list = events.getArray()) for (const auto& event : *list) {
        if (lane == TimelineLane::scale && event.hasProperty("targets")) continue;
        const juce::int64 at = event[key];
        if (at == boundary) right = event;
        if (at < boundary && (!left.isObject() || at > juce::int64(left[key]))) left = event;
        if (at > boundary) next = std::min(next, at);
    }
    const auto leftStart = left.isObject() ? juce::int64(left[key]) : 0;
    const bool shared=source.span && left.isObject() && leftStart+juce::int64(left["duration"])==boundary;
    const bool gap=lane==TimelineLane::scale;
    const auto lower=source.span && !shared && left.isObject() ? leftStart+juce::int64(left["duration"]) : leftStart;
    if (boundary<=0 || (!right.isObject() && !(shared && gap)) || target<=lower || target>=next ||
        (source.span && !shared && !gap))
        return juce::Result::fail("Choose a position inside the adjacent intervals.");
    if (right.isObject() && source.span && target>=boundary+juce::int64(right["duration"]))
        return juce::Result::fail("Choose a position inside the adjacent intervals.");
    if (shared) {
        auto first=left.clone();
        first.getDynamicObject()->setProperty("duration",target-leftStart);
        remove.add(left); insert.add(first);
    }
    if (!right.isObject()) return juce::Result::ok();
    auto moved=right.clone();
    if (source.span) moved.getDynamicObject()->setProperty("duration",boundary+juce::int64(right["duration"])-target);
    moved.getDynamicObject()->setProperty(key, target);
    remove.add(right); insert.add(moved);
    return juce::Result::ok();
}
// Compute a boundary edit without removing time from the lane.
inline juce::Result globalBoundaryEdit(const juce::var& score, TimelineLane lane, juce::int64 tick,
                                      juce::int64 endTick, bool split, bool keepRight,
                                      juce::Array<juce::var>& remove, juce::Array<juce::var>& insert) {
    const auto source = globalSource(lane);
    if (!*source.events || tick < 0 || tick >= endTick) return juce::Result::fail("Choose a global interval.");
    const auto key = source.span ? "start" : "at";
    auto left = globalEventAt(score, lane, tick);
    const bool inherited = !left.isObject() && lane == TimelineLane::key && score["song"]["key"].isObject();
    if (inherited) {
        left = new juce::DynamicObject;
        left.getDynamicObject()->setProperty("at", 0);
        left.getDynamicObject()->setProperty("key", score["song"]["key"].clone());
        left.getDynamicObject()->setProperty("id", "key-" + juce::Uuid().toString());
    }
    const bool defaultScale = !left.isObject() && lane == TimelineLane::scale && score["song"]["scale"].isObject();
    if (defaultScale) {
        const auto gap = sharedScaleGap(score, tick, {0, endTick});
        if (gap.isEmpty()) return juce::Result::fail("Choose a global interval.");
        left = score["song"]["scale"].clone();
        if (left["label"].toString().isEmpty()) left.getDynamicObject()->setProperty("label",left["id"].toString());
        left.getDynamicObject()->setProperty("id","scale-" + juce::Uuid().toString());
        left.getDynamicObject()->setProperty("start",gap.getStart());
        left.getDynamicObject()->setProperty("duration",gap.getLength());
    }
    if (!left.isObject()) return juce::Result::fail("Choose a global interval.");
    const juce::int64 start = left[key];
    juce::var next;
    auto events = score[source.group][source.events];
    if (auto* list = events.getArray()) for (const auto& other : *list) {
        if (lane == TimelineLane::scale && juce::JSON::toString(left["targets"]) != juce::JSON::toString(other["targets"])) continue;
        if (juce::int64(other[key]) > start && (!next.isObject() || juce::int64(other[key]) < juce::int64(next[key]))) next = other;
    }
    const auto end = source.span ? start + juce::int64(left["duration"]) : next.isObject() ? juce::int64(next[key]) : endTick;
    auto visited = [&](const juce::var& section) {
        if (lane != TimelineLane::section) return false;
        const auto plans = score["performancePlans"];
        if (auto* entries = plans.getArray()) for (const auto& plan : *entries)
            if (auto* visits = plan["visits"].getArray()) for (const auto& visit : *visits)
                if (auto* ids = visit["sourceSectionIds"].getArray(); ids && ids->contains(section["id"])) return true;
        return false;
    };
    if (split) {
        if (tick <= start || tick >= end) return juce::Result::fail("Choose a point inside the interval.");
        auto right = left.clone();
        right.getDynamicObject()->setProperty(key, tick);
        if (right.hasProperty("id")) right.getDynamicObject()->setProperty("id", "event-" + juce::Uuid().toString());
        if (source.span) {
            auto first = left.clone();
            first.getDynamicObject()->setProperty("duration", tick - start);
            right.getDynamicObject()->setProperty("duration", end - tick);
            if (!defaultScale) remove.add(left);
            insert.add(first);
        }
        insert.add(right);
    } else {
        if (!next.isObject() || end != juce::int64(next[key])) return juce::Result::fail("Choose an interval with an adjacent successor.");
        if (visited(left) || visited(next)) return juce::Result::fail("This section is used in a performance plan.");
        if (!inherited && !defaultScale) remove.add(left);
        remove.add(next);
        if ((!inherited && !defaultScale) || keepRight) {
            auto joined = (keepRight ? next : left).clone();
            joined.getDynamicObject()->setProperty(key, start);
            if (source.span) joined.getDynamicObject()->setProperty("duration", end + juce::int64(next["duration"]) - start);
            insert.add(joined);
        }
    }
    return juce::Result::ok();
}

inline juce::Result removeGlobalBoundary(const juce::var& score, TimelineLane lane, juce::int64 at,
                                         juce::int64 songEnd, bool keepRight,
                                         juce::Array<juce::var>& remove, juce::Array<juce::var>& insert) {
    if (at <= 0 || at >= songEnd) return juce::Result::fail("Choose an interior boundary.");
    const auto right = globalEventAt(score, lane, at);
    const auto source = globalSource(lane);
    const auto key = source.span ? "start" : "at";
    const auto left = globalEventAt(score, lane, at - 1);
    if (lane == TimelineLane::scale && left.isObject() != right.isObject()) {
        const bool gapOnRight = !right.isObject();
        const auto gap = sharedScaleGap(score, gapOnRight ? at : at - 1, {0, songEnd});
        if (gap.isEmpty() || at != (gapOnRight ? gap.getStart() : gap.getEnd()))
            return juce::Result::fail("Choose an interior boundary.");
        const auto existing = gapOnRight ? left : right;
        remove.add(existing);
        if (gapOnRight != keepRight) {
            auto extended = existing.clone();
            extended.getDynamicObject()->setProperty("start", gapOnRight ? juce::int64(existing["start"]) : gap.getStart());
            extended.getDynamicObject()->setProperty("duration", juce::int64(existing["duration"]) + gap.getLength());
            insert.add(extended);
        }
        return juce::Result::ok();
    }
    if (!right.isObject() || juce::int64(right[key]) != at)
        return juce::Result::fail("Choose an interior boundary.");
    return globalBoundaryEdit(score, lane, at - 1, songEnd, false, keepRight, remove, insert);
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
