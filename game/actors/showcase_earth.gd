extends RefCounted
## Earth element showcase, driven by --autoplay=show_earth[:seconds] (see Autoplay).
## Capture choreography only: every move goes through InputFrame -> PlayerController,
## so the video shows the real game. Stone Rain lab; the rival stands closer and only
## throws when cued, so each exchange lands on camera:
##   stone shot (tap) -> heavy heave (hold) -> a 200 kg boulder is too heavy to seize ->
##   seize a loose stone instead, drag-aim, throw -> (side camera) guard wall blocks a
##   stone -> perfectly timed guards redirect the rival's stones back at them.

const RIVAL_POS := Vector3(0.0, 0.0, 1.0)
const LOOSE_POS := Vector3(-1.9, 0.25, 3.9)
const BOULDER_POS := Vector3(3.4, 0.6, 5.4)
## Seconds before the incoming stone meets the rising wall at which the redirect guard is
## pressed: the wall needs ~0.05 s to stand, the perfect window is Moves.PERFECT_WINDOW.
const REDIRECT_LEAD := 0.11
## Camera yaw for the guard exchanges: looking across the duel line, so the wall and the
## stone's path in and back out are seen in profile instead of hidden behind the fighter.
const SIDE_YAW := PI - 1.0
## Camera yaw for the opening throws: a little off the fighter's back so the stones' flight
## reads in three quarters instead of straight down the barrel.
const OPEN_YAW := PI - 0.45
## The rival is knocked back by every hit; past this gap it walks back in (see _rival_range).
const RIVAL_GAP := 6.0

var _done := {}
var _ends := {}               # button -> time its hold ends (released on that tick)
var _shot := ""
var _guard := ""              # "", "block", "redirect": how to meet the next rival stone
var _guard_t := -1.0          # when the current guard was pressed
var _seize_t := -1.0          # technique (seize) press time
var _held_t := -1.0           # when the seized stone arrived in hand
var _boulder_t := -1.0
var _cam_yaw: Variant = null  # null = the game's own camera assist
var _jit := 1.0
var _loose_id := -1
var _boulder_id := -1
var _walking := false
var _trace := OS.has_environment("SHOWCASE_TRACE")
var _log_state := {}


func scenario() -> String:
	return "stone_rain"


func bind(g: Game) -> void:
	var w := g.world
	g.opponent.pos = RIVAL_POS
	g.fighters[g.opponent.id].snap(g.opponent)
	# The rival stands its ground, throws only when cued (_throw_cue) and never answers
	# the player's stones, so every exchange is readable.
	g.ai.cfg["drill"] = "passive"
	g.ai.cfg["interval"] = 999.0
	g.ai.cfg["reaction"] = 9.0
	g.ai._next_attack = 999.0
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, Sim.STONE_SHOT_MASS, LOOSE_POS, "scenario")
	b.on_ground = true
	_loose_id = b.id
	var bb := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 200.0, BOULDER_POS, "scenario")
	bb.on_ground = true
	_boulder_id = bb.id
	w.take_events()


func wants_shot() -> String:
	return _shot


func frame(g: Game, f: InputFrame, t: float) -> void:
	_shot = ""
	var w := g.world
	var p := g.player
	var o := g.opponent
	_release_due(f, t)
	# Back to standing still once the cued throw has started.
	if g.ai.cfg.drill == "stone_rain" and o.action != null and o.action.id == "earth_attack":
		g.ai.cfg["drill"] = "passive"
	if _cue("cam_open", true):
		# Start in three quarters (one cut on the very first tick, before anything happens).
		_cam_yaw = OPEN_YAW
		f.cam_delta.x = -wrapf(OPEN_YAW - g.cam.yaw, -PI, PI)
	if _cue("cam_side", t >= 8.4):
		_cam_yaw = SIDE_YAW
	_camera(g, f, t)

	# ---- 1. stone shot (tap)
	if _cue("shot", t >= 0.8):
		_press(f, "attack", t, 0.05)
	# ---- 2. heavy heave (hold past the charge threshold)
	if _cue("heave", t >= 2.4):
		_press(f, "attack", t, 0.95)
	# ---- 3. a 200 kg boulder: too heavy to control (released before the technique
	#         would rip a ground stone instead)
	if _cue("boulder", t >= 4.9):
		_press(f, "tech", t, 0.38)
		_boulder_t = t
	if _boulder_t >= 0.0 and t - _boulder_t <= 0.39:
		var bb := w.get_body(_boulder_id)
		if bb != null:
			_aim_at(g, f, bb.pos - p.pos)
	# ---- 4. technique: seize the loose stone, hold, drag to aim, release to throw
	if _cue("seize", t >= 6.1):
		_press(f, "tech", t, 3.0)
		_seize_t = t
	if _seize_t >= 0.0 and (f.tech_held or f.tech_released):
		var loose := w.get_body(_loose_id)
		if w.held(p) == null and _held_t < 0.0:
			if loose != null:
				_aim_at(g, f, loose.pos - p.pos)
		else:
			if _held_t < 0.0:
				_held_t = t
				_shot = "seized"
			# Drag the aim out to the left, then sweep it back onto the rival and let go.
			var s := t - _held_t
			var ang := 0.0
			if s < 0.45:
				ang = lerpf(30.0, 55.0, smoothstep(0.0, 0.45, s))
			elif s < 1.15:
				ang = lerpf(55.0, 0.0, smoothstep(0.45, 1.15, s))
			_aim_at(g, f, (o.pos - p.pos).rotated(Vector3.UP, deg_to_rad(ang)))
			if s >= 1.35 and f.tech_held:
				_release_now(f, "tech")
				_aim_at(g, f, o.pos - p.pos)
				_shot = "seize_throw"
	# ---- 5. guard wall blocks a rival stone
	if _cue("cue_block", t >= 9.5):
		_guard = "block"
		_throw_cue(g)
	# ---- 6. perfectly timed guards redirect the rival's stones
	if _cue("cue_redirect1", t >= 12.2):
		_guard = "redirect"
		_throw_cue(g)
	if _cue("cue_redirect2", t >= 14.8):
		_guard = "redirect"
		_throw_cue(g)
	# ---- 7. closing stone shot, seen from the side
	if _cue("shot2", t >= 17.2):
		_press(f, "attack", t, 0.05)

	_guards(g, f, t)
	_rival_range(g)
	if _trace:
		_trace_tick(g, t)


func _throw_cue(g: Game) -> void:
	_walking = false
	g.ai.cfg["drill"] = "stone_rain"
	g.ai.cfg["aggression"] = 0.55
	g.ai._next_attack = 0.0


## Knocked back past RIVAL_GAP, the rival walks back in with its own tactical movement
## (stone_rain drill with a short preferred range and no throw due), then stands again.
func _rival_range(g: Game) -> void:
	var o := g.opponent
	var p := g.player
	var gap := Vector2(o.pos.x - p.pos.x, o.pos.z - p.pos.z).length()
	if _walking:
		if gap <= RIVAL_GAP:
			_walking = false
			g.ai.cfg["drill"] = "passive"
			g.ai.cfg["aggression"] = 0.55
	elif gap > RIVAL_GAP + 0.7 and g.ai.cfg.drill == "passive" and o.action == null and o.stun <= 0.0:
		_walking = true
		g.ai.cfg["drill"] = "stone_rain"
		g.ai.cfg["aggression"] = 3.0   # preferred range 0.5..5 m: approach


# ------------------------------------------------------------------ guard timing

func _guards(g: Game, f: InputFrame, t: float) -> void:
	var p := g.player
	var o := g.opponent
	var inc := _incoming(g)
	match _guard:
		"block":
			# Raise the wall as soon as the rival starts ripping a stone, hold it through the hit.
			if _guard_t < 0.0 and o.action != null and o.action.id == "earth_attack":
				_press(f, "guard", t, 99.0)
				_guard_t = t
			if _guard_t >= 0.0 and t - _guard_t > 0.7 and inc == null:
				if f.guard_held and not _ends.has("guard_linger"):
					_ends["guard_linger"] = t + 0.45
				if _ends.has("guard_linger") and t >= float(_ends.guard_linger):
					_ends.erase("guard_linger")
					_release_now(f, "guard")
					_guard = ""
					_guard_t = -1.0
					_shot = "blocked"
		"redirect":
			if _guard_t < 0.0 and inc != null:
				var b: MatBody = inc
				var to := p.pos - b.pos
				to.y = 0.0
				var closing := Vector2(b.vel.x, b.vel.z).dot(Vector2(to.x, to.z).normalized())
				var contact := ActCommon.WALL_DIST - 0.28 - b.radius * 0.7
				if closing > 1.0 and (to.length() - contact) / closing <= REDIRECT_LEAD:
					_press(f, "guard", t, 0.5)
					_guard_t = t
			if _guard_t >= 0.0 and t - _guard_t > 0.55:
				_guard = ""
				_guard_t = -1.0
				_shot = "redirected"


func _incoming(g: Game) -> MatBody:
	var o := g.opponent
	for b in g.world.bodies:
		if b.alive and b.is_stone() and b.attack_id != 0 and b.attack_owner == o.id and b.controller < 0:
			return b
	return null


# ------------------------------------------------------------------ camera

func _camera(g: Game, f: InputFrame, _t: float) -> void:
	if _cam_yaw == null or f.cam_delta != Vector2.ZERO:
		return
	var diff := wrapf(float(_cam_yaw) - g.cam.yaw, -PI, PI)
	var d := clampf(diff * 0.05, -0.02, 0.02)
	if absf(d) < 0.0002:
		_jit = -_jit
		d = 0.0002 * _jit   # net zero, but keeps the assist from taking over
	f.cam_delta.x = -d


# ------------------------------------------------------------------ input helpers

func _cue(name: String, cond: bool) -> bool:
	if cond and not _done.has(name):
		_done[name] = true
		return true
	return false


func _press(f: InputFrame, what: String, t: float, hold: float) -> void:
	match what:
		"attack":
			f.attack_pressed = true
			f.attack_held = true
		"guard":
			f.guard_pressed = true
			f.guard_held = true
		"tech":
			f.tech_pressed = true
			f.tech_held = true
	_ends[what] = t + hold


func _release_now(f: InputFrame, what: String) -> void:
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


func _release_due(f: InputFrame, t: float) -> void:
	for what in ["attack", "guard", "tech"]:
		if _ends.has(what) and t >= float(_ends[what]):
			_release_now(f, what)


## Technique drag aim toward a world direction, expressed in the camera frame the
## PlayerController expects (x right, y forward).
func _aim_at(g: Game, f: InputFrame, dir: Vector3) -> void:
	dir.y = 0.0
	if dir.length() < 0.01:
		return
	dir = dir.normalized()
	# The game applies this tick's cam_delta before mapping the aim: use that yaw.
	var yaw := g.cam.yaw - f.cam_delta.x
	var fwd := Vector3(sin(yaw), 0.0, cos(yaw))
	var right := fwd.cross(Vector3.UP)
	f.tech_aim = Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)
	f.tech_aim_active = true


# ------------------------------------------------------------------ trace (SHOWCASE_TRACE=1)

func _trace_tick(g: Game, t: float) -> void:
	for a in [g.player, g.opponent]:
		var act := "-" if a.action == null else "%s/%s" % [a.action.id, a.action.phase_name()]
		var st := "%s hp%.0f bal%.0f foc%.0f %s stun:%s held:%d" % [a.name, a.health, a.balance, a.focus, act, a.stun_kind if a.stun > 0.0 else "", a.held_body]
		var key := "%s|%s|%s|%d" % [a.name, act, a.stun_kind if a.stun > 0.0 else "", a.held_body]
		if _log_state.get(a.name, "") != key:
			_log_state[a.name] = key
			print("t=%.2f %s" % [t, st])
	for b in g.world.bodies:
		if b.alive and b.is_stone() and b.form != Sim.Form.WALL:
			var k := "b%d" % b.id
			var v := "%d|%s|%d" % [b.attack_owner if b.attack_id != 0 else -9, b.last_verb, b.controller]
			if _log_state.get(k, "") != v:
				_log_state[k] = v
				print("t=%.2f   body %d %.0fkg owner=%d verb=%s ctl=%d pos=%s" % [t, b.id, b.mass, b.attack_owner if b.attack_id != 0 else -9, b.last_verb, b.controller, str(b.pos.snapped(Vector3.ONE * 0.1))])
