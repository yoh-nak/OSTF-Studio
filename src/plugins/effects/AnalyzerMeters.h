#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace ostfstudio::analyzer {
constexpr int fftSize = 4096;
constexpr double noLevel = -std::numeric_limits<double>::infinity();
inline double powerLufs(double power) {
    return power > 0 ? -0.691 + 10 * std::log10(power) : noLevel;
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process(double x) noexcept {
        const auto y = x * b0 + z1;
        z1 = x * b1 - y * a1 + z2;
        z2 = x * b2 - y * a2;
        return y;
    }
};

struct KWeighting {
    Biquad shelf, highpass;
    void prepare(double rate) {
        // Parametrise the BS.1770 48 kHz K-weighting response for the host rate.
        auto k = std::tan(std::numbers::pi * 1681.974450955533 / rate);
        const auto gain = std::pow(10.0, 3.999843853973347 / 20);
        const auto middle = std::pow(gain, 0.4996667741545416);
        auto q = 0.7071752369554196;
        auto denominator = 1 + k / q + k * k;
        shelf = {(gain + middle * k / q + k * k) / denominator, 2 * (k * k - gain) / denominator,
                 (gain - middle * k / q + k * k) / denominator, 2 * (k * k - 1) / denominator,
                 (1 - k / q + k * k) / denominator};
        k = std::tan(std::numbers::pi * 38.13547087602444 / rate);
        q = 0.5003270373238773;
        denominator = 1 + k / q + k * k;
        highpass = {1, -2, 1, 2 * (k * k - 1) / denominator, (1 - k / q + k * k) / denominator};
    }
    double process(double x) { return highpass.process(shelf.process(x)); }
};

class TruePeak {
public:
    float process(float sample) {
        // 4-phase FIR from ITU-R BS.1770-5 Annex 2. Phases 2/3 mirror 1/0.
        static constexpr float p0[]{.001708984375f,   .010986328125f,  -.0196533203125f, .033203125f,
                                    -.0594482421875f, .1373291015625f, .97216796875f,    -.102294921875f,
                                    .047607421875f,   -.026611328125f, .014892578125f,   -.00830078125f};
        static constexpr float p1[]{-.0291748046875f, .029296875f,      -.0517578125f,   .089111328125f,
                                    -.16650390625f,   .465087890625f,   .77978515625f,   -.2003173828125f,
                                    .1015625f,        -.0582275390625f, .0330810546875f, -.0189208984375f};
        index = index == 0 ? 11 : index - 1;
        delay[index] = delay[index + 12] = sample;
        float a = 0, b = 0, c = 0, d = 0;
        for (int i = 0; i < 12; ++i) {
            const float x = delay[index + i];
            a += x * p0[i];
            b += x * p1[i];
            c += x * p1[11 - i];
            d += x * p0[11 - i];
        }
        return std::max({std::abs(sample), std::abs(a), std::abs(b), std::abs(c), std::abs(d)});
    }

private:
    std::array<float, 24> delay{};
    int index = 0;
};

struct Readings {
    std::array<float, 2> peak{}, rms{}, currentTruePeak{}, maxTruePeak{};
    double correlation = 0;
    bool hasCorrelation = false;
    double momentary = noLevel, shortTerm = noLevel, integrated = noLevel;
};

// Meter clocks are in samples. Their history continues while the editor is closed or frozen.
class Meters {
public:
    void prepare(double rate, int channels) {
        stereo = channels == 2;
        hop = std::max(1, static_cast<int>(std::round(rate / 10)));
        for (auto& filter : filters)
            filter.prepare(rate);
        truePeak = {};
        values = {};
        totalEnergy = 0;
        count = 0;
        sum = {};
        cross = 0;
        peaks = {};
        truePeaksInHop = {};
        hopEnergy = {};
        cursor = 0;
        filled = 0;
        histogram = {};
        aboveGatePower = 0;
        aboveGateCount = 0;
    }

    void push(float l, float r) noexcept {
        const std::array<float, 2> input{l, r};
        for (int c = 0; c < (stereo ? 2 : 1); ++c) {
            const double weighted = filters[c].process(input[c]);
            totalEnergy += weighted * weighted;
            sum[c] += static_cast<double>(input[c]) * input[c];
            peaks[c] = std::max(peaks[c], std::abs(input[c]));
            const float peak = truePeak[c].process(input[c]);
            truePeaksInHop[c] = std::max(truePeaksInHop[c], peak);
            values.maxTruePeak[c] = std::max(values.maxTruePeak[c], peak);
        }
        cross += static_cast<double>(l) * r;
        if (++count == hop)
            finishHop();
    }

    Readings values;

private:
    struct Bin {
        double energy = 0;
        std::uint64_t count = 0;
    };
    // Exact sums per bin and a bounded 0.1 LU approximation of the relative gate.
    std::array<Bin, 1101> histogram{};
    double aboveGatePower = 0;
    std::uint64_t aboveGateCount = 0;
    std::array<double, 30> hopEnergy{};
    std::array<double, 2> sum{};
    std::array<float, 2> peaks{}, truePeaksInHop{};
    std::array<KWeighting, 2> filters{};
    std::array<TruePeak, 2> truePeak{};
    double totalEnergy = 0, cross = 0;
    bool stereo = true;
    int count = 0, hop = 4800, cursor = 0, filled = 0;

    double trailing(int n) const {
        double result = 0;
        for (int i = 1; i <= n; ++i)
            result += hopEnergy[(cursor + 30 - i) % 30];
        return result / n;
    }
    static int bin(double lufs) { return std::clamp(static_cast<int>(std::floor((lufs + 70) * 10)), 0, 1100); }
    void finishHop() noexcept {
        values.peak = peaks;
        values.currentTruePeak = truePeaksInHop;
        truePeaksInHop = {};
        for (int c = 0; c < 2; ++c)
            values.rms[c] = static_cast<float>(std::sqrt(sum[c] / hop));
        const double denominator = std::sqrt(sum[0] * sum[1]);
        values.hasCorrelation = stereo && denominator > 1e-16;
        values.correlation = values.hasCorrelation ? std::clamp(cross / denominator, -1.0, 1.0) : 0;
        hopEnergy[cursor] = totalEnergy / hop;
        cursor = (cursor + 1) % 30;
        filled = std::min(filled + 1, 30);
        if (filled >= 4) {
            const double power = trailing(4);
            values.momentary = powerLufs(power);
            if (values.momentary > -70) {
                auto& bucket = histogram[bin(values.momentary)];
                bucket.energy += power;
                ++bucket.count;
                aboveGatePower += power;
                ++aboveGateCount;
                const int threshold = bin(powerLufs(aboveGatePower / aboveGateCount) - 10);
                double energy = aboveGatePower;
                auto blocks = aboveGateCount;
                for (int i = 0; i < threshold; ++i) {
                    energy -= histogram[i].energy;
                    blocks -= histogram[i].count;
                }
                values.integrated = blocks ? powerLufs(energy / blocks) : noLevel;
            }
        }
        if (filled == 30)
            values.shortTerm = powerLufs(trailing(30));
        count = 0;
        sum = {};
        peaks = {};
        cross = 0;
        totalEnergy = 0;
    }
};
} // namespace ostfstudio::analyzer
