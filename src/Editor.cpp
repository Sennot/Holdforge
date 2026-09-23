#include "Editor.hpp"
#include "core/Fingerprint.hpp"
#include "Diagnostics.hpp"
#include <fstream>
#include <cmath>
#include <limits>
#include <unordered_map>
using namespace geode::prelude;
namespace hf {
Prepared prepare(LevelEditorLayer* editor, Replay const& replay, Calibration const* calibration) {
    if (!editor || LevelEditorLayer::get() != editor) throw Error("EDITOR_CLOSED", "Editor session changed");
    if (editor->m_playbackMode != PlaybackMode::Not || editor->m_playbackActive)
        throw Error("EDITOR_PLAYING", "Stop playtest and music playback first");
    auto settings = editor->m_levelSettings;
    if (!settings || settings->m_platformerMode) throw Error("LEVEL_PLATFORMER", "This converter supports classic levels only");
    auto mod = Mod::get(); auto& debug = Diagnostics::get();
    Prepared out;
    out.levelBefore = std::string(editor->getLevelString());
    auto info = matjson::Value::object();
    info["two_player"] = settings->m_twoPlayerMode; info["start_dual"] = settings->m_startDual;
    info["start_speed"] = static_cast<int>(settings->m_startSpeed);
    info["objects"] = editor->m_objects->count();
    uint64_t hash = fingerprint(out.levelBefore);
    info["fingerprint_fnv1a64"] = fmt::format("{:016x}", hash);
    info["serialized_bytes"] = out.levelBefore.size(); debug.set("level", info);
    // Discontinuous X cannot yet be represented safely by the recorded crossing interval.
    std::unordered_map<int, std::string> hardRisks{
        {1917, "Reverse"}, {3022, "Teleport trigger"}, {747, "Teleport portal"},
        {749, "Teleport exit"}, {2900, "Gameplay rotation"}
    };
    // These are allowed with an automatic trace because placement follows measured movement,
    // but remain unsafe for the legacy editor-time fallback.
    std::unordered_map<int, std::string> timelineRisks{
        {1935, "TimeWarp"}, {1704, "Dash orb"}, {1751, "Gravity dash orb"},
        {2901, "Gameplay offset"}, {1932, "Player Control"}
    };
    bool unsafe = mod->getSettingValue<bool>("unsafe-timeline");
    bool staticFallback = mod->getSettingValue<bool>("allow-static-fallback");
    std::vector<std::string> warnings;
    if (settings->m_rotateGameplay) throw Error("LEVEL_ROTATE", "Rotated gameplay is not yet supported by automatic trace placement");
    for (auto obj : CCArrayExt<GameObject*>(editor->m_objects)) {
        if (auto options = typeinfo_cast<GameOptionsTrigger*>(obj)) {
            if ((options->m_disableP1Controls != GameOptionsSetting::Disabled ||
                 options->m_disableP2Controls != GameOptionsSetting::Disabled) &&
                 !mod->getSettingValue<bool>("allow-existing-controls"))
                throw Error("LEVEL_CONTROL_CONFLICT", "Existing control Options Triggers found. Undo the previous import or resolve the conflict first.");
        }
        if (auto it = hardRisks.find(obj->m_objectID); it != hardRisks.end())
            throw Error("LEVEL_X_DISCONTINUITY", it->second + " is not yet supported by automatic trace placement");
        if (auto it = timelineRisks.find(obj->m_objectID); it != timelineRisks.end()) {
            if (!calibration && !unsafe)
                throw Error("LEVEL_TIMELINE_RISK", it->second + " needs an automatic trace (or the unsafe experimental fallback)");
            if (!calibration) {
                auto warning = it->second + " present: experimental editor timeline requires manual validation.";
                if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) warnings.push_back(warning);
            }
        }
    }
    PlanConfig cfg;
    cfg.twoPlayer = settings->m_twoPlayerMode; cfg.strict240 = mod->getSettingValue<bool>("strict-240");
    if (cfg.twoPlayer && GameManager::get() && GameManager::get()->getGameVariable("0010"))
        throw Error("TRACE_2P_FLIP", "Disable Flip 2-Player Controls for independent P1/P2 conversion");
    cfg.offsetMs = mod->getSettingValue<double>("offset-ms");
    cfg.maxTriggers = static_cast<size_t>(mod->getSettingValue<int64_t>("max-triggers"));
    auto effective = matjson::Value::object();
    effective["two_player"] = cfg.twoPlayer; effective["strict_240"] = cfg.strict240;
    effective["offset_ms"] = cfg.offsetMs; effective["unsafe_timeline"] = unsafe;
    effective["x_offset"] = mod->getSettingValue<double>("x-offset");
    effective["allow_static_fallback"] = staticFallback;
    out.plan = plan(replay, cfg);
    {
        auto gates = matjson::Value::array();
        for (auto const& gate : out.plan.gates) {
            auto row = matjson::Value::object(); row["frame"] = gate.frame; row["seconds"] = gate.seconds;
            row["p1"] = gate.p1; row["p2"] = gate.p2; gates.push(std::move(row));
        }
        debug.set("macro_gates", std::move(gates));
    }
    if (calibration) {
        if (calibration->levelHash != hash)
            throw Error("TRACE_LEVEL", "Recorded trace belongs to a different/modified level. Undo generated controls and record again.");
        if (calibration->twoPlayer != cfg.twoPlayer)
            throw Error("TRACE_MODE", "Recorded trace was made with a different 2 Player Mode setting");
        if (cfg.offsetMs != 0 || mod->getSettingValue<double>("x-offset") != 0)
            throw Error("TRACE_OFFSET", "Automatic trace requires Timing offset and Position offset = 0");
        out.plan.warnings.push_back("Automatic trace loaded. Final acceptance still requires a stock-GD hold test with HoldForge disabled.");
    } else if (!staticFallback) {
        throw Error("TRACE_REQUIRED", "No valid automatic .hftrace for this macro/level. Use Record trace, replay the full macro, then import again.");
    } else {
        out.plan.warnings.push_back("EXPERIMENTAL: editor timeline fallback is not publication-safe; verify in stock GD.");
    }
    effective["mapping_source"] = calibration ? "automatic_trace" : "editor_timeline_experimental";
    debug.set("effective_conversion", effective);
    out.plan.warnings.insert(out.plan.warnings.end(), warnings.begin(), warnings.end());
    float y = static_cast<float>(mod->getSettingValue<double>("trigger-y"));
    float dx = static_cast<float>(mod->getSettingValue<double>("x-offset"));
    int layer = static_cast<int>(mod->getSettingValue<int64_t>("editor-layer"));
    float previous = -std::numeric_limits<float>::infinity();
    // Use GD's portal-aware time map. Never estimate x with frame * a fixed speed.
    editor->dirtifyTriggers();
    for (auto const& gate : out.plan.gates) {
        auto point = calibration ? CCPoint{gate.frame ? static_cast<float>(calibration->position(gate.frame, gate.p1, gate.p2)) : 0.f, y}
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
            row["mapping_source"] = calibration ? "automatic_trace" : "editor_timeline_experimental";
            if (mod->getSettingValue<bool>("debug-objects")) row["object"] = p.object;
            debug.event("placement", row);
        }
    }
    auto report = matjson::Value::object(); report["triggers"] = out.placements.size();
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
    Diagnostics::get().set("stage", "creating_objects");
    Ref<CCArray> objects = editor->createObjectsFromString(text, true, true);
    auto rollback = [&] {
        if (objects) for (auto obj : CCArrayExt<GameObject*>(objects.data())) editor->removeObject(obj, true);
        editor->dirtifyTriggers();
    };
    if (!objects || objects->count() != prepared.placements.size()) {
        rollback(); throw Error("EDITOR_CREATE", "Incomplete object creation; imported objects rolled back");
    }
    size_t i = 0;
    for (auto obj : CCArrayExt<GameObject*>(objects.data())) {
        auto options = typeinfo_cast<GameOptionsTrigger*>(obj);
        auto const& expected = prepared.placements.at(i++);
        if (!options || static_cast<int>(options->m_disableP1Controls) != expected.gate.p1 ||
            static_cast<int>(options->m_disableP2Controls) != expected.gate.p2 ||
            std::abs(obj->getPositionX() - expected.x) > 0.01f || options->m_isSpawnTriggered || options->m_isTouchTriggered) {
            rollback(); throw Error("EDITOR_VERIFY", "GD did not deserialize Options Triggers as expected; batch rolled back");
        }
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
    log::info("HoldForge created {} Options Triggers", objects->count());
    return objects->count();
}
}
