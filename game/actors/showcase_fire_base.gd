class_name FireShowcase
extends RefCounted
## Shared choreography engine of the Fire sub-element showcases (showcase_fire_blue / _lightning / _combustion): a list
## of timed beats, each a Callable(g, f, bt) writing the InputFrame exactly like the touch layer would (press / hold /
## flick / drag), so every move goes through PlayerController and the real sim. The rival's cues (a thrown stone, a wall,
## a bolt) are set up in the world directly, booked in the ledgers like the real moves.
## Used by `--autoplay=show_<name>[:seconds]` (see Autoplay). Interface: scenario(), bind(g), frame(g, f, t), wants_shot().
## SHOWCASE_TRACE=1 prints one line per 6 ticks.

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


func scenario() -> String:
	return "spar"


func sub_index() -> int:
	return 0


func player_start() -> Vector3:
	return Vector3(1.5, 0.0, 5.0)


func rival_start() -> Vector3:
	return Vector3(0.5, 0.0, -3.0)


func rival_element() -> int:
	return Sim.Element.EARTH


func player_kit(g: Game) -> Dictionary:
	var k := g.progress.kit()
	k.erase("lightning")   # Lab kits use the Lightning sub-element (the legacy flag turns long Flame holds into bolts)
	return k


func build() -> void:
	pass


func bind(g: Game) -> void:
	g.progress.lab_mode = true
	var p := g.player
	var o := g.opponent
	p.kit = player_kit(g)
	p.element = Sim.Element.FIRE
	p.subs[Sim.Element.FIRE] = 0
	p.pos = player_start()
	o.pos = rival_start()
	p.facing = atan2(o.pos.x - p.pos.x, o.pos.z - p.pos.z)
	o.facing = atan2(p.pos.x - o.pos.x, p.pos.z - o.pos.z)
	o.element = rival_element()
	o.kit = {"magma": true}
	if g.ai != null:
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
	g.opponent.health = maxf(g.opponent.health, 55.0)       # the showcase never ends in a knockout
	g.player.health = maxf(g.player.health, 70.0)
	g.player.focus = maxf(g.player.focus, 60.0)             # a showcase, not a resource drill
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
	if sn != "" and bt >= float(b.d) * float(b.get("at", 0.55)) and not _shot_done.has(sn):
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


## A beat: name, duration, input Callable(g, f, bt), optional enter Callable(g), optional shot (taken at `at` x duration).
func beat(name: String, dur: float, fn: Callable, enter: Callable = Callable(), shot: String = "", at: float = 0.55) -> void:
	beats.append({"n": name, "d": dur, "f": fn, "e": enter, "s": shot, "at": at})


# input helpers (what a thumb does) ---------------------------------------------

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


## Technique drag aim toward a world direction (camera frame: x right, y forward).
func aim_at(g: Game, f: InputFrame, dir: Vector3) -> void:
	dir.y = 0.0
	if dir.length() < 0.01:
		return
	dir = dir.normalized()
	var yaw := g.cam.view_yaw() - f.cam_delta.x
	var fwd := Vector3(sin(yaw), 0.0, cos(yaw))
	var right := fwd.cross(Vector3.UP)
	f.tech_aim = Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)
	f.tech_aim_active = true


func _stick(g: Game, dir: Vector3) -> Vector2:
	var fwd := g.cam.forward_flat()
	var right := fwd.cross(Vector3.UP)
	return Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)


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


# world helpers (the rival's cues, booked like the real moves) -----------------------

func throw_at_player(g: Game, mat: int, mass: float, speed: float, from_dist: float = 8.0, temp: float = Sim.AMBIENT_C) -> MatBody:
	var w := g.world
	var p := g.player
	var o := g.opponent
	var dir := o.pos - p.pos
	dir.y = 0.0
	dir = dir.normalized()
	var from := p.chest() + dir * from_dist
	var b := w.spawn_body(mat, Sim.Form.CHUNK, mass, from, "showcase", temp)
	if mat == Sim.Mat.STONE or mat == Sim.Mat.SAND or mat == Sim.Mat.GLASS:
		w.mass_ledger.ground_taken += mass
	if mat == Sim.Mat.WATER and temp < 0.0:
		var e0 := b.thermal_energy()
		b.liquid = 0.0
		b.phase = Sim.Phase.FROZEN
		w.ledger.freeze_dump += b.thermal_energy() - e0
		b.form = Sim.Form.SHARD
	b.vel = (p.chest() - from).normalized() * speed
	b.gravity_scale = 0.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = o.id
	b.hit_set[o.id] = true
	b.damage = 10.0
	b.balance_damage = 20.0
	return b


## The rival raises a Bulwark between them (120 kg from the ground), standing for `life` s.
func rival_wall(g: Game, dist_from_player: float = 3.2, life: float = 8.0) -> MatBody:
	var w := g.world
	var p := g.player
	var o := g.opponent
	var dir := o.pos - p.pos
	dir.y = 0.0
	dir = dir.normalized()
	var at := p.pos + dir * dist_from_player
	at.y = w.arena.ground_height(at.x, at.z, p.pos.y + 0.5)
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, Sim.WALL_MASS, at, "ground@showcase")
	w.mass_ledger.ground_taken += Sim.WALL_MASS
	b.wall_yaw = atan2(-dir.x, -dir.z)
	b.wall_half = Vector3(1.1, 0.75, 0.28)
	b.wall_rise = 0.0
	b.static_body = true
	b.props["standing"] = life
	b.props["rise_time"] = 0.2
	b.touch(o.id, "wall", w.tick)
	w.emit("wall", {"actor": o.id, "body": b.id})
	return b


func puddle(g: Game, at: Vector3, kg: float = 5.0) -> MatBody:
	var w := g.world
	var b := w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, kg, Vector3(at.x, w.arena.ground_height(at.x, at.z, 0.5), at.z), "showcase")
	b.update_radius_puddle()
	return b


func rival_to(g: Game, p: Vector3) -> void:
	var o := g.opponent
	o.pos = Vector3(p.x, g.world.arena.ground_height(p.x, p.z, o.pos.y + 0.5), p.z)
	o.vel = Vector3.ZERO
	o.facing = atan2(g.player.pos.x - o.pos.x, g.player.pos.z - o.pos.z)
	g.fighters[o.id].snap(o)


func player_to(g: Game, p: Vector3) -> void:
	var a := g.player
	a.pos = Vector3(p.x, g.world.arena.ground_height(p.x, p.z, a.pos.y + 0.5), p.z)
	a.vel = Vector3.ZERO
	a.facing = atan2(g.opponent.pos.x - a.pos.x, g.opponent.pos.z - a.pos.z)
	g.fighters[a.id].snap(a)


func _trace_line(g: Game, t: float, beat_name: String) -> void:
	var line := "t=%.2f %-12s" % [t, beat_name]
	for a in [g.player, g.opponent]:
		var act := "-" if a.action == null else "%s/%s/T%d" % [a.action.id, a.action.phase_name(), a.action.tier()]
		line += " | %s %s hp%.0f foc%.0f heat%.0f st%.0f %s" % [a.name, act, a.health, a.focus, a.heat_reserve, a.static_charge, a.status.keys()]
	var n := 0
	for b in g.world.bodies:
		if b.alive and b.form != Sim.Form.POOL:
			n += 1
			if n <= 6:
				line += " | " + b.describe()
	print(line)
