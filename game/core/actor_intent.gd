class_name ActorIntent
extends RefCounted
## What an actor wants to do this tick, in world space. Produced by the player
## controller (from InputFrame + camera) or by the AI brain; both drive the same rules.

var move := Vector3.ZERO          # world XZ, length 0..1
var attack_pressed := false
var attack_held := false
var attack_released := false
var guard_pressed := false
var guard_held := false
var evade_pressed := false
var tech_pressed := false
var tech_held := false
var tech_released := false
var tech_cancel := false
## Aim direction in world XZ (unit) when aim_active; otherwise the rules auto-aim
## at the lock target / facing.
var aim_dir := Vector3.ZERO
var aim_active := false
var element_select := -1
var target_cycle := false


func clear() -> void:
	move = Vector3.ZERO
	attack_pressed = false
	attack_held = false
	attack_released = false
	guard_pressed = false
	guard_held = false
	evade_pressed = false
	tech_pressed = false
	tech_held = false
	tech_released = false
	tech_cancel = false
	aim_dir = Vector3.ZERO
	aim_active = false
	element_select = -1
	target_cycle = false
