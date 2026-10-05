extends RefCounted
## Shared choreography helpers for the Earth sub-element showcases (showcase_earth_metal / _sand / _magma).
## Every move goes through InputFrame -> PlayerController, so the video shows the real game path.
## Subclasses implement steps(g, f, t) and may override scenario() / setup(g).

const RIVAL_POS := Vector3(0.0, 0.0, 1.0)
const SIDE_YAW := PI - 1.0
const OPEN_YAW := PI - 0.45
const RIVAL_GAP := 6.5

var sub := 0
var _done := {}
var _ends := {}               # button -> time its hold ends
var _shot := ""
var _cam_yaw: Variant = null
var _jit := 1.0
var _walking := false
var _guard_cue := -1.0        # time the next rival throw was cued (guard reactions)
var _trace := OS.has_environment("SHOWCASE_TRACE")
var _log_state := {}


func scenario() -> String:
	return "stone_rain"


func bind(g: Game) -> void:
	g.opponent.pos = RIVAL_POS
	g.fighters[g.opponent.id].snap(g.opponent)
	g.ai.cfg["drill"] = "passive"
	g.ai.cfg["interval"] = 999.0
	g.ai.cfg["reaction"] = 9.0
	g.ai._next_attack = 999.0
	setup(g)
	g.world.take_events()


func setup(_g: Game) -> void:
	pass


func wants_shot() -> String:
	return _shot


func frame(g: Game, f: InputFrame, t: float) -> void:
	_shot = ""
	_release_due(f, t)
	var o := g.opponent
	# Capture only: the rival never goes down mid-showcase (a round reset would end the choreography).
	if o.health < 25.0:
		o.health = 45.0
	if g.ai.cfg.drill == "stone_rain" and o.action != null and o.action.id == "earth_attack":
		g.ai.cfg["drill"] = "passive"
	if cue("sub", t >= 0.15):
		f.sub_select = sub
	if cue("cam_open", true):
		_cam_yaw = OPEN_YAW
		f.cam_delta.x = -wrapf(OPEN_YAW - g.cam.yaw, -PI, PI)
	steps(g, f, t)
	_camera(g, f)
	_rival_range(g)
	if _trace:
		_trace_tick(g, t)


func steps(_g: Game, _f: InputFrame, _t: float) -> void:
	pass


# ------------------------------------------------------------------ rival

## The rival throws one stone at the player (cued, so every exchange lands on camera).
func throw_cue(g: Game) -> void:
	_walking = false
	g.ai.cfg["drill"] = "stone_rain"
	g.ai.cfg["aggression"] = 0.55
	g.ai._next_attack = 0.0


func rival_throwing(g: Game) -> bool:
	var o := g.opponent
	return o.action != null and o.action.id == "earth_attack"


func incoming(g: Game) -> MatBody:
	for b in g.world.bodies:
		if b.alive and b.attack_id != 0 and b.attack_owner == g.opponent.id and b.controller < 0 and b.form != Sim.Form.WAVE:
			return b
	return null


func _rival_range(g: Game) -> void:
	var o := g.opponent
	var p := g.player
	var gap := Vector2(o.pos.x - p.pos.x, o.pos.z - p.pos.z).length()
	if _walking:
		if gap <= RIVAL_GAP:
			_walking = false
			g.ai.cfg["drill"] = "passive"
	elif gap > RIVAL_GAP + 1.0 and g.ai.cfg.drill == "passive" and o.action == null and o.stun <= 0.0:
		_walking = true
		g.ai.cfg["drill"] = "stone_rain"
		g.ai.cfg["aggression"] = 3.0


# ------------------------------------------------------------------ camera

func side_camera() -> void:
	_cam_yaw = SIDE_YAW


func _camera(g: Game, f: InputFrame) -> void:
	if _cam_yaw == null or f.cam_delta != Vector2.ZERO:
		return
	var diff := wrapf(float(_cam_yaw) - g.cam.yaw, -PI, PI)
	var d := clampf(diff * 0.05, -0.02, 0.02)
	if absf(d) < 0.0002:
		_jit = -_jit
		d = 0.0002 * _jit
	f.cam_delta.x = -d


# ------------------------------------------------------------------ input helpers

func cue(name: String, cond: bool) -> bool:
	if cond and not _done.has(name):
		_done[name] = true
		return true
	return false


func shot(name: String) -> void:
	_shot = name


## Press a button and hold it for `hold` seconds. gesture: Sim.Gesture on the same tick (attack / guard).
func press(f: InputFrame, what: String, t: float, hold: float, gesture: int = Sim.Gesture.NONE) -> void:
	match what:
		"attack":
			f.attack_pressed = true
			f.attack_held = true
			f.attack_gesture = gesture
		"guard":
			f.guard_pressed = true
			f.guard_held = true
		"tech":
			f.tech_pressed = true
			f.tech_held = true
		"evade":
			f.evade_pressed = true
			f.evade_held = true
	_ends[what] = t + hold


## A guard flick (UP push / DOWN sink) while the guard is held.
func guard_flick(f: InputFrame, gesture: int) -> void:
	f.guard_gesture = gesture
	f.guard_held = true


func release_now(f: InputFrame, what: String) -> void:
	_ends.erase(what)
	match what:
		"attack":
			f.attack_held = false
			f.attack_released = true
		"guard":
			f.guard_held = false
			f.guard_released = true
		"tech":
			f.tech_held = false
			f.tech_released = true
		"evade":
			f.evade_held = false


func _release_due(f: InputFrame, t: float) -> void:
	for what in ["attack", "guard", "tech", "evade"]:
		if _ends.has(what) and t >= float(_ends[what]):
			release_now(f, what)


func holding(what: String) -> bool:
	return _ends.has(what)


## Technique drag aim toward a world direction (camera frame, x right, y forward).
func aim_at(g: Game, f: InputFrame, dir: Vector3) -> void:
	dir.y = 0.0
	if dir.length() < 0.01:
		return
	dir = dir.normalized()
	var yaw := g.cam.yaw - f.cam_delta.x
	var fwd := Vector3(sin(yaw), 0.0, cos(yaw))
	var right := fwd.cross(Vector3.UP)
	f.tech_aim = Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)
	f.tech_aim_active = true


## Stick movement toward a world direction (camera frame).
func stick_toward(g: Game, f: InputFrame, dir: Vector3, amount: float = 1.0) -> void:
	dir.y = 0.0
	if dir.length() < 0.01:
		f.move = Vector2.ZERO
		return
	dir = dir.normalized()
	var yaw := g.cam.yaw
	var fwd := Vector3(sin(yaw), 0.0, cos(yaw))
	var right := fwd.cross(Vector3.UP)
	f.move = Vector2(dir.dot(right), dir.dot(fwd)) * amount


# ------------------------------------------------------------------ trace (SHOWCASE_TRACE=1)

func _trace_tick(g: Game, t: float) -> void:
	for a in [g.player, g.opponent]:
		var act := "-" if a.action == null else "%s/%s/T%d" % [a.action.id, a.action.phase_name(), a.action.tier()]
		var key := "%s|%s|%d" % [a.name, act, a.held_body]
		if _log_state.get(a.name, "") != key:
			_log_state[a.name] = key
			print("t=%.2f %s hp%.0f foc%.0f met%.0f %s held:%d" % [t, a.name, a.health, a.focus, a.metal_carried, act, a.held_body])
