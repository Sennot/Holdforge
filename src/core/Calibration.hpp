#pragma once
#include "Slc.hpp"
#include <filesystem>
#include <istream>
#include <ostream>

namespace hf {
struct RecordedInput {
    uint64_t frame = 0;
    bool down = false;
    bool p2 = false;
    uint64_t step = 0;
    double inputX = 0, inputY = 0;
    double phasePreX = 0, phasePreY = 0;
    double phasePostX = 0, phasePostY = 0;
    double triggerX = 0;
    bool dual = false;
};

struct Calibration {
    uint64_t macroHash = 0, levelHash = 0;
    bool twoPlayer = false;
    std::vector<RecordedInput> inputs;
    double position(uint64_t frame, int p1, int p2) const;
};

Calibration readCalibration(std::istream& in, Replay const& replay);
Calibration readCalibration(std::filesystem::path const& path, Replay const& replay);
void writeCalibration(std::ostream& out, Calibration const& calibration);
void writeCalibration(std::filesystem::path const& path, Calibration const& calibration);
}
