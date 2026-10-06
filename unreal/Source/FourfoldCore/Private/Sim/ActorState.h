// Fourfold core - ports of game/core/actor_state.gd, actor_intent.gd and action_inst.gd.
#pragma once

#include "ff/Math.h"
#include "ff/Types.h"
#include "ff/Value.h"
#include "Sim/Sim.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace ff {

// What an actor wants this tick, in world space (PlayerController or AiBrain).
struct ActorIntent {
	Vec3 move;                     // world XZ, length 0..1
	bool attack_pressed = false, attack_held = false, attack_released = false;
	bool guard_pressed = false, guard_held = false;
	bool evade_pressed = false;
	bool tech_pressed = false, tech_held = false, tech_released = false, tech_cancel = false;
	Vec3 aim_dir;                  // world XZ unit when aim_active
	bool aim_active = false;
	int element_select = -1;
	bool target_cycle = false;
	int sub_select = -1;
	int attack_gesture = 0;        // Gesture
	int guard_gesture = 0;
	bool evade_held = false;

	void clear() { *this = ActorIntent(); }
};

// A running action (move instance). Phases STARTUP -> ACTIVE -> RECOVERY -> DONE; CHARGE / CHANNEL are holds.
struct ActionInst {
	std::string id;                // Moves.DEFS key
	Dict def;                      // shared handle into Moves.DEFS (Lab tuning edits it in place)
	int element = 0;
	ActionPhase phase = ActionPhase::Startup;
	double t = 0.0;                // time in the current phase
	double total = 0.0;            // time since start
	int attack_id = 0;
	bool heavy = false;
	Dict data;                     // per-action scratch (targets, mode, aim, tier, slot, spec ...)
	bool interrupted = false;
	int sub = 0;
	std::string slot;

	int tier() const;
	const char* phase_name() const { return kActionPhaseNames[static_cast<int>(phase)].data(); }
};
using ActionRef = std::shared_ptr<ActionInst>;

// Authoritative state of one fighter (the sim is the only movement authority).
class ActorState {
public:
	int id = 0;
	std::string name;
	int team = 0;
	bool is_dummy = false;

	Vec3 pos;                      // feet
	Vec3 vel;
	double facing = 0.0;           // yaw radians; forward = (sin f, 0, cos f)
	bool grounded = true;
	double ground_y = 0.0;
	bool in_water = false;
	std::string surface = "stone";
	bool gliding = false;

	double health = Sim::HEALTH_MAX;
	double balance = Sim::BALANCE_MAX;
	double focus = Sim::FOCUS_MAX;
	double heat_reserve = 0.0;
	double wetness = 0.0;
	double water_carried = 6.0;
	double focus_idle = 0.0;
	double balance_idle = 0.0;

	int element = 0;
	std::array<bool, 4> elements{{true, true, true, true}};
	Dict kit;                      // unlocked techniques: magma, heat_draw, lightning, redirect_current, glide ...
	double max_control_mass = 80.0;

	ActionRef action;
	double stun = 0.0;
	std::string stun_kind;         // light heavy knockdown getup guard_break bound
	double iframes = 0.0;
	bool guarding = false;
	int64_t guard_tick = -1000;
	int64_t guard_press_tick = -1000;
	int held_body = -1;
	int lock_target = -1;
	std::unordered_map<int, int64_t> hits_taken;   // attack_id -> tick
	double burn_cd = 0.0;
	int wall_body = -1;

	std::string buffered;          // attack evade tech guard
	int64_t buffered_tick = -1000;
	std::string buffered_slot;
	double attack_hold = 0.0;

	Vec3 last_hit_dir;
	std::string last_result;

	std::array<int, 4> subs{{0, 0, 0, 0}};
	std::array<std::array<bool, 4>, 4> subs_unlocked{{{{true, true, true, true}}, {{true, true, true, true}},
	                                                   {{true, true, true, true}}, {{true, true, true, true}}}};
	Dict status;                   // name -> {t, mag, src}
	double metal_carried = 12.0;
	double static_charge = 0.0;
	std::string stance;
	double armor = 0.0;
	bool anchored = false;
	bool flying = false;
	Dict chain;                    // {n, slots, weaved, pending?, counter_cancel?}

	ActorState();

	int sub() const { return subs[static_cast<size_t>(element < 0 ? 0 : (element > 3 ? 3 : element))]; }
	int sub_of(int e) const { return subs[static_cast<size_t>(e < 0 ? 0 : (e > 3 ? 3 : e))]; }
	Vec3 forward() const { return Vec3(static_cast<float>(std::sin(facing)), 0.0f, static_cast<float>(std::cos(facing))); }
	bool has(std::string_view tech) const { return kit.get(tech, Value(false)).truthy(); }
	bool busy() const { return action != nullptr || stun > 0.0; }
	Vec3 chest() const { return pos + Vec3(0.0f, 1.25f, 0.0f); }
	Vec3 hand_point() const { return pos + forward() * 0.55f + Vec3(0.0f, 1.2f, 0.0f); }
};

}  // namespace ff
