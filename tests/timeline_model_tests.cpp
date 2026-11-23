#include "core/TimelineModel.h"
#include "core/MixerState.h"
#include "core/MidiGeneratorPattern.h"
#include "ui/DemoArrangement.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool value, const char* label) {
    if (!value) { std::cerr << "FAILED: " << label << '\n'; std::exit(EXIT_FAILURE); }
}

int main() {
    using namespace ostfstudio;
    check(stereoBalance(-1.0f, false) == 1.0f && stereoBalance(-1.0f, true) == 0.0f
          && stereoBalance(1.0f, false) == 0.0f && stereoBalance(0.0f, true) == 1.0f,
          "stereo pan attenuates the opposite channel");
    check(generatorPitchAtBeat(0) == 60 && generatorPitchAtBeat(1) == 64
          && generatorPitchAtBeat(2) == 67 && generatorPitchAtBeat(3) == 64
          && generatorPitchAtBeat(4) == 60 && generatorPitchAtBeat(-2) == 67,
          "shared generator repeats across the song origin");
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

    const auto whole = projectBarSpan({1, 33}, {1, 33});
    check(whole && whole->start == 0.0 && whole->end == 1.0, "whole timeline maps to full width");
    const auto clipped = projectBarSpan({1, 17}, {9, 25});
    check(clipped && clipped->start == 0.0 && clipped->end == 0.5, "partially visible region clips at left");
    const auto right = projectBarSpan({17, 33}, {9, 25});
    check(right && right->start == 0.5 && right->end == 1.0, "partially visible region clips at right");
    check(!projectBarSpan({1, 9}, {9, 17}), "touching regions do not overlap");
    check(!projectBarSpan({0, 9}, {1, 33}), "region starts at an invalid bar");
    check(!projectBarSpan({9, 9}, {1, 33}), "empty region is rejected");
    check(!projectBarSpan({12, 9}, {1, 33}), "reversed region is rejected");
    check(!projectBarSpan({1, 9}, {17, 17}), "empty display range is rejected");

    for (const auto lanes : {std::span(demo::globalLanes, std::size(demo::globalLanes)),
                             std::span(demo::trackLanes, std::size(demo::trackLanes))}) {
        for (const auto& lane : lanes) {
            int previousEnd = demo::bars.firstBar;
            for (const auto& region : lane.regions) {
                check(region.bars.firstBar >= previousEnd, "demo regions stay ordered without overlaps");
                check(region.bars.endBar <= demo::bars.endBar, "demo region ends within ruler");
                check(projectBarSpan(region.bars, demo::bars).has_value(), "demo region is visible");
                previousEnd = region.bars.endBar;
            }
        }
    }
    ChannelMix mix[3];
    check(mix[0].volumeDb == 0.0 && channelIsAudible(mix, 0), "default mixer channel at unity");
    mix[1].soloed = true;
    check(channelIsAudible(mix, 1) && !channelIsAudible(mix, 0), "solo excludes other channels");
    mix[0].soloed = true;
    check(channelIsAudible(mix, 0) && !channelIsAudible(mix, 2), "multiple channels can be soloed");
    mix[0].muted = true;
    check(!channelIsAudible(mix, 0) && channelIsAudible(mix, 1), "mute takes precedence over solo");
    check(!channelIsAudible(mix, 3), "invalid channel is inaudible");
    MixerRouting routing;
    ChannelMix routed;
    check(mixGains({&routed, 1}, 0, routing).master == 1.0f, "tracks default to Master at unity");
    routed.outputBus = 1;
    routed.returnSends[0] = 0.5f;
    routed.returnSends[1] = 1.0f;
    routing.busDb[1] = -20.0;
    routing.masterDb = -20.0;
    check(std::abs(mixGains({&routed, 1}, 0, routing).master - 0.06f) < 0.0001f, "bus and active return sum before Master");
    routing.returnCount = 2;
    routing.returnDb[1] = -20.0;
    check(std::abs(mixGains({&routed, 1}, 0, routing).master - 0.07f) < 0.0001f, "added return has an independent send and fader");
    routed.cue = true;
    routed.muted = true;
    routing.cueDb = -20.0;
    const auto cue = mixGains({&routed, 1}, 0, routing);
    check(cue.master == 0.0f && std::abs(cue.cue - 0.1f) < 0.0001f, "pre-fader cue is independent of track mute and Master");
    routing.busCount = 4;
    routing.busDb[3] = -12;
    routing.outputMuted[busOutput(3)] = true;
    mix[0].outputBus = 1;
    mix[1].outputBus = 3;
    routing.removeOutput(busOutput(1), mix);
    check(routing.busCount == 3 && mix[0].outputBus == -1 && mix[1].outputBus == 2
          && routing.busDb[2] == -12 && routing.outputMuted[busOutput(2)], "removed bus falls back to Master, later buses keep their routes");
    mix[0].returnSends[1] = 0.25f;
    routing.removeOutput(returnOutput(0), mix);
    check(routing.returnCount == 1 && mix[0].returnSends[0] == 0.25f && mix[0].returnSends[1] == 0
          && routing.returnDb[0] == -20, "deleting a return preserves the other send");
    routing.removeOutput(0, mix);
    check(routing.outputCount() == 5 && routing.outputAt(3) == busOutput(2) && routing.outputAt(4) == returnOutput(0),
          "master and output order survive removal");
    std::cout << "All timeline and mixer model tests passed.\n";
}
