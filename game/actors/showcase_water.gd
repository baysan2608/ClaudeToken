extends RefCounted
## Water showcase choreography for `--autoplay=show_water[:seconds]` (loaded by
## actors/autoplay.gd). Everything goes through real InputFrames, exactly like touch input;
## the camera is steered with cam_delta like a player's drag.
## Beats: draw from the pool and shape it -> release a stream at the rival -> water lash ->
## ice lance (freezes, shatters) -> draw again while the ice melts into a puddle -> raise the
## held water as a shield -> the rival's flare boils on it (steam) -> let go (the shield refills
## the waterskin, the rest splashes down) -> lash + ice lance finisher -> the shards melt.

const PLAYER_START := Vector3(5.4, 0.0, 0.8)
const RIVAL_START := Vector3(3.6, 0.0, -4.2)

var _beat := ""
var _bt := 0.0           # time the current beat started
var _shot := ""
var _flare_seen := -1.0  # time the rival's first flare started
var _cam_off := -0.55    # camera yaw offset from the player->rival line (rad): a 3/4 view
var _cam_pitch := 0.30


func scenario() -> String:
	return "spar"


func bind(g: Game) -> void:
	g.progress.lab_mode = true
	var p := g.player
	var o := g.opponent
	p.kit = g.progress.kit()
	p.element = Sim.Element.WATER
	p.pos = PLAYER_START
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
	f.move = Vector2.ZERO
	if OS.has_environment("SHOW_TRACE") and g.world.tick % 6 == 0:
		_trace(g, t)
	match _beat:
		"intro":
			if bt >= 0.5:
				f.tech_pressed = true
				f.tech_held = true
				_go("draw", t)
		"draw":
			# Hold: water rises from the pool; drag the aim to shape it, then release at the rival.
			f.tech_held = true
			if bt > 0.9 and bt < 1.8:
				f.tech_aim_active = true
				f.tech_aim = Vector2(sin((bt - 0.9) * 2.0 * PI / 0.9) * 0.7, 0.7)
			if bt > 1.0:
				_shot = "draw"
			if bt >= 2.1:
				f.tech_held = false
				f.tech_released = true
				_go("stream", t)
		"stream":
			if bt > 0.3:
				_shot = "stream"
			if bt >= 0.6:
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
			if bt >= 0.9:
				_go("lance", t)
		"lance":
			# Hold attack: the water freezes into a shard; release to throw it.
			_hold_attack(f, bt, 0.7)
			if bt > 0.75 and bt < 0.85:
				_shot = "lance"
			if bt >= 1.2:
				f.tech_pressed = true
				f.tech_held = true
				_go("draw2", t)
		"draw2":
			# Draw again while the ice shards melt into the puddle at the rival's feet.
			f.tech_held = true
			if bt > 3.6:
				_shot = "melted"
			if bt >= 3.9:
				f.guard_pressed = true
				f.guard_held = true
				_go("shield", t)
		"shield":
			# Guard keeps the held water as a shield. Now the rival fights back with a close flare.
			f.guard_held = true
			f.tech_held = bt < Sim.DT * 2.5
			if bt >= 0.25 and g.ai.cfg.drill == "passive" and _flare_seen < 0.0:
				g.ai.cfg["drill"] = ""
				g.ai.cfg["elements"] = [Sim.Element.FIRE]
				g.ai.cfg["aggression"] = 1.0
			if o.action != null and o.action.id == "fire_attack" and _flare_seen < 0.0:
				_flare_seen = t
			if _flare_seen >= 0.0:
				if t - _flare_seen > 0.15 and t - _flare_seen < 0.3:
					_shot = "steam"
				if t - _flare_seen > 1.4:
					g.ai.cfg["drill"] = "passive"
				if t - _flare_seen > 2.0:
					_go("drop", t)
			elif bt > 3.0:
				_go("drop", t)
		"drop":
			# Letting go: the shield water refills the waterskin, the rest splashes down.
			f.guard_held = false
			f.guard_released = bt < Sim.DT * 1.5
			if bt > 0.4 and bt < 0.5:
				_shot = "drop"
			if bt >= 1.0:
				_go("lash2", t)
		"lash2":
			_tap_attack(f, bt)
			if bt >= 0.8:
				_go("lance2", t)
		"lance2":
			_hold_attack(f, bt, 0.7)
			if bt >= 1.2:
				_go("end", t)
		"end":
			if bt > 3.9:
				_shot = "end"
	_steer_cam(g, f)


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
