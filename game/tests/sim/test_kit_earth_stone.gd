extends TestCase
## Earth / Stone (sub 0): the legacy ladder past the heave, Split, Swallow, Rising Fangs, Ram Wall,
## Bulwark thickening, Stone Skin / Burrow Step and the Stone column of MOVESET §8.1.

const U := preload("res://tests/sim/test_kit_earth_util.gd")

var h: SimHarness


func _duel(dist: float = 7.0) -> Array:
	h = SimHarness.new(3)
	return U.duel(h, 0, Sim.Element.FIRE, dist)


func test_stone_shot_ladder_boulder_and_crag_breaker() -> void:
	var masses := []
	for hold in [40, 70, 118]:
		var s := _duel()
		var a: ActorState = s[0]
		var t: ActorState = s[1]
		var m0 := h.w.stone_mass()
		h.press(a, "attack")
		h.step(hold)
		var b := h.w.held(a)
		masses.append(b.mass if b != null else -1.0)
		h.release(a, "attack")
		h.until(func(): return h.has_event("launch"), 40)
		var ev := h.last_event("launch")
		var sb := h.w.get_body(int(ev.get("body", -1)))
		if hold == 118:
			check(sb != null and sb.tag == &"crag", "T3 is a Crag Breaker")
			var hit := h.until(func(): return h.has_event("shatter"), 120)
			check(hit > 0, "the crag bursts on impact")
			var parts := h.events("split").filter(func(e): return e.parent == sb.id)
			check(parts.size() == 2, "into 3 rubble (2 splits) (%d)" % parts.size())
			check(t.health < 100.0, "it hit the target (%.1f)" % t.health)
		h.step(30)
		near(h.w.stone_mass(), m0, 1e-6, "stone mass booked from the ground (hold %d)" % hold)
	near(float(masses[0]), 45.0, 1e-6, "T1 heave keeps the legacy 45 kg")
	near(float(masses[1]), 65.0, 1e-6, "T2 Boulder 65 kg")
	near(float(masses[2]), 80.0, 1e-6, "T3 Crag Breaker 80 kg")


func test_heave_t1_has_no_drain_and_t2_drains() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	h.press(a, "attack")
	h.step(40)
	var f1 := a.focus
	h.step(20)
	check(a.focus >= f1 - 1e-6, "no Focus drain while holding the T1 heave (%.2f -> %.2f)" % [f1, a.focus])
	h.step(20)
	var f2 := a.focus
	h.step(20)
	check(a.focus < f2 - 1.0, "T2 Boulder drains 8 Focus/s (%.2f -> %.2f)" % [f2, a.focus])
	h.release(a, "attack")


func test_split_it_and_spike_it_back() -> void:
	# Owner example: "someone throws a stone at me: I can split it and spike it back".
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var stone := h.launch_at(a, "stone", 20.0, 9.0, Sim.AMBIENT_C, "", t, 5.0)
	h.press(a, "tech")
	h.until(func(): return h.w.held(a) != null, 40)
	check(h.w.held(a) == stone, "Seize caught the incoming stone (REC)")
	h.step(4)
	h.it(a).attack_pressed = true
	h.step()
	check(h.has_event("shape", "shape", "split"), "T+A: split")
	h.release(a, "tech")
	h.step(3)
	var spikes := h.events("launch").filter(func(e): return e.actor == a.id and e.get("kind", "") == "split")
	check(spikes.size() == 3, "three spikes (%d)" % spikes.size())
	for x in spikes:
		var sb := h.w.get_body(int(x.body))
		check(sb != null and is_equal_approx(sb.mass, 20.0 / 3.0) and sb.tag == &"spear", "a third of the stone, a spear")
		check(sb != null and sb.vel.dot(t.pos - sb.pos) > 0.0 and sb.attack_owner == a.id, "flying back at the thrower")
	var n := h.until(func(): return t.health < 100.0, 90)
	check(n > 0, "the spikes hit the thrower (%.1f)" % t.health)


func test_swallow_sinks_an_incoming_stone() -> void:
	# Owner example: "... or put it down into the ground".
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var m0 := h.w.stone_mass()
	var r0 := float(h.w.mass_ledger.ground_returned)
	h.press(a, "guard")
	h.step(12)
	var stone := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", t, 6.5)
	h.flick(a, "guard", Sim.Gesture.DOWN)
	h.step()
	check(a.action != null and a.action.id == "swallow", "guard flick down: Swallow")
	h.until(func(): return not stone.alive, 40)
	check(not stone.alive and h.has_event("sink", "body", stone.id), "CP 22 >= TP 17: the stone sinks")
	check(h.events("interaction").any(func(e): return e.counter == "swallow" and e.outcome == "sink"), "interaction swallow -> sink")
	check(a.health == 100.0, "it never reached the fighter")
	check(h.w.mass_ledger.ground_returned >= r0 + 20.0 - 1e-6, "booked as ground_returned")
	h.release(a, "guard")
	h.step(40)
	near(h.w.stone_mass(), m0, 1e-6, "stone mass conserved")


func test_swallow_strength_follows_the_guard_hold() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var heave := U.shot(h, Sim.Mat.STONE, 45.0, Vector3(0, 1, 0), Vector3(0, 0, 14))
	var p0 := U.pr(h, heave, "swallow", 0, false, a)
	var p2 := U.pr(h, heave, "swallow", 2, false, a)
	check(p0.band == "partial" and p0.outcome == "weaken", "T0 22 vs heave 31.5: partial weaken (%s %.2f)" % [p0.band, p0.ratio])
	check(p2.outcome == "sink", "T2 40 vs 31.5: sink")
	# Live: a guard held 1.0 s then the flick swallows at T2.
	h.press(a, "guard")
	h.step(62)
	h.flick(a, "guard", Sim.Gesture.DOWN)
	h.step()
	check(a.action != null and a.action.id == "swallow" and a.action.tier() == 2, "guard held 1.0 s: Swallow T2 (%d)" % (a.action.tier() if a.action else -1))
	h.release(a, "guard")


func test_swallow_drains_a_lava_wave_from_t1() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var w0 := U.lava_wave(h, 20.0, Vector3(0, 0, 1), Vector3(0, 0, 1), t)
	var r0 := U.pr(h, w0, "swallow", 0, false, a)
	var r1 := U.pr(h, w0, "swallow", 1, false, a)
	near(float(r0.tp), 27.3, 0.05, "20 kg lava wave TP 27.3")
	check(r0.outcome == "weaken" and r0.band == "partial", "T0 (22): the trench only weakens it")
	check(r1.outcome == "sink", "T1 (30): it drains into the trench")
	var e0 := U.e0(h)
	var res := Interactions.resolve(h.w, Agent.of_body(h.w, w0, a), Agent.of_move(h.w, a, "swallow", 1, false))
	check(res.stopped and not w0.alive, "drained")
	check(U.energy_drift(h, e0) < 1e-6, "its heat is booked (removed)")


func test_rising_fangs_stop_a_lava_wave_and_stand_as_spikes() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var m0 := h.w.stone_mass()
	var e0 := U.e0(h)
	var wave := U.lava_wave(h, 20.0, Vector3(0, 0, -1.0), Vector3(0, 0, 1), t)
	h.flick(a, "attack", Sim.Gesture.DOWN)
	h.step()
	h.release(a, "attack")
	var n := h.until(func(): return h.has_event("wave_blocked", "body", wave.id) or a.health < 100.0, 120)
	check(n > 0 and h.has_event("wave_blocked", "body", wave.id), "the spike line stops the lava wave")
	check(a.health == 100.0, "the lava never reaches the caster")
	var spikes := h.w.bodies.filter(func(b): return b.alive and b.form == Sim.Form.WALL and b.tag == &"spikes")
	check(spikes.size() == 1, "it erupts into standing spikes where they met")
	h.step(140)
	check(h.w.bodies.all(func(b): return not (b.alive and b.tag == &"spikes")), "the spikes sink after 1.5 s")
	near(h.w.stone_mass(), m0, 1e-6, "stone mass conserved")
	check(U.energy_drift(h, e0) < 1e-6, "energy ledger balanced (%.6f)" % U.energy_drift(h, e0))


func test_rising_fangs_launch_the_target() -> void:
	var s := _duel(7.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.flick(a, "attack", Sim.Gesture.DOWN)
	h.step()
	h.release(a, "attack")
	var vy := 0.0
	var n := h.until(func():
		vy = maxf(vy, t.vel.y)
		return h.events("hit").any(func(e): return e.actor == t.id), 120)
	check(n > 0, "the spike line hits")
	h.step()
	vy = maxf(vy, t.vel.y)
	check(vy > 3.0, "and launches (vy %.1f)" % vy)
	h.until(func(): return h.w.bodies.any(func(b): return b.alive and b.tag == &"spikes"), 60)
	check(h.w.bodies.any(func(b): return b.alive and b.tag == &"spikes"), "then stands as spikes")


func test_earthrise_rings_the_caster_with_spikes() -> void:
	var s := _duel(3.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	U.perform(h, a, "ground", 3)
	h.step(30)
	var ring := h.w.bodies.filter(func(b): return b.alive and b.form == Sim.Form.WALL and b.tag == &"spikes")
	check(ring.size() == 4, "4 spike walls around the caster (%d)" % ring.size())
	for b in ring:
		near(KitEarth.flat_dist(b.pos, a.pos), 3.5, 0.3, "at r 3.5")
	check(t.health < 100.0, "the eruption hits a fighter in the ring")


func test_ram_wall_shoves_a_lava_wave_back_at_the_pourer() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(12)
	var wall := h.w.get_body(a.wall_body)
	check(wall != null, "setup: Bulwark")
	var wave := U.lava_wave(h, 20.0, Vector3(0, 0, -1.5), Vector3(0, 0, 1), t)
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step()
	check(a.action != null and a.action.id == "ram_wall", "guard flick up: Ram Wall")
	h.release(a, "guard")
	h.until(func(): return wave.attack_owner == a.id, 60)
	check(wave.attack_owner == a.id and wave.wave_dir.z < 0.0, "K 54 > TP 27: the wave is shoved back, now A's")
	check(h.events("interaction").any(func(e): return e.counter == "ram" and e.outcome == "earth_ram_push"), "interaction ram -> push")
	h.until(func(): return not wall.alive, 90)
	check(not wall.alive and h.has_event("wall_crumble"), "the ram ends in rubble")


func test_ram_wall_contest_against_enemy_walls() -> void:
	var results := {}
	for enemy in [["sand", Sim.Mat.SAND, 40.0], ["bulwark", Sim.Mat.STONE, 120.0], ["thick", Sim.Mat.STONE, 240.0]]:
		var s := _duel(9.0)
		var a: ActorState = s[0]
		var t: ActorState = s[1]
		var ew := U.wall(h, enemy[1], "sand" if enemy[1] == Sim.Mat.SAND else "", enemy[2], Vector3(0, 0, -0.5), t)
		h.press(a, "guard")
		h.step(12)
		var mine := h.w.get_body(a.wall_body)
		h.flick(a, "guard", Sim.Gesture.UP)
		h.step()
		h.release(a, "guard")
		h.until(func(): return not mine.alive, 90)
		results[enemy[0]] = [ew.alive, mine.alive]
		var ix := h.events("interaction").filter(func(e): return e.rule == "ram_contest")
		check(not ix.is_empty(), "%s: the ram contest resolved" % enemy[0])
	check(not results.sand[0], "K 54 vs a 40 kg dune (CP 10): the dune breaks")
	check(not results.bulwark[0], "K 54 vs Bulwark CP 30 (ratio 0.56): both walls break")
	check(results.thick[0], "K 54 vs a 240 kg wall (CP 60): it holds, the ram stops")


func test_bulwark_thickens_with_the_guard_hold_and_grounds_a_storm_bolt() -> void:
	var out := []
	for hold in [10, 66]:
		var s := _duel(8.0)
		var a: ActorState = s[0]
		var t: ActorState = s[1]
		t.is_dummy = false
		h.press(a, "guard")
		h.step(hold)
		var wall := h.w.get_body(a.wall_body)
		var storm := {"range": 16.0, "damage": 36.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
		t.lock_target = a.id
		var r := Conduction.discharge(h.w, t, a.chest(), storm, h.w.new_attack_id(), true)
		out.append([r.blocked, wall.mass if wall.alive else -1.0, wall.alive])
		h.release(a, "guard")
	check(not out[0][0] and not out[0][2], "a fresh Bulwark (CP 30) is blasted by a Storm Bolt (E 36)")
	check(out[1][0] and out[1][2] and is_equal_approx(float(out[1][1]), 160.0), "held 1.0 s: 160 kg (CP 40) grounds it (%s)" % [out[1]])


func test_stone_skin_anchors_and_burrow_step_passes_under_a_wave() -> void:
	var s := _duel(6.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "evade")
	h.it(a).evade_held = true
	h.step(20)
	check(a.stance == "stone_skin" and a.anchored, "evade held in place: Stone Skin, anchored")
	var res := h.w.hit_actor(a, {"attacker": t.id, "attack_id": h.w.new_attack_id(), "damage": 10.0, "balance": 10.0,
		"knock": Vector3(0, 6, 9), "kind": "air", "from": t.chest(), "power": 25.0})
	check(res == "hit" and a.vel.length() < 1.0, "a gust's knock does not move it (vel %.2f)" % a.vel.length())
	h.it(a).evade_held = false
	h.step(20)
	check(a.stance == "", "released: stance off")
	# Burrow Step: evade held with a direction.
	var p0 := a.pos
	var wave := U.lava_wave(h, 20.0, a.pos + Vector3(0, 0, -2.0), Vector3(0, 0, 1), t)
	h.it(a).move = Vector3(1, 0, 0)
	h.press(a, "evade")
	h.it(a).evade_held = true
	h.step(14)
	check(a.action != null and a.action.id == "stone_skin" and a.action.data.get("mode", "") == "burrow", "evade held with a direction: Burrow Step")
	h.it(a).evade_held = false
	h.step(20)
	h.it(a).move = Vector3.ZERO
	check(a.pos.distance_to(p0) > 3.5, "resurfaced away (%.1f m)" % a.pos.distance_to(p0))
	check(h.events("hit").filter(func(e): return e.actor == a.id).size() == 1, "the wave passed over the burrow (only the gust hit)")
	check(h.events("fx").any(func(e): return e.actor == a.id and e.fx == "erupt"), "erupt cue at the exit")


func test_stone_column_cells() -> void:
	# MOVESET §8.1, Stone column, against each row's reference threat.
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var bul := U.wall(h, Sim.Mat.STONE, "", 120.0, Vector3(0, 0, 2), a)
	var stone := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(0, 1, 0), Vector3(0, 0, 17), t)
	check(U.pr(h, stone, "swallow", 0, false, a).outcome == "sink", "stone shot: SNK Swallow (22 >= 17)")
	check(U.pr(h, stone, bul).outcome == "block", "stone shot: BLK Bulwark")
	var boulder := U.shot(h, Sim.Mat.STONE, 200.0, Vector3(2, 1, 0), Vector3(0, 0, 11), t)
	var pb := U.pr(h, boulder, "swallow", 3, false, a)
	check(pb.band == "partial" and pb.outcome == "weaken", "boulder 110: Swallow T3 (55) only weakens (%.2f)" % pb.ratio)
	var hot := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(-2, 1, 0), Vector3(0, 0, 17), t, "", 1000.0)
	check(U.pr(h, hot, "swallow", 1, false, a).outcome == "sink", "hot rock: SNK Swallow T1")
	var blob := U.lava(h, 20.0, Vector3(3, 1, 0))
	blob.vel = Vector3(0, 0, 12)
	blob.attack_id = h.w.new_attack_id()
	blob.attack_owner = t.id
	var pm1 := U.pr(h, blob, "swallow", 1, false, a)
	var pm2 := U.pr(h, blob, "swallow", 2, false, a)
	check(pm1.band == "partial" and pm2.outcome == "sink", "magma blob (TP %.1f): Swallow T1 partial, T2 sinks" % pm1.tp)
	var lance := U.shot(h, Sim.Mat.METAL, 6.0, Vector3(0, 1.2, 1.0), Vector3(0, 0, 30), t, "lance")
	check(U.pr(h, lance, bul).outcome == "earth_embed", "metal lance: embeds in the Bulwark (a rod)")
	var res := Interactions.resolve(h.w, Agent.of_body(h.w, lance, a), Agent.of_body(h.w, bul), {"site": "wall"})
	check(res.stopped and lance.tag == &"rod" and lance.static_body, "the lance stays as a conductive rod")
	check(Materials.conducts(lance), "and conducts")
	var ice := U.shot(h, Sim.Mat.WATER, 4.0, Vector3(0, 1, -1), Vector3(0, 0, 24), t, "", -5.0)
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	check(U.pr(h, ice, "swallow", 0, false, a).outcome == "sink", "ice lance: SNK Swallow")
	check(U.prv(h, "flame", {"H": 8.0}, bul).outcome == "block", "flame: BLK Bulwark (wall heats)")
	check(U.prv(h, "sound", {"P": 22.0}, bul).outcome == "reflect", "sound: RFL Bulwark")
	check(U.prv(h, "gust", {"P": 28.0}, bul).outcome == "block", "gust: BLK Bulwark")
	check(U.prv(h, "lightning", {"E": 24.0}, bul).outcome == "ground", "bolt E 24: GND Bulwark (legacy)")
	check(U.prv(h, "lightning", {"E": 36.0}, bul).outcome == "shatter", "Storm Bolt E 36: the Bulwark shatters")
	var sw := U.lava_wave(h, 20.0, Vector3(4, 0, 0), Vector3(0, 0, 1), t)
	var spikes := U.wall(h, Sim.Mat.STONE, "spikes", 24.0, Vector3(4, 0, 2), a)
	spikes.hardness = EarthStone.SPIKE_CP / 24.0
	check(U.pr(h, sw, spikes).outcome == "earth_spike_stop", "lava wave: Rising Fangs spikes stop it")
	var ww := U.shot(h, Sim.Mat.WATER, 18.0, Vector3(-4, 0, 0), Vector3(0, 0, 9), t)
	ww.form = Sim.Form.WAVE
	check(U.pr(h, ww, "swallow", 0, false, a).outcome == "weaken", "water wave: WKN Swallow (diverts)")
	# Tornado / suction vs Stone Skin: the core anchor cell at the stance's CP 40.
	a.status["anchored"] = {"t": -1.0, "mag": 40.0, "src": a.id}
	var tor := Agent.of_volume(h.w, t, null, &"tornado", t.chest(), Vector3(0, 0, 1), {"P": 35.0})
	check(Interactions.predict(h.w, tor, Agent.of_stance(h.w, a)).outcome == "block", "tornado 35: Stone Skin anchor (40) holds")
