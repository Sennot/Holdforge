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
    float scale = .5f;
};
struct AutoPathConfig {
    int layer = 999;
    size_t maxObjects = 20000;
    // Native portal bounds after applying scale .5, checked by the editor.
    double halfWidth = 15, halfHeight = 25;
    double clearance = 6;
    bool warningsOnly = false;
    bool protectP1 = false;
};
struct AutoGap {
    double beginTime = 0, endTime = 0, beginX = 0, endX = 0;
    size_t steps = 0;
    std::string reason;
};
struct AutoPath {
    std::vector<AutoObject> objects;
    std::vector<AutoAnchor> anchors;
    std::array<size_t, 8> modes{};
    size_t segments = 0;
    size_t dualSteps = 0, eligibleSteps = 0, skippedCrossings = 0;
    size_t skippedOverlap = 0, reducedPortals = 0, skippedGroups = 0;
    size_t groupsAvailable = 0, groupsUsed = 0;
    std::vector<AutoGap> gaps; // First 64 contiguous gaps, ordered by time.
    std::vector<std::string> warnings;
};
// Reserve reference-capable values, including unknown fields, remaps and
// sequence entries. Known object IDs/coordinates/rotation/scale/editor layers
// are scalar data, not group references, and do not consume group IDs.
std::array<bool, 10000> reservedIDs(std::string_view level);
AutoPath makeAutoPath(Trajectory const& trace, std::string_view original, AutoPathConfig const& config = {});
std::vector<PositionedGate> autoControlGates(std::vector<PositionedGate> const& base, AutoPath const& path);
}
