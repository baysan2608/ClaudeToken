class_name FighterView
extends Node3D
## Presentation of one fighter. The simulation is the only movement authority:
## this node interpolates position/facing and picks animation clips. Clips are
## in-place; playback speed is matched to ground speed (no skating) and attack
## clips are time-scaled so their authored contact frame lands on the sim's
## startup end (game/assets/characters/fighter_clips.json).
##
## On top of the clip the AnimationPlayer plays, a FighterAnimRig (SkeletonModifier3D, see
## presentation/anim/) adds the physical layer: a synced locomotion blend space driven by the
## measured ground velocity, foot IK on the ArenaMap ground with pelvis drop, lean/banking and
## landing compression, clamped look-at / chest aim, and spring-damper hit and block reactions.
## SpringBoneSimulator3D chains (sash tails, hair) are added when the rig has those bones.
## Distant fighters and practice dummies run reduced layers (docs/ANIMATION.md, "Runtime layers").

const GLB := "res://assets/characters/fighter.glb"
const CLIPS_JSON := "res://assets/characters/fighter_clips.json"
const WALK_SPEED := 1.4
const RUN_SPEED := 5.5
const STRAFE_SPEED := 1.1
const BACK_SPEED := 1.0
## Stand-ins for clips an older fighter.glb may lack (generic moveset clips -> closest legacy clip).
const FALLBACK_CLIPS := {
	"mv_push_two_hand": "air_push", "mv_uppercut_lift": "earth_lift", "mv_stomp": "earth_wall",
	"mv_sweep_low": "pour", "mv_spin": "air_gust", "mv_palm_thrust": "earth_throw",
	"mv_overhead_slam": "earth_heavy", "mv_wide_draw": "water_draw", "mv_ground_slap": "pour",
	"mv_rising_guard": "deflect", "mv_roundhouse": "water_whip", "mv_front_kick": "fire_jab",
	"land": "idle", "deflect": "guard",
}

var actor_id := -1
var ap: AnimationPlayer
var skel: Skeleton3D
var clips := {}
var model: Node3D
var _cur := ""
var _cur_key := ""
var _one_shot := ""
var _one_shot_t := 0.0
var _one_shot_speed := 1.0
var _one_shot_n := 0
var _yaw_rate_s := 0.0
var _prev_pos := Vector3.ZERO
var _curr_pos := Vector3.ZERO
var _prev_yaw := 0.0
var _curr_yaw := 0.0
var _hand_r := -1
var _hand_l := -1
var _lean := 0.0
var colors := {}
var _fallback_parts := {}

# ---- runtime animation layers (presentation/anim/)
const LOD_NEAR := 22.0                # m to the camera: beyond this, reduced layers
const LOD_FAR := 40.0                 # beyond this, plain clips only (rig off)
var rig: FighterAnimRig = null
var secondary: SpringBoneSimulator3D = null
var is_dummy := false
var lod := 0
var _vel_meas := Vector3.ZERO         # horizontal ground velocity measured from sim positions (m/s)
var _have_meas := false
var _vel_s := Vector3.ZERO
var _acc_s := Vector3.ZERO
var _yaw_rate := 0.0
var _prev_grounded := true
var _prev_vy := 0.0
var _prev_health := -1.0
var _prev_balance := 0.0
var _last_hit_tick := -1
var _vis_y := 0.0
var _vis_init := false
var _debug_draw: AnimDebugDraw = null


func setup(id: int, palette: Dictionary) -> void:
	actor_id = id
	colors = palette
	if ResourceLoader.exists(GLB):
		var scene: PackedScene = load(GLB)
		model = scene.instantiate()
		add_child(model)
		ap = _find(model, "AnimationPlayer") as AnimationPlayer
		skel = _find(model, "Skeleton3D") as Skeleton3D
		_recolor(model)
		if skel:
			_hand_r = skel.find_bone("hand.R")
			_hand_l = skel.find_bone("hand.L")
		if ap:
			ap.playback_default_blend_time = 0.12
		if skel and ap:
			rig = FighterAnimRig.new()
			rig.name = "AnimRig"
			skel.add_child(rig)
			rig.setup_rig(skel, ap)
			if not rig.is_rig_ready():
				rig.queue_free()
				rig = null
			secondary = FighterSecondaryMotion.attach(skel)
	else:
		_build_fallback()
	var args := OS.get_cmdline_user_args()
	if "--animdebug" in args:
		AnimDebugHotkeys.install(self)
	if "--anim=off" in args:
		AnimRigSettings.all_off()
	if FileAccess.file_exists(CLIPS_JSON):
		var j: Variant = JSON.parse_string(FileAccess.get_file_as_string(CLIPS_JSON))
		if j is Dictionary:
			clips = j.get("clips", j)
	# glTF carries no loop flag: apply the authored loop table.
	if ap:
		for c in clips:
			if ap.has_animation(c):
				ap.get_animation(c).loop_mode = Animation.LOOP_LINEAR if clips[c].get("loop", false) else Animation.LOOP_NONE


func _find(n: Node, cls: String) -> Node:
	if n.is_class(cls):
		return n
	for c in n.get_children():
		var r := _find(c, cls)
		if r != null:
			return r
	return null


func _recolor(n: Node) -> void:
	if n is MeshInstance3D:
		var mi := n as MeshInstance3D
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
		for i in mi.mesh.get_surface_count():
			var m := mi.mesh.surface_get_material(i)
			if m == null:
				continue
			var key := m.resource_name
			if colors.has(key) and m is BaseMaterial3D:
				var nm := (m as BaseMaterial3D).duplicate() as BaseMaterial3D
				nm.albedo_color = colors[key]
				nm.roughness = 0.82 if key != "skin" else 0.6
				mi.set_surface_override_material(i, nm)
	for c in n.get_children():
		_recolor(c)


func set_accent(c: Color) -> void:
	colors["cloth_accent"] = c
	if model:
		_recolor(model)
	elif _fallback_parts.has("sash"):
		(_fallback_parts.sash.material_override as StandardMaterial3D).albedo_color = c


func push_state(a: ActorState) -> void:
	## Called once per simulation tick.
	var d := a.pos - _curr_pos
	_prev_pos = _curr_pos
	_prev_yaw = _curr_yaw
	_curr_pos = a.pos
	_curr_yaw = a.facing
	# Measured ground velocity (what the eye sees, so feet never skate against a wall push-out);
	# a jump of more than 12 m/s is a teleport/reset, use the sim velocity then.
	_vel_meas = Vector3(d.x, 0.0, d.z) / Sim.DT
	if _vel_meas.length() > 12.0:
		_vel_meas = Vector3(a.vel.x, 0.0, a.vel.z)
	_have_meas = true
	_yaw_rate = wrapf(_curr_yaw - _prev_yaw, -PI, PI) / Sim.DT
	_react_to_sim(a)


func snap(a: ActorState) -> void:
	_prev_pos = a.pos
	_curr_pos = a.pos
	_prev_yaw = a.facing
	_curr_yaw = a.facing
	global_position = a.pos
	rotation.y = a.facing
	is_dummy = a.is_dummy
	_vel_meas = Vector3.ZERO
	_vel_s = Vector3.ZERO
	_acc_s = Vector3.ZERO
	_yaw_rate = 0.0
	_vis_init = false
	_prev_health = -1.0
	_prev_grounded = a.grounded
	if rig:
		rig.reset_state()


func render(a: ActorState, alpha: float, dt: float) -> void:
	global_position = _prev_pos.lerp(_curr_pos, alpha)
	rotation.y = lerp_angle(_prev_yaw, _curr_yaw, alpha)
	_animate(a, dt)
	if not model:
		_fallback_pose(a, dt)
		return
	_smooth_visual_height(a, dt)
	if rig:
		_drive_rig(a, dt)


## Plays any clip of the library once, over everything but stuns, for `dur` seconds (<= 0: the
## clip's own length). With contact_in >= 0 the clip is time-scaled so its authored contact lands
## that many seconds from now (like the sim-driven attacks). A clip missing from the library falls
## back to a stand-in (FALLBACK_CLIPS, e.g. a new mv_* clip on an older asset); with none, nothing
## changes and false is returned.
func play_one_shot(clip: String, dur: float = 0.35, contact_in: float = -1.0) -> bool:
	var c := resolve_clip(clip)
	if c == "":
		return false
	var speed := 1.0
	if contact_in >= 0.0:
		speed = clampf(_contact(c) / maxf(contact_in, 0.03), 0.5, 2.5)
	_one_shot = c
	_one_shot_speed = speed
	_one_shot_t = dur if dur > 0.0 else _clip_len(c) / speed
	_one_shot_n += 1
	_cur_key = ""
	return true


## The clip that plays for `clip`: itself when the library has it, else its stand-in, else "".
func resolve_clip(clip: String) -> String:
	if _has_clip(clip):
		return clip
	var alt := String(FALLBACK_CLIPS.get(clip, ""))
	if alt != "" and _has_clip(alt):
		return alt
	return ""


func _has_clip(c: String) -> bool:
	if c == "":
		return false
	if ap != null:
		return ap.has_animation(c)
	return clips.has(c)


func hand_position(right: bool = true) -> Vector3:
	# The rig caches the final (layered) hand positions each frame it runs.
	if rig and rig.active and rig.hand_frame >= Engine.get_process_frames() - 1:
		return rig.hand_world[0 if right else 1]
	var idx := _hand_r if right else _hand_l
	if skel and idx >= 0:
		return skel.global_transform * skel.get_bone_global_pose(idx).origin
	return global_position + global_transform.basis * Vector3(0.25 if right else -0.25, 1.2, 0.45)


# ------------------------------------------------------------------ animation selection

func _clip_len(c: String) -> float:
	if clips.has(c):
		return float(clips[c].get("duration", 0.5))
	if ap and ap.has_animation(c):
		return ap.get_animation(c).length
	return 0.5


func _contact(c: String) -> float:
	if clips.has(c) and clips[c].get("contact") != null:
		return float(clips[c].contact)
	return _clip_len(c) * 0.4


func _play(c: String, key: String, speed: float = 1.0, blend: float = 0.12, from_time: float = -1.0) -> void:
	if ap == null:
		_cur = c
		return
	if not ap.has_animation(c):
		var r := resolve_clip(c)
		c = r if r != "" else "idle"
		if not ap.has_animation(c):
			return
	if key == _cur_key and c == _cur:
		ap.speed_scale = speed
		return
	_cur_key = key
	_cur = c
	ap.play(c, blend, 1.0)
	ap.speed_scale = speed
	if from_time >= 0.0:
		ap.seek(from_time, true)


## Plays clip so that its contact frame lands `remaining` seconds from now. While the same clip
## runs, the speed is re-aimed from the clip's actual position (catching up a late start or a long
## frame); past contact, or inside the last tick, the follow-through keeps its speed.
func _play_aligned(c: String, key: String, remaining: float) -> void:
	var contact := _contact(c)
	if ap != null and key == _cur_key and c == _cur:
		var left := contact - ap.current_animation_position
		if left > 0.0 and remaining >= Sim.DT:
			ap.speed_scale = clampf(left / remaining, 0.5, 2.5)
		return
	_play(c, key, clampf(contact / maxf(remaining, 0.03), 0.5, 2.5), 0.06)


func _animate(a: ActorState, dt: float) -> void:
	if a.stun > 0.0:
		_one_shot_t = 0.0
		match a.stun_kind:
			"knockdown":
				_play("knockdown", "kd", 1.0, 0.06)
			"getup":
				_play("getup", "getup", _clip_len("getup") / 0.75, 0.1)
			"heavy":
				_play("hit_heavy", "hh%d" % int(a.stun * 0.0), 1.0, 0.05)
			"guard_break":
				_play("stagger", "gb", 1.0, 0.05)
			_:
				var back := a.last_hit_dir.dot(a.forward()) > 0.3
				_play("hit_back" if back else "hit_front", "hl", 1.0, 0.04)
		return
	if _one_shot_t > 0.0:
		_one_shot_t -= dt
		_play(_one_shot, "oneshot%d:%s" % [_one_shot_n, _one_shot], _one_shot_speed, 0.05)
		return
	var inst := a.action
	if inst != null:
		_animate_action(a, inst)
		return
	if not a.grounded:
		_play("glide" if a.gliding else "fall", "air", 1.0, 0.15)
		return
	var hv := Vector3(a.vel.x, 0, a.vel.z)
	var spd := hv.length()
	if spd < 0.3:
		var stance: String = ["stance_earth", "stance_water", "stance_fire", "stance_air"][a.element]
		_play(stance, "stance%d" % a.element, 1.0, 0.25)
		return
	var dir := hv / spd
	var f := a.forward()
	var fd := dir.dot(f)
	var rd := dir.dot(f.cross(Vector3.UP))   # forward x UP is the character's RIGHT (rig left is +X)
	if a.lock_target >= 0 and fd < 0.55 and spd < 3.0:
		if fd < -0.5:
			_play("walk_back", "wb", clampf(spd / BACK_SPEED, 0.5, 2.0), 0.18)
		elif rd < 0.0:
			_play("strafe_l", "sl", clampf(spd / STRAFE_SPEED, 0.5, 2.0), 0.18)
		else:
			_play("strafe_r", "sr", clampf(spd / STRAFE_SPEED, 0.5, 2.0), 0.18)
		return
	if spd < 2.9:
		_play("walk", "walk", clampf(spd / WALK_SPEED, 0.5, 2.0), 0.2)
	else:
		_play("run", "run", clampf(spd / RUN_SPEED, 0.6, 1.5), 0.2)


## Evade clip from the evade direction relative to the facing it started with, so the clip always
## travels the way the sim moves the fighter (the sim's "side" label is not trusted for l/r).
func _evade_side(a: ActorState, inst: ActionInst) -> String:
	var dir: Vector3 = inst.data.get("dir", Vector3.ZERO)
	var f: Vector3 = inst.data.get("face", a.forward())
	var fd := dir.dot(f)
	var rd := dir.dot(f.cross(Vector3.UP))   # > 0: toward the character's right
	if dir == Vector3.ZERO or absf(fd) >= absf(rd):
		return "fwd" if fd > 0.0 else "back"
	return "r" if rd > 0.0 else "l"


func _animate_action(a: ActorState, inst: ActionInst) -> void:
	var ph := inst.phase
	var P := ActionInst.P
	var key := "%s:%d:%d" % [inst.id, inst.attack_id, ph]
	var su := float(inst.data.get("startup", inst.def.startup))
	match inst.id:
		"evade":
			_play("evade_" + _evade_side(a, inst), "ev%d" % inst.attack_id, _clip_len("evade_l") / 0.42, 0.04)
		"air_dash":
			_play("air_dash", "ad%d" % inst.attack_id, 1.0, 0.04)
		"guard":
			if a.wall_body >= 0 and inst.total < 0.35:
				_play_aligned("earth_wall", "gw%d" % inst.attack_id, 0.14 - inst.total)
			else:
				_play("guard", "guard", 1.0, 0.08)
		"earth_attack":
			if ph == P.STARTUP:
				_play_aligned("earth_lift", key, su - inst.total)
			elif ph == P.CHARGE:
				_play("earth_hold", "eh%d" % inst.attack_id, 1.0, 0.12)
			else:
				var c := "earth_heavy" if inst.heavy else "earth_throw"
				_play(c, "et%d" % inst.attack_id, 1.0, 0.05, maxf(_contact(c) - 0.03, 0.0))
		"earth_tech":
			if ph == P.ACTIVE or ph == P.RECOVERY:
				_play("earth_throw", "et%d" % inst.attack_id, 1.0, 0.05, maxf(_contact("earth_throw") - 0.03, 0.0))
			else:
				_play("earth_hold", "eh%d" % inst.attack_id, 1.0, 0.15)
		"water_attack":
			if ph == P.STARTUP:
				_play_aligned("water_whip", key, su - inst.total)
			elif ph == P.CHARGE:
				_play("water_hold", "wh%d" % inst.attack_id, 1.0, 0.12)
			elif inst.heavy:
				_play("water_freeze", "wf%d" % inst.attack_id, 1.0, 0.05, maxf(_contact("water_freeze") - 0.03, 0.0))
			else:
				_play("water_whip", "ww%d" % inst.attack_id, 1.0, 0.04)
		"water_tech":
			if ph == P.STARTUP:
				_play("water_draw", key, 1.0, 0.08)
			elif ph == P.CHANNEL:
				_play("water_hold", "wh%d" % inst.attack_id, 1.0, 0.2)
			else:
				_play("water_whip", "ww%d" % inst.attack_id, 1.0, 0.04, maxf(_contact("water_whip") - 0.03, 0.0))
		"fire_attack":
			if ph == P.STARTUP:
				_play_aligned("fire_jab", key, su - inst.total)
			elif ph == P.CHARGE:
				var c := "lightning_charge" if inst.data.get("bolt_ready", false) else "fire_charge"
				_play(c, "fc%d%s" % [inst.attack_id, c], 1.0, 0.15)
			elif inst.heavy:
				_play("fire_release", "fr%d" % inst.attack_id, 1.0, 0.04, maxf(_contact("fire_release") - 0.03, 0.0))
			else:
				_play("fire_jab", "fj%d" % inst.attack_id, 1.0, 0.03)
		"lightning":
			_play("lightning_release", "lr%d" % inst.attack_id, 1.0, 0.03, maxf(_contact("lightning_release") - 0.02, 0.0))
		"fire_tech":
			var mode := String(inst.data.get("mode", ""))
			if mode == "DRAW":
				_play("heat_draw", "hd%d" % inst.attack_id, 1.0, 0.15)
			elif mode == "VENT":
				_play("fire_release", "fv%d" % inst.attack_id, 1.0, 0.05)
			elif ph == P.ACTIVE or ph == P.RECOVERY:
				_play("earth_throw", "ft%d" % inst.attack_id, 1.0, 0.05, maxf(_contact("earth_throw") - 0.03, 0.0))
			else:
				_play("magma_hold", "mh%d" % inst.attack_id, 1.0, 0.15)
		"pour":
			if ph == P.STARTUP:
				_play_aligned("pour", key, su - inst.total)
		"vent":
			_play("fire_release", key, 1.0, 0.05)
		"air_attack":
			if ph == P.STARTUP:
				_play_aligned("air_push", key, su - inst.total)
			elif ph == P.CHARGE:
				_play("air_gust", "ag%d" % inst.attack_id, 0.35, 0.1)
			elif inst.heavy:
				_play("air_gust", "ag2%d" % inst.attack_id, 1.0, 0.04, maxf(_contact("air_gust") - 0.03, 0.0))
		"air_tech":
			if ph == P.STARTUP:
				_play("jump", key, 1.0, 0.05)
			else:
				_play("glide" if a.gliding else "fall", "at%s" % str(a.gliding), 1.0, 0.18)
		_:
			_animate_from_def(a, inst)


## Any other action (new moves from the move registry) animates from its def's clip names
## (docs/MOVESET.md, def schema): `anim` through startup with its contact aligned to the end of
## startup, `anim_hold`/`anim_charge` while charging or channelling, then `anim_heavy` (heavy) or
## `anim_active` from its contact, else `anim` simply plays on. Unknown clips use their stand-ins;
## with no usable clip the current one keeps playing.
func _animate_from_def(_a: ActorState, inst: ActionInst) -> void:
	var d: Dictionary = inst.def
	var P := ActionInst.P
	var ph := inst.phase
	var base := resolve_clip(String(d.get("anim", "")))
	var su := float(inst.data.get("startup", d.get("startup", 0.0)))
	var tag := "def:%s:%d" % [inst.id, inst.attack_id]
	if ph == P.STARTUP:
		if base != "":
			_play_aligned(base, tag + ":s", su - inst.total)
		return
	if ph == P.CHARGE or ph == P.CHANNEL:
		var hold := resolve_clip(String(d.get("anim_hold", d.get("anim_charge", ""))))
		if hold != "":
			_play(hold, tag + ":h", 1.0, 0.12)
		return
	var fin := ""
	if inst.heavy:
		fin = resolve_clip(String(d.get("anim_heavy", "")))
	if fin == "":
		fin = resolve_clip(String(d.get("anim_active", "")))
	if fin != "":
		_play(fin, tag + ":a", 1.0, 0.05, maxf(_contact(fin) - 0.03, 0.0))
	elif base != "" and _cur != base:
		_play(base, tag + ":a", 1.0, 0.05, maxf(_contact(base) - 0.03, 0.0))


# ------------------------------------------------------------------ runtime layers

## Sim-driven reactions, once per tick: hits/blocks kick the springs, landings kick the pelvis.
func _react_to_sim(a: ActorState) -> void:
	if rig == null:
		return
	if not _prev_grounded and a.grounded:
		rig.kick_pelvis(absf(minf(_prev_vy, 0.0)) * 0.34)
	_prev_grounded = a.grounded
	_prev_vy = a.vel.y
	if _prev_health < 0.0:
		_prev_health = a.health
		_prev_balance = a.balance
		_last_hit_tick = _newest_hit_tick(a)
		return
	var newest := _newest_hit_tick(a)
	var dmg := _prev_health - a.health
	var bal := maxf(_prev_balance - a.balance, 0.0)
	if newest > _last_hit_tick or dmg > 0.5:
		var to_local := Basis(Vector3.UP, a.facing).inverse()
		var res := a.last_result
		if res == "hit" or res == "knockdown":
			var dir := to_local * a.last_hit_dir
			var s := clampf(0.35 + dmg / 22.0 + bal / 70.0, 0.3, 1.25)
			if res == "knockdown":
				s *= 0.45          # the authored fall carries it; just add the directional jolt
			rig.hits.hit(dir, s)
			if a.stun_kind == "heavy":
				rig.kick_pelvis(0.9)
		elif res == "block" or res == "guard_break":
			rig.hits.block(Vector3(0, 0, -1), 1.0 if res == "guard_break" else 0.55)
		elif res == "perfect":
			rig.hits.block(Vector3(0, 0, -1), 0.22)
		elif dmg > 0.5:
			rig.hits.hit(to_local * -a.forward(), 0.3)   # contact burn and similar
	_last_hit_tick = newest
	_prev_health = a.health
	_prev_balance = a.balance


static func _newest_hit_tick(a: ActorState) -> int:
	var m := -1
	for k in a.hits_taken:
		m = maxi(m, int(a.hits_taken[k]))
	return m


## The sim snaps the feet onto a step in one tick; the body follows over ~0.1 s instead
## (feet are then grounded by the IK), and the offset is bounded so it can never drift.
func _smooth_visual_height(a: ActorState, dt: float) -> void:
	var y := global_position.y
	if not _vis_init or absf(y - _vis_y) > 0.7:
		_vis_y = y
		_vis_init = true
	_vis_y = lerpf(_vis_y, y, 1.0 - exp(-(14.0 if a.grounded else 40.0) * dt))
	_vis_y = clampf(_vis_y, y - 0.45, y + 0.45)
	model.position.y = _vis_y - y


func _game() -> Node:
	var p := get_parent()
	return p if p != null and p.get("world") is CombatWorld else null


func _compute_lod(a: ActorState) -> int:
	if AnimRigSettings.force_lod >= 0:
		return AnimRigSettings.force_lod
	var l := 0
	var vp := get_viewport() if is_inside_tree() else null
	var cam := vp.get_camera_3d() if vp else null
	if cam:
		var d := cam.global_position.distance_to(global_position)
		l = 2 if d > LOD_FAR else (1 if d > LOD_NEAR else 0)
	if a.is_dummy or is_dummy:
		l = maxi(l, 1)
	var g := _game()
	if g and int(g.get("quality")) == 0:
		l = maxi(l, 1)
	return l


func _drive_rig(a: ActorState, dt: float) -> void:
	lod = _compute_lod(a)
	if secondary:
		secondary.active = AnimRigSettings.secondary and lod == 0
	if lod >= 2:
		rig.active = false
		return
	rig.active = true
	rig.lod = lod
	var g := _game()
	var world: CombatWorld = g.get("world") if g else null
	rig.arena = world.arena if world else null
	# Ground velocity in the fighter's own frame, smoothed (sim ticks are 60 Hz, frames vary).
	var raw := _vel_meas if _have_meas else Vector3(a.vel.x, 0.0, a.vel.z)
	var prev := _vel_s
	_vel_s = _vel_s.lerp(raw, 1.0 - exp(-14.0 * dt))
	if dt > 1e-5:
		_acc_s = _acc_s.lerp((_vel_s - prev) / dt, 1.0 - exp(-8.0 * dt))
	var f := Vector3(sin(rotation.y), 0.0, cos(rotation.y))
	var r := f.cross(Vector3.UP)                     # the character's right
	var local := Vector2(_vel_s.dot(r), _vel_s.dot(f))
	var spd := _vel_s.length()
	# Turning on the spot steps the feet round: the yaw rate becomes a small sideways gait input
	# (a left turn steps left). Slow target tracking stays planted (foot locking absorbs it).
	_yaw_rate_s = lerpf(_yaw_rate_s, clampf(_yaw_rate, -20.0, 20.0), 1.0 - exp(-10.0 * dt))
	var turn_k := smoothstep(1.2, 3.0, absf(_yaw_rate_s)) * (1.0 - smoothstep(0.4, 1.0, spd))
	if turn_k > 0.0:
		local.x += clampf(-_yaw_rate_s * 0.22, -1.0, 1.0) * turn_k
	rig.local_vel = local
	rig.ground_speed = spd
	rig.stance = ["stance_earth", "stance_water", "stance_fire", "stance_air"][clampi(a.element, 0, 3)]
	var inst := a.action
	var shot := _one_shot_t > 0.0 and a.stun <= 0.0
	var free := a.grounded and inst == null and a.stun <= 0.0 and not shot
	rig.loco_target = 1.0 if free else 0.0
	var legs := 0.0
	if a.grounded and a.stun <= 0.0 and (inst != null or shot):
		if shot:
			legs = smoothstep(0.15, 0.6, spd)
		elif inst.id == "guard" and a.wall_body < 0:
			legs = smoothstep(0.15, 0.6, spd)
		elif inst.phase == ActionInst.P.CHARGE or inst.phase == ActionInst.P.CHANNEL:
			legs = smoothstep(0.2, 0.7, spd)
	rig.legs_target = legs
	rig.model_lift = model.position.y
	# Planted feet lock to the ground while the legs belong to a gait or a stance (not in stuns,
	# where the knockback slide should drag them).
	rig.lock_target_w = 1.0 if (a.grounded and a.stun <= 0.0 and (free or legs > 0.0 or inst == null or inst.id == "guard")) else 0.0
	# Lean into acceleration and into turns (centripetal: speed x yaw rate), only on the ground.
	var lean := Vector2.ZERO
	if free:
		var acc_f := _acc_s.dot(f)
		var acc_r := _acc_s.dot(r)
		var fwd_spd := _vel_s.dot(f)
		lean.x = clampf(acc_f * 0.010, -0.09, 0.11)
		lean.y = clampf(-fwd_spd * _yaw_rate * 0.016 + acc_r * 0.008, -0.17, 0.17)
	rig.lean_target = lean
	# Foot IK only while standing on the ground in poses that keep the feet down.
	var lifts := inst != null and (inst.id == "evade" or inst.id == "air_dash" or inst.id == "air_tech")
	var down := a.stun > 0.0 and (a.stun_kind == "knockdown" or a.stun_kind == "getup")
	rig.ik_target_w = 1.0 if (a.grounded and not lifts and not down) else 0.0
	# Look at the incoming threat first, else the lock target; chest aims along the attack.
	var look_w := 0.0
	if lod == 0 and world and not down and not lifts:
		var tgt := _look_target(a, world)
		if tgt != Vector3.INF:
			rig.look_world = tgt
			if a.stun > 0.0:
				look_w = 0.25
			elif inst == null:
				look_w = 0.85
			elif inst.id == "guard":
				look_w = 0.7
			else:
				look_w = 0.4
	rig.look_target_w = look_w
	var aim_w := 0.0
	if inst != null and inst.data.has("face") and inst.id != "evade" and inst.id != "air_dash":
		var ph2 := inst.phase
		if ph2 == ActionInst.P.STARTUP or ph2 == ActionInst.P.CHARGE or ph2 == ActionInst.P.CHANNEL or ph2 == ActionInst.P.ACTIVE:
			var fd: Vector3 = inst.data.face
			if Vector2(fd.x, fd.z).length() > 0.01:
				rig.aim_yaw = clampf(wrapf(atan2(fd.x, fd.z) - rotation.y, -PI, PI), -0.6, 0.6)
				aim_w = 1.0
	rig.aim_target_w = aim_w
	rig.tick(dt)
	_sync_player_to_blend()
	if AnimRigSettings.debug_draw:
		if _debug_draw == null:
			_debug_draw = AnimDebugDraw.new()
			add_child(_debug_draw)
		_debug_draw.draw(rig)
	elif _debug_draw:
		_debug_draw.queue_free()
		_debug_draw = null


## While the blend space owns the body, the AnimationPlayer's own gait/stance clip is kept on the
## blend's phase and rate, so when an action takes over (the blend fades out in ~0.1 s) the clip it
## cross-fades from is the same step, not a different one (no scissoring legs).
func _sync_player_to_blend() -> void:
	if ap == null or rig.loco_w < 0.98 or not AnimRigSettings.loco_blend:
		return
	var c := ap.current_animation
	if not rig.anims.has(c) or c != _cur:
		return
	var anim: Animation = rig.anims[c]
	var want := rig.loco.clip_time(c, anim.length)
	var now := ap.current_animation_position
	var drift := wrapf(now - want, -anim.length * 0.5, anim.length * 0.5)
	if LocomotionBlender.GAITS.has(c):
		ap.speed_scale = maxf(rig.loco.cycle_rate * anim.length, 0.05)
	if absf(drift) > 0.03:
		ap.seek(want, false)


## World point to look at: the nearest incoming attack body (by time to impact), else the
## locked target's head. Vector3.INF when there is nothing to look at.
func _look_target(a: ActorState, world: CombatWorld) -> Vector3:
	var me := a.pos + Vector3(0, 1.5, 0)
	var best := Vector3.INF
	var best_t := 1.1
	for b in world.bodies:
		if not b.alive or b.attack_id <= 0 or b.attack_owner == a.id or b.controller == a.id:
			continue
		var rel := me - b.pos
		var dist := rel.length()
		if dist > 14.0 or dist < 0.4:
			continue
		var closing := b.vel.dot(rel / dist)
		if closing < 2.0:
			continue
		var tti := dist / closing
		if tti < best_t:
			best_t = tti
			best = b.pos
	if best != Vector3.INF:
		return best
	var t := world.get_actor(a.lock_target)
	if t != null and t.pos.distance_to(a.pos) < 24.0:
		return t.pos + Vector3(0, 1.5, 0)
	return Vector3.INF


# ------------------------------------------------------------------ fallback mannequin

func _build_fallback() -> void:
	var mk := func(mesh: Mesh, col: Color, pos: Vector3, nm: String) -> MeshInstance3D:
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		var m := StandardMaterial3D.new()
		m.albedo_color = col
		m.roughness = 0.8
		mi.material_override = m
		mi.position = pos
		add_child(mi)
		_fallback_parts[nm] = mi
		return mi
	var body := CapsuleMesh.new()
	body.radius = 0.22
	body.height = 0.9
	mk.call(body, colors.get("cloth_main", Color.GRAY), Vector3(0, 1.15, 0), "torso")
	var head := SphereMesh.new()
	head.radius = 0.13
	head.height = 0.26
	mk.call(head, colors.get("skin", Color.BISQUE), Vector3(0, 1.68, 0), "head")
	var leg := CapsuleMesh.new()
	leg.radius = 0.09
	leg.height = 0.85
	mk.call(leg, colors.get("cloth_main", Color.GRAY).darkened(0.2), Vector3(-0.12, 0.43, 0), "leg_l")
	mk.call(leg, colors.get("cloth_main", Color.GRAY).darkened(0.2), Vector3(0.12, 0.43, 0), "leg_r")
	var arm := CapsuleMesh.new()
	arm.radius = 0.07
	arm.height = 0.7
	mk.call(arm, colors.get("wraps", Color.WHITE), Vector3(-0.32, 1.2, 0), "arm_l")
	mk.call(arm, colors.get("wraps", Color.WHITE), Vector3(0.32, 1.2, 0), "arm_r")
	var sash := BoxMesh.new()
	sash.size = Vector3(0.48, 0.08, 0.3)
	mk.call(sash, colors.get("cloth_accent", Color.ORANGE), Vector3(0, 0.95, 0), "sash")


func _fallback_pose(a: ActorState, dt: float) -> void:
	var spd := Vector2(a.vel.x, a.vel.z).length()
	_lean = lerpf(_lean, clampf(spd * 0.03, 0.0, 0.2), 1.0 - exp(-8.0 * dt))
	var t := Time.get_ticks_msec() * 0.001
	var swing := sin(t * (2.0 + spd * 1.6)) * clampf(spd * 0.12, 0.0, 0.6)
	_fallback_parts.leg_l.rotation.x = swing
	_fallback_parts.leg_r.rotation.x = -swing
	var arm_up := 0.0
	if a.action != null:
		arm_up = -1.2
	if a.guarding:
		arm_up = -1.6
	_fallback_parts.arm_l.rotation.x = -swing * 0.6 + arm_up
	_fallback_parts.arm_r.rotation.x = swing * 0.6 + arm_up
	_fallback_parts.torso.rotation.x = _lean
	if a.stun_kind == "knockdown":
		rotation.x = lerpf(rotation.x, -1.3, 1.0 - exp(-10.0 * dt))
	else:
		rotation.x = lerpf(rotation.x, 0.0, 1.0 - exp(-10.0 * dt))
