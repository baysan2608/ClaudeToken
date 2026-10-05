extends WaterShowcase
## Water / Ice showcase for `--autoplay=show_water_ice[:seconds]` (docs/kits/water.md). Real InputFrames only.
## Beats: sub-element ring -> Frost Shard (tap) -> Icicle Volley (flick up) -> Glacier Spear (hold 1.1 s: pierces and
## stands) -> Rime Path (flick down: slick ice strip) -> Hoarfrost Fan on a wet rival (frozen solid) -> Ice Wall stops a
## stone, Glacier Shove (guard flick up) slides it into the rival -> Frost Floor (guard flick down) -> Freeze-Draw an
## ice block and Shatter it (tech + attack) -> Skate across the pool (hold evade).

var _thrown := false
var _flare := false


func sub_index() -> int:
	return 1


func build() -> void:
	beat("sub", 0.9, func(g: Game, f: InputFrame, bt: float) -> void:
		select_sub(f, bt, 1))
	beat("shard", 1.5, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05), Callable(), "shard")
	beat("volley", 1.7, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.UP), Callable(), "volley")
	beat("spear", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.12), Callable(), "spear")
	beat("rime", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.DOWN), func(g: Game) -> void:
			rival_to(g, Vector3(3.4, 0.0, -5.2)), "rime")
	beat("hoarfrost", 2.2, func(g: Game, f: InputFrame, bt: float) -> void:
		g.opponent.wetness = 1.0 if bt < 0.3 else g.opponent.wetness
		if bt < 0.5:
			face_rival_move(g, f, 3.6, 0.55)
		else:
			attack(f, bt, 0.05, Sim.Gesture.SIDE, 0.5), func(g: Game) -> void:
			rival_to(g, Vector3(4.8, 0.0, -3.0))
			g.opponent.wetness = 1.0, "hoarfrost")
	beat("wall", 4.0, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 3.7)
		if bt >= 0.9 and not _thrown:
			_thrown = true
			throw_stone(g, 20.0, 16.0, 8.0)
		guard_flick(f, bt, 2.6, Sim.Gesture.UP), func(g: Game) -> void:
			_thrown = false
			rival_to(g, Vector3(4.2, 0.0, -4.4)), "wall")
	beat("floor", 2.2, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.6)
		guard_flick(f, bt, 0.7, Sim.Gesture.DOWN), func(g: Game) -> void:
			rival_to(g, Vector3(4.6, 0.0, -2.2)), "floor")
	beat("freeze_draw", 3.8, func(g: Game, f: InputFrame, bt: float) -> void:
		tech(f, bt, 3.0)
		aim_sweep(f, bt, 1.0, 2.0)
		attack(f, bt, 0.05, 0, 2.5), func(g: Game) -> void:
			rival_to(g, Vector3(3.6, 0.0, -5.4)), "freeze_draw")
	beat("skate", 3.2, func(g: Game, f: InputFrame, bt: float) -> void:
		if bt < 0.6:
			f.move = _stick(g, Vector3(1.0, 0, 0.2).normalized() * 0.4)
		else:
			evade(f, bt, _stick(g, Vector3(1.0, 0, -0.1).normalized()), 2.6, 0.6), func(g: Game) -> void:
			player_to(g, Vector3(5.6, 0.0, -0.4)), "skate")
	beat("end", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		pass)
