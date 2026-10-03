class_name VfxEffect
extends Node3D
## Base class of the one-shot / looping VFX nodes. Gives every effect the same pooling and timing
## contract:
##   reset()      return to the idle, invisible state (called by VfxPool on release/acquire)
##   advance(dt)  step the effect; called from _process unless manual_time is set
##   finished     emitted once when a one-shot effect has fully played out
## `manual_time = true` stops the node stepping itself so tests / replays can drive advance(dt).

signal finished(effect: VfxEffect)

var manual_time: bool = false
## Set by VfxPool when the effect was handed out by a pool; finished effects release themselves.
var pool: VfxPool = null


func reset() -> void:
	visible = false
	set_process(false)


## Called by VfxPool.get_fx(). One-shot effects stay hidden until play()/strike()/... is called;
## persistent views (WaterBlobView, EarthWallView) override this to become visible.
func on_acquire() -> void:
	pass


func is_playing() -> bool:
	return false


func advance(_dt: float) -> void:
	pass


func _process(delta: float) -> void:
	if not manual_time:
		advance(delta)


## Set the world transform whether or not the node is in the tree yet.
func _place(xform: Transform3D) -> void:
	if is_inside_tree():
		global_transform = xform
	else:
		transform = xform


func _place_at(world_pos: Vector3) -> void:
	if is_inside_tree():
		global_position = world_pos
	else:
		position = world_pos


func _finish() -> void:
	set_process(false)
	finished.emit(self)
	if pool != null:
		pool.release(self)
	else:
		visible = false
