extends AirShowcase
## Air / Vacuum showcase for `--autoplay=show_air_vacuum[:seconds]` (~22 s): the Null Bubble snuffing a thrown fireball, a palm
## burst, the Suction Line yanking the rival in, a Pressure Mine, the Vacuum Well (hold: pulls the rival and a sand cloud in,
## release: collapse and the inrush), the Implode palm (T2) and the Vacuum Hop.

var _mine_at := Vector3.ZERO


func sub_index() -> int:
	return 2


func build() -> void:
	beat("intro", 0.9, _intro)
	beat("bubble", 2.4, _bubble, _far, "bubble")
	beat("palm", 1.4, _palm, _near)
	beat("suction", 2.2, _suction, _far, "suction")
	beat("mine", 3.0, _mine, _near, "mine")
	beat("well", 4.4, _well, _far, "well")
	beat("implode", 3.2, _implode, _far, "implode")
	beat("hop", 2.2, _hop, _far, "hop")
	beat("end", 1.0, _idle)


func _far(g: Game) -> void:
	rival_at_range(g, 6.5)


func _near(g: Game) -> void:
	rival_at_range(g, 3.8)


func _idle(_g: Game, _f: InputFrame, _bt: float) -> void:
	pass


func _intro(_g: Game, f: InputFrame, bt: float) -> void:
	select_sub(f, bt, 2, 0.1)


func _bubble(g: Game, f: InputFrame, bt: float) -> void:
	# the fireball leaves first; the bubble goes up ~0.2 s later so it meets it inside the perfect window (Null Catch)
	if bt > 0.1 and once("fireball"):
		throw_fireball(g, 240.0, 14.0, 7.5)
	guard(f, bt, 1.5, 0.35)


func _palm(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05)


func _suction(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05, Sim.Gesture.UP)


func _mine(g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.05, Sim.Gesture.DOWN)
	# the mine goes down ahead of the player; the rival steps onto it
	if bt > 0.7 and bt < 1.0:
		for z in g.world.bodies:
			if z.alive and String(z.tag) == "mine":
				_mine_at = z.pos
	if bt > 1.1 and _mine_at != Vector3.ZERO and once("step_on"):
		rival_to(g, _mine_at + Vector3(0.7, 0.0, 0.4))


func _well(g: Game, f: InputFrame, bt: float) -> void:
	tech(f, bt, 2.6)
	aim_at(g, f, g.opponent.pos)
	if bt > 0.9:
		feed_once("sand", g, "vacuum_well", Sim.Mat.SAND, 3.0)
	if bt > 1.2:
		feed_once("steam", g, "vacuum_well", Sim.Mat.STEAM, 0.5, 0.0, Sim.Form.CLOUD)


func _implode(_g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 1.15)


func _hop(_g: Game, f: InputFrame, bt: float) -> void:
	evade(f, bt, Vector2(-1.0, 0.3), 0.0, 0.2)
