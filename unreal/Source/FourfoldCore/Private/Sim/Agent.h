// Fourfold core - port of game/core/agent.gd: one side of an interaction (MOVESET §15.3): a threat (body, volume,
// hit) or a counter (barrier body, guard, move volume, stance, environment). The Interactions engine reads `cls`
// (threat role), `ccls` (counter role), the power channels `ch` and the counter power.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"

#include <memory>
#include <string>
#include <string_view>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;
struct ActionInst;

// Power channels in PU: K kinetic, H heat, C cold, E electric, P pressure / sonic.
struct Channels {
	double K = 0.0, H = 0.0, C = 0.0, E = 0.0, P = 0.0;
	double get(std::string_view k, double def = 0.0) const {
		if (k == "K") return K;
		if (k == "H") return H;
		if (k == "C") return C;
		if (k == "E") return E;
		if (k == "P") return P;
		return def;
	}
	double* slot(std::string_view k) {
		if (k == "K") return &K;
		if (k == "H") return &H;
		if (k == "C") return &C;
		if (k == "E") return &E;
		if (k == "P") return &P;
		return nullptr;
	}
	void set(std::string_view k, double v) {
		if (double* s = slot(k)) *s = v;
	}
};

class Agent;
using AgentRef = std::shared_ptr<Agent>;

class Agent {
public:
	std::string kind;           // body | volume | guard | move | env | stance
	std::string cls;            // threat class
	std::string ccls;           // counter class
	MatBody* body = nullptr;
	ActorState* actor = nullptr;   // threat: attacker / owner; counter: the defender
	ActionInst* inst = nullptr;
	Dict def;                   // move def (volumes, moves) or guard spec def
	Vec3 pos;
	Vec3 dir;
	Channels ch;
	double mass = 0.0;
	double speed = 0.0;
	double heat = 0.0;          // HU above ambient (volumes: heat budget still to spend)
	int tier = 0;
	bool perfect = false;
	double power = -1.0;        // explicit counter power (PU); < 0 = derived
	int mat = -1;
	bool hostile = true;
	Dict data;                  // site extras (knock ...)

	static AgentRef of_body(CombatWorld& w, MatBody& b, const ActorState* against = nullptr);
	// channels: K H C E P (+ heat_hu = heat budget).
	static AgentRef of_volume(CombatWorld* w, ActorState* a, ActionInst* inst, const std::string& cls, Vec3 pos, Vec3 dir,
	                          const Dict& channels);
	static AgentRef of_hit(CombatWorld& w, const Dict& info, const AgentRef& agent = nullptr);
	static AgentRef of_guard(CombatWorld& w, ActorState& a);
	static AgentRef of_move(CombatWorld* w, ActorState* a, const std::string& move_id, int tier, bool perfect);
	static AgentRef of_env(CombatWorld* w, const std::string& kind, Vec3 pos, ActorState* a = nullptr);
	static AgentRef of_stance(CombatWorld* w, ActorState& a);
	static int def_mat(const Dict& def);

	double total() const { return ch.K + ch.H + ch.C + ch.E + ch.P; }
};

}  // namespace ff
