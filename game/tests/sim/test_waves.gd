extends TestCase
## Lava waves: a magma fighter seizes a loose stone with the Fire technique, heats it until it is
## MOLTEN, aims and releases (pour). The wave then flows over the arena under these rules:
##  - stopped by anything taller than WAVE_STEP (arena solids, raised earth walls)
##  - drops off ledges (wave_drop, costs 1 m of budget) and continues on lower ground
##  - quenched by water (steam) and cooled to rock over time
##  - travels at most base_budget + budget_per_kg * mass (plus one step of overshoot)

var h: SimHarness
var p: ActorState
var stone: MatBody

const STEP_EPS := 0.13   # one tick of wave travel (7.5 m/s / 60)


func _world(p_pos: Vector3, stone_pos: Vector3, mass: float = 20.0, enemy_pos = null) -> void:
	h = SimHarness.new(1)
	p = h.actor("P", p_pos, 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	stone = h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, mass, stone_pos, "scenario")
	if enemy_pos != null:
		h.actor("E", enemy_pos, 1, {}, Sim.Element.EARTH)
	h.step(25)
	h.log.clear()


## Seize + heat the stone until MOLTEN, aim, release. Returns true when the wave exists.
func _pour(dir: Vector3) -> bool:
	h.aim(p, dir)
	h.press(p, "tech")
	var n := h.until(func(): return stone.phase == Sim.Phase.MOLTEN, 240)
	if n < 0:
		check(false, "the stone never became molten (%s, focus %.1f)" % [stone.describe(), p.focus])
		return false
	h.release(p, "tech")
	var m := h.until(func(): return stone.form == Sim.Form.WAVE, 60)
	check(m > 0, "releasing a molten stone pours a wave")
	return m > 0


## Follows the wave until it stops being a wave. Returns {dist, min_z, max_z, ticks}.
func _follow(max_ticks: int = 900) -> Dictionary:
	var dist := 0.0
	var last := stone.pos
	var lo := stone.pos.z
	var hi := stone.pos.z
	var ticks := 0
	for k in max_ticks:
		h.step()
		if stone.form != Sim.Form.WAVE:
			break
		ticks += 1
		dist += Vector2(stone.pos.x - last.x, stone.pos.z - last.z).length()
		last = stone.pos
		lo = minf(lo, stone.pos.z)
		hi = maxf(hi, stone.pos.z)
	return {"dist": dist, "min_z": lo, "max_z": hi, "ticks": ticks}


func _budget(mass: float) -> float:
	return float(Moves.DEFS.pour.base_budget) + float(Moves.DEFS.pour.budget_per_kg) * mass


func _count(type: String) -> int:
	return h.events(type).size()


# ---------------------------------------------------------------- stopped by solids

func test_wave_stops_before_the_cover_wall_then_cools_to_rock() -> void:
	_world(Vector3(-3.75, 0, 2.5), Vector3(-3.75, 0.2, 1.5))
	var cover := h.w.arena.solid_named("cover_wall")
	check(not cover.is_empty(), "the lab has a cover wall")
	var wall_face: float = cover.max.z   # the +z face the wave runs into
	if not _pour(Vector3(0, 0, -1)):
		return
	check(stone.attack_owner == p.id and stone.attack_id != 0, "the wave is P's attack while it flows")
	var run := _follow()
	check(_count("wave_blocked") == 1, "exactly one wave_blocked, got %d" % _count("wave_blocked"))
	var settle := h.events("wave_settle")
	check(settle.size() == 1 and settle[0].why == "blocked", "settles because it was blocked (%s)" % [settle])
	check(stone.form == Sim.Form.BLOB, "a still-liquid settled wave is a lava blob (%s)" % Sim.FORM_NAMES[stone.form])
	check(stone.attack_id == 0, "a settled wave is no longer an attack")
	check(stone.pos.z >= wall_face, "the lava stopped on the near side of the wall (z %.3f, face %.2f)" % [stone.pos.z, wall_face])
	check(run.min_z >= wall_face, "it never crossed into the wall (min z %.3f)" % run.min_z)
	check(wall_face - run.min_z < 0.0 + 0.01, "and never entered the wall's footprint")
	check(not h.has_event("hit"), "nobody was hit")
	near(stone.mass, 20.0, 1e-9, "mass preserved")
	# It stays put and cools to rock without flicker.
	var z0 := stone.pos.z
	var changes := 0
	var last := stone.phase
	for k in 900:
		h.step()
		if stone.phase != last:
			changes += 1
			last = stone.phase
	near(stone.pos.z, z0, 1e-9, "the settled lava does not creep")
	check(stone.phase == Sim.Phase.SOLID and stone.form == Sim.Form.CHUNK, "later cooled to rock (%s)" % stone.describe())
	check(h.events("transform").any(func(e): return e.from == "lava" and e.to == "rock" and e.why == "cooled"), "lava -> rock transform reported")
	check(changes <= 2, "monotonic cooling labels (%d changes)" % changes)
	check(stone.temp >= Sim.AMBIENT_C and stone.temp < 400.0, "the rock is cooling (%.0f degC)" % stone.temp)


func test_wave_stops_at_terrace_and_step_block_but_not_open_ground() -> void:
	# The terrace (0.6 m) and the step block (0.35 m) are higher than WAVE_STEP (0.3): unclimbable.
	_world(Vector3(0, 0, 6), Vector3(0, 0.2, 7))
	if _pour(Vector3(0, 0, 1)):
		_follow()
		check(_count("wave_blocked") == 1, "terrace face blocks the wave")
		check(stone.pos.z < 10.0, "stopped in front of the terrace (z %.2f)" % stone.pos.z)
	_world(Vector3(6.5, 0, 10.5), Vector3(7.3, 0.2, 10.5))
	if _pour(Vector3(1, 0, 0)):
		_follow()
		check(_count("wave_blocked") == 1, "step block face blocks the wave")
		check(stone.pos.x < 9.0, "stopped in front of the step block (x %.2f)" % stone.pos.x)
	# Open ground: not blocked at all.
	_world(Vector3(-4, 0, 7), Vector3(-3.2, 0.2, 7))
	if _pour(Vector3(1, 0, 0)):
		_follow()
		check(_count("wave_blocked") == 0, "open ground: no blocking")
		var why: String = h.last_event("wave_settle").get("why", "")
		check(why == "budget" or why == "viscous", "ends by exhausting itself (%s)" % why)


func test_wave_poured_off_the_terrace_edge_drops_and_continues() -> void:
	_world(Vector3(0, 0.6, 12.5), Vector3(0, 0.8, 11.5))
	check(absf(p.pos.y - 0.6) < 1e-6, "setup: P stands on the terrace")
	if not _pour(Vector3(0, 0, -1)):
		return
	check(absf(stone.pos.y - 0.6) < 0.05, "the wave starts on top of the terrace (y %.2f)" % stone.pos.y)
	var ys: Array[float] = []
	var dist := 0.0
	var last := stone.pos
	for k in 900:
		h.step()
		if stone.form != Sim.Form.WAVE:
			break
		dist += Vector2(stone.pos.x - last.x, stone.pos.z - last.z).length()
		last = stone.pos
		ys.append(stone.pos.y)
	var drops := h.events("wave_drop")
	check(drops.size() == 1, "exactly one wave_drop for one edge, got %d" % drops.size())
	if drops.size() == 1:
		check(absf(float(drops[0].from) - 0.6) < 0.01 and absf(float(drops[0].to)) < 0.01, "dropped from %.2f to %.2f" % [drops[0].from, drops[0].to])
	check(_count("wave_blocked") == 0, "the drop does not block the wave")
	check(stone.pos.z < 10.0 - 1.0, "the wave continued well past the edge on lower ground (z %.2f)" % stone.pos.z)
	check(absf(stone.pos.y) < 0.01, "and now flows at ground level (y %.3f)" % stone.pos.y)
	var min_y := 9.0
	for y in ys:
		min_y = minf(min_y, y)
	check(min_y > -0.01, "it never sank below the ground")
	# The drop costs 1 m of budget: total path + drop <= budget + one step.
	var bound := _budget(20.0)
	check(dist + 1.0 <= bound + STEP_EPS, "path %.2f m + 1 m drop exceeds the %.2f m budget" % [dist, bound])
	check(dist > 3.0, "and it still travelled a good way (%.2f m)" % dist)


# ---------------------------------------------------------------- budget

func test_wave_travel_never_exceeds_its_budget() -> void:
	var travelled := {}
	for m in [10.0, 20.0, 35.0]:
		_world(Vector3(-4, 0, 7), Vector3(-3.2, 0.2, 7), m)
		if not _pour(Vector3(1, 0, 0)):
			continue
		var start := stone.pos
		var run := _follow()
		var budget := _budget(m)
		check(run.dist <= budget + STEP_EPS, "mass %.0f: travelled %.3f m, budget %.2f m" % [m, run.dist, budget])
		check(run.dist >= 0.5 * budget, "mass %.0f: the wave actually flows (%.2f of %.2f m)" % [m, run.dist, budget])
		check(stone.pos.x - start.x <= budget + STEP_EPS, "mass %.0f: displacement within budget" % m)
		var why: String = h.last_event("wave_settle").get("why", "")
		check(why == "budget" or why == "viscous", "mass %.0f: ends by budget/viscosity (%s)" % [m, why])
		near(stone.mass, m, 1e-9, "mass %.0f: preserved" % m)
		note("mass %.0f: travelled %.2f of %.2f m (%s)" % [m, run.dist, budget, h.last_event("wave_settle").get("why", "")])
		travelled[m] = run.dist
	if travelled.size() == 3:
		check(travelled[10.0] < travelled[20.0] and travelled[20.0] < travelled[35.0], "more molten rock flows farther %s" % [travelled])


# ---------------------------------------------------------------- water

func test_wave_entering_the_pool_makes_steam_and_solidifies_quickly() -> void:
	_world(Vector3(3, 0, -1), Vector3(3.8, 0.2, -1))
	var wm := h.w.water_mass()
	var pool_mass := h.w.pool.mass
	var e0 := h.w.system_energy()
	if not _pour(Vector3(1, 0, 0)):
		return
	var t_pour := h.w.tick
	var t_solid := -1
	for k in 600:
		h.step()
		if t_solid < 0 and stone.phase == Sim.Phase.SOLID:
			t_solid = h.w.tick
	check(h.has_event("steam"), "steam events while the lava meets the water")
	var drop := h.events("wave_drop").filter(func(e): return e.to < -0.1)
	check(drop.size() >= 1, "the wave dropped into the pool basin")
	note("pool quench: %d ticks pour->solid, pool lost %.2f kg" % [t_solid - t_pour, pool_mass - h.w.pool.mass])
	check(t_solid > 0, "the lava solidified")
	check(t_solid - t_pour < 100, "quenched fast: %d ticks from pour to solid" % (t_solid - t_pour))
	check(stone.form == Sim.Form.CHUNK and stone.phase == Sim.Phase.SOLID, "now rock (%s)" % stone.describe())
	check(stone.pos.x >= 7.0 and stone.pos.x < 13.0, "it stopped inside the pool region (x %.2f)" % stone.pos.x)
	# Water accounting: whatever left the pool as steam is in the vapor ledger once the clouds are gone.
	h.step(300)
	check(h.w.bodies.filter(func(b): return b.alive and b.mat == Sim.Mat.STEAM).is_empty(), "all steam dissipated")
	var lost := pool_mass - h.w.pool.mass
	check(lost > 1.0, "the pool boiled away %.2f kg" % lost)
	near(h.w.mass_ledger.vapor, lost, 1e-4, "mass_ledger.vapor equals the water the pool lost")
	near(h.w.water_mass(), wm, 1e-4, "water mass conserved")
	near(h.w.system_energy() - e0, h.w.ledger_balance(), 0.05, "energy ledger balances through the quench")
	near(stone.mass, 20.0, 1e-9, "stone mass preserved")
	# Control: the same pour over dry ground takes far longer to solidify.
	_world(Vector3(3, 0, 6), Vector3(3.8, 0.2, 6))
	if _pour(Vector3(1, 0, 0)):
		var t0 := h.w.tick
		var t1 := -1
		for k in 900:
			h.step()
			if t1 < 0 and stone.phase == Sim.Phase.SOLID:
				t1 = h.w.tick
				break
		note("dry control: %d ticks to solid" % (t1 - t0))
		check(t1 < 0 or t1 - t0 > 2 * (t_solid - t_pour), "dry ground takes much longer (%d vs %d ticks)" % [t1 - t0, t_solid - t_pour])
		check(not h.has_event("steam"), "no steam on dry ground")


func test_wave_boils_a_small_puddle_without_losing_water() -> void:
	_world(Vector3(0, 0, 7), Vector3(0.8, 0.2, 7))
	# 0.9 kg puddle on the wave's path: the first contact tick boils 0.8975 kg, leaving a 0.0025 kg residue.
	var puddle := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 0.9, Vector3(4.0, 0, 7), "scenario")
	puddle.update_radius_puddle()
	var wm := h.w.water_mass()
	if not _pour(Vector3(1, 0, 0)):
		return
	_follow()
	check(not puddle.alive, "the puddle was boiled away")
	check(h.has_event("steam"), "it produced steam")
	h.step(240)
	near(h.w.water_mass(), wm, 1e-6, "no water vanished (even the sub-threshold residue)")


func test_earth_wall_blocks_a_wave_and_without_it_the_wave_hits() -> void:
	for wall in [true, false]:
		_world(Vector3(0, 0, 8), Vector3(0, 0.2, 7), 20.0, Vector3(0, 0, 0))
		var e: ActorState = h.w.actors[1]
		if wall:
			h.press(e, "guard")   # Earth guard raises a wall in front of E (facing P)
			h.step(12)
			check(e.wall_body >= 0, "setup: E raised a wall")
		if not _pour(Vector3(0, 0, -1)):
			continue
		var run := _follow()
		h.step(60)
		if wall:
			var w_body := h.w.get_body(e.wall_body)
			check(h.events("block").any(func(x): return x.kind == "wave_wall"), "block(wave_wall) reported")
			check(_count("wave_blocked") == 1, "wave_blocked once")
			check(e.health == 100.0 and not h.has_event("hit"), "E untouched behind the wall (health %.1f)" % e.health)
			check(w_body != null and stone.pos.z > w_body.pos.z, "the lava stayed on P's side of the wall (z %.2f vs wall %.2f)" % [stone.pos.z, w_body.pos.z if w_body else 0.0])
			check(run.min_z > 0.5, "it never reached E (min z %.2f)" % run.min_z)
		else:
			var hits := h.events("hit").filter(func(x): return x.actor == e.id and x.kind == "lava")
			check(hits.size() == 1, "without a wall the wave hits E exactly once, got %d" % hits.size())
			check(e.health < 100.0, "E is hurt (%.1f)" % e.health)
			check(_count("wave_blocked") == 0, "nothing blocked it")


func test_wave_lifecycle_conserves_stone_mass_and_energy() -> void:
	_world(Vector3(-3.75, 0, 2.5), Vector3(-3.75, 0.2, 1.5))
	var m0 := h.w.stone_mass()
	var e0 := h.w.system_energy()
	near(m0, 20.0, 1e-9, "setup: one 20 kg stone")
	if not _pour(Vector3(0, 0, -1)):
		return
	var worst_m := 0.0
	var worst_e := 0.0
	var gone_at := -1
	for k in 3600:   # 60 s: pour, block, cool, rock decays after REMNANT_LIFETIME
		h.step()
		worst_m = maxf(worst_m, absf(h.w.stone_mass() - m0))
		if k % 30 == 0:
			worst_e = maxf(worst_e, absf((h.w.system_energy() - e0) - h.w.ledger_balance()))
		if gone_at < 0 and not stone.alive:
			gone_at = k
	check(worst_m < 1e-6, "stone mass ledger drifted by %.8f kg" % worst_m)
	check(worst_e < 0.05, "energy ledger drifted by %.4f HU" % worst_e)
	check(gone_at > 0, "the cooled rock eventually crumbles back into the ground")
	if gone_at > 0:
		check(gone_at > 40 * Sim.HZ, "but only after its remnant lifetime (tick %d)" % gone_at)
		near(h.w.mass_ledger.ground_returned, 20.0, 1e-9, "its mass went back to the ground ledger")


# ---------------------------------------------------------------- steering

func test_wave_bends_toward_the_target_within_the_turn_rate() -> void:
	# E stands 25 degrees off the pour direction; the wave may bend toward it, slowly.
	var e_pos := Vector3(7.0, 0, 7.0 - 15.0)
	_world(Vector3(0, 0, 8), Vector3(0, 0.2, 7), 20.0, e_pos)
	if not _pour(Vector3(0, 0, -1)):
		return
	var dir0 := stone.wave_dir
	var prev := dir0
	var max_step := 0.0
	var total := 0.0
	for k in 300:
		h.step()
		if stone.form != Sim.Form.WAVE:
			break
		var a := atan2(stone.wave_dir.x, stone.wave_dir.z)
		var b := atan2(prev.x, prev.z)
		var d := absf(wrapf(a - b, -PI, PI))
		max_step = maxf(max_step, d)
		total += d
		prev = stone.wave_dir
	var limit := deg_to_rad(CombatWorld.WAVE_TURN_RATE) * Sim.DT
	check(max_step <= limit + 1e-6, "per-tick turn %.5f rad exceeds the rate limit %.5f" % [max_step, limit])
	note("steering: max %.4f rad/tick (limit %.4f), total %.1f deg" % [max_step, limit, rad_to_deg(total)])
	check(total > deg_to_rad(2.0), "the wave did bend toward its target (%.1f degrees)" % rad_to_deg(total))
	check(stone.wave_dir.x > 0.0, "and it bent the right way (toward +x)")
	# A target more than 70 degrees off the heading is ignored.
	_world(Vector3(0, 0, 8), Vector3(0, 0.2, 7), 20.0, Vector3(14.0, 0, 6.0))
	if _pour(Vector3(0, 0, -1)):
		var d0 := stone.wave_dir
		_follow()
		check(stone.wave_dir.distance_to(d0) < 1e-9, "a target behind/beside the wave does not steer it")
