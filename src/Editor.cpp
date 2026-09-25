#include "Editor.hpp"
#include "Diagnostics.hpp"
#include "core/NativeObjects.hpp"
#include "core/ManualDual.hpp"
#include "core/LevelIdentity.hpp"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
using namespace geode::prelude;
namespace hf {
Prepared prepare(LevelEditorLayer* editor, Replay const& replay, Trajectory const* calibration, bool requireRecording) {
    if (!editor || LevelEditorLayer::get() != editor) throw Error("EDITOR_CLOSED", "Editor session changed");
    if (editor->m_playbackMode != PlaybackMode::Not || editor->m_playbackActive)
        throw Error("EDITOR_PLAYING", "Stop playtest and music playback first");
    auto settings = editor->m_levelSettings;
    if (!settings) throw Error("EDITOR_SETTINGS", "Editor settings unavailable");
    auto mod = Mod::get(); auto& debug = Diagnostics::get();
    Prepared out;
    out.levelBefore = std::string(editor->getLevelString());
    auto info = matjson::Value::object();
    info["two_player"] = settings->m_twoPlayerMode; info["start_dual"] = settings->m_startDual;
    info["start_speed"] = static_cast<int>(settings->m_startSpeed);
    info["objects"] = editor->m_objects->count();
    uint64_t hash = levelFingerprint(out.levelBefore);
    info["fingerprint_fnv1a64"] = fmt::format("{:016x}", fingerprint(out.levelBefore));
    info["content_hash"] = fmt::format("{:016x}", hash); info["identity_version"] = "level-v1";
    info["serialized_bytes"] = out.levelBefore.size(); debug.set("level", info);
    // Conservative gate: these mechanics can make an editor timeline differ from actual movement.
    std::unordered_map<int, std::string> risks{
        {1917, "Reverse"}, {1935, "TimeWarp"}, {3022, "Teleport trigger"},
        {747, "Teleport portal"}, {749, "Teleport exit"}, {native::unlinkedPortal, "Unlinked teleport portal"},
        {native::unlinkedExit, "Unlinked teleport exit"}, {3027, "Teleport orb"}, {1704, "Dash orb"},
        {1751, "Gravity dash orb"}, {2900, "Gameplay rotation"},
        {2901, "Gameplay offset"}, {1932, "Player Control"}
    };
    std::vector<std::string> warnings;
    auto warning = [&](std::string const& message) {
        if (std::find(warnings.begin(), warnings.end(), message) == warnings.end()) warnings.push_back(message);
    };
    if (settings->m_platformerMode) warning("Platformer: only jump gates are generated; directional input is omitted.");
    if (requireRecording && !calibration) warning("No recording: using approximate editor timeline positions.");
    bool containsDual = settings->m_startDual;
    if (settings->m_rotateGameplay) warning("Rotated gameplay: generated positions may need manual adjustment.");
    for (auto obj : CCArrayExt<GameObject*>(editor->m_objects)) {
        containsDual |= obj->m_objectID == 286;
        if (auto options = typeinfo_cast<GameOptionsTrigger*>(obj)) {
            if ((options->m_disableP1Controls != GameOptionsSetting::Disabled ||
                 options->m_disableP2Controls != GameOptionsSetting::Disabled))
                warning("Existing control Options may override generated gates; creation is allowed.");
        }
        auto it = risks.find(obj->m_objectID);
        if (it != risks.end()) {
            auto warning = it->second + " present: generated timing requires manual validation.";
            if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) warnings.push_back(warning);
        }
    }
    bool manualRequested = !settings->m_twoPlayerMode && mod->getSettingValue<bool>("manual-duals");
    if (requireRecording && !calibration && containsDual && !settings->m_twoPlayerMode &&
        (mod->getSettingValue<bool>("dual-auto") || manualRequested))
        warning("Dual trajectory unavailable: creating Options only; dual sections need manual work.");
    PlanConfig cfg;
    cfg.twoPlayer = settings->m_twoPlayerMode; cfg.warningsOnly = true;
    cfg.offsetMs = mod->getSettingValue<double>("offset-ms");
    cfg.maxTriggers = static_cast<size_t>(mod->getSettingValue<int64_t>("max-triggers"));
    cfg.sharedP1Only = !manualRequested && mod->getSettingValue<bool>("shared-p1-only");
    auto effective = matjson::Value::object();
    effective["two_player"] = cfg.twoPlayer; effective["validation_policy"] = "warnings_only";
    effective["offset_ms"] = cfg.offsetMs;
    effective["x_offset"] = mod->getSettingValue<double>("x-offset");
    effective["native_only"] = true;
    effective["shared_p1_only"] = cfg.sharedP1Only;
    out.plan = plan(replay, cfg);
    out.manualDual = calibration && calibration->sawDual && manualRequested;
    bool autoDual = calibration && calibration->sawDual && !cfg.twoPlayer && !manualRequested && mod->getSettingValue<bool>("dual-auto");
    effective["invisible_dual_auto"] = autoDual;
    effective["manual_duals"] = out.manualDual;
    if (calibration) {
        auto identity = matjson::Value::object();
        identity["complete"] = calibration->completed;
        identity["recorded_level_hash"] = fmt::format("{:016x}", calibration->levelHash);
        identity["current_level_hash"] = fmt::format("{:016x}", hash);
        identity["recorded_macro_hash"] = fmt::format("{:016x}", calibration->macroHash);
        identity["current_macro_hash"] = fmt::format("{:016x}", replay.fingerprint);
        identity["recorded_two_player"] = calibration->twoPlayer;
        identity["current_two_player"] = cfg.twoPlayer;
        debug.set("trace_identity", identity);
        if (!calibration->completed)
            warning("Partial recording: missing inputs use approximate editor timeline positions.");
        if (calibration->macroHash != replay.fingerprint)
            warning("Recording macro differs: using available recorded frames; inspect generated positions.");
        if (calibration->twoPlayer != cfg.twoPlayer)
            warning("Recorded Two Player Mode differs from this level; inspect P1/P2 gates.");
        if (calibration->levelHash != hash)
            warning("Level differs from the recording: recorded positions are used with a warning.");
        if (cfg.offsetMs != 0 || mod->getSettingValue<double>("x-offset") != 0)
            warning("Offsets enabled: time offset uses editor timeline positions; X offset moves generated gates.");
        // Only the last generic editor-map warning is replaced. Preserve
        // meaningful planner warnings such as a missing independent P2 stream.
        if (!out.plan.warnings.empty()) out.plan.warnings.pop_back();
        out.plan.warnings.push_back("Recorded physics-step positions loaded. Use Verify, then test with HoldForge disabled before publishing.");
        if (calibration->sawDual && !cfg.twoPlayer && !autoDual && !out.manualDual)
            out.plan.warnings.push_back("Ordinary dual uses native control gates. P2 hold reactivation is not confirmed; Verify must check it. No runtime fixes are applied.");
    }
    effective["mapping_source"] = calibration ? "recorded_inputs" : "editor_timeline";
    debug.set("effective_conversion", effective);
    float y = static_cast<float>(mod->getSettingValue<double>("trigger-y"));
    float dx = static_cast<float>(mod->getSettingValue<double>("x-offset"));
    int layer = static_cast<int>(mod->getSettingValue<int64_t>("editor-layer"));
    float previous = -std::numeric_limits<float>::infinity();
    // Use GD's portal-aware time map. Never estimate x with frame * a fixed speed.
    editor->dirtifyTriggers();
    for (auto const& gate : out.plan.gates) {
        auto point = editor->posForTime(static_cast<float>(gate.seconds));
        if (calibration && cfg.offsetMs == 0) {
            try { point = CCPoint{gate.frame ? static_cast<float>(calibration->position(gate.frame)) : 0.f, y}; }
            catch (Error const&) { warning("Some recorded frames are missing: those gates use approximate editor timeline positions."); }
        }
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            throw Error("MAP_POSITION", "Game returned an invalid coordinate; cannot place this object");
        if (point.x <= previous) warning("Overlapping/reversed X positions: gates are still generated; inspect their order.");
        float back = editor->timeForPos(point, 0, 0, false, 0);
        if (!calibration && (!std::isfinite(back) || std::abs(back-gate.seconds) > 1.0/240.0))
            warning("Editor time/position roundtrip differs from macro timing; inspect generated positions.");
        previous = point.x;
        Placement p{gate, point.x + dx, y, {}};
        // max_digits10 preserves float coordinates through serialization.
        p.object = fmt::format("1,2899,2,{:.9g},3,{:.9g},20,{},165,{},199,{};", p.x, p.y, layer, gate.p1, gate.p2);
        out.placements.push_back(p);
    }
    auto basePlacements = out.placements;
    if (out.manualDual) try {
        std::vector<PositionedGate> gates;
        for (auto const& p : out.placements) gates.push_back({p.gate, p.x});
        auto result = manualDualGates(gates, *calibration, true);
        if (result.gates.size() > cfg.maxTriggers)
            warning("Manual dual gates exceed the configured object warning threshold.");
        out.placements.clear();
        for (auto const& p : result.gates) {
            auto object = fmt::format("1,2899,2,{:.9g},3,{:.9g},20,{},165,{},199,{};", p.x, y, layer, p.gate.p1, p.gate.p2);
            out.placements.push_back({p.gate, p.x, y, std::move(object)});
        }
        auto j = matjson::Value::object(); j["segments"] = result.segments;
        j["skipped_gates"] = result.skipped; j["final_gates"] = result.gates.size();
        debug.set("manual_duals", j);
        out.plan.warnings.push_back("Manual dual mode: both controls enabled at entry, no macro gates/helpers inside, macro state restored at exit. Build dual auto objects yourself. Test edited levels with normal Play.");
    }
    catch (Error const& e) {
        out.placements = basePlacements; out.manualDual = false;
        warning(std::string("Manual dual boundaries unavailable; Options kept: ") + e.what());
    }
    if (autoDual) try {
        // Detached native prototype: validate object ID/type and actual hitbox
        // before changing the editor. Never assume a Teleport trigger targets P2.
        debug.checkpoint("portal_probe_begin");
        auto object = GameObject::createWithKey(native::unlinkedPortal);
        auto prototype = typeinfo_cast<TeleportPortalObject*>(object);
        auto probe = matjson::Value::object(); probe["requested_id"] = native::unlinkedPortal;
        probe["created"] = object != nullptr; probe["teleport_class"] = prototype != nullptr;
        if (object) { probe["actual_id"] = object->m_objectID; probe["object_type"] = static_cast<int>(object->m_objectType); }
        if (prototype) { probe["yellow_exit"] = prototype->m_isYellowPortal; probe["linked_exit"] = prototype->m_orangePortal != nullptr; }
        debug.set("portal_probe", probe);
        if (!prototype || prototype->m_objectID != native::unlinkedPortal ||
            prototype->m_objectType != GameObjectType::TeleportPortal || prototype->m_isYellowPortal || prototype->m_orangePortal)
            throw Error("AUTO_PORTAL_TYPE", "Expected BLUE entrance 2902 (2064 is the orange exit). Runtime type differs; Export logs includes portal_probe. Use Manual dual sections to continue without auto helpers");
        prototype->setRScale(.5f);
        auto rect = prototype->getObjectRect();
        AutoPathConfig ac;
        ac.layer = layer; ac.warningsOnly = true; ac.maxObjects = cfg.maxTriggers > out.placements.size() ? cfg.maxTriggers - out.placements.size() : 0;
        ac.halfWidth = rect.size.width / 2; ac.halfHeight = rect.size.height / 2;
        probe["half_width"] = ac.halfWidth; probe["half_height"] = ac.halfHeight;
        debug.set("portal_probe", probe); debug.checkpoint("portal_probe_complete", probe);
        if (ac.halfWidth <= 0 || ac.halfHeight <= 0 || ac.halfWidth > 100 || ac.halfHeight > 100)
            throw Error("AUTO_PORTAL_BOUNDS", "Unexpected native portal hitbox; export logs");
        out.autoPath = makeAutoPath(*calibration, out.levelBefore, ac);
        for (auto const& w : out.autoPath.warnings) warning(w);
        std::vector<PositionedGate> sourceGates;
        for (auto const& p : basePlacements) sourceGates.push_back({p.gate, p.x});
        auto autoGates = autoControlGates(sourceGates, out.autoPath);
        out.placements.clear();
        for (auto const& p : autoGates) {
            auto object = fmt::format("1,2899,2,{:.9g},3,{:.9g},20,{},165,{},199,{};", p.x, y, layer, p.gate.p1, p.gate.p2);
            out.placements.push_back({p.gate, p.x, y, std::move(object)});
        }
        for (auto const& a : out.autoPath.anchors) {
            if (mod->getSettingValue<bool>("debug-mapping")) {
                auto j = matjson::Value::object(); j["step"] = a.step; j["time"] = a.time; j["mode"] = a.mode;
                j["x"] = a.x; j["y"] = a.y; j["target_y"] = a.targetY; j["on_x"] = a.onX; j["off_x"] = a.offX;
                j["portal_group"] = a.portalGroup; j["target_group"] = a.targetGroup;
                debug.event("auto_anchor", j);
            }
        }
        std::sort(out.placements.begin(), out.placements.end(), [](Placement const& a, Placement const& b) { return a.x < b.x; });
        if (out.placements.size() + out.autoPath.objects.size() > cfg.maxTriggers)
            warning("Generated objects exceed the configured object warning threshold.");
        auto j = matjson::Value::object(); j["anchors"] = out.autoPath.anchors.size(); j["objects"] = out.autoPath.objects.size();
        j["segments"] = out.autoPath.segments; j["portal_half_width"] = ac.halfWidth; j["portal_half_height"] = ac.halfHeight;
        j["modes"] = matjson::Value::array(); for (auto n : out.autoPath.modes) j["modes"].push(n);
        debug.set("invisible_dual_auto", j);
        out.plan.warnings.push_back("Experimental invisible dual path: native touch portals, 240 corrections/sec. Preserves forms; does not reproduce P2 input/rotation/gravity. Verify and test without mods.");
    }
    catch (Error const& e) {
        out.autoPath = {}; out.placements = basePlacements;
        warning(std::string("Invisible helpers unavailable; Options kept and P2 gates restored: ") + e.what());
    }
    if (calibration) for (auto const& w : calibration->warnings) warning(w);
    out.plan.warnings.insert(out.plan.warnings.end(), warnings.begin(), warnings.end());
    effective["invisible_dual_auto"] = !out.autoPath.anchors.empty();
    effective["manual_duals"] = out.manualDual;
    debug.set("effective_conversion", effective);
    // Report and draw the FINAL gates, including manual boundaries / auto-entry
    // blocks. Do not log gates that manual mode removed as actual placements.
    out.plan.gates.clear();
    if (calibration) out.plan.duration = std::max(out.plan.duration, calibration->endTime);
    for (auto const& p : out.placements) {
        out.plan.gates.push_back(p.gate);
        if (mod->getSettingValue<bool>("debug-mapping")) {
            auto row = matjson::Value::object(); row["frame"] = p.gate.frame; row["seconds"] = p.gate.seconds;
            row["x"] = p.x; row["y"] = p.y;
            row["roundtrip_seconds"] = editor->timeForPos({p.x-dx, p.y}, 0, 0, false, 0);
            row["p1"] = p.gate.p1; row["p2"] = p.gate.p2;
            row["mapping_source"] = calibration ? "recorded_inputs" : "editor_timeline";
            if (mod->getSettingValue<bool>("debug-objects")) row["object"] = p.object;
            debug.event("placement", row);
        }
    }
    auto report = matjson::Value::object(); report["triggers"] = out.placements.size();
    report["auto_objects"] = out.autoPath.objects.size();
    report["p1_events"] = out.plan.p1Events; report["p2_events"] = out.plan.p2Events;
    report["duplicates_removed"] = out.plan.duplicates; report["duration_seconds"] = out.plan.duration;
    report["warnings"] = matjson::Value::array(); for (auto const& w : out.plan.warnings) report["warnings"].push(w);
    debug.set("analysis", report);
    return out;
}
size_t apply(LevelEditorLayer* editor, Prepared const& prepared) {
    if (!editor || LevelEditorLayer::get() != editor || editor->m_playbackMode != PlaybackMode::Not)
        throw Error("EDITOR_CLOSED", "Editor unavailable or playtest is active");
    if (!sameLevelData(std::string(editor->getLevelString()), prepared.levelBefore))
        throw Error("EDITOR_CHANGED", "Level changed after analysis. Analyze again.");
    if (Mod::get()->getSettingValue<bool>("backup-level")) try {
        auto folder = Mod::get()->getSaveDir() / "backups";
        std::filesystem::create_directories(folder);
        auto path = folder / ("before-import-" + Diagnostics::stamp() + ".level.txt");
        std::ofstream backup(path, std::ios::binary); backup << prepared.levelBefore; backup.flush();
        if (!backup) throw Error("BACKUP_WRITE", "Backup could not be written");
        Diagnostics::get().set("backup", path.filename().string());
    }
    catch (std::exception const& e) {
        Diagnostics::get().set("backup_warning", e.what());
        log::warn("HoldForge backup warning: {}. Continuing with editor Undo.", e.what());
        FLAlertLayer::create("Backup warning", "Backup could not be saved. Creation continues with editor Undo available.", "OK")->show();
    }
    std::string text;
    for (auto const& p : prepared.placements) text += p.object;
    for (auto const& p : prepared.autoPath.objects) text += p.object;
    Diagnostics::get().set("stage", "creating_objects");
    Ref<CCArray> objects = editor->createObjectsFromString(text, true, true);
    auto rollback = [&] {
        if (objects) for (auto obj : CCArrayExt<GameObject*>(objects.data())) editor->removeObject(obj, true);
        editor->dirtifyTriggers();
    };
    if (!objects || objects->count() != prepared.placements.size() + prepared.autoPath.objects.size()) {
        rollback(); throw Error("EDITOR_CREATE", "Incomplete object creation; imported objects rolled back");
    }
    size_t i = 0;
    for (auto obj : CCArrayExt<GameObject*>(objects.data())) {
        bool ok = false;
        if (i < prepared.placements.size()) {
            auto options = typeinfo_cast<GameOptionsTrigger*>(obj);
            auto const& e = prepared.placements[i];
            ok = options && static_cast<int>(options->m_disableP1Controls) == e.gate.p1 &&
                static_cast<int>(options->m_disableP2Controls) == e.gate.p2 &&
                std::abs(obj->getPositionX()-e.x) <= .01f && !options->m_isSpawnTriggered && !options->m_isTouchTriggered;
        } else {
            auto const& e = prepared.autoPath.objects[i-prepared.placements.size()];
            ok = obj->m_objectID == e.id && std::abs(obj->getPositionX()-e.x) <= .01f &&
                std::abs(obj->getPositionY()-e.y) <= .01f && obj->m_isHide && obj->m_hasNoEffects && obj->m_hasNoParticles;
            if (e.group) ok = ok && obj->m_groupCount == 1 && obj->getGroupID(0) == e.group;
            if (e.kind == AutoKind::Portal) {
                auto portal = typeinfo_cast<TeleportPortalObject*>(obj);
                ok = ok && portal && portal->m_objectType == GameObjectType::TeleportPortal && !portal->m_isYellowPortal && !portal->m_orangePortal &&
                    portal->m_targetGroupID == e.target && portal->m_isTouchTriggered && !portal->m_isSpawnTriggered &&
                    !portal->m_isNoTouch && portal->m_ignoreX && !portal->m_ignoreY && !portal->m_saveOffset &&
                    portal->m_staticForceEnabled && portal->m_staticForce == 0 && !portal->m_staticForceAdditive &&
                    !portal->m_redirectForceEnabled && portal->m_gravityMode == 0;
            } else if (e.kind == AutoKind::Target) ok = ok && obj->m_isNoTouch;
            else {
                auto toggle = typeinfo_cast<EffectGameObject*>(obj);
                ok = ok && toggle && toggle->m_targetGroupID == e.target && toggle->m_activateGroup == e.enabled &&
                    !toggle->m_isSpawnTriggered && !toggle->m_isTouchTriggered;
            }
        }
        if (!ok) {
            auto j = matjson::Value::object(); j["index"] = i; j["id"] = obj->m_objectID;
            j["serialized"] = std::string(obj->getSaveString(editor));
            Diagnostics::get().set("object_readback_error", j);
            rollback(); throw Error("EDITOR_VERIFY", "GD did not deserialize a generated object as expected; batch rolled back. Export logs");
        }
        ++i;
    }
    auto undo = UndoObject::createWithArray(objects.data(), UndoCommand::Paste);
    if (!undo) { rollback(); throw Error("EDITOR_UNDO", "Could not allocate undo entry; batch rolled back"); }
    editor->addToUndoList(undo, false);
    editor->dirtifyTriggers();
    if (editor->m_editorUI) {
        editor->m_editorUI->selectObjects(objects.data(), true);
        editor->m_editorUI->updateButtons(); editor->m_editorUI->updateObjectInfoLabel();
    }
    Diagnostics::get().set("created", objects->count());
    Diagnostics::get().set("stage", "batch_complete");
    log::info("HoldForge created {} native objects", objects->count());
    return objects->count();
}
}
