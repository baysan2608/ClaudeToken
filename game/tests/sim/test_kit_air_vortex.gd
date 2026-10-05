extends AirKitTest
## Air / Vortex (sub 1): Twister / Tornado / Cyclone Fortress (a walking ZONE that captures, orbits and lifts),
## infusions and the neutral rule, Spiral Lance, Dust Funnel, Eddy Ring, Vortex Wall + Catch, Unleash, Funnel Down,
## Eye of the Storm (steer, contest), Spin Step, Whirl Lift, the Vortex column of the counter matrix.

const MOVES := ["vortex_twister", "vortex_spiral", "vortex_funnel", "vortex_eddy", "vortex_wall", "vortex_unleash", "vortex_funnel_down",
	"vortex_eye", "vortex_spin_step", "vortex_whirl"]


func _duel(dist: float = 10.0) -> Array:
	var s := duel(1, Sim.Element.EARTH, 3, dist)
	return s


## A tornado zone for rule-level tests (the move makes the same body): tag tornado at p, owner A.
func _tornado(owner: ActorState, p: Vector3, radius: float = 2.5, power: float = 25.0, tier: int = 2) -> MatBody:
	var z := h.spawn_zone("tornado", p, radius, owner, power)
	z.tier = tier
	z.sub = 1
	z.props["height"] = 5.0
	z.props["dps"] = 2.0
	return z


func test_every_vortex_move_has_a_def_and_a_binding() -> void:
	Moves.ensure()
	var clips: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://assets/characters/fighter_clips.json"))
	for id in MOVES:
		check(Moves.DEFS.has(id), "%s registered" % id)
		if not Moves.DEFS.has(id):
			continue
		var d: Dictionary = Moves.DEFS[id]
		for k in ["name", "desc", "slot", "sub", "element", "startup", "recovery", "cost", "anim", "fx", "ai"]:
			check(d.has(k), "%s has %s" % [id, k])
		check(int(d.sub) == 1 and int(d.element) == 3, "%s is Air/Vortex" % id)
		for ck in ["anim", "anim_active", "anim_hold"]:
			if d.has(ck):
				check(clips.has(String(d[ck])), "%s: clip %s exists" % [id, d[ck]])
		if ["strike", "thrust", "ground", "sweep", "tech"].has(String(d.slot)):
			check(d.has("tiers") and (d.tiers as Dictionary).has("t3"), "%s has tiers up to t3" % id)
			check(d.has("counter") and d.has("threat"), "%s has counter + threat" % id)
		check(Moves.slot_of(3, 1, id) == String(d.slot), "%s bound to %s" % [id, d.slot])
	for slot in Sim.SLOTS:
		check(Moves.resolve(3, 1, slot) != "" and int(Moves.DEFS[Moves.resolve(3, 1, slot)].get("sub", 0)) == 1, "Air/Vortex %s bound (%s)" % [slot, Moves.resolve(3, 1, slot)])


func test_twister_tornado_fortress_tiers() -> void:
	var want := [[1, 8.0], [2, 12.0]]
	for tier in 2:
		var s := _duel(14.0)
		var a: ActorState = s[0]
		run_move(a, "vortex_twister", tier, 1 if tier == 0 else 30, 13)
		var bs := bodies_tagged("twister")
		check(bs.size() == want[tier][0], "T%d: %d twister(s) (%d)" % [tier, want[tier][0], bs.size()])
		for b in bs:
			check(b.mat == Sim.Mat.AIR and absf(b.power - want[tier][1]) < 1e-6 and b.attack_id != 0, "T%d: AIR twister P %.0f" % [tier, want[tier][1]])
			near(b.vel.length(), 12.0, 0.6, "12 m/s")
		h.step(120)
		check(a.action == null, "T%d ends cleanly" % tier)
	var zw := [[2.5, 4.0, 25.0], [4.0, 6.0, 35.0]]
	for k in 2:
		var s2 := _duel(14.0)
		var a2: ActorState = s2[0]
		run_move(a2, "vortex_twister", 2 + k, 30, 13)
		var zs := zones_tagged("tornado")
		check(zs.size() == 1, "T%d: one tornado zone" % (2 + k))
		if zs.size() == 1:
			near(zs[0].zone_radius, zw[k][0], 1e-6, "T%d radius" % (2 + k))
			near(zs[0].max_life, zw[k][1], 0.3, "T%d lives %.0f s" % [2 + k, zw[k][1]])
			near(zs[0].power, zw[k][2], 1e-6, "T%d power" % (2 + k))
			check(zs[0].owner == a2.id and float(zs[0].props.get("walk_speed", 0.0)) == 4.0, "owned, walks 4 m/s")
		fx_catalogued("tornado T%d" % (2 + k))
		h.step(80)
		check(a2.action == null, "T%d ends cleanly" % (2 + k))
		h.step(420)
		check(zones_tagged("tornado").is_empty(), "the tornado ends on its own")


func test_a_tornado_walks_to_the_target_and_lifts_the_fighter() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	run_move(a, "vortex_twister", 2, 30, 13)
	var z: MatBody = zones_tagged("tornado")[0]
	var d0 := Vector2(z.pos.x - r.pos.x, z.pos.z - r.pos.z).length()
	h.step(60)
	var d1 := Vector2(z.pos.x - r.pos.x, z.pos.z - r.pos.z).length()
	check(d0 - d1 > 3.0 and d0 - d1 < 5.0, "walks ~4 m/s toward the rival (%.1f -> %.1f m)" % [d0, d1])
	h.step(150)
	var peak := 0.0
	for k in 120:
		h.step()
		peak = maxf(peak, r.pos.y)
	check(peak > 1.0, "the rival is lifted (peak %.2f m)" % peak)
	check(r.health < 100.0, "and hurt (%.1f)" % r.health)
	check(a.health == 100.0, "the caster's own tornado spares them")


func test_a_tornado_captures_a_light_stone_and_slows_a_heavy_one() -> void:
	var s := _duel(14.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var z := _tornado(a, Vector3(0, 0, a.pos.z - 4.0))
	var light := h.launch_at(a, "stone", 20.0, 10.0, Sim.AMBIENT_C, "", r, 6.0)
	h.step(40)
	check(light.captured_by == z.id, "a 20 kg shot is captured")
	check(h.has_event("capture", "body", light.id), "capture event")
	check(light.attack_id == 0, "and is harmless inside")
	h.step(60)
	check(light.alive and light.pos.distance_to(z.pos) < z.zone_radius + 1.0, "it orbits in the zone (%.1f m)" % light.pos.distance_to(z.pos))
	# heavy: 45 kg only slowed / bent
	var heavy := h.launch_at(a, "stone", 45.0, 12.0, Sim.AMBIENT_C, "", r, 6.0)
	h.step(30)
	check(heavy.captured_by != z.id, "a 45 kg stone is not captured")
	check(h.events("interaction").any(func(e): return String(e.threat) == "stone_heavy" and String(e.counter) == "tornado" and e.outcome == "slow"), "it is slowed and bent")
	var boulder := h.launch_at(a, "stone", 200.0, 9.0, Sim.AMBIENT_C, "", r, 7.0)
	h.step(30)
	check(boulder.captured_by != z.id and boulder.alive, "a boulder ignores the tornado")


func test_infusions_sand_fire_water_steam() -> void:
	var cases := [["sand", Sim.Mat.SAND, "blinded"], ["fire", Sim.Mat.FIRE, "burning"], ["water", Sim.Mat.WATER, "wet"], ["steam", Sim.Mat.STEAM, "scalded"]]
	for cs in cases:
		var s := _duel(10.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		var z := _tornado(a, Vector3(r.pos.x, 0, r.pos.z + 0.5))
		var b := h.w.spawn_body(cs[1], Sim.Form.CHUNK, 2.0 if cs[1] != Sim.Mat.FIRE else 0.5, z.pos + Vector3(0.6, 1.0, 0), "test")
		if cs[1] == Sim.Mat.SAND:
			h.w.mass_ledger.ground_taken += 2.0
		elif cs[1] == Sim.Mat.FIRE:
			b.heat_payload = 400.0
			h.w.ledger.generated += 400.0
		elif cs[1] == Sim.Mat.STEAM:
			b.mass = 0.5
			b.update_radius()
			h.w.mass_ledger.vapor += 0.0
		b.attack_id = 0
		b.gravity_scale = 0.0
		h.step(30)
		check(b.captured_by == z.id, "%s: captured" % cs[0])
		check(String(z.props.get("infused", "")).contains(String(cs[0])), "%s: the tornado is infused (%s)" % [cs[0], z.props.get("infused", "")])
		check(Status.has(r, String(cs[2])), "%s: the rival inside gets %s" % [cs[0], cs[2]])
		if cs[0] == "water":
			check(Materials.conducts(z), "a water tornado conducts (lightning node)")
		check(z.owner == a.id, "own material: still A's tornado")


func test_a_tornado_infused_by_the_enemys_fire_turns_neutral() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var z := _tornado(a, Vector3(0, 0, a.pos.z - 3.0))
	var ball := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 0.5, z.pos + Vector3(0.5, 1.0, 0), "test")
	ball.heat_payload = 400.0
	h.w.ledger.generated += 400.0
	ball.attack_id = h.w.new_attack_id()
	ball.attack_owner = r.id
	ball.hit_set[r.id] = true
	ball.vel = Vector3(0, 0, 2.0)
	ball.gravity_scale = 0.0
	h.step(30)
	check(ball.captured_by == z.id, "the rival's fireball is caught")
	check(z.owner == -1 and z.props.get("neutral", false), "infused by the enemy: neutral (owner -1)")
	check(h.has_event("infuse", "neutral", true), "infuse event (neutral)")
	# neutral: it hurts both - the former owner standing inside is no longer spared
	a.pos = Vector3(z.pos.x + 0.5, 0, z.pos.z)
	var hp := a.health
	h.step(60)
	check(a.health < hp, "the neutral whirl hurts its own caster (%.1f -> %.1f)" % [hp, a.health])


func test_cyclone_fortress_sets_a_lava_wave_and_a_tornado_only_crusts_it() -> void:
	# 20 kg wave (27.3): Tornado T2 (25) 0.92 partial: crust + spatter (magma vortex); Fortress T3 (35) 1.28: sets it into rock.
	for case in [[25.0, 2.5, "partial"], [35.0, 4.0, "full"]]:
		var s := _duel(14.0)
		var a: ActorState = s[0]
		var z := _tornado(a, Vector3(0, 0, a.pos.z - 6.0), case[1], case[0], 2 if case[2] == "partial" else 3)
		var wave := lava_wave(20.0, Vector3(0, 0, a.pos.z - 9.0))
		var base := snap(h.w)
		var liquid0 := wave.liquid
		h.step(14)
		var ix := h.events("interaction").filter(func(e): return String(e.threat) == "lava_wave" and String(e.counter) == "tornado")
		check(not ix.is_empty(), "%s: the wave met the tornado through the rules" % case[2])
		if case[2] == "partial":
			check(wave.alive and wave.form == Sim.Form.WAVE and wave.liquid < liquid0 - 0.04 and wave.liquid > 0.5, "Tornado T2: crusts it (liquid %.2f), it is not set" % wave.liquid)
			check(wave.wave_budget < 6.0, "its reach collapses (budget %.1f of 12 m): the partial removed CP_eff from its power" % wave.wave_budget)
			check(String(z.props.get("infused", "")).contains("magma") or float(z.props.get("spatter_until", -1.0)) > 0.0, "and picks up spatter: a magma vortex")
		else:
			check(wave.form != Sim.Form.WAVE and wave.liquid <= 0.0, "Cyclone Fortress sets the wave into rock (%s)" % Sim.FORM_NAMES[wave.form])
		ledgers_ok(base, "lava vs tornado", 1e-4)


func test_vortex_wall_captures_a_volley_and_unleash_returns_it_as_your_attack() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	h.press(a, "guard")
	h.step(20)
	var walls := zones_tagged("vortex_wall")
	check(walls.size() == 1 and walls[0].owner == a.id, "the Vortex Wall is a zone around the guard")
	var base := snap(h.w)
	var stones: Array[MatBody] = []
	for k in 2:
		stones.append(h.launch_at(a, "stone", 20.0, 14.0, Sim.AMBIENT_C, "", r, 6.0 + 1.5 * k))
	h.step(45)
	check(stones[0].captured_by == walls[0].id and stones[1].captured_by == walls[0].id, "both shots are captured, not stopped")
	check(a.health == 100.0, "nothing reached A")
	var ids := [stones[0].attack_id, stones[1].attack_id]
	h.log.clear()
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step(3)
	check(a.action != null and a.action.id == "vortex_unleash", "guard flick up = Unleash")
	h.release(a, "guard")
	h.step(30)
	var un := h.events("unleash")
	check(un.size() == 2, "both bodies unleashed (%d)" % un.size())
	for b in stones:
		check(b.attack_owner == a.id and b.attack_id != 0, "#%d flies as A's attack" % b.id)
		check(not ids.has(b.attack_id), "with a NEW attack id")
		check(b.vel.dot(r.pos - b.pos) > 0.0 or b.attack_id == 0, "toward the rival")
	check(zones_tagged("vortex_wall").is_empty(), "the wall is spent")
	h.step(80)
	ledgers_ok(base, "unleash", 1e-5)
	check(r.health < 100.0, "the returned volley hurts R (%.1f)" % r.health)


func test_vortex_catch_takes_a_heavier_shot_only_when_perfect() -> void:
	# a 40 kg stone: Vortex Wall alone only slows it; a perfect Wall (Vortex Catch) catches shots up to 45 kg
	for perfect in [false, true]:
		var s := _duel(12.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		if not perfect:
			h.press(a, "guard")
			h.step(40)
		var holder := {}
		if perfect:
			# spawn first, press the guard 4 ticks before it reaches the wall's edge
			holder["stone"] = h.launch_at(a, "stone", 40.0, 12.0, Sim.AMBIENT_C, "", r, 4.8)
			h.step(4)
			h.press(a, "guard")
		else:
			holder["stone"] = h.launch_at(a, "stone", 40.0, 12.0, Sim.AMBIENT_C, "", r, 6.0)
		h.step(40)
		var st: MatBody = holder.stone
		var walls := zones_tagged("vortex_wall")
		var caught := not walls.is_empty() and st.captured_by == walls[0].id
		if perfect:
			check(caught, "Vortex Catch (perfect) catches a 40 kg stone")
			check(h.has_event("vortex_catch"), "vortex_catch event")
		else:
			check(not caught, "a plain Vortex Wall does not catch a 40 kg stone")


func test_funnel_down_drops_the_captured_at_your_feet() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	h.press(a, "guard")
	h.step(20)
	var st := h.launch_at(a, "stone", 20.0, 14.0, Sim.AMBIENT_C, "", r, 6.0)
	h.step(40)
	check(st.captured_by >= 0, "captured")
	h.flick(a, "guard", Sim.Gesture.DOWN)
	h.step(3)
	check(a.action != null and a.action.id == "vortex_funnel_down", "guard flick down = Funnel Down")
	h.step(25)
	check(st.captured_by < 0 and st.attack_id == 0 and st.pos.distance_to(a.pos) < 2.5, "dropped at the caster's feet, harmless (%.1f m)" % st.pos.distance_to(a.pos))
	check(zones_tagged("vortex_wall").is_empty(), "the wall is gone")


func test_dust_funnel_turns_rubble_into_ammunition() -> void:
	var s := _duel(14.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	var rub: Array[MatBody] = []
	for k in 2:
		var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 6.0, Vector3(0.4 * k - 0.2, 0.2, a.pos.z - 4.0 - 2.0 * k), "test")
		h.w.mass_ledger.ground_taken += 6.0
		b.on_ground = true
		rub.append(b)
	base = snap(h.w)
	run_move(a, "vortex_funnel", 1, 1, 14)
	var f := bodies_tagged("funnel")
	check(f.size() == 1 and f[0].form == Sim.Form.WAVE and f[0].mat == Sim.Mat.AIR, "the funnel is an AIR wave (tag funnel)")
	h.step(40)
	check(rub[0].captured_by >= 0 or rub[1].captured_by >= 0, "it gathers the loose rubble on its path")
	h.step(140)
	var flung := rub.filter(func(b): return h.has_event("release_captured", "body", b.id))
	check(flung.size() >= 1, "the carried rubble is released at the end (%d)" % flung.size())
	ledgers_ok(base, "funnel", 1e-5)


func test_spiral_lance_tiers_and_class() -> void:
	var pierce := [0, 0, 1, 2]
	var power := [10.0, 14.0, 18.0, 24.0]
	for tier in 4:
		var s := _duel(14.0)
		var a: ActorState = s[0]
		run_move(a, "vortex_spiral", tier, 1 if tier == 0 else 30, 13)
		var bs := bodies_tagged("spiral")
		check(bs.size() == 1 and absf(bs[0].power - power[tier]) < 1e-6, "T%d: a spiral P %.0f" % [tier, power[tier]])
		if bs.size() == 1:
			check(int(bs[0].props.get("pierce", 0)) == pierce[tier], "T%d pierce %d" % [tier, pierce[tier]])
			check(Interactions.classify(bs[0]) == &"spiral", "its own threat class")
			near(bs[0].vel.length(), 28.0 if tier == 3 else 24.0, 0.8, "speed")
		h.step(90)
		check(a.action == null, "T%d ends cleanly" % tier)
	# pierces mist and sand clouds and sand walls: cells are 'pass' / drill


func test_eddy_ring_curves_projectiles_and_pushes_fighters_out() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var holder := {}
	var spawn := func() -> void:
		holder["stone"] = h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 7.0)
	run_when(a, "vortex_eddy", 2, spawn, 28, 0)
	h.until(func(): return h.has_event("interaction"), 80)
	var ix := h.events("interaction").filter(func(e): return String(e.counter) == "eddy")
	check(not ix.is_empty() and ["bend", "deflect", "reflect"].has(String(ix[0].outcome)), "the stone is curved around the ring: %s" % [ix[0].outcome if not ix.is_empty() else "-"])
	check(a.health == 100.0, "A is untouched")
	# a rival inside the ring is pushed out
	var s2 := _duel(10.0)
	var a2: ActorState = s2[0]
	var r2: ActorState = s2[1]
	r2.pos = a2.pos + Vector3(0, 0, -1.2)
	var d0 := r2.pos.distance_to(a2.pos)
	run_move(a2, "vortex_eddy", 0, 1, 30)
	check(r2.pos.distance_to(a2.pos) > d0 + 0.3, "a light rival is pushed out (%.1f -> %.1f m)" % [d0, r2.pos.distance_to(a2.pos)])


func test_eye_of_the_storm_summons_steers_and_lingers() -> void:
	var s := _duel(14.0)
	var a: ActorState = s[0]
	h.press(a, "tech")
	h.step(40)
	var zs := zones_tagged("tornado")
	check(zs.size() == 1 and zs[0].owner == a.id, "the Eye makes a tornado at the aim point")
	if zs.size() == 1:
		var z := zs[0]
		var dist := Vector2(z.pos.x - a.pos.x, z.pos.z - a.pos.z).length()
		check(dist > 7.0 and dist <= 10.5, "~10 m away (%.1f)" % dist)
		check(z.max_life < 0.0, "kept alive while held")
		# steer: aim to the side
		var x0 := z.pos.x
		h.aim(a, Vector3(1, 0, -0.2))
		h.step(60)
		check(z.pos.x > x0 + 0.5, "steered by the aim (%.1f -> %.1f)" % [x0, z.pos.x])
		h.release(a, "tech")
		h.step(5)
		check(z.max_life > 0.0 and z.max_life - z.age < 2.2, "released: lives about 2 s more")
		h.step(200)
		check(not z.alive, "and then it is gone")
	check(a.action == null, "ends cleanly")


func test_eye_of_the_storm_takes_over_an_enemy_tornado() -> void:
	var s := _duel(14.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var z := h.spawn_zone("tornado", Vector3(0, 0, a.pos.z - 9.0), 2.5, r, 25.0)
	z.props["height"] = 5.0
	z.tier = 2
	h.aim(a, Vector3(0, 0, -1))
	h.press(a, "tech")
	h.step(40)
	check(z.owner == a.id, "contested: the Eye (25) takes the enemy tornado (25) over")
	check(h.has_event("tornado_taken"), "event tornado_taken")
	h.release(a, "tech")
	h.step(30)


func test_two_tornadoes_meet_and_the_stronger_absorbs_the_weaker() -> void:
	var s := _duel(14.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var big := _tornado(a, Vector3(0, 0, 0), 4.0, 35.0, 3)
	var small := h.spawn_zone("tornado", Vector3(1.0, 0, 0), 2.5, r, 25.0)
	small.props["height"] = 5.0
	h.step(20)
	check(not small.alive and big.alive, "the Fortress absorbs the smaller tornado")
	check(big.power > 35.0, "and keeps part of its power (%.1f)" % big.power)
	check(h.has_event("tornado_contest"), "contest event")


func test_spin_step_deflects_a_light_shot_and_whirl_lift_hovers() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var stone := h.launch_at(a, "stone", 20.0, 14.0, Sim.AMBIENT_C, "", r, 4.5)
	h.it(a).move = Vector3(1, 0, 0)
	h.press(a, "evade")
	h.step(30)
	check(a.health == 100.0, "Spin Step: A is not hit (i-frames + spin)")
	check(a.action == null or a.action.id == "vortex_spin_step", "the evade is the Spin Step")
	check(h.has_event("evade", "move", "vortex_spin_step"), "evade event")
	# Whirl Lift
	var s2 := _duel(10.0)
	var a2: ActorState = s2[0]
	h.press(a2, "evade")
	h.it(a2).evade_held = true
	h.step(16)
	check(a2.action != null and a2.action.id == "vortex_whirl", "held 0.2 s: Whirl Lift")
	h.step(60)
	check(a2.pos.y > 1.2 and a2.pos.y < 1.8, "hovers about 1.5 m (%.2f)" % a2.pos.y)
	check(Status.immune(a2, "ground"), "ground lines pass under")
	check(not zones_tagged("eddy").is_empty(), "inside a small vortex")
	h.it(a2).evade_held = false
	h.step(40)
	check(zones_tagged("eddy").is_empty(), "the vortex ends with the hold")


func test_vortex_column_cells_at_reference_powers() -> void:
	var w := _duel(10.0)
	var a: ActorState = w[0]
	var rows := {"stone": 17.0, "metal": 12.0, "sand": 10.0, "water": 9.6, "ice": 9.0}
	for t in rows:
		var th := threat(h.w, t, float(rows[t]), 10.0 if t != "stone" else 20.0)
		var ctr := Agent.of_move(h.w, a, "vortex_twister", 2, false)
		check(Interactions.predict(h.w, th, ctr).outcome == "air_infuse", "%s x Tornado T2: caught" % t)
	var vw := Agent.of_move(h.w, a, "vortex_wall", 0, false)
	check(vw.ccls == &"wall_vortex" and absf(vw.power - 16.0) < 1e-6, "Vortex Wall CP 16")
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), vw).outcome == "air_infuse", "stone (17) x Vortex Wall 16: caught (partial)")
	check(Interactions.predict(h.w, threat(h.w, "stone", 80.0, 20.0), vw).outcome == "bend", "a very hard shot only bends")
	check(Interactions.predict(h.w, threat(h.w, "water_wave", 45.0, 45.0), vw).outcome == "overwhelm", "water mass drowns the Vortex Wall")
	var heavy := Interactions.predict(h.w, threat(h.w, "stone_heavy", 31.5, 45.0), Agent.of_move(h.w, a, "vortex_twister", 2, false))
	check(heavy.outcome == "air_slow_bend", "a 45 kg stone: bend + slow (%s)" % heavy.outcome)
	var tor := Agent.of_move(h.w, a, "vortex_twister", 2, false)
	var big := threat(h.w, "flame", 20.0, 0.0, "H")
	check(Interactions.predict(h.w, big, tor).outcome == "air_infuse", "a strong flame feeds the tornado (AMP)")
	var weak := threat(h.w, "flame", 8.0, 0.0, "H")
	check(Interactions.predict(h.w, weak, tor).outcome == "extinguish", "a small flame (>= 2x) is put out")
	check(Interactions.predict(h.w, threat(h.w, "lightning", 24.0, 0.0, "E"), tor).outcome == "pass", "lightning passes")
	check(Interactions.predict(h.w, threat(h.w, "gust", 11.0, 0.0, "P"), tor).outcome == "absorb", "a gust is absorbed (spins faster)")
	check(Interactions.predict(h.w, threat(h.w, "sound", 16.0, 0.0, "P"), tor).outcome == "weaken", "sound weakens it (x0.6)")


func test_vortex_kit_is_deterministic_and_ledgers_balance() -> void:
	var hashes := []
	for k in 2:
		var s := _duel(10.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		var base := snap(h.w)
		h.launch_at(a, "stone", 20.0, 12.0, Sim.AMBIENT_C, "", r, 6.0)
		run_move(a, "vortex_twister", 2, 30, 13)
		h.step(200)
		run_move(a, "vortex_eddy", 1, 28, 40)
		ledgers_ok(base, "vortex exchange", 1e-5)
		finite_world("vortex")
		hashes.append("%s|%s|%d|%.4f|%.4f" % [str(a.pos), str(r.pos), h.w.bodies.size(), a.focus, r.health])
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: %s vs %s" % [hashes[0], hashes[1]])
