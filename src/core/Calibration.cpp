#include "Calibration.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>

namespace hf {
namespace {
bool closeEnough(double a, double b) { return std::abs(a - b) <= 0.05; }
struct Expected { uint64_t frame; bool down, p2; };
std::vector<Expected> expectedRows(Replay const& replay, bool twoPlayer) {
    std::array<bool, 2> down{false, false};
    std::vector<Expected> out;
    for (auto const& a : replay.actions) {
        if (a.kind != Kind::Jump || (!twoPlayer && a.p2)) continue;
        size_t p = a.p2 ? 1 : 0;
        if (down[p] == a.down) continue;
        down[p] = a.down;
        if (a.frame) out.push_back({a.frame, a.down, a.p2});
    }
    return out;
}
}

Calibration readCalibration(std::istream& in, Replay const& replay) {
    Calibration out;
    std::string magic, phase;
    size_t count = 0;
    int twoPlayer = 0, complete = 0;
    if (!(in >> magic >> std::hex >> out.macroHash >> out.levelHash >> std::dec >> twoPlayer >> complete >> count >> phase) ||
        magic != "HFTRACE2" || (twoPlayer != 0 && twoPlayer != 1) || complete != 1 || count > MaxActions ||
        phase != "previous-step-midpoint")
        throw Error("TRACE_HEADER", "Invalid/incomplete automatic trace. Record a new full attempt with HoldForge.");
    out.twoPlayer = twoPlayer != 0;
    if (out.macroHash != replay.fingerprint)
        throw Error("TRACE_MACRO", "Recorded trace belongs to a different macro");
    if (std::abs(replay.tps - 240.0) > 0.00001)
        throw Error("TRACE_INPUT", "Automatic trace currently requires the original clean 240 TPS macro");

    auto expected = expectedRows(replay, out.twoPlayer);
    if (count != expected.size())
        throw Error("TRACE_INPUT", "Automatic trace does not contain exactly the macro input edges");

    uint64_t previousStep = 0, previousFrame = 0;
    double previousTrigger = -std::numeric_limits<double>::infinity();
    std::optional<double> stepsPerMacroFrame;
    for (size_t i = 0; i < count; ++i) {
        RecordedInput row{};
        int down = 0, p2 = 0, dual = 0;
        if (!(in >> row.frame >> down >> p2 >> row.step >> row.inputX >> row.inputY >>
              row.phasePreX >> row.phasePreY >> row.phasePostX >> row.phasePostY >> row.triggerX >> dual) ||
            (down != 0 && down != 1) || (p2 != 0 && p2 != 1) || (dual != 0 && dual != 1) ||
            !std::isfinite(row.inputX) || !std::isfinite(row.inputY) ||
            !std::isfinite(row.phasePreX) || !std::isfinite(row.phasePreY) ||
            !std::isfinite(row.phasePostX) || !std::isfinite(row.phasePostY) ||
            !std::isfinite(row.triggerX) || row.step == 0 || row.step < previousStep ||
            row.phasePostX <= row.phasePreX || row.triggerX <= row.phasePreX || row.triggerX >= row.phasePostX ||
            row.triggerX + 1e-9 < previousTrigger ||
            (i && closeEnough(row.triggerX, previousTrigger) && row.frame != previousFrame))
            throw Error("TRACE_ROW", "Invalid/non-monotonic automatic trace row");
        row.down = down != 0; row.p2 = p2 != 0; row.dual = dual != 0;
        if (row.frame != expected[i].frame || row.down != expected[i].down || row.p2 != expected[i].p2)
            throw Error("TRACE_INPUT", "Automatic trace input order does not match the macro");
        if (i) {
            uint64_t df = row.frame - previousFrame;
            uint64_t ds = row.step - previousStep;
            if (df == 0) {
                if (ds != 0) throw Error("TRACE_TIMING", "Same-frame trace inputs use different physics steps");
            } else if (!stepsPerMacroFrame) {
                double scale = static_cast<double>(ds) / static_cast<double>(df);
                if (!std::isfinite(scale) || scale <= 0.0 || scale > 16.0)
                    throw Error("TRACE_TIMING", "Invalid macro-frame/physics-step relation in trace");
                stepsPerMacroFrame = scale;
            } else {
                double want = static_cast<double>(df) * *stepsPerMacroFrame;
                double tolerance = std::max(1.0, want * 0.0025);
                if (std::abs(static_cast<double>(ds) - want) > tolerance)
                    throw Error("TRACE_TIMING", "Trace input spacing does not match the macro");
            }
        }
        previousStep = row.step; previousFrame = row.frame; previousTrigger = row.triggerX;
        out.inputs.push_back(row);
    }
    std::string extra;
    if (in >> extra) throw Error("TRACE_TRAILING", "Unexpected data after automatic trace");
    return out;
}

Calibration readCalibration(std::filesystem::path const& path, Replay const& replay) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size > MaxBytes) throw Error("TRACE_SIZE", "Automatic trace is unavailable or too large");
    std::ifstream in(path);
    if (!in) throw Error("TRACE_READ", "Cannot read automatic trace");
    return readCalibration(in, replay);
}

void writeCalibration(std::ostream& out, Calibration const& calibration) {
    out << "HFTRACE2 " << std::hex << calibration.macroHash << ' ' << calibration.levelHash << std::dec
        << ' ' << (calibration.twoPlayer ? 1 : 0) << " 1 " << calibration.inputs.size()
        << " previous-step-midpoint\n";
    out << std::setprecision(17);
    for (auto const& row : calibration.inputs) {
        out << row.frame << ' ' << (row.down ? 1 : 0) << ' ' << (row.p2 ? 1 : 0) << ' ' << row.step << ' '
            << row.inputX << ' ' << row.inputY << ' ' << row.phasePreX << ' ' << row.phasePreY << ' '
            << row.phasePostX << ' ' << row.phasePostY << ' ' << row.triggerX << ' ' << (row.dual ? 1 : 0) << '\n';
    }
    if (!out) throw Error("TRACE_WRITE", "Could not write automatic trace");
}

void writeCalibration(std::filesystem::path const& path, Calibration const& calibration) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw Error("TRACE_WRITE", "Cannot create automatic trace next to the macro");
    writeCalibration(out, calibration);
}

double Calibration::position(uint64_t frame, int p1, int p2) const {
    std::vector<double> candidates;
    auto collect = [&](bool player2, int state) {
        if (!state) return;
        bool down = state == -1;
        auto it = std::find_if(inputs.begin(), inputs.end(), [&](RecordedInput const& row) {
            return row.frame == frame && row.p2 == player2 && row.down == down;
        });
        if (it == inputs.end()) throw Error("TRACE_FRAME", "Automatic trace is missing a required macro edge");
        candidates.push_back(it->triggerX);
    };
    collect(false, p1);
    // In ordinary dual the macro has one logical input stream, mirrored by the
    // generated native Options gate to both players. Do not require a fake P2
    // trace row for that shared stream.
    if (twoPlayer) collect(true, p2);
    else if (p2 && p1 != p2) throw Error("TRACE_DUAL", "Ordinary-dual gate is not a mirrored shared state");
    if (candidates.empty()) throw Error("TRACE_FRAME", "Automatic trace has no placement for this gate");
    if (candidates.size() == 2 && !closeEnough(candidates[0], candidates[1]))
        throw Error("TRACE_PHASE_SPLIT", "P1/P2 edges merged by the macro landed in different physics steps");
    return *std::min_element(candidates.begin(), candidates.end());
}
}
