#pragma once
#include <Geode/Geode.hpp>
#include <unordered_map>
#include "Editor.hpp"
#include "core/Trajectory.hpp"
#include "core/LevelIdentity.hpp"
namespace hf {
Sample sample(GJBaseGameLayer* layer);
matjson::Value sampleJson(Sample const& sample);
class Workflow {
public:
    enum class Mode { None, Record, Verify };
    using EditorState = EditorSession;
private:
    std::optional<Replay> m_replay;
    std::optional<Trajectory> m_trajectory;
    std::unique_ptr<Recorder> m_recorder;
    geode::Ref<GJGameLevel> m_target;
    PlayLayer* m_layer = nullptr; // identity only; not used after onQuit/onExit
    std::string m_source, m_holdSource, m_name, m_status = "Import a macro";
    std::string m_savedLevelData; // frozen saved payload; m_target itself is mutable
    std::vector<Placement> m_gates;
    Mode m_mode = Mode::None;
    bool m_twoPlayer = false, m_armed = false;
    size_t m_checkIndex = 0, m_gateIndex = 0, m_stepIndex = 0;
    bool m_auto = false, m_manualDual = false;
    double m_maxDualError = 0;
    std::vector<AutoAnchor> m_anchors;
    std::unordered_map<int, size_t> m_autoTargets;
    std::vector<bool> m_autoSeen;
    size_t m_autoActivated = 0;
    std::array<bool, 2> m_verifyHeld{false, false};
    std::array<unsigned, 2> m_presses{0, 0};
    std::filesystem::path cachePath() const;
    bool target(PlayLayer* layer) const;
    bool sameSessionLevel(GJGameLevel* level) const;
    void fail(std::string const& code, std::string const& message);
public:
    static Workflow& get();
    EditorState editorState(LevelEditorLayer* editor) const;
    Replay const* replay() const { return m_replay ? &*m_replay : nullptr; }
    Trajectory const* trajectory() const { return m_trajectory ? &*m_trajectory : nullptr; }
    std::string const& name() const { return m_name; }
    std::string const& status() const { return m_status; }
    Mode mode() const { return m_mode; }
    bool active(GJBaseGameLayer* layer) const { return m_mode != Mode::None && layer == m_layer; }
    void select(LevelEditorLayer* editor, Replay replay, std::string name);
    void armRecord(LevelEditorLayer* editor);
    void generated(LevelEditorLayer* editor, Prepared const& prepared);
    void armVerify(LevelEditorLayer* editor);
    void attempt(PlayLayer* layer, bool forceReset);
    void resetBegin(PlayLayer* layer);
    void stepBegin(GJBaseGameLayer* layer);
    void stepEnd(GJBaseGameLayer* layer);
    void button(GJBaseGameLayer* layer, bool down, int button, bool player1, bool queued);
    void options(GJBaseGameLayer* layer, GameOptionsTrigger* object);
    void teleported(GJBaseGameLayer* layer, TeleportPortalObject* portal, PlayerObject* player, Sample const& before);
    void damaged(PlayLayer* layer, PlayerObject* player, GameObject* object);
    void complete(PlayLayer* layer);
    void leave(PlayLayer* layer);
};
}
