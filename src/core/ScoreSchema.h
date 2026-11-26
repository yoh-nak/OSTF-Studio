#pragma once

#include <string_view>
#include <string>
#include <initializer_list>

namespace ostfstudio {
// Throws a diagnostic identifying the score version and failing JSON location.
void validateScoreSchema(std::string_view version, std::string_view utf8);
// Import timed harmony scales and explicit improvisation scales on their original tracks.
// Returns the original bytes when there is no change.
std::string mergeScaleAssignments(std::string_view original, std::string_view edited);
std::string applyScoreFields(std::string_view original, std::string_view edited,
                             std::initializer_list<std::string_view> fields);
}
