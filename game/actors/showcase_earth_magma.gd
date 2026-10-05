extends "res://actors/showcase_earth_common.gd"
## Earth / Magma showcase (--autoplay=show_earth_magma[:seconds]), ~20 s:
##   Ember Clot tap -> Magma Bomb T2 (a lava puddle by the rival) -> Lava Lash -> Magma Surge re-pours the
##   lava at the rival -> Magma Curtain catches a rival stone -> Slag Wave -> Spatter Arc -> Melt Pit ->
##   Reverse Tide: the rival's lava wave is seized and turned back -> Lava Wade.

var _guard_armed := false
var _tide := -1


func _init() -> void:
	sub = 3


func setup(g: Game) -> void:
	g.player.heat_reserve = Sim.RESERVE_MAX   # a warm start: magma is paid like fire (reserve first)
	g.world.ledger.generated += Sim.RESERVE_MAX


func steps(g: Game, f: InputFrame, t: float) -> void:
	var p := g.player
	var o := g.opponent
	# 1. Ember Clot, then a Magma Bomb (T2) that splashes a lava puddle near the rival
	if cue("clot", t >= 0.8):
		press(f, "attack", t, 0.05)
		shot("clot")
	if cue("bomb", t >= 1.9):
		press(f, "attack", t, 1.15)
	# 2. Lava Lash (thrust tap)
	if cue("lash", t >= 4.0):
		press(f, "attack", t, 0.05, Sim.Gesture.UP)
	# 3. Magma Surge: the puddle (and anything molten within 6 m) runs back at the rival
	if cue("surge_walk", t >= 5.0):
		stick_toward(g, f, o.pos - p.pos, 0.6)
	if cue("surge", t >= 5.8):
		f.move = Vector2.ZERO
		press(f, "attack", t, 0.05, Sim.Gesture.DOWN)
		shot("surge")
	# 4. Magma Curtain: up when the rival throws (the stone sticks in the molten face), then Slag Wave
	if cue("cue_curtain", t >= 7.6):
		side_camera()
		throw_cue(g)
		_guard_armed = true
	if _guard_armed and rival_throwing(g) and not holding("guard"):
		press(f, "guard", t, 2.6)
		_guard_armed = false
	if cue("slag", t >= 9.6 and holding("guard")):
		guard_flick(f, Sim.Gesture.UP)
		_ends["guard"] = t + 0.05
		shot("slag")
	# 5. Spatter Arc (T1)
	if cue("spatter", t >= 11.4):
		press(f, "attack", t, 0.5, Sim.Gesture.SIDE)
	# 6. Melt Pit (guard, flick down)
	if cue("pit_guard", t >= 12.8):
		press(f, "guard", t, 0.45)
	if cue("pit_flick", t >= 13.15):
		guard_flick(f, Sim.Gesture.DOWN)
		_ends["guard"] = t + 0.05
		shot("melt_pit")
	# 7. Reverse Tide: the rival pours a lava wave; Magma Hold seizes it and turns it back
	if cue("rival_pour", t >= 14.4):
		_tide = _rival_wave(g)
	var tw := g.world.get_body(_tide) if _tide >= 0 else null
	if cue("tide", tw != null and tw.form == Sim.Form.WAVE and KitEarth.flat_dist(tw.pos, p.pos) < 4.6):
		press(f, "tech", t, 0.6)
	if holding("tech") and _tide >= 0:
		var wv := g.world.get_body(_tide)
		if wv != null:
			aim_at(g, f, wv.pos - p.pos)
	if cue("tide_shot", t >= 15.6):
		shot("reverse_tide")
	# 8. Lava Wade through what is left of the lava
	if cue("wade", t >= 17.6):
		press(f, "evade", t, 1.8)
	if holding("evade") and t >= 17.6:
		stick_toward(g, f, o.pos - p.pos, 0.5)
	elif t >= 19.5:
		f.move = Vector2.ZERO


## The rival pours a 20 kg lava wave at the player (as the flagship Fire pour would).
func _rival_wave(g: Game) -> int:
	var w := g.world
	var o := g.opponent
	var p := g.player
	var dir := Vector3(p.pos.x - o.pos.x, 0, p.pos.z - o.pos.z).normalized()
	var start := o.pos + dir * 1.3
	start.y = w.arena.ground_height(start.x, start.z, o.pos.y)
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, 20.0, start, "pour:%d" % o.id)
	w.mass_ledger.ground_taken += 20.0
	w.ledger.generated += Thermal.heat(b, 20.0 * (Sim.STONE_C * (Sim.STONE_MELT_C - Sim.AMBIENT_C) + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = dir
	b.wave_budget = 12.0
	b.wave_width = 1.6
	b.wave_path = PackedVector3Array([start])
	b.attack_id = w.new_attack_id()
	b.attack_owner = o.id
	b.hit_set[o.id] = true
	b.damage = 18.0
	b.balance_damage = 55.0
	b.touch(o.id, "pour", w.tick)
	w.emit("transform", {"body": b.id, "at": start, "from": "blob", "to": "wave", "why": "poured"})
	return b.id
