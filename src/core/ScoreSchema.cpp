#include "ScoreSchema.h"
#include <ScoreSchemas.h>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ostfstudio {
std::string mergeScaleAssignments(std::string_view original, std::string_view edited) {
    using Json = jsoncons::ojson;
    const auto options = jsoncons::json_options{}.lossless_number(true).bignum_format(jsoncons::bignum_format_kind::raw);
    auto score = Json::parse(original, options);
    const auto proposal = Json::parse(edited, options);
    const auto& tracks = proposal.at("tracks");
    if (score.at("version") != proposal.at("version") || score.at("tracks").size() != tracks.size())
        throw std::runtime_error("The Scale Editor score no longer matches the open tracks.");
    const auto end = score.at("timeline").at("duration").as<int64_t>();
    auto checkSpan = [end](const Json& event) {
        const auto start = event.at("start").as<int64_t>(), duration = event.at("duration").as<int64_t>();
        if (start < 0 || duration <= 0 || start > end || duration > end-start)
            throw std::runtime_error("Scale range is outside the song.");
    };
    auto explicitScale = [](const Json& entry) {
        if (entry.at("type") != "improvisation") return false;
        const auto& source = entry.at("parameters").at("pitchSource");
        return source.at("type") == "explicitScale";
    };
    std::set<std::string> removed;
    const auto& incoming = proposal.at("harmony").at("scales");
    auto& shared = score.at("harmony").at("scales");
    bool changed = shared != incoming;
    if (changed) {
        std::map<std::string,const Json*> previous;
        for (const auto& event : shared.array_range()) {
            const auto id = event.at("id").as<std::string>();
            previous.emplace(id,&event);
            removed.insert(id);
        }
        struct Span { int64_t start, end; bool edited; };
        std::map<std::string,std::vector<Span>> lanes;
        for (const auto& event : incoming.array_range()) {
            checkSpan(event);
            const auto id = event.at("id").as<std::string>();
            const auto old = previous.find(id);
            const auto start = event.at("start").as<int64_t>();
            lanes[event.contains("targets") ? event.at("targets").to_string() : ""].push_back({
                start,start+event.at("duration").as<int64_t>(),old == previous.end() || *old->second != event});
            removed.erase(id);
        }
        for (auto& [targets, spans] : lanes) {
            std::sort(spans.begin(),spans.end(),[](const Span& a,const Span& b){return a.start < b.start;});
            int64_t lastEnd = 0, editedEnd = 0;
            for (const auto& span : spans) {
                if (span.start < (span.edited ? lastEnd : editedEnd))
                    throw std::runtime_error("Another scale already covers this range and target.");
                lastEnd = std::max(lastEnd,span.end);
                if (span.edited) editedEnd = span.end;
            }
        }
        shared = incoming;
    }
    const Json empty = Json::array();
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto& track = score.at("tracks").at(i);
        const auto& next = tracks.at(i);
        if (next.at("id") != track.at("id")) throw std::runtime_error("Track identities changed.");
        const auto& old = track.contains("directions") ? track.at("directions") : empty;
        const auto& edits = next.contains("directions") ? next.at("directions") : empty;
        if (old == edits) continue;
        // Non-scale directions and their original numeric precision belong to the score.
        // Reuse their original objects even after JavaScript has serialized the proposal.
        Json merged = Json::array();
        size_t originalIndex = 0;
        auto skipScales = [&] {
            while (originalIndex < old.size() && explicitScale(old.at(originalIndex))) ++originalIndex;
        };
        for (const auto& entry : old.array_range())
            if (explicitScale(entry)) removed.insert(entry.at("id").as<std::string>());
        for (const auto& entry : edits.array_range()) {
            if (explicitScale(entry)) {
                checkSpan(entry);
                merged.push_back(entry);
                removed.erase(entry.at("id").as<std::string>());
            } else {
                skipScales();
                if (originalIndex == old.size() || old.at(originalIndex).at("id") != entry.at("id"))
                    throw std::runtime_error("Non-scale track directions changed.");
                merged.push_back(old.at(originalIndex++));
            }
        }
        skipScales();
        if (originalIndex != old.size()) throw std::runtime_error("A non-scale track direction is missing.");
        if (old != merged) {
            track["directions"] = std::move(merged);
            changed = true;
        }
    }
    if (!changed) return std::string(original);
    std::string bytes;
    score.dump(bytes, options);
    for (const auto& id : removed)
        if (bytes.find(Json(id).to_string()) != std::string::npos)
            throw std::runtime_error("Another score object refers to a removed scale or direction.");
    return bytes;
}

std::string applyScoreFields(std::string_view original, std::string_view edited,
                             std::initializer_list<std::string_view> fields) {
    // A DAW edit owns only specific Core fields and one extension namespace.
    // Keep numeric tokens in the rest of the score, including opaque vendor data.
    auto options = jsoncons::json_options{}.lossless_number(true).bignum_format(jsoncons::bignum_format_kind::raw);
    auto result = jsoncons::ojson::parse(original, options);
    const auto changes = jsoncons::ojson::parse(edited, options);
    for (auto key : fields) {
        if (key == "extensions" || key == "org.ostf.studio.midi-loops" || key == "org.ostf.studio.midi-takes") {
            const std::string_view studio = key == "extensions" ? "org.ostf.studio" : key;
            key = "extensions";
            if (changes.contains(key) && changes.at(key).contains(studio))
                result[key][studio] = changes.at(key).at(studio);
            else if (result.contains(key))
                result.at(key).erase(studio);
        } else if (changes.contains(key)) {
            result[key] = changes.at(key);
        } else {
            result.erase(key);
        }
    }
    std::string bytes;
    result.dump(bytes, options);
    bytes.push_back('\n');
    return bytes;
}

void validateScoreSchema(std::string_view version, std::string_view utf8) {
    using Json = jsoncons::json;
    using Schema = jsoncons::jsonschema::json_schema<Json>;
    struct Entry {
        std::once_flag init;
        std::optional<Schema> schema;
    };
    static std::array<Entry, ScoreSchemas::namedResourceListSize> entries;
    const auto filename = "ostf-" + std::string(version) + ".schema.json";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (filename != ScoreSchemas::originalFilenames[i]) continue;
        auto& entry = entries[i];
        try {
            std::call_once(entry.init, [&] {
                int size = 0;
                const char* data = ScoreSchemas::getNamedResource(ScoreSchemas::namedResourceList[i], size);
                std::string schemaText(data, size);
                // std::regex<char> matches UTF-8 bytes. Spell U+2028/2029 as byte sequences
                // so this ECMAScript character class also compiles on MSVC and libc++.
                constexpr std::string_view separators = R"([\\r\\n\\u2028\\u2029])";
                constexpr std::string_view utf8Separators = R"((?:[\\r\\n]|\\xE2\\x80[\\xA8\\xA9]))";
                for (auto p = schemaText.find(separators); p != std::string::npos;
                     p = schemaText.find(separators, p + utf8Separators.size()))
                    schemaText.replace(p, separators.size(), utf8Separators);
                entry.schema.emplace(jsoncons::jsonschema::make_json_schema(Json::parse(schemaText)));
            });
            entry.schema->validate(Json::parse(utf8));
        } catch (const std::exception& e) {
            throw std::runtime_error("OSTF " + std::string(version) + ": " + e.what());
        }
        return;
    }
    throw std::runtime_error("Unsupported OSTF score version (supported: 3.5.0, 4.0.0, 4.1.0, 4.2.0, 4.3.0, 5.0.0, 5.1.0)");
}
}
