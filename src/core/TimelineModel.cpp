#include "core/TimelineModel.h"
#include <cmath>

namespace ostfstudio {
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
