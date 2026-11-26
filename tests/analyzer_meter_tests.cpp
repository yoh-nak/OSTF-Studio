#include "AnalyzerMeters.h"
#include <iostream>

using namespace ostfstudio::analyzer;
namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::cerr << what << '\n';
        ++failures;
    }
}
bool near(double actual, double wanted, double tolerance) {
    return std::abs(actual - wanted) < tolerance;
}
void tone(Meters& meter, int rate, double seconds, float l, float r) {
    for (int i = 0; i < static_cast<int>(rate * seconds); ++i) {
        const auto x = static_cast<float>(std::sin(2 * std::numbers::pi * 997 * i / rate));
        meter.push(x * l, x * r);
    }
}
} // namespace
int main() {
    Meters m;
    for (int rate : {44100, 48000, 96000}) {
        m.prepare(rate, 2);
        tone(m, rate, .3, .1f, .1f);
        check(!std::isfinite(m.values.integrated), "integrated needs a complete 400 ms block");
        tone(m, rate, 4, .1f, -.1f);
        check(near(m.values.peak[0], .1, .0001) && near(m.values.rms[0], .07071, .0002), "sample peak and RMS");
        check(m.values.hasCorrelation && near(m.values.correlation, -1, .00001), "antiphase");
        check(near(m.values.integrated, -20, .12) && near(m.values.shortTerm, -20, .12) &&
                  near(m.values.momentary, -20, .12),
              "dual-mono power at reference frequency");
        tone(m, rate, 6, .001f, .001f);
        check(m.values.integrated > -21.2 && near(m.values.shortTerm, -60, .12),
              "relative gate rejects quiet blocks above the absolute gate");
        tone(m, rate, 7, .0002f, .0002f);
        check(m.values.integrated > -21.2 && m.values.momentary < -70, "gated integration excludes quiet blocks");
        m.prepare(rate, 1);
        tone(m, rate, 4, 1, 1);
        check(near(m.values.integrated, -3.01, .1) && !m.values.hasCorrelation, "mono is one channel");
        m.prepare(rate, 2);
        tone(m, rate, 3, 0, 0);
        check(m.values.maxTruePeak[0] == 0 && m.values.rms[1] == 0 && !m.values.hasCorrelation &&
                  !std::isfinite(m.values.momentary) && !std::isfinite(m.values.integrated),
              "reset and silence");
    }
    m.prepare(48000, 2);
    for (int i = 0; i < 48000; ++i) {
        // Ramp in to avoid counting a turn-on transient in the peak hold.
        const float x = static_cast<float>(std::min(i / 2400.0, 1.0) * std::sin((i + .5) * std::numbers::pi / 2));
        m.push(x, x);
    }
    check(near(m.values.peak[0], .7071, .001) && near(m.values.maxTruePeak[0], 1, .035), "intersample peak");
    check(near(m.values.correlation, 1, .0001), "in-phase stereo");
    return failures ? 1 : 0;
}
