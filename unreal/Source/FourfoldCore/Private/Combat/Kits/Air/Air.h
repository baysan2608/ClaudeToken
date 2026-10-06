// Fourfold core - the Air kit beyond Gust (docs/kits/air.md, MOVESET §7.13-§7.16, §8.4): ports of
// game/combat/kits/air/air_outcomes.gd, air_vortex.gd (sub 1), air_vacuum.gd (2) and air_sound.gd (3). Move defs and
// the counter cells are data (Data/moves.json, Data/rules.json); these files hold the code they reference by name.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"
#include "Sim/Agent.h"
#include "Sim/Interactions.h"

#include <string>

namespace ff {

class CombatWorld;
class MatBody;

#define FF_AIR_START(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it)
#define FF_AIR_AFTER(N) ActionPhase N(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it)
#define FF_AIR_PHASE(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p)
#define FF_AIR_INT(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason)
#define FF_AIR_OUTCOME(N) bool N(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx)

// Custom outcomes of the Gust column ("air_cool" "air_cut" "air_split" "air_shrink" "air_shatter").
namespace AirOutcomes {
void report(IxResult& res, const std::string& outcome, const std::string& to = "");
double left(const IxResult& res, double absorb = 1.0);
FF_AIR_OUTCOME(o_cool);
FF_AIR_OUTCOME(o_cut);
FF_AIR_OUTCOME(o_split);
FF_AIR_OUTCOME(o_shrink);
FF_AIR_OUTCOME(o_shatter);
}  // namespace AirOutcomes

namespace AirVortex {
inline constexpr int E = 3;
inline constexpr int SUB = 1;
inline constexpr int MAX_CAPTURED = 8;
inline constexpr double SPIN = 7.0;
bool twister_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool twister_tick(CombatWorld& w, MatBody& b, double dt);
void tornado_effect(CombatWorld& w, MatBody& z, double dt);
Dict _scan_infusions(CombatWorld& w, MatBody& z);
std::string infusion_kind(const MatBody& b);
void _orbit(CombatWorld& w, MatBody& z, double dt);
void make_neutral(CombatWorld& w, MatBody& z, int src);
void eddy_effect(CombatWorld& w, MatBody& z, double dt);
void wall_effect(CombatWorld& w, MatBody& z, double dt);
bool unleash_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool funnel_down_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
MatBody* _enemy_tornado_near(CombatWorld& w, const ActorState& a, Vec3 p, double r);
FF_AIR_AFTER(eye_after);
FF_AIR_START(eye_tick);
Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir);
FF_AIR_START(spin_tick);
FF_AIR_AFTER(whirl_after);
FF_AIR_PHASE(whirl_phase);
FF_AIR_INT(whirl_interrupt);
FF_AIR_OUTCOME(o_infuse);
FF_AIR_OUTCOME(o_catch);
FF_AIR_OUTCOME(o_slow_bend);
FF_AIR_OUTCOME(o_spatter);
FF_AIR_OUTCOME(o_contest);
}  // namespace AirVortex

namespace AirVacuum {
inline constexpr int E = 3;
inline constexpr int SUB = 2;
bool palm_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
MatBody* spawn_well(CombatWorld& w, ActorState& a, ActionInst& inst, Vec3 p, double radius, double power);
bool well_tick(CombatWorld& w, MatBody& z, double dt);
void well_effect(CombatWorld& w, MatBody& z, double dt);
void collapse(CombatWorld& w, MatBody& z);
bool suction_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool mine_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
void mine_effect(CombatWorld& w, MatBody& z, double dt);
void bubble_effect(CombatWorld& w, MatBody& z, double dt);
bool wave_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_AIR_PHASE(well_phase);
FF_AIR_INT(well_interrupt);
Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir);
FF_AIR_PHASE(hop_phase);
FF_AIR_START(slipstream_tick);
FF_AIR_OUTCOME(o_compress);
FF_AIR_OUTCOME(o_snuff);
FF_AIR_OUTCOME(o_spit);
}  // namespace AirVacuum

namespace AirSound {
inline constexpr int E = 3;
inline constexpr int SUB = 3;
inline constexpr double FLIGHT_HEIGHT = 2.2;
inline constexpr double FLIGHT_UPKEEP = 10.0;
inline constexpr int MAX_BOUNCES = 2;
bool clap_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool disrupt(CombatWorld& w, ActorState& a, ActorState& t, Agent& v);
bool lance_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool tremor_tick(CombatWorld& w, MatBody& b, double dt);
bool echo_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool reveal(CombatWorld& w, ActorState& t, int by);
FF_AIR_PHASE(boom_phase);
bool ping_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
MatBody* flight_zone(CombatWorld& w, const ActorState& a);
FF_AIR_START(flight_start);
FF_AIR_AFTER(flight_after);
void flight_effect(CombatWorld& w, MatBody& z, double dt);
void end_flight_zone(CombatWorld& w, MatBody& z, const std::string& why);
void end_flight(CombatWorld& w, ActorState& a, const std::string& why);
void _flight_off(CombatWorld& w, ActorState& a, const std::string& why);
Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir);
FF_AIR_AFTER(hover_after);
FF_AIR_OUTCOME(o_pop);
FF_AIR_OUTCOME(o_still);
}  // namespace AirSound

}  // namespace ff
