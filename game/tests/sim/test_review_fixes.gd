extends TestCase
## Regressions for the 2026-10-06 review fixes (docs/REVIEW.md "Round 3"): counter strength vs threat power,
## lightning vs held water / wind, once-per-contact partials, softer hits after partial counters, clean blocks,
## ledger leaks, NaN merges, sound vs held guards, the grounded stance and the Lab's push / sink tiers.

var h: SimHarness


func _all_finite() -> bool:
	for b in h.w.bodies:
		if b.alive and (not b.pos.is_finite() or not b.vel.is_finite() or is_nan(b.liquid) or is_nan(b.temp)):
			return false
	return true


func _guard_agent(ccls: StringName, power: float, perfect: bool) -> Agent:
	var g := Agent.new()
	g.kind = "move"
	g.ccls = ccls
	g.cls = ccls
	g.power = power
	g.perfect = perfect
	return g


func _threat(cls: String, ch: String, tp: float, mass: float) -> Agent:
	var g := Agent.new()
	g.kind = "volume"
	g.cls = StringName(cls)
	g.ccls = g.cls
	g.mass = mass
	g.hostile = true
	g.ch[ch] = tp
	return g


# ---------------------------------------------------------------- counter strength scales with the threat

func test_plain_guards_scale_with_the_threat_and_perfect_needs_a_holdable_threat() -> void:
	h = SimHarness.new(3)
	var boulder := _threat("boulder", "K", 159.0, 200.0)
	for c in [[&"guard_earth", 10.0], [&"guard", 10.0], [&"aura_flame", 10.0], [&"aura_blue", 16.0], [&"ward_static", 12.0],
			[&"guard_blast", 18.0], [&"guard_wind", 12.0]]:
		for perfect in [false, true]:
			var pr := Interactions.predict(h.w, boulder, _guard_agent(c[0], float(c[1]), perfect))
			check(String(pr.outcome) == "overwhelm", "200 kg boulder vs %s%s: overwhelmed (%s r%.2f)" % [c[0], " (perfect)" if perfect else "", pr.outcome, pr.ratio])
	var stone := _threat("stone", "K", 17.0, 20.0)
	var heave := _threat("stone_heavy", "K", 31.5, 45.0)
	var ps := Interactions.predict(h.w, stone, _guard_agent(&"guard", 10.0, true))
	check(String(ps.outcome) == "deflect", "perfect plain guard deflects a 20 kg stone (%s)" % ps.outcome)
	var ph := Interactions.predict(h.w, heave, _guard_agent(&"guard", 10.0, true))
	check(String(ph.outcome) == "block", "a 45 kg heave (TP 31.5) is only blocked, even perfect (%s r%.2f)" % [ph.outcome, ph.ratio])
	# Molten: the wind wrap and the fire / static / blast guards are no wall against lava (the owner's rule).
	var lava := _threat("lava_wave", "H", 27.3, 20.0)
	for c in [[&"guard_wind", 12.0], [&"ward_static", 12.0], [&"aura_flame", 10.0], [&"guard_blast", 18.0]]:
		var pl := Interactions.predict(h.w, lava, _guard_agent(c[0], float(c[1]), false))
		check(String(pl.outcome) == "overwhelm", "lava wave vs held %s: overwhelmed (%s r%.2f)" % [c[0], pl.outcome, pl.ratio])


func test_a_heavy_stone_chips_harder_through_a_plain_guard() -> void:
	h = SimHarness.new(3)
	var d := h.actor("D", Vector3(0, 0, 4), 0, {}, Sim.Element.FIRE)   # Flame Guard: a plain CP 10 guard
	h.step(5)
	var hits := []
	for m in [[20.0, 15.0], [45.0, 14.0]]:
		d.health = 100.0
		d.balance = 100.0
		h.press(d, "guard")
		h.step(20)
		var b := h.launch_at(d, "stone", float(m[0]), float(m[1]), Sim.AMBIENT_C, "", null, 6.0)
		var dmg := 12.0 * sqrt(float(m[0]) / 20.0)
		b.damage = dmg
		h.until(func(): return not b.alive or b.attack_id == 0, 60)
		hits.append((100.0 - d.health) / dmg)
		h.release(d, "guard")
		h.step(40)
	near(float(hits[0]), 0.12, 0.011, "a 20 kg stone: the legacy 12 % chip")
	check(float(hits[1]) > 0.16, "a 45 kg heave chips harder through the same guard (%.3f of its damage)" % float(hits[1]))


# ---------------------------------------------------------------- lightning

func test_lightning_conducts_through_a_water_shield_and_ignores_a_wind_guard() -> void:
	h = SimHarness.new(3)
	var bolt := _threat("lightning", "E", 36.0, 0.0)
	var sh := Interactions.predict(h.w, bolt, _guard_agent(&"shield_water", 6.0, false))
	check(String(sh.outcome) == "conduct" and absf(float(sh.rule.get("factor", 0.0)) - 1.5) < 1e-6, "water shield conducts x1.5 (%s)" % sh.outcome)
	var wg := Interactions.predict(h.w, bolt, _guard_agent(&"guard_wind", 12.0, true))
	check(String(wg.outcome) == "pass", "wind guard: the bolt passes (%s)" % wg.outcome)
	var pg := Interactions.predict(h.w, _threat("lightning", "E", 24.0, 0.0), _guard_agent(&"guard_earth", 10.0, false))
	check(String(pg.outcome) == "weaken", "a held plain guard only takes its own power off a bolt (%s)" % pg.outcome)


func test_the_grounded_stance_replaces_the_guard_chip() -> void:
	h = SimHarness.new(3)
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	var t := h.actor("T", Vector3(0, 0, -4), 1, {}, Sim.Element.FIRE)
	h.step(5)
	h.aim(a, t.pos - a.pos)
	h.press(a, "guard")
	h.step(20)
	check(a.surface == "stone" and a.grounded, "the Earth fighter guards on stone")
	t.lock_target = a.id
	var n0 := h.events("block").size()
	Conduction.discharge(h.w, t, a.chest(), {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
		h.w.new_attack_id(), true)
	near(100.0 - a.health, 24.0 * 0.4, 1e-6, "the grounded stance takes 40 % (no extra guard chip on top)")
	check(h.events("block").size() == n0, "no plain-guard block on top of the stance")


# ---------------------------------------------------------------- partials: once per contact, softer hits, heat caps

func test_a_small_fog_does_not_stop_an_80_kg_lava_wave() -> void:
	h = SimHarness.new(3)
	var o := h.actor("O", Vector3(10, 0, 10), 0, {}, Sim.Element.WATER)
	h.step(2)
	var fog := WaterUtil.zone(h.w, "fog", Vector3(0, 0, -1.0), 3.0, o.id, 30.0, {"height": 3.0, "rate": 0.1}, Sim.Mat.STEAM, 2.0, 6.0)
	o.water_carried -= 2.0
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, 80.0, Vector3(0, 0, -6.0), "test")
	h.w.mass_ledger.ground_taken += 80.0
	h.w.ledger.generated += Thermal.heat(b, 80.0 * (Sim.STONE_C * 980.0 + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = Vector3(0, 0, 1)
	b.wave_budget = 14.0
	b.vel = Vector3(0, 0, 7.5)
	b.attack_id = h.w.new_attack_id()
	var e0 := h.w.system_energy() - h.w.ledger_balance()
	h.step(75)
	check(b.alive and b.pos.z > 2.5, "the wave rolled through the fog (z %.1f, %s)" % [b.pos.z, Sim.FORM_NAMES[b.form]])
	check(b.liquid > 0.6, "it is still molten (liquid %.2f)" % b.liquid)
	check(fog.alive or true, "fog")
	near(h.w.system_energy() - h.w.ledger_balance(), e0, 1e-3, "energy ledger")


func test_a_partial_applies_once_per_contact_and_softens_the_hit() -> void:
	h = SimHarness.new(3)
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 80.0, Vector3(0, 1, 0), "test")
	h.w.mass_ledger.ground_taken += 80.0
	b.vel = Vector3(0, 0, 11.0)
	b.attack_id = h.w.new_attack_id()
	var wall := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.WALL, 115.0, Vector3(0, 0, 2), "test")
	h.w.mass_ledger.ground_taken += 115.0
	var t := Agent.of_body(h.w, b)
	var c := Agent.of_body(h.w, wall)
	c.power = 28.8
	var r1 := Interactions.resolve(h.w, t, c, {"site": "wall"}, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	check(String(r1.outcome) == "weaken", "first contact: weaken (%s r%.2f)" % [r1.outcome, r1.ratio])
	var s1 := h.w.hit_scale(b)
	check(s1 < 0.6 and s1 > 0.2, "the weakened stone will hit softer (x%.2f)" % s1)
	h.step(1)
	var r2 := Interactions.resolve(h.w, Agent.of_body(h.w, b), c, {"site": "wall"}, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	check(String(r2.outcome) == "pass" and absf(h.w.hit_scale(b) - s1) < 1e-9, "the same contact does not weaken again (%s)" % r2.outcome)


# ---------------------------------------------------------------- clean blocks, ledgers, NaN

func _guard_vs_tap(de: int, ds: int, ae: int, asub: int, dist: float, gesture: int) -> Dictionary:
	h = SimHarness.new(5)
	var d := h.actor("D", Vector3(0, 0, dist * 0.5), 0, {}, de)
	var a := h.actor("A", Vector3(0, 0, -dist * 0.5), 1, {}, ae)
	d.subs[de] = ds
	a.subs[ae] = asub
	h.step(20)
	a.heat_reserve = 200.0
	h.aim(a, d.pos - a.pos)
	h.aim(d, a.pos - d.pos)
	h.press(d, "guard")
	h.step(12)
	var e0 := h.w.system_energy() - h.w.ledger_balance()
	if gesture == 0:
		h.press(a, "attack")
	else:
		h.flick(a, "attack", gesture)
	h.step(1)
	h.release(a, "attack")
	var finite := true
	for k in 70:
		h.step(1)
		finite = finite and _all_finite()
	var staggered := false
	var stopped := false
	for e in h.log:
		if int(e.tick) <= 32:
			continue
		if e.type == "stagger" and int(e.actor) == d.id:
			staggered = true
		if e.type == "interaction" and int(e.counter_actor) == d.id and ["absorb", "extinguish", "neutralize"].has(String(e.outcome)):
			stopped = true
	return {"drift": h.w.system_energy() - h.w.ledger_balance() - e0, "staggered": staggered, "stopped": stopped, "d": d,
		"finite": finite}


func test_a_guard_that_absorbs_the_threat_stays_up() -> void:
	var r := _guard_vs_tap(Sim.Element.AIR, 1, Sim.Element.AIR, 0, 2.6, 0)   # Vortex Wall vs a palm gust
	check(not r.staggered, "Vortex Wall vs palm gust: no stagger")
	check((r.d as ActorState).health == 100.0, "no damage")
	var r2 := _guard_vs_tap(Sim.Element.AIR, 1, Sim.Element.FIRE, 0, 2.6, 0)  # Vortex Wall vs a fire jab (extinguish)
	check(not r2.staggered and absf(float(r2.drift)) < 0.01, "Vortex Wall vs a fire jab: clean and booked (drift %.3f)" % float(r2.drift))


func test_vortex_wall_absorbing_a_gust_crescent_stays_finite() -> void:
	var r := _guard_vs_tap(Sim.Element.AIR, 1, Sim.Element.AIR, 0, 6.0, Sim.Gesture.UP)
	check(r.finite, "every body stays finite (massless merge)")


func test_dew_fall_then_a_held_guard_stays_finite() -> void:
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(0, 0, 4), 0, {}, Sim.Element.WATER)
	var r := h.actor("R", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
	r.is_dummy = true
	w.subs[Sim.Element.WATER] = 2
	h.step(10)
	h.press(w, "guard")
	h.step(12)
	h.flick(w, "guard", Sim.Gesture.DOWN)
	var finite := true
	for k in 120:
		h.step(1)
		finite = finite and _all_finite()
	check(finite, "Dew Fall + the guard held on: every body finite")


func test_the_guard_matrix_keeps_the_energy_ledger() -> void:
	# Every guard (16 sub-elements) against every element's tap strike (16): no unbooked heat.
	var bad := []
	var stag := []
	for de in 4:
		for ds in 4:
			for ae in 4:
				for asub in 4:
					var r := _guard_vs_tap(de, ds, ae, asub, 2.6, 0)
					if absf(float(r.drift)) > 0.01:
						bad.append("D %d/%d vs A %d/%d drift %.2f" % [de, ds, ae, asub, float(r.drift)])
					if not r.finite:
						bad.append("D %d/%d vs A %d/%d non-finite" % [de, ds, ae, asub])
	check(bad.is_empty(), "%d guard cells leak: %s" % [bad.size(), ", ".join(PackedStringArray(bad))])


# ---------------------------------------------------------------- sound vs held guards

func test_a_sound_clap_does_not_disrupt_a_held_null_bubble() -> void:
	var r := _guard_vs_tap(Sim.Element.AIR, 2, Sim.Element.AIR, 3, 2.6, 0)
	check(not h.has_event("disrupt"), "the bubble is not disrupted")
	check((r.d as ActorState).health == 100.0, "the vacuum swallows the clap (health %.1f)" % (r.d as ActorState).health)


# ---------------------------------------------------------------- the Lab plays push / sink at the requested tier

func test_lab_script_push_and_sink_reach_the_requested_tier() -> void:
	var bad := []
	var n := 0
	for e in 4:
		for s in 4:
			for slot in ["push", "sink"]:
				var id := Moves.resolve(e, s, slot)
				if id == "" or not Moves.DEFS.has(id):
					continue
				var mt := Charge.max_tier(Moves.DEFS[id])
				for tier in range(1, mt + 1):
					h = SimHarness.new(7)
					var d := h.actor("D", Vector3(0, 0, 4), 0, {"heat_draw": true, "magma": true, "lightning": true}, e)
					var o := h.actor("O", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
					o.is_dummy = true
					d.elements = [true, true, true, true]
					h.step(3)
					var sc := LabScript.for_move(e, s, slot, tier)
					var best := -1
					for k in sc.length() + 20:
						var it := h.it(d)
						it.clear()
						LabScript.apply_dict(it, sc.next())
						h.step(1)
						if d.action != null and d.action.id == id:
							best = maxi(best, d.action.tier())
					n += 1
					if best < tier:
						bad.append("%s T%d reached T%d" % [id, tier, best])
	check(bad.is_empty(), "%d push / sink tiers short: %s" % [bad.size(), ", ".join(PackedStringArray(bad))])
	note("%d push / sink tier runs" % n)
