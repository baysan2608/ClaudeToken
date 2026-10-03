class_name PlayerController
extends RefCounted
## Maps one InputFrame (screen space) + camera yaw to a world-space ActorIntent.

var intent := ActorIntent.new()


## cam_yaw: camera looks along (sin(yaw), 0, cos(yaw)) on the ground plane.
func build(f: InputFrame, cam_yaw: float) -> ActorIntent:
	intent.clear()
	var fwd := Vector3(sin(cam_yaw), 0.0, cos(cam_yaw))
	var right := fwd.cross(Vector3.UP)
	var m := f.move.limit_length(1.0)
	intent.move = right * m.x + fwd * m.y
	intent.attack_pressed = f.attack_pressed
	intent.attack_held = f.attack_held
	intent.attack_released = f.attack_released
	intent.guard_pressed = f.guard_pressed
	intent.guard_held = f.guard_held
	intent.evade_pressed = f.evade_pressed
	intent.tech_pressed = f.tech_pressed
	intent.tech_held = f.tech_held
	intent.tech_released = f.tech_released
	intent.tech_cancel = f.tech_cancel
	intent.aim_active = f.tech_aim_active and f.tech_held
	if intent.aim_active:
		var a := f.tech_aim
		var d := right * a.x + fwd * maxf(a.y, -0.2)
		intent.aim_dir = d.normalized() if d.length() > 0.05 else fwd
	intent.element_select = f.element_select
	intent.target_cycle = f.target_cycle
	return intent
