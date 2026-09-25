#pragma once

namespace hf::native {
// Alphalaneous/Tinker resources/objects.csv, checked 2026-09-24.
// 2064 is the ORANGE EXIT. It must never be used as the touch entrance.
inline constexpr int unlinkedPortal = 2902;
inline constexpr int unlinkedExit = 2064;
inline constexpr int options = 2899;
inline constexpr int toggle = 1049;
// A factory-created 2902 has reported m_objectType=0 in GD 2.2081.
// Gameplay-category enums do not establish the C++ class or entrance role.
inline bool isUnlinkedEntrance(int id, bool teleportClass, bool isExit, bool linked) {
    return id == unlinkedPortal && teleportClass && !isExit && !linked;
}
}
