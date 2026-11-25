#include "ScoreSchema.h"
#include <ScoreSchemas.h>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <array>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace ostfstudio {
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
