#pragma once
#include "Slc.hpp"
namespace hf {
struct PlanConfig { bool twoPlayer = false; bool strict240 = true; double offsetMs = 0; size_t maxTriggers = 50000; bool sharedP1Only = false; bool warningsOnly = false; };
struct Gate {
    uint64_t frame = 0;
    double seconds = 0;
    int p1 = 0, p2 = 0; // 0 = unchanged, +1 = block, -1 = allow.
};
struct PositionedGate { Gate gate; float x = 0; };
struct Plan {
    std::vector<Gate> gates;
    std::vector<std::string> warnings;
    size_t p1Events = 0, p2Events = 0, duplicates = 0;
    double duration = 0;
};
Plan plan(Replay const& replay, PlanConfig const& config);
}
