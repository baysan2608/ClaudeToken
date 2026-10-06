extends AirKitTest
## Air / Gust (sub 0): the legacy palm gust / cyclone / updraft / dash / Wind Guard stay exact at T0-T1, Gale and
## Hurricane Palm follow the lava rule, every new move runs at T0-T3, the owner's "deflect it with wind" example.

const GUST_MOVES := ["air_attack", "gust_crescent", "gust_dust_line", "gust_crosswind", "gust_wall", "gust_downdraft", "gust_tailwind", "gust_grip"]


func _a_vs_r(dist: float = 10.0) -> Array:
	return duel(0, Sim.Element.EARTH, 3, dist)


# ---------------------------------------------------------------- legacy kit stays exact

func test_legacy_palm_gust_and_cyclone_numbers() -> void:
	var s := _a_vs_r(4.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var f0 := a.focus
	h.press(a, "attack")
	h.step()
	h.release(a, "attack")
	h.step(40)
	var g := h.last_event("gust")
	check(not g.is_empty() and not g.heavy, "a tap fires the palm gust")
	near(float(g.range), 5.5, 1e-6, "palm gust range 5.5 m")
	check(r.health < 100.0 and r.vel.length() > 0.0 or r.stun > 0.0 or r.health < 100.0, "the rival at 4 m is hit")
	near(f0 - a.focus, 5.0, 0.8, "palm gust costs 5 Focus")
	var fx := h.events("fx").filter(func(e): return e.fx == "cone" and e.mat == "wind")
	check(fx.size() >= 1 and absf(float(fx[0].angle) - 35.0) < 1e-3, "cone fx 35 deg")
	# the hold: cyclone push (legacy T1) 7 m 45 deg
	s = _a_vs_r(4.0)
	a = s[0]
	h.press(a, "attack")
	h.step(40)
	h.release(a, "attack")
	h.step(30)
	g = h.last_event("gust")
	check(not g.is_empty() and g.heavy and int(g.tier) == 1, "a 0.6 s hold fires the cyclone push at T1")
	near(float(g.range), 7.0, 1e-6, "cyclone range 7 m")


func test_gust_tiers_have_the_designed_ranges_cones_and_powers() -> void:
	var want := [[5.5, 35.0, 7.0], [7.0, 45.0, 11.0], [8.0, 50.0, 18.0], [10.0, 60.0, 28.0]]
	for tier in 4:
		var s := _a_vs_r(14.0)
		var a: ActorState = s[0]
		run_move(a, "air_attack", tier, 1 if tier == 0 else 30, 70)
		var g := h.last_event("gust")
		check(not g.is_empty(), "T%d fires" % tier)
		near(float(g.range), want[tier][0], 1e-6, "T%d range" % tier)
		var fx := h.events("fx").filter(func(e): return e.fx == "cone")
		check(fx.size() >= 1, "T%d cone fx" % tier)
		if fx.size() >= 1:
			near(float(fx[0].angle), want[tier][1], 1e-3, "T%d cone angle" % tier)
			near(float(fx[0].power), want[tier][2], 1e-3, "T%d pressure" % tier)
		check(a.action == null, "T%d ends cleanly" % tier)
		fx_catalogued("gust T%d" % tier)


func test_gust_hold_tier_ladder_from_real_input() -> void:
	# The real hold clock: 0.4 s T1, 1.0 s T2, 1.8 s T3 (Charge), drain from T1 on only for T2+.
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	h.press(a, "attack")
	var tiers := {}
	for k in 150:
		h.step()
		if a.action != null and a.action.id == "air_attack":
			tiers[a.action.tier()] = true
	check(tiers.has(1) and tiers.has(2) and tiers.has(3), "held 2.5 s reaches T3: %s" % [tiers.keys()])
	check(h.events("charge").size() == 3, "one charge event per tier (%d)" % h.events("charge").size())
	h.release(a, "attack")
	h.step(40)
	var g := h.last_event("gust")
	check(not g.is_empty() and int(g.tier) == 3 and absf(float(g.range) - 10.0) < 1e-6, "released at T3: Hurricane Palm")


func test_gust_costs_per_tier() -> void:
	var costs := [5.0, 12.0, 18.0, 24.0]
	for tier in 4:
		var s := _a_vs_r(14.0)
		var a: ActorState = s[0]
		var f0 := a.focus
		var fmin := f0
		var it := h.it(a)
		h.w.start_action(a, "air_attack", it, {"slot": "strike", "tier": tier, "charge_frozen": true})
		it.attack_held = tier > 0
		for k in 30:
			h.step()
			fmin = minf(fmin, a.focus)
		it.attack_held = false
		for k in 50:
			h.step()
			fmin = minf(fmin, a.focus)
		check(h.has_event("gust"), "T%d fired" % tier)
		near(f0 - fmin, costs[tier], 1.0, "T%d total cost" % tier)


func test_gust_without_focus_falls_back_a_tier_never_negative() -> void:
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	a.focus = 14.0                   # pays the 5 start + 7 heavy, not the +12 hurricane
	run_move(a, "air_attack", 3, 30, 60)
	var g := h.last_event("gust")
	check(not g.is_empty() and int(g.tier) < 3, "short of Focus the hurricane fires a tier lower (T%s)" % [g.get("tier", "?")])
	check(a.focus >= 0.0, "Focus never negative")


# ---------------------------------------------------------------- the lava rule

func _wave_after_gust(tier: int, mass: float) -> Dictionary:
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	var holder := {}
	var spawn := func() -> void:
		holder["wave"] = lava_wave(mass, Vector3(0, 0, a.pos.z - 4.0))
		holder["liquid0"] = holder.wave.liquid
		holder["base"] = snap(h.w)
	run_when(a, "air_attack", tier, spawn, 28, 12)
	return {"wave": holder.wave, "liquid0": holder.liquid0, "base": holder.base, "a": a}


func test_you_can_not_block_lava_with_a_simple_air_attack() -> void:
	# 20 kg lava wave: TP 27.3. Palm Gust T0 (7) and Cyclone T1 (11) fail, Gale T2 (18) crusts and slows, Hurricane Palm T3 (28) sets it.
	var res := []
	for tier in 4:
		var o := _wave_after_gust(tier, 20.0)
		var wave: MatBody = o.wave
		res.append([wave.form, wave.liquid, o.liquid0, wave.alive])
		if tier <= 1:
			check(wave.form == Sim.Form.WAVE and wave.liquid >= float(o.liquid0) - 0.08, "T%d can't touch the lava (liquid %.2f)" % [tier, wave.liquid])
		elif tier == 2:
			check(wave.form == Sim.Form.WAVE and wave.liquid < float(o.liquid0) - 0.2 and wave.liquid > 0.0, "T2 Gale crusts it: liquid %.2f -> %.2f, still a wave" % [o.liquid0, wave.liquid])
			check(Thermal.flow_factor(wave) < 1.0, "and slows it (flow %.2f)" % Thermal.flow_factor(wave))
		else:
			check(wave.form != Sim.Form.WAVE and wave.liquid <= 0.0, "T3 Hurricane Palm stalls the wave and sets it into rock (form %s)" % [Sim.FORM_NAMES[wave.form]])
		var ix := h.events("interaction").filter(func(e): return String(e.threat) == "lava_wave" and String(e.counter) == "gust")
		if tier >= 2:
			check(ix.size() >= 1, "T%d: an interaction event" % tier)
		# energy: every HU the wind took is booked (ambient)
		ledgers_ok(o.base, "lava T%d" % tier, 1e-4)


func test_a_45_kg_lava_wave_survives_a_hurricane() -> void:
	var o := _wave_after_gust(3, 45.0)
	var wave: MatBody = o.wave
	check(wave.alive and wave.form == Sim.Form.WAVE and wave.liquid > 0.5, "a 45 kg wave (TP 61.5) is still flowing after Hurricane Palm (liquid %.2f)" % wave.liquid)
	ledgers_ok(o.base, "45 kg", 1e-4)


# ---------------------------------------------------------------- deflect it with wind (owner example)

func test_wind_guard_deflects_a_20_kg_stone_and_a_perfect_one_returns_it() -> void:
	var s := _a_vs_r(12.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	h.press(a, "guard")
	h.step(30)
	var s1 := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r)
	h.until(func(): return h.has_event("deflect") or h.has_event("hit") or h.has_event("block"), 60)
	check(h.has_event("deflect", "actor", a.id) and a.health == 100.0, "Wind Guard (12 x 1.5 = 18 >= 17) deflects the shot")
	check(s1.attack_id == 0, "the deflected stone is spent")
	h.release(a, "guard")
	h.step(40)
	h.log.clear()
	h.press(a, "guard")
	h.step(2)
	var s2 := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 2.5)
	h.until(func(): return h.has_event("perfect_deflect") or h.has_event("hit"), 40)
	check(h.events("perfect_deflect").any(func(e): return e.get("verb", "") == "reflect"), "a perfect guard (x1.5 = 27) is Return Wind: back to the sender")
	check(s2.attack_owner == a.id and s2.vel.dot(r.pos - s2.pos) > 0.0, "now A's stone, flying at R")


func test_palm_gust_t0_only_bends_a_stone_a_cyclone_turns_it() -> void:
	# The legacy gust cell follows the counter rule (MOVESET §5.4): Palm Gust 7 x2 = 14 vs a 20 kg stone at 17 m/s
	# (TP 17, ratio 0.82) only bends it; Cyclone 11 x2 = 22 (ratio 1.29) turns it back along the push.
	var th := threat(h.w, "stone", 17.0, 20.0, "K")
	var s0 := _a_vs_r(12.0)
	var a0: ActorState = s0[0]
	var p0 := Interactions.predict(h.w, th, Agent.of_move(h.w, a0, "air_attack", 0, false))
	check(p0.outcome == "bend", "palm gust T0 vs TP 17: bend (%s r%.2f)" % [p0.outcome, p0.ratio])
	var p1 := Interactions.predict(h.w, th, Agent.of_move(h.w, a0, "air_attack", 1, false))
	check(p1.outcome == "redirect", "cyclone T1 vs TP 17: redirect (%s r%.2f)" % [p1.outcome, p1.ratio])
	var fast := threat(h.w, "stone", 43.4, 29.0, "K")
	var pf := Interactions.predict(h.w, fast, Agent.of_move(h.w, a0, "air_attack", 0, false))
	check(pf.outcome == "pass", "a 29 kg stone at 30 m/s (TP 43) ignores a palm gust (%s r%.2f)" % [pf.outcome, pf.ratio])
	# In play: the cyclone push turns the stone and re-owns it.
	var s := _a_vs_r(12.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var holder := {}
	var spawn := func() -> void:
		holder["stone"] = h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 4.0)
		holder["v0"] = holder.stone.vel
	run_when(a, "air_attack", 1, spawn, 28, 30)
	var stone: MatBody = holder.stone
	var ix := h.events("deflect")
	check(not ix.is_empty() and ix[0].get("verb", "") == "gust", "the cyclone turned it")
	check(stone.alive and stone.attack_owner == a.id, "turned and re-owned, not stopped")
	check(a.health == 100.0, "it never reached A")


func test_gale_and_hurricane_deflect_a_stone_heavy_stones_only_bend() -> void:
	for tier in [2, 3]:
		var s := _a_vs_r(12.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		var holder := {}
		var spawn := func() -> void:
			holder["stone"] = h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 3.0)
		run_when(a, "air_attack", tier, spawn, 28, 12)
		check(h.has_event("deflect") and holder.stone.attack_id == 0, "T%d deflects a 20 kg stone (x1.5)" % tier)
		var s2 := _a_vs_r(12.0)
		var a2: ActorState = s2[0]
		var r2: ActorState = s2[1]
		var holder2 := {}
		var spawn2 := func() -> void:
			holder2["stone"] = h.launch_at(a2, "stone", 45.0, 14.0, Sim.AMBIENT_C, "", r2, 5.5)
			holder2["v"] = holder2.stone.vel
		run_when(a2, "air_attack", tier, spawn2, 28, 6)
		var heavy: MatBody = holder2.stone
		check(h.has_event("bend"), "T%d: a 45 kg stone (31.5) only bends (x0.6)" % tier)
		check(heavy.alive and heavy.attack_id != 0 and heavy.vel.length() > (holder2.v as Vector3).length() * 0.6, "it keeps coming")


func test_a_gale_puts_out_a_flame_a_palm_gust_only_turns_it() -> void:
	# fire bands (flame x gust) at every tier: < 1 fan, 1-2 blow aside, >= 2 extinguish.
	var s := _a_vs_r(12.0)
	var a: ActorState = s[0]
	var th := threat(h.w, "flame", 8.0, 0.0, "H")
	var cp := Agent.of_move(h.w, a, "air_attack", 2, false)
	var pr := Interactions.predict(h.w, th, cp)
	check(pr.outcome == "extinguish", "Gale (18) vs a blaze (8): ratio %.2f -> %s" % [pr.ratio, pr.outcome])
	var weak := threat(h.w, "flame", 30.0, 0.0, "H")
	pr = Interactions.predict(h.w, weak, cp)
	check(pr.outcome == "amplify", "against 30 PU of fire the wind only fans it (%s)" % pr.outcome)
	var mid := threat(h.w, "flame", 12.0, 0.0, "H")
	pr = Interactions.predict(h.w, mid, cp)
	check(pr.outcome == "deflect", "ratio 1.5: blown aside (%s)" % pr.outcome)
	var t0 := Agent.of_move(h.w, a, "air_attack", 0, false)
	# The fire bands apply at every tier (weak wind feeds fire): a palm gust (7) fans a Sunfall-size fireball (19).
	check(not Interactions.rule(&"flame", &"gust", 0).get("legacy", false), "T0 flame x gust is the kit's fire band too")
	check(not Interactions.rule(&"flame", &"gust", 2).get("legacy", false), "T2 is the kit's")
	check(t0.power == 7.0, "palm gust CP 7")
	var big := threat(h.w, "flame", 19.0, 0.0, "H")
	pr = Interactions.predict(h.w, big, t0)
	check(pr.outcome == "amplify", "palm gust vs a 19 PU fireball: fanned (%s r%.2f)" % [pr.outcome, pr.ratio])
	var flare := threat(h.w, "flame", 3.0, 0.0, "H")
	pr = Interactions.predict(h.w, flare, t0)
	check(pr.outcome == "extinguish", "palm gust vs a flare (3): put out (%s)" % pr.outcome)


# ---------------------------------------------------------------- the new moves, T0-T3

func test_every_gust_move_has_a_full_def_and_a_binding() -> void:
	Moves.ensure()
	var clips: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://assets/characters/fighter_clips.json"))
	for id in GUST_MOVES:
		check(Moves.DEFS.has(id), "%s registered" % id)
		if not Moves.DEFS.has(id):
			continue
		var d: Dictionary = Moves.DEFS[id]
		for k in ["name", "desc", "slot", "sub", "element", "anim", "fx", "ai"]:
			check(d.has(k) or id == "air_attack" and k == "anim" or id == "air_tech", "%s has %s" % [id, k])
		if id == "air_attack":
			for k in ["tiers", "counter", "threat"]:
				check(d.has(k), "%s has %s" % [id, k])
			continue
		check(int(d.sub) == 0 and int(d.element) == 3, "%s is Air/Gust" % id)
		check(d.has("startup") and d.has("recovery") and d.has("cost"), "%s has frames and a cost" % id)
		check(d.has("tiers") or id in ["gust_wall", "gust_downdraft", "gust_tailwind", "gust_grip"], "%s has tier data" % id)
		check(d.has("counter") or d.has("threat") or id == "gust_tailwind", "%s has counter / threat metadata" % id)
		for ck in ["anim", "anim_active", "anim_hold"]:
			if d.has(ck):
				check(clips.has(String(d[ck])), "%s: clip %s exists" % [id, d[ck]])
	for slot in ["strike", "thrust", "ground", "sweep", "guard", "push", "sink", "tech", "evade", "evade_hold"]:
		check(Moves.resolve(3, 0, slot) != "", "Air/Gust slot %s bound (%s)" % [slot, Moves.resolve(3, 0, slot)])
	check(Moves.resolve(3, 0, "strike") == "air_attack" and Moves.resolve(3, 0, "tech") == "air_tech" and Moves.resolve(3, 0, "evade") == "air_dash",
		"the legacy strike, technique and evade keep their ids")
	check(Moves.resolve(3, 0, "guard") == "guard", "the Wind Guard keeps the id guard")
	check(Moves.resolve(3, 0, "thrust") == "gust_crescent" and Moves.resolve(3, 0, "ground") == "gust_dust_line", "thrust / ground bound")


func test_wind_crescent_t0_to_t3() -> void:
	var counts := [1, 2, 1, 1]
	var powers := [8.0, 11.0, 15.0, 22.0]
	for tier in 4:
		var s := _a_vs_r(14.0)
		var a: ActorState = s[0]
		var inst := run_move(a, "gust_crescent", tier, 1, 13)
		var cr := bodies_tagged("crescent")
		check(cr.size() == counts[tier], "T%d: %d crescent(s) (%d)" % [tier, counts[tier], cr.size()])
		for b in cr:
			check(b.mat == Sim.Mat.AIR and b.tier == tier and absf(b.power - powers[tier]) < 1e-6, "T%d: AIR body, tier %d, P %.0f (%.1f)" % [tier, b.tier, powers[tier], b.power])
			check(b.attack_owner == a.id and b.attack_id != 0, "armed with its own attack id")
			near(b.vel.length(), 22.0, 0.6, "speed 22 m/s")
			check(b.radius >= (1.0 if tier >= 2 else 0.5) - 1e-6, "T%d width radius %.1f" % [tier, b.radius])
		if tier == 3 and not cr.is_empty():
			check(int(cr[0].props.get("pierce", 0)) == 2, "the Wind Scythe pierces 2 targets")
		if tier == 1 and cr.size() == 2:
			check(cr[0].vel.normalized().dot(cr[1].vel.normalized()) < 0.9999, "the pair crosses (different headings)")
		h.step(80)
		check(a.action == null, "T%d ends cleanly" % tier)
		fx_catalogued("crescent T%d" % tier)


func test_a_crescent_deflects_a_light_shot_and_cuts_vines() -> void:
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var stone := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 8.0)   # meets the crescent mid-way
	run_move(a, "gust_crescent", 2, 1, 0)
	h.until(func(): return h.has_event("interaction"), 90)
	var ix := h.events("interaction").filter(func(e): return String(e.counter) == "crescent")
	check(not ix.is_empty(), "the crescent met the stone through the rules")
	check(ix.size() > 0 and ["deflect", "bend"].has(String(ix[0].outcome)), "outcome %s" % [ix[0].outcome if ix.size() > 0 else "-"])
	check(stone.attack_id == 0 or stone.vel.dot(Vector3(0, 0, 1)) < 17.0 * 0.9, "the stone is no longer a clean hit on A")
	# vines
	var s2 := _a_vs_r(14.0)
	var a2: ActorState = s2[0]
	var base := snap(h.w)
	var vine := h.w.spawn_body(Sim.Mat.PLANT, Sim.Form.CHUNK, 10.0, Vector3(0, 1.2, a2.pos.z - 5.0), "test")
	h.w.mass_ledger.plant_from_ground += 10.0
	vine.static_body = false
	vine.gravity_scale = 0.0
	base = snap(h.w)
	run_move(a2, "gust_crescent", 0, 1, 0)
	h.until(func(): return h.has_event("transform", "to", "cut"), 90)
	check(h.has_event("transform", "to", "cut"), "the crescent cut the vine")
	h.step(5)
	ledgers_ok(base, "vine cut", 1e-5)


# ---------------------------------------------------------------- dust line, crosswind, wall of wind, downdraft

func test_dust_devil_line_is_a_tripping_wind_wave() -> void:
	var widths := [1.6, 1.9, 2.4, 3.0]
	for tier in 4:
		var s := _a_vs_r(12.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		run_move(a, "gust_dust_line", tier, 1, 14)
		var ws := bodies_tagged("dust_line")
		check(ws.size() == 1 and ws[0].form == Sim.Form.WAVE and ws[0].mat == Sim.Mat.AIR, "T%d: one AIR wave" % tier)
		if ws.size() == 1:
			near(ws[0].wave_width, widths[tier], 1e-6, "T%d width" % tier)
			near(float(ws[0].props.speed), 14.0, 1e-6, "14 m/s")
		h.step(120)
		if tier == 0:
			check(r.health < 100.0 or r.balance < 100.0, "the line reaches the rival: trips (balance %.0f)" % r.balance)
			check(Status.has(r, "blinded") or h.has_event("status", "status", "blinded"), "and raises dust (light blind)")
		check(a.action == null, "T%d ends cleanly" % tier)
	fx_catalogued("dust line")


func test_crosswind_curves_a_projectile_in_flight() -> void:
	var s := _a_vs_r(12.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var stone := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 3.5)
	var dir0 := stone.vel.normalized()
	run_move(a, "gust_crosswind", 1, 1, 0)
	h.until(func(): return h.has_event("bend") or h.has_event("interaction"), 40)
	var ang := rad_to_deg(dir0.angle_to(stone.vel.normalized()))
	check(h.events("interaction").any(func(e): return String(e.counter) == "crosswind"), "met through the rules as crosswind")
	check(ang > 3.0 and ang <= 40.0 + 1e-3, "curved by %.1f deg (<= 40)" % ang)
	check(stone.alive and stone.attack_id != 0, "still flying")


func test_wall_of_wind_and_downdraft_from_the_guard() -> void:
	var s := _a_vs_r(10.0)
	var a: ActorState = s[0]
	h.press(a, "guard")
	h.step(20)
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step(3)
	check(a.action != null and a.action.id == "gust_wall", "guard flick up = Wall of Wind")
	h.step(14)
	var ws := bodies_tagged("wind_wall")
	check(ws.size() == 1 and ws[0].form == Sim.Form.WAVE, "a moving wall (wave tag wind_wall)")
	if ws.size() == 1:
		check(ws[0].wave_width == 3.0 and absf(float(ws[0].props.speed) - 8.0) < 1e-6 and absf(ws[0].power - 14.0) < 1e-6, "3 m wide, 8 m/s, P 14")
	h.release(a, "guard")
	h.step(80)
	check(a.action == null, "the wall move ends")
	# downdraft
	s = _a_vs_r(10.0)
	a = s[0]
	h.press(a, "guard")
	h.step(20)
	h.flick(a, "guard", Sim.Gesture.DOWN)
	h.step(3)
	check(a.action != null and a.action.id == "gust_downdraft", "guard flick down = Downdraft")
	h.release(a, "guard")
	h.step(60)
	check(a.action == null, "Downdraft ends")
	fx_catalogued("wall / downdraft")


func test_downdraft_slams_an_airborne_enemy() -> void:
	var s := _a_vs_r(3.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = false
	r.pos.y = 2.0
	r.grounded = false
	r.vel.y = 3.0
	var b0 := r.balance
	run_move(a, "gust_downdraft", 0, 1, 0)
	h.step(12)
	check(r.vel.y < -5.0 or r.pos.y < 1.0, "the airborne enemy is driven down (vy %.1f)" % r.vel.y)
	check(r.balance < b0, "and loses balance")


func test_tailwind_runs_faster_while_held() -> void:
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	h.it(a).move = Vector3(0, 0, -1)
	h.step(40)
	var v0 := Vector2(a.vel.x, a.vel.z).length()
	h.press(a, "evade")
	h.it(a).evade_held = true
	h.step(16)
	check(a.action != null and a.action.id == "gust_tailwind" and a.action.slot == "evade_hold", "held 0.2 s: Tailwind")
	h.step(40)
	var v1 := Vector2(a.vel.x, a.vel.z).length()
	check(v1 > v0 * 1.1, "x1.3 run speed (%.2f -> %.2f)" % [v0, v1])
	h.it(a).evade_held = false
	h.it(a).move = Vector3.ZERO
	h.step(30)
	check(a.action == null or a.action.phase == ActionInst.P.RECOVERY, "released: the wind drops")


# ---------------------------------------------------------------- Wind Grip (context technique)

func test_updraft_stays_the_legacy_updraft_with_nothing_to_grip() -> void:
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	h.press(a, "tech")
	h.step(30)
	check(a.action != null and a.action.id == "air_tech" and h.has_event("updraft"), "no light body: the legacy updraft")
	check(not a.grounded or a.pos.y > 0.1, "lifted")
	var pv := h.w.tech_preview(a, Vector3(0, 0, -1))
	check(pv.get("mode", "") != "WIND GRIP", "preview: %s" % pv.get("mode", ""))


func test_wind_grip_takes_a_light_stone_and_flings_it() -> void:
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var stone := h.launch_at(a, "stone", 20.0, 12.0, Sim.AMBIENT_C, "", r, 7.0)
	var pv := h.w.tech_preview(a, Vector3(0, 0, -1))
	check(pv.get("mode", "") == "WIND GRIP" and int(pv.get("body", -1)) == stone.id, "preview: WIND GRIP on the stone")
	h.press(a, "tech")
	h.step(1)
	check(a.action != null and a.action.id == "gust_grip" and h.has_event("morph"), "the technique morphs into Wind Grip")
	h.until(func(): return h.w.held(a) != null, 40)
	check(h.w.held(a) == stone, "the light stone is gripped")
	h.aim(a, Vector3(0, 0, -1))
	h.release(a, "tech")
	h.step(3)
	check(h.events("launch").any(func(e): return int(e.body) == stone.id and int(e.actor) == a.id), "released: flung as A's attack")
	h.step(40)
	var heavy := h.launch_at(a, "stone", 45.0, 3.0, Sim.AMBIENT_C, "", r, 6.0)
	check(not AirGust.grip_target(h.w, a, Vector3(0, 0, -1)) == heavy, "a 45 kg stone is not a Wind Grip target")


func test_a_gripped_fireball_grows_20_percent() -> void:
	var s := _a_vs_r(14.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	var ball := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 0.5, Vector3(0, 1.2, a.pos.z - 5.0), "test")
	ball.heat_payload = 100.0
	ball.tag = &"fireball"
	ball.attack_id = h.w.new_attack_id()
	ball.attack_owner = r.id
	ball.vel = Vector3(0, 0, 8.0)
	ball.gravity_scale = 0.0
	ball.hit_set[r.id] = true
	h.w.ledger.generated += 100.0
	base = snap(h.w)
	h.press(a, "tech")
	h.until(func(): return h.w.held(a) != null, 40)
	check(h.w.held(a) == ball, "the fireball is gripped (light, <= 30 kg)")
	h.step(4)
	check(ball.heat_payload > 100.0 * 1.15 or ball.props.get("fed", false), "fed +20 %% (%.0f HU)" % ball.heat_payload)
	h.release(a, "tech")
	h.step(20)
	ledgers_ok(base, "fed fireball", 1e-4)


# ---------------------------------------------------------------- determinism

func test_gust_kit_is_deterministic() -> void:
	var hashes := []
	for k in 2:
		var s := _a_vs_r(8.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 6.0)
		run_move(a, "gust_crescent", 1, 1, 20)
		run_move(a, "air_attack", 2, 30, 40)
		run_move(a, "gust_crosswind", 2, 1, 40)
		hashes.append("%s|%s|%d|%.4f|%.4f" % [str(a.pos), str(r.pos), h.w.bodies.size(), a.focus, r.health])
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: %s vs %s" % [hashes[0], hashes[1]])
