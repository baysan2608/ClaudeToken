extends FireShowcase
## Fire / Combustion showcase for `--autoplay=show_fire_combustion[:seconds]` (docs/kits/fire.md). Real InputFrames only.
## Beats: sub-element ring -> Pop (tap) -> Burst (hold) -> Detonation (hold 1.8 s, 0.5 s fuse) -> Spark Mine (flick up)
## and its remote detonation (flick up again) -> Chain Blasts (flick down) -> Scatter Charges (flick side) -> the rival
## throws a stone: Reactive Blast (guard) blows it aside -> the rival's fire field: Smother Blast (guard flick down) snuffs
## it and jumps -> Fuse (hold the technique, steer, release) -> Blast Jump and Afterglow.

var _cue := false


func sub_index() -> int:
	return 3


func build() -> void:
	beat("sub", 0.8, func(g: Game, f: InputFrame, bt: float) -> void:
		select_sub(f, bt, 3))
	beat("pop", 0.9, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05), func(g: Game) -> void:
			rival_to(g, Vector3(1.2, 0.0, 3.3)), "pop", 0.2)
	beat("burst", 1.3, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.48), func(g: Game) -> void:
			rival_to(g, Vector3(0.6, 0.0, -1.0)), "burst", 0.6)
	beat("detonation", 3.0, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.86), func(g: Game) -> void:
			rival_to(g, Vector3(0.0, 0.0, -6.0)), "detonation", 0.83)
	beat("mine", 1.4, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.UP), func(g: Game) -> void:
			rival_to(g, Vector3(4.0, 0.0, -2.0)), "mine", 0.7)
	beat("mine_pop", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.UP), Callable(), "mine_pop", 0.4)
	beat("chain", 1.8, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.DOWN), func(g: Game) -> void:
			rival_to(g, Vector3(0.5, 0.0, -2.0)), "chain_blasts", 0.55)
	beat("scatter", 1.4, func(g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05, Sim.Gesture.SIDE), Callable(), "scatter", 0.55)
	beat("reactive", 1.4, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.1)
		if bt >= 0.25 and not _cue:
			_cue = true
			throw_at_player(g, Sim.Mat.STONE, 20.0, 17.0, 7.0), func(g: Game) -> void:
			_cue = false, "reactive", 0.42)
	beat("smother", 1.5, func(g: Game, f: InputFrame, bt: float) -> void:
		guard(f, bt, 1.0)
		guard_flick(f, bt, 0.35, Sim.Gesture.DOWN), func(g: Game) -> void:
			var p := g.player
			for k in 3:
				FireUtil.spawn_field(g.world, g.opponent.id, p.pos + Vector3(cos(k * 2.1), 0, sin(k * 2.1)) * 2.4, 1.1, 4.0, 120.0)
				g.world.ledger.generated += 120.0, "smother", 0.35)
	beat("fuse", 2.4, func(g: Game, f: InputFrame, bt: float) -> void:
		tech(f, bt, 1.5), func(g: Game) -> void:
			rival_to(g, Vector3(-1.5, 0.0, -2.5)), "fuse", 0.66)
	beat("blast_jump", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, Vector2.ZERO), Callable(), "blast_jump", 0.3)
	beat("afterglow", 1.4, func(g: Game, f: InputFrame, bt: float) -> void:
		evade(f, bt, _stick(g, g.player.forward().cross(Vector3.UP)) * 0.5, 1.0), Callable(), "afterglow", 0.4)
	beat("end", 1.0, func(g: Game, f: InputFrame, bt: float) -> void:
		pass)
