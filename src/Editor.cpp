#include "Editor.hpp"
#include "Diagnostics.hpp"
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
    if (!settings || settings->m_platformerMode) throw Error("LEVEL_PLATFORMER", "This converter supports classic levels only");
    auto mod = Mod::get(); auto& debug = Diagnostics::get();
    if (requireRecording && !calibration && mod->getSettingValue<bool>("require-recording"))
        throw Error("RECORD_FIRST", "Record a complete replay with the Record button before generating");
    Prepared out;
    out.levelBefore = std::string(editor->getLevelString());
    auto info = matjson::Value::object();
    info["two_player"] = settings->m_twoPlayerMode; info["start_dual"] = settings->m_startDual;
    info["start_speed"] = static_cast<int>(settings->m_startSpeed);
    info["objects"] = editor->m_objects->count();
    uint64_t hash = 14695981039346656037ULL;
    for (auto c : out.levelBefore) { hash ^= static_cast<uint8_t>(c); hash *= 1099511628211ULL; }
    info["fingerprint_fnv1a64"] = fmt::format("{:016x}", hash);
    info["serialized_bytes"] = out.levelBefore.size(); debug.set("level", info);
    // Conservative gate: these mechanics can make an editor timeline differ from actual movement.
    std::unordered_map<int, std::string> risks{
        {1917, "Reverse"}, {1935, "TimeWarp"}, {3022, "Teleport trigger"},
        {747, "Teleport portal"}, {749, "Teleport exit"}, {2064, "Unlinked teleport portal"},
        {2065, "Teleport exit"}, {3027, "Teleport orb"}, {1704, "Dash orb"},
        {1751, "Gravity dash orb"}, {2900, "Gameplay rotation"},
        {2901, "Gameplay offset"}, {1932, "Player Control"}
    };
    bool unsafe = mod->getSettingValue<bool>("unsafe-timeline");
    std::vector<std::string> warnings;
    bool containsDual = settings->m_startDual;
    if (settings->m_rotateGameplay && !unsafe) throw Error("LEVEL_ROTATE", "Rotated gameplay needs a recorded trajectory; timeline conversion is disabled");
    for (auto obj : CCArrayExt<GameObject*>(editor->m_objects)) {
        containsDual |= obj->m_objectID == 286;
        if (auto options = typeinfo_cast<GameOptionsTrigger*>(obj)) {
            if ((options->m_disableP1Controls != GameOptionsSetting::Disabled ||
                 options->m_disableP2Controls != GameOptionsSetting::Disabled) &&
                 !mod->getSettingValue<bool>("allow-existing-controls"))
                throw Error("LEVEL_CONTROL_CONFLICT", "Existing control Options Triggers found. Undo the previous import or resolve the conflict first.");
        }
        auto it = risks.find(obj->m_objectID);
        if (it != risks.end()) {
            if (!unsafe) throw Error("LEVEL_TIMELINE_RISK", it->second + " found. Static timeline may be inaccurate; see Experimental timeline setting.");
            auto warning = it->second + " present: generated timing requires manual validation.";
            if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) warnings.push_back(warning);
        }
    }
    if (requireRecording && !calibration && containsDual && !settings->m_twoPlayerMode && mod->getSettingValue<bool>("dual-auto"))
        throw Error("AUTO_RECORD", "Invisible dual needs a NEW full Record, even when Require recorded trajectory is disabled");
    PlanConfig cfg;
    cfg.twoPlayer = settings->m_twoPlayerMode; cfg.strict240 = mod->getSettingValue<bool>("strict-240");
    cfg.offsetMs = mod->getSettingValue<double>("offset-ms");
    cfg.maxTriggers = static_cast<size_t>(mod->getSettingValue<int64_t>("max-triggers"));
    cfg.sharedP1Only = mod->getSettingValue<bool>("shared-p1-only");
    auto effective = matjson::Value::object();
    effective["two_player"] = cfg.twoPlayer; effective["strict_240"] = cfg.strict240;
    effective["offset_ms"] = cfg.offsetMs; effective["unsafe_timeline"] = unsafe;
    effective["x_offset"] = mod->getSettingValue<double>("x-offset");
    effective["native_only"] = true;
    effective["shared_p1_only"] = cfg.sharedP1Only;
    out.plan = plan(replay, cfg);
    bool autoDual = calibration && calibration->sawDual && !cfg.twoPlayer && mod->getSettingValue<bool>("dual-auto");
    effective["invisible_dual_auto"] = autoDual;
    if (autoDual) for (auto& g : out.plan.gates) g.p2 = 1;
    if (calibration) {
        if (!calibration->completed || calibration->twoPlayer != cfg.twoPlayer || calibration->macroHash != replay.fingerprint || calibration->levelHash != hash)
            throw Error("TRACE_LEVEL", "Recorded positions need the unmodified original level. Remove the old hold triggers first. The level must match the recorded run.");
        if (cfg.offsetMs != 0 || mod->getSettingValue<double>("x-offset") != 0)
            throw Error("TRACE_OFFSET", "Set Timing offset and Position offset to zero for recorded positions");
        // Only the last generic editor-map warning is replaced. Preserve
        // meaningful planner warnings such as a missing independent P2 stream.
        if (!out.plan.warnings.empty()) out.plan.warnings.pop_back();
        out.plan.warnings.push_back("Recorded physics-step positions loaded. Use Verify, then test with HoldForge disabled before publishing.");
        if (calibration->sawDual && !cfg.twoPlayer && !autoDual)
            out.plan.warnings.push_back("Ordinary dual uses native control gates. P2 hold reactivation is not confirmed; Verify must check it. No runtime fixes are applied.");
    }
    effective["mapping_source"] = calibration ? "recorded_inputs" : "editor_timeline";
    debug.set("effective_conversion", effective);
    out.plan.warnings.insert(out.plan.warnings.end(), warnings.begin(), warnings.end());
    float y = static_cast<float>(mod->getSettingValue<double>("trigger-y"));
    float dx = static_cast<float>(mod->getSettingValue<double>("x-offset"));
    int layer = static_cast<int>(mod->getSettingValue<int64_t>("editor-layer"));
    float previous = -std::numeric_limits<float>::infinity();
    // Use GD's portal-aware time map. Never estimate x with frame * a fixed speed.
    editor->dirtifyTriggers();
    for (auto const& gate : out.plan.gates) {
        auto point = calibration ? CCPoint{gate.frame ? static_cast<float>(calibration->position(gate.frame)) : 0.f, y}
                                 : editor->posForTime(static_cast<float>(gate.seconds));
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x <= previous)
            throw Error("MAP_NON_MONOTONIC", "Timeline has overlapping/reversed X positions; use a trajectory-based conversion");
        float back = editor->timeForPos(point, 0, 0, false, 0);
        if (!calibration && (!std::isfinite(back) || (!unsafe && std::abs(back - gate.seconds) > 1.0 / 240.0)))
            throw Error("MAP_ROUNDTRIP", "Editor time/position roundtrip differs by more than one physics tick");
        previous = point.x;
        Placement p{gate, point.x + dx, y, {}};
        // max_digits10 preserves float coordinates through serialization.
        p.object = fmt::format("1,2899,2,{:.9g},3,{:.9g},20,{},165,{},199,{};", p.x, p.y, layer, gate.p1, gate.p2);
        out.placements.push_back(p);
        if (mod->getSettingValue<bool>("debug-mapping")) {
            auto row = matjson::Value::object(); row["frame"] = gate.frame; row["seconds"] = gate.seconds;
            row["x"] = p.x; row["y"] = p.y; row["roundtrip_seconds"] = back;
            row["p1"] = gate.p1; row["p2"] = gate.p2;
            row["mapping_source"] = calibration ? "recorded_inputs" : "editor_timeline";
            if (mod->getSettingValue<bool>("debug-objects")) row["object"] = p.object;
            debug.event("placement", row);
        }
    }
    if (autoDual) {
        // Detached native prototype: validate object ID/type and actual hitbox
        // before changing the editor. Never assume a Teleport trigger targets P2.
        auto prototype = typeinfo_cast<TeleportPortalObject*>(GameObject::createWithKey(2064));
        if (!prototype || prototype->m_objectType != GameObjectType::TeleportPortal || prototype->m_orangePortal)
            throw Error("AUTO_PORTAL_TYPE", "GD does not recognize the expected unlinked touch portal (2064); export logs");
        prototype->setRScale(.5f);
        auto rect = prototype->getObjectRect();
        AutoPathConfig ac;
        ac.layer = layer; ac.maxObjects = cfg.maxTriggers - out.placements.size();
        ac.halfWidth = rect.size.width / 2; ac.halfHeight = rect.size.height / 2;
        if (ac.halfWidth <= 0 || ac.halfHeight <= 0 || ac.halfWidth > 100 || ac.halfHeight > 100)
            throw Error("AUTO_PORTAL_BOUNDS", "Unexpected native portal hitbox; export logs");
        out.autoPath = makeAutoPath(*calibration, out.levelBefore, ac);
        // A dual portal may reset the new icon's controls. Re-block it when
        // each segment starts, even when there is no macro edge at entry.
        double lastTime = -1;
        for (auto const& a : out.autoPath.anchors) {
            if (lastTime < 0 || a.time-lastTime > 1.5/240.0) {
                auto found = std::find_if(out.placements.begin(), out.placements.end(), [&](Placement const& p) { return std::abs(p.x-a.onX) < .01; });
                if (found == out.placements.end()) {
                    Gate gate{static_cast<uint64_t>(std::llround(std::max(0.0, a.time-calibration->clockOffset)*240)), a.time, 0, 1};
                    Placement p{gate, static_cast<float>(a.onX), y, {}};
                    p.object = fmt::format("1,2899,2,{:.9g},3,{:.9g},20,{},165,0,199,1;", p.x, p.y, layer);
                    out.placements.push_back(p);
                }
            }
            lastTime = a.time;
            if (mod->getSettingValue<bool>("debug-mapping")) {
                auto j = matjson::Value::object(); j["step"] = a.step; j["time"] = a.time; j["mode"] = a.mode;
                j["x"] = a.x; j["y"] = a.y; j["target_y"] = a.targetY; j["on_x"] = a.onX; j["off_x"] = a.offX;
                j["portal_group"] = a.portalGroup; j["target_group"] = a.targetGroup;
                debug.event("auto_anchor", j);
            }
        }
        std::sort(out.placements.begin(), out.placements.end(), [](Placement const& a, Placement const& b) { return a.x < b.x; });
        if (out.placements.size() + out.autoPath.objects.size() > cfg.maxTriggers)
            throw Error("AUTO_OBJECT_LIMIT", "Options plus invisible helpers exceed Maximum generated objects");
        auto j = matjson::Value::object(); j["anchors"] = out.autoPath.anchors.size(); j["objects"] = out.autoPath.objects.size();
        j["segments"] = out.autoPath.segments; j["portal_half_width"] = ac.halfWidth; j["portal_half_height"] = ac.halfHeight;
        j["modes"] = matjson::Value::array(); for (auto n : out.autoPath.modes) j["modes"].push(n);
        debug.set("invisible_dual_auto", j);
        out.plan.warnings.push_back("Experimental invisible dual path: native touch portals, 240 corrections/sec. Preserves forms; does not reproduce P2 input/rotation/gravity. Verify and test without mods.");
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
    if (std::string(editor->getLevelString()) != prepared.levelBefore)
        throw Error("EDITOR_CHANGED", "Level changed after analysis. Analyze again.");
    if (Mod::get()->getSettingValue<bool>("backup-level")) {
        auto folder = Mod::get()->getSaveDir() / "backups";
        std::filesystem::create_directories(folder);
        auto path = folder / ("before-import-" + Diagnostics::stamp() + ".level.txt");
        std::ofstream backup(path, std::ios::binary); backup << prepared.levelBefore; backup.flush();
        if (!backup) throw Error("BACKUP_WRITE", "Backup failed; import cancelled");
        Diagnostics::get().set("backup", path.filename().string());
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
                ok = ok && portal && portal->m_objectType == GameObjectType::TeleportPortal && !portal->m_orangePortal &&
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
