#pragma once

#include "core/TimelineModel.h"
#include <span>

namespace ostfstudio {
// Presentation data. The source owns all labels and regions while the view is in use.
struct ArrangementRegion {
    BarSpan bars;
    const char* label;
};

struct ArrangementLane {
    const char* label;
    std::span<const ArrangementRegion> regions;
};

struct ArrangementView {
    BarSpan bars;
    std::span<const ArrangementLane> globalLanes;
    std::span<const ArrangementLane> trackLanes;
};
}
