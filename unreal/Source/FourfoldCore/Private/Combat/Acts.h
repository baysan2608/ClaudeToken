// Fourfold core - ports of game/combat/act_common.gd, act_earth.gd, act_water.gd, act_fire.gd, act_air.gd: the legacy
// (sub-0) kits with today's exact numbers (tests pin them). Module names "common" "earth" "water" "fire" "air".
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"

#include <string>

namespace ff {

class CombatWorld;
class MatBody;

namespace ActCommon {
inline constexpr double WALL_COST = 8.0;
inline constexpr double WALL_DIST = 1.25;
void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
bool _has_spec(const ActionInst& inst);
void raise_wall(CombatWorld& w, ActorState& a, ActionInst& inst);
void water_shield(CombatWorld& w, ActorState& a, ActionInst& inst);
void _end_guard(CombatWorld& w, ActorState& a, ActionInst& inst, bool keep_material = false);
}  // namespace ActCommon

namespace ActEarth {
inline constexpr double LOOSE_RADIUS = 2.6;
bool _stone_filter(MatBody& b);
void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
void _seek(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void _throw_held(CombatWorld& w, ActorState& a, ActionInst& inst);
void _ladder(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b);
void _split(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b);
void _throw_split(CombatWorld& w, ActorState& a, ActionInst& inst);
Vec3 _throw_target(CombatWorld& w, ActorState& a, ActionInst& inst);
MatBody* _rip(CombatWorld& w, ActorState& a, double mass);
MatBody* _loose_stone_near(CombatWorld& w, ActorState& a);
void _drop(CombatWorld& w, ActorState& a);
void _fizzle(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& what);
Vec3 launch_vel(Vec3 from, Vec3 to, double h_speed);
}  // namespace ActEarth

namespace ActWater {
bool vapor_filter(MatBody& b);
bool enemy_water(MatBody& b, const ActorState& a);
bool _water_filter(MatBody& b);
void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
void _draw(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void _stream(CombatWorld& w, ActorState& a, ActionInst& inst);
void _lash(CombatWorld& w, ActorState& a, ActionInst& inst);
void _ice_lance(CombatWorld& w, ActorState& a, ActionInst& inst);
void _torrent(CombatWorld& w, ActorState& a, ActionInst& inst);
void _maelstrom(CombatWorld& w, ActorState& a, ActionInst& inst);
bool _seize(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it, double reach);
void _condense(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& vp, MatBody* b, double take);
void _freeze_held(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b);
}  // namespace ActWater

namespace ActFire {
inline constexpr double GRIP_WINDOW = 0.25;
inline constexpr double INCOMING_RANGE = 14.0;
inline constexpr double VENT_MIN = 40.0;
Dict preview(CombatWorld& w, ActorState& a, Vec3 dir);
MatBody* scorch_target(CombatWorld& w, ActorState& a, Vec3 dir);
void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
Vec3 _face_target(CombatWorld& w, ActorState& a, ActionInst& inst);
void _heat_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void _heat_water(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b, const ActorIntent& it);
void _draw_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void _pour(CombatWorld& w, ActorState& a, ActionInst& inst);
// {pos, blocked}
Vec3 _pour_start(CombatWorld& w, const ActorState& a, Vec3 dir, bool* blocked);
void _flare(CombatWorld& w, ActorState& a, ActionInst& inst);
void _column(CombatWorld& w, ActorState& a, ActionInst& inst);
void _vent(CombatWorld& w, ActorState& a);
}  // namespace ActFire

namespace ActAir {
inline constexpr double LIGHT_MASS = 30.0;
inline constexpr double POWER = 7.0;
inline constexpr double HEAVY_POWER = 11.0;
void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
bool _wind_grip_context(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void _push(CombatWorld& w, ActorState& a, ActionInst& inst);
}  // namespace ActAir

}  // namespace ff
