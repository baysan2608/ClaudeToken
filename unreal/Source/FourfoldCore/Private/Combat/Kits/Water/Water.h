// Fourfold core - the Water kit (docs/kits/water.md, MOVESET §7.5-§7.8): ports of game/combat/kits/water/
// water_water.gd + water_jet.gd (sub 0), water_ice.gd (1), water_mist.gd (2), water_plant.gd (3) and the outcome handlers
// / zone effects of water_rules.gd (the cells are data: Data/rules.json). Move defs are data (Data/moves.json); these
// files hold the code the defs and KitWater.handle() reference.
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

#define FF_WATER_START(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it)
#define FF_WATER_AFTER(N) ActionPhase N(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it)
#define FF_WATER_PHASE(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p)
#define FF_WATER_INT(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason)

namespace WaterJet {
inline constexpr double PULSE = 0.1;
bool start(CombatWorld& w, ActorState& a, ActionInst& inst);
void tick(CombatWorld& w, ActorState& a, ActionInst& inst);
void end(CombatWorld& w, ActorState& a, ActionInst& inst);
}  // namespace WaterJet

namespace WaterWater {
bool bullet_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_WATER_START(bullet_tick);
FF_WATER_PHASE(bullet_phase);
FF_WATER_INT(bullet_interrupt);
double tidal_scale(CombatWorld& w, ActorState& a, int tier);
bool tidal_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool wave_tick(CombatWorld& w, MatBody& b, double dt);
bool wave_contacts(CombatWorld& w, MatBody& b);
bool spray_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool orb_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool orb_impact(CombatWorld& w, MatBody& b, const std::string& what);
FF_WATER_AFTER(after_active);
FF_WATER_START(slick_start);
void slick_zone(CombatWorld& w, MatBody& z, double dt);
FF_WATER_START(riptide_start);
FF_WATER_START(evade_tick);
FF_WATER_PHASE(evade_phase);
FF_WATER_START(ride_tick);
Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir);
}  // namespace WaterWater

namespace WaterIce {
bool spear_impact(CombatWorld& w, MatBody& b, const std::string& what);
bool rime_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool rime_tick(CombatWorld& w, MatBody& b, double dt);
MatBody* ice_zone(CombatWorld& w, Vec3 p, double radius, int owner_id, double life, double power = 10.0);
bool hoarfrost_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_WATER_START(wall_start);
bool wall_tick(CombatWorld& w, MatBody& b, double dt);
void _slide(CombatWorld& w, MatBody& b, double dt);
bool shove_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_WATER_START(freeze_draw_tick);
FF_WATER_START(glide_start);
FF_WATER_AFTER(skate_after);
FF_WATER_PHASE(skate_phase);
FF_WATER_INT(skate_interrupt);
}  // namespace WaterIce

namespace WaterMist {
bool puff_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool geyser_tick(CombatWorld& w, MatBody& z, double dt);
bool lance_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool fog_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool veil_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool blast_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool dew_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_WATER_START(vapor_tick);
bool ball_impact(CombatWorld& w, MatBody& b, const std::string& what);
FF_WATER_START(step_start);
FF_WATER_PHASE(step_phase);
FF_WATER_AFTER(walk_after);
FF_WATER_PHASE(walk_phase);
FF_WATER_INT(walk_interrupt);
}  // namespace WaterMist

namespace WaterPlant {
MatBody* grow(CombatWorld& w, double kg, Vec3 p, double life = -1.0);
MatBody* plant_zone(CombatWorld& w, const std::string& tag, Vec3 p, double radius, int owner_id, double life, double kg, const Dict& props,
                    double power = 0.0);
bool lash_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool burr_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool seed_impact(CombatWorld& w, MatBody& b, const std::string& what);
bool roots_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool roots_tick(CombatWorld& w, MatBody& b, double dt);
bool thicket_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_WATER_START(lattice_start);
FF_WATER_START(lattice_tick);
bool roll_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool vine_tick(CombatWorld& w, MatBody& b, double dt);
FF_WATER_START(roots_stance_tick);
FF_WATER_START(vinegrip_tick);
FF_WATER_START(swing_start);
FF_WATER_PHASE(swing_phase);
FF_WATER_START(canopy_tick);
}  // namespace WaterPlant

namespace WaterRules {
using OutcomeSig = bool(CombatWorld&, Agent&, Agent&, IxResult&, const Dict&, IxCtx&);
OutcomeSig o_carry, o_ridge, o_freeze, o_skin, o_hot_block, o_dampen, o_condense_in, o_brittle, o_feed, o_drown, o_melt, o_quench, o_burn,
    o_sling;
void make_ridge(CombatWorld& w, MatBody& b, ActorState* owner, double stand);
void fog_zone(CombatWorld& w, MatBody& z, double dt);
void ice_floor_zone(CombatWorld& w, MatBody& z, double dt);
void steam_zone(CombatWorld& w, MatBody& z, double dt);
}  // namespace WaterRules

#undef FF_WATER_START
#undef FF_WATER_AFTER
#undef FF_WATER_PHASE
#undef FF_WATER_INT

}  // namespace ff
