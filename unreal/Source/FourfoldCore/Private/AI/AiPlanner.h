// Fourfold core - port of game/actors/ai_planner.gd: matrix-driven counter planner and offense chooser of the sparring
// AI (docs/AI.md, MOVESET §13). Pure functions over observable state: never steps the world, never reads the rival's
// intent; every prediction goes through Interactions.predict. Generic over the move registry (def counter / threat /
// ai metadata). GDScript dictionaries become typed structs here (AiThreat, AiOption, AiParams, AiObserve).
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/Agent.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;
class Rng;

// Kit: {element: [subs]} in insertion order (GDScript dictionary order matters for kit_moves).
using AiKit = std::vector<std::pair<int, std::vector<int>>>;
bool ai_kit_has(const AiKit& k, int element);
std::vector<int>* ai_kit_subs(AiKit& k, int element);

// Planner parameters (an AiPresets row + cfg overrides + callables; GDScript _planner_params()).
struct AiParams {
	double reaction = 0.3, counter = 0.75, misjudge = 0.0, timing_err = 0.07, aggression = 0.55;
	int tiers = 1, chain = 1;
	bool env = false, punish = false, weave = false;
	std::function<bool(MatBody&)> draw_ok;   // legacy heat-draw estimate (AiBrain._draw_sets_in_time)
	Dict recent;                             // move id -> recent use weight
	std::vector<std::string> only;           // restrict counters to these ids (tests)
	void merge(const Dict& d);               // prm.merge(extra, true)
};

struct AiThreat {
	bool valid = false;
	std::string key, kind, cls, move;        // kind: body | volume
	MatBody* body = nullptr;
	AgentRef agent;
	double dist = 0.0, tti = 9.0, closing = 0.0;
	bool charging = false;
	int attack_id = 0, owner = -1, tier = 0;
};

struct AiOption {
	bool valid = false;
	std::string id;
	int element = 0, sub = 0;
	std::string slot, press;
	int tier = 0;
	double hold = 0.0;
	bool perfect = false;
	double p = 1.0;
	std::string outcome, band;
	double ratio = 0.0, value = 0.0, utility = 0.0;
	std::string kind;
	double press_in = 0.0, release_in = 0.0, guard_for = 0.0;
	int gesture = 0;
	std::string label, mode;
	// offense
	double score = 0.0;
	std::vector<std::string> reasons;
	int aim_body = -1;
	Vec3 aim;
	bool weave = false;
	bool has_reason(const std::string& r) const;
	Dict to_dict() const;
};

struct AiObserve {
	double dist = 0.0;
	Vec3 dir;
	bool wet = false, in_water = false, on_plate = false, airborne = false, charging = false, recovering = false;
	bool behind_barrier = false, cover = false, hidden = false, near_wall = false, puddle = false;
	int wall_body = -1, charge_tier = 0;
};

namespace AiPlanner {

extern const char* const COUNTER_SLOTS[9];
extern const char* const OFFENSE_SLOTS[5];
inline constexpr double PERFECT_LEAD = 0.09;
inline constexpr double EVADE_VALUE = 0.25;
inline constexpr double ACTIVE_PAD = 0.06;

double outcome_value(const std::string& outcome, const std::string& band, const Dict& rule = Dict());
double perfect_chance(double err);
AiThreat body_threat(CombatWorld& w, ActorState& me, MatBody& b);
AiThreat action_threat(CombatWorld& w, ActorState& me, ActorState* foe);
AgentRef perceived(const Agent& agent, double err);

struct KitMove {
	std::string id;
	int element = 0, sub = 0;
	std::string slot;
};
std::vector<KitMove> kit_moves(const AiKit& kit, const std::vector<std::string>& slots);
std::vector<std::string> counter_slots();
std::vector<std::string> offense_slots();
std::vector<std::string> attack_slots();

double busy_time(const ActorState& me, const std::string& press);
double hold_for(const Dict& def, int tier);
// {focus, heat, water, metal}
struct Cost {
	double focus = 0.0, heat = 0.0, water = 0.0, metal = 0.0;
};
Cost move_cost(const Dict& def, int tier, double hold);
bool can_afford(CombatWorld& w, const ActorState& me, const Cost& cost);
double reach_of(const Dict& def, int tier, const std::string& kind = "");
std::string meet_kind(const Dict& def, const std::string& slot);

AgentRef counter_agent(CombatWorld& w, ActorState& me, const KitMove& c, int tier, bool perfect, const AiThreat& threat);
std::string tech_mode(CombatWorld& w, const ActorState& me, const KitMove& c, const AiThreat& threat);
bool tech_legal(CombatWorld& w, ActorState& me, const KitMove& c, const AiThreat& threat, const Agent& ag);
std::vector<AiOption> counters(CombatWorld& w, ActorState& me, const AiThreat& threat, const AiKit& kit, const AiParams& prm, double err);
AiOption _evaluate(CombatWorld& w, ActorState& me, const AiThreat& threat, const Agent& ag, const KitMove& c, const Dict& def, int tier,
                   bool perfect, const AiParams& prm);
AiOption choose(const std::vector<AiOption>& options, const AiParams& prm, Rng& rng);

AiObserve observe(CombatWorld& w, const ActorState& me, const ActorState& foe);
std::vector<AiOption> offense(CombatWorld& w, ActorState& me, ActorState& foe, const AiKit& kit, const AiParams& prm, Rng& rng);
AiOption chain_follow(CombatWorld& w, ActorState& me, ActorState& foe, const AiKit& kit, const AiParams& prm, Rng& rng);
Vec3 bank_aim(CombatWorld& w, const ActorState& me, const ActorState& foe, double max_range);
bool _kit_has_tag(const AiKit& kit, const std::string& tag);

}  // namespace AiPlanner
}  // namespace ff
