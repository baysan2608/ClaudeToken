// Fourfold core - sim / app events (one-shot cues for VFX, audio, camera, haptics, HUD, Lab).
// FROZEN CONTRACT (architect). Owner after creation: stream `core` (additive only).
//
// `type` and the `data` fields are EXACTLY the Godot event names / dictionary keys (159 sim event types; catalogue:
// game/core/fx_events.gd, docs/MOVESET.md §15.6, docs/COMBAT_SPEC.md §E8), e.g.
//   fx          {fx, mat, shape, actor, body, element, sub, move, tier, pos, dir, radius, length, angle, height, path, dur, power, seed, on}
//   interaction {threat, counter, outcome, band, ratio, tp, cp, perfect, pos, dir, threat_actor, counter_actor, threat_body, counter_body, to, rule, tier}
//   hit / block / impact {..., power, mat, tier, dir}   charge {actor, move, element, sub, tier, ready, slot}
//   status {actor, status, on, t, mag}   zone {body, kind, phase, radius, owner, pos, tier}   action {actor, id, ..., sub, slot, tier}
// Vectors are ff::Vec3 values (sim space). Session-level events use the "app_" prefix (see Session.h).
#pragma once

#include "ff/Config.h"
#include "ff/Value.h"

#include <cstdint>
#include <string>

namespace ff {

struct Event {
	std::string type;     // "fx", "hit", "interaction", ... or "app_*"
	int64_t tick = 0;     // sim tick that emitted it
	Value data;           // Dict with the Godot fields
};

}  // namespace ff
