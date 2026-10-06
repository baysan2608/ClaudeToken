// FourfoldFX logic island - analytic ground queries on the sim arena (port of ArenaMap.ground_height / base_floor).
// Owner: stream `fx`.
#pragma once

#include "FxBase.h"

namespace ff {
struct ArenaView;
}

namespace ffx {

inline constexpr float kStepHeight = 0.4f;   // Sim.STEP_HEIGHT

// Highest walkable surface under (x, z) reachable from height fromY (surfaces above fromY + step are walls).
// No arena: 0.
float GroundHeight(const ff::ArenaView* arena, float x, float z, float fromY = 1e9f);
// Ground under p (looked up from just above it, like FxCues._ground).
inline float GroundUnder(const ff::ArenaView* arena, const Vec3& p) { return GroundHeight(arena, p.x, p.z, p.y + 0.3f); }

}  // namespace ffx
