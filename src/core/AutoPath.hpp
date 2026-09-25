#pragma once
#include "Trajectory.hpp"
#include <array>
#include <string_view>

namespace hf {
enum class AutoKind { Portal, Target, Toggle };
struct AutoObject {
    AutoKind kind = AutoKind::Portal;
    int id = 0, group = 0, target = 0;
    float x = 0, y = 0;
    bool enabled = false;
    std::string object;
};
struct AutoAnchor {
    size_t step = 0;
    double time = 0, onX = 0, offX = 0;
    float x = 0, y = 0, targetX = 0, targetY = 0;
    int mode = 0, portalGroup = 0, targetGroup = 0;
};
struct AutoPathConfig {
    int layer = 999;
    size_t maxObjects = 20000;
    // Native portal bounds after applying scale .5, checked by the editor.
    double halfWidth = 15, halfHeight = 25;
    double clearance = 6;
    bool warningsOnly = false;
};
struct AutoPath {
    std::vector<AutoObject> objects;
    std::vector<AutoAnchor> anchors;
    std::array<size_t, 8> modes{};
    size_t segments = 0;
    std::vector<std::string> warnings;
};
// Reserve all positive integer components of serialized VALUES, not just group
// membership. This also protects unused targets, remaps and sequence entries.
// Conservative false positives cost capacity, but never reuse a referenced ID.
std::array<bool, 10000> reservedIDs(std::string_view level);
AutoPath makeAutoPath(Trajectory const& trace, std::string_view original, AutoPathConfig const& config = {});
std::vector<PositionedGate> autoControlGates(std::vector<PositionedGate> const& base, AutoPath const& path);
}
