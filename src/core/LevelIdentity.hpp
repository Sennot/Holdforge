#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace hf {
// Object order, unknown properties and gameplay values remain significant.
// Only field ordering, decimal spelling of X/Y/rotation and editor-only
// layers/color-page metadata are normalized. Duplicate keys stay byte-exact.
std::string canonicalLevel(std::string_view level);
uint64_t levelFingerprint(std::string_view level);
bool sameLevelData(std::string_view a, std::string_view b);

enum class EditorSession { Unselected, Source, Generated, Changed, Other };
EditorSession classifyEditorSession(bool hasMacro, bool sameLevel, bool sameMode,
    std::string_view source, std::string_view generated, std::string_view current);

struct LevelDifference {
    bool equal = true;
    size_t record = 0; // 0 = header, 1+ = object index
    std::string property, expected, actual;
};
// Bounded first difference for diagnostics; never exports a whole level.
LevelDifference firstLevelDifference(std::string_view expected, std::string_view actual);
}
