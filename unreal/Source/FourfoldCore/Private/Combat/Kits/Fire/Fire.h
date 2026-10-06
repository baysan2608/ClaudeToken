// Fourfold core - the Fire kit (docs/kits/fire.md, MOVESET §7.9-§7.12): ports of game/combat/kits/fire/
// fire_flame.gd (sub 0), fire_blue.gd (1), fire_lightning.gd (2), fire_combustion.gd (3) and the outcome handlers /
// channel hooks of fire_rules.gd (the cells are data: Data/rules.json). Move defs are data (Data/moves.json); these files
// hold the code the defs and KitFire.handle() reference (hooks, body ticks, zone effects, previews, stage overrides).
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"
#include "Sim/Agent.h"
#include "Sim/Interactions.h"

#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class MatBody;

#define FF_FIRE_STAGE_START(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it)
#define FF_FIRE_STAGE_AFTER(N) ActionPhase N(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it)
#define FF_FIRE_STAGE_PHASE(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p)
#define FF_FIRE_STAGE_TICK(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it)
#define FF_FIRE_STAGE_INT(N) void N(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason)

namespace FireFlame {
inline constexpr double HEAT_SINK_DRAW = 300.0;
inline constexpr double HEAT_SINK_REACH = 2.4;
bool fireball_impact(CombatWorld& w, MatBody& b, const std::string& what);
void burst_fire_body(CombatWorld& w, MatBody* b, const std::string& why);
bool fireball_tick(CombatWorld& w, MatBody& b, double dt);
bool line_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool line_tick(CombatWorld& w, MatBody& b, double dt);
FF_FIRE_STAGE_TICK(guard_tick);
bool backdraft_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool ground_heat_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_FIRE_STAGE_TICK(dash_tick);
FF_FIRE_STAGE_TICK(hop_tick);
}  // namespace FireFlame

namespace FireBlue {
inline constexpr double PULSE = 0.1;
inline constexpr double WALL_FACE_HU = 50.0;
FF_FIRE_STAGE_PHASE(needle_phase);
FF_FIRE_STAGE_TICK(needle_tick);
FF_FIRE_STAGE_INT(needle_interrupt);
bool furrow_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool rift_tick(CombatWorld& w, MatBody& b, double dt);
bool corona_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool kiln_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
void kiln_tick(CombatWorld& w, MatBody& z, double dt);
Dict smelter_preview(CombatWorld& w, ActorState& a, Vec3 dir);
FF_FIRE_STAGE_TICK(afterburn_tick);
}  // namespace FireBlue

namespace FireLightning {
inline constexpr double STATIC_MAX = 60.0;
inline constexpr double CHARGE_DECAY = 4.0;
inline constexpr double SKYBREAK_DELAY = 0.4;
FF_FIRE_STAGE_PHASE(spark_phase);
bool rail_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool current_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool current_tick(CombatWorld& w, MatBody& b, double dt);
bool fan_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_FIRE_STAGE_TICK(ward_tick);
bool burst_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_FIRE_STAGE_TICK(grounding_tick);
Dict hand_preview(CombatWorld& w, ActorState& a, Vec3 dir);
FF_FIRE_STAGE_AFTER(hand_after);
FF_FIRE_STAGE_TICK(hand_tick);
FF_FIRE_STAGE_PHASE(hand_phase);
void charge_body(CombatWorld& w, MatBody* b, double e, int owner_id);
void static_field_tick(CombatWorld& w, MatBody& z, double dt);
FF_FIRE_STAGE_TICK(arc_step_tick);
FF_FIRE_STAGE_TICK(overcharge_tick);
}  // namespace FireLightning

namespace FireCombustion {
inline constexpr double REACTIVE_COST = 8.0;
inline constexpr double MINE_RANGE = 1.5;
inline constexpr double MINE_LIFE = 12.0;
MatBody* pocket(CombatWorld& w, ActorState* a, Vec3 p, double delay, const Dict& prm, double heat, int tier, const std::string& mode = "strike");
Dict fire_pocket(CombatWorld& w, MatBody* z);
void fuse_zone_tick(CombatWorld& w, MatBody& z, double dt);
bool pop_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_FIRE_STAGE_START(mine_start);
bool mine_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool mine_impact(CombatWorld& w, MatBody& b, const std::string& what);
bool ember_tick(CombatWorld& w, MatBody& b, double dt);
void pop_ember(CombatWorld& w, MatBody* b, const std::string& why);
bool chain_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool scatter_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool smother_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
Dict fuse_preview(CombatWorld& w, ActorState& a, Vec3 dir);
FF_FIRE_STAGE_AFTER(fuse_after);
FF_FIRE_STAGE_TICK(fuse_tick_action);
FF_FIRE_STAGE_PHASE(fuse_phase);
FF_FIRE_STAGE_INT(fuse_interrupt);
FF_FIRE_STAGE_START(jump_start);
FF_FIRE_STAGE_TICK(afterglow_tick);
}  // namespace FireCombustion

namespace FireRules {
using OutcomeSig = bool(CombatWorld&, Agent&, Agent&, IxResult&, const Dict&, IxCtx&);
OutcomeSig o_heat, o_evaporate, o_burn, o_melt, o_snuffed, o_dampen, o_fanned, o_blown, o_tornado, o_guard_absorb, o_aegis_melt, o_static,
    o_static_full, o_reactive, o_counter_blast, o_body_burst, o_charge_body, o_disrupt_zone, o_fill_void, o_suppressed, o_fulgurite, o_glassify,
    o_conduct_owner, o_smother;
void _fire_channels(CombatWorld& w, MatBody& b, Agent& g);
void _current_channels(CombatWorld& w, MatBody& b, Agent& g);
Dict plain_guard(const Dict& extra = Dict());   // CoreRules.plain_guard
}  // namespace FireRules

#undef FF_FIRE_STAGE_START
#undef FF_FIRE_STAGE_AFTER
#undef FF_FIRE_STAGE_PHASE
#undef FF_FIRE_STAGE_TICK
#undef FF_FIRE_STAGE_INT

}  // namespace ff
