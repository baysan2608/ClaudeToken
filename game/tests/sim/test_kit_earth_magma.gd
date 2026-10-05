extends TestCase
## Earth / Magma (sub 3): Ember Clot ladder (globs, bomb -> lava puddle, Caldera pool), Lava Lash /
## Molten Lance (wall face -> slump), Magma Surge / Lava Tide (re-pour: "push it back on them"), Spatter,
## Magma Curtain / Obsidian Set, Slag Wave, Melt Pit, Magma Hold / Cool & Set / Reverse Tide, Cinder Step,
## Lava Wade and the Magma column of MOVESET §8.1. Every HU is booked (energy identity).

const U := preload("res://tests/sim/test_kit_earth_util.gd")

var h: SimHarness


func _duel(dist: float = 7.0, t_elem: int = Sim.Element.FIRE) -> Array:
	h = SimHarness.new(6)
	return U.duel(h, 3, t_elem, dist)


func test_ember_clot_ladder_globs_bomb_and_caldera() -> void:
	for tier in 4:
		var s := _duel(9.0)
		var a: ActorState = s[0]
		var t: ActorState = s[1]
		var e0 := U.e0(h)
		var sm0 := h.w.stone_mass()
		U.perform(h, a, "strike", tier)
		h.until(func(): return h.has_event("launch"), 40)
		var globs := h.events("launch").filter(func(e): return e.actor == a.id).map(func(e): return h.w.get_body(int(e.body)))
		check(globs.size() == (3 if tier == 1 else 1), "T%d: %d glob(s)" % [tier, globs.size()])
		h.step()
		for g in globs:
			check(g != null and g.is_stone() and g.liquid > 0.9, "T%d: molten (liquid %.2f)" % [tier, g.liquid if g else -1.0])
		if tier <= 1:
			h.until(func(): return t.status.has("burning"), 60)
			check(t.status.has("burning"), "T%d: the glob burns the target" % tier)
		else:
			var n := h.until(func(): return h.w.bodies.any(func(b): return b.alive and b.tag == &"lava_pool"), 90)
			check(n > 0, "T%d: the bomb splashes into a lava pool" % tier)
			var pool: MatBody = h.w.bodies.filter(func(b): return b.alive and b.tag == &"lava_pool")[0] if n > 0 else null
			if pool != null:
				near(pool.zone_radius, 1.75 if tier == 3 else 1.0, 1e-6, "T%d pool radius" % tier)
				near(pool.mass, 20.0 if tier == 3 else 12.0, 1e-6, "the pool is the bomb's lava")
				check(pool.is_stone() and pool.liquid > 0.5, "still molten")
		check(U.energy_drift(h, e0) < 1e-5, "T%d: every HU booked (%.6f)" % [tier, U.energy_drift(h, e0)])
		near(h.w.stone_mass(), sm0, 1e-6, "T%d: stone from the ground (booked)" % tier)


func test_magma_surge_pushes_a_slumped_wall_back_at_the_builder() -> void:
	# Owner example, second half of Melt & Return: the rival's wall face has slumped into a molten pool
	# on this side; Magma Surge re-pours it as a wave that reaches the builder.
	var s := _duel(8.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var e0 := U.e0(h)
	var slump := U.lava(h, 30.0, Vector3(0, 0.1, 0.5), 0.55)
	h.step()
	U.perform(h, a, "ground", 0)
	h.until(func(): return slump.form == Sim.Form.WAVE, 40)
	check(slump.form == Sim.Form.WAVE and slump.attack_owner == a.id, "the slump becomes A's lava wave")
	check(slump.wave_dir.dot(t.pos - slump.pos) > 0.0, "flowing at the builder")
	var n := h.until(func(): return t.health < 100.0, 180)
	check(n > 0, "it reaches the builder (%.1f)" % t.health)
	check(U.energy_drift(h, e0) < 1e-5, "energy booked (%.6f)" % U.energy_drift(h, e0))


func test_melt_and_return_entirely_with_magma() -> void:
	# Two Molten Lances (300 HU each) slump the rival's Bulwark face; Magma Surge sends it back.
	var s := _duel(7.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var wall := U.wall(h, Sim.Mat.STONE, "", 120.0, Vector3(0, 0, -1.6), t, 0.0)
	var e0 := U.e0(h)
	var sm0 := h.w.stone_mass()
	a.focus = 100.0
	U.perform(h, a, "thrust", 3)
	h.until(func(): return a.action == null, 120)
	check(float(wall.props.get("face_hu", 0.0)) > 250.0 or h.has_event("slump"), "the first lance pours its heat into the face")
	a.focus = 100.0
	a.heat_reserve = 300.0
	h.w.ledger.generated += 300.0
	U.perform(h, a, "thrust", 3)
	var n := h.until(func(): return h.has_event("slump"), 120)
	check(n > 0, "the second lance slumps the face")
	check(not wall.alive, "the rest of the wall crumbles")
	h.until(func(): return a.action == null, 120)
	var face := h.w.get_body(int(h.last_event("slump").get("body", -1)))
	check(face != null and face.liquid >= 0.5, "a molten face on A's side (%.2f)" % (face.liquid if face else -1.0))
	a.focus = 100.0
	U.perform(h, a, "ground", 0)
	h.until(func(): return face != null and face.form == Sim.Form.WAVE, 40)
	check(face != null and face.form == Sim.Form.WAVE and face.attack_owner == a.id, "Magma Surge re-pours it")
	h.until(func(): return t.health < 100.0, 180)
	check(t.health < 100.0, "back on the builder")
	near(h.w.stone_mass(), sm0, 1e-6, "stone mass conserved")
	check(U.energy_drift(h, e0) < 1e-5, "energy booked (%.6f)" % U.energy_drift(h, e0))


func test_magma_surge_raises_a_vein_and_lava_tide_merges() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var e0 := U.e0(h)
	U.perform(h, a, "ground", 0)
	h.until(func(): return h.has_event("magma_surge"), 40)
	var ev := h.last_event("magma_surge")
	check((ev.waves as Array).size() == 1, "no lava near: one vein wave")
	var v := h.w.get_body(int(ev.waves[0]))
	check(v != null and is_equal_approx(v.mass, 8.0) and v.liquid > 0.9, "an 8 kg molten vein (160 HU)")
	check(U.energy_drift(h, e0) < 1e-5, "booked")
	var s2 := _duel(9.0)
	var a2: ActorState = s2[0]
	U.lava(h, 12.0, Vector3(1.5, 0.1, 2.5))
	U.lava(h, 20.0, Vector3(-1.5, 0.1, 2.0))
	var pool := U.lava(h, 12.0, Vector3(0.0, 0.1, 1.5))
	pool.form = Sim.Form.ZONE
	pool.tag = &"lava_pool"
	pool.zone_radius = 1.0
	U.perform(h, a2, "ground", 0)
	h.until(func(): return h.has_event("magma_surge"), 40)
	check((h.last_event("magma_surge").waves as Array).size() == 3, "T0: every molten body within 6 m becomes a wave (incl. the pool)")
	var s3 := _duel(9.0)
	var a3: ActorState = s3[0]
	U.lava(h, 12.0, Vector3(1.5, 0.1, 2.5))
	U.lava(h, 20.0, Vector3(-1.5, 0.1, 2.0))
	U.perform(h, a3, "ground", 3)
	h.until(func(): return h.has_event("magma_surge"), 40)
	var ev3 := h.last_event("magma_surge")
	check((ev3.waves as Array).size() == 1, "T3 Lava Tide: merged into one wave")
	var big := h.w.get_body(int(ev3.waves[0]))
	check(big != null and is_equal_approx(big.mass, 32.0), "32 kg of lava")


func test_magma_curtain_sticks_absorbs_and_sets_obsidian() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var e0 := U.e0(h)
	h.press(a, "guard")
	h.step(20)
	var wall := h.w.get_body(a.wall_body)
	check(wall != null and wall.tag == &"obsidian" and wall.heat_payload > 100.0, "a curtain with a molten face")
	near(Interactions.counter_power(Agent.of_body(h.w, wall)), 28.0, 1e-6, "CP 28")
	var stone := h.launch_at(a, "stone", 10.0, 17.0, Sim.AMBIENT_C, "", t)
	var m0 := wall.mass
	h.until(func(): return not stone.alive or a.health < 100.0, 40)
	check(not stone.alive and is_equal_approx(wall.mass, m0 + 10.0), "a small stone sticks and fuses into the wall")
	var blob := U.lava(h, 12.0, a.chest() + a.forward() * 6.0)
	blob.vel = -a.forward() * 12.0
	blob.attack_id = h.w.new_attack_id()
	blob.attack_owner = t.id
	blob.gravity_scale = 0.0
	h.until(func(): return not blob.alive or a.health < 100.0, 40)
	check(not blob.alive and a.health == 100.0, "lava is absorbed into the face")
	var water := h.launch_at(a, "water", 3.0, 14.0, Sim.AMBIENT_C, "", t)
	water.form = Sim.Form.BLOB
	h.until(func(): return bool(wall.props.get("set", false)), 40)
	check(bool(wall.props.get("set", false)) and h.has_event("steam"), "water: steam burst, the face sets to obsidian")
	near(Interactions.counter_power(Agent.of_body(h.w, wall)), wall.mass * 0.38, 1e-6, "harder (0.38 / kg: 38 for 100 kg)")
	check(U.energy_drift(h, e0) < 1e-5, "energy booked (%.6f)" % U.energy_drift(h, e0))
	h.release(a, "guard")


func test_slag_wave_pours_the_molten_face() -> void:
	var s := _duel(8.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(20)
	var wall := h.w.get_body(a.wall_body)
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step()
	h.release(a, "guard")
	h.until(func(): return h.events("transform").any(func(e): return e.get("why", "") == "slag_wave"), 30)
	var ev: Array = h.events("transform").filter(func(e): return e.get("why", "") == "slag_wave")
	check(not ev.is_empty(), "the face slumps forward as a wave")
	var wv := h.w.get_body(int(ev[0].body)) if not ev.is_empty() else null
	check(wv != null and is_equal_approx(wv.mass, 15.0) and wv.liquid > 0.9, "15 kg of lava (face 150 + 150 HU)")
	check(wall.heat_payload == 0.0, "the face is spent")
	h.until(func(): return t.health < 100.0, 160)
	check(t.health < 100.0, "it reaches the rival")


func test_melt_pit_melts_what_lands_and_burns_walkers() -> void:
	var s := _duel(4.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var e0 := U.e0(h)
	U.perform(h, a, "sink", 2)
	h.until(func(): return h.w.bodies.any(func(b): return b.alive and b.tag == &"melt_pit"), 30)
	var pit: MatBody = h.w.bodies.filter(func(b): return b.alive and b.tag == &"melt_pit")[0]
	check(pit.heat_payload > 200.0, "T2 pit: 120 + 120 HU of heat (%.0f)" % pit.heat_payload)
	var stone := U.shot(h, Sim.Mat.STONE, 10.0, pit.pos + Vector3(0, 0.4, 0), Vector3(0, -2, 0), t)
	h.step(30)
	check(stone.temp > 200.0, "a landing stone heats in the pit (%.0f °C)" % stone.temp)
	t.pos = pit.pos
	h.step(10)
	check(t.status.has("burning"), "a walker burns")
	check(U.energy_drift(h, e0) < 1e-5, "booked (%.6f)" % U.energy_drift(h, e0))


func test_magma_hold_pours_cools_and_holds_insulated() -> void:
	var s := _duel(8.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var blob := U.lava(h, 12.0, Vector3(0.5, 0.2, 2.0))
	var e0 := U.e0(h)
	h.press(a, "tech")
	h.until(func(): return h.w.held(a) == blob, 40)
	check(h.w.held(a) == blob, "Magma Hold seizes the lava (no burn)")
	var liq := blob.liquid
	h.step(60)
	check(blob.liquid >= liq - 1e-6, "held lava doesn't cool (insulated, upkeep)")
	h.release(a, "tech")
	h.step(2)
	check(blob.form == Sim.Form.WAVE and blob.attack_owner == a.id, "release: poured as a wave")
	check(not a.has("magma"), "the hold's insulation is only for its duration")
	check(U.energy_drift(h, e0) < 1e-5, "booked")
	var s2 := _duel(8.0)
	var a2: ActorState = s2[0]
	var blob2 := U.lava(h, 12.0, Vector3(0.5, 0.2, 2.0))
	var e2 := U.e0(h)
	h.press(a2, "tech")
	h.until(func(): return h.w.held(a2) == blob2, 40)
	h.it(a2).attack_pressed = true
	h.step()
	check(blob2.liquid <= 0.0 and blob2.temp <= Sim.AMBIENT_C + 1e-3, "T+A Cool & Set: solid rock in hand")
	h.release(a2, "tech")
	h.step(2)
	check(blob2.form != Sim.Form.WAVE and blob2.attack_id != 0, "thrown as a stone")
	check(U.energy_drift(h, e2) < 1e-5, "the heat went into the ground (removed)")


func test_reverse_tide_turns_the_rivals_wave_around() -> void:
	# Owner flagship counter: the rival pours lava; Magma Hold on the wave wins the grip contest.
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var wave := U.lava_wave(h, 20.0, Vector3(0, 0, 0.0), Vector3(0, 0, 1), t)
	h.press(a, "tech")
	h.until(func(): return h.has_event("reverse_tide") or a.health < 100.0, 40)
	check(h.has_event("reverse_tide"), "Reverse Tide: the contest is won")
	check(wave.attack_owner == a.id and wave.wave_dir.dot(t.pos - wave.pos) > 0.0, "the wave turns back toward the pourer")
	h.release(a, "tech")
	h.until(func(): return t.health < 100.0, 180)
	check(t.health < 100.0, "the rival's own wave hits them")
	check(a.health == 100.0, "not the caster")
	# A 45 kg wave is too much authority to turn.
	var s2 := _duel(9.0)
	var a2: ActorState = s2[0]
	var t2: ActorState = s2[1]
	var big := U.lava_wave(h, 45.0, Vector3(0, 0, -0.5), Vector3(0, 0, 1), t2)
	h.press(a2, "tech")
	h.until(func(): return h.has_event("control_fail") or h.has_event("reverse_tide"), 40)
	check(not h.has_event("reverse_tide") and big.attack_owner == t2.id, "a 45 kg wave wins the contest (evade or wall it)")
	h.release(a2, "tech")


func test_cinder_step_and_lava_wade() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var p0 := a.pos
	h.it(a).move = Vector3(1, 0, 0)
	h.press(a, "evade")
	h.step(25)
	h.it(a).move = Vector3.ZERO
	check(a.pos.distance_to(p0) > 3.0, "Cinder Step: a 3.5 m dash (%.1f)" % a.pos.distance_to(p0))
	h.step(10)
	h.press(a, "evade")
	h.it(a).evade_held = true
	h.step(30)
	var pool := U.lava(h, 12.0, a.pos + Vector3(0, 0.05, 0))
	pool.form = Sim.Form.ZONE
	pool.tag = &"lava_pool"
	pool.zone_radius = 1.5
	pool.radius = 1.5
	h.step(20)
	check(a.stance == "lava_wade" and Status.immune(a, "burn"), "Lava Wade: immune to lava burns")
	check(not a.status.has("burning"), "wading in the pool without burning")
	h.it(a).evade_held = false
	h.step(20)
	check(a.status.has("burning"), "out of the stance, the pool burns")


func test_magma_column_cells() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var cur := U.wall(h, Sim.Mat.STONE, "obsidian", 100.0, Vector3(0, 0, 2), a)
	cur.hardness = 0.28
	cur.heat_payload = 150.0
	h.w.ledger.generated += 150.0
	var stone := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(0, 1, 0), Vector3(0, 0, 17), t)
	check(U.pr(h, stone, cur).outcome == "earth_stick", "stone shot: CAP Magma Curtain (sticks, heats)")
	check(U.pr(h, stone, "melt_pit", 2, false, a).outcome == "earth_melt_in", "stone shot: Melt Pit T2")
	var heave := U.shot(h, Sim.Mat.STONE, 45.0, Vector3(1, 1, 0), Vector3(0, 0, 14), t)
	var ph := U.pr(h, heave, cur)
	check(ph.band == "partial" and ph.outcome == "weaken", "heavy stone: WKN Curtain (%.2f)" % ph.ratio)
	var hot := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(-1, 1, 0), Vector3(0, 0, 17), t, "", 1000.0)
	check(Interactions.allows(hot, &"grip_magma"), "hot rock: REC Magma Hold")
	var blob := U.lava(h, 20.0, Vector3(3, 1, 0))
	check(Interactions.allows(blob, &"grip_magma"), "magma blob: REC Magma Hold")
	blob.attack_id = h.w.new_attack_id()
	blob.attack_owner = t.id
	blob.vel = Vector3(0, 0, 12)
	check(U.pr(h, blob, cur).outcome == "earth_absorb_face", "magma blob: ABS Curtain")
	var wave := U.lava_wave(h, 20.0, Vector3(-3, 0, 0), Vector3(0, 0, 1), t)
	check(Interactions.allows(wave, &"grip_magma"), "lava wave: Reverse Tide (grip_magma)")
	var disc := U.shot(h, Sim.Mat.METAL, 2.0, Vector3(4, 1, 0), Vector3(0, 0, 24), t, "disc")
	check(U.pr(h, disc, cur).outcome == "earth_stick", "metal: sticks and heats in the face")
	var sand := U.shot(h, Sim.Mat.SAND, 5.0, Vector3(-4, 1, 0), Vector3(0, 0, 22), t, "slug")
	check(U.pr(h, sand, cur).outcome == "earth_glass_beads", "sand: glass beads")
	var surge := U.shot(h, Sim.Mat.SAND, 30.0, Vector3(5, 0, 0), Vector3(0, 0, 9), t)
	surge.form = Sim.Form.WAVE
	check(U.pr(h, surge, "melt_pit", 0, false, a).outcome == "earth_glaze", "sand surge: glass in the Melt Pit (stalls)")
	var water := U.shot(h, Sim.Mat.WATER, 6.0, Vector3(-5, 1, 0), Vector3(0, 0, 16), t)
	check(U.pr(h, water, cur).outcome == "earth_set", "water: steam, the face sets to obsidian")
	var ice := U.shot(h, Sim.Mat.WATER, 4.0, Vector3(6, 1, 0), Vector3(0, 0, 24), t, "", -5.0)
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	check(U.pr(h, ice, cur).outcome == "earth_face_heat", "ice: XFM steam (x3)")
	check(U.prv(h, "flame", {"H": 8.0}, cur).outcome == "earth_feed_face", "flame: ABS Curtain (keeps it molten)")
	check(U.prv(h, "blue_fire", {"H": 12.0}, cur).outcome == "earth_feed_face", "blue fire: ABS (heat into lava)")
	check(U.prv(h, "lightning", {"E": 24.0}, cur).outcome == "ground", "lightning: GND Curtain (stone-wall rule)")
	check(U.prv(h, "blast", {"P": 34.0}, cur).outcome == "weaken", "combustion T3: WKN Curtain (splashes)")
	check(U.prv(h, "gust", {"P": 28.0}, cur).outcome == "block", "gust: BLK Curtain")
	check(U.prv(h, "sound", {"P": 22.0}, cur).outcome == "block", "sound T2: BLK Curtain")
	check(U.prv(h, "sound", {"P": 40.0}, cur).outcome == "overwhelm", "strong sound shatters the brittle face")
	var vine := h.w.spawn_body(Sim.Mat.PLANT, Sim.Form.CHUNK, 6.0, Vector3(7, 1, 0), "test")
	vine.vel = Vector3(0, 0, 10)
	vine.attack_id = h.w.new_attack_id()
	vine.attack_owner = t.id
	check(U.pr(h, vine, cur).outcome == "transform", "vines: burn (ash)")
