extends WaterShowcase
## Water / Plant showcase for `--autoplay=show_water_plant[:seconds]` (docs/kits/water.md). Real InputFrames only.
## Beats: sub-element ring -> Bramble Lash yanks the rival in -> Burr Shot (flick up): snares sprout -> Root Snare (flick down)
## -> Thicket Fan (flick side) -> Living Lattice catches a stone, Lattice Roll (guard flick up) -> Deep Roots (guard flick
## down) -> Vinegrip a stone and throw it / hook the rival -> Vine Swing (evade) -> Canopy (hold evade).

var _thrown := false


func sub_index() -> int:
	return 3


func build() -> void:
	beat("sub", 0.9, func(g: Game, f: InputFrame, bt: float) -> void:
		select_sub(f, bt, 3))
	beat("close", 1.2, func(g: Game, f: InputFrame, bt: float) -> void:
		face_rival_move(g, f, 3.4, 0.6))
	beat("lash", 1.8, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05), func(g: Game) -> void:
			rival_to(g, g.player.pos + (g.opponent.pos - g.player.pos).normalized() * 3.8), "lash")
	beat("burr", 2.6, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.UP), func(g: Game) -> void:
			g.player.water_carried = 6.0
			rival_to(g, g.player.pos + (g.opponent.pos - g.player.pos).normalized() * 7.0), "burr")
	beat("roots", 2.8, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.DOWN), func(g: Game) -> void:
			g.player.water_carried = 6.0
			rival_to(g, g.player.pos + (g.opponent.pos - g.player.pos).normalized() * 8.0), "roots")
	beat("thicket", 2.2, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.SIDE), func(g: Game) -> void:
			g.player.water_carried = 6.0, "thicket")
	beat("lattice", 4.2, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 3.9)
		if bt > 1.0 and not _thrown:
			_thrown = true
			throw_stone(g, 20.0, 15.0, 8.0)
		guard_flick(f, bt, 3.0, Sim.Gesture.UP), func(g: Game) -> void:
			_thrown = false
			rival_to(g, g.player.pos + (g.opponent.pos - g.player.pos).normalized() * 6.5), "lattice")
	beat("roots_stance", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.9)
		guard_flick(f, bt, 0.6, Sim.Gesture.DOWN), Callable(), "deep_roots")
	beat("vinegrip", 4.0, func(g: Game, f: InputFrame, bt: float) -> void:
		tech(f, bt, 3.2)
		aim_sweep(f, bt, 1.4, 2.2)
		attack(f, bt, 0.05, 0, 2.6), func(g: Game) -> void:
			rival_to(g, g.player.pos + (g.opponent.pos - g.player.pos).normalized() * 6.0)
			var b := throw_stone(g, 20.0, 9.0, 8.5)
			b.vel *= 0.8, "vinegrip")
	beat("hook", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		tech(f, bt, 1.0), func(g: Game) -> void:
			rival_to(g, g.player.pos + (g.opponent.pos - g.player.pos).normalized() * 7.0), "hook")
	beat("swing", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, _stick(g, Vector3(1.0, 0, 0.3).normalized()), 0.0), func(g: Game) -> void:
			player_to(g, Vector3(7.5, 0.0, 6.0)), "swing")
	beat("canopy", 2.8, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, Vector2.ZERO, 2.2), Callable(), "canopy")
	beat("end", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		pass)
