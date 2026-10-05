extends "res://actors/showcase_earth_common.gd"
## Earth / Metal showcase (--autoplay=show_earth_metal[:seconds]), ~20 s:
##   Razor Disc tap -> Disc T2 (3 ricochet discs) -> Iron Lance T1 -> Aegis Plate blocks a rival stone ->
##   Rod Plant (guard flick down) -> Lodestone Line (caltrops) -> Chain Arc wraps a rival stone ->
##   Recall (every piece flies back through the rival) -> Magnet Glide to the rod -> Iron Stance.

var _chain_armed := false
var _guard_armed := false


func _init() -> void:
	sub = 1


func steps(g: Game, f: InputFrame, t: float) -> void:
	var p := g.player
	var o := g.opponent
	var away := (p.pos - o.pos).rotated(Vector3.UP, PI * 0.5)
	# 1. Razor Disc, tap then T2 (three discs that ricochet)
	if cue("disc", t >= 0.8):
		press(f, "attack", t, 0.05)
		shot("disc")
	if cue("disc_t2", t >= 1.9):
		press(f, "attack", t, 1.15)
	# 2. Recall: technique aimed where there is no metal - every piece flies back to the satchel
	if cue("recall", t >= 4.0):
		press(f, "tech", t, 0.4)
		shot("recall")
	if holding("tech") and t < 4.6:
		aim_at(g, f, away)
	# 3. Aegis Plate: raised when the rival starts its throw, held through the hit
	if cue("cue_block", t >= 6.0):
		throw_cue(g)
		_guard_armed = true
	if _guard_armed and rival_throwing(g) and not holding("guard"):
		press(f, "guard", t, 1.0)
		_guard_armed = false
	# 4. Rod Plant: guard, then flick down
	if cue("rod_guard", t >= 8.2):
		press(f, "guard", t, 0.4)
	if cue("rod_flick", t >= 8.4):
		guard_flick(f, Sim.Gesture.DOWN)
		shot("rod")
	# 5. Lodestone Line -> caltrops
	if cue("line", t >= 9.5):
		press(f, "attack", t, 0.05, Sim.Gesture.DOWN)
	# 6. Iron Lance (thrust tap)
	if cue("lance", t >= 10.9):
		press(f, "attack", t, 0.05, Sim.Gesture.UP)
		shot("lance")
	# 7. Chain Arc: charged while the rival throws, released as the stone arrives - wraps it
	if cue("cue_chain", t >= 12.2):
		side_camera()
		throw_cue(g)
		_chain_armed = true
	if _chain_armed and rival_throwing(g) and not holding("attack"):
		press(f, "attack", t, 3.0, Sim.Gesture.SIDE)
	if _chain_armed and holding("attack"):
		var inc := incoming(g)
		if inc != null and inc.pos.distance_to(p.chest()) < 4.5:
			release_now(f, "attack")
			_chain_armed = false
			shot("chain")
	# 8. Magnet Glide toward the planted rod, then Iron Stance against a stone
	if cue("glide", t >= 15.0):
		press(f, "evade", t, 0.05)
		shot("glide")
	if cue("cue_iron", t >= 16.2):
		throw_cue(g)
	if cue("iron", t >= 16.4):
		press(f, "evade", t, 1.5)
		shot("iron")
	# 9. Recall again: discs, lance, caltrops and rod fly home
	if cue("recall2", t >= 18.4):
		press(f, "tech", t, 0.4)
	if holding("tech") and t >= 18.4:
		aim_at(g, f, away)
