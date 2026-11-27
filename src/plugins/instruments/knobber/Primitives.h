#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace knobber {
constexpr double pi = std::numbers::pi;
struct Stereo {
    double left = 0, right = 0;
    Stereo &operator+=(Stereo value) {
        left += value.left;
        right += value.right;
        return *this;
    }
};

inline double clamp(double x, double lo, double hi) { return std::clamp(x, lo, hi); }
inline double min(double a, double b) { return std::min(a, b); }
inline double max(double a, double b) { return std::max(a, b); }
inline double mix(double a, double b, double t) { return a + t * (b - a); }
inline double wrap(double x, double lo, double hi) {
    return x - (hi - lo) * std::floor((x - lo) / (hi - lo));
}
inline double mtof(double n) { return 440.0 * std::exp2((n - 69.0) / 12.0); }
inline double dbtoa(double db) { return std::pow(10.0, db / 20.0); }

struct Noise {
    uint32_t seed = 123456789u;
    double operator()() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<double>(seed) * (2.0 / 4294967296.0) - 1.0;
    }
};
struct DcBlock {
    double previous = 0, output = 0;
    double operator()(double input) {
        output = input - previous + 0.9997 * output;
        previous = input;
        return output;
    }
};

class Delay {
  public:
    explicit Delay(int capacity) : samples(static_cast<size_t>(capacity) + 1) {}
    void clear() { std::fill(samples.begin(),samples.end(),0.0); cursor=0; }
    double read(double delay) const {
        const auto whole = static_cast<size_t>(delay);
        const auto length = samples.size();
        const auto recent = (cursor + length - whole) % length;
        return mix(samples[recent], samples[(recent + length - 1) % length],
                   delay - static_cast<double>(whole));
    }
    void write(double sample) { samples[cursor] = sample; }
    void advance() {
        if (++cursor == samples.size())
            cursor = 0;
    }

  private:
    std::vector<double> samples;
    size_t cursor = 0;
};

// A symmetric 65-tap low-pass at a quarter of the internal sample rate.
// Keep both render modes aligned for host compensation and A/B comparisons.
class OutputFilter {
  public:
    static constexpr int latency = 16;
    OutputFilter() {
        double sum = 0;
        for (int i = 0; i < 65; ++i) {
            const int offset = i - 32;
            const double window = 0.42 - 0.5 * std::cos(2 * pi * i / 64) + 0.08 * std::cos(4 * pi * i / 64);
            taps[i] = (offset == 0 ? 0.5 : std::sin(pi * 0.5 * offset) / (pi * offset)) * window;
            sum += taps[i];
        }
        for (auto &tap : taps)
            tap /= sum;
    }
    Stereo downsample(Stereo even, Stereo odd) {
        history[index] = even;
        Stereo output;
        int pos = index;
        for (auto tap : taps) {
            output.left += history[pos].left * tap;
            output.right += history[pos].right * tap;
            if (--pos < 0)
                pos = 64;
        }
        index = (index + 1) % 65;
        history[index] = odd;
        index = (index + 1) % 65;
        return output;
    }
    void prime(Stereo level) {
        history.fill(level);
        base.fill(level);
    }
    Stereo delayBase(Stereo input) {
        const Stereo output = base[baseIndex];
        base[baseIndex] = input;
        baseIndex = (baseIndex + 1) % latency;
        return output;
    }

  private:
    std::array<double, 65> taps{};
    std::array<Stereo, 65> history{};
    std::array<Stereo, latency> base{};
    int index = 0, baseIndex = 0;
};
} // namespace knobber
