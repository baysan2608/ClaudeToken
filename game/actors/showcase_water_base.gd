class_name WaterShowcase
extends RefCounted
## Shared choreography engine of the Water showcases (showcase_water_ice / _mist / _plant, and the new beats of
## showcase_water): a list of timed beats, each a Callable(g, f, bt) writing the InputFrame exactly like the touch
## layer would (press / hold / flick / drag), so every move goes through PlayerController and the real sim.
## Used by `--autoplay=show_<name>[:seconds]` (see Autoplay). Interface: scenario(), bind(g), frame(g, f, t), wants_shot().
##
## A beat is {n: name, d: seconds, e: Callable(g) (enter), f: Callable(g, f, bt), s: shot name (taken once, mid beat)}.

const DT := 1.0 / 60.0

var beats: Array = []
var _i := -1
var _bt0 := 0.0
var _shot := ""
var _shot_done := {}
var _cam_off := -0.55            # camera yaw offset from the player->rival line (rad): a 3/4 view
var _cam_pitch := 0.30
var _built := false
var _trace := OS.has_environment("SHOWCASE_TRACE")

# overridable ----------------------------------------------------------------

func scenario() -> String:
	return "spar"


## Sub-element the fighter plays (selected with a real sub_select edge at the start).
func sub_index() -> int:
	return 0


func player_start() -> Vector3:
	return Vector3(5.4, 0.0, 0.8)


func rival_start() -> Vector3:
	return Vector3(3.6, 0.0, -4.2)


func rival_element() -> int:
	return Sim.Element.EARTH


func build() -> void:
	pass


# engine ---------------------------------------------------------------------

func bind(g: Game) -> void:
	g.progress.lab_mode = true
	var p := g.player
	var o := g.opponent
	p.kit = g.progress.kit()
	p.element = Sim.Element.WATER
	p.subs[Sim.Element.WATER] = 0
	p.pos = player_start()
	o.pos = rival_start()
	p.facing = atan2(o.pos.x - p.pos.x, o.pos.z - p.pos.z)
	o.facing = atan2(p.pos.x - o.pos.x, p.pos.z - o.pos.z)
	o.element = rival_element()
	o.kit = {"lightning": true, "magma": true}
	g.ai.cfg["drill"] = "passive"
	g.ai.cfg["reaction"] = 99.0
	g.ai.cfg["interval"] = 999.0
	for a in [p, o]:
		g.fighters[a.id].snap(a)
	g.fighters[p.id].set_accent(UiStyle.element_color(p.element))
	var to := o.pos - p.pos
	g.cam.snap_to(p.pos, p.pos + to.rotated(Vector3.UP, _cam_off))
	if not _built:
		_built = true
		build()
	_go(0, 0.0, g)


func wants_shot() -> String:
	var s := _shot
	_shot = ""
	return s


func frame(g: Game, f: InputFrame, t: float) -> void:
	f.move = Vector2.ZERO
	f.attack_held = false
	f.guard_held = false
	f.tech_held = false
	f.evade_held = false
	g.opponent.health = maxf(g.opponent.health, 55.0)       # the showcase never ends in a knockout (the scenario would reset)
	g.player.health = maxf(g.player.health, 70.0)
	if _i < 0 or _i >= beats.size():
		_steer_cam(g, f)
		return
	var b: Dictionary = beats[_i]
	var bt := t - _bt0
	if _trace and g.world.tick % 6 == 0:
		_trace_line(g, t, String(b.n))
	if bt >= float(b.d):
		_go(_i + 1, t, g)
		if _i >= beats.size():
			_steer_cam(g, f)
			return
		b = beats[_i]
		bt = 0.0
	var fn: Callable = b.f
	if fn.is_valid():
		fn.call(g, f, bt)
	var sn := String(b.get("s", ""))
	if sn != "" and bt >= float(b.d) * 0.55 and not _shot_done.has(sn):
		_shot_done[sn] = true
		_shot = sn
	_steer_cam(g, f)


func _go(i: int, t: float, g: Game) -> void:
	_i = i
	_bt0 = t
	if i >= 0 and i < beats.size():
		var e: Variant = beats[i].get("e")
		if e is Callable and (e as Callable).is_valid():
			(e as Callable).call(g)


func beat(name: String, dur: float, fn: Callable, enter: Callable = Callable(), shot: String = "") -> void:
	beats.append({"n": name, "d": dur, "f": fn, "e": enter, "s": shot})


# input helpers (what a thumb does) ---------------------------------------------

## Attack button: pressed at the start of the beat, held `hold` s, released; an optional flick gesture on the press.
static func attack(f: InputFrame, bt: float, hold: float = 0.05, gesture: int = 0, at: float = 0.0) -> void:
	var x := bt - at
	if x < 0.0:
		return
	if x < DT * 1.5:
		f.attack_pressed = true
		f.attack_gesture = gesture
	f.attack_held = x < hold
	f.attack_released = x >= hold and x < hold + DT * 1.5


static func guard(f: InputFrame, bt: float, hold: float, at: float = 0.0) -> void:
	var x := bt - at
	if x < 0.0:
		return
	if x < DT * 1.5:
		f.guard_pressed = true
	f.guard_held = x < hold
	f.guard_released = x >= hold and x < hold + DT * 1.5


## A guard flick (Sim.Gesture UP = push, DOWN = sink) at `at` seconds into the beat; the guard stays held.
static func guard_flick(f: InputFrame, bt: float, at: float, gesture: int) -> void:
	if bt >= at and bt < at + DT * 1.5:
		f.guard_gesture = gesture
		f.guard_held = true


static func tech(f: InputFrame, bt: float, hold: float, at: float = 0.0) -> void:
	var x := bt - at
	if x < 0.0:
		return
	if x < DT * 1.5:
		f.tech_pressed = true
	f.tech_held = x < hold
	f.tech_released = x >= hold and x < hold + DT * 1.5


## Aim drag while the technique holds (sweeps once, then lets go so the release goes to the locked target).
static func aim_sweep(f: InputFrame, bt: float, from: float, to: float, amp: float = 0.7) -> void:
	if bt > from and bt < to:
		f.tech_aim_active = true
		f.tech_aim = Vector2(sin((bt - from) * 2.0 * PI / (to - from)) * amp, 0.7)


static func evade(f: InputFrame, bt: float, dir: Vector2, hold: float = 0.0, at: float = 0.0) -> void:
	var x := bt - at
	if x < 0.0:
		return
	if x < DT * 1.5:
		f.evade_pressed = true
	f.move = dir if x < maxf(hold, DT * 3.0) else Vector2.ZERO
	f.evade_held = x < hold


static func select_sub(f: InputFrame, bt: float, sub: int, at: float = 0.0) -> void:
	if bt >= at and bt < at + DT * 1.5:
		f.sub_select = sub


## Walk toward a world point (camera-relative stick) until within `stop` metres.
func walk_to(g: Game, f: InputFrame, p: Vector3, stop: float = 0.4, speed: float = 0.55) -> bool:
	var d := p - g.player.pos
	d.y = 0.0
	if d.length() <= stop:
		return true
	f.move = _stick(g, d.normalized() * speed)
	return false


func face_rival_move(g: Game, f: InputFrame, stop: float, speed: float = 0.6) -> bool:
	var d := g.opponent.pos - g.player.pos
	d.y = 0.0
	if d.length() <= stop:
		return true
	f.move = _stick(g, d.normalized() * speed)
	return false


## World-space direction -> camera-relative stick (x right, y forward).
func _stick(g: Game, dir: Vector3) -> Vector2:
	var fwd := g.cam.forward_flat()
	var right := fwd.cross(Vector3.UP)
	return Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)


## Camera drag: hold a 3/4 view of the exchange. A tiny alternating nudge keeps the rig's idle auto-centre from taking over.
func _steer_cam(g: Game, f: InputFrame) -> void:
	var to := g.opponent.pos - g.player.pos
	to.y = 0.0
	var want := atan2(to.x, to.z) + _cam_off
	var err := wrapf(want - g.cam.yaw, -PI, PI)
	var dy := clampf(err * 0.07, -0.02, 0.02)
	if absf(dy) < 2e-4:
		dy = 2e-4 if g.world.tick % 2 == 0 else -2e-4
	var dp := clampf((_cam_pitch - g.cam.pitch) * 0.07, -0.01, 0.01)
	f.cam_delta = Vector2(-dy, -dp)


# world helpers (cues the rival would make) --------------------------------------

## The rival throws a stone at the player (booked from the ground like any stone).
func throw_stone(g: Game, mass: float = 20.0, speed: float = 17.0, from_dist: float = 8.0, temp: float = Sim.AMBIENT_C) -> MatBody:
	var w := g.world
	var p := g.player
	var o := g.opponent
	var dir := o.pos - p.pos
	dir.y = 0.0
	dir = dir.normalized()
	var from := p.chest() + dir * from_dist
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, mass, from, "showcase", temp)
	w.mass_ledger.ground_taken += mass
	b.vel = (p.chest() - from).normalized() * speed
	b.gravity_scale = 0.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = o.id
	b.hit_set[o.id] = true
	b.damage = 10.0
	b.balance_damage = 20.0
	return b


## The rival pours a lava wave at the player (energy booked as generated heat).
func pour_lava(g: Game, mass: float = 20.0, from_dist: float = 9.0) -> MatBody:
	var w := g.world
	var p := g.player
	var o := g.opponent
	var dir := p.pos - o.pos
	dir.y = 0.0
	dir = dir.normalized()
	var from := p.pos - dir * from_dist
	from.y = w.arena.ground_height(from.x, from.z, 1.0)
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, from, "showcase")
	w.mass_ledger.ground_taken += mass
	w.ledger.generated += Thermal.heat(b, mass * (Sim.STONE_C * 980.0 + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = dir
	b.wave_budget = 14.0
	b.wave_width = 1.8
	b.vel = dir * 7.5
	b.attack_id = w.new_attack_id()
	b.attack_owner = o.id
	b.hit_set[o.id] = true
	b.damage = 14.0
	b.balance_damage = 40.0
	return b


## Keeps the rival where the choreography wants it (a hit knocks them back).
func rival_to(g: Game, p: Vector3) -> void:
	var o := g.opponent
	o.pos = Vector3(p.x, o.pos.y, p.z)
	o.vel = Vector3.ZERO
	o.facing = atan2(g.player.pos.x - o.pos.x, g.player.pos.z - o.pos.z)
	g.fighters[o.id].snap(o)


func player_to(g: Game, p: Vector3) -> void:
	var a := g.player
	a.pos = Vector3(p.x, a.pos.y, p.z)
	a.vel = Vector3.ZERO
	a.facing = atan2(g.opponent.pos.x - a.pos.x, g.opponent.pos.z - a.pos.z)
	g.fighters[a.id].snap(a)


## SHOWCASE_TRACE=1: one line per 6 ticks (beat, both fighters' action / phase / tier, key statuses, bodies).
func _trace_line(g: Game, t: float, beat_name: String) -> void:
	var line := "t=%.2f %-12s" % [t, beat_name]
	for a in [g.player, g.opponent]:
		var act := "-" if a.action == null else "%s/%s/T%d" % [a.action.id, a.action.phase_name(), a.action.tier()]
		line += " | %s %s hp%.0f bal%.0f foc%.0f skin%.1f st%s" % [a.name, act, a.health, a.balance, a.focus, a.water_carried, a.status.keys()]
	var n := 0
	for b in g.world.bodies:
		if b.alive and b.form != Sim.Form.POOL:
			n += 1
			if n <= 6:
				line += " | " + b.describe()
	print(line)
