extends Node3D
## Animation lab: scripted fighters on the real lab arena, driven through the real simulation
## (CombatWorld + intents) and shown through the real FighterView, with a close camera on the feet
## and body. Used to film the runtime animation layers (docs/ANIMATION.md, presentation/anim/).
##
##   tools/scripts/godot.sh --render --resolution 1280x592 --write-movie /tmp/x.avi --fixed-fps 30 \
##       res://tests/anim/anim_lab.tscn -- --shot=loco [--anim=off] [--seconds=20] [--markers]
##
## Shots: loco (idle > walk > run > stop > sharp turns), strafe (locked: strafe/backpedal/diagonals),
## ledge (step block, terrace edge + drop, pool edge + wade), hits (front/side/back/heavy/block/knockdown),
## crowd (6 fighters, perf). --anim=off turns every runtime layer off (the plain-clip baseline).
## Exposes `world` and `quality` like Game, so FighterView finds the arena and the incoming threats.

var world: CombatWorld
var quality := 2
var shot := "loco"
var seconds := -1.0
var intents := {}
var views := {}
var subject: ActorState
var other: ActorState
var cam: Camera3D
var t := 0.0
var _cam_pos := Vector3.ZERO
var _cam_look := Vector3.ZERO
var _cam_init := false
var _events_done := {}
var _seg := -1
var metrics := false
var manual := false                  # tests: driven by step_manual(), `args` instead of the command line
var args := PackedStringArray()
var trace_from := -1.0
var trace_to := -1.0
var _m := {}                # segment -> accumulators
var _prev_probe := PackedVector3Array()
var _prev2_probe := PackedVector3Array()
var _prev_planted := [false, false]
var _frames := 0
const SEGMENTS := {
	"loco": [[1.0, "idle"], [2.6, "walk_slow"], [3.6, "walk"], [5.2, "run"], [6.4, "stop"], [7.8, "turn180"],
		[9.2, "curve"], [10.4, "stop2"], [11.6, "walk2"], [13.0, "run2"], [99.0, "decel"]],
	"strafe": [[1.0, "idle"], [3.0, "strafe_a"], [5.0, "strafe_b"], [6.5, "back"], [8.0, "in"], [9.5, "diag_a"],
		[11.0, "diag_b"], [99.0, "idle2"]],
	"ledge": [[0.5, "idle"], [4.0, "step_up"], [6.5, "on_step_edge"], [9.1, "terrace_walk"], [10.9, "terrace_edge"],
		[13.5, "drop_land"], [16.1, "pool_walk"], [17.9, "pool_edge"], [99.0, "wade"]],
	"hits": [[1.0, "idle"], [2.5, "light"], [4.0, "medium"], [5.5, "side"], [7.0, "back"], [9.0, "heavy"],
		[10.5, "block"], [99.0, "knockdown"]],
	"crowd": [[99.0, "crowd"]],
}


func _ready() -> void:
	physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	for a in (args if manual else OS.get_cmdline_user_args()):
		if a.begins_with("--shot="):
			shot = a.substr(7)
		elif a.begins_with("--seconds="):
			seconds = float(a.substr(10))
		elif a == "--anim=off":
			AnimRigSettings.loco_blend = false
			AnimRigSettings.foot_ik = false
			AnimRigSettings.look_at = false
			AnimRigSettings.hit_springs = false
			AnimRigSettings.lean = false
			AnimRigSettings.secondary = false
		elif a.begins_with("--off="):
			for k in a.substr(6).split(","):
				match k:
					"loco": AnimRigSettings.loco_blend = false
					"ik": AnimRigSettings.foot_ik = false
					"lock": AnimRigSettings.foot_lock = false
					"look": AnimRigSettings.look_at = false
					"hits": AnimRigSettings.hit_springs = false
					"lean": AnimRigSettings.lean = false
					"secondary": AnimRigSettings.secondary = false
		elif a.begins_with("--trace="):
			var tr := a.substr(8).split(":")
			trace_from = float(tr[0])
			trace_to = float(tr[1])
		elif a == "--metrics":
			metrics = true
			AnimRigSettings.record_probes = true
		elif a == "--markers":
			AnimRigSettings.debug_draw = true
	world = CombatWorld.new(3)
	if not metrics:     # headless measurements need no scenery
		var av := ArenaView.new()
		add_child(av)
		av.build(world.arena, quality)
	match shot:
		"strafe":
			subject = _actor("Subject", Vector3(0, 0, 5), 0, Sim.Element.WATER)
			other = _actor("Target", Vector3(0, 0, 0.5), 1, Sim.Element.EARTH)
			other.is_dummy = true
		"hits":
			subject = _actor("Subject", Vector3(0, 0, 4.0), 0, Sim.Element.EARTH)
			other = _actor("Attacker", Vector3(0, 0, 1.6), 1, Sim.Element.FIRE)
			other.is_dummy = true
		"ledge":
			subject = _actor("Subject", Vector3(7.4, 0, 9.07), 0, Sim.Element.AIR)
		"crowd":
			for i in 6:
				var p := Vector3(-6.0 + (i % 3) * 6.0, 0, -3.0 + int(i / 3) * 6.0)
				var a := _actor("F%d" % i, p, i % 2, i % 4)
				if i == 0:
					subject = a
		_:
			subject = _actor("Subject", Vector3(-12, 0, 5), 0, Sim.Element.FIRE)
	subject.facing = PI * 0.5
	for a in world.actors:
		var fv := FighterView.new()
		add_child(fv)
		fv.setup(a.id, {"cloth_main": Color(0.2, 0.25, 0.35), "cloth_accent": Color(0.9, 0.45, 0.1),
			"skin": Color(0.8, 0.62, 0.5), "wraps": Color(0.85, 0.82, 0.75), "hair": Color(0.1, 0.08, 0.07)})
		fv.snap(a)
		views[a.id] = fv
		if manual and fv.ap:
			fv.ap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
	cam = Camera3D.new()
	cam.fov = 40.0
	cam.near = 0.05
	cam.far = 120.0
	add_child(cam)
	cam.make_current()
	if seconds < 0.0:
		seconds = {"loco": 15.0, "strafe": 12.0, "ledge": 20.0, "hits": 13.0, "crowd": 8.0}.get(shot, 12.0)


func _actor(nm: String, p: Vector3, team: int, element: int) -> ActorState:
	var a := world.add_actor(nm, p, team, {}, element)
	intents[a.id] = ActorIntent.new()
	return a


func _physics_process(_dt: float) -> void:
	if manual:
		return
	if subject == null:
		push_error("anim lab: setup failed")
		get_tree().quit(1)
		return
	_tick()
	if t >= seconds:
		if metrics:
			_print_metrics()
		get_tree().quit(0)


## Tests: one sim tick and one rendered frame, deterministic (AnimationPlayers advanced by hand).
## Await a process frame afterwards so the skeleton modifiers run.
func step_manual() -> void:
	_tick()
	for a in world.actors:
		var fv: FighterView = views[a.id]
		fv.render(a, 1.0, Sim.DT)
		if fv.ap:
			fv.ap.advance(Sim.DT)
	if metrics:
		_measure(Sim.DT)


func _tick() -> void:
	t += Sim.DT
	for i in intents.values():
		var x: ActorIntent = i
		x.move = Vector3.ZERO
		x.attack_pressed = false
		x.attack_released = false
		x.guard_pressed = false
		x.evade_pressed = false
		x.tech_pressed = false
		x.tech_released = false
	if has_method("_script_" + shot):
		call("_script_" + shot)
	else:
		_script_loco()
	world.step(intents)
	world.take_events()
	for a in world.actors:
		views[a.id].push_state(a)


func _process(dt: float) -> void:
	if subject == null or manual:
		return
	var alpha := Engine.get_physics_interpolation_fraction()
	for a in world.actors:
		views[a.id].render(a, alpha, dt)
	_update_camera(dt)
	if metrics:
		_measure(dt)


func _seg_name() -> String:
	for e in SEGMENTS.get(shot, [[99.0, "all"]]):
		if t < float(e[0]):
			return String(e[1])
	return "end"


## Per rendered frame, from the rig's recorded output of the previous frame:
##   skate  - horizontal travel of the ball of a foot while it is in ground contact (cm/s of contact)
##   ground - mean |ankle height - planted ankle height| over contact frames (cm): hover/penetration
##   jerk   - peak and p95 acceleration of head/hands/knees/hips in the fighter's own frame (m/s^2):
##            pops show up as spikes
func _measure(dt: float) -> void:
	var fv: FighterView = views[subject.id]
	if fv.rig == null or fv.rig.probe_world.size() < 10 or dt <= 0.0:
		return
	_frames += 1
	if _frames < 4:
		_prev2_probe = _prev_probe
		_prev_probe = fv.rig.probe_world.duplicate()
		return
	var cur := fv.rig.probe_world.duplicate()
	var seg := _seg_name()
	if not _m.has(seg):
		_m[seg] = {"frames": 0, "contact": 0.0, "skate": 0.0, "gerr": 0.0, "gn": 0, "jerk": [], "slide_max": 0.0}
	var m: Dictionary = _m[seg]
	m.frames += 1
	var inv := fv.global_transform.affine_inverse()
	var floor_top := subject.pos.y + Sim.STEP_HEIGHT + 0.02
	for i in 2:
		var ank: Vector3 = cur[5 + i]
		var toe: Vector3 = cur[7 + i]
		var g := world.arena.ground_height(toe.x, toe.z, floor_top, 0.0)
		var ga := world.arena.ground_height(ank.x, ank.z, floor_top, 0.0)
		var planted := ank.y - ga < 0.11 and toe.y - g < 0.05
		if planted and _prev_planted[i]:
			var pt: Vector3 = _prev_probe[7 + i]
			var d := Vector2(toe.x - pt.x, toe.z - pt.z).length()
			m.skate += d
			m.contact += dt
			m.slide_max = maxf(m.slide_max, d / dt)
		if planted:
			m.gerr += absf(ank.y - ga - 0.085)
			m.gn += 1
		if t >= trace_from and t <= trace_to:
			var pt2: Vector3 = _prev_probe[7 + i]
			print("TR t=%.3f %s planted=%s lk=%s w=%.2f toe_h=%.3f ank_h=%.3f dxz=%.1fcm phase=%.3f rate=%.2f" % [t, "LR"[i], planted,
				fv.rig.locked[i], fv.rig.lock_w[i], toe.y - g, ank.y - ga, 100.0 * Vector2(toe.x - pt2.x, toe.z - pt2.z).length(),
				fv.rig.loco.phase, fv.rig.loco.cycle_rate])
		_prev_planted[i] = planted
	if _prev2_probe.size() == cur.size():
		var pk := 0.0
		for j in [0, 1, 2, 3, 4, 9]:
			var p0: Vector3 = inv * _prev2_probe[j]
			var p1: Vector3 = inv * _prev_probe[j]
			var p2: Vector3 = inv * cur[j]
			var jk := (p2 - 2.0 * p1 + p0).length() / (dt * dt)
			pk = maxf(pk, jk)
			if t >= trace_from and t <= trace_to and jk > 120.0:
				print("JERK t=%.3f probe=%s %.0f  locks=%s/%s w=%.2f/%.2f" % [t, FighterAnimRig.PROBES[j], jk, fv.rig.locked[0], fv.rig.locked[1], fv.rig.lock_w[0], fv.rig.lock_w[1]])
		m.jerk.append(pk)
	_prev2_probe = _prev_probe
	_prev_probe = cur


## {segment: {contact, skate_cms, slide_max_cms, ground_cm, jerk_p95, jerk_max}}
func metric_table() -> Dictionary:
	var out := {}
	for seg in _m:
		var m: Dictionary = _m[seg]
		var j: Array = m.jerk.duplicate()
		j.sort()
		out[seg] = {"contact": m.contact, "skate_cms": 100.0 * m.skate / maxf(m.contact, 1e-3),
			"slide_max_cms": 100.0 * m.slide_max, "ground_cm": 100.0 * m.gerr / maxf(m.gn, 1),
			"jerk_p95": j[int(j.size() * 0.95)] if j.size() > 0 else 0.0, "jerk_max": j[j.size() - 1] if j.size() > 0 else 0.0}
	return out


func _print_metrics() -> void:
	print("ANIM-METRICS shot=%s anim=%s" % [shot, "on" if AnimRigSettings.loco_blend else "off"])
	print("%-14s %8s %10s %10s %10s %9s %9s" % ["segment", "contact", "skate", "slide_max", "ground", "jerk_p95", "jerk_max"])
	for seg in _m:
		var m: Dictionary = _m[seg]
		var j: Array = m.jerk
		j.sort()
		var p95: float = j[int(j.size() * 0.95)] if j.size() > 0 else 0.0
		var jm: float = j[j.size() - 1] if j.size() > 0 else 0.0
		print("%-14s %7.2fs %7.1fcm/s %7.1fcm/s %8.2fcm %9.1f %9.1f" % [seg, m.contact,
			100.0 * m.skate / maxf(m.contact, 1e-3), 100.0 * m.slide_max, 100.0 * m.gerr / maxf(m.gn, 1), p95, jm])


## Teleports the subject (new segment of a shot): the view snaps, nothing is interpolated.
func _place(a: ActorState, p: Vector3, yaw: float) -> void:
	a.pos = p
	a.vel = Vector3.ZERO
	a.facing = yaw
	a.grounded = true
	a.ground_y = p.y
	views[a.id].snap(a)
	_cam_init = false
	_frames = 0
	_prev_planted = [false, false]


func _once(key: String) -> bool:
	if _events_done.has(key):
		return false
	_events_done[key] = true
	return true


func _mv(v: Vector3) -> void:
	intents[subject.id].move = v


# ------------------------------------------------------------------ shots

func _script_loco() -> void:
	# x+ along z = 5: idle, slow walk, full walk, run, stop; run back, curve, hard stop, walk off.
	if t < 1.0:
		pass
	elif t < 2.6:
		_mv(Vector3(0.3, 0, 0))
	elif t < 3.6:
		_mv(Vector3(0.6, 0, 0))
	elif t < 5.2:
		_mv(Vector3(1.0, 0, 0))
	elif t < 6.4:
		pass
	elif t < 7.8:
		_mv(Vector3(-1.0, 0, 0))                  # 180 deg turn into a run
	elif t < 9.2:
		var k := (t - 7.8) / 1.4                   # sweeping 90 deg curve at speed
		_mv(Vector3(-cos(k * PI * 0.5), 0, -sin(k * PI * 0.5)))
	elif t < 10.4:
		pass
	elif t < 11.6:
		_mv(Vector3(0.55, 0, 0.0))                 # walk, then accelerate straight into a run
	elif t < 13.0:
		_mv(Vector3(1.0, 0, 0))
	else:
		_mv(Vector3(0.2, 0, 0.0))                  # run decelerates to a slow walk


func _script_strafe() -> void:
	var s := 0.55
	if t < 1.0:
		pass
	elif t < 3.0:
		_mv(Vector3(s, 0, 0))                      # strafe (target is at -Z)
	elif t < 5.0:
		_mv(Vector3(-s, 0, 0))
	elif t < 6.5:
		_mv(Vector3(0, 0, s))                      # backpedal
	elif t < 8.0:
		_mv(Vector3(0, 0, -s))                     # walk in
	elif t < 9.5:
		_mv(Vector3(s, 0, s).normalized() * s)     # diagonal back-right
	elif t < 11.0:
		_mv(Vector3(-s, 0, -s).normalized() * s)
	# then idle


func _script_ledge() -> void:
	# 0-6.5 s: step block (0.35 m) - step up, stop with the left foot over the edge.
	# 6.5-13.5 s: terrace (0.6 m) - stand at its edge, then walk off and land.
	# 13.5-20 s: pool edge (floor -0.3 m) - stand at the edge, then wade in.
	if t < 6.5:
		if t > 0.5 and t < 4.0:
			_mv(Vector3(0.28, 0, 0))
	elif t < 13.5:
		if _seg != 1:
			_seg = 1
			_place(subject, Vector3(-3.2, 0.6, 10.07), PI * 0.5)
		var u := t - 6.5
		if u > 0.4 and u < 2.6:
			_mv(Vector3(0.28, 0, 0))
		elif u > 4.4 and u < 5.6:
			_mv(Vector3(0, 0, -0.35))
	else:
		if _seg != 2:
			_seg = 2
			_place(subject, Vector3(7.0, 0.0, 3.08), PI * 0.5)
		var u := t - 13.5
		if u > 0.4 and u < 2.6:
			_mv(Vector3(0.28, 0, 0))
		elif u > 4.4 and u < 5.4:
			_mv(Vector3(0, 0, -0.3))


func _script_hits() -> void:
	# One blow every 1.5 s from the attacker's side; the subject is locked on the attacker.
	var blows := [
		[1.0, Vector3(0, 0, -1), 6.0, 8.0, false],      # light, front
		[2.5, Vector3(0, 0, -1), 15.0, 18.0, false],    # medium, front
		[4.0, Vector3(-1, 0, 0), 10.0, 12.0, false],    # from the side
		[5.5, Vector3(0, 0, 1), 10.0, 12.0, false],     # from behind
		[7.0, Vector3(0, 0, -1), 14.0, 30.0, false],    # heavy stagger
		[9.0, Vector3(0, 0, -1), 10.0, 10.0, true],     # blocked
		[10.5, Vector3(0, 0, -1), 20.0, 200.0, false],  # knockdown
	]
	var it: ActorIntent = intents[subject.id]
	it.guard_held = t > 8.4 and t < 9.6
	if t > 8.4 and t < 8.45:
		it.guard_pressed = true
	for b in blows:
		var at: float = b[0]
		if t >= at and _once("b%.1f" % at):
			var src: Vector3 = b[1]
			var from := subject.pos + src * 1.5 + Vector3(0, 1.2, 0)
			world.hit_actor(subject, {"attack_id": world.new_attack_id(), "attacker": other.id, "from": from,
				"damage": b[2], "balance": b[3], "knock": -src * 2.5, "kind": "lab"})


func _script_crowd() -> void:
	for a in world.actors:
		var k := float(a.id)
		intents[a.id].move = Vector3(sin(t * 0.7 + k), 0, cos(t * 0.5 + k * 1.3)) * (0.4 + 0.6 * absf(sin(t * 0.3 + k)))


# ------------------------------------------------------------------ camera

func _update_camera(dt: float) -> void:
	var p := views[subject.id].global_position as Vector3
	var pos: Vector3
	var look: Vector3
	match shot:
		"ledge":
			# Low, in front of the edge the subject stands on (edges run along X at z = 9 / 10 / 3).
			var edge_z := 9.0 if _seg < 1 else (10.0 if _seg == 1 else 3.0)
			var ground := 0.35 if _seg < 1 else (0.6 if _seg == 1 else 0.0)
			pos = Vector3(p.x - 1.2, ground + 0.75, edge_z - 3.6)
			look = Vector3(p.x, ground + 0.55, edge_z)
		"hits":
			pos = p + Vector3(2.7, 1.25, 0.9)
			look = p + Vector3(0, 0.85, -0.2)
		"strafe":
			pos = p + Vector3(2.2, 1.6, 4.0)
			look = p + Vector3(0, 0.8, -0.6)
		"crowd":
			pos = Vector3(0, 9, 12)
			look = Vector3(0, 0, 0)
		_:
			pos = p + Vector3(0.0, 0.85, 3.6)
			look = p + Vector3(0, 0.8, 0)
	if not _cam_init:
		_cam_pos = pos
		_cam_look = look
		_cam_init = true
	var k := 1.0 - exp(-6.0 * dt)
	_cam_pos = _cam_pos.lerp(pos, k)
	_cam_look = _cam_look.lerp(look, k)
	cam.look_at_from_position(_cam_pos, _cam_look, Vector3.UP)
