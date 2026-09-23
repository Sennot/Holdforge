#include "Calibration.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
namespace hf {
Calibration readCalibration(std::istream& in, Replay const& replay) {
    Calibration out; std::string magic; size_t count = 0;
    if (!(in >> magic >> std::hex >> out.macroHash >> out.levelHash >> std::dec >> count) ||
        magic != "HFTRACE1" || !count || count > MaxActions)
        throw Error("TRACE_HEADER", "Invalid recorded-position file");
    if (out.macroHash != replay.fingerprint)
        throw Error("TRACE_MACRO", "Recorded positions belong to a different macro");
    if (replay.tps != 240 || replay.actions.size() != count)
        throw Error("TRACE_INPUT", "Recorded positions require the original clean 240 TPS macro");
    double previous = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < count; ++i) {
        RecordedInput row{}; int down, p2;
        if (!(in >> row.frame >> down >> p2 >> row.x) || (down != 0 && down != 1) || p2 != 0 ||
            !std::isfinite(row.x) || row.x < 0 || row.x <= previous)
            throw Error("TRACE_ROW", "Invalid/non-increasing recorded position");
        row.down = down; row.p2 = p2;
        auto const& a = replay.actions[i];
        if (a.kind != Kind::Jump || a.frame != row.frame || a.down != row.down || a.p2 != row.p2)
            throw Error("TRACE_INPUT", "Recorded input sequence differs from the macro");
        previous = row.x; out.inputs.push_back(row);
    }
    std::string extra;
    if (in >> extra) throw Error("TRACE_TRAILING", "Unexpected data after recorded positions");
    return out;
}
Calibration readCalibration(std::filesystem::path const& path, Replay const& replay) {
    if (std::filesystem::file_size(path) > MaxBytes) throw Error("TRACE_SIZE", "Recorded-position file is too large");
    std::ifstream in(path);
    if (!in) throw Error("TRACE_READ", "Cannot read recorded positions");
    return readCalibration(in, replay);
}
double Calibration::position(uint64_t frame) const {
    auto it = std::lower_bound(inputs.begin(), inputs.end(), frame,
        [](RecordedInput const& input, uint64_t f) { return input.frame < f; });
    if (it == inputs.end() || it->frame != frame) throw Error("TRACE_FRAME", "Missing recorded input frame");
    // Options is processed after movement; place inside the crossing interval
    // ending at the original pre-input X. 0.25 survives GD's 0.1-unit save rounding.
    // This phase choice still needs an in-game test; it is not a physics simulation.
    return std::max(0.0, it->x - 0.25);
}
}
