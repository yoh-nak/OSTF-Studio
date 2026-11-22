#include "core/TimelineModel.h"
#include <algorithm>
#include <cmath>

namespace ostfstudio {
std::optional<TimelineExtent> projectBarSpan(BarSpan span, BarSpan visible) noexcept {
    if (span.firstBar < 1 || visible.firstBar < 1 || visible.endBar <= visible.firstBar)
        return std::nullopt;

    const auto start = std::max(span.firstBar, visible.firstBar);
    const auto end = std::min(span.endBar, visible.endBar);
    if (end <= start)
        return std::nullopt;

    const auto width = static_cast<double>(visible.endBar - visible.firstBar);
    return TimelineExtent {(start - visible.firstBar) / width, (end - visible.firstBar) / width};
}

TimelineModel::TimelineModel(int beatsPerBar) noexcept
    : beatsPerBar_(beatsPerBar > 0 ? beatsPerBar : 4) {}

int TimelineModel::getBeatsPerBar() const noexcept { return beatsPerBar_; }

std::optional<double> TimelineModel::toBeats(MusicalPosition p) const noexcept {
    if (p.bar < 1 || !std::isfinite(p.beat) || p.beat < 1.0 || p.beat >= static_cast<double>(beatsPerBar_) + 1.0)
        return std::nullopt;
    return static_cast<double>(p.bar - 1) * beatsPerBar_ + (p.beat - 1.0);
}

bool TimelineModel::isValid(LoopRange r) const noexcept {
    return r.firstBar >= 1 && r.lastBar >= r.firstBar;
}

bool TimelineModel::contains(LoopRange r, std::int64_t bar) const noexcept {
    return isValid(r) && bar >= r.firstBar && bar <= r.lastBar;
}
}
