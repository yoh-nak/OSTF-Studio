#pragma once

#include <string_view>
#include <string>
#include <initializer_list>

namespace ostfstudio {
// Throws a diagnostic identifying the score version and failing JSON location.
void validateScoreSchema(std::string_view version, std::string_view utf8);
std::string applyScoreFields(std::string_view original, std::string_view edited,
                             std::initializer_list<std::string_view> fields);
}
