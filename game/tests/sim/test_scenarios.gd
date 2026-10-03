extends TestCase
## Every lab scenario builds, runs 40 s of AI + random player input, can be reset
## repeatedly, and keeps its invariants (bounded bodies, conserved mass, balanced energy).

func _random_intent(rng: RandomNumberGenerator, it: ActorIntent) -> void:
	it.attack_pressed = false
	it.attack_released = false
	it.guard_pressed = false
	it.evade_pressed = false
	it.tech_pressed = false
	it.tech_released = false
	it.tech_cancel = false
	it.element_select = -1
	if rng.randf() < 0.08:
		it.move = Vector3(rng.randf_range(-1, 1), 0, rng.randf_range(-1, 1)).limit_length(1.0)
	var r := rng.randf()
	if r < 0.01:
		it.element_select = rng.randi_range(0, 3)
	elif r < 0.03:
		it.attack_pressed = true
		it.attack_held = true
	elif r < 0.04:
		it.guard_pressed = true
		it.guard_held = true
	elif r < 0.045:
		it.evade_pressed = true
	elif r < 0.06:
		it.tech_pressed = true
		it.tech_held = true
	if it.attack_held and rng.randf() < 0.05:
		it.attack_held = false
		it.attack_released = true
	if it.guard_held and rng.randf() < 0.05:
		it.guard_held = false
	if it.tech_held and rng.randf() < 0.03:
		it.tech_held = false
		it.tech_released = true


func _run(id: String, seed_value: int, ticks: int) -> Dictionary:
	var pr := Progression.new()
	pr.path = "user://test_progress.cfg"
	var r := Scenarios.build(id, pr, seed_value)
	var w: CombatWorld = r.world
	var p: ActorState = r.player
	var o: ActorState = r.opponent
	var ai: AiBrain = AiBrain.new(w, o, r.ai_cfg, seed_value) if o else null
	var rng := RandomNumberGenerator.new()
	rng.seed = seed_value
	var it := ActorIntent.new()
	var e0 := w.system_energy()
	var s0 := w.stone_mass()
	var w0 := w.water_mass()
	var max_bodies := 0
	var bad := ""
	for k in ticks:
		_random_intent(rng, it)
		var intents := {p.id: it}
		if ai:
			intents[o.id] = ai.think(Sim.DT)
		w.step(intents)
		w.take_events()
		max_bodies = maxi(max_bodies, w.alive_count())
		for a in w.actors:
			if not a.pos.is_finite():
				bad = "non-finite actor position"
		for b in w.bodies:
			if b.alive and (not b.pos.is_finite() or b.mass < -1e-6):
				bad = "bad body %s" % b.describe()
	return {"w": w, "bad": bad, "max_bodies": max_bodies,
		"dE": w.system_energy() - e0 - w.ledger_balance(), "dS": w.stone_mass() - s0, "dW": w.water_mass() - w0}


func test_every_scenario_runs_with_invariants() -> void:
	for s in Scenarios.LIST:
		var res := _run(s.id, 5, 2400)
		check(res.bad == "", "%s: %s" % [s.id, res.bad])
		check(res.max_bodies <= Sim.MAX_BODIES, "%s: body cap (%d)" % [s.id, res.max_bodies])
		# Scenario "vent" lava and launcher stones are external sources recorded in the ledgers.
		check(absf(res.dE) < 1.0, "%s: energy ledger balances (off by %.3f HU)" % [s.id, res.dE])
		check(absf(res.dS) < 1e-3, "%s: stone mass conserved (off by %.4f kg)" % [s.id, res.dS])
		check(absf(res.dW) < 1e-3, "%s: water mass conserved (off by %.4f kg)" % [s.id, res.dW])


func test_repeated_resets_do_not_accumulate() -> void:
	var counts: Array[int] = []
	for k in 12:
		var res := _run("molten_exchange", 100 + k, 300)
		counts.append((res.w as CombatWorld).alive_count())
	check(counts.max() <= Sim.MAX_BODIES, "fresh worlds stay bounded %s" % str(counts))
	# Each reset is a brand-new world: body ids restart, nothing carries over.
	var r1 := Scenarios.build("molten_exchange", Progression.new(), 1)
	var r2 := Scenarios.build("molten_exchange", Progression.new(), 1)
	check((r1.world as CombatWorld).bodies.size() == (r2.world as CombatWorld).bodies.size(), "reset yields identical initial state")
