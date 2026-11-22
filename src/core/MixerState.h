#pragma once

#include <array>
#include <cmath>
#include <span>

namespace ostfstudio {
inline constexpr std::size_t maxMixerReturns = 8;

struct ChannelMix {
    double volumeDb = 0.0;
    bool muted = false;
    bool soloed = false;
    int outputBus = -1; // -1 routes directly to Master.
    std::array<float, maxMixerReturns> returnSends {};
    bool cue = false;
    float pan = 0.0f;
};

struct MixerRouting {
    std::array<bool, 3 + maxMixerReturns> outputMuted {};
    double masterDb = 0.0;
    double cueDb = 0.0;
    std::array<double, 2> busDb {};
    std::array<double, maxMixerReturns> returnDb {};
    std::size_t returnCount = 1;
};

struct MixGains {
    float master = 0.0f;
    float cue = 0.0f;
    float pan = 0.0f;
};

inline float stereoBalance(float pan, bool right) {
    const float attenuated = right ? -pan : pan;
    return attenuated > 0.0f ? 1.0f - attenuated : 1.0f;
}

inline bool channelIsAudible(std::span<const ChannelMix> channels, std::size_t index) {
    if (index >= channels.size() || channels[index].muted) return false;
    if (channels[index].soloed) return true;
    for (const auto& channel : channels)
        if (channel.soloed) return false;
    return true;
}

inline float gainFromDb(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }

inline MixGains mixGains(std::span<const ChannelMix> channels, std::size_t index, const MixerRouting& routing) {
    if (index >= channels.size()) return {};
    const auto& channel = channels[index];
    const auto cue = channel.cue ? gainFromDb(routing.cueDb) : 0.0f;
    if (!channelIsAudible(channels, index)) return {0.0f, cue, channel.pan};

    float paths = 1.0f;
    if (channel.outputBus >= 0 && static_cast<std::size_t>(channel.outputBus) < routing.busDb.size())
        paths = gainFromDb(routing.busDb[static_cast<std::size_t>(channel.outputBus)]);
    for (std::size_t i = 0; i < routing.returnCount && i < routing.returnDb.size(); ++i)
        paths += channel.returnSends[i] * gainFromDb(routing.returnDb[i]);
    return {gainFromDb(channel.volumeDb) * paths * gainFromDb(routing.masterDb), cue, channel.pan};
}
}
