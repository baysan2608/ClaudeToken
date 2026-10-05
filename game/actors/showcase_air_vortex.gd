extends AirShowcase
## Air / Vortex showcase for `--autoplay=show_air_vortex[:seconds]` (~21 s): a Twister tap, a charged Tornado (T2) that lifts the
## rival and spins up a sand cloud (sandblasted), the Vortex Wall catching a thrown stone and Unleash firing it back, the Eddy
## Ring, the Dust Funnel, Eye of the Storm, and the Spin Step out of a lava wave.


func sub_index() -> int:
	return 1


func build() -> void:
	beat("intro", 0.9, _intro)
	beat("twister", 1.6, _twister, _far)
	beat("wall", 3.6, _wall, _far, "wall")
	beat("tornado", 4.6, _tornado, _far, "tornado")
	beat("eddy", 1.8, _eddy, _near, "eddy")
	beat("funnel", 2.0, _funnel, _far, "funnel")
	beat("eye", 3.0, _eye, _far, "eye")
	beat("spin_step", 2.2, _spin, _far, "spin_step")
	beat("end", 1.2, _idle)


func _far(g: Game) -> void:
	rival_at_range(g, 6.5)


func _near(g: Game) -> void:
	rival_at_range(g, 3.5)


func _idle(_g: Game, _f: InputFrame, _bt: float) -> void:
	pass


func _intro(_g: Game, f: InputFrame, bt: float) -> void:
	select_sub(f, bt, 1, 0.1)


func _twister(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05)


func _tornado(g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 1.15)
	if bt > 2.0:
		feed_once("sand", g, "tornado", Sim.Mat.SAND, 3.0)


func _wall(g: Game, f: InputFrame, bt: float) -> void:
	# the stone leaves first; the guard goes up ~0.25 s later so it meets the stone inside the perfect window (Vortex Catch)
	if bt > 0.1 and once("stone"):
		throw_stone(g, 18.0, 15.0, 7.5)
	guard(f, bt, 3.0, 0.3)
	guard_flick(f, bt, 2.0, Sim.Gesture.UP)


func _eddy(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05, Sim.Gesture.SIDE)


func _funnel(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05, Sim.Gesture.DOWN)


func _eye(g: Game, f: InputFrame, bt: float) -> void:
	tech(f, bt, 1.6)
	aim_at(g, f, g.opponent.pos)


func _spin(g: Game, f: InputFrame, bt: float) -> void:
	if bt > 0.3 and once("lava"):
		pour_lava(g, 20.0, 8.0)
	evade(f, bt, Vector2(-1.0, 0.0), 0.0, 0.75)
