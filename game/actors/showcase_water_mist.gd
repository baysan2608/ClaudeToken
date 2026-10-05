extends WaterShowcase
## Water / Mist showcase for `--autoplay=show_water_mist[:seconds]` (docs/kits/water.md). Real InputFrames only.
## Beats: sub-element ring -> Scald Puff (tap) -> Steam Jet (hold) -> Geyser (hold 1.1 s: erupts under the rival and
## launches them) -> Fog Lance (flick up: wet, chill, blind) -> Creeping Fog (flick down) with the rival's lightning
## conducted through the fog -> Veil (flick side) -> Steam Screen (guard) against a flare -> Steam Blast (guard flick up)
## -> Dew Fall (guard flick down) -> Vapor Draw and hurl it -> Mist Step (evade).

var _bolt := false
var _flares := 0


func sub_index() -> int:
	return 2


func rival_element() -> int:
	return Sim.Element.FIRE


func build() -> void:
	beat("sub", 0.9, func(g: Game, f: InputFrame, bt: float) -> void:
		select_sub(f, bt, 2)
		g.player.heat_reserve = maxf(g.player.heat_reserve, 300.0))
	beat("close", 1.2, func(g: Game, f: InputFrame, bt: float) -> void:
		face_rival_move(g, f, 3.0, 0.6))
	beat("puff", 1.5, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05), Callable(), "puff")
	beat("jet", 1.8, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.55), func(g: Game) -> void:
			g.player.heat_reserve = 300.0
			g.player.water_carried = 6.0, "jet")
	beat("geyser", 3.2, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.12), func(g: Game) -> void:
			g.player.heat_reserve = 300.0
			g.player.water_carried = 6.0
			rival_to(g, Vector3(3.6, 0.0, -3.6)), "geyser")
	beat("lance", 1.8, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.UP), func(g: Game) -> void:
			g.player.water_carried = 6.0
			rival_to(g, Vector3(3.8, 0.0, -5.6)), "lance")
	beat("fog", 3.4, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.DOWN)
		if bt > 2.2 and not _bolt:
			_bolt = true
			_lightning_into_fog(g), func(g: Game) -> void:
			g.player.water_carried = 6.0
			_bolt = false
			rival_to(g, Vector3(3.6, 0.0, -6.8)), "fog")
	beat("veil", 2.0, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.SIDE), func(g: Game) -> void:
			g.player.water_carried = 6.0, "veil")
	beat("screen", 4.0, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 3.7)
		if bt > 0.9 and _flares == 0:
			_flares = 1
			_flare_cone(g)
		guard_flick(f, bt, 2.7, Sim.Gesture.UP), func(g: Game) -> void:
			_flares = 0
			g.player.heat_reserve = 300.0
			rival_to(g, Vector3(4.0, 0.0, -3.6)), "screen")
	beat("dew", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.6)
		guard_flick(f, bt, 0.8, Sim.Gesture.DOWN), func(g: Game) -> void:
			g.world._spawn_steam(g.player.pos + Vector3(1.0, 1.0, -1.5), 2.0)
			g.world.mass_ledger.moisture_taken += 2.0, "dew")
	beat("vapor", 3.6, func(g: Game, f: InputFrame, bt: float) -> void:
		tech(f, bt, 2.4)
		aim_sweep(f, bt, 0.9, 1.7), func(g: Game) -> void:
			var w := g.world
			var z := WaterUtil.zone(w, "fog", g.player.pos + Vector3(0.3, 0.0, -2.6), 2.4, g.player.id, 8.0, {"actor_status": "concealed",
				"status_t": 0.4, "spare_owner": false, "height": 3.0, "rate": 0.15}, Sim.Mat.STEAM, 2.5, 6.0)
			w.mass_ledger.moisture_taken += 2.5
			rival_to(g, Vector3(3.6, 0.0, -5.0)), "vapor")
	beat("step", 2.0, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, _stick(g, Vector3(1.0, 0, 0.0)), 0.0), Callable(), "step")
	beat("end", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		pass)


## The rival's lightning bolt aimed at the fog the player just rolled out: everyone inside is hit at 60 %.
func _lightning_into_fog(g: Game) -> void:
	var w := g.world
	var fog: MatBody = null
	for b in w.bodies:
		if b.alive and b.tag == &"fog":
			fog = b
	if fog == null:
		return
	var o := g.opponent
	o.pos = fog.pos + Vector3(0, 0, -8.0)
	g.fighters[o.id].snap(o)
	o.lock_target = -1
	var def := {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
	Conduction.discharge(w, o, fog.pos, def, w.new_attack_id(), true)


## A flame cone thrown by the rival into the player's Steam Screen (a flame body: the zone dampens it).
func _flare_cone(g: Game) -> void:
	var w := g.world
	var p := g.player
	var o := g.opponent
	var dir := (p.pos - o.pos)
	dir.y = 0.0
	dir = dir.normalized()
	var f := w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 1.0, p.chest() + dir * 4.0, "showcase")
	f.tag = &"fireball"
	f.heat_payload = 160.0
	w.ledger.generated += 160.0
	f.vel = -dir * 9.0
	f.gravity_scale = 0.0
	f.attack_id = w.new_attack_id()
	f.attack_owner = o.id
	f.hit_set[o.id] = true
	f.max_life = 3.0
	f.damage = 8.0
