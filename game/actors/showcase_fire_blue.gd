extends FireShowcase
## Fire / Blue showcase for `--autoplay=show_fire_blue[:seconds]` (docs/kits/fire.md). Real InputFrames only.
## Beats: sub-element ring -> Blue Needle (tap) -> Blue Lance (hold) -> the rival throws a stone: Searing Beam (hold
## 1 s) melts it in flight into a falling magma blob -> Comet Flame (flick up) -> Blue Furrow (flick down: a lava
## channel) -> Corona (flick side) melts an ice shard -> Blue Aegis melts a metal shot -> the rival raises a Bulwark:
## Smelter (hold the technique) slumps its face into lava -> Kiln (guard + flick down) superheats a stone -> Shimmer
## Step and Afterburn.

var _thrown := false


func sub_index() -> int:
	return 1


func build() -> void:
	beat("sub", 0.8, func(g: Game, f: InputFrame, bt: float) -> void:
		select_sub(f, bt, 1))
	beat("needle", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05), Callable(), "needle", 0.25)
	beat("lance", 1.3, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.5), Callable(), "lance", 0.5)
	beat("searing", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.08)
		if bt >= 0.86 and not _thrown:
			_thrown = true
			throw_at_player(g, Sim.Mat.STONE, 20.0, 17.0, 7.5), func(g: Game) -> void:
			_thrown = false, "searing_melt", 0.55)
	beat("comet", 1.3, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.UP), Callable(), "comet", 0.3)
	beat("furrow", 2.0, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.DOWN), func(g: Game) -> void:
			rival_to(g, Vector3(0.5, 0.0, -3.0)), "furrow", 0.45)
	beat("corona", 1.6, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.SIDE)
		if bt >= 0.32 and not _thrown:
			_thrown = true
			throw_at_player(g, Sim.Mat.WATER, 4.0, 16.0, 5.0, -8.0), func(g: Game) -> void:
			_thrown = false, "corona", 0.3)
	beat("aegis", 1.6, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.3)
		if bt >= 0.35 and not _thrown:
			_thrown = true
			throw_at_player(g, Sim.Mat.METAL, 6.0, 18.0, 6.0), func(g: Game) -> void:
			_thrown = false, "aegis", 0.45)
	beat("wall", 0.6, func(g: Game, f: InputFrame, bt: float) -> void:
		pass, func(g: Game) -> void:
			rival_wall(g, 3.0, 10.0))
	beat("smelter", 2.2, func(g: Game, f: InputFrame, bt: float) -> void:
		tech(f, bt, 1.6), Callable(), "smelter_slump", 0.62)
	beat("kiln", 1.6, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.0)
		guard_flick(f, bt, 0.2, Sim.Gesture.DOWN), func(g: Game) -> void:
			var w := g.world
			var p := g.player
			var s := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, p.pos + p.forward() * 1.6 + Vector3(0, 0.3, 0), "showcase")
			w.mass_ledger.ground_taken += 20.0
			s.on_ground = true, "kiln", 0.7)
	beat("shimmer", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, _stick(g, g.player.forward().cross(Vector3.UP))), Callable(), "shimmer", 0.15)
	beat("afterburn", 2.0, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, _stick(g, -g.player.forward().cross(Vector3.UP)), 1.6), Callable(), "afterburn", 0.6)
	beat("end", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		pass)
