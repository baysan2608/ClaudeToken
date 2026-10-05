extends RefCounted
## Water showcase choreography for `--autoplay=show_water[:seconds]` (loaded by
## actors/autoplay.gd). Everything goes through real InputFrames, exactly like touch input;
## the camera is steered with cam_delta like a player's drag.
## Beats: walk to the pool -> draw from it and shape it (hold technique, drag aim) -> release a
## stream at the rival -> water lash -> waterskin shield (guard) blocks two flares in steam ->
## let go (the water flows back into the waterskin) -> counter with two lashes -> ice lance
## (freezes, shatters) -> draw + stream again while the ice shards melt into a puddle.
## The rival is kept passive (ai cfg) except during the shield beat.
## Act two (the moveset kit, docs/kits/water.md): Torrent (hold 1.1 s) -> Water Bullet (flick up) and the Pressure Jet (hold)
## -> Tidal Rush carries a thrown stone back at the rival (flick down) -> Spray Fan (flick side) -> Surge Orb (guard flick up)
## and Slick (guard flick down) -> Riptide Step (evade) -> Maelstrom Lash (hold 1.9 s, rival close).

const PLAYER_START := Vector3(5.4, 0.0, 0.8)
const RIVAL_START := Vector3(3.6, 0.0, -4.2)

var _beat := ""
var _bt := 0.0           # time the current beat started
var _shot := ""
var _flare_seen := -1.0  # time the rival's latest flare started
var _flare_id := 0
var _flares := 0
var _cam_off := -0.55    # camera yaw offset from the player->rival line (rad): a 3/4 view
var _cam_pitch := 0.30
var _stone_thrown := false


func scenario() -> String:
	return "spar"


func bind(g: Game) -> void:
	g.progress.lab_mode = true
	var p := g.player
	var o := g.opponent
	p.kit = g.progress.kit()
	p.element = Sim.Element.WATER
	p.pos = PLAYER_START + Vector3(-1.2, 0.0, 2.5)
	o.pos = RIVAL_START
	p.facing = atan2(o.pos.x - p.pos.x, o.pos.z - p.pos.z)
	o.facing = atan2(p.pos.x - o.pos.x, p.pos.z - o.pos.z)
	o.element = Sim.Element.FIRE
	# The rival stands its ground and doesn't react until the shield beat.
	g.ai.cfg["drill"] = "passive"
	g.ai.cfg["reaction"] = 99.0
	for a in [p, o]:
		g.fighters[a.id].snap(a)
	g.fighters[p.id].set_accent(UiStyle.element_color(p.element))
	var to := o.pos - p.pos
	g.cam.snap_to(p.pos, p.pos + to.rotated(Vector3.UP, _cam_off))
	_go("intro", 0.0)


func wants_shot() -> String:
	var s := _shot
	_shot = ""
	return s


func frame(g: Game, f: InputFrame, t: float) -> void:
	var p := g.player
	var o := g.opponent
	var bt := t - _bt
	if _beat in ["torrent", "bullet", "jet", "tidal", "spray", "orb", "slick", "riptide", "maelstrom", "end"]:
		_prime(g, bt)
	f.move = Vector2.ZERO
	if OS.has_environment("SHOW_TRACE") and g.world.tick % 6 == 0:
		_trace(g, t)
	match _beat:
		"intro":
			# Walk up to the pool's edge.
			var d0 := PLAYER_START - p.pos
			d0.y = 0.0
			if d0.length() > 0.15 and bt < 2.5:
				f.move = _stick(g, d0.normalized() * 0.55)
			elif bt >= 1.5:
				f.tech_pressed = true
				f.tech_held = true
				_go("draw", t)
		"draw":
			# Hold: water is drawn from the pool to the hands; drag the aim to shape it,
			# then release: the stream flies at the rival.
			f.tech_held = true
			_shape(f, bt, 1.1, 2.1)
			if bt > 1.2:
				_shot = "draw"
			if bt >= 2.7:
				f.tech_held = false
				f.tech_released = true
				_go("stream", t)
		"stream":
			if bt > 0.3:
				_shot = "stream"
			if bt >= 1.1:
				_go("approach", t)
		"approach":
			var d := o.pos - p.pos
			d.y = 0.0
			if d.length() > 3.3 and bt < 2.0:
				f.move = _stick(g, d.normalized() * 0.7)
			else:
				_go("lash", t)
		"lash":
			_tap_attack(f, bt)
			if bt > 0.2 and bt < 0.3:
				_shot = "lash"
			if bt >= 1.0:
				f.guard_pressed = true
				f.guard_held = true
				_go("shield", t)
		"shield":
			# Guard with water in the waterskin raises a water shield; now the rival fights back
			# with close-range flares, which boil on the water into steam.
			f.guard_held = true
			if bt >= 0.3 and _flares == 0 and g.ai.cfg.drill == "passive":
				# Any drill name other than the built-in ones = free sparring at a fixed interval.
				g.ai.cfg["drill"] = "flares"
				g.ai.cfg["interval"] = 0.9
				g.ai.cfg["elements"] = [Sim.Element.FIRE]
				g.ai.cfg["aggression"] = 1.5
			if o.action != null and o.action.id == "fire_attack" and o.action.attack_id != _flare_id:
				_flare_id = o.action.attack_id
				_flares += 1
				_flare_seen = t
				if _flares >= 2:
					g.ai.cfg["drill"] = "passive"
			if _flares > 0 and t - _flare_seen > 0.15 and t - _flare_seen < 0.3:
				_shot = "steam%d" % _flares
			if (_flares >= 2 and t - _flare_seen > 1.2) or bt > 4.5:
				g.ai.cfg["drill"] = "passive"
				_go("drop", t)
		"drop":
			# Letting go: the shield water flows back into the waterskin.
			f.guard_held = false
			f.guard_released = bt < Sim.DT * 1.5
			if bt >= 0.6:
				_go("counter", t)
		"counter":
			# Counter-attack once the flares stop: two quick lashes.
			_tap_attack(f, bt)
			if bt >= 0.75:
				_go("counter_b", t)
		"counter_b":
			_tap_attack(f, bt)
			if bt >= 1.0:
				_go("lance", t)
		"lance":
			# Hold attack: 4 kg of water freezes into a shard; release to throw it. It shatters.
			_hold_attack(f, bt, 0.7)
			if bt > 0.75 and bt < 0.85:
				_shot = "lance"
			if bt >= 1.3:
				f.tech_pressed = true
				f.tech_held = true
				_go("draw2", t)
		"draw2":
			# Draw and shape again while the ice shards at the rival's feet melt.
			f.tech_held = true
			_shape(f, bt, 0.9, 1.9)
			if bt >= 2.1:
				f.tech_held = false
				f.tech_released = true
				_go("close", t)
		"close":
			# Step in to watch the last of the ice melt into the puddle.
			var d2 := o.pos - p.pos
			d2.y = 0.0
			if bt > 0.4 and d2.length() > 2.6 and bt < 1.6:
				f.move = _stick(g, d2.normalized() * 0.4)
			if not _any_ice(g) and bt > 0.4:
				_go("torrent", t)
		"torrent":
			# Hold the strike past 1.0 s: T2 Torrent, a 10 kg water slug.
			_prime(g, bt)
			_attack(f, bt, 1.12)
			if bt > 0.9 and bt < 1.0:
				_shot = "torrent"
			if bt >= 2.4:
				_go("bullet", t)
		"bullet":
			_attack(f, bt, 0.05, Sim.Gesture.UP)
			if bt >= 1.2:
				_go("jet", t)
		"jet":
			# Hold the thrust to T2: the Pressure Jet stays connected to the caster.
			_attack(f, bt, 1.12, Sim.Gesture.UP)
			if bt > 1.3 and bt < 1.4:
				_shot = "jet"
			if bt >= 3.0:
				_go("tidal", t)
		"tidal":
			# The rival throws a stone; Tidal Rush captures it and carries it back.
			_attack(f, bt, 0.05, Sim.Gesture.DOWN)
			if bt > 0.45 and not _stone_thrown:
				_stone_thrown = true
				_throw_stone(g)
			if bt > 1.3 and bt < 1.4:
				_shot = "tidal"
			if bt >= 4.2:
				_stone_thrown = false
				_go("spray", t)
		"spray":
			_attack(f, bt, 0.05, Sim.Gesture.SIDE)
			if bt > 0.3 and bt < 0.4:
				_shot = "spray"
			if bt >= 1.6:
				_go("orb", t)
		"orb":
			# Guard, then flick the guard up: the shield is hurled as an orb.
			f.guard_held = bt < 1.4
			f.guard_pressed = bt < Sim.DT * 1.5
			if bt > 0.7 and bt < 0.7 + Sim.DT * 1.5:
				f.guard_gesture = Sim.Gesture.UP
				f.guard_held = true
			if bt >= 2.2:
				_go("slick", t)
		"slick":
			f.guard_held = bt < 1.2
			f.guard_pressed = bt < Sim.DT * 1.5
			if bt > 0.5 and bt < 0.5 + Sim.DT * 1.5:
				f.guard_gesture = Sim.Gesture.DOWN
				f.guard_held = true
			if bt > 1.6 and bt < 1.7:
				_shot = "slick"
			if bt >= 2.4:
				_go("riptide", t)
		"riptide":
			f.evade_pressed = bt < Sim.DT * 1.5
			f.move = _stick(g, Vector3(1, 0, 0.4).normalized()) if bt < 0.2 else Vector2.ZERO
			if bt >= 1.4:
				_go("maelstrom", t)
		"maelstrom":
			# Close in and whip all around.
			var d3 := o.pos - p.pos
			d3.y = 0.0
			if bt < 0.8 and d3.length() > 3.0:
				f.move = _stick(g, d3.normalized() * 0.6)
			else:
				_attack(f, bt - 0.8, 1.9)
			if bt > 2.8 and bt < 2.9:
				_shot = "maelstrom"
			if bt >= 4.2:
				_go("end", t)
		"end":
			if bt > 0.3 and bt < 0.4:
				_shot = "melted"
	_steer_cam(g, f)


func _any_ice(g: Game) -> bool:
	for b in g.world.bodies:
		if b.alive and b.is_water() and b.phase == Sim.Phase.FROZEN:
			return true
	return false


## Aim drag while holding the technique: sweep side to side once (shaping), then let go of
## the drag so the release goes to the locked target.
func _shape(f: InputFrame, bt: float, from: float, to: float) -> void:
	if bt > from and bt < to:
		f.tech_aim_active = true
		f.tech_aim = Vector2(sin((bt - from) * 2.0 * PI / (to - from)) * 0.7, 0.7)


## Attack press at the start of the beat, held `hold` s, with an optional flick gesture on the press.
func _attack(f: InputFrame, bt: float, hold: float, gesture: int = 0) -> void:
	if bt < 0.0:
		return
	if bt < Sim.DT * 1.5:
		f.attack_pressed = true
		f.attack_gesture = gesture
	f.attack_held = bt < hold
	f.attack_released = bt >= hold and bt < hold + Sim.DT * 1.5


## Keeps the waterskin topped up and the rival in range for act two (the showcase is about the moves, not the economy).
func _prime(g: Game, bt: float) -> void:
	if bt < Sim.DT * 2.0:
		g.player.water_carried = 6.0
		g.player.focus = 100.0
	g.opponent.health = maxf(g.opponent.health, 55.0)      # no knockout: the scenario would reset mid-show


## The rival throws a stone at the player (booked from the ground like any stone).
func _throw_stone(g: Game) -> void:
	var w := g.world
	var p := g.player
	var o := g.opponent
	var dir := o.pos - p.pos
	dir.y = 0.0
	dir = dir.normalized()
	var from := p.chest() + dir * 8.0
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, from, "showcase")
	w.mass_ledger.ground_taken += 20.0
	b.vel = (p.chest() - from).normalized() * 17.0
	b.gravity_scale = 0.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = o.id
	b.hit_set[o.id] = true
	b.damage = 10.0
	b.balance_damage = 20.0


func _tap_attack(f: InputFrame, bt: float) -> void:
	if bt < Sim.DT * 1.5:
		f.attack_pressed = true
		f.attack_held = true
	elif bt < Sim.DT * 3.5:
		f.attack_held = false
		f.attack_released = bt < Sim.DT * 2.5


func _hold_attack(f: InputFrame, bt: float, hold: float) -> void:
	f.attack_held = bt < hold
	f.attack_pressed = bt < Sim.DT * 1.5
	f.attack_released = bt >= hold and bt < hold + Sim.DT * 1.5


## Camera drag: hold a 3/4 view of the exchange (player one side, rival the other, pool in
## view). A tiny alternating nudge keeps the rig's idle auto-centre from taking over.
func _steer_cam(g: Game, f: InputFrame) -> void:
	var to := g.opponent.pos - g.player.pos
	to.y = 0.0
	var want := atan2(to.x, to.z) + _cam_off
	var err := wrapf(want - g.cam.yaw, -PI, PI)
	var dy := clampf(err * 0.07, -0.02, 0.02)
	if absf(dy) < 2e-4:
		dy = 2e-4 if g.world.tick % 2 == 0 else -2e-4
	var dp := clampf((_cam_pitch - g.cam.pitch) * 0.07, -0.01, 0.01)
	# add_input: yaw -= delta.x, pitch -= delta.y
	f.cam_delta = Vector2(-dy, -dp)


func _go(beat: String, t: float) -> void:
	_beat = beat
	_bt = t


func _trace(g: Game, t: float) -> void:
	var line := "t=%.2f %s cam=%.2f" % [t, _beat, g.cam.yaw]
	for a in [g.player, g.opponent]:
		var act := "-" if a.action == null else "%s/%s" % [a.action.id, a.action.phase_name()]
		line += " | %s %s hp%.0f bal%.0f foc%.0f skin%.1f stun%.2f" % [a.name, act, a.health, a.balance, a.focus, a.water_carried, a.stun]
	for b in g.world.bodies:
		if b.alive and b.form != Sim.Form.POOL:
			line += " | " + b.describe()
	print(line)


## World-space direction -> camera-relative stick (x right, y forward).
func _stick(g: Game, dir: Vector3) -> Vector2:
	var fwd := g.cam.forward_flat()
	var right := fwd.cross(Vector3.UP)
	return Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)
