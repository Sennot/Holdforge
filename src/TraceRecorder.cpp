#include "TraceRecorder.hpp"
#include "Diagnostics.hpp"
#include "core/Fingerprint.hpp"
#include "core/Plan.hpp"
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace hf {
namespace {
TracePlayerState playerState(PlayerObject* p) {
    TracePlayerState out;
    if (!p) return out;
    out.x = p->getPositionX(); out.y = p->getPositionY();
    out.disabled = p->m_controlsDisabled; out.buffered = p->m_jumpBuffered;
    auto held = p->m_holdingButtons.find(1);
    out.holding = held != p->m_holdingButtons.end() && held->second;
    out.dead = p->m_isDead; out.speed = p->m_playerSpeed;
    return out;
}
matjson::Value playerJson(TracePlayerState const& p) {
    auto row = matjson::Value::object();
    row["x"] = p.x; row["y"] = p.y; row["disabled"] = p.disabled;
    row["buffered"] = p.buffered; row["holding"] = p.holding;
    row["dead"] = p.dead; row["speed"] = p.speed;
    return row;
}
matjson::Value stepJson(TraceStepState const& s) {
    auto row = matjson::Value::object();
    row["serial"] = s.serial; row["progress_tick"] = s.progress;
    row["level_time"] = s.levelTime; row["dual"] = s.dual;
    row["p1"] = playerJson(s.p1); row["p2"] = playerJson(s.p2);
    return row;
}
}

TraceRecorder& TraceRecorder::get() { static TraceRecorder value; return value; }

TraceStepState TraceRecorder::snapshot(GJBaseGameLayer* layer, uint64_t serial) {
    TraceStepState out; out.serial = serial;
    if (!layer) return out;
    out.progress = layer->m_gameState.m_currentProgress;
    out.levelTime = layer->m_gameState.m_levelTime;
    out.dual = layer->m_gameState.m_isDualMode;
    out.p1 = playerState(layer->m_player1); out.p2 = playerState(layer->m_player2);
    return out;
}

void TraceRecorder::fail(std::string code, std::string message) {
    if (m_failed) return;
    m_failed = true; m_error = code + ": " + message;
    auto row = matjson::Value::object(); row["code"] = code; row["message"] = message;
    row["matched"] = m_index; row["expected"] = m_expected.size();
    Diagnostics::get().event("trace_rejected", row);
    log::warn("HoldForge trace rejected: {} - {}", code, message);
}

void TraceRecorder::arm(LevelEditorLayer* editor, Replay const& replay, std::filesystem::path const& path) {
    if (!editor || LevelEditorLayer::get() != editor || editor->m_playbackMode != PlaybackMode::Not || editor->m_playbackActive)
        throw Error("TRACE_ARM_EDITOR", "Stop playtest/playback before arming trace recording");
    if (!editor->m_levelSettings || editor->m_levelSettings->m_platformerMode)
        throw Error("TRACE_ARM_LEVEL", "Automatic trace supports classic levels only");
    for (auto obj : CCArrayExt<GameObject*>(editor->m_objects)) {
        if (auto options = typeinfo_cast<GameOptionsTrigger*>(obj)) {
            if (options->m_disableP1Controls != GameOptionsSetting::Disabled ||
                options->m_disableP2Controls != GameOptionsSetting::Disabled)
                throw Error("TRACE_ARM_CONTROLS", "Undo/remove generated control Options before recording the source macro");
        }
    }

    PlanConfig cfg; cfg.twoPlayer = editor->m_levelSettings->m_twoPlayerMode; cfg.strict240 = true;
    cfg.offsetMs = 0; cfg.maxTriggers = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("max-triggers"));
    if (cfg.twoPlayer && GameManager::get() && GameManager::get()->getGameVariable("0010"))
        throw Error("TRACE_FLIPPED_2P", "Disable GD's Flip 2-Player Controls while recording an independent P1/P2 trace");
    (void)plan(replay, cfg);

    m_expected.clear();
    std::array<bool, 2> down{false, false};
    for (auto const& a : replay.actions) {
        if (a.kind != Kind::Jump) continue;
        if (!cfg.twoPlayer && a.p2) continue;
        size_t p = a.p2 ? 1 : 0;
        if (down[p] == a.down) continue;
        down[p] = a.down;
        if (a.frame) m_expected.push_back({a.frame, a.down, a.p2});
    }
    if (m_expected.empty()) throw Error("TRACE_ARM_EMPTY", "Macro has no usable jump edges");

    m_macroHash = replay.fingerprint;
    m_macroTps = replay.tps > 0 ? static_cast<double>(replay.tps) : 240.0;
    m_levelHash = fingerprint(std::string(editor->getLevelString()));
    m_twoPlayer = cfg.twoPlayer; m_path = path;
    m_armed = true; m_attempt = false; m_failed = false; m_error.clear();
    m_index = 0; m_actualDown = {false, false};
    m_result = {}; m_result.macroHash = m_macroHash; m_result.levelHash = m_levelHash; m_result.twoPlayer = m_twoPlayer;

    auto row = matjson::Value::object();
    row["macro_hash"] = fmt::format("{:016x}", m_macroHash);
    row["level_hash"] = fmt::format("{:016x}", m_levelHash);
    row["two_player"] = m_twoPlayer; row["expected_edges"] = m_expected.size();
    row["path"] = utils::string::pathToString(m_path.filename());
    row["phase"] = "input-edge-snapshot";
    Diagnostics::get().event("trace_armed", row);
}

void TraceRecorder::cancel() {
    m_armed = false; m_attempt = false; m_failed = false; m_error.clear();
    m_expected.clear(); m_result.inputs.clear();
}

std::string TraceRecorder::status() const {
    if (!m_armed) return "Trace recorder idle";
    if (m_failed) return m_error;
    if (m_attempt) return fmt::format("Recording trace: {}/{} edges", m_index, m_expected.size());
    return fmt::format("Trace armed: replay full macro from start ({} edges)", m_expected.size());
}

void TraceRecorder::onPlaytestStart(LevelEditorLayer* editor) {
    if (!m_armed) return;
    auto now = fingerprint(std::string(editor->getLevelString()));
    if (now != m_levelHash) { fail("TRACE_LEVEL_CHANGED", "Level changed after trace was armed"); return; }
    m_attempt = true; m_failed = false; m_error.clear(); m_index = 0;
    m_actualDown = {false, false}; m_result.inputs.clear();
    Diagnostics::get().event("trace_attempt_begin");
    Notification::create("HoldForge: recording macro trace", NotificationIcon::Info, 2.f)->show();
}

void TraceRecorder::onButton(GJBaseGameLayer* layer, bool down, int button, bool player1) {
    if (!active() || !layer || button != 1) return;
    if (layer != LevelEditorLayer::get()) return;

    bool p2 = m_twoPlayer ? !player1 : false;
    size_t p = p2 ? 1 : 0;
    if (m_actualDown[p] == down) {
        auto duplicate = matjson::Value::object(); duplicate["down"] = down; duplicate["p2"] = p2;
        duplicate["ignored_duplicate"] = true;
        Diagnostics::get().event("trace_actual_input", duplicate);
        return;
    }
    m_actualDown[p] = down;

    if (m_index >= m_expected.size()) {
        fail("TRACE_EXTRA_INPUT", "More jump edges were observed than the macro contains");
        return;
    }
    auto const expected = m_expected[m_index];
    if (expected.down != down || expected.p2 != p2) {
        fail("TRACE_INPUT_ORDER", fmt::format("Expected {} {} at macro frame {}, observed {} {}",
            expected.p2 ? "P2" : "P1", expected.down ? "press" : "release", expected.frame,
            p2 ? "P2" : "P1", down ? "press" : "release"));
        return;
    }

    auto at = snapshot(layer, m_index + 1);
    double expectedTime = static_cast<double>(expected.frame) / m_macroTps;
    double timeError = std::abs(at.levelTime - expectedTime);
    double timeTolerance = std::max(0.010, 2.0 / m_macroTps);
    if (!std::isfinite(at.levelTime) || timeError > timeTolerance) {
        fail("TRACE_TIMING", fmt::format(
            "Macro frame {} expected at {:.6f}s, observed {:.6f}s (error {:.3f}ms)",
            expected.frame, expectedTime, at.levelTime, timeError * 1000.0));
        return;
    }

    // The diagnostic log supplied for 0.1.5 proves handleButton itself is the
    // stable synchronization point in editor playtest: all 412 macro edges were
    // observed in order and level_time matched frame / 240. processCommands was
    // not a reliable hook point in this setup, so recording must not depend on it.
    auto target = p2 ? at.p2 : at.p1;
    double triggerX = at.p1.x; // level progression / trigger crossing follows the primary X timeline
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(triggerX)) {
        fail("TRACE_POSITION", "Non-finite player position at input edge");
        return;
    }
    if (!m_result.inputs.empty() && triggerX <= m_result.inputs.back().triggerX + 1e-6) {
        fail("TRACE_NON_MONOTONIC", fmt::format(
            "Input-edge X did not advance: previous {:.6f}, current {:.6f}",
            m_result.inputs.back().triggerX, triggerX));
        return;
    }

    RecordedInput recorded;
    recorded.frame = expected.frame;
    recorded.down = down;
    recorded.p2 = p2;
    recorded.sequence = m_index + 1;
    recorded.levelTime = at.levelTime;
    recorded.inputX = target.x;
    recorded.inputY = target.y;
    recorded.triggerX = triggerX;
    recorded.dual = at.dual;
    m_result.inputs.push_back(recorded);

    auto diag = matjson::Value::object();
    diag["edge_index"] = m_index;
    diag["macro_frame"] = expected.frame;
    diag["expected_time"] = expectedTime;
    diag["time_error_ms"] = timeError * 1000.0;
    diag["down"] = down; diag["p2"] = p2;
    diag["capture"] = "handleButton-input-edge";
    diag["trigger_x"] = triggerX;
    diag["at_input"] = stepJson(at);
    Diagnostics::get().event("trace_actual_input", diag);
    ++m_index;
}

void TraceRecorder::onDamage(GJBaseGameLayer* layer, PlayerObject*) {
    if (active() && layer == LevelEditorLayer::get())
        fail("TRACE_DEATH", "Source macro attempt took damage before completion");
}

void TraceRecorder::onPlaytestStop(LevelEditorLayer* editor) {
    if (!m_armed || !m_attempt) return;
    if (!editor || editor != LevelEditorLayer::get())
        fail("TRACE_EDITOR_LOST", "Editor layer changed before trace stop");
    if (!m_failed && m_index != m_expected.size())
        fail("TRACE_INCOMPLETE", fmt::format(
            "Stop pressed after {}/{} expected edges; let Silicate replay all inputs before Stop",
            m_index, m_expected.size()));
    if (!m_failed && m_result.inputs.size() != m_expected.size())
        fail("TRACE_EMPTY", "Not every matched macro edge has a recorded input position");

    if (!m_failed) {
        try {
            writeCalibration(m_path, m_result);
            auto row = matjson::Value::object(); row["path"] = utils::string::pathToString(m_path);
            row["inputs"] = m_result.inputs.size();
            row["manual_stop_accepted"] = true;
            row["matched_edges"] = m_index;
            row["expected_edges"] = m_expected.size();
            row["capture"] = "handleButton-input-edge";
            row["macro_hash"] = fmt::format("{:016x}", m_result.macroHash);
            row["level_hash"] = fmt::format("{:016x}", m_result.levelHash);
            Diagnostics::get().set("automatic_trace", row);
            Diagnostics::get().event("trace_manual_stop_saved", row);
            Notification::create("HoldForge: trace saved", NotificationIcon::Success, 3.f)->show();
        } catch (std::exception const& e) {
            fail("TRACE_WRITE", e.what());
        }
    }
    if (m_failed) {
        auto msg = fmt::format("HoldForge: trace rejected - {}", m_error);
        Notification::create(msg, NotificationIcon::Error, 5.f)->show();
    }
    m_attempt = false; m_armed = false;
}
}
