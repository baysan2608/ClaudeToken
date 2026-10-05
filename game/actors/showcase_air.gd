extends RefCounted
## Air showcase choreography for `--autoplay=show_air[:seconds]` (loaded by actors/autoplay.gd).
## Every move goes through real InputFrames -> PlayerController, exactly like touch input; the
## camera is steered with cam_delta like a player's drag. Free Spar in lab mode (glide unlocked).
## Beats: palm gust (tap) knocks the rival back -> the rival throws a stone, a palm gust turns
## it back at them -> step in, cyclone push (hold) -> the rival throws again, air dash out of
## its path -> run to the high ledge, updraft onto it -> updraft off the ledge and glide down
## toward the rival -> land and finish with a cyclone push.
## Then the Gust sub-element's new moves: a charged Gale (T2) against a poured lava wave (the wave is turned to rock),
## a Crescent (flick up), a Dust Line (flick down) and a Crosswind (flick sideways).
## The rival is kept passive (ai cfg) and only throws when cued.

## The duel runs west -> east north of the cover wall, so knockbacks carry the rival into open
## floor and the camera (south-west of the player) never ends up behind the wall.
const PLAYER_START := Vector3(-6.4, 0.0, -8.4)
const RIVAL_START := Vector3(-2.6, 0.0, -9.0)
## Spot in front of the high ledge's south face (ledge: x -15..-10.5, z -15..-10.5, 1.8 m).
const LEDGE_APPROACH := Vector3(-12.2, 0.0, -9.6)
## Camera yaw on the ledge: looking north from the open floor, so the corner walls never
## come between the camera and the fighter; the glide crosses the frame left to right.
const LEDGE_YAW := PI - 0.25

var _beat := ""
var _bt := 0.0           # time the current beat started
var _shot := ""
var _cam_off := 0.5      # camera yaw offset from the player->rival line (rad): a 3/4 view
var _cam_pitch := 0.24
var _cam_yaw: Variant = null   # absolute yaw override (ledge beats)
var _stone := -1         # the rival's stone in flight
var _cued := false
var _trace := OS.has_environment("SHOW_TRACE")
var _cues := AirShowcase.new()   # the rival's cued threats (lava wave) and shared helpers
var _lava_done := false


func scenario() -> String:
	return "spar"


func bind(g: Game) -> void:
	g.progress.lab_mode = true
	var p := g.player
	var o := g.opponent
	p.kit = g.progress.kit()
	p.element = Sim.Element.AIR
	p.pos = PLAYER_START
	o.pos = RIVAL_START
	p.facing = atan2(o.pos.x - p.pos.x, o.pos.z - p.pos.z)
	o.facing = atan2(p.pos.x - o.pos.x, p.pos.z - o.pos.z)
	o.element = Sim.Element.EARTH
	# The rival stands its ground, never reacts, and throws only when cued.
	g.ai.cfg["drill"] = "passive"
	g.ai.cfg["reaction"] = 99.0
	g.ai.cfg["interval"] = 999.0
	g.ai._next_attack = 999.0
	for a in [p, o]:
		g.fighters[a.id].snap(a)
	g.fighters[p.id].set_accent(UiStyle.element_color(p.element))
	var to := o.pos - p.pos
	g.cam.snap_to(p.pos, p.pos + to.rotated(Vector3.UP, _cam_off))
	g.cam.pitch = _cam_pitch
	_go("intro", 0.0)


func wants_shot() -> String:
	var s := _shot
	_shot = ""
	return s


func frame(g: Game, f: InputFrame, t: float) -> void:
	var p := g.player
	var o := g.opponent
	var bt := t - _bt
	var mv := Vector3.ZERO     # world-space move wish, mapped to the stick after the camera
	_rival_taps(g)
	match _beat:
		"intro":
			if bt >= 1.4:
				_go("gust", t)
		"gust":
			# Palm gust (tap): the rival is shoved back.
			_tap_attack(f, bt)
			if bt > 0.2 and bt < 0.3:
				_shot = "gust"
			if bt >= 1.2:
				_cue_throw(g)
				_go("deflect", t)
		"deflect":
			# The rival rips a stone and throws it; a palm gust as it leaves the hand turns it back.
			var b := _incoming(g)
			if b != null and _stone < 0:
				_stone = b.id
				f.attack_pressed = true
				f.attack_held = true
				_bt = t
			elif _stone >= 0 and bt < Sim.DT * 2.5:
				f.attack_held = false
				f.attack_released = true
			if _stone >= 0 and bt > 0.2 and bt < 0.3:
				_shot = "deflect"
			if _stone >= 0 and bt >= 1.3:
				_stone = -1
				_go("approach", t)
			elif _stone < 0 and bt > 3.0:
				_go("approach", t)
		"approach":
			var d := o.pos - p.pos
			d.y = 0.0
			if d.length() > 5.2 and bt < 2.0:
				mv = d.normalized() * 0.8
			else:
				_go("cyclone", t)
		"cyclone":
			# Hold attack past the charge threshold: the wider, stronger cyclone push.
			_hold_attack(f, bt, 0.6)
			if bt > 0.75 and bt < 0.85:
				_shot = "cyclone"
			if bt >= 1.6 and o.stun <= 0.0:
				_cue_throw(g)
				_go("dash", t)
		"dash":
			# Air selected: evade becomes the long air dash, out of the stone's path.
			var b2 := _incoming(g)
			if b2 != null and _stone < 0:
				_stone = b2.id
				_bt = t
			if _stone >= 0 and bt >= 0.08 and bt < 0.08 + Sim.DT * 1.5:
				f.evade_pressed = true
				mv = _side_of(p, o) * 1.0
			if _stone >= 0 and bt > 0.25 and bt < 0.35:
				_shot = "dash"
			if _stone >= 0 and bt >= 0.9:
				_stone = -1
				_go("run", t)
			elif _stone < 0 and bt > 3.0:
				_go("run", t)
		"run":
			# Run to the high ledge (the camera swings to look along the run).
			var d2 := LEDGE_APPROACH - p.pos
			d2.y = 0.0
			mv = d2.normalized() if d2.length() > 0.6 else Vector3(0, 0, -1)
			_cam_yaw = atan2(d2.x, d2.z) if d2.length() > 1.5 else PI
			if d2.length() < 0.6 or bt > 4.0:
				_go("updraft", t)
		"updraft":
			# Updraft (technique) lifts ~2 m: onto the 1.8 m ledge, drifting forward.
			_cam_yaw = PI
			mv = Vector3(0, 0, -1)
			if bt < Sim.DT * 1.5:
				f.tech_pressed = true
			f.tech_held = bt < 0.55
			f.tech_released = bt >= 0.55 and bt < 0.55 + Sim.DT * 1.5
			if bt > 0.3 and bt < 0.4:
				_shot = "updraft"
			if bt > 0.4 and p.grounded:
				_go("ledge", t)
		"ledge":
			# On the ledge: the fighter turns to the rival while the camera swings to a side view.
			_cam_yaw = LEDGE_YAW
			if bt > 0.4 and bt < 0.5:
				_shot = "on_ledge"
			if bt >= 1.3:
				_go("glide", t)
		"glide":
			# Updraft off the ledge and keep holding: glide down toward the rival. The camera
			# eases from the side view back to the duel's 3/4 view as the glide clears the corner.
			_cam_yaw = lerp_angle(LEDGE_YAW, _duel_yaw(g), smoothstep(0.6, 2.6, bt))
			var to2 := o.pos - p.pos
			to2.y = 0.0
			mv = to2.normalized()
			if bt < Sim.DT * 1.5:
				f.tech_pressed = true
			f.tech_held = not (bt > 0.6 and p.grounded)
			if bt > 1.2 and bt < 1.3:
				_shot = "glide"
			if bt > 0.6 and p.grounded:
				f.tech_held = false
				f.tech_released = true
				_go("finish", t)
		"finish":
			# Land, step in: a palm gust, then a cyclone push to close.
			var to3 := o.pos - p.pos
			to3.y = 0.0
			if not _cued:
				if to3.length() > 4.2 and bt < 1.5:
					mv = to3.normalized() * 0.7
				else:
					_cued = true
					_bt = t
			else:
				_tap_attack(f, bt)
				if bt >= 0.75:
					_go("finish2", t)
		"finish2":
			_hold_attack(f, bt, 0.6)
			if bt > 0.75 and bt < 0.85:
				_shot = "finish"
			if bt > 1.5:
				_go("gale", t)
		"gale":
			# Gale (T2, hold ~1.1 s): the rival pours a lava wave; the wide gust meets it and turns it to rock
			if not _lava_done and bt > 0.05:
				_lava_done = true
				_cues.pour_lava(g, 20.0, 11.0)
			_hold_attack(f, bt, 1.15)
			if bt > 1.5 and bt < 1.6:
				_shot = "gale"
			if bt >= 2.6:
				_go("crescent", t)
		"crescent":
			_flick_attack(f, bt, Sim.Gesture.UP)
			if bt > 0.5 and bt < 0.6:
				_shot = "crescent"
			if bt >= 1.5:
				_go("dust_line", t)
		"dust_line":
			_flick_attack(f, bt, Sim.Gesture.DOWN)
			if bt >= 1.5:
				_go("crosswind", t)
		"crosswind":
			_flick_attack(f, bt, Sim.Gesture.SIDE)
			if bt >= 1.6:
				_go("end", t)
		"end":
			pass
	if _beat in ["intro", "gust", "deflect", "approach", "cyclone", "dash", "finish", "finish2", "gale", "crescent", "dust_line", "crosswind", "end"]:
		_cam_yaw = null
	_steer_cam(g, f)
	if mv.length() > 0.01:
		f.move = _stick(g, f, mv)
	else:
		f.move = Vector2.ZERO
	if _trace and g.world.tick % 6 == 0:
		_trace_line(g, t)


## The rival's cued throws are always quick taps (a 20 kg stone: light enough to be turned).
func _rival_taps(g: Game) -> void:
	var ai := g.ai
	if ai._hold == "attack" and ai._hold_until - ai._t > 0.1:
		ai._hold_until = ai._t + 0.05
	var o := g.opponent
	if g.ai.cfg.drill == "stone_rain" and o.action != null and o.action.id == "earth_attack":
		g.ai.cfg["drill"] = "passive"


func _cue_throw(g: Game) -> void:
	g.ai.cfg["drill"] = "stone_rain"
	g.ai._next_attack = 0.0


func _incoming(g: Game) -> MatBody:
	var o := g.opponent
	for b in g.world.bodies:
		if b.alive and b.is_stone() and b.attack_id != 0 and b.attack_owner == o.id and b.controller < 0 and b.is_projectile():
			return b
	return null


## Perpendicular to the duel line, toward the camera side (so the dash crosses the frame).
func _side_of(p: ActorState, o: ActorState) -> Vector3:
	var to := o.pos - p.pos
	to.y = 0.0
	var side := to.normalized().cross(Vector3.UP)
	return side


func _tap_attack(f: InputFrame, bt: float) -> void:
	if bt < Sim.DT * 1.5:
		f.attack_pressed = true
		f.attack_held = true
	elif bt < Sim.DT * 3.5:
		f.attack_held = false
		f.attack_released = bt < Sim.DT * 2.5


func _flick_attack(f: InputFrame, bt: float, gesture: int) -> void:
	if bt < Sim.DT * 1.5:
		f.attack_pressed = true
		f.attack_gesture = gesture
		f.attack_held = true
	elif bt < Sim.DT * 3.5:
		f.attack_held = false
		f.attack_released = bt < Sim.DT * 2.5


func _hold_attack(f: InputFrame, bt: float, hold: float) -> void:
	f.attack_held = bt < hold
	f.attack_pressed = bt < Sim.DT * 1.5
	f.attack_released = bt >= hold and bt < hold + Sim.DT * 1.5


## Camera drag: a 3/4 view of the duel, or an absolute yaw for the ledge beats. A tiny
## alternating nudge keeps the rig's idle auto-centre from taking over.
func _steer_cam(g: Game, f: InputFrame) -> void:
	var want: float = float(_cam_yaw) if _cam_yaw != null else _duel_yaw(g)
	var err := wrapf(want - g.cam.yaw, -PI, PI)
	var dy := clampf(err * 0.06, -0.025, 0.025)
	if absf(dy) < 2e-4:
		dy = 2e-4 if g.world.tick % 2 == 0 else -2e-4
	var dp := clampf((_cam_pitch - g.cam.pitch) * 0.07, -0.01, 0.01)
	# add_input: yaw -= delta.x, pitch -= delta.y
	f.cam_delta = Vector2(-dy, -dp)


func _duel_yaw(g: Game) -> float:
	var to := g.opponent.pos - g.player.pos
	to.y = 0.0
	return atan2(to.x, to.z) + _cam_off


## World-space direction -> camera-relative stick (x right, y forward), using the yaw the
## game will have after applying this tick's cam_delta.
func _stick(g: Game, f: InputFrame, dir: Vector3) -> Vector2:
	var yaw := g.cam.yaw - f.cam_delta.x
	var fwd := Vector3(sin(yaw), 0.0, cos(yaw))
	var right := fwd.cross(Vector3.UP)
	return Vector2(dir.dot(right), dir.dot(fwd)).limit_length(1.0)


func _go(beat: String, t: float) -> void:
	_beat = beat
	_bt = t


func _trace_line(g: Game, t: float) -> void:
	var line := "t=%.2f %s cam=%.2f" % [t, _beat, g.cam.yaw]
	for a in [g.player, g.opponent]:
		var act := "-" if a.action == null else "%s/%s%s" % [a.action.id, a.action.phase_name(), "H" if a.action.heavy else ""]
		line += " | %s %s pos=%s gr=%d gl=%d hp%.0f bal%.0f foc%.0f stun%.2f" % [a.name, act, str(a.pos.snapped(Vector3.ONE * 0.1)), int(a.grounded), int(a.gliding), a.health, a.balance, a.focus, a.stun]
	for b in g.world.bodies:
		if b.alive and b.is_stone() and b.form != Sim.Form.WALL:
			line += " | b%d %.0fkg own=%d ctl=%d pos=%s v=%.1f" % [b.id, b.mass, b.attack_owner if b.attack_id != 0 else -9, b.controller, str(b.pos.snapped(Vector3.ONE * 0.1)), b.vel.length()]
	print(line)
