#include "ManualDual.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hf {
ManualDualResult manualDualGates(std::vector<PositionedGate> const& gates, Trajectory const& trace, bool allowPartial) {
    if (trace.twoPlayer) throw Error("MANUAL_2P", "Manual dual mode is only for ordinary dual");
    if ((!trace.completed && !allowPartial) || trace.steps.empty())
        throw Error("MANUAL_RECORD", "Record a complete trajectory to locate dual sections");
    for (size_t i = 0; i < gates.size(); ++i) {
        auto const& g = gates[i];
        if (!std::isfinite(g.x) || (i && g.x <= gates[i-1].x) ||
            g.gate.p1 < -1 || g.gate.p1 > 1 || g.gate.p2 < -1 || g.gate.p2 > 1)
            throw Error("MANUAL_GATES", "Invalid or unordered control gates");
    }
    struct Boundary { float x; double time; bool entering; };
    std::vector<Boundary> boundaries;
    bool dual = false;
    for (size_t i = 0; i < trace.steps.size(); ++i) {
        auto const& s = trace.steps[i];
        if (!std::isfinite(s.time) || !std::isfinite(s.x) ||
            (i && (s.time <= trace.steps[i-1].time || s.time-trace.steps[i-1].time > 1.5/240.0 || s.x < trace.steps[i-1].x)))
            throw Error("MANUAL_RECORD", "Invalid trajectory step in manual dual mode");
        if (s.dual != dual) {
            boundaries.push_back({i ? static_cast<float>(crossingPosition(trace.steps[i-1].x, s.x)) : 0.f, s.time, s.dual});
            dual = s.dual;
        }
    }
    // Do not erase unobserved tail gates when an interrupted capture ends in
    // dual. Only the known interval is available for manual construction.
    if (allowPartial && !trace.completed && dual) {
        auto const& last = trace.steps.back();
        boundaries.push_back({static_cast<float>(last.x), last.time, false});
    }
    ManualDualResult out;
    size_t edge = 0;
    int p1 = 1, p2 = 1;
    dual = false;
    auto consume = [&](PositionedGate const& g) {
        if (g.gate.p1) p1 = g.gate.p1;
        if (g.gate.p2) p2 = g.gate.p2;
    };
    for (auto const& b : boundaries) {
        while (edge < gates.size() && gates[edge].x < b.x) {
            auto const& g = gates[edge++]; consume(g);
            if (!dual) out.gates.push_back(g); else ++out.skipped;
        }
        // The boundary replaces any same-position macro gate. At an exit it
        // restores the state AFTER those inputs, including a press on the exit.
        while (edge < gates.size() && gates[edge].x == b.x) {
            consume(gates[edge++]); ++out.skipped;
        }
        dual = b.entering;
        if (dual) ++out.segments;
        auto frame = static_cast<uint64_t>(std::llround(std::max(0.0, b.time-trace.clockOffset)*240));
        out.gates.push_back({{frame, b.time, dual ? -1 : p1, dual ? -1 : p2}, b.x});
    }
    while (edge < gates.size()) {
        auto const& g = gates[edge++];
        if (!dual) out.gates.push_back(g); else ++out.skipped;
    }
    return out;
}
}
