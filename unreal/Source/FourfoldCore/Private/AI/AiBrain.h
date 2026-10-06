// Fourfold core - port of game/actors/ai_brain.gd: the sparring opponent. Reads only observable world state, reacts
// after a human-like delay, pays the same costs and is limited to its configured kit. Produces an ActorIntent per tick.
// cfg keys: aggression 0..1, counter 0..1, reaction s, elements [int], drill ("" free sparring, stone_rain, passive,
// lightning, seize, matrix, "element:<e>/<sub>"), interval s. configure(cfg) switches to the planner mode (presets).
// AiBrain.cpp: think + legacy rules; AiBrainPlanner.cpp: planner mode (configure, perception, plans, offense, drills).
#pragma once

#include "ff/Value.h"
#include "AI/AiPlanner.h"
#include "Sim/ActorState.h"
#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ff {

class CombatWorld;
class MatBody;

// A counter / offense plan being executed (GDScript _plan dictionary): an AiOption + execution state.
struct AiPlan {
	bool active = false;
	AiOption o;
	double t0 = 0.0;
	std::string stage;          // wait | guard_gesture | after_gesture | tech | evade_hold
	int body = -1;
	int attack_id = 0;
	double tti_abs = 0.0;
	std::string legacy;         // "" | draw | redirect | dodge
	double press_at = 0.0;
	bool has_guard_until = false;
	double guard_until = 0.0;
	double press_t = 0.0;
	double gesture_at = 0.0;
	double gest_t = 0.0;
	bool started = false;
	double release_at = 0.0;
	bool has_caught = false;
	double caught_at = 0.0;
	Vec3 move;
	double until = 0.0;
};

class AiBrain {
public:
	CombatWorld* w = nullptr;
	ActorState* me = nullptr;
	Dict cfg;                   // aggression counter reaction elements drill interval (+ preset, range_lo/hi ...)
	ActorIntent intent;
	Rng rng;
	std::string debug_state;
	// ---- planner mode (docs/AI.md)
	bool planner = false;
	Dict prm;                   // preset row (+ overrides)
	AiKit kit;                  // {element: [subs]}
	Dict last_plan;             // last counter decision (debug overlay, Lab, tests)

	AiBrain(CombatWorld& world, ActorState& actor, const Dict& config = Dict(), uint64_t seed_value = 3);
	const ActorIntent& think(double dt);
	void configure(const Dict& opts);
	Dict describe() const;
	AiParams _planner_params();
	std::string matrix_next() const;
	static std::vector<std::string> preset_names();

	// ---- legacy rules (ai_brain.gd) - public for tests
	bool _draw_sets_in_time(MatBody& b);
	double _busy_time() const;

private:
	friend struct AiTestAccess;   // CoreTests read / poke the brain's state (GDScript has no private members)
	double t_ = 0.0;
	std::map<std::string, double> seen_;        // threat key -> time first perceived
	std::map<std::string, std::string> decided_;
	int pour_body_ = -1;
	double pour_seen_ = 0.0;
	double next_attack_ = 2.0;
	double strafe_ = 1.0;
	double strafe_t_ = 0.0;
	std::string hold_;           // tech | guard | attack while holding
	double hold_until_ = 0.0;
	int hold_body_ = -1;
	double hold_start_ = 0.0;
	bool hold_started_ = false;
	ActionRef hold_from_;
	bool hold_draw_ = false;
	int guard_attack_ = 0;
	double detour_ = 1.0;
	double detour_t_ = 0.0;
	int await_draw_ = -1;
	std::string pending_press_;
	double guard_at_ = -1.0;
	AiPlan plan_;
	int pending_gesture_ = 0;
	int drill_i_ = 0;
	std::vector<std::pair<std::string, std::vector<AiPlanner::KitMove>>> matrix_;
	Dict recent_;                // move id -> recent use weight
	Vec3 aim_hold_;

	bool cfg_has_element(int e) const;
	double cfg_num(const char* k, double def = 0.0) const { return dnum(cfg, k, def); }
	std::string cfg_drill() const { return dstr(cfg, "drill", ""); }

	bool _react(ActorState* foe, bool stamp_only = false);
	void _continue_holds(ActorState* foe);
	void _press(const std::string& what);
	bool _switch_then(int element, const std::string& what);
	bool _react_to_threats(ActorState* foe, bool stamp_only);
	std::string _choose_wave_response(MatBody& b, double dist);
	std::string _choose_projectile_response(MatBody& b, double tti);
	bool _act_on(const std::string& decision, MatBody& b, double tti = 1.0);
	void _start_draw(MatBody& b);
	void _hold_tech(int body_id, double secs);
	bool _opportunities(ActorState& foe);
	void _offense(ActorState& foe);
	MatBody* _nearest_loose(double r);
	MatBody* _nearby_rock();
	void _move_tactical(ActorState* foe, double amount);
	bool _needs_water() const;
	double _pool_distance() const;
	Vec3 _around_pool(Vec3 want);
	bool _wet(Vec3 v, double reach) const;
	int _turn_steps(Vec3 v, double sense, double reach) const;
	// planner
	void _set_ranges();
	bool _react_planner(ActorState* foe, bool stamp_only);
	bool _perceived(const std::string& key);
	bool _decide(const AiThreat& th);
	void _adopt(const AiOption& pick, const AiThreat* th = nullptr);
	void _end_plan();
	void _exec_plan(ActorState* foe);
	void _press_plan(AiPlan& p, ActorState* foe);
	void _aim_plan(AiPlan& p, ActorState* foe);
	void _tech_stage(AiPlan& p, ActorState* foe);
	void _offense_planner(ActorState& foe);
	bool _chain(ActorState* foe);
	bool _interrupt(ActorState* foe);
	std::vector<AiPlanner::KitMove> _drill_moves(int e, int s);
	void _drill_element(ActorState& foe, int e, int s);
	void _fire_drill_move(const AiPlanner::KitMove& c, ActorState& foe);
	void _build_matrix();
	void _drill_matrix(ActorState& foe);
};

}  // namespace ff
