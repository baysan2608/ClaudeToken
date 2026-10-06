// Fourfold core - ports of game/combat/verbs/*.gd: generic move templates (MOVESET §15.7; COMBAT_SPEC E6).
// A def with module "verbs" and a `verb` key runs on data: projectile cone beam burst ground_line barrier zone grip
// ranged_heat summon dash mode stance. Tier parameters are read with Charge::param; costs go through Verbs::pay;
// every verb emits FxEvents cues. Per-move code hooks: hook_execute, hook_tick, hook_impact (Hooks.h).
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"
#include "Sim/Agent.h"
#include "Sim/Interactions.h"

#include <string>
#include <string_view>
#include <vector>

namespace ff {

class CombatWorld;
class MatBody;

// GDScript `P := func(k, dflt): return over[k] if over.has(k) else Charge.param(inst, k, dflt)`.
struct VerbParams {
	const ActionInst* inst = nullptr;
	Dict over;
	Value get(std::string_view k, const Value& dflt = Value()) const;
	double f(std::string_view k, double dflt) const { return vnum_(get(k, Value(dflt)), dflt); }
	int i(std::string_view k, int dflt) const;
	bool b(std::string_view k, bool dflt) const;
	std::string s(std::string_view k, std::string_view dflt) const;
	static double vnum_(const Value& v, double d) { return v.is_nil() ? d : v.as_float(d); }
};

namespace Verbs {
bool is_channel_verb(std::string_view verb);
void ensure();
void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
void execute(CombatWorld& w, ActorState& a, ActionInst& inst);
// stage "start" / "release" (pay_stage) or an explicit {focus, heat, water, metal} (pay_dict).
bool pay(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& stage = "start");
bool pay_dict(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& need);
void _take_pool(CombatWorld& w, ActorState& a, double kg);
double take_heat(ActionInst& inst);
Mat mat_id(const Value& name);
std::string fx_mat(const ActionInst& inst);
void fx(CombatWorld& w, const ActorState& a, const ActionInst& inst, const std::string& key, const Dict& extra = Dict());
Vec3 target_point(CombatWorld& w, const ActorState& a, const ActionInst& inst, double reach = 14.0);
Vec3 launch_vel(Vec3 from, Vec3 to, double h_speed, double gscale = 1.0);
void arm(CombatWorld& w, const ActorState& a, const ActionInst& inst, MatBody& b, double damage, double balance);
void on_impact(CombatWorld& w, MatBody& b, const std::string& what);
void on_wave_end(CombatWorld& w, MatBody& b, const std::string& why);
void leave_trail(CombatWorld& w, MatBody& b);
Dict grip_preview(CombatWorld& w, ActorState& a, const Dict& d, Vec3 dir);
}  // namespace Verbs

namespace VerbProjectile {
std::vector<MatBody*> fire(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& over = Dict());
std::string _source_mat(const std::string& source);
MatBody* spawn(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& source, Mat mat, double mass, double heat,
               const VerbParams& P);
void on_impact(CombatWorld& w, MatBody& b, const std::string& what);
}  // namespace VerbProjectile

namespace VerbVolume {
AgentRef _volume(CombatWorld& w, ActorState& a, ActionInst& inst, Vec3 pos, Vec3 dir);
std::string _default_cls(const ActionInst& inst);
Dict _hit_info(const ActorState& a, const ActionInst* inst, const Agent& v, Vec3 from, Vec3 knock_dir);
void _after_hit(CombatWorld& w, ActorState& a, ActionInst& inst, ActorState& t, const std::string& res);
IxResult meet_body(CombatWorld& w, ActorState* a, Agent& v, MatBody& b);
AgentRef cone(CombatWorld& w, ActorState& a, ActionInst& inst);
AgentRef beam(CombatWorld& w, ActorState& a, ActionInst& inst);
void beam_tick(CombatWorld& w, ActorState& a, ActionInst& inst);
AgentRef _beam_once(CombatWorld& w, ActorState& a, ActionInst& inst);
void burst(CombatWorld& w, ActorState& a, ActionInst& inst);
// prm: radius power damage balance knock lift cls heat_hu mat status status_t
void burst_at(CombatWorld& w, ActorState* a, ActionInst* inst, Vec3 p, const Dict& prm);
bool fuse_tick(CombatWorld& w, MatBody& z, double dt);
}  // namespace VerbVolume

namespace VerbGroundLine {
MatBody* launch(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& over = Dict());
void on_end(CombatWorld& w, MatBody& b, const std::string& why);
void leave_trail(CombatWorld& w, MatBody& b);
}  // namespace VerbGroundLine

namespace VerbBarrier {
Dict _spec(const ActionInst& inst);
void start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void _raise_wall(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& spec);
bool _take_source(CombatWorld& w, ActorState& a, const std::string& source, double mass);
void _give_back(CombatWorld& w, ActorState& a, const std::string& source, MatBody& b, double kg);
void _take_held(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& spec);
void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void end(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
void raise_free(CombatWorld& w, ActorState& a, ActionInst& inst);
}  // namespace VerbBarrier

namespace VerbZone {
Vec3 _point(CombatWorld& w, const ActorState& a, const ActionInst& inst);
MatBody* spawn(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& over = Dict());
void summon_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void summon_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void summon_release(CombatWorld& w, ActorState& a, ActionInst& inst);
}  // namespace VerbZone

namespace VerbGrip {
inline constexpr double PLATE_REACH = 3.0;
inline constexpr double PLATE_RIP = 10.0;
bool filter(const ActorState& a, const Dict& d, MatBody& b);
Dict preview(CombatWorld& w, ActorState& a, const Dict& d, Vec3 dir);
void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void _seek(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& d);
void shape(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b);
void throw_(CombatWorld& w, ActorState& a, ActionInst& inst);
void drop(CombatWorld& w, ActorState& a, ActionInst& inst);
}  // namespace VerbGrip

namespace VerbHeat {
void start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
MatBody* _split_face(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wall);
void _slump(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wall, MatBody& face);
void end(CombatWorld& w, ActorState& a, ActionInst& inst);
}  // namespace VerbHeat

namespace VerbMotion {
void dash_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void dash_tick(CombatWorld& w, ActorState& a, ActionInst& inst);
void mode_start(CombatWorld& w, ActorState& a, ActionInst& inst);
void mode_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void mode_end(CombatWorld& w, ActorState& a, ActionInst& inst);
void stance_start(CombatWorld& w, ActorState& a, ActionInst& inst);
void stance_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void stance_end(CombatWorld& w, ActorState& a, ActionInst& inst);
}  // namespace VerbMotion

}  // namespace ff
