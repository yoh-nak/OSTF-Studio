#pragma once
#include "../../instruments/knobber/Primitives.h"

namespace knobverb {
// Blend two stationary taps when the host automates pre-delay. Continuous
// automation picks up the most recent target at the end of the current fade.
struct Predelay {
  double from = -1, to = -1, phase = 1;
  knobber::Stereo read(const knobber::Delay& left, const knobber::Delay& right,
                       double l, double r, double target, double rate) {
    if (to < 0) from = to = target;
    if (phase >= 1 && target != to) {
      from = to;
      to = target;
      phase = 0;
    }
    const auto tap = [&](const knobber::Delay& delay, double input, double time) {
      return time < 1 ? input : delay.read(std::clamp(time, 1., 140000.));
    };
    knobber::Stereo value{tap(left,l,to), tap(right,r,to)};
    if (phase < 1) {
      value.left = std::lerp(tap(left,l,from),value.left,phase);
      value.right = std::lerp(tap(right,r,from),value.right,phase);
      phase = std::min(1.,phase + 1. / (rate * .020));
    }
    return value;
  }
};
}
