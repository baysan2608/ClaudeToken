extends FireKitTest
## Fire / Flame (sub 0): the legacy kit stays exact (flagship, waves, lightning suites) and gains Fire Column / Inferno,
## Fireball (water quenches it, wind feeds it), Fire Line, the extended Heat Sink, Backdraft, Ground Heat, SCORCH
## ("melt the wall") and Rocket Hop.


func _hold_strike(p: ActorState, ticks: int) -> void:
	h.press(p, "attack")
	h.step(ticks)
	h.release(p, "attack")
	h.step(2)


func test_fire_column_t2_and_inferno_t3_leave_fire_fields() -> void:
	for tier in [2, 3]:
		var pr := duel(0, Sim.Element.EARTH, 5, 6.0)
		var p: ActorState = pr[0]
		var r: ActorState = pr[1]
		var base := snap(h.w)
		var f0 := p.focus
		_hold_strike(p, int(Charge.tier_times(Moves.DEFS.fire_attack)[tier - 1] * 60.0) + 4)
		var fl := h.last_event("flare")
		check(not fl.is_empty() and int(fl.get("tier", 0)) == tier, "T%d: the strike released at its tier (%s)" % [tier, fl])
		check(h.has_event("fire_column"), "T%d: fire_column event" % tier)
		var fields := zones_tagged("fire_field")
		check(fields.size() == 1, "T%d: one fire field (%d)" % [tier, fields.size()])
		if fields.size() == 1:
			check(fields[0].heat_payload > 50.0, "T%d: the field holds paid heat (%.0f HU)" % [tier, fields[0].heat_payload])
			near(fields[0].zone_radius, 1.5 if tier == 2 else 2.6, 1e-6, "T%d: field radius" % tier)
		check(r.health < 100.0 - (15.0 if tier == 2 else 20.0), "T%d: the rival in range is hit hard (%.1f)" % [tier, r.health])
		check(r.status.has("burning"), "T%d: and burns" % tier)
		var cost := float(Charge.pget(Moves.DEFS.fire_attack, tier, "col_hu", 0.0)) / Sim.HU_PER_FOCUS
		check(f0 - p.focus >= cost - 0.5, "T%d: paid %.0f HU (%.1f Focus spent)" % [tier, cost * 10.0, f0 - p.focus])
		h.step(300)
		check(zones_tagged("fire_field").is_empty(), "T%d: the field burns out" % tier)
		ledgers_ok(base, "T%d column" % tier)
		fx_catalogued("column T%d" % tier)


func test_legacy_lightning_flag_still_bolts_on_flame() -> void:
	for hold in [48, 100]:
		var pr := duel(0, Sim.Element.EARTH, 5, 8.0, {"lightning": true})
		var p: ActorState = pr[0]
		_hold_strike(p, hold)
		check(h.has_event("lightning") and not h.has_event("fire_column"), "hold %d ticks with the lightning flag: the bolt" % hold)
	var pr2 := duel(0, Sim.Element.EARTH, 5, 8.0)
	_hold_strike(pr2[0], 48)
	check(h.has_event("flare", "heavy", true) and not h.has_event("lightning"), "no flag, 0.8 s: the legacy blaze")


func test_fireball_bursts_on_the_rival_and_water_quenches_it() -> void:
	var pr := duel(0, Sim.Element.EARTH, 5, 9.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	var base := snap(h.w)
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step(3)
	h.release(p, "attack")
	h.until(func(): return not bodies_of(Sim.Mat.FIRE).is_empty(), 40)
	var fb := bodies_of(Sim.Mat.FIRE)
	check(fb.size() == 1 and fb[0].tag == &"fireball", "a fireball body")
	if fb.size() == 1:
		check(fb[0].heat_payload > 100.0, "carrying the paid heat (%.0f HU)" % fb[0].heat_payload)
	h.until(func(): return h.has_event("fire_burst"), 90)
	check(h.has_event("fire_burst") and r.health < 100.0, "it bursts on the rival (hp %.1f)" % r.health)
	h.step(60)
	ledgers_ok(base, "fireball")
	# Water in the way quenches it (booked boil).
	pr = duel(0, Sim.Element.EARTH, 5, 9.0)
	p = pr[0]
	r = pr[1]
	var wb := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, 4.0, (p.chest() + r.chest()) * 0.5, "test")
	wb.gravity_scale = 0.0
	wb.static_body = true
	base = snap(h.w)
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step(3)
	h.release(p, "attack")
	h.until(func(): return h.has_event("extinguish") or h.has_event("fire_burst"), 90)
	check(h.has_event("steam_block"), "the water takes the fireball's heat (steam)")
	check(h.has_event("extinguish") and not h.has_event("fire_burst"), "the fireball is quenched before it bursts")
	check(wb.mass < 4.0, "some water boiled away (%.2f kg)" % wb.mass)
	h.step(30)
	ledgers_ok(base, "fireball quenched")


func test_fireball_held_by_a_wind_grip_is_fed_20_percent() -> void:
	h = SimHarness.new(4)
	h.begin_scope()
	Moves.register("t_wind_grip", {"element": 3, "sub": 1, "slot": "tech", "verb": "grip", "ccls": "grip_wind", "startup": 0.05,
		"active": 0.05, "recovery": 0.2, "reach": 8.0, "cone": 70.0, "base": 0.95, "speed": 16.0, "damage": 6.0, "balance": 10.0})
	Moves.bind(3, 1, "tech", "t_wind_grip")
	if not Interactions.allows(_probe_fireball(), &"grip_wind"):
		Interactions.add_rule("flame", "grip_wind", {"outcome": "reclaim", "bands": [[0.0, "reclaim"]], "full_at": 0.0, "id": "t_wind_grip_fire"})
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.AIR)
	var o := h.actor("O", Vector3(0, 0, -6), 1, {}, Sim.Element.FIRE)
	o.is_dummy = true
	a.subs[3] = 1
	h.step(20)
	var fb := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 0.5, a.chest() + a.forward() * 3.0, "test")
	fb.tag = &"fireball"
	fb.heat_payload = 200.0
	fb.gravity_scale = 0.0
	fb.static_body = false
	h.w.ledger.generated += 200.0
	var base := snap(h.w)
	h.press(a, "tech")
	h.step(12)
	check(fb.controller == a.id, "the wind grip holds the fireball")
	var before := fb.heat_payload
	h.release(a, "tech")
	h.step(2)
	check(h.has_event("fed"), "released from the wind: it is fed")
	var fed := h.last_event("fed")
	if not fed.is_empty():
		near(float(fed.add), before * 0.2, before * 0.03, "+20 % heat")
	h.step(80)
	ledgers_ok(base, "fed fireball")
	h.end_scope()


func _probe_fireball() -> MatBody:
	var b := MatBody.new()
	b.mat = Sim.Mat.FIRE
	b.tag = &"fireball"
	return b


func test_fire_line_runs_leaves_a_trail_and_dies_on_a_puddle() -> void:
	var pr := duel(0, Sim.Element.EARTH, 5, 10.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	var base := snap(h.w)
	h.flick(p, "attack", Sim.Gesture.DOWN)
	h.step(3)
	h.release(p, "attack")
	h.step(30)
	check(not h.events("spawn").is_empty() and zones_tagged("fire_field").size() >= 1, "the line drops burning patches")
	h.until(func(): return h.events("hit").any(func(e): return e.actor == r.id), 60)
	check(r.health < 100.0, "it reaches the rival 10 m away")
	h.step(240)
	ledgers_ok(base, "fire line")
	# A puddle on its way puts it out.
	pr = duel(0, Sim.Element.EARTH, 5, 10.0)
	p = pr[0]
	r = pr[1]
	var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 6.0, Vector3(p.pos.x, 0.0, p.pos.z - 4.0), "test")
	pd.update_radius_puddle()
	pd.radius = 1.2
	base = snap(h.w)
	h.flick(p, "attack", Sim.Gesture.DOWN)
	h.step(3)
	h.release(p, "attack")
	h.step(80)
	check(h.events("extinguish").any(func(e): return e.get("by", "") == "water"), "the puddle douses the line")
	check(r.health == 100.0, "the rival behind the puddle is safe (%.1f)" % r.health)
	ledgers_ok(base, "fire line doused")


func test_perfect_flame_guard_draws_300_hu_from_a_magma_blob_and_melts_ice() -> void:
	var pr := duel(0, Sim.Element.EARTH, 5, 10.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	var blob := h.launch_at(p, Sim.Mat.STONE, 20.0, 15.0, 1100.0, "", r, 6.0)
	Thermal.heat(blob, 200.0)
	Thermal.update_phase(blob)
	h.w.ledger.generated += blob.thermal_energy()
	check(String(Interactions.classify(blob)) == "magma", "a magma blob (%s)" % Interactions.classify(blob))
	var base := snap(h.w)
	var e0 := blob.thermal_energy()
	h.step(14)
	h.press(p, "guard")
	h.until(func(): return h.has_event("heat_sink"), 30)
	var hs := h.last_event("heat_sink")
	check(not hs.is_empty() and absf(float(hs.gain) - 300.0) < 1.0, "Heat Sink drew 300 HU (%s)" % [hs.get("gain")])
	check(p.heat_reserve > 280.0, "into the reserve (%.0f)" % p.heat_reserve)
	check(blob.thermal_energy() <= e0 - 299.0, "the blob lost it (it crusts mid-air)")
	h.step(40)
	h.release(p, "guard")
	h.step(20)
	ledgers_ok(base, "heat sink")
	# Ice: a perfect guard melts the shard before it lands.
	pr = duel(0, Sim.Element.EARTH, 5, 10.0)
	p = pr[0]
	r = pr[1]
	var ice := h.launch_at(p, Sim.Mat.WATER, 4.0, 18.0, -5.0, "", r, 6.0)
	var e1 := ice.thermal_energy()
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	h.w.ledger.freeze_dump += ice.thermal_energy() - e1
	base = snap(h.w)
	h.step(16)
	h.press(p, "guard")
	h.until(func(): return h.has_event("heat_sink"), 30)
	check(not ice.alive or ice.phase == Sim.Phase.LIQUID, "the ice melted (%s)" % [ice.describe() if ice.alive else "gone"])
	h.step(30)
	h.release(p, "guard")
	h.step(10)
	ledgers_ok(base, "heat sink ice")


func test_backdraft_returns_the_reserve_and_ground_heat_boils_puddles() -> void:
	var pr := duel(0, Sim.Element.EARTH, 5, 4.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	p.heat_reserve = 300.0
	var base := snap(h.w)
	h.press(p, "guard")
	h.step(10)
	h.flick(p, "guard", Sim.Gesture.UP)
	h.step(30)
	var bd := h.last_event("backdraft")
	check(not bd.is_empty() and float(bd.hu) > 280.0, "Backdraft releases the reserve (%s)" % [bd.get("hu")])
	check(p.heat_reserve < 1.0 and r.health < 90.0, "reserve spent (%.1f), rival burned (%.1f)" % [p.heat_reserve, r.health])
	h.release(p, "guard")
	h.step(40)
	ledgers_ok(base, "backdraft")
	pr = duel(0, Sim.Element.EARTH, 5, 6.0)
	p = pr[0]
	p.heat_reserve = 200.0
	var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 2.0, p.pos + Vector3(0.8, 0, 0), "test")
	pd.update_radius_puddle()
	base = snap(h.w)
	var wm := h.w.water_mass()
	h.press(p, "guard")
	h.step(10)
	h.flick(p, "guard", Sim.Gesture.DOWN)
	h.step(40)
	check(not pd.alive or pd.mass < 0.1, "the puddle at the feet boiled away")
	check(p.heat_reserve < 1.0 and h.has_event("vent"), "the reserve went into the ground")
	near(h.w.water_mass(), wm, 1e-6, "water mass conserved (vapour booked)")
	h.release(p, "guard")
	h.step(20)
	ledgers_ok(base, "ground heat")


## "Melt the wall": SCORCH (the thermal technique on a wall) slumps a Bulwark's face in about 1.5 s: the molten face
## body is on the caster's side, the rest crumbles, every ledger exact.
func test_scorch_slumps_a_bulwark_in_about_one_and_a_half_seconds() -> void:
	var pr := duel(0, Sim.Element.EARTH, 5, 8.0, {"magma": true, "heat_draw": true})
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	var wall := bulwark(r, Vector3(0, 0, p.pos.z - 3.5), p.facing)
	var base := snap(h.w)
	var pv := h.w.tech_preview(p, p.forward())
	check(String(pv.mode) == "SCORCH" and int(pv.body) == wall.id, "the technique offers SCORCH on the wall (%s)" % [pv])
	h.press(p, "tech")
	var t0 := h.w.tick
	h.until(func(): return h.has_event("slump"), 200)
	var dt := float(h.w.tick - t0) / 60.0
	check(h.has_event("slump"), "the wall slumped")
	check(dt > 1.3 and dt < 1.8, "in about 1.5 s (%.2f s)" % dt)
	var sl := h.last_event("slump")
	var face := h.w.get_body(int(sl.get("body", -1)))
	check(face != null and face.alive and face.liquid >= 0.5 and face.is_stone(), "the molten face body exists")
	if face != null:
		near(face.mass, 30.0, 1e-6, "25 % of the wall")
		check((face.pos - wall.pos).dot(p.pos - wall.pos) > 0.0, "on the caster's side")
	check(not wall.alive and h.has_event("wall_crumble"), "the rest crumbled")
	h.release(p, "tech")
	h.step(30)
	ledgers_ok(base, "scorch")


func test_rocket_hop_hovers_and_lands() -> void:
	var pr := duel(0, Sim.Element.EARTH, 5, 8.0)
	var p: ActorState = pr[0]
	var y0 := p.pos.y
	h.press(p, "evade")
	h.it(p).evade_held = true
	var top := y0
	for k in 60:
		h.step()
		top = maxf(top, p.pos.y)
	h.it(p).evade_held = false
	check(h.events("action").any(func(e): return e.move == "rocket_hop"), "held evade morphs into Rocket Hop")
	check(top > y0 + 1.8, "it hops up (%.2f m)" % (top - y0))
	h.until(func(): return p.grounded and p.action == null, 120)
	check(p.grounded and absf(p.pos.y - y0) < 0.05, "and lands again")


## Combo 11 "Fire Tornado" (MOVESET §9.3): a fireball thrown into a tornado sets it alight; a vacuum snuffs it.
func test_fireball_into_a_tornado_makes_a_fire_tornado_and_a_vacuum_snuffs_it() -> void:
	# Test-local zone tags (the Air kit's own tornado / bubble zones add their own capture and cells on top).
	for tag in ["t_tornado", "t_null"]:
		var pr := duel(0, Sim.Element.AIR, 5, 8.0)
		var p: ActorState = pr[0]
		var r: ActorState = pr[1]
		r.is_dummy = true
		h.begin_scope()
		Interactions.register_tag_class(&"t_tornado", &"tornado", &"t_tornado")
		Interactions.register_tag_class(&"t_null", &"vacuum", &"t_null")
		var z := h.spawn_zone(tag, (p.pos + r.pos) * 0.5, 1.8, r, 25.0)
		z.max_life = 5.0
		var base := snap(h.w)
		h.flick(p, "attack", Sim.Gesture.UP)
		h.step(3)
		h.release(p, "attack")
		h.step(40)
		if tag == "t_tornado":
			check(h.has_event("infuse") and z.props.get("fire", false), "the tornado becomes a fire tornado")
			check(zones_tagged("fire_field").any(func(f): return int(f.props.get("follow", -1)) == z.id), "its fire rides the tornado")
		else:
			check(h.events("extinguish").any(func(e): return e.get("by", "") == "vacuum"), "the vacuum snuffs the fireball")
			check(not h.has_event("fire_burst"), "no burst")
		ledgers_ok(base, "fireball into %s" % tag)
		h.end_scope()
