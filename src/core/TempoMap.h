#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <vector>

namespace ostfstudio {
// Each OSTF tempo event is effective at its tick, until the next event.
class TempoMap {
public:
    explicit TempoMap(std::int64_t ppq = 0) : ppq_(ppq) {}
    bool append(std::int64_t tick, double bpm) {
        if (ppq_ <= 0 || !std::isfinite(bpm) || bpm <= 0 || bpm > 1000
            || (points_.empty() ? tick != 0 : tick <= points_.back().tick)) return false;
        const auto seconds = secondsAt(tick);
        points_.push_back({tick, seconds, bpm, 60.0 / ppq_ / bpm});
        return true;
    }
    bool empty() const { return points_.empty(); }
    std::optional<TempoMap> scaled(double factor) const {
        TempoMap result(ppq_);
        for (const auto& p : points_)
            if (!result.append(p.tick, p.bpm * factor)) return std::nullopt;
        return result;
    }
    double secondsAt(std::int64_t tick) const {
        if (empty()) return 0;
        const auto& p = atTick(tick);
        return p.seconds + (tick - p.tick) * p.secondsPerTick;
    }
    double tickAt(double seconds) const {
        if (empty()) return 0;
        auto it = std::upper_bound(points_.begin(), points_.end(), seconds,
            [](double t, const Point& p) { return t < p.seconds; });
        const auto& p = it == points_.begin() ? *it : *std::prev(it);
        return p.tick + (seconds - p.seconds) / p.secondsPerTick;
    }
    double bpmAt(std::int64_t tick) const { return empty() ? 0.0 : atTick(tick).bpm; }
    std::optional<std::int64_t> nextChangeAfter(std::int64_t tick) const {
        const auto it = std::upper_bound(points_.begin(), points_.end(), tick,
            [](std::int64_t t, const Point& p) { return t < p.tick; });
        return it == points_.end() ? std::nullopt : std::optional(it->tick);
    }
private:
    struct Point { std::int64_t tick; double seconds, bpm, secondsPerTick; };
    const Point& atTick(std::int64_t tick) const {
        auto it = std::upper_bound(points_.begin(), points_.end(), tick,
            [](std::int64_t t, const Point& p) { return t < p.tick; });
        return it == points_.begin() ? *it : *std::prev(it);
    }
    std::int64_t ppq_;
    std::vector<Point> points_;
};
}
