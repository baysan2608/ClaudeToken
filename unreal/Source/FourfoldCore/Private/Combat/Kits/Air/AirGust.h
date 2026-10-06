// Fourfold core - port of game/combat/kits/air/air_gust.gd (Air / Gust, sub 0: the legacy kit + T2 Gale / T3
// Hurricane Palm, Wind Grip, Wind Crescent, Dust Devil Line, Crosswind, Wall of Wind, Downdraft, Tailwind). The move
// defs are data (Data/moves.json); this file holds the code they reference.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"

namespace ff {

class CombatWorld;
class MatBody;

namespace AirGust {

inline constexpr int E = 3;
inline constexpr int SUB = 0;
inline constexpr double GRIP_REACH = 9.0;
inline constexpr double GRIP_CONE = 40.0;
inline constexpr double FIRE_FEED = 1.2;

bool crescent_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool crescent_tick(CombatWorld& w, MatBody& b, double dt);
bool crosswind_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool downdraft_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
MatBody* grip_target(CombatWorld& w, const ActorState& a, Vec3 dir);
void grip_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir);

}  // namespace AirGust
}  // namespace ff
