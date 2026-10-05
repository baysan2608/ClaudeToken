class_name AnimRigSettings
extends RefCounted
## Global switches for the procedural animation layers (FighterAnimRig). Toggled by the
## animation lab (tests/anim/anim_lab.tscn) and, in the game, by F5-F10 when started with
## `-- --animdebug` (AnimDebugHotkeys). Defaults are the shipping configuration.

static var loco_blend := true      # synced gait blend space (else the AnimationPlayer's single clip)
static var foot_ik := true         # ground the feet on steps/ledges/pool floor + pelvis drop
static var look_at := true         # head/chest look toward the lock target or an incoming threat
static var hit_springs := true     # additive spring-damper hit and block reactions
static var lean := true            # acceleration lean, turn banking, landing compression
static var secondary := true       # SpringBoneSimulator3D chains (sash tails, hair) when present
static var debug_draw := false     # foot targets / look target markers
static var force_lod := -1         # >= 0 forces a detail level on every fighter


static func summary() -> String:
	return "blend %s  ik %s  look %s  hits %s  lean %s  2nd %s  lod %s" % [
		_on(loco_blend), _on(foot_ik), _on(look_at), _on(hit_springs), _on(lean), _on(secondary),
		"auto" if force_lod < 0 else str(force_lod)]


static func _on(b: bool) -> String:
	return "on" if b else "off"
