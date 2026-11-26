#pragma once
#include <functional>

namespace ostfstudio {
// frameAt maps destination offsets to decoded source frames. It is monotone
// within one pass. Start a new pass at a loop seam.
void renderPitchWarp(const float* const* input, int frames, float* const* output, int length,
                     int channels, double rate, int semitones, const std::function<double(int)>& frameAt);
}
