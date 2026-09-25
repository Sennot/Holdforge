#include "AutoPath.hpp"
#include "NativeObjects.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace hf {
std::vector<PositionedGate> autoControlGates(std::vector<PositionedGate> const& base, AutoPath const& path) {
    for (size_t i=0; i<base.size(); ++i)
        if (!std::isfinite(base[i].x) || (i && base[i].x <= base[i-1].x))
            throw Error("AUTO_GATES", "Cannot order control gates for invisible helpers");
    auto result = base;
    auto boundary = [&](float x, double time, int p2) {
        auto it = std::find_if(result.begin(), result.end(), [&](auto const& g) { return g.x == x; });
        if (it != result.end()) it->gate.p2 = p2;
        else result.push_back({{0, time, 0, p2}, x});
    };
    for (size_t first=0; first<path.anchors.size();) {
        size_t last = first;
        while (last+1 < path.anchors.size() && path.anchors[last+1].step == path.anchors[last].step+1) ++last;
        auto const& a = path.anchors[first]; auto const& b = path.anchors[last];
        float on = static_cast<float>(a.onX), off = static_cast<float>(b.offX);
        if (!std::isfinite(on) || !std::isfinite(off) || on >= off || (first && on < path.anchors[first-1].offX))
            throw Error("AUTO_GATES", "Overlapping/reversed helper intervals cannot be ordered");
        // Block P2 only while actual helpers exist. A partial recording or a
        // skipped interval must not leave all later dual sections blocked.
        for (auto& g : result) if (g.x >= on && g.x < off) g.gate.p2 = 1;
        int restore = -1; // P1-only diagnostic leaves native P2 enabled.
        for (auto const& g : base) if (g.x <= off && g.gate.p2) restore = g.gate.p2;
        boundary(on, a.time, 1); boundary(off, b.time+1.0/240.0, restore);
        first = last+1;
    }
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.x < b.x; });
    return result;
}
std::array<bool, 10000> reservedIDs(std::string_view level) {
    std::array<bool, 10000> used{}; used[0] = true;
    bool value = false;
    for (size_t begin = 0; begin < level.size();) {
        auto end = level.find_first_of(",;", begin);
        if (end == std::string_view::npos) end = level.size();
        if (value) {
            unsigned n = 0; bool digit = false;
            for (size_t i = begin; i <= end; ++i) {
                if (i < end && level[i] >= '0' && level[i] <= '9') {
                    digit = true; n = std::min(10000u, n * 10 + unsigned(level[i] - '0'));
                } else {
                    if (digit && n < used.size()) used[n] = true;
                    digit = false; n = 0;
                }
            }
        }
        value = end < level.size() && level[end] != ';' ? !value : false;
        begin = end + 1;
    }
    return used;
}
namespace {
float saved(double n) {
    if (!std::isfinite(n) || std::abs(n) > 1e7) throw Error("AUTO_POSITION", "Invalid recorded dual position");
    return static_cast<float>(std::round(n * 10) / 10);
}

}
AutoPath makeAutoPath(Trajectory const& trace, std::string_view original, AutoPathConfig const& cfg) {
    AutoPath out;
    auto warn = [&](std::string const& code, std::string const& message) {
        if (!cfg.warningsOnly) throw Error(code, message);
        if (std::find(out.warnings.begin(), out.warnings.end(), message) == out.warnings.end()) out.warnings.push_back(message);
    };
    if (trace.twoPlayer) throw Error("AUTO_2P", "Invisible path helpers are for ordinary dual, not Two Player Mode");
    if (trace.steps.size() < 2 || !std::isfinite(trace.endTime))
        throw Error("AUTO_RECORD", "Insufficient recorded positions for invisible helpers");
    if (!trace.completed ||
        trace.endTime - trace.steps.back().time > 1.5 / 240.0)
        warn("AUTO_RECORD", "Partial recording: invisible helpers cover only the recorded sections.");
    if (cfg.layer < 0 || cfg.layer > 999 || !std::isfinite(cfg.clearance) || cfg.clearance < 0 ||
        !std::isfinite(cfg.halfWidth) || !std::isfinite(cfg.halfHeight) || cfg.halfWidth <= 0 || cfg.halfHeight <= 0)
        throw Error("AUTO_CONFIG", "Invalid auto helper dimensions/settings");
    if (!trace.sawDual) return out;
    auto used = reservedIDs(original); int nextGroup = 9999;
    auto group = [&]() {
        while (nextGroup > 0 && used[nextGroup]) --nextGroup;
        if (!nextGroup) throw Error("AUTO_GROUP_LIMIT", "Not enough unused group IDs for the dual trajectory; no objects created");
        used[nextGroup] = true; return nextGroup--;
    };
    auto add = [&](AutoKind kind, int id, float x, float y, int own, int target, bool enabled) {
        if (out.objects.size() >= cfg.maxObjects) warn("AUTO_OBJECT_LIMIT", "Invisible helpers exceed the configured object warning threshold.");
        AutoObject o{kind, id, own, target, x, y, enabled, {}};
        std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(9);
        s << "1," << id << ",2," << x << ",3," << y << ",20," << cfg.layer
          << ",135,1,116,1,507,1,64,1,67,1,96,1";
        if (own) s << ",57," << own;
        if (target) s << ",51," << target;
        if (kind == AutoKind::Portal) {
            // The unlinked native touch portal, NOT Teleport Trigger 3022.
            // Ignore X preserves P1's timing. Zero static force prevents the
            // disabled icon's falling velocity accumulating between anchors.
            // Gravity is intentionally kept: no implicit linked-gravity flip.
            s << ",32,0.5,11,1,62,0,87,0,352,1,353,0,351,0,345,1,346,0,347,0,354,0,443,0,510,0";
        } else if (kind == AutoKind::Target) s << ",121,1";
        else s << ",56," << (enabled ? 1 : 0) << ",11,0,62,0";
        s << ';'; o.object = s.str(); out.objects.push_back(std::move(o));
    };
    bool inDual = false;
    for (size_t i = 0; i < trace.steps.size(); ++i) {
        auto const& s = trace.steps[i];
        if (!std::isfinite(s.time) || !std::isfinite(s.x) || s.mode1 < 0 || s.mode1 > 7 || s.mode2 < 0 || s.mode2 > 7)
            throw Error("AUTO_STEPS", "Invalid physics sample");
        if (i && (s.time <= trace.steps[i-1].time || s.time-trace.steps[i-1].time > 1.5/240.0 || s.x < trace.steps[i-1].x))
            warn("AUTO_STEPS", "Gaps/reversed physics samples: invisible helpers may be inaccurate.");
        if (!s.dual) { inDual = false; continue; }
        if (!inDual) { ++out.segments; inDual = true; }
        if (!i || i+1 == trace.steps.size() || !trace.steps[i+1].dual) continue;
        auto const& n = trace.steps[i+1];
        double on, off;
        try { on = crossingPosition(trace.steps[i-1].x, s.x); off = crossingPosition(s.x, n.x); }
        catch (Error const& e) { warn(e.code, "Unmappable dual steps omitted from invisible helpers."); continue; }
        float x = saved(s.p2x), y = saved(s.p2y);
        // Physical portals cannot be assumed to honor a P2-only field. Check
        // a swept box for P1 over the full active interval, with drift margin.
        double minX = s.x, maxX = s.x, minY = s.y, maxY = s.y, radius = 0;
        for (size_t k = i-1; k <= i+1; ++k) {
            auto const& p = trace.steps[k];
            if (!std::isfinite(p.y) || !std::isfinite(p.size1) || p.size1 <= 0 || p.size1 > 10)
                throw Error("AUTO_POSITION", "Invalid P1 size/position in trajectory");
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            // Full rotating square diagonal, conservative for every mode.
            radius = std::max(radius, 22 * p.size1 + cfg.clearance);
        }
        if (x + cfg.halfWidth >= minX-radius && x-cfg.halfWidth <= maxX+radius &&
            y + cfg.halfHeight >= minY-radius && y-cfg.halfHeight <= maxY+radius)
            warn("AUTO_P1_OVERLAP", "Dual paths overlap: generated invisible portals could also catch P1; inspect these sections.");
        int pg = group();
        AutoAnchor a{i, s.time, on, off, x, y, saved(n.p2x), saved(n.p2y), s.mode2, pg, 0};
        out.anchors.push_back(a); ++out.modes[s.mode2];
        if (out.anchors.size() > cfg.maxObjects / 4)
            warn("AUTO_OBJECT_LIMIT", "Invisible helpers exceed the configured object warning threshold.");
    }
    if (out.anchors.empty()) throw Error("AUTO_NO_ANCHORS", "Dual run has no complete pair of physics samples");
    for (size_t i=0; i<out.anchors.size(); ++i) {
        auto& a = out.anchors[i];
        bool nextIsTarget = i+1 < out.anchors.size() && out.anchors[i+1].step == a.step+1;
        // The next (still disabled) portal is also the destination marker.
        // Native getPortalTarget resolves its group even while toggled off.
        // This needs one group/step, plus one inert target per segment.
        a.targetGroup = nextIsTarget ? out.anchors[i+1].portalGroup : group();
        add(AutoKind::Portal, native::unlinkedPortal, a.x, a.y, a.portalGroup, a.targetGroup, false);
        if (!nextIsTarget) add(AutoKind::Target, native::unlinkedExit, a.targetX, a.targetY, a.targetGroup, 0, false);
        add(AutoKind::Toggle, native::toggle, 0, -90, 0, a.portalGroup, false);
        add(AutoKind::Toggle, native::toggle, static_cast<float>(a.onX), -60, 0, a.portalGroup, true);
        add(AutoKind::Toggle, native::toggle, static_cast<float>(a.offX), -30, 0, a.portalGroup, false);
    }
    return out;
}
}
