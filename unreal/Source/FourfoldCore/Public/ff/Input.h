// Fourfold core - device-agnostic player input for ONE 60 Hz tick (port of game/ui/input_frame.gd).
// FROZEN CONTRACT (architect). Owner after creation: stream `core` (additive only).
// Producers (stream `game`): touch overlay (Slate) and keyboard / gamepad. Edge flags (*_pressed / *_released) are
// LATCHED by the producer until a tick consumes them, so a tap shorter than one tick is never lost. Semantics:
// docs/CONTROLS.md "InputFrame semantics" (a tap may carry pressed AND released with held == false in one frame;
// tech_released = commit, tech_cancel = abort; on the release tick tech_aim still holds the final aim, ...).
#pragma once

#include "ff/Config.h"
#include "ff/Math.h"
#include "ff/Types.h"

namespace ff {

struct InputFrame {
	Vec2 move;                    // stick, screen space: x right, y up/forward; length <= 1, dead zone removed
	Vec2 cam_delta;               // camera orbit this tick, radians: x yaw right+, y pitch up+ (consumed by the camera, not the sim)

	bool attack_pressed = false, attack_held = false, attack_released = false;
	bool guard_pressed = false, guard_held = false, guard_released = false;
	bool evade_pressed = false;
	bool evade_held = false;      // level: >= 0.2 s morphs the evade into evade_hold

	bool tech_pressed = false, tech_held = false, tech_released = false;
	Vec2 tech_aim;                // drag relative to the press point, normalised (|v| <= 1), x right, y away (screen)
	bool tech_aim_active = false; // drag passed the aim threshold
	bool tech_cancel = false;

	int element_select = -1;      // -1 none, else 0..3 (only unlocked elements)
	int sub_select = -1;          // -1 none, else 0..3 of the current element (edge)
	Gesture attack_gesture = Gesture::None;   // flick recognised this tick
	Gesture guard_gesture = Gesture::None;    // Up = push, Down = sink (while guarding)
	bool target_cycle = false;
	bool pause_pressed = false;

	void ClearEdges() {
		attack_pressed = attack_released = false;
		guard_pressed = guard_released = false;
		evade_pressed = false;
		tech_pressed = tech_released = tech_cancel = false;
		element_select = -1;
		sub_select = -1;
		target_cycle = false;
		pause_pressed = false;
		cam_delta = Vec2{};
		attack_gesture = Gesture::None;
		guard_gesture = Gesture::None;
	}

	// Merge another device's frame (touch + pad at once): move / camera add, buttons OR, stronger aim wins.
	void MergeFrom(const InputFrame& o) {
		move = (move + o.move).limit_length(1.0f);
		cam_delta += o.cam_delta;
		attack_pressed = attack_pressed || o.attack_pressed;
		attack_held = attack_held || o.attack_held;
		attack_released = attack_released || o.attack_released;
		guard_pressed = guard_pressed || o.guard_pressed;
		guard_held = guard_held || o.guard_held;
		guard_released = guard_released || o.guard_released;
		evade_pressed = evade_pressed || o.evade_pressed;
		tech_pressed = tech_pressed || o.tech_pressed;
		tech_held = tech_held || o.tech_held;
		tech_released = tech_released || o.tech_released;
		tech_cancel = tech_cancel || o.tech_cancel;
		if (o.tech_aim_active && (!tech_aim_active || o.tech_aim.length_squared() > tech_aim.length_squared()))
			tech_aim = o.tech_aim;
		tech_aim_active = tech_aim_active || o.tech_aim_active;
		if (element_select < 0) element_select = o.element_select;
		target_cycle = target_cycle || o.target_cycle;
		pause_pressed = pause_pressed || o.pause_pressed;
		if (sub_select < 0) sub_select = o.sub_select;
		if (attack_gesture == Gesture::None) attack_gesture = o.attack_gesture;
		if (guard_gesture == Gesture::None) guard_gesture = o.guard_gesture;
		evade_held = evade_held || o.evade_held;
	}
};

}  // namespace ff
