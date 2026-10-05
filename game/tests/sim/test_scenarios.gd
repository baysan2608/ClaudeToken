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


func test_lab_scenario_has_everything_unlocked() -> void:
	var pr := Progression.new()
	pr.path = "user://test_progress.cfg"
	var r := Scenarios.build("lab", pr, 1)
	var p: ActorState = r.player
	var o: ActorState = r.opponent
	check(o != null, "the Lab has a rival (it throws what the spawner launches)")
	for e in 4:
		check(p.elements[e] and o.elements[e], "element %d unlocked for both" % e)
		for s in 4:
			check(p.subs_unlocked[e][s], "%s/%s unlocked" % [Sim.ELEMENT_NAMES[e], Sim.SUB_NAMES[e][s]])
	for t in Progression.ALL:
		if t in Progression.LAB_OMIT:
			# The bolt lives on Fire / Lightning in the Lab; a long Flame hold grows to Fire Column / Inferno.
			check(not p.has(t), "legacy flag %s left out so Flame T2/T3 are reachable" % t)
			continue
		check(p.has(t), "player technique %s" % t)
	check(Moves.resolve(2, 2, "strike") != "" and Moves.resolve(2, 2, "strike") != "fire_attack", "the bolt is on Fire / Lightning")
	var dummies := 0
	for a in r.world.actors:
		if a.is_dummy:
			dummies += 1
	check(dummies >= 2, "dummies to hit")
	check(r.def.get("lab", false), "flagged as the lab")
	check(bool(r.def.opponent.get("ai_default", true)) == false, "the rival starts passive in the Lab")


func test_every_legacy_scenario_id_is_kept() -> void:
	var ids: Array[String] = []
	for s in Scenarios.LIST:
		ids.append(s.id)
	for want in ["molten_exchange", "spar", "stone_rain", "boulder", "lava_paths", "contest", "water_ice", "conduction", "redirect", "cold_hands", "updraft", "lab"]:
		check(ids.has(want), "scenario %s exists" % want)


func test_spar_picks_difficulty_and_kit() -> void:
	var pr := Progression.new()
	pr.path = "user://test_progress.cfg"
	var base := Scenarios.build("spar", pr, 1)
	check((base.opponent as ActorState).elements == [true, false, true, false], "default rival: earth + fire")
	check(base.ai_cfg.preset == "adept", "default difficulty")
	pr.spar_difficulty = "master"
	pr.spar_kit = "water"
	var r := Scenarios.build("spar", pr, 1)
	var o: ActorState = r.opponent
	check(o.elements == [false, true, false, false], "water kit")
	check(o.element == 1, "starts on water")
	check(r.ai_cfg.preset == "master", "difficulty reaches the AI config")
	check(r.ai_cfg.elements == [1], "AI kit")
	pr.spar_kit = "fire/blue"
	var r2 := Scenarios.build("spar", pr, 1)
	check(r2.ai_cfg.subs == {2: [1]}, "one sub-element")
	pr.spar_kit = "all"
	check((Scenarios.build("spar", pr, 1).opponent as ActorState).elements == [true, true, true, true], "all four")
	# Other scenarios are unchanged by the spar choice.
	var me := Scenarios.build("molten_exchange", pr, 1)
	check(not me.ai_cfg.has("preset"), "legacy rivals keep their legacy config")
	# Practice list carries the pickers; choices persist.
	var items := Scenarios.practice_items(pr)
	var spar_item: Dictionary = {}
	for it in items:
		if it.id == "spar":
			spar_item = it
	check(spar_item.has("options") and spar_item.options.size() == 2, "spar offers difficulty and kit")
	pr.save()
	var back := Progression.load_from(pr.path)
	check(back.spar_kit == "all", "kit persists")
	check(back.spar_difficulty == "master", "difficulty persists")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(pr.path))


func test_ai_configure_is_guarded_and_accepts_the_spar_config() -> void:
	var pr := Progression.new()
	pr.path = "user://test_progress.cfg"
	pr.spar_kit = "air"
	pr.spar_difficulty = "novice"
	var r := Scenarios.build("spar", pr, 3)
	var ai := AiBrain.new(r.world, r.opponent, r.ai_cfg, 3)
	if ai.has_method("configure"):
		ai.configure(r.ai_cfg)
	for k in 600:
		r.world.step({r.player.id: ActorIntent.new(), r.opponent.id: ai.think(Sim.DT)})
		r.world.take_events()
	check(r.opponent.pos.is_finite(), "the configured rival runs")
