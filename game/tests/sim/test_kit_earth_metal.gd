extends TestCase
## Earth / Metal (sub 1): satchel, Razor Disc ladder, Iron Lance -> rod, Lodestone Line caltrops, Chain Arc,
## Aegis Plate (+ heat, lightning, Magnet Catch), Plate Rush, Rod Plant, Lodestone Grip / Reforge /
## Recall / plate scrap, Magnet Glide, Iron Stance and the Metal column of MOVESET §8.1.

const U := preload("res://tests/sim/test_kit_earth_util.gd")

var h: SimHarness


func _duel(dist: float = 7.0, t_elem: int = Sim.Element.FIRE) -> Array:
	h = SimHarness.new(4)
	return U.duel(h, 1, t_elem, dist)


func test_razor_disc_ladder_spends_the_satchel() -> void:
	var counts := []
	for tier in 4:
		var s := _duel()
		var a: ActorState = s[0]
		var mm0 := h.w.metal_mass()
		U.perform(h, a, "strike", tier)
		h.until(func(): return h.has_event("launch"), 40)
		h.step(tier == 3 and 30 or 2)
		var discs := h.w.bodies.filter(func(b): return b.alive and b.tag == &"disc" and b.mat == Sim.Mat.METAL)
		counts.append(discs.size())
		near(a.metal_carried, 12.0 - 2.0 * discs.size(), 1e-6, "T%d: 2 kg per disc from the satchel" % tier)
		near(h.w.metal_mass(), mm0, 1e-6, "T%d: metal mass conserved (satchel -> field)" % tier)
		if tier == 2:
			check(discs.all(func(b): return int(b.props.get("ricochet", 0)) == 1), "T2 discs ricochet once")
		if tier == 3:
			check(discs.all(func(b): return b.props.has("orbit_until") or b.vel.length() > 5.0), "T3 Disc Storm: orbiting")
			h.step(40)
			check(discs.all(func(b): return not b.props.has("orbit_until")), "then they fire")
	check(counts == [1, 2, 3, 5], "1 / 2 / 3 / 5 discs: %s" % [counts])


func test_iron_lance_embeds_and_railspike_pierces() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	U.wall(h, Sim.Mat.STONE, "", 120.0, Vector3(0, 0, -1.0), t, 0.0)
	U.perform(h, a, "thrust", 0)
	var n := h.until(func(): return h.has_event("stick"), 60)
	check(n > 0, "the lance embeds in the rival's Bulwark")
	var rods := h.w.bodies.filter(func(b): return b.alive and b.tag == &"rod" and b.mat == Sim.Mat.METAL)
	check(rods.size() == 1 and rods[0].static_body and Materials.conducts(rods[0]), "it stays as a conductive rod")
	check(int(rods[0].props.get("metal_owner", -1)) == a.id if not rods.is_empty() else false, "recallable (owned)")
	var s2 := _duel(9.0)
	var a2: ActorState = s2[0]
	U.perform(h, a2, "thrust", 3)
	h.until(func(): return h.has_event("launch"), 40)
	var ls := h.w.bodies.filter(func(b): return b.alive and b.tag == &"lance")
	check(ls.size() == 1 and is_equal_approx(ls[0].mass, 12.0) and int(ls[0].props.get("pierce", 0)) == 1, "Railspike: 12 kg, pierces one body")
	check(float(h.last_event("launch").get("speed", 0.0)) >= 42.0, "@42 m/s")


func test_lodestone_line_springs_caltrops_that_slow_and_conduct() -> void:
	var s := _duel(6.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var mm0 := h.w.metal_mass()
	U.perform(h, a, "ground", 0)
	var n := h.until(func(): return h.w.bodies.any(func(b): return b.alive and b.tag == &"caltrops" and b.form == Sim.Form.ZONE), 120)
	check(n > 0, "the filings spring into a caltrops zone")
	var z: MatBody = h.w.bodies.filter(func(b): return b.alive and b.tag == &"caltrops")[0] if n > 0 else null
	check(z != null and Materials.conducts(z) and z.mat == Sim.Mat.METAL, "a metal conductor node")
	check(z != null and is_equal_approx(z.zone_radius, 2.0), "r 2 m at T0")
	t.pos = z.pos if z != null else t.pos
	h.step(20)
	check(t.status.has("slowed"), "a walker inside is slowed")
	check(t.health < 100.0, "and chipped")
	near(h.w.metal_mass(), mm0, 1e-6, "metal mass conserved")
	h.step(300)
	check(z != null and (not z.alive or z.form != Sim.Form.ZONE), "the field ends after 4 s (the metal stays as scrap)")
	near(h.w.metal_mass(), mm0, 1e-6, "metal mass conserved after")


func test_chain_arc_wraps_a_stone_out_of_the_air() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.flick(a, "attack", Sim.Gesture.SIDE)
	h.step(36)
	var stone := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", t, 4.5)
	h.release(a, "attack")
	h.until(func(): return h.has_event("capture") or a.health < 100.0, 40)
	check(h.has_event("capture", "body", stone.id), "Chain Arc T1 (16 x 1.2 >= 17) wraps the stone (CAP)")
	check(a.health == 100.0, "it never hits the swinger")
	h.step(40)
	check(KitEarth.flat_dist(stone.pos, a.pos) < 2.5, "yanked to the swinger's feet (%.1f m)" % KitEarth.flat_dist(stone.pos, a.pos))
	var s2 := _duel(4.0)
	var a2: ActorState = s2[0]
	var t2: ActorState = s2[1]
	h.flick(a2, "attack", Sim.Gesture.SIDE)
	h.step()
	h.release(a2, "attack")
	h.until(func(): return h.events("hit").any(func(e): return e.actor == t2.id), 40)
	check(t2.vel.dot(a2.pos - t2.pos) > 0.0, "a fighter in the arc is yanked toward the swinger")


func test_aegis_plate_blocks_and_magnet_catches_metal() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(20)
	var plate := h.w.held(a)
	check(plate != null and plate.tag == &"plate" and is_equal_approx(plate.mass, 6.0), "a 6 kg plate from the satchel")
	near(a.metal_carried, 6.0, 1e-6, "satchel 12 -> 6")
	h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", t)
	h.until(func(): return h.has_event("block") or h.has_event("hit"), 40)
	check(h.events("block").any(func(e): return e.actor == a.id), "Aegis (CP 20 >= 17) blocks a stone shot")
	h.release(a, "guard")
	h.step(30)
	near(a.metal_carried, 12.0, 1e-6, "guard down: the plate goes back into the satchel")
	h.log.clear()
	h.press(a, "guard")
	h.step(2)
	var mm0 := h.w.metal_mass()
	var disc := U.shot(h, Sim.Mat.METAL, 2.0, a.chest() + a.forward() * 2.2, -a.forward() * 24.0, t, "disc")
	h.until(func(): return not disc.alive or h.has_event("hit"), 30)
	check(not disc.alive and h.has_event("satchel"), "perfect Aegis: Magnet Catch pulls the disc into the satchel")
	near(h.w.metal_mass(), mm0, 1e-6, "metal mass conserved")
	h.release(a, "guard")


func test_aegis_plate_heats_red_hot_and_is_dropped() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	h.press(a, "guard")
	h.step(10)
	var plate := h.w.held(a)
	var e0 := U.e0(h)
	h.w.ledger.generated += h.w.heat_body(plate, 20.0)
	h.step(3)
	check(h.w.held(a) == plate, "warm plate still held (%.0f °C)" % plate.temp)
	h.w.ledger.generated += h.w.heat_body(plate, 20.0)
	h.step(3)
	check(h.w.held(a) == null and h.has_event("drop"), "red-hot (>= 300 °C): dropped")
	check(int(plate.props.get("metal_owner", -1)) == a.id, "still A's metal on the field")
	check(U.energy_drift(h, e0) < 1e-6, "energy balanced")
	h.release(a, "guard")


func test_aegis_grounds_bolts_on_stone_and_conducts_them_on_the_plate() -> void:
	var dmg := []
	for where in ["stone", "plate"]:
		var s := _duel(8.0, Sim.Element.FIRE)
		var a: ActorState = s[0]
		var t: ActorState = s[1]
		if where == "plate":
			a.pos = Vector3(-9, 0.02, -1)
			t.pos = Vector3(-9, 0, -9)
			h.step(5)
		h.press(a, "guard")
		h.step(20)
		var hp := a.health
		t.lock_target = a.id
		Conduction.discharge(h.w, t, a.chest(), {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
			h.w.new_attack_id(), true)
		dmg.append(hp - a.health)
		h.release(a, "guard")
	check(float(dmg[0]) == 0.0, "on stone the plate grounds the bolt (%.1f)" % dmg[0])
	check(float(dmg[1]) > 24.0, "on the metal plate it conducts into the holder x1.2 (%.1f)" % dmg[1])


func test_plate_rush_hurls_the_plate() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(15)
	var plate := h.w.held(a)
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step()
	h.release(a, "guard")
	h.until(func(): return plate.attack_id != 0, 30)
	check(plate.attack_id != 0 and plate.attack_owner == a.id and plate.tag == &"plate", "the plate flies as A's attack")
	h.until(func(): return t.health < 100.0, 60)
	check(t.health < 100.0, "and hits")
	near(a.metal_carried, 6.0, 1e-6, "the plate is on the field now (recallable)")


func test_rod_plant_draws_and_grounds_bolts_until_it_melts() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(10)
	h.flick(a, "guard", Sim.Gesture.DOWN)
	h.step()
	h.release(a, "guard")
	h.step(20)
	var rods := h.w.bodies.filter(func(b): return b.alive and b.tag == &"rod" and b.form == Sim.Form.ZONE)
	check(rods.size() == 1, "a rod is planted (a 6 m field)")
	var rod: MatBody = rods[0] if not rods.is_empty() else null
	t.lock_target = a.id
	var out := Conduction.discharge(h.w, t, a.chest(), {"range": 16.0, "damage": 36.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
		h.w.new_attack_id(), true)
	h.step()
	check(out.blocked and a.health == 100.0, "E 36 <= 60: drawn to the rod and grounded")
	check(h.has_event("grounded", "via", "rod"), "grounded via the rod")
	var out2 := Conduction.discharge(h.w, t, a.chest(), {"range": 16.0, "damage": 70.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
		h.w.new_attack_id(), true)
	check(not out2.blocked and rod != null and not rod.alive, "E 70 > 60: the rod melts")
	near(float(out2.get("e", 0.0)), 40.0, 1e-6, "and the bolt continues with 70 - 0.5 x 60")


func test_rod_touching_a_puddle_conducts_into_it() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var bystander := h.actor("B", Vector3(3, 0, 1.6), 1, {}, Sim.Element.WATER)
	bystander.is_dummy = true
	var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 6.0, Vector3(3, 0, 1.6), "scenario")
	pd.update_radius_puddle()
	var rod := h.w.spawn_zone(&"rod", Vector3(3, 0, 2.1), 6.0, a.id, 60.0, Sim.Mat.METAL, 3.0, -1.0, "test")
	h.w.mass_ledger.metal_taken += 3.0
	rod.static_body = true
	rod.props["ccls"] = "rod"
	rod.props["barrier"] = true
	rod.props["height"] = 3.5
	h.step(3)
	t.lock_target = a.id
	Conduction.discharge(h.w, t, a.chest(), {"range": 16.0, "damage": 30.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
		h.w.new_attack_id(), true)
	h.step()
	check(a.health == 100.0, "the rod grounds the bolt")
	check(bystander.health < 100.0, "but it conducts into the puddle it touches (%.1f)" % bystander.health)


func test_lodestone_grip_steals_reforges_and_throws() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var disc := U.shot(h, Sim.Mat.METAL, 2.0, a.chest() + a.forward() * 6.0, -a.forward() * 8.0, t, "disc")
	h.press(a, "tech")
	h.until(func(): return h.w.held(a) != null, 40)
	check(h.w.held(a) == disc, "Lodestone Grip seizes the rival's disc in flight (REC)")
	h.step(3)
	h.it(a).attack_pressed = true
	h.step()
	check(h.has_event("shape", "to", "lance") and disc.tag == &"lance", "T+A Reforge: a lance")
	h.release(a, "tech")
	h.step(2)
	check(disc.attack_owner == a.id and disc.attack_id != 0 and disc.tag == &"lance", "thrown back as A's lance")


func test_recall_brings_every_piece_back_through_the_rival() -> void:
	var s := _duel(5.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var mm0 := h.w.metal_mass()
	# Two discs lie behind the rival.
	for x in [-0.6, 0.6]:
		var d := h.w.spawn_body(Sim.Mat.METAL, Sim.Form.CHUNK, 2.0, Vector3(x, 0.3, -6.0), "test")
		h.w.mass_ledger.metal_taken += 2.0
		d.tag = &"disc"
		d.on_ground = true
		d.props["metal_owner"] = a.id
	var sat0 := a.metal_carried
	h.press(a, "tech")
	h.step(24)
	h.release(a, "tech")
	h.step()
	check(h.has_event("recall"), "nothing to grip: Recall")
	h.until(func(): return a.metal_carried >= sat0 + 4.0 - 1e-6, 120)
	near(a.metal_carried, sat0 + 4.0, 1e-6, "both discs back in the satchel")
	check(t.health < 100.0, "they hit the rival on the way (from behind) (%.1f)" % t.health)
	near(h.w.metal_mass(), mm0, 1e-6, "metal mass conserved")


func test_scrap_rip_on_the_arena_plate() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	a.pos = Vector3(-9, 0.02, 0)
	h.step(3)
	var taken0 := float(h.w.mass_ledger.metal_taken)
	h.press(a, "tech")
	h.until(func(): return h.w.held(a) != null, 40)
	var b := h.w.held(a)
	check(b != null and is_equal_approx(b.mass, 10.0), "10 kg of scrap ripped from the plate")
	near(float(h.w.mass_ledger.metal_taken), taken0 + 10.0, 1e-6, "booked metal_taken")
	h.release(a, "tech")
	h.step(30)
	h.log.clear()
	h.press(a, "tech")
	h.step(30)
	check(not h.has_event("rip"), "1.2 s cooldown")
	h.release(a, "tech")


func test_magnet_glide_dashes_to_metal_and_iron_stance_anchors() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var rod := h.w.spawn_body(Sim.Mat.METAL, Sim.Form.CHUNK, 3.0, a.pos + Vector3(5, 0.3, 0), "test")
	h.w.mass_ledger.metal_taken += 3.0
	rod.tag = &"rod"
	rod.static_body = true
	var p0 := a.pos
	h.press(a, "evade")
	h.step(30)
	check(a.pos.distance_to(rod.pos) < 1.8, "Magnet Glide lands at the rod (%.1f m)" % a.pos.distance_to(rod.pos))
	check(a.pos.distance_to(p0) > 3.5, "a long dash (%.1f m)" % a.pos.distance_to(p0))
	h.step(20)
	h.press(a, "evade")
	h.it(a).evade_held = true
	h.step(20)
	check(a.stance == "iron" and a.anchored and is_equal_approx(a.armor, 0.25), "Iron Stance: anchored, 25 % armor")
	h.it(a).evade_held = false
	h.step(15)
	check(a.stance == "" and not a.anchored, "released")


func test_metal_column_cells() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(20)
	var plate := h.w.held(a)
	var g := Agent.of_guard(h.w, a)
	check(g.ccls == &"plate_metal" and is_equal_approx(g.power, 19.8), "Aegis: plate_metal, CP 6 x 3.3")
	var P := func(b: MatBody) -> Dictionary:
		return Interactions.predict(h.w, Agent.of_body(h.w, b, a), Agent.of_guard(h.w, a))
	var stone := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(0, 1, 0), Vector3(0, 0, 17), t)
	check(P.call(stone).outcome == "block", "stone shot: BLK Aegis (20 vs 17)")
	var heave := U.shot(h, Sim.Mat.STONE, 45.0, Vector3(1, 1, 0), Vector3(0, 0, 14), t)
	var ph: Dictionary = P.call(heave)
	check(ph.band == "partial" and ph.outcome == "weaken", "heavy stone: WKN Aegis (plate knocked back) (%.2f)" % ph.ratio)
	var boulder := U.shot(h, Sim.Mat.STONE, 200.0, Vector3(2, 1, 0), Vector3(0, 0, 11), t)
	check(P.call(boulder).outcome == "overwhelm", "boulder: FAIL")
	var hot := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(-1, 1, 0), Vector3(0, 0, 17), t, "", 1000.0)
	check(P.call(hot).outcome == "earth_plate_heat", "hot rock: the plate heats")
	var wave := U.lava_wave(h, 20.0, Vector3(-2, 0, 0), Vector3(0, 0, 1), t)
	check(P.call(wave).outcome == "overwhelm", "lava wave: FAIL (jump)")
	var disc := U.shot(h, Sim.Mat.METAL, 2.0, Vector3(3, 1, 0), Vector3(0, 0, 24), t, "disc")
	check(Interactions.allows(disc, &"grip_metal"), "metal: Lodestone Grip may seize it (REC)")
	check(Interactions.predict(h.w, Agent.of_body(h.w, disc, a), Agent.of_move(h.w, a, "lodestone_grip", 0, false)).outcome == "reclaim", "metal x grip_metal: reclaim")
	var ice := U.shot(h, Sim.Mat.WATER, 4.0, Vector3(4, 1, 0), Vector3(0, 0, 24), t, "", -5.0)
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	check(Interactions.predict(h.w, Agent.of_body(h.w, ice, a), Agent.of_move(h.w, a, "chain_arc", 0, false)).outcome == "deflect", "ice: DEF Chain Arc")
	var vine := h.w.spawn_body(Sim.Mat.PLANT, Sim.Form.CHUNK, 6.0, Vector3(5, 1, 0), "test")
	vine.vel = Vector3(0, 0, 10)
	vine.attack_id = h.w.new_attack_id()
	vine.attack_owner = t.id
	check(Interactions.predict(h.w, Agent.of_body(h.w, vine, a), Agent.of_move(h.w, a, "chain_arc", 0, false)).outcome == "shatter", "vines: cut by the chain")
	var fl := Agent.of_volume(h.w, t, null, &"flame", t.chest(), Vector3(0, 0, 1), {"H": 8.0, "heat_hu": 160.0})
	check(Interactions.predict(h.w, fl, Agent.of_guard(h.w, a)).outcome == "earth_plate_heat", "flame: BLK Aegis, the plate heats")
	var sd := Agent.of_volume(h.w, t, null, &"sound", t.chest(), Vector3(0, 0, 1), {"P": 14.0})
	check(Interactions.predict(h.w, sd, Agent.of_guard(h.w, a)).outcome == "reflect", "sound: RFL Aegis (x1.2)")
	var bl := Agent.of_volume(h.w, t, null, &"blast", t.chest(), Vector3(0, 0, 1), {"P": 16.0})
	check(Interactions.predict(h.w, bl, Agent.of_guard(h.w, a)).outcome == "block", "combustion: BLK Aegis")
	var rod := h.w.spawn_zone(&"rod", Vector3(0, 0, 0), 6.0, a.id, 60.0, Sim.Mat.METAL, 3.0)
	h.w.mass_ledger.metal_taken += 3.0
	rod.props["ccls"] = "rod"
	check(U.prv(h, "lightning", {"E": 52.0}, rod).outcome == "earth_rod_ground", "Skybreak E 52: Rod Plant grounds it (cap 60)")
	check(U.prv(h, "lightning", {"E": 70.0}, rod).outcome == "earth_rod_melt", "E 70: over capacity, the rod melts")
	a.status["anchored"] = {"t": -1.0, "mag": 30.0, "src": a.id}
	var tor := Agent.of_volume(h.w, t, null, &"tornado", t.chest(), Vector3(0, 0, 1), {"P": 25.0})
	check(Interactions.predict(h.w, tor, Agent.of_stance(h.w, a)).outcome == "block", "tornado 25: Iron Stance anchor (30) holds")
	h.release(a, "guard")
	check(plate != null, "plate existed")
