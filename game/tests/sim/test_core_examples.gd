extends TestCase
## The owner's examples of docs/MOVESET.md §5.4 at rule / verb level, with test-local defs and rules
## (the element kits ship the real moves): lava vs gust T0-T3, lightning through stone, sink a stone,
## a wave carries a stone back, wind guard deflect / perfect return, melt a wall (slump), split it
## and spike it back.

var h: SimHarness


func _energy_drift(e0: float) -> float:
	return absf(h.w.system_energy() - h.w.ledger_balance() - e0)


func _lava_wave(mass: float) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, Vector3(0, 0, 0), "test")
	h.w.mass_ledger.ground_taken += mass
	h.w.ledger.generated += Thermal.heat(b, mass * (Sim.STONE_C * 980.0 + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = Vector3(0, 0, 1)
	b.wave_budget = 12.0
	b.vel = Vector3(0, 0, 7.5)
	b.attack_id = h.w.new_attack_id()
	return b


func test_lava_vs_gust_needs_a_t3_gale() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	# Air kit cell (test-local): strong wind sets lava (convective cooling, booked as ambient).
	Interactions.add_rule("molten", "gust", {"tiers": [2, 3], "outcome": "transform", "to": "rock", "partial": "weaken", "fail": "pass"})
	var e0 := h.w.system_energy() - h.w.ledger_balance()
	var powers := [7.0, 11.0, 18.0, 28.0]
	var outs := []
	var tps := []
	for tier in 4:
		var lava := _lava_wave(20.0)
		var tp0 := Interactions.threat_power(Agent.of_body(h.w, lava), {})
		var g := Agent.of_volume(h.w, null, null, &"gust", Vector3(0, 1, -3), Vector3(0, 0, 1), {"P": powers[tier]})
		g.tier = tier
		var r := Interactions.resolve(h.w, Agent.of_body(h.w, lava), g)
		outs.append(r.outcome)
		tps.append([tp0, Interactions.threat_power(Agent.of_body(h.w, lava), {})])
		if tier == 3:
			h.step()
			check(lava.form != Sim.Form.WAVE and lava.liquid <= 0.0, "T3: the front stalls and sets into rock")
		h.w.decay_body(lava, "test")
	near(float(tps[0][0]), 27.3, 0.01, "20 kg lava wave TP 27.3")
	check(outs[0] == "pass" and outs[1] == "pass", "Palm Gust / Cyclone (7, 11) can't stop lava: %s" % [outs])
	check(outs[2] == "weaken", "Gale T2 (18, ratio 0.66) crusts and slows: %s" % outs[2])
	near(float(tps[2][1]), 27.3 - 18.0, 0.05, "the partial removes CP_eff from TP")
	check(outs[3] == "transform", "Hurricane Palm T3 (28, ratio 1.03) sets it: %s" % outs[3])
	check(_energy_drift(e0) < 1e-6, "every removed HU is booked (ambient)")
	h.end_scope()


func _bolt_world() -> Array:
	h = SimHarness.new(1)
	var c := h.actor("C", Vector3(0, 0, 6), 0, {"lightning": true}, Sim.Element.FIRE)
	var t := h.actor("T", Vector3(0, 0, -3), 1, {}, Sim.Element.EARTH)
	var wall := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, 120.0, Vector3(0, 0, 0), "test")
	h.w.mass_ledger.ground_taken += 120.0
	wall.wall_half = Vector3(1.1, 0.75, 0.28)
	wall.wall_rise = 1.0
	wall.static_body = true
	wall.props["standing"] = 999.0
	h.step(5)
	h.log.clear()
	return [c, t, wall]


func test_lightning_grounds_in_stone_and_a_storm_bolt_blasts_through() -> void:
	var s := _bolt_world()
	var c: ActorState = s[0]
	var t: ActorState = s[1]
	var wall: MatBody = s[2]
	var def := {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
	var out := Conduction.discharge(h.w, c, t.chest(), def, h.w.new_attack_id(), true)
	h.step()
	check(out.blocked and wall.alive and t.health == 100.0, "Bolt E 24 vs Bulwark 30: grounded, the wall stands")
	check(h.events("interaction").any(func(e): return e.counter == "wall_stone" and e.outcome == "ground"), "interaction ground")
	var storm := {"range": 16.0, "damage": 36.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
	var out2 := Conduction.discharge(h.w, c, t.chest(), storm, h.w.new_attack_id(), true)
	h.step()
	check(not out2.blocked and not wall.alive and h.has_event("wall_crumble"), "Storm Bolt E 36 shatters the wall")
	near(float(out2.e), 21.0, 1e-6, "it continues with 36 - 0.5 x 30 = 21")
	near(100.0 - t.health, 21.0, 1e-6, "and hits the fighter behind it for 21")


func test_ice_insulates_bolts_up_to_1_5_cp() -> void:
	var s := _bolt_world()
	var c: ActorState = s[0]
	var t: ActorState = s[1]
	var wall: MatBody = s[2]
	h.w.decay_body(wall, "test")
	var ice := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.WALL, 50.0, Vector3(0, 0, 0), "test", -5.0)
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	ice.tag = &"ice"
	ice.wall_half = Vector3(1.1, 0.75, 0.28)
	ice.wall_rise = 1.0
	ice.static_body = true
	ice.props["standing"] = 999.0
	var out := Conduction.discharge(h.w, c, t.chest(), {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}, h.w.new_attack_id(), true)
	check(out.blocked and ice.alive, "ice wall 50 kg (CP 22 x 1.5 = 33) stops E 24")
	var out2 := Conduction.discharge(h.w, c, t.chest(), {"range": 14.0, "damage": 36.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}, h.w.new_attack_id(), true)
	check(not out2.blocked and not ice.alive, "E 36 > 33 shatters it")


func test_sink_a_stone_into_the_ground() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.register("t_swallow", {"verb": "zone", "counter": {"cls": "swallow", "power": [22, 30, 40, 55]}})
	Interactions.add_rule("solid_light", "swallow", {"outcome": "sink", "partial": "weaken"})
	var e := h.actor("E", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	var stone := h.launch_at(e, "stone", 20.0, 17.0)
	var m0 := h.w.stone_mass()
	var t := Agent.of_body(h.w, stone, e)
	var pr := h.predict(t, "t_swallow", 0, false, e)
	check(pr.outcome == "sink" and is_equal_approx(float(pr.ratio), 22.0 / 17.0), "Swallow CP 22 >= 17: sink")
	var r := Interactions.resolve(h.w, t, Agent.of_move(h.w, e, "t_swallow", 0, false))
	check(r.stopped and not stone.alive, "the stone is gone into the ground")
	near(h.w.mass_ledger.ground_returned, 20.0, 1e-9, "booked as ground_returned")
	near(h.w.stone_mass(), m0, 1e-9, "stone mass conserved")
	h.end_scope()


func test_a_wave_carries_the_stone_back() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.register("t_tide", {"element": 1, "verb": "ground_line", "startup": 0.1, "active": 0.05, "recovery": 0.3, "cost": 8.0,
		"source": "waterskin", "mat": "water", "mass": 6.0, "tag": "water_wave", "speed": 9.0, "budget": 10.0, "width": 2.0,
		"damage": 8.0, "balance": 30.0, "power": 18.0, "channel": "K"})
	Moves.bind(1, 1, "ground", "t_tide")
	Interactions.add_rule("stone", "wave_water", {"outcome": "capture", "partial": "slow", "release_speed": 9.0})
	var wr := h.actor("W", Vector3(0, 0, 4), 0, {}, Sim.Element.WATER)
	var th := h.actor("T", Vector3(0, 0, -10), 1, {}, Sim.Element.EARTH)
	th.is_dummy = true
	h.sub(wr, 1)
	h.step(20)
	var w0 := h.w.water_mass()
	var stone := h.launch_at(wr, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", th)
	h.flick(wr, "attack", Sim.Gesture.DOWN)
	h.step()
	h.release(wr, "attack")
	h.until(func(): return h.has_event("capture"), 40)
	check(h.has_event("capture", "body", stone.id), "the wave captured the stone")
	check(wr.health == 100.0, "it never reached W")
	h.until(func(): return h.has_event("release_captured"), 120)
	check(stone.attack_owner == wr.id and stone.attack_id != 0, "released as W's attack")
	check(stone.vel.dot(th.pos - stone.pos) > 0.0, "flying back toward the thrower")
	near(h.w.water_mass(), w0, 1e-6, "water mass conserved (wave -> puddle)")
	h.end_scope()


func test_wind_guard_deflects_and_a_perfect_one_returns_the_stone() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.AIR)
	var t := h.actor("T", Vector3(0, 0, -8), 1, {}, Sim.Element.EARTH)
	t.is_dummy = true
	h.step(20)
	h.press(a, "guard")
	h.step(30)
	var s1 := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", t)
	h.until(func(): return h.has_event("deflect") or h.has_event("hit") or h.has_event("block"), 40)
	check(h.has_event("deflect", "actor", a.id) and a.health == 100.0, "Wind Guard 12 x 1.5 = 18 >= 17: deflected")
	check(s1.attack_id == 0, "the stone is spent")
	h.release(a, "guard")
	h.step(40)
	h.log.clear()
	h.press(a, "guard")
	h.step(2)
	var s2 := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", t, 2.5)
	h.until(func(): return h.has_event("perfect_deflect") or h.has_event("hit"), 30)
	check(h.events("perfect_deflect").any(func(e): return e.get("verb", "") == "reflect"), "perfect (x1.5 = 27): back to the sender")
	check(s2.attack_owner == a.id and s2.vel.dot(t.pos - s2.pos) > 0.0, "now A's stone, flying at T")


func test_melt_the_wall_and_it_slumps() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.register("t_smelter", {"element": 2, "verb": "ranged_heat", "startup": 0.12, "active": 0.0, "recovery": 0.2, "cost": 6.0,
		"rate": 450.0, "range": 7.0, "slump_fraction": 0.25, "slump_at": 0.5})
	Moves.bind(2, 1, "tech", "t_smelter")
	var b := h.actor("B", Vector3(0, 0, -4), 0, {}, Sim.Element.EARTH)
	var f := h.actor("F", Vector3(0, 0, 2.5), 1, {}, Sim.Element.FIRE)
	h.sub(f, 1)
	h.step(20)
	h.press(b, "guard")
	h.step(10)
	var wall := h.w.get_body(b.wall_body)
	check(wall != null and wall.alive, "setup: Bulwark up")
	var m0 := h.w.stone_mass()
	var e0 := h.w.system_energy() - h.w.ledger_balance()
	h.press(f, "tech")
	var n := h.until(func(): return h.has_event("slump"), 150)
	check(n > 0, "the wall face slumps")
	note("slump after %.2f s" % (float(n) * Sim.DT))
	check(n > 0 and float(n) * Sim.DT < 1.6, "within about 1 s of Smelter heat")
	var ev := h.last_event("slump")
	var face := h.w.get_body(int(ev.get("body", -1)))
	check(face != null and face.liquid >= 0.5 and face.form == Sim.Form.BLOB, "a molten body")
	check(face != null and is_equal_approx(face.mass, 30.0), "25 % of the 120 kg wall")
	check(face != null and face.pos.z > -2.75, "on the heated side")
	check(not wall.alive and h.has_event("wall_crumble"), "the rest crumbled")
	h.release(f, "tech")
	h.release(b, "guard")
	h.step(5)
	near(h.w.stone_mass(), m0, 1e-6, "stone mass conserved")
	check(_energy_drift(e0) < 1e-6, "energy ledger balanced (%.6f)" % _energy_drift(e0))
	h.end_scope()


func test_split_the_stone_and_spike_it_back() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.register("t_seize", {"element": 0, "verb": "grip", "startup": 0.1, "active": 0.06, "recovery": 0.28, "cost": 6.0,
		"ccls": "grip_stone", "reach": 7.5, "shape": "split", "pieces": 3, "spread": 30.0, "speed": 20.0, "shape_cost": 3.0})
	Moves.bind(0, 1, "tech", "t_seize")
	var e := h.actor("E", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	var t := h.actor("T", Vector3(0, 0, -8), 1, {}, Sim.Element.FIRE)
	t.is_dummy = true
	h.sub(e, 1)
	h.step(20)
	var stone := h.launch_at(e, "stone", 20.0, 9.0, Sim.AMBIENT_C, "", t, 5.0)
	h.press(e, "tech")
	h.until(func(): return h.w.held(e) != null, 30)
	check(h.w.held(e) == stone, "seized the incoming stone (REC)")
	h.step(5)
	h.it(e).attack_pressed = true
	h.step()
	check(h.has_event("shape", "shape", "split"), "T+A: split")
	h.release(e, "tech")
	h.step(2)
	var spikes := h.events("launch").filter(func(x): return x.actor == e.id)
	check(spikes.size() == 3, "three spikes (%d)" % spikes.size())
	for x in spikes:
		var sb := h.w.get_body(int(x.body))
		check(sb != null and is_equal_approx(sb.mass, 20.0 / 3.0) and sb.vel.dot(t.pos - sb.pos) > 0.0, "a third of the stone, flying back")
	h.end_scope()
