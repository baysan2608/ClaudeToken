class_name FighterView
extends Node3D
## Presentation of one fighter. The simulation is the only movement authority:
## this node interpolates position/facing and picks animation clips. Clips are
## in-place; playback speed is matched to ground speed (no skating) and attack
## clips are time-scaled so their authored contact frame lands on the sim's
## startup end (game/assets/characters/fighter_clips.json).

const GLB := "res://assets/characters/fighter.glb"
const CLIPS_JSON := "res://assets/characters/fighter_clips.json"
const WALK_SPEED := 1.4
const RUN_SPEED := 5.5

var actor_id := -1
var ap: AnimationPlayer
var skel: Skeleton3D
var clips := {}
var model: Node3D
var _cur := ""
var _cur_key := ""
var _one_shot := ""
var _one_shot_t := 0.0
var _prev_pos := Vector3.ZERO
var _curr_pos := Vector3.ZERO
var _prev_yaw := 0.0
var _curr_yaw := 0.0
var _hand_r := -1
var _hand_l := -1
var _lean := 0.0
var colors := {}
var _fallback_parts := {}


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
	else:
		_build_fallback()
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
	_prev_pos = _curr_pos
	_prev_yaw = _curr_yaw
	_curr_pos = a.pos
	_curr_yaw = a.facing


func snap(a: ActorState) -> void:
	_prev_pos = a.pos
	_curr_pos = a.pos
	_prev_yaw = a.facing
	_curr_yaw = a.facing
	global_position = a.pos
	rotation.y = a.facing


func render(a: ActorState, alpha: float, dt: float) -> void:
	global_position = _prev_pos.lerp(_curr_pos, alpha)
	rotation.y = lerp_angle(_prev_yaw, _curr_yaw, alpha)
	_animate(a, dt)
	if not model:
		_fallback_pose(a, dt)


func play_one_shot(clip: String, dur: float = 0.35) -> void:
	_one_shot = clip
	_one_shot_t = dur
	_cur_key = ""


func hand_position(right: bool = true) -> Vector3:
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
		c = "idle" if ap.has_animation("idle") else c
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


## Plays clip so that its contact frame lands `remaining` seconds from now.
func _play_aligned(c: String, key: String, remaining: float) -> void:
	var contact := _contact(c)
	var spd := clampf(contact / maxf(remaining, 0.03), 0.5, 2.5)
	_play(c, key, spd, 0.06)


func _animate(a: ActorState, dt: float) -> void:
	if _one_shot_t > 0.0:
		_one_shot_t -= dt
		_play(_one_shot, "oneshot:" + _one_shot, 1.0, 0.05)
		return
	if a.stun > 0.0:
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
	var rd := dir.dot(f.cross(Vector3.UP))
	if a.lock_target >= 0 and fd < 0.55:
		if fd < -0.5:
			_play("walk_back", "wb", clampf(spd / WALK_SPEED, 0.6, 2.2), 0.18)
		elif rd < 0.0:
			_play("strafe_r", "sr", clampf(spd / WALK_SPEED, 0.6, 2.2), 0.18)
		else:
			_play("strafe_l", "sl", clampf(spd / WALK_SPEED, 0.6, 2.2), 0.18)
		return
	if spd < 2.8:
		_play("walk", "walk", clampf(spd / WALK_SPEED, 0.5, 2.0), 0.2)
	else:
		_play("run", "run", clampf(spd / RUN_SPEED, 0.6, 1.5), 0.2)


func _animate_action(a: ActorState, inst: ActionInst) -> void:
	var ph := inst.phase
	var P := ActionInst.P
	var key := "%s:%d:%d" % [inst.id, inst.attack_id, ph]
	var su := float(inst.data.get("startup", inst.def.startup))
	match inst.id:
		"evade":
			_play("evade_" + String(inst.data.get("side", "back")), "ev%d" % inst.attack_id, _clip_len("evade_l") / 0.42, 0.04)
		"air_dash":
			_play("air_dash", "ad%d" % inst.attack_id, 1.0, 0.04)
		"guard":
			if a.wall_body >= 0 and inst.total < 0.35:
				_play_aligned("earth_wall", "gw%d" % inst.attack_id, maxf(0.14 - inst.total, 0.05))
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
