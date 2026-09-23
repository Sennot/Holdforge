#include "Plan.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace hf {
Plan plan(Replay const& replay, PlanConfig const& config) {
    if (!std::isfinite(config.offsetMs) || std::abs(config.offsetMs) > 1000)
        throw Error("PLAN_OFFSET", "Offset must be finite and within +/-1000 ms");
    if (replay.actions.empty()) throw Error("PLAN_EMPTY", "Macro contains no actions");
    Plan out;
    double tps = replay.tps, seconds = 0;
    uint64_t previousFrame = 0;
    std::array<bool, 2> down{false, false};
    std::array<uint64_t, 2> lastChange{UINT64_MAX, UINT64_MAX};
    std::array<std::vector<std::pair<uint64_t, bool>>, 2> streams;
    for (auto const& a : replay.actions) {
        if (a.kind == Kind::Jump) streams[a.p2].emplace_back(a.frame, a.down);
    }
    if (!config.twoPlayer && !streams[1].empty() && streams[0] != streams[1])
        throw Error("PLAN_P2_SHARED", "Independent P2 stream requires Two Player Mode in level settings");
    if (streams[0].empty() && streams[1].empty()) throw Error("PLAN_NO_JUMP", "Macro contains no jump input");
    auto checkTps = [&](double value) {
        if (!std::isfinite(value) || value < 1 || value > 1000000) throw Error("PLAN_TPS", "Invalid TPS");
        if (std::abs(value - 240) > 0.00001) {
            if (config.strict240) throw Error("PLAN_TPS", "Macro is not 240 TPS. Disable Strict 240 TPS only for an approximate conversion.");
            if (out.warnings.empty()) out.warnings.push_back("Non-240 TPS: timestamps are converted, but physics equivalence is not guaranteed.");
        }
    };
    checkTps(tps);
    out.gates.push_back({0, 0, 1, 1});
    for (auto const& a : replay.actions) {
        if (a.frame < previousFrame) throw Error("PLAN_ORDER", "Non-monotonic frame sequence");
        seconds += static_cast<double>(a.frame - previousFrame) / tps;
        previousFrame = a.frame;
        if (!std::isfinite(seconds) || seconds > 3600) throw Error("PLAN_DURATION", "Macro exceeds one hour");
        if (a.kind == Kind::TPS) { checkTps(a.tps); tps = a.tps; continue; }
        if (a.kind == Kind::Skip) continue;
        if (a.kind == Kind::Left || a.kind == Kind::Right)
            throw Error("PLAN_PLATFORMER", "Left/right inputs cannot be represented by a single hold gate");
        if (a.kind != Kind::Jump)
            throw Error("PLAN_SPECIAL", "Restart, death or bugpoint found. Export a single clean attempt.");
        if (!config.twoPlayer && a.p2) continue; // Verified identical shared stream above.
        a.p2 ? ++out.p2Events : ++out.p1Events;
        size_t p = a.p2 ? 1 : 0;
        if (down[p] == a.down) { ++out.duplicates; continue; }
        if (lastChange[p] == a.frame) throw Error("PLAN_SAME_FRAME", "Press/release on the same frame (swift) cannot be represented reliably by Options Triggers");
        lastChange[p] = a.frame; down[p] = a.down;
        double time = seconds + config.offsetMs / 1000;
        if (time < 0) throw Error("PLAN_NEGATIVE", "Offset moves an input before level start");
        Gate gate{a.frame, time, 0, 0};
        int state = a.down ? -1 : 1;
        if (p == 0) { gate.p1 = state; if (!config.twoPlayer) gate.p2 = state; }
        else gate.p2 = state;
        if (out.gates.back().seconds == time) {
            auto& prev = out.gates.back();
            if (gate.p1) prev.p1 = gate.p1;
            if (gate.p2) prev.p2 = gate.p2;
        } else out.gates.push_back(gate);
        if (out.gates.size() > config.maxTriggers) throw Error("PLAN_LIMIT", "Generated trigger count exceeds configured limit");
    }
    out.duration = out.gates.back().seconds;
    if (config.twoPlayer && streams[1].empty()) out.warnings.push_back("No P2 inputs: P2 controls stay blocked throughout this conversion.");
    if (down[0] || down[1]) out.warnings.push_back("Macro ends while held: final controls remain enabled, matching the macro.");
    out.warnings.push_back("Editor timeline mapping requires an in-game hold test. Offset may need calibration.");
    return out;
}
}
