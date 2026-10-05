extends FireShowcase
## Fire / Lightning showcase for `--autoplay=show_fire_lightning[:seconds]` (docs/kits/fire.md). Real InputFrames only.
## Beats: sub-element ring -> Spark (tap) -> Bolt (hold 0.65 s) -> the rival raises a Bulwark: the Bolt is grounded,
## the Storm Bolt (hold 1.2 s) blasts through it -> Skybreak (hold 1.8 s) from above -> Arc Fan (flick side) ->
## a trail of puddles: Ground Current (flick down) races along it -> the rival's bolt into the Static Ward (guard), Static
## Burst (guard flick up) sends it back -> Conductor's Hand on a metal rod, Arc Link around the cover wall -> Rail Arc
## (flick up) -> Arc Step and Overcharge.

var _cue := false


func sub_index() -> int:
	return 2


func build() -> void:
	beat("sub", 0.8, func(g: Game, f: InputFrame, bt: float) -> void:
		select_sub(f, bt, 2))
	beat("spark", 0.9, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05), func(g: Game) -> void:
			rival_to(g, Vector3(1.0, 0.0, 1.5)), "spark", 0.25)
	beat("bolt", 1.4, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.72), func(g: Game) -> void:
			rival_to(g, Vector3(0.5, 0.0, -3.0)), "bolt", 0.55)
	beat("wall", 0.5, func(g: Game, f: InputFrame, bt: float) -> void:
		pass, func(g: Game) -> void:
			rival_wall(g, 3.2, 6.0))
	beat("grounded", 1.3, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.72), Callable(), "grounded", 0.62)
	beat("storm", 1.9, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.28), Callable(), "storm_bolt", 0.7)
	beat("skybreak", 2.8, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.86), func(g: Game) -> void:
			rival_wall(g, 3.2, 2.3), "skybreak", 0.82)
	beat("fan", 1.2, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.SIDE), func(g: Game) -> void:
			rival_to(g, Vector3(0.5, 0.0, 0.0)), "arc_fan", 0.3)
	beat("current", 1.6, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.DOWN), func(g: Game) -> void:
			var p := g.player
			var d := (g.opponent.pos - p.pos)
			d.y = 0.0
			for k in 3:
				var pd := puddle(g, p.pos + d.normalized() * (1.6 + 1.7 * float(k)), 6.0)
				pd.radius = 1.0, "ground_current", 0.35)
	beat("ward", 1.6, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.4)
		if bt >= 0.4 and not _cue:
			_cue = true
			var o := g.opponent
			var def := (Moves.DEFS.lightning as Dictionary).duplicate()
			def["E"] = 24.0
			Conduction.discharge(g.world, o, g.player.chest(), def, g.world.new_attack_id(), false), func(g: Game) -> void:
			_cue = false
			rival_to(g, Vector3(0.5, 0.0, 1.5)), "static_ward", 0.3)
	beat("burst", 1.3, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 0.9)
		guard_flick(f, bt, 0.25, Sim.Gesture.UP), Callable(), "static_burst", 0.35)
	beat("relay", 2.0, func(g: Game, f: InputFrame, bt: float) -> void:
		tech(f, bt, 0.8)
		var rod := g.world.get_body(int(g.world.get_meta("fire_rod", -1)) if g.world.has_meta("fire_rod") else -1)
		if rod != null and bt < 0.8:
			aim_at(g, f, rod.pos - g.player.pos), func(g: Game) -> void:
			player_to(g, Vector3(-3.75, 0.0, 4.5))
			rival_to(g, Vector3(-3.75, 0.0, -3.5))
			var w := g.world
			var rod := w.spawn_body(Sim.Mat.METAL, Sim.Form.CHUNK, 3.0, Vector3(-0.3, 0.25, -1.0), "showcase")
			rod.tag = &"rod"
			rod.on_ground = true
			rod.static_body = true
			w.mass_ledger.metal_taken += 3.0
			w.set_meta("fire_rod", rod.id), "arc_link", 0.45)
	beat("rail", 1.2, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.UP), func(g: Game) -> void:
			player_to(g, Vector3(1.5, 0.0, 5.0))
			rival_to(g, Vector3(0.5, 0.0, -3.0)), "rail_arc", 0.25)
	beat("arc_step", 0.9, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, _stick(g, g.player.forward().cross(Vector3.UP))), Callable(), "arc_step", 0.12)
	beat("overcharge", 1.6, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, _stick(g, -g.player.forward().cross(Vector3.UP)), 1.3), Callable(), "overcharge", 0.5)
	beat("end", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		pass)
