extends TestCase
## Matrix-driven counter planner (AiPlanner + AiBrain planner mode, docs/AI.md): full-outcome counters
## per kit, REC over BLK, costs and time to impact, difficulty, honesty (no hidden state), determinism.


func _wave(w: CombatWorld, owner: ActorState, mass: float, p: Vector3, dir: Vector3) -> MatBody:
	var d: Dictionary = Moves.DEFS.pour
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, p, "test", Sim.STONE_MELT_C)
	w.mass_ledger.ground_taken += mass
	b.liquid = 1.0
	b.phase = Sim.Phase.MOLTEN
	b.on_ground = true
	b.update_radius()
	b.wave_dir = dir
	b.wave_budget = 30.0
	b.wave_width = 1.1 + mass * 0.025
	b.wave_path = PackedVector3Array([p])
	b.max_life = -1.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = owner.id
	b.hit_set[owner.id] = true
	b.damage = float(d.damage)
	b.balance_damage = float(d.balance)
	return b


## A configured brain facing a rival: {h, p, o, ai}.
func _setup(kit: Dictionary, opts: Dictionary = {}, seed_value: int = 3, gap: float = 11.0) -> Dictionary:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", Vector3(0, 0, gap * 0.5), 0, {}, Sim.Element.FIRE)
	var o := h.actor("opponent", Vector3(0, 0, -gap * 0.5), 1, {}, int(kit.keys()[0]))
	var ai := AiBrain.new(h.w, o, {}, seed_value)
	var c := {"preset": "master", "elements": kit.keys(), "subs": kit, "drill": "passive", "counter": 1.0, "misjudge": 0.0}
	c.merge(opts, true)
	ai.configure(c)
	for k in 10:
		h.intents[o.id] = ai.think(Sim.DT)
		h.step()
	return {"h": h, "p": p, "o": o, "ai": ai}


func _best(d: Dictionary, th: Dictionary, extra: Dictionary = {}) -> Dictionary:
	var ai: AiBrain = d.ai
	var prm := ai._planner_params()
	prm.merge(extra, true)
	var opts := AiPlanner.counters(d.h.w, d.o, th, ai.kit, prm, 0.0)
	return opts[0] if not opts.is_empty() else {}


## Steps the duel until the body is gone / settled; returns whether the AI was hit by it.
func _run_until_resolved(d: Dictionary, b: MatBody, ticks: int = 300) -> Dictionary:
	var h: SimHarness = d.h
	var o: ActorState = d.o
	var ai: AiBrain = d.ai
	var res := {"hit": false, "outcomes": []}
	for k in ticks:
		h.intents[o.id] = ai.think(Sim.DT)
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "hit" and e.actor == o.id:
				res.hit = true
			if e.type == "interaction" and int(e.get("counter_actor", -1)) == o.id:
				(res.outcomes as Array).append(String(e.outcome))
		if not b.alive or (b.form == Sim.Form.WAVE and b.wave_budget <= 0.0):
			break
	return res


# ------------------------------------------------------------------ full-outcome counters by kit

func test_lava_wave_full_counter_per_kit() -> void:
	# Earth/Stone: a wall answer (Bulwark, Ram Wall, Swallow trench, Rising Fangs) with a full outcome.
	var de := _setup({0: [0]})
	var we := _wave(de.h.w, de.p, 20.0, Vector3(0, 0, 4.5), Vector3(0, 0, -1))
	var be := _best(de, AiPlanner.body_threat(de.h.w, de.o, we))
	note("earth/stone vs lava wave: %s %s/%s r%.2f" % [be.get("label", "-"), be.get("outcome", ""), be.get("band", ""), be.get("ratio", 0.0)])
	check(int(be.get("element", -1)) == Sim.Element.EARTH and String(be.get("band", "")) == "full", "Earth/Stone answers the wave with a full outcome (%s)" % be.get("label", "-"))
	check(["guard", "push", "sink", "ground"].has(String(be.get("slot", ""))), "a wall / trench / spike answer (%s)" % be.get("slot", ""))
	var re := _run_until_resolved(de, we)
	check(not re.hit, "the wall answer stops the lava (%s)" % str(re.outcomes))
	# Fire/Flame with heat draw: DRAW sets the lava.
	var df := _setup({2: [0]}, {"kit": {"heat_draw": true}})
	var wf := _wave(df.h.w, df.p, 20.0, Vector3(0, 0, 4.5), Vector3(0, 0, -1))
	var bf := _best(df, AiPlanner.body_threat(df.h.w, df.o, wf))
	note("fire/flame vs lava wave: %s %s" % [bf.get("label", "-"), bf.get("mode", "")])
	check(String(bf.get("id", "")) == "fire_tech" and String(bf.get("mode", "")) == "DRAW", "Fire/Flame draws the heat (%s)" % bf.get("label", "-"))
	var rf := _run_until_resolved(df, wf)
	check(not rf.hit, "the draw sets the wave before it arrives")
	# Air/Gust, far wave: only a T3 gale (Hurricane Palm) answers it fully.
	var da := _setup({3: [0]}, {}, 3, 22.0)
	var wa := _wave(da.h.w, da.p, 20.0, Vector3(0, 0, 4.5), Vector3(0, 0, -1))
	var ba := _best(da, AiPlanner.body_threat(da.h.w, da.o, wa))
	note("air/gust vs far lava wave: %s %s/%s r%.2f" % [ba.get("label", "-"), ba.get("outcome", ""), ba.get("band", ""), ba.get("ratio", 0.0)])
	check(int(ba.get("element", -1)) == Sim.Element.AIR and int(ba.get("tier", 0)) == 3 and String(ba.get("band", "")) == "full",
		"Air/Gust needs a T3 gale for a full answer (%s)" % ba.get("label", "-"))


func test_simple_air_cannot_stop_lava() -> void:
	# A close lava wave: no gust tier is both strong enough and in time, so the gust kit never "attacks" the
	# lava with a weak gust; it guards (chip) or evades.
	var d := _setup({3: [0]})
	var wv := _wave(d.h.w, d.p, 20.0, Vector3(0, 0, 2.0), Vector3(0, 0, -1))
	var ai: AiBrain = d.ai
	var opts := AiPlanner.counters(d.h.w, d.o, AiPlanner.body_threat(d.h.w, d.o, wv), ai.kit, ai._planner_params(), 0.0)
	for o in opts:
		if ["strike", "thrust", "ground", "sweep"].has(String(o.slot)):
			check(String(o.band) != "full", "no full answer from a weak gust (%s r%.2f)" % [o.label, o.ratio])
	var best: Dictionary = opts[0]
	check(not ["strike", "thrust", "ground", "sweep"].has(String(best.slot)), "best answer is not a weak gust (%s)" % best.label)
	# The counter rule itself: Palm Gust T0 fails, Hurricane T3 sets it (MOVESET §5.4).
	var th := AiPlanner.body_threat(d.h.w, d.o, wv)
	var t0 := Interactions.predict(d.h.w, th.agent, Agent.of_move(d.h.w, d.o, "air_attack", 0, false))
	var t3 := Interactions.predict(d.h.w, th.agent, Agent.of_move(d.h.w, d.o, "air_attack", 3, false))
	check(AiPlanner.outcome_value(String(t0.outcome), String(t0.band), t0.rule) < 0.3,
		"palm gust T0 does not stop lava (%s/%s r%.2f)" % [t0.outcome, t0.band, t0.ratio])
	check(String(t3.band) == "full", "hurricane T3 answers lava (r%.2f)" % t3.ratio)


# ------------------------------------------------------------------ utility rules

func _test_moves(h: SimHarness, cost_rec: float = 5.0, startup_rec: float = 0.1, swap: bool = false) -> void:
	h.begin_scope()
	var base := {"element": 0, "sub": 1, "verb": "projectile", "startup": 0.1, "active": 0.05, "recovery": 0.2, "cost": 5.0,
		"source": "none", "mat": "stone", "mass": 5.0, "speed": 20.0, "damage": 1.0, "ai": {"role": "counter", "range": [0.0, 12.0]}}
	var rec := base.duplicate(true)
	rec["counter"] = {"cls": "ai_t_rec", "power": [20.0]}
	rec["cost"] = cost_rec
	rec["startup"] = startup_rec
	var blk := base.duplicate(true)
	blk["counter"] = {"cls": "ai_t_blk", "power": [20.0]}
	var ids := ["ai_t_blk", "ai_t_rec"] if swap else ["ai_t_rec", "ai_t_blk"]
	var defs := {"ai_t_rec": rec, "ai_t_blk": blk}
	for id in ids:
		Moves.register(id, defs[id])
	Moves.bind(0, 1, "thrust", ids[0])
	Moves.bind(0, 1, "sweep", ids[1])
	Interactions.add_rule("stone", "ai_t_rec", {"outcome": "reclaim", "partial": "weaken", "fail": "overwhelm"})
	Interactions.add_rule("stone", "ai_t_blk", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})


func test_prefers_reclaim_over_block_when_equal() -> void:
	for swap in [false, true]:
		var d := _setup({0: [1]})
		var h: SimHarness = d.h
		_test_moves(h, 5.0, 0.1, swap)
		var st := h.launch_at(d.o, "stone", 20.0, 17.0, 20.0, "", d.p, 10.0)
		var th := AiPlanner.body_threat(h.w, d.o, st)
		var best := _best(d, th, {"only": ["ai_t_rec", "ai_t_blk"]})
		h.end_scope()
		check(String(best.get("id", "")) == "ai_t_rec", "REC is preferred over an equal BLK (order %s: %s)" % [swap, best.get("label", "-")])
	check(AiPlanner.outcome_value("reclaim", "full") > AiPlanner.outcome_value("redirect", "full"), "REC > DEF↩")
	check(AiPlanner.outcome_value("redirect", "full") > AiPlanner.outcome_value("transform", "full"), "DEF↩ > XFM")
	check(AiPlanner.outcome_value("transform", "full") > AiPlanner.outcome_value("block", "full"), "XFM > BLK")
	check(AiPlanner.outcome_value("block", "full") > AiPlanner.outcome_value("weaken", "partial"), "BLK > partial")
	check(AiPlanner.outcome_value("weaken", "partial") > AiPlanner.outcome_value("overwhelm", "fail"), "partial > fail")


func test_respects_costs_and_time_to_impact() -> void:
	# Focus: a REC that costs more than the AI has is not an option; the affordable BLK is taken.
	var d := _setup({0: [1]})
	var h: SimHarness = d.h
	_test_moves(h, 40.0)
	d.o.focus = 20.0
	var st := h.launch_at(d.o, "stone", 20.0, 17.0, 20.0, "", d.p, 10.0)
	var best := _best(d, AiPlanner.body_threat(h.w, d.o, st), {"only": ["ai_t_rec", "ai_t_blk"]})
	check(String(best.get("id", "")) == "ai_t_blk", "an unaffordable REC is skipped (%s)" % best.get("label", "-"))
	d.o.focus = 100.0
	best = _best(d, AiPlanner.body_threat(h.w, d.o, st), {"only": ["ai_t_rec", "ai_t_blk"]})
	check(String(best.get("id", "")) == "ai_t_rec", "with Focus the REC is back (%s)" % best.get("label", "-"))
	h.end_scope()
	# Time to impact: a slow REC (startup 0.8 s) can't meet a stone 0.5 s out; it can meet one 1.3 s out.
	var d2 := _setup({0: [1]})
	var h2: SimHarness = d2.h
	_test_moves(h2, 5.0, 0.8)
	var near := h2.launch_at(d2.o, "stone", 20.0, 17.0, 20.0, "", d2.p, 8.0)
	var th_near := AiPlanner.body_threat(h2.w, d2.o, near)
	var b_near := _best(d2, th_near, {"only": ["ai_t_rec", "ai_t_blk"]})
	check(String(b_near.get("id", "")) == "ai_t_blk", "tti %.2f s: the slow REC is too late (%s)" % [th_near.tti, b_near.get("label", "-")])
	near.alive = false
	var far := h2.launch_at(d2.o, "stone", 20.0, 12.0, 20.0, "", d2.p, 16.0)
	var th_far := AiPlanner.body_threat(h2.w, d2.o, far)
	var b_far := _best(d2, th_far, {"only": ["ai_t_rec", "ai_t_blk"]})
	check(String(b_far.get("id", "")) == "ai_t_rec", "tti %.2f s: the slow REC is in time (%s)" % [th_far.tti, b_far.get("label", "-")])
	h2.end_scope()
	# Charge tiers cost time: no T3 hold (1.8 s) against a threat 0.8 s out.
	var d3 := _setup({3: [0]})
	var wv := _wave(d3.h.w, d3.p, 20.0, Vector3(0, 0, 0.0), Vector3(0, 0, -1))
	var th3 := AiPlanner.body_threat(d3.h.w, d3.o, wv)
	var ai3: AiBrain = d3.ai
	for o in AiPlanner.counters(d3.h.w, d3.o, th3, ai3.kit, ai3._planner_params(), 0.0):
		check(float(o.hold) < float(th3.tti), "no option holds longer than the time to impact (%s hold %.2f, tti %.2f)" % [o.label, o.hold, th3.tti])


# ------------------------------------------------------------------ difficulty

func _skill_run(preset: String, seed_value: int) -> Dictionary:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", Vector3(0, 0, 6), 0, {}, Sim.Element.EARTH)
	var o := h.actor("opponent", Vector3(0, 0, -5), 1, {}, Sim.Element.EARTH)
	var ai := AiBrain.new(h.w, o, {}, seed_value)
	ai.configure({"preset": preset, "elements": [0, 3], "subs": {0: [0, 1, 2, 3], 3: [0, 1, 2, 3]}, "drill": "passive"})
	var rng := RandomNumberGenerator.new()
	rng.seed = seed_value * 7919
	for k in 15:
		h.intents[o.id] = ai.think(Sim.DT)
		h.step()
	var hits := 0
	var fails := 0
	for n in 4:
		o.focus = 100.0
		o.health = 100.0
		o.balance = 100.0
		var mass := rng.randf_range(15.0, 60.0)
		var speed := rng.randf_range(13.0, 18.0)
		var b := h.launch_at(o, "stone", mass, speed, 20.0 if rng.randf() < 0.7 else 900.0, "", p, rng.randf_range(9.0, 13.0))
		var hit := false
		for k in 150:
			h.intents[o.id] = ai.think(Sim.DT)
			var n0 := h.log.size()
			h.step()
			for i in range(n0, h.log.size()):
				var e: Dictionary = h.log[i]
				if e.type == "hit" and e.actor == o.id:
					hit = true
			if not b.alive or (b.attack_id == 0) or b.pos.distance_to(o.chest()) > 25.0:
				break
		hits += 1 if hit else 0
		if not ai.last_plan.is_empty() and String(ai.last_plan.get("band", "")) != "full":
			fails += 1
		for k in 40:
			h.intents[o.id] = ai.think(Sim.DT)
			h.step()
	return {"hits": hits, "fails": fails}


func test_novice_fails_more_than_master() -> void:
	var nov := 0
	var mas := 0
	for s in 5:   # 5 seeds x 4 threats = 20 seeded threats per preset
		nov += int(_skill_run("novice", 100 + s).hits)
		mas += int(_skill_run("master", 100 + s).hits)
	note("hits taken over 20 thrown stones: novice %d, master %d" % [nov, mas])
	check(nov > mas, "the novice is hit more often than the master (%d vs %d)" % [nov, mas])
	check(mas <= 6, "the master answers most threats (%d/20 hits)" % mas)


# ------------------------------------------------------------------ honesty and determinism

## Two worlds with the same observable state; world B differs only in hidden state (the rival's intent,
## buffered press, unrevealed technique flags, world RNG, private body/action data). The AI must act the same.
func test_never_reads_hidden_state() -> void:
	var streams := []
	for variant in 2:
		var d := _setup({0: [0, 1, 2, 3], 3: [0, 1, 2, 3]}, {"counter": 0.6, "misjudge": 0.2}, 5)
		var h: SimHarness = d.h
		var p: ActorState = d.p
		var st := h.launch_at(d.o, "stone", 25.0, 15.0, 20.0, "", p, 12.0)
		if variant == 1:
			h.w.rng.seed = 987654
			for k in 17:
				h.w.rng.randi()
			p.buffered = "tech"
			p.buffered_tick = h.w.tick
			p.kit = {"magma": true, "heat_draw": true, "lightning": true, "redirect_current": true}
			var it := h.it(p)
			it.attack_held = true
			it.guard_held = true
			it.aim_dir = Vector3(1, 0, 0)
			st.props["ai_secret"] = 42
			st.residual_owner = 99
		var ai: AiBrain = d.ai
		var s := []
		for k in 90:
			var i := ai.think(Sim.DT)
			s.append("%s|%s|%s|%s|%s|%s|%d|%d|%d" % [i.attack_pressed, i.guard_pressed, i.tech_pressed, i.evade_pressed,
				i.guard_held, i.tech_held, i.element_select, i.sub_select, i.guard_gesture])
			# Advance the stone by hand (identical in both worlds), the world itself does not step.
			st.pos += st.vel * Sim.DT
		s.append(String(ai.last_plan.get("label", "-")))
		streams.append(s)
	check(streams[0] == streams[1], "the AI's decisions ignore hidden state")
	note("decision: %s" % streams[0][streams[0].size() - 1])


func _duel(seed_value: int, ticks: int, preset_a: String = "master", preset_b: String = "master") -> Dictionary:
	var h := SimHarness.new(seed_value)
	var a := h.actor("A", Vector3(0, 0, 7), 0, {}, Sim.Element.EARTH)
	var b := h.actor("B", Vector3(0, 0, -7), 1, {}, Sim.Element.FIRE)
	var ba := AiBrain.new(h.w, a, {}, seed_value * 2 + 1)
	var bb := AiBrain.new(h.w, b, {}, seed_value * 2 + 2)
	ba.configure({"preset": preset_a, "elements": [0, 1, 2, 3]})
	bb.configure({"preset": preset_b, "elements": [0, 1, 2, 3]})
	return {"h": h, "a": a, "b": b, "ba": ba, "bb": bb, "ticks": ticks}


func _hash(h: SimHarness) -> String:
	var parts := []
	for x in h.w.actors:
		parts.append("%.4f,%.4f,%.4f,%.3f,%.3f,%d,%d" % [x.pos.x, x.pos.z, x.health, x.focus, x.heat_reserve, x.element, x.sub()])
	var n := 0
	for bd in h.w.bodies:
		if bd.alive:
			n += 1
			parts.append("%d:%.3f,%.3f,%.2f" % [bd.id, bd.pos.x, bd.pos.z, bd.mass])
	parts.append(str(n))
	return ";".join(parts).sha256_text()


func test_deterministic_per_seed() -> void:
	var hashes := []
	for run in [[21, 0], [21, 1], [22, 0]]:
		var d := _duel(int(run[0]), 1200)
		var h: SimHarness = d.h
		var acts := []
		for k in 1200:
			h.intents[d.a.id] = d.ba.think(Sim.DT)
			h.intents[d.b.id] = d.bb.think(Sim.DT)
			h.step()
		for e in h.log:
			if e.type == "action" and e.phase == "startup":
				acts.append("%d:%s" % [e.actor, e.move])
		hashes.append([_hash(h), acts.size(), str(acts).sha256_text()])
	check(hashes[0][0] == hashes[1][0] and hashes[0][2] == hashes[1][2], "same seed -> same duel (%d actions)" % hashes[0][1])
	check(hashes[0][0] != hashes[2][0], "another seed -> another duel")
