// Fourfold core - port of game/combat/kits/earth/kit_earth.gd: the Earth kit dispatcher (module "kit_earth": by the
// def key `part` -> EarthStone / EarthMetal / EarthSand / EarthMagma, else the generic verbs) and shared helpers.
#pragma once

#include "Combat/Kits/KitDispatch.h"
#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"

#include <string>
#include <unordered_map>

namespace ff {

class CombatWorld;
class MatBody;

namespace KitEarthUtil {
inline constexpr double FPS = 60.0;
inline double f(double n) { return n / FPS; }
void _return_kept(CombatWorld& w, ActorState& a, ActionInst& inst);
void _aura(CombatWorld& w, ActorState& a, ActionInst& inst, bool on);
inline Vec3 flatv(Vec3 v) { return Vec3(v.x, 0.0f, v.z); }
float flat_dist2(Vec3 p, Vec3 q);
void fizzle(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& what);
int guard_tier(const ActionInst& inst);
MatBody* pour_wave(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b, Vec3 dir, double mult, const std::string& why);
Vec3 ground_point(CombatWorld& w, const ActorState& a, Vec3 dir, double dist);
}  // namespace KitEarthUtil

// Part modules ("stone" "metal" "sand" "magma") register their lifecycle here (a missing part -> Verbs).
using EarthPartMap = std::unordered_map<std::string, KitStages>;
void RegisterEarthParts(EarthPartMap& m);
// Optional hook from EarthMetal: a kept metal plate goes back into the satchel (nullptr until ported).
using ToSatchelFn = void (*)(CombatWorld&, ActorState&, MatBody&, const std::string&);
ToSatchelFn EarthToSatchel();

}  // namespace ff
