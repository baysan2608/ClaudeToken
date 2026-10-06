// Fourfold core - port of game/combat/kits/water/water_util.gd: shared helpers of the Water kit. Every water mass /
// heat move goes through the CombatWorld ledgers.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"

#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class MatBody;

namespace WaterUtil {

inline constexpr double POOL_REACH = 2.5;

inline double sec(double frames) { return frames / 60.0; }
double pool_dist(CombatWorld& w, Vec3 p);
MatBody* liquid_puddle_near(CombatWorld& w, Vec3 p, double reach);
bool near_water(CombatWorld& w, Vec3 p, double reach = POOL_REACH);
double available(CombatWorld& w, const ActorState& a, double reach = POOL_REACH);
double _src_energy(const MatBody& b, double kg);
double take(CombatWorld& w, ActorState& a, double kg, double reach = POOL_REACH);
void give_back(CombatWorld& w, ActorState& a, double kg, Vec3 p);
MatBody* make_puddle(CombatWorld& w, double kg, Vec3 p);
void disperse(CombatWorld& w, double kg);
bool freeze_body(CombatWorld& w, MatBody* b);
bool is_ice(const MatBody& b);
bool is_liquid_water(const MatBody& b);
double transfer_heat(CombatWorld& w, MatBody* src, MatBody* dst, double hu);
double make_steam(CombatWorld& w, ActorState& a, ActionInst* inst, double kg, Vec3 p);
void wet(ActorState& a, double v = 1.0);
void evade_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it, const Dict& spec);
void evade_tick(CombatWorld& w, ActorState& a, ActionInst& inst);
void evade_end(ActionInst& inst);
MatBody* zone(CombatWorld& w, const std::string& tag, Vec3 p, double radius, int owner_id, double life, const Dict& props = Dict(),
              Mat mat = Mat::Air, double mass = 0.0, double power = 0.0);
Vec3 ground_at(CombatWorld& w, Vec3 p);
bool hit_result_ok(const ActorState& t);
double take_moisture(CombatWorld& w, double kg);
void give_back_moisture(CombatWorld& w, double kg);
bool freeze_any(CombatWorld& w, MatBody* b);
double absorb_into_skin(CombatWorld& w, ActorState* a, MatBody* b);
void rain_down(CombatWorld& w, MatBody& b, Vec3 p);
std::vector<ActorState*> foes_near(CombatWorld& w, const ActorState* owner, Vec3 p, double r);
std::vector<MatBody*> zones_of(CombatWorld& w, const ActorState& a, const std::string& tag);
Vec3 aim_flat(CombatWorld& w, const ActorState& a, const ActionInst& inst);

}  // namespace WaterUtil
}  // namespace ff
