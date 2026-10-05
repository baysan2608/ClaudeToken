extends "res://actors/showcase_earth_common.gd"
## Earth / Sand showcase (--autoplay=show_earth_sand[:seconds]), ~20 s:
##   Grit Shot tap (blind) -> Sand Cannon T2 (a cloud) -> Sand Surge -> Veil of Grit -> Dune Wall captures a
##   rival stone, held thicker -> Dune Push -> Quicksand (guard flick down) -> Sandform gathers,
##   T+A Compress (sandstone) and throws -> Sand Surf ride.

var _guard_armed := false


func _init() -> void:
	sub = 2


func steps(g: Game, f: InputFrame, t: float) -> void:
	var p := g.player
	var o := g.opponent
	# 1. Grit Shot tap, then Sand Cannon (T2: bursts into a sand cloud)
	if cue("grit", t >= 0.8):
		press(f, "attack", t, 0.05)
		shot("grit")
	if cue("cannon", t >= 1.9):
		press(f, "attack", t, 1.15)
	# 2. Sand Surge (ground)
	if cue("surge", t >= 4.4):
		press(f, "attack", t, 0.5, Sim.Gesture.DOWN)
		shot("surge")
	# 3. Veil of Grit (sweep)
	if cue("veil", t >= 6.4):
		press(f, "attack", t, 0.05, Sim.Gesture.SIDE)
	# 4. Dune Wall: up when the rival throws, held 1.6 s (thicker), then Dune Push
	if cue("cue_dune", t >= 8.4):
		side_camera()
		throw_cue(g)
		_guard_armed = true
	if _guard_armed and rival_throwing(g) and not holding("guard"):
		press(f, "guard", t, 2.0)
		_guard_armed = false
	if cue("dune_push", t >= 11.0 and holding("guard")):
		guard_flick(f, Sim.Gesture.UP)
		shot("dune_push")
	# 5. Quicksand in front of the rival's approach
	if cue("qs_guard", t >= 12.4):
		press(f, "guard", t, 0.7)
	if cue("qs_flick", t >= 12.95):
		guard_flick(f, Sim.Gesture.DOWN)
		shot("quicksand")
	# 6. Sandform: gather, Compress (T+A) to sandstone, throw
	if cue("sandform", t >= 14.2):
		press(f, "tech", t, 2.2)
	if cue("compress", t >= 15.9):
		f.attack_pressed = true
		shot("compress")
	if holding("tech"):
		aim_at(g, f, o.pos - p.pos)
	# 7. Sand Surf: ride sideways
	if cue("surf", t >= 17.4):
		press(f, "evade", t, 1.6)
	if holding("evade") and t >= 17.4:
		stick_toward(g, f, (o.pos - p.pos).rotated(Vector3.UP, PI * 0.5))
	elif t >= 19.0:
		f.move = Vector2.ZERO
