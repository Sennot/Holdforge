#include "core/Slc.hpp"
#include "core/Plan.hpp"
#include "core/Calibration.hpp"
#include <sstream>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <random>

using Bytes = std::vector<uint8_t>;
static int checks = 0;
void check(bool ok, char const* message) { ++checks; if (!ok) throw std::runtime_error(message); }
void put(Bytes& b, uint64_t n, int width) { for (int i = 0; i < width; ++i) b.push_back(static_cast<uint8_t>(n >> (8 * i))); }
void real(Bytes& b, double n) { uint64_t bits; std::memcpy(&bits, &n, 8); put(b, bits, 8); }
void text(Bytes& b, std::string const& s) { b.insert(b.end(), s.begin(), s.end()); }
void error(std::function<void()> f, std::string const& expected) {
    try { f(); } catch (hf::Error const& e) { check(e.code == expected, ("Expected " + expected + ", got " + e.code).c_str()); return; }
    throw std::runtime_error("Expected error: " + expected);
}
Bytes v3(Bytes const& sections, uint64_t count, bool zero = false, bool unknown = false) {
    Bytes b; text(b, "SLC3RPLY"); put(b, 64, 2); real(b, 240); put(b, 12345, 8);
    put(b, 2, 4); put(b, 81, 4); put(b, 0, 4); b.resize(b.size() + 36, 0);
    if (unknown) { put(b, 91, 4); put(b, 3, 8); text(b, "abc"); }
    put(b, 1, 4); put(b, zero ? 0 : sections.size() + 8, 8); put(b, count, 8);
    b.insert(b.end(), sections.begin(), sections.end()); b.push_back(0xcc); return b;
}
Bytes input(uint64_t delta, int button, bool down, bool p2, int width = 2) {
    Bytes b;
    auto size = width == 1 ? 0 : width == 2 ? 1 : width == 4 ? 2 : 3;
    put(b, uint64_t(size) << 12, 2);
    put(b, (delta << 4) | (uint64_t(button) << 2) | (p2 << 1) | down, width); return b;
}
void append(Bytes& b, Bytes const& x) { b.insert(b.end(), x.begin(), x.end()); }
Bytes v2(int metaSize = 64) {
    Bytes b; text(b, "SILL"); real(b, 240); put(b, metaSize, 8);
    if (metaSize) { put(b, 77, 8); b.resize(b.size() + metaSize - 8, 0); }
    put(b, 3, 8); put(b, 1, 8); // count, blob count
    put(b, 2, 8); put(b, 0, 8); put(b, 3, 8); // width, start, count
    put(b, (240 << 5) | 5, 2); // P1 jump down at frame 240
    put(b, (120 << 5) | (7 << 2), 2); real(b, 480);
    put(b, (240 << 5) | 4, 2); text(b, "EOM"); return b;
}
int main(int argc, char** argv) {
 try {
    Bytes s = input(240, 1, true, false); append(s, input(120, 1, false, false));
    auto data = v3(s, 2); auto r = hf::parse(data);
    check(r.format == 3 && r.tps == 240 && r.seed == 12345 && r.build == 81, "v3 metadata");
    check(r.actions.size() == 2 && r.actions[0].frame == 240 && r.actions[1].frame == 360, "delta frames");
    std::ostringstream header;
    header << "HFTRACE3 " << std::hex << r.fingerprint << " 1234 " << std::dec << "0 1 2 input-edge-snapshot\n";
    auto readTrace = [&](std::string const& rows) {
        std::istringstream stream(header.str() + rows); return hf::readCalibration(stream, r);
    };
    auto calibrated = readTrace(
        "240 1 0 1 1.0 100.5 5 100.5 0\n"
        "360 0 0 2 1.5 200.5 5 200.5 0\n");
    check(calibrated.position(240, -1, -1) == 100.5 && calibrated.position(360, 1, 1) == 200.5, "recorded input-edge positions");
    check(calibrated.levelHash == 0x1234, "recorded level binding");
    error([&] { readTrace("240 0 0 1 1.0 100.5 5 100.5 0\n360 0 0 2 1.5 200.5 5 200.5 0\n"); }, "TRACE_INPUT");
    error([&] { readTrace("240 1 0 1 1.0 100.5 5 100.5 0\n360 0 0 2 1.5 90 5 90 0\n"); }, "TRACE_ROW");
    error([&] { readTrace("240 1 0 1 1.0 100.5 5 100.5 0\n360 0 0 2 1.2 200.5 5 200.5 0\n"); }, "TRACE_TIMING");
    error([&] { readTrace("240 1 0 2 1.0 100.5 5 100.5 0\n360 0 0 3 1.5 200.5 5 200.5 0\n"); }, "TRACE_ROW");
    error([&] { readTrace("240 1 0 1 1.0 100.5 5 100.4 0\n360 0 0 2 1.5 200.5 5 200.5 0\n"); }, "TRACE_ROW");
    error([&] { readTrace("240 1 0 1 1.0 100.5 5 100.5 0\n"); }, "TRACE_ROW");
    error([&] { readTrace("240 1 0 1 1.0 100.5 5 100.5 0\n360 0 0 2 1.5 200.5 5 200.5 0\nextra"); }, "TRACE_TRAILING");
    error([&] { calibrated.position(100, -1, 0); }, "TRACE_FRAME");
    error([&] { std::istringstream stream("HFTRACE3 0 1234 0 1 2 input-edge-snapshot"); hf::readCalibration(stream, r); }, "TRACE_MACRO");
    error([&] { std::istringstream stream("HFTRACE2 0 1234 0 1 2 previous-step-midpoint"); hf::readCalibration(stream, r); }, "TRACE_HEADER");
    auto p = hf::plan(r, {});
    check(p.gates.size() == 3 && p.gates[0].p1 == 1 && p.gates[0].p2 == 1, "initial shared native control gate");
    check(p.gates[1].seconds == 1 && p.gates[1].p1 == -1 && p.gates[1].p2 == -1, "ordinary dual mirrors shared native gate");
    check(p.gates[2].seconds == 1.5 && p.gates[2].p1 == 1, "release blocks");
    check(hf::parse(v3(s, 2, true, true)).actions.size() == 2, "zero size and unknown atom");
    for (int width : {1, 2, 4, 8}) {
        auto x = hf::parse(v3(input(3, 1, true, true, width), 1));
        check(x.actions[0].p2 && x.actions[0].down && x.actions[0].frame == 3, "all input widths");
    }
    Bytes repeat; put(repeat, (1 << 14) | (1 << 8) | (2 << 3), 2);
    put(repeat, (2 << 4) | 5, 1); put(repeat, (3 << 4) | 4, 1);
    auto repeated = hf::parse(v3(repeat, 8));
    check(repeated.actions.size() == 8 && repeated.actions.back().frame == 20, "repeat cluster expansion");
    error([&] { hf::parse(v3(repeat, 2)); }, "SLC_REPEAT_LIMIT");
    auto swift = hf::parse(v3(input(10, 0, true, false), 2));
    check(swift.actions[0].down && !swift.actions[1].down && swift.actions[1].frame == 10, "swift decode");
    error([&] { hf::plan(swift, {}); }, "PLAN_SAME_FRAME");
    error([&] { hf::parse(v3(input(10, 0, true, false), 1)); }, "SLC_ACTION_COUNT");
    for (int meta : {0, 8, 64}) {
        auto legacy = hf::parse(v2(meta));
        check(legacy.format == 2 && legacy.actions[2].frame == 600, "v2 blobs");
        error([&] { hf::plan(legacy, {}); }, "PLAN_TPS");
        hf::PlanConfig cfg; cfg.strict240 = false;
        auto result = hf::plan(legacy, cfg);
        check(result.gates.back().seconds == 2.0, "piecewise TPS timing");
    }
    auto two = r;
    two.actions = {{120, hf::Kind::Jump, true, true}, {240, hf::Kind::Jump, true, false},
                   {360, hf::Kind::Jump, false, true}, {360, hf::Kind::Jump, false, false}};
    error([&] { hf::plan(two, {}); }, "PLAN_P2_SHARED");
    hf::PlanConfig cfg; cfg.twoPlayer = true;
    auto dual = hf::plan(two, cfg);
    check(dual.gates[1].p1 == 0 && dual.gates[1].p2 == -1, "independent P2");
    check(dual.gates.back().p1 == 1 && dual.gates.back().p2 == 1, "coincident P1/P2 merge");
    two.actions = {{240, hf::Kind::Jump, true, false}, {240, hf::Kind::Jump, true, true},
                   {360, hf::Kind::Jump, false, false}, {360, hf::Kind::Jump, false, true}};
    check(hf::plan(two, {}).gates.size() == 3, "mirrored shared streams");
    auto duplicate = r; duplicate.actions.insert(duplicate.actions.begin()+1, r.actions.front());
    check(hf::plan(duplicate, {}).duplicates == 1, "duplicate removal");
    auto frame0 = r; frame0.actions[0].frame = 0;
    check(hf::plan(frame0, {}).gates[0].p1 == -1, "frame zero merged with initial gate");
    cfg = {}; cfg.offsetMs = -100;
    error([&] { hf::plan(frame0, cfg); }, "PLAN_NEGATIVE");
    cfg = {}; cfg.maxTriggers = 2;
    error([&] { hf::plan(r, cfg); }, "PLAN_LIMIT");
    for (auto kind : {hf::Kind::Restart, hf::Kind::RestartFull, hf::Kind::Death, hf::Kind::Bugpoint}) {
        auto special = r; special.actions[0].kind = kind;
        error([&] { hf::plan(special, {}); }, "PLAN_SPECIAL");
    }
    auto left = r; left.actions[0].kind = hf::Kind::Left;
    error([&] { hf::plan(left, {}); }, "PLAN_PLATFORMER");
    for (int special = 0; special < 5; ++special) {
        Bytes body; put(body, (2 << 14) | (special << 10), 2); put(body, 10, 1);
        if (special <= 2) put(body, 999, 8);
        if (special == 3) real(body, 480);
        auto x = hf::parse(v3(body, 1));
        check(x.actions.size() == 1 && x.actions[0].frame == 10, "special section parsing");
    }
    auto nan = data; double bad = std::numeric_limits<double>::quiet_NaN(); std::memcpy(nan.data()+10, &bad, 8);
    error([&] { hf::parse(nan); }, "SLC_TPS");
    auto invalidFooter = data; invalidFooter.back() = 0;
    error([&] { hf::parse(invalidFooter); }, "SLC_FOOTER");
    for (auto const& valid : {data, v2()}) {
        for (size_t n = 0; n < valid.size(); ++n) {
            bool rejected = false;
            try { hf::parse(Bytes(valid.begin(), valid.begin() + n)); } catch (hf::Error const&) { rejected = true; }
            check(rejected, "all truncated prefixes must be rejected");
        }
    }
    // Deterministic mutations exercise bounds/count/overflow paths under sanitizers.
    std::mt19937 rng(1234);
    for (int i = 0; i < 2000; ++i) {
        auto mutant = data; auto index = rng() % mutant.size(); mutant[index] ^= uint8_t(1 + rng() % 255);
        try { auto parsed = hf::parse(mutant); check(parsed.actions.size() <= hf::MaxActions, "bounded mutation output"); }
        catch (hf::Error const&) {}
    }
    if (argc == 3) {
        auto actual = hf::read(argv[1]);
        auto reference = hf::readCalibration(std::filesystem::path(argv[2]), actual);
        hf::PlanConfig actualCfg; actualCfg.twoPlayer = reference.twoPlayer;
        auto actualPlan = hf::plan(actual, actualCfg);
        double previousX = 0;
        for (auto const& g : actualPlan.gates) {
            if (!g.frame) continue;
            auto x = reference.position(g.frame, g.p1, g.p2);
            check(std::isfinite(x) && x > previousX, "actual macro has increasing calibrated gates");
            previousX = x;
        }
        std::cout << "PASS: supplied .slc matched " << reference.inputs.size() << " recorded inputs\n";
    }
    std::cout << "PASS: " << checks << " checks; 2000 deterministic malformed-input mutations\n";
    return 0;
 } catch (std::exception const& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
