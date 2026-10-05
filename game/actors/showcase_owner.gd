extends FireShowcase
## The owner's examples (docs/MOVESET.md §5.4) in one capture, across all four elements, through real InputFrames only
## (element chips and the sub-element ring are switched with the same input edges the touch layer sends):
##   `--autoplay=show_owner[:seconds]` (about 55 s).
## 1. Holding longer makes it stronger: Earth/Stone Stone Shot T0 -> Heave T1 -> Boulder T2 -> Crag Breaker T3.
## 2. A stone is thrown at me: Earth/Stone Seize + Split (spikes back) · Earth/Stone Swallow (into the ground) ·
##    Water Tidal Rush (a wave carries it back) · Fire/Flame magma grip (turn it into lava, pour it back) ·
##    Air Wind Guard, perfect (deflect it back with wind).
## 3. Lava against wind: a palm gust (T0) does NOT stop a lava wave; a Cyclone Fortress tornado (Vortex T3) does.
## 4. A stone wall: Earth/Magma Molten Lances melt its face, Magma Surge pushes the lava back on the builder.
## 5. Lightning blasts through stone: Fire/Lightning Bolt (T1) is grounded by the wall, Storm Bolt (T2) shatters it.
## The rival is passive; its throws / pours / walls are spawned and booked like its own moves (FireShowcase helpers).
## SHOWCASE_TRACE=1 prints the key sim events (interactions, hits, slumps) for headless verification.

var _caption := ""
var _cap_t := 0.0
var _st := {}        # per-beat scratch state (cleared on every beat)
var _log_on := OS.has_environment("SHOWCASE_TRACE")
var _t_now := 0.0


func player_start() -> Vector3:
	return Vector3(4.6, 0.0, 5.0)    # by the pool (x 7..13): Tidal Rush draws from it


func rival_start() -> Vector3:
	return Vector3(3.6, 0.0, -4.2)


func player_kit(g: Game) -> Dictionary:
	var k := g.progress.kit()
	k.erase("lightning")
	k["magma"] = true
	k["heat_draw"] = true
	return k


func scenario() -> String:
	return "spar"


func frame(g: Game, f: InputFrame, t: float) -> void:
	_t_now = t
	super.frame(g, f, t)
	if _caption != "" and t - _cap_t > 1.2:
		_cap_t = t
		g.hud.toast(_caption)


func _go(i: int, t: float, g: Game) -> void:
	_st = {}
	super._go(i, t, g)
	if i >= 0 and i < beats.size():
		var c := String(beats[i].get("c", ""))
		if c != "":
			_caption = c
			_cap_t = t
			g.hud.toast(c)
			if _log_on:
				print("--- %.2f %s: %s" % [t, beats[i].n, c])


## beat() + a caption shown as a HUD toast for the beat.
func cbeat(name: String, dur: float, caption: String, fn: Callable, enter: Callable = Callable(), shot: String = "", at: float = 0.55) -> void:
	beat(name, dur, fn, enter, shot, at)
	beats[beats.size() - 1]["c"] = caption


## Element chip then sub-element ring, as two input edges.
static func pick(f: InputFrame, bt: float, element: int, sub: int) -> void:
	if bt < DT * 1.5:
		f.element_select = element
	elif bt >= 0.12 and bt < 0.12 + DT * 1.5:
		f.sub_select = sub


func _pick_beat(element: int, sub: int, caption: String, dur: float = 0.5) -> void:
	cbeat("pick_%d_%d" % [element, sub], dur, caption, func(_g: Game, f: InputFrame, bt: float) -> void:
		pick(f, bt, element, sub))


func _once(key: String) -> bool:
	if _st.has(key):
		return false
	_st[key] = true
	return true


func _incoming(g: Game) -> MatBody:
	var best: MatBody = null
	for b in g.world.bodies:
		if b.alive and b.attack_owner == g.opponent.id and b.attack_id != 0 and b.controller < 0 and b.form != Sim.Form.WALL:
			if best == null or b.pos.distance_to(g.player.pos) < best.pos.distance_to(g.player.pos):
				best = b
	return best


## The rival pours a 20 kg lava wave at the player (heat booked as generated, mass from the ground).
func pour_lava(g: Game, mass: float = 20.0, from_dist: float = 9.0) -> MatBody:
	var w := g.world
	var p := g.player
	var o := g.opponent
	var dir := Vector3(p.pos.x - o.pos.x, 0.0, p.pos.z - o.pos.z).normalized()
	var from := p.pos - dir * from_dist
	from.y = w.arena.ground_height(from.x, from.z, 1.0)
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, from, "pour:%d" % o.id)
	w.mass_ledger.ground_taken += mass
	w.ledger.generated += Thermal.heat(b, mass * (Sim.STONE_C * 980.0 + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = dir
	b.wave_budget = 14.0
	b.wave_width = 1.6
	b.wave_path = PackedVector3Array([b.pos])
	b.vel = dir * 7.5
	b.max_life = -1.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = o.id
	b.hit_set[o.id] = true
	b.damage = 14.0
	b.balance_damage = 40.0
	return b


func _reset_pos(g: Game) -> void:
	player_to(g, player_start())
	rival_to(g, rival_start())
	# Clear the stage: earlier walls, rubble and cooled rock sink back into the ground (booked as returned).
	for b in g.world.bodies.duplicate():
		if b.alive and b.controller < 0 and (b.form == Sim.Form.WALL or b.form == Sim.Form.CHUNK or b.form == Sim.Form.BLOB) \
				and b.mat == Sim.Mat.STONE and b.liquid <= 0.0:
			g.world.decay_body(b, "scenario")


func build() -> void:
	# ------------------------------------------------------------ 1. charge tiers
	cbeat("intro", 1.0, "THE OWNER'S EXAMPLES  ·  1. Hold longer = stronger (Earth / Stone)", func(_g: Game, f: InputFrame, bt: float) -> void:
		pick(f, bt, Sim.Element.EARTH, 0))
	cbeat("t0", 1.3, "Stone Shot  T0  (tap)", func(_g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.05), Callable(), "tier0", 0.35)
	cbeat("t1", 1.7, "Heave  T1  (hold 0.55 s)", func(_g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.62), Callable(), "tier1", 0.45)
	cbeat("t2", 2.3, "Boulder  T2  (hold 1.1 s)", func(_g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.18), Callable(), "tier2", 0.6)
	cbeat("t3", 3.2, "Crag Breaker  T3  (hold 1.8 s): splits into rubble", func(_g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.95), Callable(), "tier3", 0.72)
	# ------------------------------------------------------------ 2. a stone is thrown at me
	cbeat("split", 3.2, "2. A stone is thrown at me: SEIZE it, SPLIT it (T+A), spike it back", _split, func(g: Game) -> void:
		_reset_pos(g)
		throw_at_player(g, Sim.Mat.STONE, 20.0, 9.0, 7.0), "split")
	cbeat("swallow", 2.4, "...or SWALLOW it into the ground (Guard, flick down)", _swallow, Callable(), "swallow", 0.4)
	_pick_beat(Sim.Element.WATER, 0, "...or make a WAVE back (Water)")
	cbeat("tidal", 3.6, "Tidal Rush (flick down): the wave captures the stone and carries it back", _tidal, Callable(), "tidal", 0.45)
	_pick_beat(Sim.Element.FIRE, 0, "...or turn it into LAVA (Fire / Flame)")
	cbeat("melt", 5.2, "Grip it with heat, melt it, pour the lava back", _melt, func(g: Game) -> void:
		_reset_pos(g)
		throw_at_player(g, Sim.Mat.STONE, 20.0, 9.0, 8.0), "melt", 0.5)
	_pick_beat(Sim.Element.AIR, 0, "...or DEFLECT it with wind (Air / Gust)")
	cbeat("deflect", 2.4, "Wind Guard, timed: Return Wind sends it back", _deflect, func(g: Game) -> void:
		_reset_pos(g), "deflect", 0.4)
	# ------------------------------------------------------------ 3. lava vs wind
	cbeat("weak_gust", 3.0, "3. Lava vs wind: a palm gust (T0) can NOT stop a lava wave", _weak_gust, func(g: Game) -> void:
		_reset_pos(g)
		pour_lava(g, 20.0, 8.5), "weak_gust", 0.45)
	_pick_beat(Sim.Element.AIR, 1, "...it takes a lot of wind (Air / Vortex)")
	cbeat("fortress", 4.4, "Cyclone Fortress (Vortex T3, hold 1.8 s): the tornado sets the lava to rock", _fortress, func(g: Game) -> void:
		_reset_pos(g), "fortress", 0.78)
	# ------------------------------------------------------------ 4. melt the wall, push it back
	_pick_beat(Sim.Element.EARTH, 3, "4. A stone wall: melt it and push it back (Earth / Magma)", 2.6)   # the tornado walks off and dies
	cbeat("lance1", 3.4, "Molten Lance (flick up, hold to T3): 300 HU into the wall's face", _lance, func(g: Game) -> void:
		_reset_pos(g)
		rival_to(g, Vector3(3.95, 0.0, 0.1))
		rival_wall(g, 3.2, 16.0), "lance")
	cbeat("lance2", 2.1, "...a second lance: the face slumps into lava", _lance, Callable(), "slump", 0.85)
	cbeat("surge", 4.2, "Magma Surge (flick down): the lava goes back at the builder", _surge, func(g: Game) -> void:
		rival_to(g, Vector3(3.95, 0.0, 0.1)), "surge", 0.5)
	# ------------------------------------------------------------ 5. lightning through stone
	_pick_beat(Sim.Element.FIRE, 2, "5. Lightning through stone (Fire / Lightning)")
	cbeat("bolt", 1.6, "Bolt (T1): grounded by the stone wall", func(_g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 0.72), func(g: Game) -> void:
			_reset_pos(g)
			rival_wall(g, 3.2, 10.0), "grounded", 0.5)
	cbeat("storm", 2.3, "Storm Bolt (T2, hold 1.2 s): blasts through the wall", func(_g: Game, f: InputFrame, bt: float) -> void:
		attack(f, bt, 1.28), Callable(), "storm_bolt", 0.6)
	cbeat("end", 2.0, "Every element answers every element: mass, heat, speed and charge decide", func(_g: Game, _f: InputFrame, _bt: float) -> void:
		pass)
	# SHOWCASE_FROM=<beat name>: start at that beat (iteration on one chapter).
	var from := OS.get_environment("SHOWCASE_FROM")
	if from != "":
		for i in beats.size():
			if String(beats[i].n) == from:
				beats = beats.slice(i)
				break


# ------------------------------------------------------------------ beat bodies

## Seize the incoming stone, T+A Split while holding, release toward the rival.
func _split(g: Game, f: InputFrame, bt: float) -> void:
	var p := g.player
	if not _st.has("pressed"):
		var inc := _incoming(g)
		if inc != null and inc.pos.distance_to(p.chest()) < 7.0:
			_st["pressed"] = bt
			f.tech_pressed = true
	if _st.has("pressed"):
		f.tech_held = true
		var held := g.world.held(p)
		if held != null and not _st.has("shaped"):
			_st["shaped"] = bt
			f.attack_pressed = true
		elif _st.has("shaped"):
			aim_at(g, f, g.opponent.pos - p.pos)
			if bt - float(_st.shaped) > 0.25:
				f.tech_held = false
				if _once("released"):
					f.tech_released = true


## Guard up, the stone comes, flick down while it is close: Swallow.
func _swallow(g: Game, f: InputFrame, bt: float) -> void:
	guard(f, bt, 1.3, 0.05)
	if bt > 0.25 and _once("throw"):
		throw_at_player(g, Sim.Mat.STONE, 20.0, 17.0, 6.5)
	var inc := _incoming(g)
	if inc != null and inc.pos.distance_to(g.player.chest()) < 5.0 and bt < 1.3 and _once("flick"):
		f.guard_gesture = Sim.Gesture.DOWN
		f.guard_held = true


## Tidal Rush first (the wave is out), then the rival's stone meets it and rides it back.
func _tidal(g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 0.04, Sim.Gesture.DOWN, 0.05)
	if bt > 0.48 and _once("throw"):
		throw_at_player(g, Sim.Mat.STONE, 20.0, 17.0, 7.0)


## The flagship: technique on the incoming stone, hold until molten, aim at the rival, release = pour.
func _melt(g: Game, f: InputFrame, bt: float) -> void:
	var p := g.player
	if not _st.has("pressed"):
		var inc := _incoming(g)
		if inc != null and inc.pos.distance_to(p.chest()) < 7.5:
			_st["pressed"] = true
			_st["body"] = inc.id
			f.tech_pressed = true
	if _st.has("pressed") and not _st.has("released"):
		f.tech_held = true
		var b := g.world.get_body(int(_st.body))
		if b != null and b.controller == p.id:
			aim_at(g, f, g.opponent.pos - p.pos)
			if b.phase == Sim.Phase.MOLTEN and b.liquid >= 0.99:
				_st["released"] = true
				f.tech_held = false
				f.tech_released = true
		elif b == null or (p.action == null and bt > 1.0):
			_st["released"] = true


## Wind Guard pressed just before the stone arrives (inside the 0.18 s perfect window).
func _deflect(g: Game, f: InputFrame, bt: float) -> void:
	if bt > 0.1 and _once("throw"):
		_st["stone"] = throw_at_player(g, Sim.Mat.STONE, 20.0, 15.0, 7.5).id
	var b := g.world.get_body(int(_st.get("stone", -1)))
	if b != null and b.alive and not _st.has("g"):
		var d := b.pos.distance_to(g.player.chest())
		if d < 15.0 * 0.12 + 0.6:
			_st["g"] = bt
	if _st.has("g"):
		guard(f, bt, 0.5, float(_st.g))


## A palm gust (T0) as the wave arrives: it passes (no full answer); the player still takes it.
func _weak_gust(g: Game, f: InputFrame, bt: float) -> void:
	var inc := _incoming(g)
	if inc != null and inc.form == Sim.Form.WAVE and inc.pos.distance_to(g.player.pos) < 4.5 and _once("palm"):
		_st["palm"] = bt
	if _st.has("palm"):
		attack(f, bt, 0.05, 0, float(_st.palm))


## Cyclone Fortress: start the hold first, the rival pours while it charges; T3 releases a 4 m tornado in front.
func _fortress(g: Game, f: InputFrame, bt: float) -> void:
	attack(f, bt, 1.92, 0, 0.05)
	if bt > 1.2 and _once("pour"):
		pour_lava(g, 20.0, 11.0)


## The builder stays tucked behind their wall (1.5 m) during the melt chapter.
func _pin_builder(g: Game) -> void:
	var want := Vector3(3.95, 0.0, 0.1)
	var o := g.opponent
	if Vector2(o.pos.x - want.x, o.pos.z - want.z).length() > 0.05:
		rival_to(g, want)


func _lance(g: Game, f: InputFrame, bt: float) -> void:
	_pin_builder(g)
	attack(f, bt, 1.95, Sim.Gesture.UP, 0.05)
	if g.player.focus < 80.0:
		g.player.focus = 100.0       # two T3 lances back to back (a showcase, not a resource drill)
	if g.player.heat_reserve < 300.0:
		g.world.ledger.generated += 300.0 - g.player.heat_reserve
		g.player.heat_reserve = 300.0


## Magma Surge as soon as the lance lets go (a press every 0.1 s until it starts), held to T1 for more budget.
func _surge(g: Game, f: InputFrame, bt: float) -> void:
	var p := g.player
	if not _st.has("hit"):
		_pin_builder(g)
	if not _st.has("t0"):
		if p.action != null and p.action.id == "magma_surge":
			_st["t0"] = bt
		elif int(bt * 60.0) % 6 == 0:
			f.attack_pressed = true
			f.attack_gesture = Sim.Gesture.DOWN
		f.attack_held = true
	else:
		f.attack_held = bt - float(_st.t0) < 0.7
		f.attack_released = not f.attack_held and _once("rel")


# ------------------------------------------------------------------ trace

func after_tick(g: Game, evs: Array[Dictionary]) -> void:
	if not _log_on:
		return
	if OS.has_environment("SHOWCASE_WAVES") and g.world.tick % 6 == 0:
		for b in g.world.bodies:
			if b.alive and b.form == Sim.Form.WAVE:
				print("  wave #%d pos %s v %.2f liq %.2f budget %.1f owner %d hitset %s rival %s" % [b.id, b.pos, b.vel.length(), b.liquid, b.wave_budget, b.attack_owner, b.hit_set.keys(), g.opponent.pos])
	for e in evs:
		match String(e.type):
			"interaction":
				print("  %.2f interaction %s x %s -> %s/%s r%.2f (rule %s)" % [_t_now, e.get("threat", ""), e.get("counter", ""), e.get("outcome", ""),
					e.get("band", ""), float(e.get("ratio", 0.0)), e.get("rule", "")])
			"hit":
				if int(e.actor) == g.opponent.id and String(e.get("kind", "")) == "lava":
					_st["hit"] = true
				print("  %.2f hit %s kind %s dmg %.1f result %s" % [_t_now, g.world.get_actor(int(e.actor)).name, e.get("kind", ""), float(e.get("damage", 0.0)), e.get("result", "")])
			"slump", "shatter", "transform", "sink", "shape", "magma_surge", "grounded", "heating", "crumble":
				var d := e.duplicate()
				d.erase("type")
				print("  %.2f %s %s" % [_t_now, e.type, str(d).left(160)])
