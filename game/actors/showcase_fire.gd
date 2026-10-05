extends RefCounted
## Fire showcase choreography for `--autoplay=show_fire[:seconds]` (loaded by
## actors/autoplay.gd). Every move goes through real InputFrames, exactly like touch
## input; the camera is steered with cam_delta like a player's drag.
## Conductors lab (no rival; practice targets), with the lab kit (magma, heat draw,
## lightning). Beats:
##   flare jab x2 (tap) -> blaze (hold) on a practice target ->
##   magma grip on a loose stone, melt it into a molten blob, drag-aim, release: the lava
##   pours along the ground into the target by the cover wall and pools against the wall ->
##   heat draw: pull the heat back out until the lava sets into rock (reserve fills) ->
##   vent the reserve -> run to the pool ->
##   lightning: hold attack until it crackles (aim line), release at a target standing in
##   the pool: the bolt conducts through the water into the other target -> a second bolt ->
##   (moveset, sub 0 Flame) Fire Column (hold 1.4 s) leaves a burning field -> Fireball (flick up) -> Fire Line
##   (flick down) -> Fire Fan (flick side) -> SCORCH: a stone wall in front, hold the technique: its face slumps.
## The other sub-elements have their own showcases: showcase_fire_blue / _lightning / _combustion.

const THERMAL_SPOT := Vector3(0.3, 0.0, 2.7)     # start; the thermal beats happen here
const POOL_SPOT := Vector3(4.4, 0.0, 2.6)        # the lightning beats happen here
const FLARE_DUMMY := Vector3(1.95, 0.0, -0.16)   # Target 3, moved off the metal plate
const WALL_DUMMY := Vector3(-3.1, 0.0, 0.05)     # Target 4, in front of the cover wall
const POOL_1 := Vector3(8.0, -0.3, 0.4)          # Target 1, in the pool (first bolt)
const POOL_2 := Vector3(10.4, -0.3, 1.9)         # Target 2, in the pool (second bolt)
const STONE_POS := Vector3(-1.4, 0.25, 2.1)      # loose stone, out of the flare cones
## Thermal beats: the camera looks past the player's right shoulder, so the stone held in
## front of the chest (facing the stone, to the west) is seen in profile, not hidden.
const THERMAL_CAM_YAW := 2.97
const POOL_T1 := 2                                # actor ids (player is 1)
const POOL_T2 := 3
const FLARE_T := 4
const WALL_T := 5

var _beat := ""
var _bt := 0.0            # time the current beat started
var _shot := ""
var _cam_yaw := 3.4       # wanted camera yaw (rad)
var _cam_pitch := 0.30
var _cam_rate := 0.03     # max yaw change per tick
var _stone := -1
var _want_target := FLARE_T
var _trace := OS.has_environment("SHOW_TRACE")
var _log := {}


func scenario() -> String:
	return "conduction"


func bind(g: Game) -> void:
	g.progress.lab_mode = true
	var w := g.world
	var p := g.player
	p.kit = g.progress.kit()
	p.element = Sim.Element.FIRE
	_place(w, p, THERMAL_SPOT)
	_place(w, w.get_actor(FLARE_T), FLARE_DUMMY)
	_place(w, w.get_actor(WALL_T), WALL_DUMMY)
	_place(w, w.get_actor(POOL_T1), POOL_1)
	_place(w, w.get_actor(POOL_T2), POOL_2)
	for a in w.actors:
		if a.is_dummy:
			a.facing = _yaw_to(a.pos, p.pos)
	var d := w.get_actor(FLARE_T)
	p.facing = _yaw_to(p.pos, d.pos)
	p.lock_target = FLARE_T
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, Sim.STONE_SHOT_MASS, STONE_POS, "scenario")
	b.on_ground = true
	_stone = b.id
	w.take_events()
	for a in w.actors:
		g.fighters[a.id].snap(a)
	g.fighters[p.id].set_accent(UiStyle.element_color(p.element))
	_cam_yaw = _yaw_to(p.pos, d.pos) + 0.45
	g.cam.snap_to(p.pos, p.pos + _dir_yaw(_cam_yaw))
	_go("intro", 0.0)


func _place(w: CombatWorld, a: ActorState, pos: Vector3) -> void:
	a.pos = pos
	a.pos.y = w.arena.ground_height(pos.x, pos.z, pos.y + 0.5)
	a.ground_y = a.pos.y


func wants_shot() -> String:
	var s := _shot
	_shot = ""
	return s


func frame(g: Game, f: InputFrame, t: float) -> void:
	var w := g.world
	var p := g.player
	var bt := t - _bt
	f.move = Vector2.ZERO
	match _beat:
		"intro":
			if bt >= 0.9:
				_go("jab1", t)
		# ---------------------------------------------------------------- flare / blaze
		"jab1":
			_tap_attack(f, bt)
			if bt > 0.15 and bt < 0.25:
				_shot = "flare_jab"
			if bt >= 0.8:
				_go("jab2", t)
		"jab2":
			_tap_attack(f, bt)
			if bt >= 0.9:
				_go("blaze", t)
		"blaze":
			# Hold past the charge threshold, let go before the bolt is ready: blaze.
			_hold_attack(f, bt, 0.5)
			if bt > 0.6 and bt < 0.7:
				_shot = "blaze"
			if bt >= 1.25:
				_want_target = WALL_T
				_cam_yaw = THERMAL_CAM_YAW
				_go("to_stone", t)
		# ---------------------------------------------------------------- thermal
		"to_stone":
			if bt >= 0.6:
				f.tech_pressed = true
				f.tech_held = true
				var s0 := w.get_body(_stone)
				_aim_at(g, f, s0.pos - p.pos)
				_go("grip", t)
		"grip":
			# Magma grip on the loose stone, then heat it until molten.
			f.tech_held = true
			var s := w.get_body(_stone)
			var held := s != null and s.controller == p.id
			if not held and s != null:
				_aim_at(g, f, s.pos - p.pos)
			elif s != null:
				if s.liquid > 0.3:
					_shot = "melting"
				if s.phase == Sim.Phase.MOLTEN and s.liquid >= 0.99:
					_go("molten", t)
			if bt > 3.0:
				_go("molten", t)
		"molten":
			# Show the molten blob for a moment, drag the aim onto the target and release.
			f.tech_held = true
			_aim_at(g, f, w.get_actor(WALL_T).pos - p.pos)
			if bt >= 0.7:
				f.tech_held = false
				f.tech_released = true
				_go("pour", t)
		"pour":
			if bt > 0.6 and bt < 0.7:
				_shot = "lava_wave"
			if bt >= 2.0:
				f.tech_pressed = true
				f.tech_held = true
				_go("draw", t)
		"draw":
			# Heat draw: the lava crusts and sets into rock; the heat goes into the reserve.
			f.tech_held = true
			var lv := w.get_body(_stone)
			if lv != null:
				_aim_at(g, f, lv.pos - p.pos)
			if lv != null and lv.phase == Sim.Phase.SOLID and bt > 0.6 and not _log.has("rock_t"):
				_log["rock_t"] = t
				_shot = "rock"
			if (_log.has("rock_t") and t - float(_log.rock_t) > 0.8) or bt > 4.0:
				f.tech_held = false
				f.tech_released = true
				_go("pre_vent", t)
		"pre_vent":
			if bt >= 0.45:
				f.tech_pressed = true
				f.tech_held = true
				_aim_at(g, f, _vent_dir(g))
				_go("vent", t)
		"vent":
			# Nothing to work in the aim + a full reserve: VENT dumps the heat.
			f.tech_held = bt < 0.15
			f.tech_released = bt >= 0.15 and bt < 0.15 + Sim.DT * 1.5
			if bt < 0.2:
				_aim_at(g, f, _vent_dir(g))
			if bt > 0.3 and bt < 0.4:
				_shot = "vent"
			if bt >= 1.0:
				_want_target = POOL_T1
				_cam_yaw = _yaw_to(POOL_SPOT, POOL_1) + 0.33
				_go("run", t)
		# ---------------------------------------------------------------- lightning
		"run":
			var to := POOL_SPOT - p.pos
			to.y = 0.0
			if to.length() > 0.2 and bt < 2.5:
				f.move = _stick(g, to.normalized() * (0.85 if to.length() > 1.0 else 0.35))
			elif bt >= 0.4:
				_go("charge", t)
		"charge":
			# Hold until it crackles (aim line on the target in the pool), then release.
			_hold_attack(f, bt - 0.3, 1.15)
			if bt > 1.2 and bt < 1.3:
				_shot = "charged"
			if bt > 1.55 and bt < 1.65:
				_shot = "bolt"
			if bt >= 2.6:
				_want_target = POOL_T2
				_cam_yaw = _yaw_to(p.pos, POOL_2) + 0.5
				_go("charge2", t)
		"charge2":
			_hold_attack(f, bt - 0.5, 1.2)
			if bt > 1.8 and bt < 1.9:
				_shot = "bolt2"
			if bt >= 3.4:
				p.kit.erase("lightning")   # the moveset Flame: long holds grow into Fire Column / Inferno
				_want_target = FLARE_T
				_cam_yaw = _yaw_to(p.pos, w.get_actor(FLARE_T).pos) + 0.5
				_go("column", t)
		# ---------------------------------------------------------------- moveset Flame
		"column":
			_hold_attack(f, bt - 0.4, 1.5)
			if bt > 2.0 and bt < 2.1:
				_shot = "fire_column"
			if bt >= 2.8:
				_go("fireball", t)
		"fireball":
			_gesture_attack(f, bt - 0.1, Sim.Gesture.UP)
			if bt > 0.6 and bt < 0.7:
				_shot = "fireball"
			if bt >= 1.4:
				_go("fireline", t)
		"fireline":
			_gesture_attack(f, bt - 0.1, Sim.Gesture.DOWN)
			if bt > 0.8 and bt < 0.9:
				_shot = "fire_line"
			if bt >= 1.8:
				_go("fan", t)
		"fan":
			_gesture_attack(f, bt - 0.1, Sim.Gesture.SIDE)
			if bt > 0.35 and bt < 0.45:
				_shot = "fire_fan"
			if bt >= 1.3:
				_spawn_wall(g)
				_go("scorch", t)
		"scorch":
			f.tech_held = bt > 0.3 and bt < 2.2
			f.tech_pressed = bt > 0.3 and bt < 0.3 + Sim.DT * 1.5
			f.tech_released = bt >= 2.2 and bt < 2.2 + Sim.DT * 1.5
			if bt > 1.75 and bt < 1.85:
				_shot = "scorch_slump"
			if bt >= 3.0:
				_go("end", t)
		"end":
			pass
	if p.lock_target != _want_target and w.get_actor(_want_target) != null:
		f.target_cycle = true
	_steer_cam(g, f)
	if _trace:
		_trace_tick(g, t)


## A flick gesture on the attack button (desktop: press + gesture on one tick).
func _gesture_attack(f: InputFrame, bt: float, g: int) -> void:
	if bt < 0.0:
		return
	if bt < Sim.DT * 1.5:
		f.attack_pressed = true
		f.attack_gesture = g
	f.attack_held = bt < 0.05
	f.attack_released = bt >= 0.05 and bt < 0.05 + Sim.DT * 1.5


## The SCORCH beat: a Bulwark (booked from the ground) 3 m in front of the player, toward the flare target.
func _spawn_wall(g: Game) -> void:
	var w := g.world
	var p := g.player
	var d := w.get_actor(FLARE_T).pos - p.pos
	d.y = 0.0
	d = d.normalized()
	var at := p.pos + d * 3.0
	at.y = w.arena.ground_height(at.x, at.z, p.pos.y + 0.5)
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, Sim.WALL_MASS, at, "ground@showcase")
	w.mass_ledger.ground_taken += Sim.WALL_MASS
	b.wall_yaw = atan2(d.x, d.z)
	b.wall_half = Vector3(1.1, 0.75, 0.28)
	b.static_body = true
	b.props["standing"] = 8.0
	b.props["rise_time"] = 0.25
	b.touch(w.get_actor(FLARE_T).id, "wall", w.tick)
	w.emit("wall", {"actor": FLARE_T, "body": b.id})


## A vent aim with nothing to work in it (no stone, lava or loose water): away from the rock.
func _vent_dir(g: Game) -> Vector3:
	return _dir_yaw(_yaw_to(g.player.pos, POOL_SPOT) - 0.2)


# ------------------------------------------------------------------ input helpers

func _tap_attack(f: InputFrame, bt: float) -> void:
	if bt < Sim.DT * 1.5:
		f.attack_pressed = true
		f.attack_held = true
	elif bt < Sim.DT * 3.5:
		f.attack_held = false
		f.attack_released = bt < Sim.DT * 2.5


func _hold_attack(f: InputFrame, bt: float, hold: float) -> void:
	if bt < 0.0:
		return
	f.attack_held = bt < hold
	f.attack_pressed = bt < Sim.DT * 1.5
	f.attack_released = bt >= hold and bt < hold + Sim.DT * 1.5


## Technique drag aim toward a world direction, in the camera frame the PlayerController
## expects (x right, y forward).
func _aim_at(g: Game, f: InputFrame, dir: Vector3) -> void:
	dir.y = 0.0
	if dir.length() < 0.01:
		return
	dir = dir.normalized()
	var yaw := g.cam.yaw - f.cam_delta.x
	var fwd := Vector3(sin(yaw), 0.0, cos(yaw))
	var right := fwd.cross(Vector3.UP)
	f.tech_aim = Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)
	f.tech_aim_active = true


## World-space direction -> camera-relative stick (x right, y forward).
func _stick(g: Game, dir: Vector3) -> Vector2:
	var fwd := g.cam.forward_flat()
	var right := fwd.cross(Vector3.UP)
	return Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)


func _yaw_to(from: Vector3, to: Vector3) -> float:
	return atan2(to.x - from.x, to.z - from.z)


func _dir_yaw(yaw: float) -> Vector3:
	return Vector3(sin(yaw), 0.0, cos(yaw))


## Camera drag toward the wanted yaw/pitch; a tiny alternating nudge keeps the rig's idle
## auto-centre from taking over.
func _steer_cam(g: Game, f: InputFrame) -> void:
	var err := wrapf(_cam_yaw - g.cam.yaw, -PI, PI)
	var dy := clampf(err * 0.08, -_cam_rate, _cam_rate)
	if absf(dy) < 2e-4:
		dy = 2e-4 if g.world.tick % 2 == 0 else -2e-4
	var dp := clampf((_cam_pitch - g.cam.pitch) * 0.07, -0.01, 0.01)
	# add_input: yaw -= delta.x, pitch -= delta.y
	f.cam_delta = Vector2(-dy, -dp)


func _go(beat: String, t: float) -> void:
	_beat = beat
	_bt = t
	if _trace:
		print("t=%.2f beat %s" % [t, beat])


func _trace_tick(g: Game, t: float) -> void:
	var p := g.player
	var act := "-" if p.action == null else "%s/%s %.2f" % [p.action.id, p.action.phase_name(), p.action.total]
	var key := "%s|%d" % [act.get_slice(" ", 0), p.lock_target]
	if _log.get("p", "") != key or g.world.tick % 30 == 0:
		_log["p"] = key
		var line := "t=%.2f %s foc%.0f heat%.0f lock%d cam%.2f pos%s" % [t, act, p.focus, p.heat_reserve, p.lock_target, g.cam.yaw, str(p.pos.snapped(Vector3.ONE * 0.1))]
		for b in g.world.bodies:
			if b.alive and b.form != Sim.Form.POOL:
				line += " | " + b.describe() + " @" + str(b.pos.snapped(Vector3.ONE * 0.1))
		for a in g.world.actors:
			if a.is_dummy:
				line += " | %s hp%.0f%s" % [a.name, a.health, " stun:" + a.stun_kind if a.stun > 0.0 else ""]
		print(line)
	if g.hud._toast != _log.get("toast", "") and g.hud._toast_t > 1.7:
		print("t=%.2f TOAST %s" % [t, g.hud._toast])
	_log["toast"] = g.hud._toast
