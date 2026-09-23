#include "TraceRecorder.hpp"
#include "Diagnostics.hpp"
#include "core/Fingerprint.hpp"
#include "core/Plan.hpp"
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

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
        m_expected.push_back({a.frame, a.down, a.p2});
    }
    if (m_expected.empty()) throw Error("TRACE_ARM_EMPTY", "Macro has no usable jump edges");

    m_macroHash = replay.fingerprint;
    m_levelHash = fingerprint(std::string(editor->getLevelString()));
    m_twoPlayer = cfg.twoPlayer; m_path = path;
    m_armed = true; m_attempt = false; m_failed = false; m_error.clear();
    m_index = 0; m_stepSerial = 0; m_actualDown = {false, false};
    m_lastMatchedFrame.reset(); m_lastMatchedStep.reset(); m_stepsPerMacroFrame.reset();
    m_currentPre.reset(); m_lastPre.reset(); m_lastPost.reset();
    m_result = {}; m_result.macroHash = m_macroHash; m_result.levelHash = m_levelHash; m_result.twoPlayer = m_twoPlayer;

    auto row = matjson::Value::object();
    row["macro_hash"] = fmt::format("{:016x}", m_macroHash);
    row["level_hash"] = fmt::format("{:016x}", m_levelHash);
    row["two_player"] = m_twoPlayer; row["expected_edges"] = m_expected.size();
    row["path"] = utils::string::pathToString(m_path.filename());
    row["phase"] = "previous-physics-step-midpoint";
    Diagnostics::get().event("trace_armed", row);
}

void TraceRecorder::cancel() {
    m_armed = false; m_attempt = false; m_failed = false; m_error.clear();
    m_expected.clear(); m_result.inputs.clear();
    m_lastMatchedFrame.reset(); m_lastMatchedStep.reset(); m_stepsPerMacroFrame.reset();
    m_currentPre.reset(); m_lastPre.reset(); m_lastPost.reset();
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
    m_attempt = true; m_failed = false; m_error.clear(); m_index = 0; m_stepSerial = 0;
    m_actualDown = {false, false};
    m_lastMatchedFrame.reset(); m_lastMatchedStep.reset(); m_stepsPerMacroFrame.reset();
    m_currentPre.reset(); m_lastPre.reset(); m_lastPost.reset(); m_result.inputs.clear();
    Diagnostics::get().event("trace_attempt_begin");
    Notification::create("HoldForge: recording macro trace", NotificationIcon::Info, 2.f)->show();
}

void TraceRecorder::onStepBegin(GJBaseGameLayer* layer) {
    if (!active() || layer != LevelEditorLayer::get()) return;
    ++m_stepSerial;
    m_currentPre = snapshot(layer, m_stepSerial);
}

void TraceRecorder::onStepEnd(GJBaseGameLayer* layer) {
    if (!active() || layer != LevelEditorLayer::get() || !m_currentPre) return;
    m_lastPre = *m_currentPre;
    m_lastPost = snapshot(layer, m_currentPre->serial);
    m_currentPre.reset();
}

void TraceRecorder::onButton(GJBaseGameLayer* layer, bool down, int button, bool player1) {
    if (!active() || layer != LevelEditorLayer::get() || button != 1) return;
    bool p2 = m_twoPlayer ? !player1 : false;
    size_t p = p2 ? 1 : 0;
    if (m_actualDown[p] == down) {
        auto row = matjson::Value::object(); row["down"] = down; row["p2"] = p2; row["ignored_duplicate"] = true;
        Diagnostics::get().event("trace_actual_input", row);
        return;
    }
    m_actualDown[p] = down;
    if (m_index >= m_expected.size()) { fail("TRACE_EXTRA_INPUT", "More jump edges were observed than the macro contains"); return; }
    auto const expected = m_expected[m_index];
    if (expected.down != down || expected.p2 != p2) {
        fail("TRACE_INPUT_ORDER", fmt::format("Expected {} {} at macro frame {}, observed {} {}",
            expected.p2 ? "P2" : "P1", expected.down ? "press" : "release", expected.frame,
            p2 ? "P2" : "P1", down ? "press" : "release"));
        return;
    }

    // Do not assume where Silicate's processQueuedButtons sits relative to processCommands.
    // If handleButton is observed inside a command step, that step is the input step;
    // if it is observed between steps, the input applies to the next command step.
    bool insideStep = m_currentPre.has_value();
    uint64_t inputStep = insideStep ? m_currentPre->serial : m_stepSerial + 1;

    // Verify the temporal signature without assuming m_currentProgress == macro frame.
    // The scale between Silicate frames and GD command steps is learned from observed
    // edges (it happened to be 2 in one old log, but that is never hard-coded).
    if (m_lastMatchedFrame && m_lastMatchedStep) {
        uint64_t df = expected.frame - *m_lastMatchedFrame;
        uint64_t ds = inputStep - *m_lastMatchedStep;
        if (df == 0) {
            if (ds != 0) { fail("TRACE_TIMING", "Same-frame macro edges arrived in different physics command steps"); return; }
        } else if (!m_stepsPerMacroFrame) {
            double scale = static_cast<double>(ds) / static_cast<double>(df);
            if (!std::isfinite(scale) || scale <= 0.0 || scale > 16.0) {
                fail("TRACE_TIMING", "Could not establish a stable macro-frame/physics-step relation"); return;
            }
            m_stepsPerMacroFrame = scale;
        } else {
            double expectedSteps = static_cast<double>(df) * *m_stepsPerMacroFrame;
            double tolerance = std::max(1.0, expectedSteps * 0.0025);
            if (std::abs(static_cast<double>(ds) - expectedSteps) > tolerance) {
                fail("TRACE_TIMING", "Observed input spacing does not match the armed macro"); return;
            }
        }
    }
    m_lastMatchedFrame = expected.frame; m_lastMatchedStep = inputStep;

    auto at = snapshot(layer, inputStep);
    auto diag = matjson::Value::object();
    diag["edge_index"] = m_index; diag["macro_frame"] = expected.frame;
    diag["down"] = down; diag["p2"] = p2; diag["inside_process_commands"] = insideStep;
    diag["input_step"] = inputStep; diag["at_input"] = stepJson(at);
    if (m_stepsPerMacroFrame) diag["steps_per_macro_frame"] = *m_stepsPerMacroFrame;

    if (expected.frame > 0) {
        if (!m_lastPre || !m_lastPost || m_lastPre->serial != m_lastPost->serial ||
            m_lastPost->serial + 1 != inputStep) {
            fail("TRACE_PHASE", "No immediately preceding completed physics step for this input edge");
            return;
        }
        double preX = m_lastPre->p1.x, preY = m_lastPre->p1.y;
        double postX = m_lastPost->p1.x, postY = m_lastPost->p1.y;
        if (!std::isfinite(preX) || !std::isfinite(postX) || postX <= preX + 1e-6) {
            fail("TRACE_NON_FORWARD", "Previous physics step did not cross forward in X; reverse/teleport needs separate native validation");
            return;
        }
        double triggerX = std::midpoint(preX, postX);
        if (!m_result.inputs.empty() && triggerX + 1e-6 < m_result.inputs.back().triggerX) {
            fail("TRACE_NON_MONOTONIC", "Recorded gate positions moved backwards");
            return;
        }
        RecordedInput row;
        row.frame = expected.frame; row.down = down; row.p2 = p2; row.step = inputStep;
        auto target = p2 ? at.p2 : at.p1;
        row.inputX = target.x; row.inputY = target.y;
        row.phasePreX = preX; row.phasePreY = preY; row.phasePostX = postX; row.phasePostY = postY;
        row.triggerX = triggerX; row.dual = at.dual;
        m_result.inputs.push_back(row);
        diag["phase_before"] = stepJson(*m_lastPre);
        diag["phase_after"] = stepJson(*m_lastPost);
        diag["trigger_x"] = triggerX;
    } else {
        diag["frame_zero"] = true;
    }
    Diagnostics::get().event("trace_actual_input", diag);
    ++m_index;
}

void TraceRecorder::onDamage(GJBaseGameLayer* layer, PlayerObject*) {
    if (active() && layer == LevelEditorLayer::get())
        fail("TRACE_DEATH", "Source macro attempt took damage before completion");
}

void TraceRecorder::onPlaytestStop(LevelEditorLayer* editor) {
    if (!m_armed || !m_attempt) return;
    bool completed = editor && editor->m_levelEndAnimationStarted;
    if (!m_failed && m_index != m_expected.size())
        fail("TRACE_INCOMPLETE", fmt::format("Playtest stopped after {}/{} expected edges", m_index, m_expected.size()));
    if (!m_failed && !completed)
        fail("TRACE_NOT_COMPLETE", "All edges matched, but the level was not completed; partial attempts are rejected");

    if (!m_failed) {
        try {
            writeCalibration(m_path, m_result);
            auto row = matjson::Value::object(); row["path"] = utils::string::pathToString(m_path);
            row["inputs"] = m_result.inputs.size(); row["complete"] = true;
            row["macro_hash"] = fmt::format("{:016x}", m_result.macroHash);
            row["level_hash"] = fmt::format("{:016x}", m_result.levelHash);
            Diagnostics::get().set("automatic_trace", row);
            Notification::create("HoldForge: trace saved", NotificationIcon::Success, 3.f)->show();
        } catch (std::exception const& e) {
            fail("TRACE_WRITE", e.what());
        }
    }
    if (m_failed)
        Notification::create("HoldForge: trace rejected - export logs", NotificationIcon::Error, 4.f)->show();
    m_attempt = false; m_armed = false;
}
}
