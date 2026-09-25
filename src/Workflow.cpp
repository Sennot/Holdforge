#include "Workflow.hpp"
#include "Diagnostics.hpp"
#include <cmath>
#include <algorithm>
#include <fstream>
using namespace geode::prelude;
namespace hf {
namespace {
int mode(PlayerObject* p) {
    return !p ? 0 : p->m_isShip ? 1 : p->m_isBird ? 2 : p->m_isBall ? 3 : p->m_isDart ? 4 : p->m_isRobot ? 5 : p->m_isSpider ? 6 : p->m_isSwing ? 7 : 0;
}
bool held(PlayerObject* p) {
    if (!p) return false;
    auto it = p->m_holdingButtons.find(1); return it != p->m_holdingButtons.end() && it->second;
}
}
Sample sample(GJBaseGameLayer* l) {
    Sample s; s.time = l->m_gameState.m_levelTime; s.dual = l->m_gameState.m_isDualMode;
    if (auto p = l->m_player1) { s.x = p->m_position.x; s.y = p->m_position.y; s.mode1 = mode(p); s.held1 = held(p); s.velocity1 = p->m_yVelocity; s.size1 = p->m_vehicleSize; s.upside1 = p->m_isUpsideDown; s.disabled1 = p->m_controlsDisabled; }
    if (auto p = l->m_player2) { s.p2x = p->m_position.x; s.p2y = p->m_position.y; s.mode2 = mode(p); s.held2 = held(p); s.velocity2 = p->m_yVelocity; s.size2 = p->m_vehicleSize; s.upside2 = p->m_isUpsideDown; s.disabled2 = p->m_controlsDisabled; }
    return s;
}
matjson::Value sampleJson(Sample const& s) {
    auto j = matjson::Value::object(); j["time"] = s.time; j["x"] = s.x; j["y"] = s.y;
    j["p2_x"] = s.p2x; j["p2_y"] = s.p2y; j["dual"] = s.dual;
    j["mode1"] = s.mode1; j["mode2"] = s.mode2; j["held1"] = s.held1; j["held2"] = s.held2;
    j["velocity1"] = s.velocity1; j["velocity2"] = s.velocity2;
    j["size1"] = s.size1; j["size2"] = s.size2;
    j["upside1"] = s.upside1; j["upside2"] = s.upside2;
    j["disabled1"] = s.disabled1; j["disabled2"] = s.disabled2; return j;
}
Workflow& Workflow::get() { static Workflow w; return w; }
bool Workflow::sameSessionLevel(GJGameLevel* level) const {
    return level && m_target && (level == m_target.data() ||
        (!m_savedLevelData.empty() && std::string(level->m_levelString) == m_savedLevelData));
}
Workflow::EditorState Workflow::editorState(LevelEditorLayer* editor) const {
    if (!m_replay) return EditorState::Unselected;
    if (!editor || !editor->m_levelSettings) return EditorState::Other;
    auto current = std::string(editor->getLevelString());
    auto state = classifyEditorSession(true, sameSessionLevel(editor->m_level),
        editor->m_levelSettings->m_twoPlayerMode == m_twoPlayer, m_source, m_holdSource, current);
    if (state == EditorState::Source || state == EditorState::Generated) return state;
    auto j = matjson::Value::object(); j["source_hash"] = fmt::format("{:016x}", fingerprint(m_source));
    j["current_hash"] = fmt::format("{:016x}", fingerprint(current));
    j["source_content_hash"] = fmt::format("{:016x}", levelFingerprint(m_source));
    j["current_content_hash"] = fmt::format("{:016x}", levelFingerprint(current));
    j["same_session_level"] = sameSessionLevel(editor->m_level);
    j["recording_retained"] = m_trajectory.has_value();
    j["recorded_two_player"] = m_twoPlayer; j["current_two_player"] = editor->m_levelSettings->m_twoPlayerMode;
    j["source_bytes"] = m_source.size(); j["current_bytes"] = current.size();
    j["first_different_byte"] = static_cast<size_t>(std::mismatch(m_source.begin(), m_source.end(), current.begin(), current.end()).first-m_source.begin());
    auto diff = firstLevelDifference(m_source, current);
    j["first_changed_record"] = diff.record; j["first_changed_property"] = diff.property;
    j["expected_value"] = diff.expected; j["actual_value"] = diff.actual;
    Diagnostics::get().set("editor_session_changed", j);
    return state;
}
std::filesystem::path Workflow::cachePath() const {
    return Mod::get()->getSaveDir() / "trajectories" / fmt::format("level-v1-{:016x}-{:016x}-{}.hftrace4", m_replay->fingerprint, levelFingerprint(m_source), m_twoPlayer ? 2 : 1);
}
bool Workflow::target(PlayLayer* l) const {
    return l && m_target && (l->m_level == m_target.data() ||
        (l->m_level && !m_target->m_levelString.empty() && l->m_level->m_levelString == m_target->m_levelString));
}
void Workflow::warn(std::string const& code, std::string const& message) {
    if (!m_warnings.emplace(code, message).second) return;
    m_status = "Warning: " + message;
    auto j = matjson::Value::object(); j["code"] = code; j["message"] = message;
    if (m_recorder) { j["captured"] = m_recorder->count(); j["expected"] = m_recorder->expected(); }
    Diagnostics::get().event("workflow_warning", j);
    auto list = matjson::Value::array();
    for (auto const& [key, value] : m_warnings) {
        auto row = matjson::Value::object(); row["code"] = key; row["message"] = value; list.push(row);
    }
    Diagnostics::get().set("workflow_warnings", list);
}
void Workflow::recorderWarnings() {
    if (!m_recorder) return;
    for (auto const& w : m_recorder->warnings()) {
        auto colon = w.find(':'); warn(w.substr(0, colon), colon == std::string::npos ? w : w.substr(colon+2));
    }
}
void Workflow::saveRecording(Trajectory result) {
    if (!result.completed && m_trajectory && m_trajectory->levelHash == result.levelHash &&
        m_trajectory->macroHash == result.macroHash && m_trajectory->steps.size() > result.steps.size())
        result = *m_trajectory;
    result.advisory = true;
    for (auto const& [code, message] : m_warnings) {
        auto w = code + ": " + message;
        if (result.warnings.size() < 128 && std::find(result.warnings.begin(), result.warnings.end(), w) == result.warnings.end())
            result.warnings.push_back(std::move(w));
    }
    // Keep the usable recording even if disk I/O fails.
    m_trajectory = std::move(result);
    m_status = m_trajectory->completed ? "Recording ready - Analyze then Create" : "Partial recording kept - Analyze then Create";
    if (!m_trajectory->warnings.empty()) m_status += " (warnings)";
    try {
        auto path = cachePath(); std::filesystem::create_directories(path.parent_path());
        auto temp = path; temp += ".tmp";
        { std::ofstream out(temp, std::ios::trunc); writeTrajectory(out, *m_trajectory); out.flush();
          if (!out) throw Error("CAPTURE_SAVE", "Could not flush trajectory cache"); }
        std::error_code ec; std::filesystem::remove(path, ec); std::filesystem::rename(temp, path);
    } catch (std::exception const& e) { warn("CAPTURE_SAVE", std::string("Recording retained in memory; cache not saved: ") + e.what()); }
    auto info = matjson::Value::object(); info["inputs"] = m_trajectory->inputs.size();
    info["completed"] = m_trajectory->completed; info["warnings"] = m_trajectory->warnings.size();
    info["saw_dual"] = m_trajectory->sawDual; info["physics_steps"] = m_trajectory->steps.size();
    info["clock_offset"] = m_trajectory->clockOffset;
    Diagnostics::get().set("capture_complete", info);
}
void Workflow::select(LevelEditorLayer* editor, Replay replay, std::string name) {
    if (!editor || !editor->m_levelSettings || editor->m_playbackMode != PlaybackMode::Not)
        throw Error("CAPTURE_EDITOR", "Open a stopped editor first");
    PlanConfig config; config.twoPlayer = editor->m_levelSettings->m_twoPlayerMode;
    config.warningsOnly = true;
    config.maxTriggers = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("max-triggers"));
    auto preview = plan(replay, config);
    m_warnings.clear();
    m_replay = std::move(replay); m_name = std::move(name); m_target = editor->m_level;
    m_savedLevelData = editor->m_level ? std::string(editor->m_level->m_levelString) : std::string{};
    m_twoPlayer = editor->m_levelSettings->m_twoPlayerMode;
    m_source = std::string(editor->getLevelString()); m_holdSource.clear(); m_gates.clear(); m_auto = false; m_manualDual = false; m_anchors.clear(); m_autoTargets.clear(); m_autoSeen.clear();
    m_mode = Mode::None; m_armed = false; m_layer = nullptr; m_recorder.reset(); m_trajectory.reset();
    m_status = "Macro loaded - Record a full replay";
    auto path = cachePath();
    bool legacy = false;
    if (!std::filesystem::exists(path)) path.replace_extension(".hftrace3");
    if (!std::filesystem::exists(path)) {
        // Old caches are reusable only when their ORIGINAL byte-exact hash
        // matches. Never guess which old recording belongs to a changed save.
        path = Mod::get()->getSaveDir() / "trajectories" / fmt::format("{:016x}-{:016x}-{}.hftrace3", m_replay->fingerprint, fingerprint(m_source), m_twoPlayer ? 2 : 1);
        legacy = true;
    }
    try { if (std::filesystem::exists(path)) {
        if (std::filesystem::file_size(path) > 256 * 1024 * 1024) throw Error("CAPTURE_CACHE", "Trajectory cache too large");
        std::ifstream in(path);
        m_trajectory = readTrajectory(in, *m_replay, legacy ? fingerprint(m_source) : levelFingerprint(m_source), m_twoPlayer);
        if (legacy) m_trajectory->levelHash = levelFingerprint(m_source);
        m_status = "Recorded trajectory loaded - Analyze";
    } } catch (std::exception const& e) { warn("CAPTURE_CACHE", std::string("Cache skipped; macro remains loaded: ") + e.what()); }
    for (auto const& w : preview.warnings) Diagnostics::get().event("import_warning", w);
    Diagnostics::get().set("trajectory_loaded", m_trajectory.has_value());
}
void Workflow::armRecord(LevelEditorLayer* editor) {
    auto state = editorState(editor);
    if (!m_replay || !editor || !editor->m_levelSettings)
        throw Error("CAPTURE_LEVEL", "Select a macro for this level first");
    m_warnings.clear();
    if (state == EditorState::Other) warn("CAPTURE_LEVEL", "Reusing the selected macro on a different level; recording this level now.");
    if (state == EditorState::Generated)
        warn("CAPTURE_GENERATED", "Recording generated level: its existing triggers may affect the macro.");
    bool twoPlayer = editor->m_levelSettings->m_twoPlayerMode;
    // An explicit Record is enough to capture the current source snapshot;
    // retaining the selected macro must not force another file picker cycle.
    m_source = std::string(editor->getLevelString()); m_twoPlayer = twoPlayer;
    m_holdSource.clear(); m_gates.clear(); m_auto = false; m_manualDual = false;
    m_anchors.clear(); m_autoTargets.clear(); m_autoSeen.clear();
    m_savedLevelData = editor->m_level ? std::string(editor->m_level->m_levelString) : std::string{};
    m_target = editor->m_level; m_trajectory.reset(); m_recorder.reset(); m_layer = nullptr;
    m_mode = Mode::Record; m_armed = true;
    m_status = "Record armed - Save and Exit, then normal Play + Silicate";
    Diagnostics::get().set("capture_status", "armed");
}
void Workflow::generated(LevelEditorLayer* editor, Prepared const& prepared) {
    m_twoPlayer = editor->m_levelSettings->m_twoPlayerMode;
    m_manualDual = prepared.manualDual;
    m_target = editor->m_level; m_holdSource = std::string(editor->getLevelString()); m_gates = prepared.placements; m_auto = !prepared.autoPath.anchors.empty();
    m_anchors = prepared.autoPath.anchors; m_autoTargets.clear();
    for (size_t i=0; i<m_anchors.size(); ++i) m_autoTargets.emplace(m_anchors[i].targetGroup, i);
    m_status = m_manualDual ? "Generated - edit duals, then normal Play with macro OFF" : "Generated - Verify with macro playback OFF";
    if (prepared.autoRequested && prepared.autoPath.anchors.empty())
        m_status = "Warning: Options created, 0 dual helpers - Export logs";
    else if (prepared.autoPath.anchors.size() < prepared.autoPath.eligibleSteps)
        m_status = "Generated with partial dual coverage - check gaps";
    Diagnostics::get().set("native_verification", "not_tested");
}
void Workflow::armVerify(LevelEditorLayer* editor) {
    if (!editor || !editor->m_levelSettings) throw Error("EDITOR_CLOSED", "Open the editor first");
    m_warnings.clear();
    if (m_manualDual)
        warn("VERIFY_MANUAL_DUAL", "Manual dual edits may differ from the reference path; Verify will continue.");
    if (!m_trajectory || m_gates.empty() || editorState(editor) != EditorState::Generated)
        warn("VERIFY_LEVEL", "Missing reference or changed level: Verify will observe available data only.");
    m_target = editor->m_level; m_layer = nullptr; m_mode = Mode::Verify; m_armed = true;
    m_status = "Verify armed - macro OFF, hold input from start";
    Diagnostics::get().set("native_verification", "armed");
}
void Workflow::attempt(PlayLayer* layer, bool reset) {
    if (m_mode != Mode::None && !m_layer.valid()) m_armed = true;
    if (m_mode == Mode::None || !target(layer) || (!reset && m_layer == layer)) return;
    if (!m_armed && m_layer != layer) return;
    m_layer = layer; m_armed = false;
    if (layer->m_isPracticeMode || layer->m_startPosObject || layer->m_gameState.m_levelTime > 1.5 / 240.0) {
        warn("FULL_START_REQUIRED", "Practice/StartPos or late start: missing sections remain unverified.");
    }
    m_checkIndex = 0; m_gateIndex = 0; m_stepIndex = 0; m_maxDualError = 0; m_autoActivated = 0; m_autoSeen.assign(m_anchors.size(), false); m_verifyHeld = {false, false}; m_presses = {0, 0};
    if (m_mode == Mode::Record) {
        m_recorder = std::make_unique<Recorder>(*m_replay, levelFingerprint(m_source), m_twoPlayer, true);
        if (layer->m_level) m_savedLevelData = std::string(layer->m_level->m_levelString);
        m_status = "Recording replay";
    } else m_status = "Checking native hold";
    Diagnostics::get().set("workflow_attempt", m_mode == Mode::Record ? "record" : "native_verify");
}
void Workflow::resetBegin(PlayLayer* layer) {
    if (m_mode == Mode::None || !target(layer)) return;
    if (m_recorder) {
        // A real game reset starts a fresh capture, but keep the previous
        // attempt in memory until the next one contains useful data.
        auto previous = m_recorder->partial();
        if (!previous.steps.empty() && (!m_trajectory || previous.steps.size() > m_trajectory->steps.size())) m_trajectory = std::move(previous);
    }
    m_layer = nullptr; m_recorder.reset(); m_armed = true;
}
void Workflow::stepBegin(GJBaseGameLayer* l) {
    // Silicate can consume frame-zero inputs inside resetLevel itself. Attach
    // on the first queue call, then preserve those events at reset's return.
    if (m_armed && l->m_player1)
        attempt(typeinfo_cast<PlayLayer*>(l), false);
    if (!active(l)) return;
    try { if (m_recorder && m_mode == Mode::Record) m_recorder->step(sample(l)); }
    catch (Error const& e) { warn(e.code, e.what()); }
    recorderWarnings();
}
void Workflow::stepEnd(GJBaseGameLayer* l) {
    if (!active(l)) return;
    auto s = sample(l);
    if (m_mode == Mode::Record) {
        try { m_recorder->stepEnd(s); } catch (Error const& e) { warn(e.code, e.what()); }
        recorderWarnings();
        return;
    }
    if (!m_trajectory) return;
    // Approximate native path helpers do not reproduce raw P2 button state.
    // Check the continuous path even if no macro inputs occur in the dual.
    while (m_auto && m_stepIndex < m_trajectory->steps.size()) {
        auto const& e = m_trajectory->steps[m_stepIndex];
        if (s.time < e.time - .25/240.0) break;
        if (e.dual || s.dual) {
            double delta = std::max(std::abs(s.p2x-e.p2x), std::abs(s.p2y-e.p2y));
            m_maxDualError = std::max(m_maxDualError, delta);
            bool phase = std::abs(s.time-e.time) <= .25/240.0;
            bool p1 = std::abs(s.x-e.x) <= .5 && std::abs(s.y-e.y) <= .5 && s.mode1 == e.mode1;
            bool p2 = s.dual == e.dual && (!e.dual || (delta <= 6 && s.mode2 == e.mode2));
            if (!phase || !p1 || !p2) {
                auto j = matjson::Value::object(); j["expected"] = sampleJson(e); j["actual"] = sampleJson(s);
                j["phase_ok"] = phase; j["p1_ok"] = p1; j["p2_ok"] = p2; j["max_dual_error"] = m_maxDualError;
                j["step"] = m_stepIndex; j["tolerance"] = 6;
                if (!m_warnings.contains("VERIFY_AUTO")) Diagnostics::get().set("auto_first_mismatch", j);
                warn("VERIFY_AUTO", fmt::format("Invisible dual differs at {:.4f}s (form {}, error {:.2f}). Verify continues", s.time, e.mode2, delta));
            }
        }
        ++m_stepIndex;
    }
    while (m_checkIndex < m_trajectory->inputs.size()) {
        auto const& r = m_trajectory->inputs[m_checkIndex];
        if (s.time < r.sample.time - .25 / 240.0) break;
        if (!r.hasAfter) { warn("VERIFY_PHASE", "Some reference edges lack a post-input snapshot."); ++m_checkIndex; continue; }
        auto const& e = r.after;
        bool phase = std::abs(s.time - r.sample.time) <= .25 / 240.0;
        bool p1 = std::abs(s.x-e.x) <= .5 && std::abs(s.y-e.y) <= .5 && s.mode1 == e.mode1;
        bool p2 = s.dual == e.dual && (!e.dual || (std::abs(s.p2x-e.p2x) <= (m_auto ? 6 : .5) && std::abs(s.p2y-e.p2y) <= (m_auto ? 6 : .5) && s.mode2 == e.mode2));
        auto report = matjson::Value::object(); report["frame"] = r.frame; report["expected"] = sampleJson(e);
        report["actual"] = sampleJson(s); report["phase_ok"] = phase; report["p1_ok"] = p1; report["p2_ok"] = p2;
        Diagnostics::get().event("native_comparison", report);
        if (!phase || !p1 || !p2) {
            if (!m_warnings.contains("VERIFY_PHASE") && !m_warnings.contains("VERIFY_P1") && !m_warnings.contains("VERIFY_P2")) Diagnostics::get().set("native_first_mismatch", report);
            warn(!phase ? "VERIFY_PHASE" : !p1 ? "VERIFY_P1" : "VERIFY_P2", fmt::format("Native hold differs at macro frame {}. Verify continues", r.frame));
        }
        ++m_checkIndex;
    }
}
void Workflow::button(GJBaseGameLayer* l, bool down, int b, bool player1, bool queued) {
    if (!active(l) || b != 1) return;
    bool p2 = m_twoPlayer && !player1;
    if (m_mode == Mode::Record) {
        if (!queued) warn("CAPTURE_PHASE", "Input outside physics queue; position may be approximate.");
        try {
            auto s = sample(l);
            if (m_recorder->input(down, p2, s)) {
                auto j = sampleJson(s); j["input_index"] = m_recorder->count()-1; j["down"] = down; j["p2"] = p2;
                Diagnostics::get().event("capture_input", j);
            }
        } catch (Error const& e) { warn(e.code, e.what()); }
        recorderWarnings();
    } else {
        // Opening normal Play can carry its mouse-down into the game. Permit
        // releasing that UI click before any required macro press, at most
        // the first .25 seconds; subsequent play must be one continuous hold.
        double grace = .25;
        if (m_trajectory) for (auto const& edge : m_trajectory->inputs) if (edge.down && edge.p2 == p2) {
            grace = std::min(grace, std::max(0.0, edge.sample.time-1.0/240.0)); break;
        }
        if (!down && l->m_gameState.m_levelTime < grace) {
            m_verifyHeld[p2] = false; m_presses[p2] = 0; return;
        }
        if (down && !m_verifyHeld[p2]) ++m_presses[p2];
        m_verifyHeld[p2] = down;
        if (m_presses[p2] > 1 || (!down && l->m_gameState.m_levelTime > .1))
            warn("VERIFY_INPUT", "Observed input differs from a continuous hold; Verify continues.");
    }
}
void Workflow::options(GJBaseGameLayer* l, GameOptionsTrigger* o) {
    if (m_mode == Mode::Verify && m_armed && l->m_player1 && l->m_gameState.m_levelTime <= 1.5 / 240.0)
        attempt(typeinfo_cast<PlayLayer*>(l), false);
    if (!active(l) || !o || m_mode != Mode::Verify) return;
    if (m_gateIndex >= m_gates.size()) return;
    auto const& gate = m_gates[m_gateIndex];
    if (std::abs(o->getPositionX() - gate.x) > .06 || static_cast<int>(o->m_disableP1Controls) != gate.gate.p1 || static_cast<int>(o->m_disableP2Controls) != gate.gate.p2) return;
    ++m_gateIndex;
}
void Workflow::teleported(GJBaseGameLayer* l, TeleportPortalObject* portal, PlayerObject* player, Sample const& before) {
    if (!active(l) || m_mode != Mode::Verify || !m_auto || !portal) return;
    auto it = m_autoTargets.find(portal->m_targetGroupID);
    if (it == m_autoTargets.end()) return;
    auto const& a = m_anchors[it->second];
    auto after = sample(l);
    auto j = matjson::Value::object(); j["anchor"] = it->second; j["reference_time"] = a.time;
    j["target_group"] = a.targetGroup; j["player2"] = player == l->m_player2;
    j["expected_y"] = a.targetY; j["before"] = sampleJson(before); j["after"] = sampleJson(after);
    Diagnostics::get().event("auto_portal_activated", j);
    if (player != l->m_player2) {
        Diagnostics::get().set("auto_wrong_player", j);
        warn("AUTO_HIT_P1", "A generated portal caught P1. Verify continues; inspect this section."); return;
    }
    if (std::abs(after.p2y-a.targetY) > .15 || std::abs(after.p2x-before.p2x) > .01) {
        Diagnostics::get().set("auto_portal_mismatch", j);
        warn("AUTO_TELEPORT", "Native portal did not preserve X / reach the recorded Y. Verify continues.");
    }
    if (!m_autoSeen[it->second]) { m_autoSeen[it->second] = true; ++m_autoActivated; }
}
void Workflow::damaged(PlayLayer* l, PlayerObject*, GameObject* object) {
    if (!active(l) || object == l->m_anticheatSpike) return;
    auto data = sampleJson(sample(l));
    if (object) data["object_id"] = object->m_objectID;
    Diagnostics::get().set("workflow_damage", data);
    // A callback is neither proof of death nor evidence of a particular mod.
    warn("DAMAGE_CALLBACK", "Game reported a damage callback. Death is not confirmed; recording/Verify continues.");
}
void Workflow::complete(PlayLayer* l) {
    if (!active(l)) return;
    if (m_mode == Mode::Record) {
        try {
            auto result = m_recorder->finish(l->m_gameState.m_levelTime);
            recorderWarnings(); saveRecording(std::move(result));
        } catch (Error const& e) {
            warn(e.code, e.what()); saveRecording(m_recorder->partial());
        }
    } else {
        if (!m_trajectory || !m_trajectory->completed || !m_trajectory->warnings.empty())
            warn("VERIFY_REFERENCE", "Reference is missing, partial or carries warnings; result is unverified.");
        if (!m_trajectory || m_checkIndex != m_trajectory->inputs.size() || m_gateIndex != m_gates.size() ||
            m_gates.empty() || !m_presses[0] || (m_twoPlayer && !m_presses[1]))
            warn("VERIFY_INCOMPLETE", "Not all reference inputs, Options or physical holds were observed.");
        if (m_auto && (!m_trajectory || m_autoActivated != m_anchors.size() || m_stepIndex+1 < m_trajectory->steps.size()))
            warn("VERIFY_AUTO_INCOMPLETE", "Not every invisible portal/physics step was observed.");
        Diagnostics::get().set("auto_portals_verified", m_autoActivated);
        Diagnostics::get().set("max_dual_path_error", m_maxDualError);
        Diagnostics::get().set("verified_physics_steps", m_stepIndex);
        bool matched = m_warnings.empty();
        m_status = matched ? "Native check matched here - test without mods before publishing" : "Verify finished with warnings - Export logs";
        Diagnostics::get().set("native_verification", matched ? "matched_in_this_mod_environment" : "completed_with_warnings_unverified");
    }
    m_mode = Mode::None; m_armed = false; m_layer = nullptr; m_recorder.reset();
}
void Workflow::leave(PlayLayer* layer) {
    if (active(layer)) {
        warn("RUN_INCOMPLETE", "Left before completion; recorded portions are kept and missing sections use timeline mapping.");
        if (m_mode == Mode::Record && m_recorder) { recorderWarnings(); saveRecording(m_recorder->partial()); }
        else Diagnostics::get().set("native_verification", "interrupted_with_warnings_unverified");
        m_mode = Mode::None; m_armed = false; m_recorder.reset();
    }
    if (m_layer == layer) m_layer = nullptr;
}
void Workflow::sceneExit(PlayLayer* layer) {
    if (!active(layer)) return;
    // CCNode::onExit is also used by scene transitions/overlays. The report
    // showed Verify being cancelled here while the same run continued.
    // Explicit onQuit/levelComplete end the session; a WeakRef guards lifetime.
    auto info = sampleJson(sample(layer));
    info["mode"] = m_mode == Mode::Record ? "record" : "verify";
    info["session_retained"] = true;
    Diagnostics::get().event("workflow_scene_exit", info);
    if (m_mode == Mode::Record && m_recorder && m_recorder->count()) {
        recorderWarnings(); saveRecording(m_recorder->partial());
        m_status = "Recording retained across scene transition";
    }
}
}
