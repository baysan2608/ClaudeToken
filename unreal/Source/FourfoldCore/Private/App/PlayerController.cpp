// Fourfold core - port of game/actors/player_controller.gd.
#include "App/PlayerController.h"

#include "Util/GodotMath.h"

#include <cmath>

namespace ff {

const ActorIntent& PlayerController::build(const InputFrame& f, double cam_yaw) {
	intent.clear();
	const Vec3 fwd = V3(std::sin(cam_yaw), 0.0, std::cos(cam_yaw));
	const Vec3 right = fwd.cross(Vec3::Up());
	const Vec2 m = f.move.limit_length(1.0f);
	intent.move = right * m.x + fwd * m.y;
	intent.attack_pressed = f.attack_pressed;
	intent.attack_held = f.attack_held;
	intent.attack_released = f.attack_released;
	intent.guard_pressed = f.guard_pressed;
	intent.guard_held = f.guard_held;
	intent.evade_pressed = f.evade_pressed;
	intent.tech_pressed = f.tech_pressed;
	intent.tech_held = f.tech_held;
	intent.tech_released = f.tech_released;
	intent.tech_cancel = f.tech_cancel;
	// The release tick still carries the final aim (InputFrame contract) and is the commit.
	intent.aim_active = f.tech_aim_active && (f.tech_held || f.tech_released);
	if (intent.aim_active) {
		const Vec2 a = f.tech_aim;
		const Vec3 d = right * a.x + fwd * std::max(a.y, -0.2f);
		intent.aim_dir = d.length() > 0.05f ? d.normalized() : fwd;
	}
	intent.element_select = f.element_select;
	intent.target_cycle = f.target_cycle;
	intent.sub_select = f.sub_select;
	intent.attack_gesture = static_cast<int>(f.attack_gesture);
	intent.guard_gesture = static_cast<int>(f.guard_gesture);
	intent.evade_held = f.evade_held;
	return intent;
}

InputFrame PlayerController::frame_from_intent(const ActorIntent& it, double cam_yaw) {
	InputFrame f;
	const Vec3 fwd = V3(std::sin(cam_yaw), 0.0, std::cos(cam_yaw));
	const Vec3 right = fwd.cross(Vec3::Up());
	f.move = Vec2(it.move.dot(right), it.move.dot(fwd)).limit_length(1.0f);
	f.attack_pressed = it.attack_pressed;
	f.attack_held = it.attack_held;
	f.attack_released = it.attack_released;
	f.guard_pressed = it.guard_pressed;
	f.guard_held = it.guard_held;
	f.guard_released = false;
	f.evade_pressed = it.evade_pressed;
	f.evade_held = it.evade_held;
	f.tech_pressed = it.tech_pressed;
	f.tech_held = it.tech_held;
	f.tech_released = it.tech_released;
	f.tech_cancel = it.tech_cancel;
	f.element_select = it.element_select;
	f.sub_select = it.sub_select;
	f.attack_gesture = static_cast<Gesture>(it.attack_gesture);
	f.guard_gesture = static_cast<Gesture>(it.guard_gesture);
	f.target_cycle = it.target_cycle;
	f.tech_aim_active = it.aim_active;
	if (it.aim_active) f.tech_aim = Vec2(it.aim_dir.dot(right), std::max(it.aim_dir.dot(fwd), -0.2f)).limit_length(1.0f);
	return f;
}

}  // namespace ff
