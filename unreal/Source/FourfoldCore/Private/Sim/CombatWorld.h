// Fourfold core - port of game/core/combat_world.gd: authoritative fixed-step combat simulation (60 Hz).
// Owns actors, material bodies, contests, hits and the energy / mass ledgers. No scene tree: the Session, the AI,
// the tests and replays all drive step().
//
// Order of one tick (docs/COMBAT_SPEC.md): 1 timers  2 intents -> action starts  3 action ticks  4 actor movement
// 5 grip contests  6 bodies  7 contacts (clash, zones)  8 regen  9 cleanup.
//
// Port notes: bodies live in `bodies` (shared_ptr, sim order) and are only removed from it in _cleanup(); sim code
// passes MatBody* / ActorState* (valid for the tick). Loops over `bodies` that may spawn bodies iterate by index with a
// live size check, exactly like GDScript's `for b in bodies`. Actions are shared (ActionRef) because a running action
// may replace itself: hold an ActionRef whenever GDScript keeps `var inst := a.action` across calls.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"
#include "Sim/ArenaMap.h"
#include "Sim/Interactions.h"
#include "Sim/MatBody.h"
#include "Util/Rng.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ff {

class Agent;
using AgentRef = std::shared_ptr<Agent>;

// Energy ledger (HU) and mass ledger (kg): every heat / mass change goes through them; tests check conservation.
struct EnergyLedger {
	double generated = 0.0, ambient = 0.0, vapor = 0.0, reserve_dissipated = 0.0, vented = 0.0, spent = 0.0,
	       freeze_dump = 0.0, removed = 0.0;
};
struct MassLedger {
	double ground_taken = 0.0, ground_returned = 0.0, vapor = 0.0, evaporated = 0.0, metal_taken = 0.0, metal_returned = 0.0,
	       water_to_plant = 0.0, plant_from_ground = 0.0, plant_returned = 0.0, moisture_taken = 0.0, burned = 0.0,
	       sand_to_glass = 0.0, glass_to_sand = 0.0, sand_to_sandstone = 0.0;
	double* field(std::string_view key);   // mass_ledger[key] by name (convert_mat ledger keys); nullptr when unknown
};

struct GripRequest {
	int actor = -1;
	int body = -1;
	double strength = 0.0;
	std::string verb;
	ActionRef inst;
};

using BodyFilter = std::function<bool(MatBody&)>;

class CombatWorld {
public:
	static constexpr double GRIP_MARGIN = 0.12;
	static constexpr double RESIDUAL_START = 0.6;
	static constexpr double RESIDUAL_DECAY = 1.7;
	static constexpr double TURN_RATE = 14.0;
	static constexpr double RUN_SPEED = 5.5;
	static constexpr double RUN_MIN = 4.0;
	static constexpr double WALK_MAX = 1.8;
	static constexpr double RUN_STICK = 0.62;
	static constexpr double ACCEL = 34.0;
	static constexpr double DECEL = 42.0;
	static constexpr double WAVE_TURN_RATE = 32.0;
	static constexpr double MORPH_WINDOW = 0.12;
	static constexpr double EVADE_HOLD_TIME = 0.2;
	static constexpr double WEAVE_COST = 6.0;
	static constexpr double COUNTER_CANCEL_COST = 8.0;
	static constexpr double COUNTER_CANCEL_WINDOW = 0.5;
	static constexpr int CHAIN_MAX = 3;

	ArenaMap arena;
	std::vector<std::unique_ptr<ActorState>> actors;
	std::vector<BodyRef> bodies;
	int64_t tick = 0;
	Rng rng;
	std::vector<Dict> events;
	MatBody* pool = nullptr;
	EnergyLedger ledger;
	MassLedger mass_ledger;

	std::unordered_map<std::string, int64_t> _ix_last;
	std::unordered_map<std::string, int64_t> _zone_pairs;
	std::unordered_map<std::string, int64_t> _partial_pairs;
	bool _record_events = true;

	explicit CombatWorld(uint64_t seed_value = 1);
	~CombatWorld();
	CombatWorld(const CombatWorld&) = delete;
	CombatWorld& operator=(const CombatWorld&) = delete;

	// ---------------------------------------------------------------- registry
	ActorState* add_actor(const std::string& nm, Vec3 p, int team, const Dict& kit = Dict(), int element = 0);
	ActorState* get_actor(int id) const;
	// Open world: removes a fighter (encounter over). Bodies it held fall free; locks on it clear. Ids stay unique.
	void remove_actor(int id);
	// Open world: shifts everything in local space by -d (the bubble origin moved by +d). Call between ticks.
	void translate(Vec3 d);
	MatBody* get_body(int id) const;
	BodyRef body_ref(int id) const;
	MatBody* spawn_body(Mat mat, Form form, double mass, Vec3 p, const std::string& origin, double temp = Sim::AMBIENT_C);
	MatBody* split_body(MatBody& parent, double mass, Vec3 p);
	void merge_bodies(MatBody& into, MatBody& other);
	void _set_energy(MatBody& b, double e);
	void remove_body(MatBody& b, const std::string& reason);
	int new_attack_id();
	void emit(const std::string& type, Dict d = Dict());
	std::vector<Dict> take_events();

	// ---------------------------------------------------------------- step
	void step(const std::map<int, ActorIntent>& intents);
	const ActorIntent& _intent_for(const ActorState& a) const;
	void _timers(ActorState& a);

	// ---------------------------------------------------------------- intents & actions
	static std::string gesture_slot(int g);
	void _process_intent(ActorState& a, const ActorIntent& it);
	void _gesture_morph(ActorState& a, const ActorIntent& it);
	ActionRef morph_action(ActorState& a, const std::string& id, const std::string& slot, const ActorIntent& it,
	                       bool at_release = false, const Dict& extra = Dict());
	void _guard_gesture(ActorState& a, const ActorIntent& it);
	void _evade_hold(ActorState& a, const ActorIntent& it);
	bool can_cancel(ActorState& a, const std::string& into, const std::string& slot = "");
	bool _chain_ok(ActorState& a, ActionInst& inst, const std::string& slot, double rec);
	bool _counter_cancel_ok(ActorState& a, const std::string& into);
	MatBody* incoming_threat(ActorState& a, double within);
	bool _try_start(ActorState& a, const std::string& press, const ActorIntent& it, const std::string& slot = "");
	ActionRef start_action(ActorState& a, const std::string& id, const ActorIntent& it, const Dict& pre = Dict());
	void set_phase(ActorState& a, ActionInst& inst, ActionPhase p);
	void finish_action(ActorState& a, ActionInst& inst);
	void interrupt_action(ActorState& a, const std::string& reason);
	void _enter_after_startup(ActorState& a, ActionInst& inst, const ActorIntent& it);
	void _tick_action(ActorState& a, const ActorIntent& it);
	double _startup_of(const ActorState& a, const ActionInst& inst) const;
	double _active_of(const ActionInst& inst) const;
	void module_start(const std::string& module, ActorState& a, ActionInst& inst, const ActorIntent& it);
	ActionPhase module_after_startup(const std::string& module, ActorState& a, ActionInst& inst, const ActorIntent& it);
	void module_phase(const std::string& module, ActorState& a, ActionInst& inst, ActionPhase p);
	void module_tick(const std::string& module, ActorState& a, ActionInst& inst, const ActorIntent& it);
	void module_interrupt(const std::string& module, ActorState& a, ActionInst& inst, const std::string& reason);
	ActionPhase attack_after_startup(ActorState& a, ActionInst& inst, const ActorIntent& it);

	// ---------------------------------------------------------------- movement & targeting
	void _move_actor(ActorState& a, const ActorIntent& it);
	double _ground_under(Vec3 p, double from_y) const;
	MatBody* zone_surface_at(Vec3 p) const;
	double _friction_at(const ActorState& a) const;
	void _update_surface(ActorState& a);
	void _separate_actors();
	Vec3 _push_out_walls(Vec3 p, double r) const;
	Vec3 _push_out_obb(Vec3 p, double r, const MatBody& w) const;
	bool point_in_wall(Vec3 p, const MatBody& w, double pad = 0.0) const;
	bool _valid_target(const ActorState& a, int id) const;
	bool _lockable(const ActorState& a, const ActorState& t) const;
	int _auto_target(const ActorState& a) const;
	void _cycle_target(ActorState& a);
	Vec3 aim_dir(const ActorState& a, const ActorIntent& it) const;
	Vec3 aim_point(const ActorState& a, const ActorIntent& it) const;
	MatBody* find_body(const ActorState& a, Vec3 dir, double reach, double cone_deg, const BodyFilter& filter) const;
	MatBody* puddle_at(Vec3 p) const;

	// ---------------------------------------------------------------- resources
	double pay_heat(ActorState& a, double hu, bool allow_partial = true);
	bool can_pay_heat(const ActorState& a, double hu) const;
	bool spend_focus(ActorState& a, double f);
	void _regen(ActorState& a);

	// ---------------------------------------------------------------- hits
	// info: attacker, attack_id, damage, balance, knock, kind, from, body, facing_vel, unblockable, src_attack, power,
	// tier, mat ... `agent` is GDScript's info.agent (the threat agent of the hit, may be null).
	std::string hit_actor(ActorState& t, const Dict& info, AgentRef agent = nullptr);
	Dict _hit_meta(const Dict& info, const Agent* agent, const ActorState& t) const;
	void _mark_contact(const Dict& info);
	std::string guard_chip(ActorState& t, const Dict& info, const Dict& rule, const Agent* threat = nullptr, double ratio = -1.0);
	void _stagger(ActorState& t, const std::string& kind, double dur, const Dict& info);
	bool perfect_guard(const ActorState& t) const;
	int guard_element(const ActorState& a) const;
	std::vector<ActorState*> actors_in_cone(const ActorState& a, Vec3 dir, double rng_m, double cone_deg) const;
	bool _wall_between(Vec3 p0, Vec3 p1) const;
	double wall_hit(Vec3 p0, Vec3 p1) const;
	double wall_segment_t(Vec3 p0, Vec3 p1, const MatBody& b) const;
	bool los(Vec3 p0, Vec3 p1) const;

	// ---------------------------------------------------------------- grips / control
	void request_grip(ActorState& a, MatBody& b, double strength, const std::string& verb);
	double grip_strength(const ActorState& a, const MatBody& b, double base, double reach) const;
	void _resolve_grips();
	void take_control(ActorState& a, MatBody& b, double strength, const std::string& verb);
	MatBody* release_body(ActorState& a, Vec3 v, bool as_attack, double damage = 0.0, double balance = 0.0);
	MatBody* held(ActorState& a);

	// ---------------------------------------------------------------- bodies
	void _update_bodies();
	void _material_tick(MatBody& b, double dt);
	void _on_phase_changed(MatBody& b, Phase old_phase);
	void _update_ballistic(MatBody& b, double dt);
	void _home(MatBody& b, double dt);
	void _ricochet(MatBody& b, Vec3 at);
	void _quench_contact(MatBody& b, double dt);
	void _body_impact(MatBody& b, const std::string& what);
	IxResult _body_hits_wall(MatBody& b, MatBody& w);
	void _crumble_wall(MatBody& w);
	void _update_wall(MatBody& w, double dt);
	void _update_wave(MatBody& b, double dt);
	void _wave_sweep(MatBody& wv);
	void _settle_wave(MatBody& b, const std::string& why);
	void _quench(MatBody& lava, MatBody& water, double dt);
	void quench_energy(MatBody& lava, MatBody& water, double q_max);
	double heat_body(MatBody& b, double energy);
	double boil_water(MatBody& water, double energy, Vec3 at);
	void _spawn_steam(Vec3 p, double kg);
	void _water_to_puddle(MatBody& b);
	void _merge_puddle(MatBody& b);
	void _shatter(MatBody& b);
	void _return_mass(MatBody& b);
	void decay_body(MatBody& b, const std::string& why);
	void convert_mat(MatBody& b, Mat new_mat, const std::string& ledger_key = "");
	MatBody* grow_plant(MatBody* src, double kg, Vec3 p, ActorState* a = nullptr);
	void burn_plant(MatBody& b, double kg);
	void release_captured(MatBody& captor);

	// ---------------------------------------------------------------- zones
	MatBody* spawn_zone(const std::string& tag, Vec3 p, double radius, int owner_id, double power = 0.0, Mat mat = Mat::Air,
	                    double mass = 0.0, double life = -1.0, const std::string& origin = "zone");
	void close_zone(MatBody& z, const std::string& why);
	void _update_zone(MatBody& z, double dt);
	void _zone_pass();
	bool _in_zone(const MatBody& z, Vec3 p, double r) const;
	std::vector<MatBody*> bodies_in_zone(const MatBody& z) const;
	std::vector<ActorState*> actors_in_zone(const MatBody& z) const;

	// ---------------------------------------------------------------- contacts
	void _contacts();
	void _clash_pass();
	double hit_scale(const MatBody& b) const;
	bool _touches_actor(const MatBody& b, const ActorState& a, double pad) const;
	void _projectile_hits_actor(MatBody& b, ActorState& a);

	// ---------------------------------------------------------------- cleanup & accounting
	void _cleanup();
	int alive_count() const;
	void _enforce_cap();
	void trim_remnants();
	bool player_focus_low(const ActorState& a) const { return a.focus < 8.0; }
	double system_energy() const;
	double ledger_balance() const;
	double water_mass() const;
	double stone_mass() const;
	double earth_mass() const;
	double metal_mass() const;
	double plant_mass() const;

	// ---------------------------------------------------------------- hooks (process-wide registries, Hooks.h)
	Dict tech_preview(ActorState& a, Vec3 dir);

	// Snapshot of the list for loops that must not see bodies spawned meanwhile (GDScript bodies.duplicate()).
	std::vector<MatBody*> body_list() const;
	int next_body_id() const { return _next_body; }   // GDScript w._next_body (Lab spawner)
	uint64_t instance_id() const { return _uid; }       // GDScript get_instance_id() (process-unique, never reused)

private:
	uint64_t _uid = 0;
	int _next_actor_id = 0;
	int _next_body = 1;
	int _next_attack = 1;
	std::vector<GripRequest> _grips;
	const std::map<int, ActorIntent>* _intents = nullptr;
	std::unordered_map<int, MatBody*> _body_by_id;
	ActorIntent _null_intent;
};

}  // namespace ff
