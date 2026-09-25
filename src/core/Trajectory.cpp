#include "Trajectory.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
namespace hf {
namespace {
constexpr size_t MaxSteps = 240 * 3600 + 2;
bool validSample(Sample const& s) {
    for (double value : {s.time, s.x, s.y, s.p2x, s.p2y, s.velocity1, s.velocity2, s.size1, s.size2})
        if (!std::isfinite(value) || std::abs(value) > 1e7) return false;
    return s.time >= 0 && s.time <= 3600 && s.mode1 >= 0 && s.mode1 <= 7 && s.mode2 >= 0 && s.mode2 <= 7
        && s.size1 > 0 && s.size1 <= 10 && s.size2 > 0 && s.size2 <= 10;
}
void writeSample(std::ostream& out, Sample const& s) {
    out << s.time << ' ' << s.x << ' ' << s.y << ' ' << s.p2x << ' ' << s.p2y << ' '
        << s.dual << ' ' << s.mode1 << ' ' << s.mode2 << ' ' << s.held1 << ' ' << s.held2 << ' '
        << s.velocity1 << ' ' << s.velocity2 << ' ' << s.size1 << ' ' << s.size2 << ' '
        << s.upside1 << ' ' << s.upside2 << ' ' << s.disabled1 << ' ' << s.disabled2;
}
void readSample(std::istream& in, Sample& s) {
    if (!(in >> s.time >> s.x >> s.y >> s.p2x >> s.p2y >> s.dual >> s.mode1 >> s.mode2 >> s.held1 >> s.held2
        >> s.velocity1 >> s.velocity2 >> s.size1 >> s.size2 >> s.upside1 >> s.upside2 >> s.disabled1 >> s.disabled2)
        || !validSample(s)) throw Error("CAPTURE_CACHE", "Invalid physics sample in trajectory");
}
}
uint64_t fingerprint(std::string const& text) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
std::vector<Action> traceActions(Replay const& replay, bool twoPlayer, bool warningsOnly) {
    PlanConfig cfg; cfg.twoPlayer = twoPlayer; cfg.warningsOnly = warningsOnly; plan(replay, cfg);
    std::array<bool, 2> down{false, false};
    std::vector<Action> result;
    for (auto const& a : replay.actions) {
        if (a.kind != Kind::Jump || (!twoPlayer && a.p2) || down[a.p2] == a.down) continue;
        down[a.p2] = a.down; result.push_back(a);
    }
    return result;
}
double crossingPosition(double left, double right) {
    if (!std::isfinite(left) || !std::isfinite(right) || right <= left || left < 0)
        throw Error("CAPTURE_X", "Reverse, stationary or invalid X cannot be mapped to one-pass Options");
    // Use an actual observed crossing interval, with a coordinate that survives
    // GD's one-decimal save precision. Avoid both endpoints and float roundoff.
    float x = static_cast<float>(std::round((left + (right - left) * .5) * 10.0) / 10.0);
    double margin = std::max(0.001, std::abs(right) * 2e-7);
    if (!(x > left + margin && x < right - margin))
        throw Error("CAPTURE_PRECISION", "No stable saved coordinate inside this physics-step crossing");
    return x;
}
double Trajectory::position(uint64_t frame) const {
    auto it = std::lower_bound(inputs.begin(), inputs.end(), frame,
        [](TraceInput const& a, uint64_t f) { return a.frame < f; });
    if (it == inputs.end() || it->frame != frame) throw Error("CAPTURE_FRAME", "Recorded frame is missing");
    return it->triggerX;
}
Recorder::Recorder(Replay const& replay, uint64_t hash, bool twoPlayer, bool warningsOnly)
  : m_expected(traceActions(replay, twoPlayer, warningsOnly)), m_warningsOnly(warningsOnly) {
    m_trace.macroHash = replay.fingerprint; m_trace.levelHash = hash; m_trace.twoPlayer = twoPlayer;
    m_trace.advisory = warningsOnly;
    double time = 0, tps = replay.tps; uint64_t frame = 0; size_t next = 0;
    for (auto const& a : replay.actions) {
        time += static_cast<double>(a.frame-frame) / tps; frame = a.frame;
        if (a.kind == Kind::TPS) tps = a.tps;
        if (next < m_expected.size() && a.kind == Kind::Jump && a.frame == m_expected[next].frame &&
            a.p2 == m_expected[next].p2 && a.down == m_expected[next].down) {
            m_expectedTimes.push_back(time); ++next;
        }
    }
}
void Recorder::warning(std::string const& code, std::string const& message) {
    if (!m_warningsOnly) throw Error(code, message);
    auto text = code + ": " + message;
    if (m_trace.warnings.size() < 128 && std::find(m_trace.warnings.begin(), m_trace.warnings.end(), text) == m_trace.warnings.end())
        m_trace.warnings.push_back(std::move(text));
}
Trajectory Recorder::partial() const {
    auto result = m_trace; result.advisory = true; result.completed = false;
    result.endTime = result.steps.empty() ? 0 : result.steps.back().time;
    return result;
}
void Recorder::step(Sample const& s) {
    if (!validSample(s))
        throw Error("CAPTURE_TIME", "Invalid gameplay clock/position");
    if (!m_step) {
        if (s.time > 1.5 / 240.0) warning("CAPTURE_START", "Recording starts mid-level; missing sections use timeline mapping.");
        m_step = s;
    } else if (s.time > m_step->time + 1e-7) {
        if (s.time - m_step->time > 1.5 / 240.0)
            warning("CAPTURE_STEP", "Missing physics steps or a changed clock; input phase may differ.");
        if (s.x < m_step->x) warning("CAPTURE_REVERSE", "P1 moved backwards; one-pass X triggers may be inaccurate.");
        m_previousX = m_step->x; m_hasPrevious = true; m_step = s;
    } else if (s.time < m_step->time - 1e-7) {
        warning("CAPTURE_RESET", "Clock went backwards; recording resumes as a new attempt.");
        m_trace.inputs.clear(); m_trace.steps.clear(); m_trace.sawDual = false;
        m_down = {false, false}; m_nextExpected = 0; m_hasClockOffset = false;
        m_hasPrevious = false; m_previousX = 0; m_step = s;
    }
    m_trace.sawDual |= s.dual;
    size_t i = m_warningsOnly ? m_nextExpected : m_trace.inputs.size();
    if (i < m_expected.size() && s.time > m_expectedTimes[i] + (m_hasClockOffset ? m_trace.clockOffset + .75 / 240.0 : 1.75 / 240.0))
        warning("CAPTURE_MISSING_INPUT", "Some expected inputs were not observed; missing gates use timeline mapping.");
}
bool Recorder::input(bool down, bool p2, Sample const& s) {
    if (!validSample(s)) throw Error("CAPTURE_POSITION", "Invalid player position");
    if (!m_step || std::abs(s.time - m_step->time) > 1e-7) {
        warning("CAPTURE_PHASE", "Input outside the observed queue phase; its position is approximate.");
        step(s);
    }
    if (!m_trace.twoPlayer) p2 = false;
    if (m_down[p2] == down) return false;
    size_t i = m_warningsOnly ? m_nextExpected : m_trace.inputs.size();
    if (m_warningsOnly) {
        while (i < m_expected.size() && m_expectedTimes[i] + (m_hasClockOffset ? m_trace.clockOffset : 0) < s.time-1.75/240.0) {
            warning("CAPTURE_MISSING_INPUT", "Some expected inputs were not observed; missing gates use timeline mapping."); ++i;
        }
        m_nextExpected = i;
    }
    if (i == m_expected.size()) {
        warning("CAPTURE_EXTRA_INPUT", "Unmatched input after the macro; event ignored."); return false;
    }
    auto const& expected = m_expected[i];
    double offset = s.time - m_expectedTimes[i];
    if (m_warningsOnly && (expected.down != down || expected.p2 != p2 ||
        std::abs(offset - (m_hasClockOffset ? m_trace.clockOffset : 0)) > 1.75/240.0)) {
        warning("CAPTURE_INPUT", "Unmatched input ignored; missing gates use timeline mapping."); return false;
    }
    if (!m_hasClockOffset) {
        // GD's input phase and a bot's zero-based frame counter may differ by
        // one tick. Observe that origin once; never drift-match later inputs.
        if (std::min(std::abs(offset), std::abs(offset - 1.0 / 240.0)) > .25 / 240.0)
            warning("CAPTURE_INPUT", "First input has a different clock origin; recorded timing is approximate.");
        m_trace.clockOffset = offset; m_hasClockOffset = true;
    }
    if (expected.down != down || expected.p2 != p2 || std::abs(offset - m_trace.clockOffset) > .25 / 240.0)
        warning("CAPTURE_INPUT", "Input/player/time differs from the selected macro.");
    if (expected.frame && !m_hasPrevious) warning("CAPTURE_PHASE", "Missing previous physics-step position; using current X.");
    double x = 0;
    if (expected.frame) {
        try { x = crossingPosition(m_previousX, s.x); }
        catch (Error const& e) { warning(e.code, e.what()); x = std::round(s.x*10.0)/10.0; }
    }
    if (!std::isfinite(s.y) || !std::isfinite(s.p2x) || !std::isfinite(s.p2y))
        throw Error("CAPTURE_POSITION", "Invalid player position");
    m_down[p2] = down;
    m_trace.inputs.push_back({expected.frame, down, p2, m_previousX, x, s, {}, false});
    m_nextExpected = i+1;
    return true;
}
void Recorder::stepEnd(Sample const& s) {
    if (!validSample(s)) throw Error("CAPTURE_POSITION", "Invalid post-queue physics sample");
    if (!m_step || std::abs(s.time - m_step->time) > 1e-7) {
        warning("CAPTURE_PHASE", "Post-queue phase differs; keeping approximate sample."); step(s);
    }
    if (m_trace.steps.empty() || s.time > m_trace.steps.back().time + 1e-7) {
        if (m_trace.steps.size() >= MaxSteps) throw Error("CAPTURE_LIMIT", "Physics recording exceeds one hour");
        m_trace.steps.push_back(s);
    } else if (std::abs(s.time - m_trace.steps.back().time) <= 1e-7) m_trace.steps.back() = s;
    else throw Error("CAPTURE_RESET", "Post-queue clock moved backwards");
    m_trace.sawDual |= s.dual;

    for (auto it = m_trace.inputs.rbegin(); it != m_trace.inputs.rend() && std::abs(it->sample.time - s.time) < 1e-7; ++it) {
        it->after = s; it->hasAfter = true;
    }
}
Trajectory Recorder::finish(double endTime) {
    if (m_trace.inputs.size() != m_expected.size() || m_trace.inputs.empty())
        warning("CAPTURE_INCOMPLETE", "Some macro inputs were not observed; recorded portions remain usable.");
    if (!std::isfinite(endTime) || (!m_trace.inputs.empty() && endTime < m_trace.inputs.back().sample.time) || endTime > 3600 || endTime < 0)
        throw Error("CAPTURE_END", "Invalid level completion time");
    for (auto const& r : m_trace.inputs) if (!r.hasAfter)
        warning("CAPTURE_PHASE", "Missing post-input phase snapshot; exact verification unavailable for this edge.");
    if (m_trace.steps.empty() || endTime < m_trace.steps.back().time || endTime-m_trace.steps.back().time > 1.5/240.0)
        warning("CAPTURE_INCOMPLETE", "Recording does not reach the final physics step.");
    m_trace.completed = true; m_trace.endTime = endTime; return m_trace;
}
void writeTrajectory(std::ostream& out, Trajectory const& t) {
    out.imbue(std::locale::classic());
    out << (t.advisory ? "HFTRACE4 " : "HFTRACE3 ") << std::hex << t.macroHash << ' ' << t.levelHash << std::dec << ' '
        << t.twoPlayer << ' ' << t.completed << ' ' << t.sawDual << ' ' << std::setprecision(17)
        << t.endTime << ' ' << t.clockOffset << ' ' << t.inputs.size() << ' ' << t.steps.size() << '\n';
    for (auto const& r : t.inputs) {
        out << r.frame << ' ' << r.down << ' ' << r.p2 << ' ' << r.previousX << ' ' << r.triggerX << ' ' << r.hasAfter << ' ';
        writeSample(out, r.sample); out << ' '; writeSample(out, r.after); out << '\n';
    }
    for (auto const& s : t.steps) { writeSample(out, s); out << '\n'; }
    if (t.advisory) {
        out << t.warnings.size() << '\n';
        for (auto const& w : t.warnings) out << std::quoted(w) << '\n';
    }
    if (!out) throw Error("CAPTURE_SAVE", "Failed to save recorded trajectory");
}
Trajectory readTrajectory(std::istream& in, Replay const& replay, uint64_t levelHash, bool twoPlayer) {
    in.imbue(std::locale::classic());
    Trajectory t; std::string magic; size_t count = 0, steps = 0;
    if (!(in >> magic) || (magic != "HFTRACE3" && magic != "HFTRACE4"))
        throw Error("CAPTURE_CACHE", "Old trajectory format: Record again to capture the complete dual path");
    if (magic == "HFTRACE4") {
        t.advisory = true;
        if (!(in >> std::hex >> t.macroHash >> t.levelHash >> std::dec >> t.twoPlayer >> t.completed >> t.sawDual >> t.endTime >> t.clockOffset >> count >> steps) ||
            count > MaxActions || steps > MaxSteps || !std::isfinite(t.endTime) || t.endTime < 0 || t.endTime > 3600 || !std::isfinite(t.clockOffset))
            throw Error("CAPTURE_CACHE", "Invalid trajectory header");
        if (t.macroHash != replay.fingerprint || t.levelHash != levelHash || t.twoPlayer != twoPlayer)
            throw Error("CAPTURE_IDENTITY", "Cache identity differs; use timeline mapping or record this level");
        for (size_t i = 0; i < count; ++i) {
            TraceInput r;
            if (!(in >> r.frame >> r.down >> r.p2 >> r.previousX >> r.triggerX >> r.hasAfter) ||
                !std::isfinite(r.previousX) || !std::isfinite(r.triggerX) || std::abs(r.triggerX) > 1e7 ||
                (!t.inputs.empty() && r.frame < t.inputs.back().frame))
                throw Error("CAPTURE_CACHE", "Invalid trajectory edge");
            readSample(in, r.sample); readSample(in, r.after); t.inputs.push_back(r);
        }
        for (size_t i = 0; i < steps; ++i) {
            Sample s; readSample(in, s);
            if (!t.steps.empty() && s.time <= t.steps.back().time) throw Error("CAPTURE_CACHE", "Unordered trajectory steps");
            t.steps.push_back(s);
        }
        size_t warnings = 0;
        if (!(in >> warnings) || warnings > 128) throw Error("CAPTURE_CACHE", "Invalid warning count");
        for (size_t i = 0; i < warnings; ++i) {
            std::string w;
            if (!(in >> std::quoted(w)) || w.size() > 4096) throw Error("CAPTURE_CACHE", "Invalid cached warning");
            t.warnings.push_back(std::move(w));
        }
        std::string extra; if (in >> extra) throw Error("CAPTURE_CACHE", "Trailing trajectory data");
        return t;
    }
    if (!(in >> std::hex >> t.macroHash >> t.levelHash >> std::dec >> t.twoPlayer >> t.completed >> t.sawDual >> t.endTime >> t.clockOffset >> count >> steps)
        || !t.completed || count == 0 || count > MaxActions || steps == 0 || steps > MaxSteps || !std::isfinite(t.clockOffset)
        || std::min(std::abs(t.clockOffset), std::abs(t.clockOffset - 1.0 / 240.0)) > .25 / 240.0)
        throw Error("CAPTURE_CACHE", "Invalid/incomplete trajectory cache");
    auto expected = traceActions(replay, twoPlayer);
    if (t.macroHash != replay.fingerprint || t.levelHash != levelHash || t.twoPlayer != twoPlayer || count != expected.size())
        throw Error("CAPTURE_IDENTITY", "Recorded trajectory belongs to another macro or level");
    double previousX = -1; uint64_t previousFrame = 0;
    bool sawDual = false;
    for (size_t i = 0; i < count; ++i) {
        TraceInput r;
        if (!(in >> r.frame >> r.down >> r.p2 >> r.previousX >> r.triggerX >> r.hasAfter))
            throw Error("CAPTURE_CACHE", "Truncated trajectory cache");
        readSample(in, r.sample); readSample(in, r.after);
        auto const& s = r.sample; auto const& a = expected[i];
        if (!r.hasAfter || r.after.time != s.time || r.frame != a.frame || r.down != a.down || r.p2 != a.p2 ||
            std::abs(s.time - a.frame / 240.0 - t.clockOffset) > .25 / 240.0 || !std::isfinite(r.previousX))
            throw Error("CAPTURE_CACHE", "Trajectory events do not match the macro");
        double x = r.frame ? crossingPosition(r.previousX, s.x) : 0;
        if (!std::isfinite(r.triggerX) || std::abs(x - r.triggerX) > .001 ||
            (i && r.frame != previousFrame && r.triggerX <= previousX) ||
            (i && r.frame == previousFrame && r.triggerX != previousX))
            throw Error("CAPTURE_CACHE", "Invalid trigger crossing intervals");
        previousX = r.triggerX; previousFrame = r.frame; sawDual |= s.dual;
        t.inputs.push_back(r);
    }
    t.steps.reserve(steps);
    size_t edge = 0;
    for (size_t i = 0; i < steps; ++i) {
        Sample s; readSample(in, s);
        if ((!i && s.time > 1.5 / 240.0) || (i && (s.time <= t.steps.back().time + 1e-7 ||
            s.time - t.steps.back().time > 1.5 / 240.0 || s.x < t.steps.back().x)))
            throw Error("CAPTURE_CACHE", "Missing/reversed physics steps in trajectory");
        while (edge < t.inputs.size() && t.inputs[edge].after.time <= s.time + 1e-7) {
            auto const& a = t.inputs[edge++].after;
            if (std::abs(a.time-s.time) > 1e-7 || a.x != s.x || a.y != s.y || a.p2x != s.p2x || a.p2y != s.p2y ||
                a.dual != s.dual || a.mode1 != s.mode1 || a.mode2 != s.mode2 || a.held1 != s.held1 || a.held2 != s.held2 || a.upside1 != s.upside1 || a.upside2 != s.upside2 || a.size1 != s.size1 || a.size2 != s.size2 || a.velocity1 != s.velocity1 || a.velocity2 != s.velocity2)
                throw Error("CAPTURE_CACHE", "Input and continuous trajectory disagree");
        }
        sawDual |= s.dual; t.steps.push_back(s);
    }
    std::string extra;
    if ((in >> extra) || edge != t.inputs.size() || !std::isfinite(t.endTime) || t.endTime < t.steps.back().time ||
        t.endTime > 3600 || t.endTime-t.steps.back().time > 1.5/240.0 || (sawDual && !t.sawDual))
        throw Error("CAPTURE_CACHE", "Invalid trajectory completion metadata");
    return t;
}
}
