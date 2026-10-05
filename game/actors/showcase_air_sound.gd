extends AirShowcase
## Air / Sound showcase for `--autoplay=show_air_sound[:seconds]` (~21 s): a charged Clap (T2) shattering a thrown stone in the
## air, the Sound Lance, the Echo Ring, a Tremor Hum along the floor, Flight (hold: rise to 2.2 m, a Clap from the air, release
## to glide down), the Sound Barrier against a stone and the Thunder Step flick out of it, and the Boom Step.


func sub_index() -> int:
	return 3


func build() -> void:
	beat("intro", 0.9, _intro)
	beat("clap", 2.6, _clap, _far, "clap")
	beat("lance", 2.0, _lance, _far, "lance")
	beat("echo", 1.8, _echo, _near, "echo")
	beat("tremor", 2.2, _tremor, _far, "tremor")
	beat("flight", 4.2, _flight, _far, "flight")
	beat("barrier", 3.4, _barrier, _far, "barrier")
	beat("boom", 1.8, _boom, _far, "boom")
	beat("end", 1.0, _idle)


func _far(g: Game) -> void:
	rival_at_range(g, 6.0)


func _near(g: Game) -> void:
	rival_at_range(g, 3.2)


func _idle(_g: Game, _f: InputFrame, _bt: float) -> void:
	pass


func _intro(_g: Game, f: InputFrame, bt: float) -> void:
	select_sub(f, bt, 3, 0.1)


func _clap(g: Game, f: InputFrame, bt: float) -> void:
	# a slow stone; the clap is charged to T2 and released as the stone closes in: it shatters
	if bt > 0.05 and once("stone"):
		throw_stone(g, 14.0, 6.0, 8.0)
	attack(f, bt, 1.15)


func _lance(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05, Sim.Gesture.UP)


func _echo(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05, Sim.Gesture.SIDE)


func _tremor(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05, Sim.Gesture.DOWN)


func _flight(_g: Game, f: InputFrame, bt: float) -> void:
	# tap = rise to 2.2 m and hover (Focus drains); a Clap from the air; tap again = land and glide down
	tech(f, bt, 0.05)
	attack(f, bt, 0.05, 0, 1.6)
	tech(f, bt, 0.05, 3.2)


func _barrier(g: Game, f: InputFrame, bt: float) -> void:
	if bt > 0.1 and once("stone2"):
		throw_stone(g, 14.0, 15.0, 7.5)
	guard(f, bt, 2.8, 0.3)
	guard_flick(f, bt, 1.9, Sim.Gesture.UP)


func _boom(_g: Game, f: InputFrame, bt: float) -> void:
	evade(f, bt, Vector2(-0.6, 0.8), 0.0, 0.2)
