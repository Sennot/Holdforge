#pragma once
#include <Geode/Geode.hpp>
#include "core/Calibration.hpp"
#include "core/Slc.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hf {
struct TracePlayerState {
    double x = 0, y = 0;
    bool disabled = false, buffered = false, holding = false, dead = false;
    float speed = 0;
};
struct TraceStepState {
    uint64_t serial = 0;
    int progress = 0;
    double levelTime = 0;
    bool dual = false;
    TracePlayerState p1, p2;
};

class TraceRecorder {
    struct Expected { uint64_t frame; bool down, p2; };
    bool m_armed = false, m_attempt = false, m_failed = false, m_twoPlayer = false, m_completed = false;
    uint64_t m_macroHash = 0, m_levelHash = 0, m_stepSerial = 0;
    size_t m_index = 0;
    std::optional<uint64_t> m_lastMatchedFrame, m_lastMatchedStep;
    std::optional<double> m_stepsPerMacroFrame;
    std::array<bool, 2> m_actualDown{false, false};
    std::filesystem::path m_path;
    std::vector<Expected> m_expected;
    Calibration m_result;
    std::optional<TraceStepState> m_currentPre;
    std::optional<TraceStepState> m_lastPre;
    std::optional<TraceStepState> m_lastPost;
    std::string m_error;

    static TraceStepState snapshot(GJBaseGameLayer* layer, uint64_t serial);
    void fail(std::string code, std::string message);
public:
    static TraceRecorder& get();
    void arm(LevelEditorLayer* editor, Replay const& replay, std::filesystem::path const& path);
    void cancel();
    bool armed() const { return m_armed; }
    bool active() const { return m_armed && m_attempt && !m_failed; }
    std::string status() const;

    void onPlaytestStart(LevelEditorLayer* editor);
    void onStepBegin(GJBaseGameLayer* layer);
    void onStepEnd(GJBaseGameLayer* layer);
    void onButton(GJBaseGameLayer* layer, bool down, int button, bool player1);
    void onDamage(GJBaseGameLayer* layer, PlayerObject* player);
    void onLevelComplete(GJBaseGameLayer* layer);
    void onPlaytestStop(LevelEditorLayer* editor);
};
}
