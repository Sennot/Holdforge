#pragma once
#include "Plan.hpp"
#include <array>
#include <istream>
#include <ostream>
#include <optional>
namespace hf {
struct Sample {
    double time = 0;
    double x = 0, y = 0, p2x = 0, p2y = 0;
    bool dual = false;
    int mode1 = 0, mode2 = 0;
    bool held1 = false, held2 = false;
};
struct TraceInput {
    uint64_t frame = 0;
    bool down = false, p2 = false;
    double previousX = 0, triggerX = 0;
    Sample sample;
    Sample after;
    bool hasAfter = false;
};
struct Trajectory {
    uint64_t macroHash = 0, levelHash = 0;
    bool twoPlayer = false, completed = false, sawDual = false;
    double endTime = 0, clockOffset = 0;
    std::vector<TraceInput> inputs;
    double position(uint64_t frame) const;
};
uint64_t fingerprint(std::string const& text);
std::vector<Action> traceActions(Replay const& replay, bool twoPlayer);
double crossingPosition(double previousX, double currentX);
void writeTrajectory(std::ostream& out, Trajectory const& trace);
Trajectory readTrajectory(std::istream& in, Replay const& replay, uint64_t levelHash, bool twoPlayer);

class Recorder {
    std::vector<Action> m_expected;
    std::array<bool, 2> m_down{false, false};
    Trajectory m_trace;
    std::optional<Sample> m_step;
    double m_previousX = 0;
    bool m_hasPrevious = false;
    bool m_hasClockOffset = false;
public:
    Recorder(Replay const& replay, uint64_t levelHash, bool twoPlayer);
    void step(Sample const& sample);
    void stepEnd(Sample const& sample);
    // Called before GD handles an observed jump; never injects input.
    bool input(bool down, bool p2, Sample const& sample);
    Trajectory finish(double endTime);
    size_t count() const { return m_trace.inputs.size(); }
    size_t expected() const { return m_expected.size(); }
};
}
