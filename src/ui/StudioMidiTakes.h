#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <vector>

namespace ostfstudio {
// Studio authoring data. The standard enabled state on each note carries the
// adopted performance to readers that do not understand this extension.
struct MidiTakeSpan {
    juce::Range<juce::int64> ticks;
    int adopted = 1;
};
struct StudioMidiTakes {
    static constexpr auto extension = "org.ostf.studio.midi-takes";
    int count = 1;
    std::vector<MidiTakeSpan> spans;
    std::map<juce::String,int> owners;
    std::set<juce::String> suppressed;

    int takeOf(const juce::String& note) const {
        const auto it = owners.find(note);
        return it == owners.end() ? 1 : it->second;
    }
    int adoptedAt(juce::int64 tick) const {
        for (const auto& span : spans) if (span.ticks.contains(tick)) return span.adopted;
        return 1;
    }
    void adopt(juce::Range<juce::int64> range, int number) {
        std::vector<MidiTakeSpan> next;
        for (auto span : spans) {
            if (!span.ticks.intersects(range)) { next.push_back(span); continue; }
            if (span.ticks.getStart() < range.getStart()) next.push_back({{span.ticks.getStart(),range.getStart()},span.adopted});
            if (span.ticks.getEnd() > range.getEnd()) next.push_back({{range.getEnd(),span.ticks.getEnd()},span.adopted});
        }
        if (number!=1) next.push_back({range,number});
        std::sort(next.begin(),next.end(),[](const auto& a,const auto& b){return a.ticks.getStart()<b.ticks.getStart();});
        spans.clear();
        for (const auto& span : next) {
            if (!spans.empty() && spans.back().adopted==span.adopted && spans.back().ticks.getEnd()==span.ticks.getStart())
                spans.back().ticks.setEnd(span.ticks.getEnd());
            else spans.push_back(span);
        }
    }
    void project(juce::var track) {
        std::set<juce::String> seen, nextSuppressed;
        if (auto* clips=track["clips"].getArray()) for (const auto& clip:*clips) {
            if (auto* notes=clip["notes"].getArray()) for (auto note:*notes) {
                const auto id=note["id"].toString();
                seen.insert(id);
                const bool enabled=suppressed.contains(id) || bool(note.getProperty("enabled",true));
                const auto tick=juce::int64(clip["start"])+juce::int64(note["start"]);
                if (enabled && takeOf(id)!=adoptedAt(tick)) {
                    note.getDynamicObject()->setProperty("enabled",false);
                    nextSuppressed.insert(id);
                } else if (suppressed.contains(id)) {
                    note.getDynamicObject()->setProperty("enabled",true);
                }
            }
        }
        suppressed.swap(nextSuppressed);
        std::erase_if(owners,[&](const auto& pair){return !seen.contains(pair.first);});
    }
    juce::var encode() const {
        juce::var data(new juce::DynamicObject()), ids(new juce::DynamicObject());
        juce::Array<juce::var> ranges, muted;
        for (const auto& span:spans)
            ranges.add(juce::Array<juce::var>{span.ticks.getStart(),span.ticks.getEnd(),span.adopted});
        for (const auto& [id,n]:owners) ids.getDynamicObject()->setProperty(id,n);
        for (const auto& id:suppressed) muted.add(id);
        auto* obj=data.getDynamicObject();
        obj->setProperty("count",count); obj->setProperty("ranges",ranges);
        obj->setProperty("notes",ids); obj->setProperty("suppressed",muted);
        return data;
    }
    static StudioMidiTakes decode(const juce::var& data) {
        StudioMidiTakes takes;
        takes.count=std::clamp(int(data["count"]),1,999);
        if (auto* ranges=data["ranges"].getArray()) for (const auto& r:*ranges)
            if (r.isArray() && r.size()==3 && juce::int64(r[0])>=0 && juce::int64(r[1])>juce::int64(r[0]) && int(r[2])>1 && int(r[2])<=takes.count)
                takes.spans.push_back({{juce::int64(r[0]),juce::int64(r[1])},int(r[2])});
        if (auto* notes=data["notes"].getDynamicObject()) for (const auto& p:notes->getProperties())
            if (int(p.value)>1 && int(p.value)<=takes.count) takes.owners[p.name.toString()]=int(p.value);
        if (auto* muted=data["suppressed"].getArray()) for (const auto& id:*muted) takes.suppressed.insert(id.toString());
        return takes;
    }
};
struct TakeSelection {
    size_t track;
    juce::Range<juce::int64> ticks;
    int number;
};
}
