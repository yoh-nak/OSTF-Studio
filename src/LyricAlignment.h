#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <map>
#include <vector>

namespace ostfstudio {
using LyricStarts = std::map<juce::String, juce::int64>;

inline std::vector<juce::String> lyricIdsInTimeOrder(const juce::Array<juce::var>& ids, const LyricStarts& starts) {
    std::vector<juce::String> order;
    for (const auto& id : ids) order.push_back(id.toString());
    if (order.size() > 1 && std::all_of(order.begin(), order.end(), [&](const auto& id) { return starts.contains(id); }))
        std::stable_sort(order.begin(), order.end(), [&](const auto& a,const auto& b) { return starts.at(a) < starts.at(b); });
    return order;
}
}
