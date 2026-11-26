#pragma once

#include <array>
#include <cmath>
#include <span>

namespace ostfstudio {
inline constexpr std::size_t minMixerOutputsPerKind = 1;
inline constexpr std::size_t maxMixerReturns = 8;

inline constexpr std::size_t maxMixerBuses = 8;
inline constexpr std::size_t mixerOutputCapacity = 1 + maxMixerBuses + maxMixerReturns;
// Keep the output slot IDs used in existing Studio session files.
constexpr std::size_t busOutput(std::size_t bus) { return 1 + bus + (bus < 2 ? 0 : maxMixerReturns); }
constexpr std::size_t returnOutput(std::size_t send) { return 3 + send; }
constexpr bool isReturnOutput(std::size_t slot) { return slot >= 3 && slot < returnOutput(maxMixerReturns); }
constexpr std::size_t outputBusIndex(std::size_t slot) { return slot - 1 - (slot > 2 ? maxMixerReturns : 0); }
constexpr std::size_t outputReturnIndex(std::size_t slot) { return slot - 3; }

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
    std::array<bool, mixerOutputCapacity> outputMuted {};
    double masterDb = 0.0;
    double cueDb = 0.0;
    std::size_t busCount = 2;
    std::array<double, maxMixerBuses> busDb {};
    std::array<double, maxMixerReturns> returnDb {};
    std::size_t returnCount = 1;

    std::size_t outputCount() const { return 1 + busCount + returnCount; }
    std::size_t outputAt(std::size_t row) const {
        if (!row) return 0;
        return row <= busCount ? busOutput(row - 1) : returnOutput(row - 1 - busCount);
    }
    bool canRemoveOutput(std::size_t slot) const {
        return slot != 0 && hasOutput(slot)
            && (isReturnOutput(slot) ? returnCount : busCount) > minMixerOutputsPerKind;
    }
    // Remove a strip in mixer order, preserving sends and destinations of the surviving strips.
    void removeOutput(std::size_t slot, std::span<ChannelMix> channels) {
        if (!canRemoveOutput(slot)) return;
        const bool send = isReturnOutput(slot);
        const auto index = send ? outputReturnIndex(slot) : outputBusIndex(slot);
        auto& count = send ? returnCount : busCount;
        for (auto& channel : channels) {
            if (send) {
                for (auto i = index; i + 1 < count; ++i) channel.returnSends[i] = channel.returnSends[i + 1];
                channel.returnSends[count - 1] = 0;
            } else if (channel.outputBus == static_cast<int>(index)) channel.outputBus = -1;
            else if (channel.outputBus > static_cast<int>(index)) --channel.outputBus;
        }
        const auto at = send ? returnOutput : busOutput;
        for (auto i = index; i + 1 < count; ++i) {
            outputGain(at(i)) = outputGain(at(i + 1));
            outputMuted[at(i)] = outputMuted[at(i + 1)];
        }
        const auto last = at(--count);
        outputGain(last) = 0;
        outputMuted[last] = false;
    }
    double& outputGain(std::size_t slot) {
        if (slot == 0) return masterDb;
        return isReturnOutput(slot) ? returnDb[outputReturnIndex(slot)] : busDb[outputBusIndex(slot)];
    }
    bool hasOutput(std::size_t slot) const {
        return slot == 0 || (isReturnOutput(slot) ? outputReturnIndex(slot) < returnCount : outputBusIndex(slot) < busCount);
    }
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
    if (channel.outputBus >= 0 && static_cast<std::size_t>(channel.outputBus) < routing.busCount)
        paths = gainFromDb(routing.busDb[static_cast<std::size_t>(channel.outputBus)]);
    for (std::size_t i = 0; i < routing.returnCount && i < routing.returnDb.size(); ++i)
        paths += channel.returnSends[i] * gainFromDb(routing.returnDb[i]);
    return {gainFromDb(channel.volumeDb) * paths * gainFromDb(routing.masterDb), cue, channel.pan};
}
}
