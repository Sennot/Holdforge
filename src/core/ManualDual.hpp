#pragma once
#include "Trajectory.hpp"

namespace hf {
struct ManualDualResult {
    std::vector<PositionedGate> gates;
    size_t segments = 0, skipped = 0;
};
// Keep conversion outside dual. During dual, enable both controls once and
// leave the section to the author. Restore macro state at each exit.
ManualDualResult manualDualGates(std::vector<PositionedGate> const& gates,
                                Trajectory const& trace, bool allowPartial = false);
}
