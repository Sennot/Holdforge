#include "core/Slc.hpp"
#include "core/Plan.hpp"
#include "core/Trajectory.hpp"
#include "core/AutoPath.hpp"
#include <set>
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
// Observe synthetic physics steps, including both known clock origins. This
// tests recording identity/phase checks without pretending to simulate GD.
hf::Trajectory capture(hf::Replay const& replay, bool twoPlayer = false, double origin = 0) {
    hf::Recorder recorder(replay, 0x1234, twoPlayer);
    auto inputs = hf::traceActions(replay, twoPlayer);
    size_t next = 0; bool held1 = false, held2 = false;
    for (uint64_t f = 0; f <= inputs.back().frame + 240; ++f) {
        hf::Sample s; s.time = origin + f / 240.0;
        s.x = 10 + 1.25 * f; s.y = 105;
        s.dual = f >= 2; s.p2x = s.x; s.p2y = 200;
        s.held1 = held1; s.held2 = held2;
        recorder.step(s);
        while (next < inputs.size() && inputs[next].frame == f) {
            auto const& a = inputs[next++];
            check(recorder.input(a.down, a.p2, s), "capture accepts matching edge");
            if (a.p2) held2 = a.down; else held1 = a.down;
        }
        s.held1 = held1; s.held2 = held2; recorder.stepEnd(s);
    }
    return recorder.finish(origin + inputs.back().frame / 240.0 + 1);
}
void trajectoryTests() {
    hf::Replay r; r.fingerprint = 91;
    r.actions = {{2, hf::Kind::Jump, true, false}, {4, hf::Kind::Jump, false, false}};
    for (double origin : {0.0, 1.0 / 240.0}) {
        auto t = capture(r, false, origin);
        check(t.completed && t.sawDual && t.inputs.size() == 2, "completed dual recording");
        check(std::abs(t.clockOffset - origin) < 1e-9, "observed clock origin");
        check(t.position(2) > 11.25 && t.position(2) < 12.5, "trigger inside observed crossing");
        check(t.inputs[0].after.held1 && !t.inputs[1].after.held1, "post-input state retained");
        std::ostringstream output; hf::writeTrajectory(output, t);
        auto load = [&](std::string text, hf::Replay replay = hf::Replay{}) {
            if (replay.actions.empty()) replay = r;
            std::istringstream in(text); return hf::readTrajectory(in, replay, 0x1234, false);
        };
        auto loaded = load(output.str());
        check(loaded.position(4) == t.position(4), "trace roundtrip");
        check(loaded.steps.size() == 245, "full 240 TPS recording including silent tail");
        check(loaded.steps.back().time == t.steps.back().time, "full path cache roundtrip");
        error([&] { t.position(3); }, "CAPTURE_FRAME");
        error([&] { load(output.str() + "unexpected"); }, "CAPTURE_CACHE");
        error([&] { load(output.str().substr(0, output.str().size() / 2)); }, "CAPTURE_CACHE");
        auto other = r; ++other.fingerprint;
        error([&] { load(output.str(), other); }, "CAPTURE_IDENTITY");
        error([&] { std::istringstream in(output.str()); hf::readTrajectory(in, r, 0x4321, false); }, "CAPTURE_IDENTITY");
        t.completed = false; std::ostringstream partial; hf::writeTrajectory(partial, t);
        error([&] { load(partial.str()); }, "CAPTURE_CACHE");
        t.completed = true; t.inputs[1].triggerX += 1;
        std::ostringstream damaged; hf::writeTrajectory(damaged, t);
        error([&] { load(damaged.str()); }, "CAPTURE_CACHE");
    }
    auto zero = r; zero.actions[0].frame = 0;
    auto t0 = capture(zero); check(t0.position(0) == 0, "frame-zero initial gate");
    auto two = r;
    two.actions = {{2, hf::Kind::Jump, true, false}, {2, hf::Kind::Jump, true, true},
                   {4, hf::Kind::Jump, false, true}, {5, hf::Kind::Jump, false, false}};
    auto both = capture(two, true);
    check(both.inputs[0].triggerX == both.inputs[1].triggerX, "simultaneous 2P position");
    check(both.inputs[0].after.held1 && both.inputs[0].after.held2, "2P post-queue snapshot");
    std::ostringstream out; hf::writeTrajectory(out, both);
    std::istringstream in(out.str());
    check(hf::readTrajectory(in, two, 0x1234, true).inputs.size() == 4, "2P trace roundtrip");
    hf::PlanConfig config; config.sharedP1Only = true;
    auto alternative = hf::plan(r, config);
    for (auto const& g : alternative.gates) check(g.p2 == 0, "P1-only diagnostic leaves P2 untouched");
    config.twoPlayer = true;
    check(hf::plan(two, config).gates[1].p2 == -1, "P1-only does not affect 2P");
    auto duplicate = r; duplicate.actions.insert(duplicate.actions.begin()+1, duplicate.actions.front());
    check(hf::traceActions(duplicate, false).size() == 2, "recording canonical duplicate removal");
    error([&] { hf::crossingPosition(1, 1); }, "CAPTURE_X");
    error([&] { hf::crossingPosition(2, 1); }, "CAPTURE_X");
    error([&] { hf::crossingPosition(1.001, 1.002); }, "CAPTURE_PRECISION");
    hf::Sample s; s.x = 10; s.y = 105;
    hf::Recorder phase(r, 0x1234, false);
    error([&] { phase.input(true, false, s); }, "CAPTURE_PHASE");
    phase.step(s); s.time = 1.0 / 240; s.x += 1; phase.step(s);
    s.time = 2.0 / 240; s.x += 1; phase.step(s);
    check(phase.input(true, false, s), "correct first edge");
    check(!phase.input(true, false, s), "repeated down ignored");
    error([&] { phase.input(false, false, s); }, "CAPTURE_INPUT");
    error([&] { phase.finish(1); }, "CAPTURE_INCOMPLETE");
    hf::Recorder missed(r, 1, false); s = {}; s.x = 10; missed.step(s);
    for (int f=1; f<=3; ++f) { s.time=f/240.0; s.x++; missed.step(s); }
    s.time=4/240.0; s.x++;
    error([&] { missed.step(s); }, "CAPTURE_MISSING_INPUT");
    hf::Recorder start(r, 1, false); s = {}; s.time = 1;
    error([&] { start.step(s); }, "CAPTURE_START");
    hf::Recorder gap(r, 1, false); s = {}; s.x = 10; gap.step(s); s.time = 2/240.0;
    error([&] { gap.step(s); }, "CAPTURE_STEP");
    hf::Recorder reverse(r, 1, false); s = {}; s.x = 10; reverse.step(s); s.time = 1/240.0; s.x = 9;
    error([&] { reverse.step(s); }, "CAPTURE_REVERSE");
    hf::Recorder clock(r, 1, false); s = {}; s.x = 10; clock.step(s); s.time=1/240.0; s.x++; clock.step(s); s.time=0;
    error([&] { clock.step(s); }, "CAPTURE_RESET");
    auto one = zero; one.actions.resize(1);
    hf::Recorder noAfter(one, 1, false); s = {}; s.x = 10; noAfter.step(s); noAfter.input(true, false, s);
    error([&] { noAfter.finish(1); }, "CAPTURE_PHASE");
}
hf::Trajectory autoTrace(int mode1, int mode2) {
    hf::Trajectory t; t.completed = true; t.sawDual = true; t.endTime = 60/240.0;
    for (int i=0; i<=60; ++i) {
        hf::Sample s; s.time = i/240.0; s.x = 100 + i*1.5; s.y = 100;
        s.p2x = s.x; s.p2y = 400 + 10*std::sin(i*.05);
        s.mode1 = mode1; s.mode2 = mode2; s.size1 = mode1%2 ? .6 : 1; s.size2 = mode2%2 ? .6 : 1;
        s.dual = i>=5 && i<55; t.steps.push_back(s);
    }
    return t;
}
void autoTests() {
    // These are serialization/planning tests, NOT a simulation of GD physics.
    for (int p1=0; p1<8; ++p1) for (int p2=0; p2<8; ++p2) {
        auto t = autoTrace(p1, p2);
        auto plan = hf::makeAutoPath(t, "kS38,1;1,1,57,9999.9998,51,9997;");
        check(plan.segments == 1 && plan.anchors.size() == 49, "every complete dual interval gets a helper");
        check(plan.modes[p2] == 49 && plan.objects.size() == 197, "all 64 mode pairs share path planner");
        std::set<int> ids;
        for (auto const& a : plan.anchors) {
            check(a.portalGroup < 9997 && a.targetGroup < 9997, "existing and referenced groups reserved");
            check(ids.insert(a.portalGroup).second && a.portalGroup != a.targetGroup, "unique portal groups and non-self target");
            check(a.onX > t.steps[a.step-1].x && a.onX < t.steps[a.step].x, "activation inside P1 crossing");
            check(a.offX > t.steps[a.step].x && a.offX < t.steps[a.step+1].x, "deactivation in following crossing");
            check(std::abs(a.targetY-t.steps[a.step+1].p2y) <= .051, "target follows next recorded step");
        }
        std::set<int> targetMembers;
        for (auto const& o : plan.objects) if (o.group) check(targetMembers.insert(o.group).second, "exactly one member per target group");
        for (auto const& a : plan.anchors) check(targetMembers.contains(a.targetGroup), "every teleport target resolves");
        for (auto const& o : plan.objects) {
            check(o.object.find(",135,1,") != std::string::npos, "helper serialized hidden");
            if (o.kind == hf::AutoKind::Target) check(o.object.find(",121,1;") != std::string::npos, "target has no collision");
            if (o.kind == hf::AutoKind::Portal) {
                check(o.id == 2064 && o.object.find(",352,1,") != std::string::npos, "native touch portal preserves X");
                check(o.object.find(",121,1") == std::string::npos, "portal collision must remain enabled");
            }
        }
    }
    auto t = autoTrace(0, 1);
    auto used = hf::reservedIDs("kA,0;1,1,57,12.24,51,9000,442,50.80.99;");
    check(used[12] && used[24] && used[9000] && used[50] && used[99] && !used[442], "IDs reserved in values including remaps");
    auto overlap = t; overlap.steps[22].y = 400;
    error([&] { hf::makeAutoPath(overlap, ""); }, "AUTO_P1_OVERLAP");
    auto two = t; two.twoPlayer = true;
    error([&] { hf::makeAutoPath(two, ""); }, "AUTO_2P");
    auto partial = t; partial.completed = false;
    error([&] { hf::makeAutoPath(partial, ""); }, "AUTO_RECORD");
    auto tail = t; tail.endTime += 1;
    error([&] { hf::makeAutoPath(tail, ""); }, "AUTO_RECORD");
    auto gap = t; gap.steps.erase(gap.steps.begin()+10);
    error([&] { hf::makeAutoPath(gap, ""); }, "AUTO_STEPS");
    auto badMode = t; badMode.steps[30].mode2 = 8;
    error([&] { hf::makeAutoPath(badMode, ""); }, "AUTO_STEPS");
    auto badPos = t; badPos.steps[30].p2y = std::numeric_limits<double>::infinity();
    error([&] { hf::makeAutoPath(badPos, ""); }, "AUTO_POSITION");
    hf::AutoPathConfig limit; limit.maxObjects = 12;
    error([&] { hf::makeAutoPath(t, "", limit); }, "AUTO_OBJECT_LIMIT");
    std::string all = "1,1,57,";
    for (int i=1; i<=9999; ++i) all += std::to_string(i) + ".";
    all += ";";
    error([&] { hf::makeAutoPath(t, all); }, "AUTO_GROUP_LIMIT");
    auto solo = t; solo.sawDual = false; for (auto& s : solo.steps) s.dual = false;
    check(hf::makeAutoPath(solo, "").objects.empty(), "solo run has no invisible helpers");
    auto multi = t; for (size_t i=25; i<30; ++i) multi.steps[i].dual = false;
    check(hf::makeAutoPath(multi, "").segments == 2, "separated dual sections planned independently");
}
int main(int argc, char** argv) {
 try {
    trajectoryTests();
    autoTests();
    Bytes s = input(240, 1, true, false); append(s, input(120, 1, false, false));
    auto data = v3(s, 2); auto r = hf::parse(data);
    check(r.format == 3 && r.tps == 240 && r.seed == 12345 && r.build == 81, "v3 metadata");
    check(r.actions.size() == 2 && r.actions[0].frame == 240 && r.actions[1].frame == 360, "delta frames");
    auto p = hf::plan(r, {});
    check(p.gates.size() == 3 && p.gates[0].p1 == 1 && p.gates[0].p2 == 1, "initial control gate");
    check(p.gates[1].seconds == 1 && p.gates[1].p1 == -1 && p.gates[1].p2 == -1, "shared dual press");
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
    if (argc == 2) {
        auto actual = hf::read(argv[1]);
        auto actualPlan = hf::plan(actual, {});
        check(hf::traceActions(actual, false).size() + 1 == actualPlan.gates.size(), "supplied macro canonical coverage");
        std::cout << "PASS: supplied .slc parsed/planned, " << actual.actions.size() << " inputs (no in-game claim)\n";
    }
    std::cout << "PASS: " << checks << " checks; 2000 deterministic malformed-input mutations\n";
    return 0;
 } catch (std::exception const& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
