#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <set>

namespace ostfstudio::scoreimport {
using Tick = juce::int64;
using List = juce::Array<juce::var>;
inline juce::var obj(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields) {
    auto o = std::make_unique<juce::DynamicObject>();
    for (const auto &[key, value] : fields)
        o->setProperty(key, value);
    return juce::var(o.release());
}
struct Part {
    juce::String name;
    int channel = 1;
    List notes;
    juce::String role;
    List midiEvents;
};
struct ImportSong {
    int ppq = 480, beats = 4, beatType = 4, count = 0;
    Tick length = 1;
    std::map<Tick, double> tempos{{0, 120.0}};
    std::map<Tick, std::pair<int, int>> meters{{0, {4, 4}}};
    std::map<Tick, juce::var> keys;
    std::set<int> drums;
    std::vector<Part> parts;
    juce::String title;
    List documents, phrases, units, alignments;
};
juce::Result readMusicXml(const juce::File &, ImportSong &, List &chords);
} // namespace ostfstudio::scoreimport
