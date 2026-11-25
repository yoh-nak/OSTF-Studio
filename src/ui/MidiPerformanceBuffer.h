#pragma once
#include "ui/OstfpPreview.h"
#include <array>
#include <map>

namespace ostfstudio {
// Single writer at a time under the input monitor lock. Snapshot processing and
// score edits happen on the message thread, outside that lock.
class MidiPerformanceBuffer {
public:
    struct Edge {
        double seconds;
        juce::int64 tick, port;
        int key, channel, velocity;
    };
    void clear() { first_ = next_; }
    void push(Edge e) {
        if (next_ > first_) {
            const auto& last = data_[(next_-1) % capacity];
            if ((e.tick<0)!=(last.tick<0) || (e.tick>=0 && e.tick<last.tick)) clear();
        }
        if (e.velocity > 0) lastOnsetEnd_ = next_ + 1;
        data_[next_++ % capacity] = e;
        if (next_ - first_ > capacity) ++first_;
        while (first_ < next_ && e.seconds-data_[first_%capacity].seconds > 120.) ++first_;
    }
    bool hasNotes() const { return lastOnsetEnd_ > first_; }
    uint64_t snapshot(std::vector<Edge>& events) const {
        for (auto i=first_; i<next_; ++i) events.push_back(data_[i%capacity]);
        return next_;
    }
    void consume(uint64_t end) { first_ = std::max(first_,std::min(end,next_)); }
    static constexpr size_t capacity = 4096;
    static std::vector<PitchedNote> notes(const std::vector<Edge>& events, const TempoMap& clock,
                                          juce::int64 cursor, juce::int64 songEnd, double now, juce::int64 playingTick) {
        std::vector<PitchedNote> result;
        const auto onset = std::find_if(events.begin(),events.end(),[](const Edge& e){return e.velocity>0;});
        if (onset == events.end()) return result;
        const double anchor = clock.secondsAt(cursor);
        std::map<std::tuple<juce::int64,int,int>, Edge> held;
        auto tickAt = [&](const Edge& e) -> juce::int64 {
            return e.tick>=0 ? e.tick : std::llround(clock.tickAt(anchor+e.seconds-onset->seconds));
        };
        auto close = [&](const Edge& down, juce::int64 end) {
            const auto start = tickAt(down);
            if (start>=0 && start<songEnd) result.push_back({start,std::clamp(end,start+1,songEnd),down.key,down.velocity});
        };
        for (auto it=onset; it!=events.end(); ++it) {
            const auto id=std::make_tuple(it->port,it->channel,it->key);
            if (auto p=held.find(id); p!=held.end()) { close(p->second,tickAt(*it)); held.erase(p); }
            if (it->velocity>0) held.emplace(id,*it);
        }
        for (const auto& [key,down] : held) {
            const double elapsed = std::clamp(now-down.seconds,0.,120.);
            const auto end = down.tick>=0 && playingTick>=down.tick ? playingTick :
                juce::int64(std::llround(clock.tickAt(clock.secondsAt(tickAt(down))+elapsed)));
            close(down,end);
        }
        return result;
    }
private:
    std::vector<Edge> data_ = std::vector<Edge>(capacity);
    uint64_t first_ = 0, next_ = 0, lastOnsetEnd_ = 0;
};
}
