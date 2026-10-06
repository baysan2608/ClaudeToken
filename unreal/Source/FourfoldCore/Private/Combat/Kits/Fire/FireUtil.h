// Fourfold core - port of game/combat/kits/fire/fire_util.gd: shared helpers of the Fire kit. Every heat change is
// booked (pay_heat -> ledger, FIRE body heat_payload, a volume's paid budget -> heat_body / boil_water).
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"
#include "Sim/Agent.h"

#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class MatBody;

namespace FireUtil {

inline constexpr int E = 2;
inline constexpr double FIELD_STATUS_T = 0.6;
inline constexpr double DAMPEN = 0.5;
inline constexpr double FAN = 1.3;
inline constexpr double INRUSH = 1.5;
inline constexpr int INRUSH_TICKS = 30;
bool is_vapor_tag(std::string_view tag);   // VAPOR_TAGS
bool is_gust_tag(std::string_view tag);    // GUST_TAGS

inline double s(double frames) { return frames / 60.0; }
void with_params(ActionInst& inst, const Dict& over);
Vec3 aim_ground(CombatWorld& w, const ActorState& a, const ActionInst& inst, double rmax);
double _src_avail(const Agent& src);
double transfer(CombatWorld& w, Agent* src, MatBody* dst, double hu, bool boil = true);
double melt_need(const MatBody& b, double to_liquid = 1.0);
double pay_into(CombatWorld& w, ActorState& a, MatBody* dst, double hu, bool allow_partial = true);
MatBody* spawn_field(CombatWorld& w, int owner_id, Vec3 pos, double radius, double life, double heat_hu, bool blue = false, int tier = 0,
                     const std::string& tag = "fire_field");
double field_power(const MatBody& b);
void field_tick(CombatWorld& w, MatBody& z, double dt);
std::vector<MatBody*> zones_at(CombatWorld& w, Vec3 p, const std::vector<std::string>& classes, double r = 0.2);
bool in_vapor(CombatWorld& w, Vec3 p);
bool in_wind(CombatWorld& w, Vec3 p);
ActionRef blast_inst(CombatWorld& w, const std::string& move_id, int tier, int attack_id = 0, int sub = 3);
// Returns {suppressed, power, radius, mods: [..], tornado}.
Dict detonate(CombatWorld& w, ActorState* a, ActionInst* inst, Vec3 p, const Dict& prm, const std::string& mode = "strike",
              int64_t inrush_tick = -1000000);

}  // namespace FireUtil
}  // namespace ff
