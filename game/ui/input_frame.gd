class_name InputFrame
extends RefCounted
## Device-agnostic player input for ONE simulation tick (60 Hz).
## Produced by TouchControls / DesktopInput, consumed by PlayerController.
## Edge flags (*_pressed / *_released) are latched by the producer until a
## tick consumes them, so a tap shorter than one tick is never lost.

## Movement stick, screen space: x = right, y = up/forward. Length 0..1.
var move := Vector2.ZERO
## Camera orbit delta for this tick, radians: x = yaw (right +), y = pitch (up +).
var cam_delta := Vector2.ZERO

var attack_pressed := false
var attack_held := false
var attack_released := false

var guard_pressed := false
var guard_held := false
var guard_released := false

var evade_pressed := false

## Technique: hold to acquire/shape, drag to aim, release to commit.
var tech_pressed := false
var tech_held := false
var tech_released := false
## Aim drag relative to the press point, normalised (-1..1 each axis, length <= 1).
## x = right, y = up/away from the player (screen space).
var tech_aim := Vector2.ZERO
## True once the drag passed the aim threshold (the aim is intentional).
var tech_aim_active := false
## Explicit cancel (drag onto the cancel zone, or cancel button / gamepad B).
var tech_cancel := false

## -1 = no change, else Elements.EARTH/WATER/FIRE/AIR.
var element_select := -1
var target_cycle := false
var pause_pressed := false


func clear_edges() -> void:
	attack_pressed = false
	attack_released = false
	guard_pressed = false
	guard_released = false
	evade_pressed = false
	tech_pressed = false
	tech_released = false
	tech_cancel = false
	element_select = -1
	target_cycle = false
	pause_pressed = false
	cam_delta = Vector2.ZERO


func copy_from(o: InputFrame) -> void:
	move = o.move
	cam_delta = o.cam_delta
	attack_pressed = o.attack_pressed
	attack_held = o.attack_held
	attack_released = o.attack_released
	guard_pressed = o.guard_pressed
	guard_held = o.guard_held
	guard_released = o.guard_released
	evade_pressed = o.evade_pressed
	tech_pressed = o.tech_pressed
	tech_held = o.tech_held
	tech_released = o.tech_released
	tech_aim = o.tech_aim
	tech_aim_active = o.tech_aim_active
	tech_cancel = o.tech_cancel
	element_select = o.element_select
	target_cycle = o.target_cycle
	pause_pressed = o.pause_pressed


## Merge another device's frame into this one (touch + gamepad/keyboard at
## once): movement/camera add, buttons OR, the stronger technique aim wins.
func merge_from(o: InputFrame) -> void:
	move = (move + o.move).limit_length(1.0)
	cam_delta += o.cam_delta
	attack_pressed = attack_pressed or o.attack_pressed
	attack_held = attack_held or o.attack_held
	attack_released = attack_released or o.attack_released
	guard_pressed = guard_pressed or o.guard_pressed
	guard_held = guard_held or o.guard_held
	guard_released = guard_released or o.guard_released
	evade_pressed = evade_pressed or o.evade_pressed
	tech_pressed = tech_pressed or o.tech_pressed
	tech_held = tech_held or o.tech_held
	tech_released = tech_released or o.tech_released
	tech_cancel = tech_cancel or o.tech_cancel
	if o.tech_aim_active and (not tech_aim_active or o.tech_aim.length_squared() > tech_aim.length_squared()):
		tech_aim = o.tech_aim
	tech_aim_active = tech_aim_active or o.tech_aim_active
	if element_select < 0:
		element_select = o.element_select
	target_cycle = target_cycle or o.target_cycle
	pause_pressed = pause_pressed or o.pause_pressed
