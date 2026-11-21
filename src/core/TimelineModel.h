#pragma once
#include <cstdint>
#include <optional>

namespace ostfstudio {
// Internal transport model only. NOT an OSTF 5.0 schema implementation.
struct MusicalPosition {
    std::int64_t bar {1};
    double beat {1.0};  // one-indexed within bar
};

struct LoopRange {
    std::int64_t firstBar {1};
    std::int64_t lastBar {8};  // inclusive
};

class TimelineModel {
public:
    explicit TimelineModel(int beatsPerBar = 4) noexcept;
    [[nodiscard]] int getBeatsPerBar() const noexcept;
    [[nodiscard]] std::optional<double> toBeats(MusicalPosition position) const noexcept;
    [[nodiscard]] bool isValid(LoopRange range) const noexcept;
    [[nodiscard]] bool contains(LoopRange range, std::int64_t bar) const noexcept;
private:
    int beatsPerBar_ {4};
};
}
