extends TestCase
## Moveset engine: the counter rule (docs/MOVESET.md §5; COMBAT_SPEC "Engine" §E4) with synthetic
## agents and test-local rules.

var h: SimHarness


func _counter(ccls: String, power: float, tier: int = 0, perfect: bool = false) -> Agent:
	var c := Agent.new()
	c.kind = "move"
	c.ccls = StringName(ccls)
	c.cls = c.ccls
	c.power = power
	c.tier = tier
	c.perfect = perfect
	c.dir = Vector3(1, 0, 0)
	return c


func _stone(mass: float, speed: float, temp: float = Sim.AMBIENT_C) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, mass, Vector3(0, 1.5, 0), "test", temp)
	h.w.mass_ledger.ground_taken += mass
	b.vel = Vector3(0, 0, speed)
	b.gravity_scale = 0.0
	b.attack_id = h.w.new_attack_id()
	return b


func test_threat_power_channels() -> void:
	h = SimHarness.new(1)
	var t := Agent.of_body(h.w, _stone(20.0, 17.0))
	near(float(t.ch.K), 17.0, 1e-9, "K = m v / 20 (stone shot 17)")
	near(Interactions.threat_power(t, {}), 17.0, 1e-9, "TP")
	var lava := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, 20.0, Vector3(3, 0, 0), "test")
	h.w.mass_ledger.ground_taken += 20.0
	Thermal.heat(lava, 396.0)
	lava.vel = Vector3(7.5, 0, 0)
	var la := Agent.of_body(h.w, lava)
	check(la.cls == &"lava_wave", "class lava_wave (%s)" % la.cls)
	near(Interactions.threat_power(la, {}), 27.3, 0.01, "20 kg lava wave = K 7.5 + H 19.8")
	near(Interactions.threat_power(la, {"w": {"H": 2.0}}), 7.5 + 39.6, 0.01, "rule weights")
	var ice := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.SHARD, 4.0, Vector3(0, 1, 3), "test", -5.0)
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	var ia := Agent.of_body(h.w, ice)
	near(float(ia.ch.C), 18.2 / 20.0, 0.01, "4 kg ice at -5 C: C 0.9")
	near(Interactions.threat_power(ia, {}, &"heat"), 0.91, 0.01, "cold counts vs heat counters")
	near(Interactions.threat_power(ia, {}, &"pressure"), 0.0, 0.01, "cold does not count vs others")


func test_bands_full_partial_fail_and_perfect() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Interactions.add_rule("stone", "t_counter", {"outcome": "deflect", "partial": "weaken", "fail": "overwhelm", "perfect": "reflect"})
	var t := Agent.of_body(h.w, _stone(20.0, 17.0))
	var r := Interactions.predict(h.w, t, _counter("t_counter", 18.0))
	check(r.band == "full" and r.outcome == "deflect", "18 vs 17: full (%s %s)" % [r.band, r.outcome])
	r = Interactions.predict(h.w, t, _counter("t_counter", 12.0))
	check(r.band == "partial" and r.outcome == "weaken", "12 vs 17: partial")
	r = Interactions.predict(h.w, t, _counter("t_counter", 8.0))
	check(r.band == "fail" and r.outcome == "overwhelm", "8 vs 17: fail")
	r = Interactions.predict(h.w, t, _counter("t_counter", 12.0, 0, true))
	near(float(r.cp_eff), 18.0, 1e-9, "perfect x1.5")
	check(r.outcome == "reflect", "perfect outcome at full")
	Interactions.add_rule("stone", "t_eff", {"eff": 2.0, "outcome": "deflect", "partial": "bend"})
	r = Interactions.predict(h.w, t, _counter("t_eff", 7.0))
	check(r.outcome == "bend" and is_equal_approx(float(r.ratio), 14.0 / 17.0), "Palm Gust 7 x 2 = 14 vs 17: bends (ratio %.3f)" % r.ratio)
	h.end_scope()


func test_custom_bands_wind_vs_fire_and_lightning_vs_barrier() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Interactions.add_rule("flame", "t_wind", {"bands": [[0.0, "amplify"], [1.0, "deflect"], [2.0, "extinguish"]]})
	var f := _counter("flame", 0.0)
	f.kind = "volume"
	f.cls = &"flame"
	f.ch.H = 12.0
	var outs := []
	for cp in [7.0, 18.0, 28.0]:
		outs.append(Interactions.predict(h.w, f, _counter("t_wind", cp)).outcome)
	check(outs == ["amplify", "deflect", "extinguish"], "wind vs fire 12: %s" % [outs])
	# Lightning vs a stone wall (core cell): ground at >= 1, shatter below.
	var bolt := Agent.of_volume(h.w, null, null, &"lightning", Vector3.ZERO, Vector3.FORWARD, {"E": 24.0})
	var wall := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, 120.0, Vector3(0, 0, -3), "test")
	h.w.mass_ledger.ground_taken += 120.0
	var wa := Agent.of_body(h.w, wall)
	check(wa.ccls == &"wall_stone", "Bulwark class")
	near(Interactions.counter_power(wa), 30.0, 1e-9, "CP = 120 kg x 0.25")
	check(Interactions.predict(h.w, bolt, wa).outcome == "ground", "bolt 24 grounds")
	bolt.ch.E = 36.0
	var pr := Interactions.predict(h.w, bolt, wa)
	check(pr.outcome == "shatter" and pr.band == "partial", "storm bolt 36 shatters (%s)" % pr.outcome)
	h.end_scope()


func test_weaken_is_subtractive_and_counters_stack() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Interactions.add_rule("boulder", "t_swallow", {"outcome": "sink", "partial": "weaken", "fail": "weaken", "partial_at": 0.0})
	Interactions.add_rule("boulder", "t_ram", {"outcome": "block", "partial": "weaken"})
	var b := _stone(200.0, 11.0)
	var t := Agent.of_body(h.w, b)
	near(Interactions.threat_power(t, {}), 110.0, 1e-9, "boulder 110")
	var r := Interactions.resolve(h.w, t, _counter("t_swallow", 55.0))
	check(r.outcome == "weaken", "Swallow T3 (55) only weakens a boulder")
	near(b.vel.length(), 5.5, 1e-6, "subtractive: speed x (110 - 55) / 110")
	var t2 := Agent.of_body(h.w, b)
	near(Interactions.threat_power(t2, {}), 55.0, 1e-6, "55 left")
	var r2 := Interactions.resolve(h.w, t2, _counter("t_ram", 56.0))
	check(r2.outcome == "block" and r2.stopped, "then the Ram Wall stops it (stacking)")
	var ixs := h.w.events.filter(func(e): return e.type == "interaction")
	check(ixs.size() == 2, "interaction events (%d)" % ixs.size())
	var ev: Dictionary = ixs[-1] if not ixs.is_empty() else {}
	for k in ["threat", "counter", "outcome", "band", "ratio", "tp", "cp", "perfect", "pos", "dir", "threat_actor", "counter_actor", "threat_body", "counter_body", "to"]:
		check(ev.has(k), "interaction field %s" % k)
	h.end_scope()


func test_overwhelm_breaks_the_counter_and_passes_the_rest() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	var wall := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.WALL, 40.0, Vector3(0, 0, 3), "test")
	h.w.mass_ledger.ground_taken += 40.0
	wall.wall_rise = 1.0
	var b := _stone(45.0, 14.0)
	var r := Interactions.resolve(h.w, Agent.of_body(h.w, b), Agent.of_body(h.w, wall))
	check(r.band == "fail" and r.outcome == "overwhelm", "40 kg sand wall (CP 10) vs heave 31.5: fail")
	check(not wall.alive and r.counter_broken, "the wall crumbled")
	near(float(r.pass_scale), (31.5 - 0.5 * 10.0) / 31.5, 1e-6, "TP - 0.5 CP continues")
	near(b.vel.length(), 14.0 * (31.5 - 5.0) / 31.5, 1e-6, "the stone keeps going, slower")
	h.end_scope()


func test_rule_lookup_order_tiers_and_legacy_protection() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Interactions.add_rule("solid_light", "t_c", {"outcome": "bend", "id": "fam"})
	Interactions.add_rule("*", "t_c", {"outcome": "pass", "id": "wild"})
	check(Interactions.rule(&"stone", &"t_c").id == "fam", "family beats wildcard")
	Interactions.add_rule("stone", "t_c", {"outcome": "sink", "id": "exact", "tiers": [2, 3]})
	check(Interactions.rule(&"stone", &"t_c", 3).id == "exact", "exact at its tiers")
	check(Interactions.rule(&"stone", &"t_c", 0).id == "fam", "outside its tiers it falls through")
	check(Interactions.rule(&"water", &"t_c").id == "wild", "other families: wildcard")
	check(Interactions.rule(&"zzz", &"zzz").id == "default", "default rule")
	Interactions.add_rule("stone", "t_c", {"outcome": "block", "id": "exact2", "tiers": [2, 3]})
	check(Interactions.rule(&"stone", &"t_c", 2).id == "exact2", "same key + tiers: replaced (deterministic)")
	check(not Interactions.can_add("stone", "wall_stone", {"outcome": "pass"}), "legacy cell cannot be replaced")
	check(Interactions.can_add("stone", "wall_stone", {"outcome": "pass", "tiers": [3]}) == false, "not even for some tiers")
	check(Interactions.can_add("sound", "wall_stone", {"outcome": "reflect"}), "a kit may add a new threat class cell")
	check(Interactions.rule(&"stone", &"guard").get("legacy", false), "legacy guard cell")
	h.end_scope()
	check(not Interactions.has_rule(&"stone", &"t_c"), "scope restored")


func test_predict_is_pure_and_move_counters() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.register("t_shield", {"verb": "barrier", "barrier": "held", "mat": "water", "mass": 6.0, "counter": {"cls": "t_sh"}})
	Moves.register("t_gale", {"verb": "cone", "counter": {"cls": "t_g", "power": [7, 11, 18, 28]}})
	Interactions.add_rule("stone", "t_g", {"eff": 2.0, "outcome": "deflect", "partial": "bend"})
	var b := _stone(20.0, 17.0)
	var v0 := b.vel
	var n := h.log.size()
	var ne := h.w.events.size()
	var t := Agent.of_body(h.w, b)
	var r := h.predict(t, "t_gale", 3)
	check(r.outcome == "deflect" and is_equal_approx(float(r.cp), 28.0) and is_equal_approx(float(r.cp_eff), 56.0), "gale T3 deflects")
	check(h.predict(t, "t_gale", 0).outcome == "bend", "T0 bends")
	check(is_equal_approx(float(h.predict(t, "t_shield", 0).cp), 6.0), "barrier spec power = mass x hardness (held water 1/kg)")
	check(b.vel == v0 and h.w.events.size() == ne and h.log.size() == n, "predict changes nothing")
	h.end_scope()


func test_allows_drives_technique_legality() -> void:
	h = SimHarness.new(1)
	var s := _stone(20.0, 0.0)
	check(Interactions.allows(s, &"grip_stone"), "seize a stone")
	check(not Interactions.allows(s, &"draw_heat"), "no heat to draw from cold stone")
	Thermal.heat(s, 300.0)
	check(Interactions.allows(s, &"draw_heat"), "hot rock: draw")
	var m := h.w.spawn_body(Sim.Mat.METAL, Sim.Form.CHUNK, 5.0, Vector3.ZERO, "test")
	check(not Interactions.allows(m, &"grip_stone"), "Stone Seize does not take metal")
	check(Interactions.cohesion(2) == 0.8 and Interactions.disrupt_threshold(1) == 10.0, "cohesion / disrupt thresholds")
