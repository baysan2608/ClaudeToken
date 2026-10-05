class_name AnimRigSettings
extends RefCounted
## Global switches for the procedural animation layers (FighterAnimRig). Toggled by the
## animation lab (tests/anim/anim_lab.tscn) and, in the game, by F5-F10 when started with
## `-- --animdebug` (AnimDebugHotkeys). Defaults are the shipping configuration.

static var loco_blend := true      # synced gait blend space (else the AnimationPlayer's single clip)
static var foot_ik := true         # ground the feet on steps/ledges/pool floor + pelvis drop
static var foot_lock := true      # planted feet hold their world position until they lift
static var look_at := true         # head/chest look toward the lock target or an incoming threat
static var hit_springs := true     # additive spring-damper hit and block reactions
static var lean := true            # acceleration lean, turn banking, landing compression
static var secondary := true       # SpringBoneSimulator3D chains (sash tails, hair) when present
static var debug_draw := false     # foot targets / look target markers
static var record_probes := false   # tests/lab: the rig records world positions of key bones
static var force_lod := -1         # >= 0 forces a detail level on every fighter


## Every procedural layer off: the plain AnimationPlayer clips (the pre-runtime baseline).
static func all_off() -> void:
	loco_blend = false
	foot_ik = false
	look_at = false
	hit_springs = false
	lean = false
	secondary = false


## The shipping configuration (tests restore it after each case).
static func reset_defaults() -> void:
	loco_blend = true
	foot_ik = true
	foot_lock = true
	look_at = true
	hit_springs = true
	lean = true
	secondary = true
	debug_draw = false
	record_probes = false
	force_lod = -1


static func summary() -> String:
	return "blend %s  ik %s  look %s  hits %s  lean %s  2nd %s  lod %s" % [
		_on(loco_blend), _on(foot_ik), _on(look_at), _on(hit_springs), _on(lean), _on(secondary),
		"auto" if force_lod < 0 else str(force_lod)]


static func _on(b: bool) -> String:
	return "on" if b else "off"
