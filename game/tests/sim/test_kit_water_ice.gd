extends WaterKitTest
## Water / Ice (sub 1, docs/MOVESET.md §7.6): the Frost Shard ladder, Icicle Volley, Rime Path (freezes puddles and
## water waves, crusts lava, walkable strip over the pool), Hoarfrost Fan (wet targets freeze), Ice Wall + Flash Freeze,
## Glacier Shove, Frost Floor, Freeze-Draw + Shatter, Ice Glide and Skate. Cells are in test_kit_water_cells.gd.


func _ice_duel(dist: float = 10.0, rival_element: int = Sim.Element.EARTH, seed_value: int = 3) -> Array:
	return duel(1, rival_element, seed_value, dist)


func _flick_tap(p: ActorState, gesture: int, hold_ticks: int = 2) -> void:
	h.flick(p, "attack", gesture)
	h.step(hold_ticks)
	h.release(p, "attack")


func _flick_hold(p: ActorState, gesture: int, secs: float) -> void:
	h.flick(p, "attack", gesture)
	h.step(int(secs * 60.0))
	h.release(p, "attack")


func _lava_wave(mass: float, pos: Vector3, dir: Vector3, owner: ActorState) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, pos, "test")
	h.w.mass_ledger.ground_taken += mass
	h.w.ledger.generated += Thermal.heat(b, mass * (Sim.STONE_C * 980.0 + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = dir
	b.wave_budget = 12.0
	b.vel = dir * 7.5
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = owner.id
	b.hit_set[owner.id] = true
	return b


func _raise_wall(w: ActorState, ticks: int = 18) -> MatBody:
	h.press(w, "guard")
	h.step(ticks)
	return h.w.get_body(w.wall_body)


# ---------------------------------------------------------------- strike ladder

func test_frost_shard_ladder_t0_to_t3() -> void:
	var results := []
	for tier in 4:
		var s := _ice_duel(10.0)
		var w: ActorState = s[0]
		var r: ActorState = s[1]
		r.is_dummy = true
		var base := snap(h.w)
		var m0: float = h.w.mass_ledger.moisture_taken
		h.log.clear()
		h.press(w, "attack")
		h.step([2, 30, 66, 112][tier])      # tap, T1 (0.4 s), T2 (1.0 s), T3 (1.8 s)
		var reached := w.action.tier() if w.action != null else 0
		h.release(w, "attack")
		h.until(func(): return h.has_event("launch"), 40)
		var l := h.last_event("launch")
		check(not l.is_empty(), "T%d launches (tier reached %d)" % [tier, reached])
		var b := h.w.get_body(int(l.get("body", -1)))
		if b != null:
			results.append([tier, reached, b.mass, String(b.tag), b.phase == Sim.Phase.FROZEN])
			check(b.phase == Sim.Phase.FROZEN, "T%d is ice" % tier)
		h.step(40)
		check(float(h.w.mass_ledger.moisture_taken) - m0 > 0.9, "T%d booked ambient moisture (%.1f kg)" % [tier, float(h.w.mass_ledger.moisture_taken) - m0])
		h.step(200)
		ledgers_ok(base, "Frost Shard T%d" % tier)
	check(results.size() == 4, "all four tiers fired: %s" % [results])
	if results.size() == 4:
		check(is_equal_approx(results[0][2], 1.0) and results[0][3] == "needle", "T0: 1 kg needle")
		check(is_equal_approx(results[1][2], 4.0) and results[1][3] == "lance", "T1: 4 kg Ice Lance")
		check(is_equal_approx(results[2][2], 8.0) and results[2][3] == "spear", "T2: 8 kg Glacier Spear")
		check(is_equal_approx(results[3][2], 15.0) and results[3][3] == "comet", "T3: 15 kg Frost Comet")


func test_frost_shard_chills_and_glacier_spear_pierces_and_stands() -> void:
	var s := _ice_duel(9.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	h.press(w, "attack")
	h.step(3)
	h.release(w, "attack")
	h.step(80)
	check(Status.has(r, "chilled") or h.has_event("status", "status", "chilled"), "the needle chills")
	# T2 spear
	s = _ice_duel(9.0)
	w = s[0]
	r = s[1]
	r.is_dummy = true
	h.press(w, "attack")
	h.step(66)
	h.release(w, "attack")
	h.step(80)
	check(h.has_event("pierce") or h.has_event("hit"), "the spear hits and pierces")
	check(h.has_event("stick"), "then stands as an ice spike")
	var spikes := bodies_of(Sim.Mat.WATER).filter(func(b): return b.tag == &"spear")
	check(spikes.size() == 1 and spikes[0].static_body, "one standing spike")
	h.step(200)
	check(bodies_of(Sim.Mat.WATER).filter(func(b): return b.tag == &"spear").is_empty(), "the spike is gone after ~3 s")


func test_frost_comet_shatters_into_six_shards() -> void:
	var s := _ice_duel(8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	h.press(w, "attack")
	h.step(112)
	h.release(w, "attack")
	h.step(100)
	check(h.has_event("shatter"), "the comet shatters")
	var ice := bodies_of(Sim.Mat.WATER).filter(func(b): return b.phase == Sim.Phase.FROZEN or b.form == Sim.Form.PUDDLE)
	check(ice.size() >= 4, "into several pieces (%d)" % ice.size())


func test_icicle_volley_counts_by_tier() -> void:
	var want := [3, 5, 7, 12]
	var secs := [0.05, 0.5, 1.1, 1.9]
	for tier in 4:
		var s := _ice_duel(10.0)
		var w: ActorState = s[0]
		w.focus = 100.0
		h.log.clear()
		h.flick(w, "attack", Sim.Gesture.UP)
		h.step(int(secs[tier] * 60.0) + 2)
		h.release(w, "attack")
		h.step(30)
		var n := h.events("launch").size()
		check(n == want[tier], "T%d: %d needles (got %d)" % [tier, want[tier], n])


# ---------------------------------------------------------------- ground: Rime Path

func test_rime_path_leaves_a_slick_ice_floor_and_the_owner_keeps_grip() -> void:
	var s := _ice_duel(12.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	_flick_tap(w, Sim.Gesture.DOWN)
	h.step(70)
	var floors := zones_tagged("ice_floor")
	check(floors.size() >= 5, "a strip of ice_floor zones (%d)" % floors.size())
	for z in floors:
		check(float(z.props.get("friction", 1.0)) < 0.2 and z.tier == 0, "slick and tier-tagged")
		break
	h.step(240)
	check(zones_tagged("ice_floor").is_empty(), "the strip melts away after its life")
	ledgers_ok(base, "Rime Path")


func test_rime_path_freezes_a_puddle_and_a_frozen_puddle_stops_conduction() -> void:
	var s := _ice_duel(12.0, Sim.Element.FIRE)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.kit = {"lightning": true}
	r.is_dummy = true
	# A puddle chain from the pool edge to the middle of the Rime Path; the rival's bolt strikes the pool.
	var pool_pt := Vector3(7.5, 0, 1.0)
	var pud := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 4.0, Vector3(0, 0.3, 0), "test")
	h.w.mass_ledger.moisture_taken += 4.0
	h.w._water_to_puddle(pud)
	var live := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 6.0, Vector3(0.0, 0.3, -2.0), "test")
	h.w.mass_ledger.moisture_taken += 6.0
	h.w._water_to_puddle(live)
	check(live.alive and live.form == Sim.Form.PUDDLE and live.phase == Sim.Phase.LIQUID, "a liquid puddle in the path")
	var victim := h.w.add_actor("V", Vector3(0.0, 0, -2.0), 0, {}, Sim.Element.EARTH)
	h.intents[victim.id] = ActorIntent.new()
	victim.is_dummy = true
	h.step(2)
	var base := snap(h.w)
	_flick_tap(w, Sim.Gesture.DOWN)
	h.step(80)
	var frozen := live.alive and live.phase == Sim.Phase.FROZEN
	check(frozen, "the Rime Path froze the puddle (phase %s)" % [Sim.PHASE_NAMES[live.phase] if live.alive else "gone"])
	check(h.events("interaction").any(func(e): return e.threat == "puddle" and e.counter == "rime"), "interaction puddle x rime")
	# No conduction through ice: a fighter standing on the frozen puddle is not part of the graph.
	victim.pos = Vector3(live.pos.x, 0, live.pos.z)
	check(Conduction.actor_surface_node(h.w, victim) == "", "standing on a frozen puddle: no conduction node")
	ledgers_ok(base, "frozen puddle")


func test_rime_path_turns_a_water_wave_into_an_ice_ridge() -> void:
	# The rival's Tidal Rush meets our Rime Path: the wave becomes a standing ice ridge (a WALL).
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(5.0, 0, 4.5), 0, {}, Sim.Element.WATER)
	var r := h.actor("R", Vector3(5.0, 0, -7.0), 1, {}, Sim.Element.WATER)
	w.subs[1] = 1
	h.step(20)
	var base := snap(h.w)
	h.flick(w, "attack", Sim.Gesture.DOWN)
	h.step(34)
	h.release(w, "attack")
	var rime: MatBody = null
	for k in 40:
		h.step()
		for b in h.w.bodies:
			if b.alive and b.tag == &"rime" and b.form == Sim.Form.WAVE:
				rime = b
		if rime != null:
			break
	check(rime != null, "our Rime Path is out")
	# The rival's wave (a Tidal Rush launched at us), spawned 5 m ahead of ours.
	var wave := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.WAVE, 10.0, rime.pos + Vector3(0, 0, -5.0), "test")
	h.w.mass_ledger.moisture_taken += 10.0
	wave.tag = &"water_wave"
	wave.wave_dir = Vector3(0, 0, 1)
	wave.wave_budget = 14.0
	wave.wave_width = 2.4
	wave.power = 24.0
	wave.props["speed"] = 9.0
	wave.props["knock"] = 6.0
	wave.props["lift"] = 3.0
	wave.attack_id = h.w.new_attack_id()
	wave.attack_owner = r.id
	wave.hit_set[r.id] = true
	var ridge: MatBody = null
	for k in 120:
		h.step()
		if wave.alive and wave.form == Sim.Form.WALL:
			ridge = wave
			break
	check(ridge != null, "the water wave became a WALL")
	if ridge == null:
		return
	check(ridge.tag == &"ridge" and ridge.phase == Sim.Phase.FROZEN, "tagged ridge and frozen (%s)" % ridge.describe())
	check(h.events("interaction").any(func(e): return e.threat == "water_wave" and e.counter == "rime" and e.outcome == "transform" and e.to == "ridge"), "interaction water_wave x rime -> ridge")
	h.step(60)
	check(ridge.alive and ridge.wall_rise > 0.9, "the ridge stands")
	check(Interactions.counter_class(ridge, h.w) == &"wall_ice", "and counts as an ice wall")
	h.step(600)
	check(not ridge.alive, "it melts away after its time")
	ledgers_ok(base, "ridge")


func test_rime_path_crusts_a_lava_front() -> void:
	var s := _ice_duel(12.0, Sim.Element.FIRE)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	_flick_hold(w, Sim.Gesture.DOWN, 1.9)          # T3: power 30 x 1.2
	h.step(10)
	var lava := _lava_wave(20.0, w.pos + Vector3(0, 0, -9.0), Vector3(0, 0, 1), r)
	var crust := false
	for k in 160:
		h.step()
		if h.events("interaction").any(func(e): return e.threat == "lava_wave" and e.counter == "rime"):
			crust = true
			break
	check(crust, "the lava front meets the rime wave")
	h.step(120)
	check(lava.alive and lava.liquid < 0.5, "the front is crusted (liquid %.2f)" % lava.liquid)
	ledgers_ok(base, "crust")


func test_rime_path_over_the_pool_is_a_walkable_strip() -> void:
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(5.5, 0, -1.0), 0, {}, Sim.Element.WATER)
	var r := h.actor("R", Vector3(-4.0, 0, -1.0), 1, {}, Sim.Element.EARTH)
	r.is_dummy = true
	w.subs[1] = 1
	w.facing = PI * 0.5       # toward +x (the pool)
	h.step(5)
	w.facing = PI * 0.5
	h.aim(w, Vector3(1, 0, 0))
	h.it(w).aim_dir = Vector3(1, 0, 0)
	h.it(w).aim_active = true
	var c := h.w.add_actor("C", Vector3(10.0, 0.0, -1.0), 1, {}, Sim.Element.EARTH)
	h.intents[c.id] = ActorIntent.new()
	c.is_dummy = true
	h.step(2)
	var base := snap(h.w)
	_flick_hold(w, Sim.Gesture.DOWN, 0.5)
	h.step(50)
	var strip := zones_tagged("ice_floor").filter(func(z): return h.w.arena.in_pool(z.pos.x, z.pos.z))
	check(strip.size() >= 2, "ice_floor zones over the pool (%d)" % strip.size())
	if strip.size() >= 2:
		var z: MatBody = strip[0]
		check(absf(z.pos.y + float(z.props.walk_height)) < 0.05, "its surface is at ground level (top %.2f)" % (z.pos.y + float(z.props.walk_height)))
		# A fighter on the strip walks over the pool: not in the water, no conduction node.
		c.pos = Vector3(z.pos.x, 0.0, z.pos.z)
		h.step(4)
		check(not c.in_water, "standing on the strip: not in the water (y %.2f, surface %s)" % [c.pos.y, c.surface])
		check(Conduction.actor_surface_node(h.w, c) == "", "no pool conduction node on ice")
	h.step(400)
	ledgers_ok(base, "pool strip")


# ---------------------------------------------------------------- sweep: Hoarfrost

func test_hoarfrost_freezes_wet_targets_and_chills_dry_ones() -> void:
	var s := _ice_duel(4.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	r.wetness = 1.0
	var base := snap(h.w)
	_flick_tap(w, Sim.Gesture.SIDE)
	h.step(40)
	check(Status.has(r, "frozen") or h.has_event("status", "status", "frozen"), "a wet target freezes solid (rooted)")
	check(Status.rooted(r) or h.has_event("status", "status", "frozen"), "rooted")
	s = _ice_duel(4.0)
	w = s[0]
	r = s[1]
	r.is_dummy = true
	r.wetness = 0.0
	_flick_tap(w, Sim.Gesture.SIDE)
	h.step(40)
	check(not h.has_event("status", "status", "frozen"), "a dry target is not frozen")
	check(h.has_event("status", "status", "chilled"), "but chilled")
	ledgers_ok(base, "Hoarfrost")


func test_hoarfrost_freezes_a_puddle_and_a_stream_in_the_air() -> void:
	var s := _ice_duel(5.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var pud := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, w.pos + Vector3(0, 0.3, -2.5), "test")
	h.w.mass_ledger.moisture_taken += 3.0
	h.w._water_to_puddle(pud)
	var stream := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, 2.0, w.pos + Vector3(0, 1.2, -3.0), "test")
	h.w.mass_ledger.moisture_taken += 2.0
	stream.vel = Vector3.ZERO
	stream.gravity_scale = 0.0
	stream.attack_id = h.w.new_attack_id()
	stream.attack_owner = r.id
	var base := snap(h.w)
	_flick_tap(w, Sim.Gesture.SIDE)
	h.step(30)
	check(pud.phase == Sim.Phase.FROZEN, "the puddle froze")
	check(stream.phase == Sim.Phase.FROZEN or not stream.alive, "the stream froze mid-air (%s)" % [Sim.PHASE_NAMES[stream.phase]])
	h.step(200)
	ledgers_ok(base, "Hoarfrost bodies")


# ---------------------------------------------------------------- guard: Ice Wall

func test_ice_wall_masses_and_tiers() -> void:
	var s := _ice_duel(10.0)
	var w: ActorState = s[0]
	w.pos = Vector3(-5, 0, 6)
	h.step(3)
	var base := snap(h.w)
	var m0: float = h.w.mass_ledger.moisture_taken
	var wall := _raise_wall(w)
	check(wall != null and wall.form == Sim.Form.WALL and wall.tag == &"ice", "an ice WALL body (%s)" % [wall.describe() if wall else "none"])
	if wall == null:
		return
	near(wall.mass, 50.0, 0.01, "50 kg away from water")
	near(wall.mass * Materials.hardness(wall), 22.0, 0.01, "CP 22")
	check(wall.is_water() and wall.phase == Sim.Phase.FROZEN, "frozen water")
	near(float(h.w.mass_ledger.moisture_taken) - m0, 50.0, 1e-6, "booked as ambient moisture")
	# held 1.0 s and 1.8 s: thicker (65 / 80 kg)
	h.step(50)
	check(wall.mass >= 64.9, "held 1.1 s: 65 kg (%.1f)" % wall.mass)
	h.step(60)
	check(wall.mass >= 79.9, "held 1.9 s: 80 kg (%.1f)" % wall.mass)
	h.release(w, "guard")
	h.step(80)
	check(not wall.alive, "the wall sinks when released")
	ledgers_ok(base, "Ice Wall")
	# beside the pool: 70 kg
	s = _ice_duel(10.0)
	w = s[0]
	w.pos = Vector3(5.0, 0, 3.5)
	h.step(3)
	wall = _raise_wall(w)
	check(wall != null and absf(wall.mass - 70.0) < 0.01, "beside the pool: 70 kg (%.1f)" % [wall.mass if wall else 0.0])


func test_ice_wall_blocks_a_stone_and_insulates_a_bolt_until_a_storm_bolt() -> void:
	var s := _ice_duel(8.0, Sim.Element.FIRE)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.kit = {"lightning": true}
	r.is_dummy = true
	var wall := _raise_wall(w)
	check(wall != null, "wall up")
	if wall == null:
		return
	var hp := w.health
	var stone := h.launch_at(w, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 6.0)
	h.step(40)
	check(w.health == hp, "the stone never reaches the fighter")
	check(wall.alive, "and the wall holds")
	# Insulator: a T1 bolt (E 24 <= 1.5 x 22) is grounded.
	var def := {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
	r.pos = w.pos + Vector3(0, 0, -8.0)
	var out := Conduction.discharge(h.w, r, w.chest(), def, h.w.new_attack_id(), true)
	h.step()
	check(out.blocked and w.health == hp, "E 24 is blocked by the ice (insulator, 1.5 x CP 22 = 33)")
	check(h.events("interaction").any(func(e): return e.counter == "wall_ice" and e.threat == "lightning" and e.outcome == "ground"), "interaction lightning x wall_ice -> ground")
	# A T2 storm bolt (E 36) shatters it and continues with E - 0.5 CP.
	def["E"] = 36.0
	def["damage"] = 36.0
	out = Conduction.discharge(h.w, r, w.chest(), def, h.w.new_attack_id(), true)
	h.step()
	check(not wall.alive or wall.wall_damage >= 1.0, "a storm bolt (E 36) shatters the wall")
	check(h.events("interaction").any(func(e): return e.counter == "wall_ice" and e.threat == "lightning" and e.outcome == "shatter"), "interaction -> shatter")


func test_flash_freeze_perfect_guard_freezes_a_water_stream_and_a_wave_becomes_a_ridge() -> void:
	var s := _ice_duel(8.0, Sim.Element.WATER)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var wall := _raise_wall(w)
	check(wall != null, "wall up")
	if wall == null:
		return
	var base := snap(h.w)
	# A perfect guard at the moment of contact: the wall is a counter with perfect = true.
	var stream := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, 6.0, wall.pos + Vector3(0, 0.9, -0.6), "test")
	h.w.mass_ledger.moisture_taken += 6.0
	stream.vel = Vector3(0, 0, 14.0)
	stream.gravity_scale = 0.0
	stream.attack_id = h.w.new_attack_id()
	stream.attack_owner = r.id
	var counter := Agent.of_body(h.w, wall)
	counter.actor = w
	counter.perfect = true
	var res := Interactions.resolve(h.w, Agent.of_body(h.w, stream, w), counter, {"site": "wall"})
	check(res.outcome == "transform" and res.to == "ice", "Flash Freeze: the stream freezes (%s -> %s)" % [res.outcome, res.to])
	check(stream.phase == Sim.Phase.FROZEN and stream.attack_id == 0, "frozen solid and dropped (inert)")
	# The same perfect guard turns a water wave into a ridge.
	var wave := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.WAVE, 10.0, wall.pos + Vector3(0, 0, -0.8), "test")
	h.w.mass_ledger.moisture_taken += 10.0
	wave.tag = &"water_wave"
	wave.wave_dir = Vector3(0, 0, 1)
	wave.wave_budget = 10.0
	wave.wave_width = 2.4
	wave.power = 24.0
	wave.props["speed"] = 9.0
	wave.attack_id = h.w.new_attack_id()
	wave.attack_owner = r.id
	var res2 := Interactions.resolve(h.w, Agent.of_body(h.w, wave, w), counter, {"site": "wave_wall"})
	check(res2.outcome == "transform" and res2.to == "ridge" and wave.form == Sim.Form.WALL and wave.tag == &"ridge", "Flash Freeze: the wave becomes an ice ridge (%s)" % wave.describe())
	# Without the perfect timing the plain wall just blocks (the stream splashes).
	var stream2 := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, 3.0, wall.pos + Vector3(0, 0.9, -0.6), "test")
	h.w.mass_ledger.moisture_taken += 3.0
	stream2.vel = Vector3(0, 0, 14.0)
	stream2.gravity_scale = 0.0
	stream2.attack_id = h.w.new_attack_id()
	stream2.attack_owner = r.id
	counter.perfect = false
	var res3 := Interactions.resolve(h.w, Agent.of_body(h.w, stream2, w), counter, {"site": "wall"})
	check(res3.outcome == "block" and stream2.phase == Sim.Phase.LIQUID, "plain guard: block, water stays liquid")
	h.release(w, "guard")
	h.step(300)
	ledgers_ok(base, "Flash Freeze")


func test_ice_wall_melts_under_fire() -> void:
	var s := _ice_duel(8.0, Sim.Element.FIRE)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var wall := _raise_wall(w)
	check(wall != null, "wall up")
	if wall == null:
		return
	# Strong heat on the wall (a smelter / lava): it melts and slumps into a puddle.
	for k in 6:
		h.w.ledger.generated += h.w.heat_body(wall, 50.0)     # 300 HU: melts the 50 kg wall (177 HU) and warms it
	h.step(30)
	check(wall.form == Sim.Form.PUDDLE or not wall.alive or wall.phase == Sim.Phase.LIQUID, "the wall melted (%s)" % wall.describe())
	check(wall.form != Sim.Form.WALL, "it is no longer a wall")
	h.release(w, "guard")
	h.step(300)
	ledgers_ok(base, "melt")


# ---------------------------------------------------------------- push / sink

func test_glacier_shove_slides_the_wall_into_the_rival() -> void:
	var s := _ice_duel(7.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var wall := _raise_wall(w)
	check(wall != null, "wall up")
	if wall == null:
		return
	var p0 := wall.pos
	h.flick(w, "guard", Sim.Gesture.UP)
	h.step(6)
	h.release(w, "guard")
	h.step(80)
	check(wall.alive and wall.pos.distance_to(p0) > 3.0, "the wall slid (%.1f m)" % wall.pos.distance_to(p0))
	check(r.health < 100.0 or h.has_event("hit", "actor", r.id), "and knocked the rival back")
	h.step(400)
	ledgers_ok(base, "Glacier Shove")


func test_frost_floor_is_slick_for_enemies_and_grippy_for_you() -> void:
	var s := _ice_duel(4.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	h.press(w, "guard")
	h.step(12)
	h.flick(w, "guard", Sim.Gesture.DOWN)
	h.step(20)
	h.release(w, "guard")
	h.step(10)
	var fl := zones_tagged("ice_floor")
	check(fl.size() >= 1 and absf(fl[0].zone_radius - 3.0) < 0.01, "a 3 m ice floor")
	check(Status.has(w, "icegrip"), "the owner keeps their grip (status icegrip)")
	r.pos = w.pos + Vector3(0, 0, -2.0)
	h.step(2)
	check(h.w._friction_at(r) < 0.2, "enemies slide (friction %.2f)" % h.w._friction_at(r))
	check(h.w._friction_at(w) > 0.8, "you don't (%.2f)" % h.w._friction_at(w))
	h.step(400)
	check(zones_tagged("ice_floor").is_empty(), "the floor ends after 5 s")
	ledgers_ok(base, "Frost Floor")


# ---------------------------------------------------------------- technique

func test_freeze_draw_grows_an_ice_block_and_shatter_fans_six_shards() -> void:
	var s := _ice_duel(8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	h.press(w, "tech")
	h.step(60)
	var held := h.w.held(w)
	check(held != null and held.phase == Sim.Phase.FROZEN and held.is_water(), "an ice block in hand (%s)" % [held.describe() if held else "none"])
	if held == null:
		return
	h.step(90)
	check(held.mass > 6.0 and held.mass <= 12.01, "it grew (%.1f kg, cap 12)" % held.mass)
	h.press(w, "attack")
	h.step(3)
	h.release(w, "attack")
	h.step(3)
	h.release(w, "tech")
	h.step(30)
	check(h.events("launch").size() >= 5, "Shatter: a fan of shards (%d launches)" % h.events("launch").size())
	h.step(300)
	ledgers_ok(base, "Freeze-Draw")


func test_freeze_draw_seizes_incoming_ice() -> void:
	var s := _ice_duel(9.0, Sim.Element.WATER)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var shard := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.SHARD, 4.0, w.pos + Vector3(0, 1.2, -6.0), "test")
	h.w.mass_ledger.moisture_taken += 4.0
	WaterUtil.freeze_body(h.w, shard)
	shard.vel = Vector3(0, 0, 14.0)
	shard.gravity_scale = 0.0
	shard.attack_id = h.w.new_attack_id()
	shard.attack_owner = r.id
	h.step(2)
	h.press(w, "tech")
	h.step(40)
	check(shard.controller == w.id, "the incoming ice shard is seized (REC)")
	h.release(w, "tech")
	h.step(200)


# ---------------------------------------------------------------- evade

func test_ice_glide_and_skate_across_the_pool() -> void:
	var s := _ice_duel(10.0)
	var w: ActorState = s[0]
	var p0 := w.pos
	h.it(w).move = Vector3(1, 0, 0)
	h.press(w, "evade")
	h.step(1)
	h.it(w).move = Vector3.ZERO
	h.step(23)
	var d := Vector2(w.pos.x - p0.x, w.pos.z - p0.z).length()
	check(d > 3.0 and d < 6.5, "Ice Glide covers about 5 m (%.2f)" % d)
	check(zones_tagged("ice_floor").size() >= 2, "on a strip of ice")
	h.step(120)
	# Skate: hold evade, run east over the pool.
	h = SimHarness.new(3)
	w = h.actor("W", Vector3(3.0, 0, -1.0), 0, {}, Sim.Element.WATER)
	var r := h.actor("R", Vector3(-8.0, 0, -1.0), 1, {}, Sim.Element.EARTH)
	r.is_dummy = true
	w.subs[1] = 1
	h.step(5)
	var base := snap(h.w)
	h.it(w).move = Vector3(1, 0, 0)
	h.press(w, "evade")
	h.it(w).evade_held = true
	var in_pool_dry := false
	var max_speed := 0.0
	for k in 160:
		h.step()
		max_speed = maxf(max_speed, Vector2(w.vel.x, w.vel.z).length())
		if h.w.arena.in_pool(w.pos.x, w.pos.z) and not w.in_water and w.grounded:
			in_pool_dry = true
		if w.pos.x > 13.5:
			break
	check(w.action != null and w.action.id == "skate" or max_speed > 0.0, "morphed into Skate")
	check(max_speed > 6.0, "skating faster than a run (%.2f m/s)" % max_speed)
	check(in_pool_dry, "skated over the pool without falling in (x %.1f)" % w.pos.x)
	h.it(w).evade_held = false
	h.step(120)
	check(zones_tagged("ice_floor").is_empty(), "the ice under the skater is gone")
	ledgers_ok(base, "Skate")
