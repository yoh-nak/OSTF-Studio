#include "core/TimelineModel.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool value, const char* label) {
    if (!value) { std::cerr << "FAILED: " << label << '\n'; std::exit(EXIT_FAILURE); }
}

int main() {
    using namespace ostfstudio;
    TimelineModel model {4};
    check(model.getBeatsPerBar() == 4, "default meter");
    check(model.toBeats({1, 1.0}).value_or(-1) == 0.0, "bar one origin");
    check(model.toBeats({9, 1.0}).value_or(-1) == 32.0, "bar nine origin");
    check(!model.toBeats({0, 1.0}).has_value(), "bar zero invalid");
    check(!model.toBeats({1, 5.0}).has_value(), "out of range beat invalid");
    check(model.toBeats({2, 4.5}).value_or(-1) == 7.5, "fractional beat before next bar");
    check(!model.toBeats({1, 0.5}), "beat before bar start invalid");
    check(!model.toBeats({1, std::numeric_limits<double>::quiet_NaN()}), "NaN beat invalid");
    check(!model.toBeats({1, std::numeric_limits<double>::infinity()}), "infinite beat invalid");
    check(TimelineModel{3}.toBeats({3, 2.5}).value_or(-1) == 7.5, "custom fixed meter");
    check(TimelineModel{0}.getBeatsPerBar() == 4, "invalid meter uses default");
    check(model.isValid({9, 16}), "valid loop range");
    check(!model.isValid({16, 9}), "reverse range invalid");
    check(model.contains({9, 16}, 16), "end inclusive");
    check(!model.contains({9, 16}, 17), "outside loop");
    check(model.contains({9, 9}, 9), "single-bar loop");
    check(!model.contains({0, 8}, 1), "invalid loop excludes all bars");
    std::cout << "All timeline model tests passed.\n";
}
