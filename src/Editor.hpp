#pragma once
#include <Geode/Geode.hpp>
#include "core/Plan.hpp"
#include "core/Trajectory.hpp"
namespace hf {
struct Placement { Gate gate; float x = 0, y = 0; std::string object; };
struct Prepared {
    Plan plan;
    std::vector<Placement> placements;
    std::string levelBefore;
};
Prepared prepare(LevelEditorLayer* editor, Replay const& replay, Trajectory const* trajectory = nullptr, bool requireRecording = true);
size_t apply(LevelEditorLayer* editor, Prepared const& prepared);
}
