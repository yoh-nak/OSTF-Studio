#pragma once

#include "core/TimelineModel.h"
#include <span>

namespace ostfstudio {
enum class TimelineLane { none, section, chord, voicing, key, scale, meter, tempo };
// Presentation data. The source owns all labels and regions while the view is in use.
struct ArrangementRegion {
    BarSpan bars;
    const char* label;
    int noteCount = 0;
};

struct ArrangementLane {
    const char* label;
    std::span<const ArrangementRegion> regions;
    TimelineLane kind = TimelineLane::none;
};

struct ArrangementView {
    BarSpan bars;
    std::span<const ArrangementLane> globalLanes;
    std::span<const ArrangementLane> trackLanes;
};
}
