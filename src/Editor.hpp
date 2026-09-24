#pragma once
#include <Geode/Geode.hpp>
#include "core/Plan.hpp"
#include "core/Trajectory.hpp"
#include "core/AutoPath.hpp"
namespace hf {
struct Placement { Gate gate; float x = 0, y = 0; std::string object; };
struct Prepared {
    Plan plan;
    AutoPath autoPath;
    std::vector<Placement> placements;
    std::string levelBefore;
    bool manualDual = false;
};
Prepared prepare(LevelEditorLayer* editor, Replay const& replay, Trajectory const* trajectory = nullptr, bool requireRecording = true);
size_t apply(LevelEditorLayer* editor, Prepared const& prepared);
}
