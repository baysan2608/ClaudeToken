extends AirKitTest
## Air / Vacuum (sub 2): Pressure Palm -> Air Cannon -> Implode -> Collapse, Suction Line, Pressure Mine, Vacuum Arc,
## Null Bubble (+ Vacuum Catch), Pressure Wave, Anchor, Vacuum Well (+ collapse and the air-inrush flag), Pressure Hop,
## Slipstream, the Vacuum column of the counter matrix.

const MOVES := ["vacuum_palm", "vacuum_suction", "vacuum_mine", "vacuum_arc", "vacuum_bubble", "vacuum_wave", "vacuum_anchor", "vacuum_well",
	"vacuum_hop", "vacuum_slipstream"]


func _duel(dist: float = 10.0) -> Array:
	return duel(2, Sim.Element.EARTH, 3, dist)


func _fire_body(pos: Vector3, hu: float = 120.0, owner: ActorState = null) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 0.5, pos, "test")
	b.heat_payload = hu
	h.w.ledger.generated += hu
	b.tag = &"fireball"
	b.gravity_scale = 0.0
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = owner.id if owner != null else -1
	if owner != null:
		b.hit_set[owner.id] = true
	return b


func test_every_vacuum_move_has_a_def_and_a_binding() -> void:
	Moves.ensure()
	var clips: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://assets/characters/fighter_clips.json"))
	for id in MOVES:
		check(Moves.DEFS.has(id), "%s registered" % id)
		if not Moves.DEFS.has(id):
			continue
		var d: Dictionary = Moves.DEFS[id]
		for k in ["name", "desc", "slot", "sub", "element", "startup", "recovery", "cost", "anim", "fx", "ai"]:
			check(d.has(k), "%s has %s" % [id, k])
		check(int(d.sub) == 2 and int(d.element) == 3, "%s is Air/Vacuum" % id)
		for ck in ["anim", "anim_active", "anim_hold"]:
			if d.has(ck):
				check(clips.has(String(d[ck])), "%s: clip %s exists" % [id, d[ck]])
		if ["strike", "thrust", "ground", "sweep", "tech"].has(String(d.slot)):
			check(d.has("tiers") and (d.tiers as Dictionary).has("t3"), "%s has tiers up to t3" % id)
			check(d.has("counter") and d.has("threat"), "%s has counter + threat" % id)
		check(Moves.slot_of(3, 2, id) == String(d.slot), "%s bound to %s" % [id, d.slot])
	for slot in Sim.SLOTS:
		var id := Moves.resolve(3, 2, slot)
		check(id != "" and int(Moves.DEFS[id].get("sub", 0)) == 2, "Air/Vacuum %s bound (%s)" % [slot, id])


func test_pressure_palm_air_cannon_implode_collapse() -> void:
	var s := _duel(6.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	r.pos = a.pos + Vector3(0, 0, -1.8)
	var hp := r.health
	run_move(a, "vacuum_palm", 0, 1, 24)
	check(r.health < hp and r.balance < 100.0, "T0 Pressure Palm: point-blank burst (balance %.0f)" % r.balance)
	check(h.events("fx").any(func(e): return e.fx == "burst" and e.mat == "vacuum"), "burst fx (vacuum)")
	# T1 Air Cannon: a pressure bullet 12 m
	s = _duel(10.0)
	a = s[0]
	r = s[1]
	hp = r.health
	run_move(a, "vacuum_palm", 1, 30, 30)
	check(r.health < hp, "T1 Air Cannon reaches 10 m (%.0f)" % r.health)
	check(h.events("fx").any(func(e): return e.fx == "beam" and e.mat == "vacuum"), "beam fx")
	# T2 Implode / T3 Collapse
	for tier in [2, 3]:
		s = _duel(9.0)
		a = s[0]
		r = s[1]
		var base := snap(h.w)
		var b0 := r.balance
		var pos0 := r.pos
		var holder := {}
		var spawn := func() -> void:
			pass
		run_move(a, "vacuum_palm", tier, 30, 13)
		var wells := zones_tagged("vacuum_well")
		check(wells.size() == 1 and wells[0].owner == a.id, "T%d: a vacuum point" % tier)
		if wells.size() == 1:
			near(wells[0].power, 22.0 if tier == 2 else 30.0, 1e-6, "T%d power" % tier)
			near(wells[0].zone_radius, 3.0 if tier == 2 else 4.0, 1e-6, "T%d radius" % tier)
		h.step(40)
		check(zones_tagged("vacuum_well").is_empty(), "T%d: it collapsed after ~0.4 s" % tier)
		check(h.has_event("collapse"), "collapse event")
		check(not zones_tagged("inrush").is_empty() or h.has_event("inrush"), "and left an air inrush")
		check(r.balance < b0 or r.health < 100.0, "T%d crushed the rival (balance %.0f health %.0f)" % [tier, r.balance, r.health])
		ledgers_ok(base, "implode T%d" % tier, 1e-5)
		fx_catalogued("palm T%d" % tier)


func test_suction_line_pulls_a_fighter_and_yanks_a_light_projectile() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var d0 := a.pos.distance_to(r.pos)
	run_move(a, "vacuum_suction", 0, 1, 40)
	var d1 := a.pos.distance_to(r.pos)
	check(d0 - d1 > 1.2 and d0 - d1 < 3.2, "pulls the rival ~2 m toward A (%.1f -> %.1f)" % [d0, d1])
	# a 6 kg metal plate in flight is yanked into the hand (<= 12 kg)
	s = _duel(10.0)
	a = s[0]
	r = s[1]
	var holder := {}
	var spawn := func() -> void:
		holder["plate"] = h.launch_at(a, "metal", 6.0, 8.0, Sim.AMBIENT_C, "", r, 6.0)
		h.w.mass_ledger.metal_taken += 6.0
		holder["base"] = snap(h.w)
	run_when(a, "vacuum_suction", 0, spawn, 1, 20)
	var plate: MatBody = holder.plate
	check(h.w.held(a) == plate, "the 6 kg plate is reclaimed into A's hand")
	ledgers_ok(holder.base, "yank", 1e-6)
	# a 20 kg stone (> 12 kg) is not yanked
	s = _duel(10.0)
	a = s[0]
	r = s[1]
	var holder2 := {}
	var spawn2 := func() -> void:
		holder2["stone"] = h.launch_at(a, "stone", 20.0, 8.0, Sim.AMBIENT_C, "", r, 6.0)
	run_when(a, "vacuum_suction", 0, spawn2, 1, 20)
	check(h.w.held(a) != holder2.stone, "a 20 kg stone is too heavy to yank")


func test_suction_line_pulls_you_to_a_wall() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	r.pos = Vector3(8, 0, -8)           # out of the line
	var wall := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, 120.0, a.pos + a.forward() * 6.0, "test")
	h.w.mass_ledger.ground_taken += 120.0
	wall.wall_yaw = a.facing
	wall.wall_half = Vector3(1.1, 0.75, 0.28)
	wall.wall_rise = 1.0
	wall.static_body = true
	wall.props["standing"] = 999.0
	h.step(5)
	var d0 := a.pos.distance_to(wall.pos)
	h.aim(a, Vector3(0, 0, -1))
	run_move(a, "vacuum_suction", 0, 1, 40)
	check(h.has_event("grapple"), "no fighter in line: the beam anchors on the wall")
	check(a.pos.distance_to(wall.pos) < d0 - 2.0, "A is pulled toward the wall (%.1f -> %.1f)" % [d0, a.pos.distance_to(wall.pos)])


func test_pressure_mine_launches_the_first_fighter_who_steps_on_it() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	run_move(a, "vacuum_mine", 0, 1, 14)
	var ms := zones_tagged("mine")
	check(ms.size() == 1 and ms[0].owner == a.id and absf(ms[0].zone_radius - 1.2) < 1e-6, "a mine (zone r 1.2 m)")
	check(ms[0].max_life == 10.0, "lives 10 s")
	h.step(40)
	check(a.health == 100.0 and not h.has_event("mine_burst"), "it ignores its owner and stays armed")
	r.pos = ms[0].pos + Vector3(0.3, 0, 0)
	h.step(4)
	check(h.has_event("mine_burst"), "stepped on: it bursts")
	h.step(10)
	check(r.pos.y > 0.4 or r.vel.y > 3.0 or r.health < 100.0, "the rival is launched (vy %.1f y %.2f)" % [r.vel.y, r.pos.y])
	check(zones_tagged("mine").is_empty(), "and the mine is spent")


func test_vacuum_arc_snuffs_flames_and_ledgers_balance() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var ball := _fire_body(Vector3(0, 1.2, a.pos.z - 2.5), 120.0, r)
	var base := snap(h.w)
	run_move(a, "vacuum_arc", 0, 1, 40)
	check(not ball.alive or ball.heat_payload < 1.0, "the arc puts the fireball out (x2)")
	check(h.events("interaction").any(func(e): return String(e.counter) == "vacuum" and e.outcome == "extinguish"), "interaction vacuum x flame -> extinguish")
	ledgers_ok(base, "arc", 1e-5)
	check(a.action == null, "ends cleanly")


func test_null_bubble_snuffs_fire_nullifies_sound_and_lets_solids_pass() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	h.press(a, "guard")
	h.step(20)
	var bs := zones_tagged("null_bubble")
	check(bs.size() == 1 and bs[0].owner == a.id and absf(bs[0].zone_radius - 1.8) < 1e-6, "Null Bubble: a vacuum shell r 1.8 m")
	check(bs.size() == 1 and bs[0].props.get("barrier", false), "it insulates (blocks bolts)")
	var base := snap(h.w)
	var fire := _fire_body(Vector3(0, 1.2, a.pos.z - 5.0), 100.0, r)
	fire.vel = Vector3(0, 0, 8.0)
	base = snap(h.w)
	h.step(40)
	check(not fire.alive, "a fireball dies at the shell")
	check(a.health == 100.0, "A is unhurt")
	ledgers_ok(base, "bubble vs fire", 1e-5)
	# a stone passes through (solids pass) and hits - a plain bubble is not a wall
	var stone := h.launch_at(a, "stone", 20.0, 14.0, Sim.AMBIENT_C, "", r, 6.0)
	h.step(40)
	check(stone.attack_id == 0 and a.health < 100.0 and stone.captured_by < 0, "a stone passes the bubble and lands (solids pass)")
	# steam collapses into water
	h.log.clear()
	var steam := h.w.spawn_body(Sim.Mat.STEAM, Sim.Form.CLOUD, 0.5, a.pos + Vector3(0, 1.2, -0.5), "test")
	h.w.mass_ledger.vapor += 0.0
	steam.max_life = 5.0
	var w0 := h.w.water_mass()
	h.step(20)
	check(steam.mat == Sim.Mat.WATER and steam.form != Sim.Form.CLOUD, "steam collapses into water drops")
	near(h.w.water_mass(), w0, 1e-6, "water mass conserved")


func test_vacuum_catch_spits_a_light_projectile_back() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var holder := {}
	holder["stone"] = h.launch_at(a, "stone", 20.0, 12.0, Sim.AMBIENT_C, "", r, 4.8)
	h.step(4)
	h.press(a, "guard")
	h.step(40)
	var st: MatBody = holder.stone
	check(h.has_event("vacuum_catch"), "Vacuum Catch")
	check(st.attack_owner == a.id and st.attack_id != 0, "the stone is A's attack now")
	check(st.vel.dot(r.pos - st.pos) > 0.0 or st.attack_id == 0, "spat back at the thrower")
	check(a.health == 100.0, "A is unhurt")


func test_pressure_wave_and_anchor_from_the_null_bubble() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	r.pos = a.pos + Vector3(0, 0, -2.0)
	h.press(a, "guard")
	h.step(20)
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step(3)
	check(a.action != null and a.action.id == "vacuum_wave", "guard flick up = Pressure Wave")
	h.release(a, "guard")
	var d0 := a.pos.distance_to(r.pos)
	h.step(40)
	check(r.pos.distance_to(a.pos) > d0 + 0.8 or r.health < 100.0, "the ring shoves the rival back (%.1f -> %.1f)" % [d0, r.pos.distance_to(a.pos)])
	check(zones_tagged("null_bubble").is_empty(), "the bubble collapsed into the wave")
	# Anchor: immune to knockback / pull / lift
	s = _duel(10.0)
	a = s[0]
	h.press(a, "guard")
	h.step(20)
	h.flick(a, "guard", Sim.Gesture.DOWN)
	h.step(8)
	check(a.action != null and a.action.id == "vacuum_anchor" and a.anchored, "guard flick down = Anchor (stance anchored)")
	check(Status.immune(a, "lift") and Status.immune(a, "pull") and Status.immune(a, "knockback"), "immune to lift, pull and knockback")
	var p0 := a.pos
	h.w.hit_actor(a, {"attacker": 99, "attack_id": h.w.new_attack_id(), "damage": 4.0, "balance": 10.0, "knock": Vector3(0, 0, 9.0), "kind": "air", "from": a.pos + Vector3(0, 0, -2)})
	h.step(20)
	check(a.pos.distance_to(p0) < 0.8, "anchored: not knocked around (%.2f m)" % a.pos.distance_to(p0))
	h.release(a, "guard")
	h.step(30)
	check(not a.anchored, "released: anchor off")


func test_vacuum_well_pulls_compresses_drags_and_collapses_with_an_inrush() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	h.aim(a, Vector3(0, 0, -1))
	h.press(a, "tech")
	h.step(30)
	var wells := zones_tagged("vacuum_well")
	check(wells.size() == 1 and wells[0].owner == a.id, "Vacuum Well at the aim point")
	var well: MatBody = wells[0]
	var dist := Vector2(well.pos.x - a.pos.x, well.pos.z - a.pos.z).length()
	check(dist > 6.0 and dist <= 9.5, "9 m range (%.1f)" % dist)
	# steam -> water (booked), sand -> sandstone (booked)
	var base := snap(h.w)
	var steam := h.w.spawn_body(Sim.Mat.STEAM, Sim.Form.CLOUD, 0.6, well.pos + Vector3(0.5, 1.2, 0), "test")
	steam.max_life = 8.0
	var sand := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.CLOUD, 3.0, well.pos + Vector3(-0.5, 1.2, 0), "test")
	h.w.mass_ledger.ground_taken += 3.0
	sand.max_life = 8.0
	base = snap(h.w)
	h.step(30)
	check(steam.mat == Sim.Mat.WATER, "steam is compressed into water")
	check(sand.mat == Sim.Mat.STONE and sand.tag == &"sandstone", "sand is compressed into sandstone")
	ledgers_ok(base, "well compress", 1e-5)
	# fire in the well goes out
	var fire := _fire_body(well.pos + Vector3(0.4, 1.2, 0.4), 60.0, r)
	h.step(20)
	check(not fire.alive, "fire is suppressed in the well")
	# a stone is pulled out of the air and hangs in the well
	var stone := h.launch_at(a, "stone", 20.0, 12.0, Sim.AMBIENT_C, "", r, 0.0)
	stone.pos = well.pos + Vector3(2.5, 1.2, 0)
	stone.vel = Vector3(-4, 0, 0)
	h.step(40)
	check(stone.captured_by == well.id, "a stone is caught in the well")
	# a fighter near it is dragged in
	r.pos = well.pos + Vector3(3.5, 0, 0)
	var d0 := Vector2(r.pos.x - well.pos.x, r.pos.z - well.pos.z).length()
	h.step(30)
	var d1 := Vector2(r.pos.x - well.pos.x, r.pos.z - well.pos.z).length()
	check(d1 < d0 - 0.5, "the rival is dragged toward the well (%.1f -> %.1f)" % [d0, d1])
	# release: collapse + inrush
	h.log.clear()
	h.release(a, "tech")
	h.step(4)
	check(h.has_event("collapse"), "release collapses the well")
	check(not zones_tagged("inrush").is_empty(), "and leaves an air-inrush zone")
	check(AirUtil.inrush_tick_at(h.w, well.pos) >= 0, "the inrush flag Fire reads (AirUtil.inrush_tick_at)")
	check(stone.captured_by < 0, "the captured stone is released")
	h.step(60)
	check(zones_tagged("inrush").is_empty(), "the inrush fades (0.6 s)")
	check(a.action == null, "ends cleanly")


func test_pressure_hop_and_slipstream() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	h.it(a).move = Vector3(1, 0, 0)
	h.press(a, "evade")
	h.step(4)
	check(a.action != null and a.action.id == "vacuum_hop", "the evade is the Pressure Hop")
	var peak := 0.0
	for k in 90:
		h.step()
		peak = maxf(peak, a.pos.y)
	check(peak > 0.8, "leaps up (peak %.2f m)" % peak)
	check(a.grounded and a.action == null, "lands, action over")
	# Slipstream
	s = _duel(14.0)
	a = s[0]
	var r: ActorState = s[1]
	h.it(a).move = Vector3(0, 0, 1)
	h.step(30)
	h.press(a, "evade")
	h.it(a).evade_held = true
	h.step(16)
	check(a.action != null and a.action.id == "vacuum_slipstream", "held 0.2 s: Slipstream")
	var stone := h.launch_at(a, "stone", 20.0, 12.0, Sim.AMBIENT_C, "", r, 2.5)
	stone.pos = a.pos + Vector3(0, 1.2, 2.5)
	stone.vel = Vector3(0, 0, 12.0)
	h.step(10)
	check(stone.vel.length() < 12.0 * 0.9, "a projectile behind the runner is slowed (%.1f m/s)" % stone.vel.length())
	h.it(a).evade_held = false
	h.it(a).move = Vector3.ZERO
	h.step(30)


func test_vacuum_column_cells_at_reference_powers() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var bub := Agent.of_move(h.w, a, "vacuum_bubble", 0, false)
	check(bub.ccls == &"bubble_null" and absf(bub.power - 14.0) < 1e-6, "Null Bubble CP 14")
	check(Interactions.predict(h.w, threat(h.w, "flame", 8.0, 0.0, "H"), bub).outcome == "extinguish", "flame: EXT (x2)")
	check(Interactions.predict(h.w, threat(h.w, "blast", 24.0, 0.0, "P"), bub).outcome == "extinguish", "a 24 PU combustion: ratio 1.17 EXT")
	check(Interactions.predict(h.w, threat(h.w, "blast", 38.0, 0.0, "P"), bub).outcome == "weaken", "a T3 detonation (38) only weakens: ratio 0.74")
	check(Interactions.predict(h.w, threat(h.w, "sound", 16.0, 0.0, "P"), bub).outcome == "absorb", "sound x3: nullified")
	check(Interactions.predict(h.w, threat(h.w, "sound", 32.0, 0.0, "P"), bub).outcome == "absorb", "Resonance 32: 42 vs 32 still nullified")
	check(Interactions.predict(h.w, threat(h.w, "sound", 60.0, 0.0, "P"), bub).outcome == "weaken", "but 60 PU of sound only weakens")
	# lightning: E <= 1.5 x CP = 21 is blocked, above it the bolt is weakened (bands)
	var bolt := threat(h.w, "lightning", 20.0, 0.0, "E")
	check(Interactions.predict(h.w, bolt, bub).outcome == "block", "a 20 PU bolt (<= 21) is blocked")
	var big := Interactions.predict(h.w, threat(h.w, "lightning", 30.0, 0.0, "E"), bub)
	check(big.outcome == "weaken" and big.band == "partial", "a 30 PU bolt: ratio %.2f -> weakened" % big.ratio)
	check(Interactions.predict(h.w, threat(h.w, "lightning", 52.0, 0.0, "E"), bub).outcome == "weaken" or Interactions.predict(h.w, threat(h.w, "lightning", 52.0, 0.0, "E"), bub).outcome == "pass", "Skybreak (52) is not stopped")
	for t in ["stone", "stone_heavy", "boulder", "lava_wave", "water", "sand_surge", "metal"]:
		check(Interactions.predict(h.w, threat(h.w, t, 17.0, 20.0), bub).outcome == "pass", "%s passes the vacuum" % t)
	check(Interactions.predict(h.w, threat(h.w, "mist", 3.0, 2.0), bub).outcome == "air_compress", "mist collapses")
	check(Interactions.predict(h.w, threat(h.w, "steam", 6.0, 1.0), bub).outcome == "air_compress", "steam collapses")
	# a perfect bubble spits a light projectile back
	var perfect := Agent.of_move(h.w, a, "vacuum_bubble", 0, true)
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), perfect).outcome == "air_spit", "Vacuum Catch")
	# the well
	var well := Agent.of_move(h.w, a, "vacuum_well", 1, false)
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), well).outcome == "capture", "stone x Well T1 (14 / 17 = 0.82): captured")
	check(Interactions.predict(h.w, threat(h.w, "stone_heavy", 31.5, 45.0), well).outcome == "weaken", "heavy stone: weakened")
	check(Interactions.predict(h.w, threat(h.w, "boulder", 110.0, 200.0), Agent.of_move(h.w, a, "vacuum_well", 3, false)).outcome == "pass", "boulder: nothing")
	check(Interactions.predict(h.w, threat(h.w, "magma", 35.0, 20.0), well).outcome == "pass", "magma passes the well")
	# the suction line
	var suc := Agent.of_move(h.w, a, "vacuum_suction", 0, false)
	var plate := Interactions.predict(h.w, threat(h.w, "metal", 12.0, 6.0), suc)
	check(plate.outcome == "reclaim", "metal <= 12 kg: reclaimed (%s)" % plate.outcome)
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), suc).outcome == "bend", "a 20 kg stone only bends")
	# the arc
	var arc := Agent.of_move(h.w, a, "vacuum_arc", 0, false)
	check(Interactions.predict(h.w, threat(h.w, "flame", 8.0, 0.0, "H"), arc).outcome == "extinguish", "Vacuum Arc: flames out (x2)")
	check(Interactions.predict(h.w, threat(h.w, "sound", 8.0, 0.0, "P"), arc).outcome == "absorb", "Vacuum Arc silences sound")


func test_vacuum_kit_is_deterministic_and_ledgers_balance() -> void:
	var hashes := []
	for k in 2:
		var s := _duel(9.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		var base := snap(h.w)
		h.launch_at(a, "stone", 20.0, 10.0, Sim.AMBIENT_C, "", r, 6.0)
		h.aim(a, Vector3(0, 0, -1))
		h.hold(a, "tech", 50)
		h.step(40)
		run_move(a, "vacuum_palm", 2, 30, 40)
		ledgers_ok(base, "vacuum exchange", 1e-5)
		finite_world("vacuum")
		hashes.append("%s|%s|%d|%.4f|%.4f" % [str(a.pos), str(r.pos), h.w.bodies.size(), a.focus, r.health])
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: %s vs %s" % [hashes[0], hashes[1]])
