// Fourfold core - the Earth kit (docs/kits/earth.md, MOVESET §7.1-§7.4): ports of game/combat/kits/earth/
// earth_stone.gd (sub 0), earth_metal.gd (1), earth_sand.gd (2), earth_magma.gd (3) and the outcome handlers of
// earth_rules.gd (the cells themselves are data: Data/rules.json). Move defs are data (Data/moves.json); these
// files hold the code the defs reference by name (hooks, body ticks, zone effects, previews, part lifecycles).
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

#define FF_EARTH_PART_DECL                                                                                      \
	void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);                      \
	ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);          \
	void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);                              \
	void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);                       \
	void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);

namespace EarthStone {
inline constexpr double BULWARK_MASS[3] = {120.0, 160.0, 200.0};
inline constexpr double BULWARK_TIMES[2] = {1.0, 1.8};
inline constexpr double RAM_SPEED = 9.0;
inline constexpr double SPIKE_CP = 20.0;
inline constexpr double SPIKE_LIFE = 1.5;
void bulwark_channels(CombatWorld& w, MatBody& b, Agent& g);
bool thicken(CombatWorld& w, ActorState& a, MatBody& b, double held_s);
FF_EARTH_PART_DECL
bool fangs_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool spike_tick(CombatWorld& w, MatBody& b, double dt);
void _erupt(CombatWorld& w, MatBody& b);
void _make_spikes(CombatWorld& w, int owner, MatBody& b, double yaw, double half_x, double half_y);
void release_hold(CombatWorld& w, MatBody& b);
bool crag_tick(CombatWorld& w, MatBody& b, double dt);
}  // namespace EarthStone

namespace EarthMetal {
inline constexpr double SATCHEL_MAX = 30.0;
inline constexpr double SCRAP_MASS = 10.0;
inline constexpr double SCRAP_COOLDOWN = 1.2;
inline constexpr double RECALL_SPEED = 20.0;
inline constexpr double ORBIT_R = 1.25;
inline constexpr double PLATE_HOT_C = 300.0;
void to_satchel(CombatWorld& w, ActorState& a, MatBody* b, const std::string& why);
void mark(MatBody* b, const ActorState& a);
std::vector<MatBody*> owned(CombatWorld& w, const ActorState& a);
bool owned_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool disc_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool lance_impact(CombatWorld& w, MatBody& b, const std::string& what);
bool line_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool filings_tick(CombatWorld& w, MatBody& b, double dt);
bool metal_tick(CombatWorld& w, MatBody& b, double dt);
void recall(CombatWorld& w, ActorState& a, ActionInst& inst, const std::vector<MatBody*>& pieces);
bool chain_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
FF_EARTH_PART_DECL
Dict preview(CombatWorld& w, ActorState& a, Vec3 dir);
bool rod_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
void rod_spread(CombatWorld& w, Agent& t, MatBody* rod, double e);
}  // namespace EarthMetal

namespace EarthSand {
inline constexpr double SETTLE_TIME = 1.2;
inline constexpr double GATHER_RATE = 6.0;
inline constexpr double GATHER_MAX = 30.0;
inline constexpr double GATHER_FOCUS = 2.0;
bool slug_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool slug_impact(CombatWorld& w, MatBody& b, const std::string& what);
bool slug_tick(CombatWorld& w, MatBody& b, double dt);
bool blast_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool surge_tick(CombatWorld& w, MatBody& b, double dt);
void cloud_effect(CombatWorld& w, MatBody& z, double dt);
void quicksand_effect(CombatWorld& w, MatBody& z, double dt);
bool pit_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool mud_tick(CombatWorld& w, MatBody& b, double dt);
FF_EARTH_PART_DECL
Dict preview(CombatWorld& w, ActorState& a, Vec3 dir);
}  // namespace EarthSand

namespace EarthMagma {
inline constexpr double SURGE_REACH = 6.0;
inline constexpr double VEIN_MASS = 8.0;
inline constexpr double VEIN_HU = 160.0;
inline constexpr double POOL_SET_LIQUID = 0.04;
bool clot_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool bomb_impact(CombatWorld& w, MatBody& b, const std::string& what);
void pool_effect(CombatWorld& w, MatBody& z, double dt);
bool lash_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
void pour_face(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wall, double hu);
bool surge_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
bool pit_execute(CombatWorld& w, ActorState& a, ActionInst& inst);
void pit_effect(CombatWorld& w, MatBody& z, double dt);
FF_EARTH_PART_DECL
Dict preview(CombatWorld& w, ActorState& a, Vec3 dir);
}  // namespace EarthMagma

namespace EarthRules {
using OutcomeSig = bool(CombatWorld&, Agent&, Agent&, IxResult&, const Dict&, IxCtx&);
OutcomeSig _o_embed, _o_stick, _o_face_heat, _o_absorb_face, _o_feed_face, _o_set, _o_glass_beads, _o_crust, _o_mud, _o_glassify,
    _o_glass_ground, _o_plate_heat, _o_plate_bolt, _o_magnet_catch, _o_rod_ground, _o_rod_melt, _o_melt_in, _o_bolt_grit, _o_quench,
    _o_smother, _o_ram_push, _o_ram_blocked, _o_ram_both, _o_wrap, _o_spike_stop, _o_glaze, _o_drag;
void _stop_body(MatBody& b);
double _left(const IxResult& res);
void _to_glass(CombatWorld& w, MatBody& b, const std::string& why);
}  // namespace EarthRules

#undef FF_EARTH_PART_DECL

}  // namespace ff
