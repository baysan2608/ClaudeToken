extends WaterKitTest
## Water / Plant (sub 3, docs/MOVESET.md §7.8, P2): vines grow from water (booked water_to_plant), burn x3, are brittle
## when frozen. Bramble Lash, Burr Shot, Root Snare, Thicket Fan, Living Lattice (+ Catch & Sling), Lattice Roll, Deep Roots,
## Vinegrip (+ Wrap, Hook), Vine Swing, Canopy.


func _plant_duel(dist: float = 8.0, rival_element: int = Sim.Element.EARTH) -> Array:
	return duel(3, rival_element, 3, dist)


func _tap(p: ActorState, gesture: int, hold_ticks: int = 2) -> void:
	h.flick(p, "attack", gesture)
	h.step(hold_ticks)
	h.release(p, "attack")


func _hold(p: ActorState, gesture: int, secs: float) -> void:
	h.flick(p, "attack", gesture)
	h.step(int(secs * 60.0))
	h.release(p, "attack")


func _raise(w: ActorState, ticks: int = 18) -> MatBody:
	h.press(w, "guard")
	h.step(ticks)
	return h.w.get_body(w.wall_body)


func test_bramble_lash_yanks_a_target_in_and_the_vine_withers() -> void:
	var s := _plant_duel(4.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var d0 := w.pos.distance_to(r.pos)
	var skin0 := w.water_carried
	_tap(w, Sim.Gesture.NONE)
	h.step(40)
	check(h.has_event("action", "move", "bramble_lash"), "Bramble Lash ran")
	check(r.health < 100.0 or h.has_event("hit"), "the whip hits")
	check(w.pos.distance_to(r.pos) < d0 - 0.4, "the target is yanked toward the caster (%.2f -> %.2f m)" % [d0, w.pos.distance_to(r.pos)])
	near(skin0 - w.water_carried, 1.0, 0.01, "1 kg of water went into the vine")
	check(float(h.w.mass_ledger.water_to_plant) >= 1.0, "booked water_to_plant")
	check(bodies_of(Sim.Mat.PLANT).size() == 1, "one vine lies where the whip ended")
	h.step(240)
	check(bodies_of(Sim.Mat.PLANT).is_empty(), "and withers away")
	ledgers_ok(base, "Bramble Lash")
	# T3 Briar Storm: 360 degrees
	s = _plant_duel(4.0)
	w = s[0]
	r = s[1]
	r.pos = w.pos + Vector3(0, 0, 3.0)      # behind
	r.is_dummy = true
	_hold(w, Sim.Gesture.NONE, 1.9)
	h.step(30)
	check(r.health < 100.0, "Briar Storm hits a rival behind the caster (hp %.1f)" % r.health)


func test_burr_shot_seeds_sprout_snares_that_root() -> void:
	var s := _plant_duel(8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	_tap(w, Sim.Gesture.UP)
	h.step(10)
	check(h.events("launch").size() == 3, "three burrs (%d)" % h.events("launch").size())
	var rooted := false
	for k in 100:
		h.step()
		if Status.rooted(r):
			rooted = true
	check(rooted or h.has_event("status", "status", "rooted"), "a burr sprouted under the rival and rooted them")
	check(zones_tagged("snare").size() >= 1, "snare zones stand where the burrs landed")
	h.step(400)
	check(zones_tagged("snare").is_empty(), "they wither")
	ledgers_ok(base, "Burr Shot")


func test_root_snare_travels_underground_and_roots_but_not_fliers() -> void:
	var s := _plant_duel(9.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	_tap(w, Sim.Gesture.DOWN)
	var wave: MatBody = null
	for k in 60:
		h.step()
		for b in h.w.bodies:
			if b.alive and b.tag == &"roots":
				wave = b
		if wave != null:
			break
	check(wave != null and wave.mat == Sim.Mat.PLANT and wave.form == Sim.Form.WAVE, "a PLANT wave tagged roots")
	h.step(60)
	check(Status.rooted(r) or h.has_event("status", "status", "rooted"), "the roots erupt under the target: rooted")
	h.step(300)
	ledgers_ok(base, "Root Snare")
	# A flying (levitating) fighter passes over the line.
	s = _plant_duel(9.0)
	w = s[0]
	r = s[1]
	r.is_dummy = true
	r.flying = true
	_tap(w, Sim.Gesture.DOWN)
	h.step(120)
	check(not Status.rooted(r), "a fighter in the air is not rooted")


func test_thicket_fan_slows_and_catches() -> void:
	var s := _plant_duel(5.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	_tap(w, Sim.Gesture.SIDE)
	h.step(30)
	var z := zones_tagged("briar")
	check(z.size() == 1 and z[0].mat == Sim.Mat.PLANT and absf(z[0].zone_radius - 2.4) < 0.01, "a briar zone of vine (%s)" % [z.size()])
	if z.is_empty():
		return
	check(Interactions.counter_class(z[0], h.w) == &"briar", "counter class briar")
	r.pos = z[0].pos
	h.step(10)
	check(Status.has(r, "slowed"), "whoever stands in it is slowed (-40%)")
	var stone := h.launch_at(w, "stone", 10.0, 14.0, Sim.AMBIENT_C, "", r, 3.5)
	var v0 := stone.vel.length()
	h.step(12)
	check(stone.vel.length() < v0 * 0.9 or stone.attack_id == 0, "a small projectile is caught and slowed (%.1f -> %.1f)" % [v0, stone.vel.length()])
	h.step(400)
	ledgers_ok(base, "Thicket Fan")


func test_living_lattice_captures_catches_and_slings_back() -> void:
	var s := _plant_duel(9.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var wall := _raise(w)
	check(wall != null and wall.mat == Sim.Mat.PLANT and wall.tag == &"vine" and wall.form == Sim.Form.WALL, "a vine WALL (%s)" % [wall.describe() if wall else "none"])
	if wall == null:
		return
	near(wall.mass * Materials.hardness(wall), 16.0, 0.01, "40 kg x 0.4 = CP 16")
	check(float(h.w.mass_ledger.water_to_plant) >= 40.0 - 1e-6, "its 40 kg are booked water -> vine")
	# A stone is captured (x1.3).
	var stone := h.launch_at(w, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 6.0)
	h.step(40)
	check(stone.captured_by == wall.id or h.events("interaction").any(func(e): return e.counter == "wall_vine" and e.outcome == "capture"), "the stone is caught in the lattice")
	check(w.health == 100.0, "and never reaches the fighter")
	h.release(w, "guard")
	h.step(120)
	ledgers_ok(base, "Living Lattice")
	# Catch & Sling: a perfect guard throws the projectile back at its thrower.
	s = _plant_duel(9.0)
	w = s[0]
	r = s[1]
	r.is_dummy = true
	base = snap(h.w)
	wall = _raise(w)
	var st2 := h.launch_at(w, "stone", 12.0, 17.0, Sim.AMBIENT_C, "", r, 6.0)
	var counter := Agent.of_body(h.w, wall)
	counter.actor = w
	counter.perfect = true
	var res := Interactions.resolve(h.w, Agent.of_body(h.w, st2, w), counter, {"site": "wall"})
	check(res.outcome == "redirect" and res.rule_id.contains("vine"), "perfect: slung back (%s via %s)" % [res.outcome, res.rule_id])
	check(st2.attack_owner == w.id and st2.vel.dot(Vector3(0, 0, -1)) > 5.0, "now flying at the thrower as the caster's attack")
	h.step(80)
	check(r.health < 100.0, "and it hits them (hp %.1f)" % r.health)
	h.release(w, "guard")
	h.step(200)
	ledgers_ok(base, "Catch & Sling")


func test_a_flame_burns_the_lattice_x3_and_water_feeds_it() -> void:
	var s := _plant_duel(9.0, Sim.Element.FIRE)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var wall := _raise(w)
	check(wall != null, "wall up")
	if wall == null:
		return
	var m0 := wall.mass
	var f := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 1.0, wall.pos + Vector3(0, 1.0, -1.5), "test")
	f.heat_payload = 160.0
	h.w.ledger.generated += 160.0
	f.vel = Vector3(0, 0, 8.0)
	f.gravity_scale = 0.0
	f.attack_id = h.w.new_attack_id()
	f.attack_owner = r.id
	f.hit_set[r.id] = true
	f.max_life = 3.0
	h.step(40)
	check(wall.mass < m0 - 2.0 or not wall.alive, "the flame burned vine away (%.1f -> %.1f kg)" % [m0, wall.mass if wall.alive else 0.0])
	check(float(h.w.mass_ledger.burned) > 2.0, "booked as burned (%.1f)" % float(h.w.mass_ledger.burned))
	h.release(w, "guard")
	h.step(240)
	ledgers_ok(base, "burn")
	# Water feeds it: a water body hitting the lattice grows it.
	s = _plant_duel(9.0, Sim.Element.WATER)
	w = s[0]
	r = s[1]
	r.is_dummy = true
	base = snap(h.w)
	wall = _raise(w)
	m0 = wall.mass
	var stream := h.launch_at(w, "water", 6.0, 16.0, Sim.AMBIENT_C, "slug", r, 6.0)
	h.w.mass_ledger.moisture_taken += 6.0
	stream.form = Sim.Form.BLOB
	h.step(40)
	check(wall.mass > m0 + 2.0, "the vines drink the water and grow (%.1f -> %.1f kg)" % [m0, wall.mass])
	h.release(w, "guard")
	h.step(240)
	ledgers_ok(base, "feed")


func test_frozen_vines_are_brittle() -> void:
	var s := _plant_duel(9.0)
	var w: ActorState = s[0]
	var wall := _raise(w)
	var thorns := h.w.spawn_body(Sim.Mat.PLANT, Sim.Form.CHUNK, 4.0, w.pos + Vector3(0, 0.3, -3.0), "test")
	h.w.mass_ledger.water_to_plant += 4.0
	var base := snap(h.w)
	var fr := Agent.new()
	fr.kind = "volume"
	fr.ccls = &"frost"
	fr.cls = &"frost"
	fr.power = 12.0
	fr.ch.C = 12.0
	var res := Interactions.resolve(h.w, Agent.of_body(h.w, thorns), fr)
	check(res.outcome == "transform" and res.to == "brittle" and thorns.hardness < 0.1, "frost x vine -> brittle (hardness %.2f)" % thorns.hardness)
	h.release(w, "guard")
	h.step(240)
	ledgers_ok(base, "brittle")


func test_lattice_roll_entangles() -> void:
	var s := _plant_duel(7.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var wall := _raise(w)
	h.flick(w, "guard", Sim.Gesture.UP)
	h.step(6)
	h.release(w, "guard")
	var p0 := wall.pos
	h.step(80)
	check(wall.alive and wall.pos.distance_to(p0) > 3.0, "the lattice rolled forward (%.1f m)" % wall.pos.distance_to(p0))
	check(Status.has(r, "entangled") or h.has_event("status", "status", "entangled"), "and entangled the rival")
	h.step(400)
	ledgers_ok(base, "Lattice Roll")


func test_deep_roots_anchor_beats_a_tornado_and_drinks_puddles() -> void:
	var s := _plant_duel(7.0)
	var w: ActorState = s[0]
	var base := snap(h.w)
	h.press(w, "guard")
	h.step(10)
	h.flick(w, "guard", Sim.Gesture.DOWN)
	h.step(20)
	check(w.anchored and w.stance == "roots", "rooted: anchored (%s)" % w.stance)
	var anchor := Agent.of_stance(h.w, w)
	check(anchor.power >= 35.0, "anchor CP 35 (%.0f)" % anchor.power)
	var torn := threat(h.w, "tornado", 30.0, 0.0, "P")
	var pr := Interactions.predict(h.w, torn, anchor)
	check(pr.band == "full", "a 30 PU tornado cannot lift an anchored fighter (ratio %.2f)" % pr.ratio)
	# drinks puddles under the fighter
	w.water_carried = 2.0
	var pud := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, w.pos + Vector3(0, 0.3, 0), "test")
	h.w.mass_ledger.moisture_taken += 3.0
	h.w._water_to_puddle(pud)
	base = snap(h.w)
	h.step(120)
	check(w.water_carried > 2.9, "the puddle was drunk into the waterskin (%.2f kg)" % w.water_carried)
	h.release(w, "guard")
	h.step(40)
	check(not w.anchored, "released: free again")
	ledgers_ok(base, "Deep Roots")


func test_vinegrip_wins_at_long_range_and_wraps() -> void:
	var s := _plant_duel(12.0, Sim.Element.EARTH)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var stone := h.launch_at(w, "stone", 20.0, 6.0, Sim.AMBIENT_C, "", r, 8.5)
	stone.attack_owner = r.id
	h.press(w, "tech")
	var got := false
	for k in 40:
		h.step()
		if stone.controller == w.id:
			got = true
			break
	check(got, "a stone 8.5 m away is seized (REC, 9 m reach)")
	check(h.has_event("control_won", "actor", w.id), "the grip contest was won")
	# Wrap: attack while holding -> the body roots whoever it hits.
	h.press(w, "attack")
	h.step(3)
	h.release(w, "attack")
	h.step(3)
	var held := h.w.held(w)
	check(held != null and String(held.props.get("hit_status", "")) == "rooted", "Wrap: the held body carries a rooting hit")
	h.release(w, "tech")
	h.step(160)
	check(Status.rooted(r) or h.has_event("status", "status", "rooted") or r.health < 100.0, "it roots the rival on hit")
	h.step(300)
	ledgers_ok(base, "Vinegrip")


func test_vinegrip_hooks_a_fighter() -> void:
	var s := _plant_duel(7.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var d0 := w.pos.distance_to(r.pos)
	var bal0 := r.balance
	h.press(w, "tech")
	h.step(30)
	h.release(w, "tech")
	h.step(40)
	check(h.has_event("hook", "target", r.id), "Hook event")
	check(w.pos.distance_to(r.pos) < d0 - 1.0, "the fighter is yanked toward the caster (%.1f -> %.1f m)" % [d0, w.pos.distance_to(r.pos)])
	check(r.balance < bal0, "-20 balance (%.0f -> %.0f)" % [bal0, r.balance])


func test_vine_swing_to_a_pillar_and_canopy_hover() -> void:
	# The pillar at (-13, 12.5..13.5) of the lab: swing toward it.
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(-8.0, 0, 10.0), 0, {}, Sim.Element.WATER)
	var r := h.actor("R", Vector3(6.0, 0, -4.0), 1, {}, Sim.Element.EARTH)
	r.is_dummy = true
	w.subs[1] = 3
	h.step(5)
	var base := snap(h.w)
	var p0 := w.pos
	h.it(w).move = Vector3(-1, 0, 0.5).normalized()
	h.press(w, "evade")
	h.step(1)
	h.it(w).move = Vector3.ZERO
	h.step(30)
	check(h.has_event("swing", "anchored", true), "an anchor was found within 9 m")
	check(w.pos.distance_to(p0) > 3.0, "swung %.1f m" % w.pos.distance_to(p0))
	h.step(80)
	ledgers_ok(base, "Vine Swing")
	# Canopy: hold evade, hover about 1.5 s then drop.
	var s := _plant_duel(10.0)
	w = s[0]
	h.it(w).move = Vector3.ZERO
	h.press(w, "evade")
	h.it(w).evade_held = true
	h.step(30)
	check(w.action != null and w.action.id == "canopy", "morphed into Canopy (%s)" % [w.action.id if w.action else "none"])
	check(w.flying and w.pos.y > 0.4, "hovering (y %.2f)" % w.pos.y)
	h.step(60)
	h.step(40)
	check(not w.flying, "let go after about 1.5 s")
	h.it(w).evade_held = false
	h.step(60)
