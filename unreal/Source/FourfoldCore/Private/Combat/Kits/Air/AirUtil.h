// Fourfold core - port of game/combat/kits/air/air_util.gd: shared helpers of the Air kit (aim, ground points, zones,
// body queries, arena reflection, shoves that respect immunities, the vacuum inrush flag). Wind is not matter.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"
#include "Sim/Agent.h"

#include <functional>
#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class MatBody;

namespace AirUtil {

inline constexpr int E = 3;
inline constexpr double LIGHT = 30.0;
inline constexpr double INRUSH_LIFE = 0.6;
inline constexpr double NO_PAIR = 99.0;

inline double f(double n) { return n / 60.0; }
Vec3 aim_flat(const ActorState& a, const ActionInst& inst);
Vec3 ground_at(CombatWorld& w, Vec3 p);
Vec3 aim_ground(CombatWorld& w, const ActorState& a, const ActionInst& inst, double rmax);
bool in_cone(Vec3 origin, Vec3 dir, Vec3 p, double rng_m, double half_deg, double pad = 0.0);
MatBody* zone(CombatWorld& w, const std::string& tag, Vec3 pos, double radius, int owner_id, double power, double life,
              const Dict& props = Dict(), int tier = 0, int sub = 1);
std::vector<MatBody*> zones_of(CombatWorld& w, int owner_id, const std::string& tag);
AgentRef zone_counter(CombatWorld& w, MatBody& z, bool perfect = false);
void mark_pair(CombatWorld& w, const MatBody& z, const MatBody& b);
bool pair_recent(CombatWorld& w, const MatBody& z, const MatBody& b, int ticks);
int source_owner(const MatBody& b);
bool carriable(const MatBody& b, double max_mass = LIGHT);
std::vector<MatBody*> bodies_near(CombatWorld& w, Vec3 p, double r, const std::function<bool(MatBody&)>& filter = nullptr);
bool hostile_shot(const MatBody& b, const ActorState* to);
std::vector<ActorState*> foes_near(CombatWorld& w, const ActorState& a, Vec3 p, double r);
bool shove(ActorState& t, Vec3 v, const std::string& what = "knockback");
bool airborne(const ActorState& t);
void guard_zone_effect(CombatWorld& w, MatBody& z, const std::function<bool(MatBody&)>& skip = nullptr);
// {t, n, solid_index} or {} (empty Dict when nothing is hit).
Dict arena_hit(CombatWorld& w, Vec3 p0, Vec3 p1);
Dict _slab_n(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx);
Vec3 wall_normal(const MatBody& wall, Vec3 p);
Vec3 reflect_dir(Vec3 d, Vec3 n);
bool condense(CombatWorld& w, MatBody& b);
MatBody* spawn_inrush(CombatWorld& w, Vec3 pos, double radius, double power, int owner_id);
int64_t inrush_tick_at(CombatWorld& w, Vec3 p);

}  // namespace AirUtil
}  // namespace ff
