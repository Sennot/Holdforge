#pragma once
#include "Slc.hpp"
#include <istream>
namespace hf {
struct RecordedInput { uint64_t frame; bool down, p2; double x; };
struct Calibration {
    uint64_t macroHash = 0, levelHash = 0;
    std::vector<RecordedInput> inputs;
    double position(uint64_t frame) const;
};
Calibration readCalibration(std::istream& in, Replay const& replay);
Calibration readCalibration(std::filesystem::path const& path, Replay const& replay);
}
