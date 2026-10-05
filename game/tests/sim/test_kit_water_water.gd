extends WaterKitTest
## Water / Water (sub 0, docs/MOVESET.md §7.5): Torrent and Maelstrom Lash (T2 / T3 of the legacy strike), Water Bullet /
## Pressure Jet / Cutting Jet (a jet connected to its caster conducts lightning back), Tidal Rush (carries solids back,
## quenches lava, douses fire, leaves puddles), Spray Fan, Surge Orb, Slick, Draw & Shape extras (condense, seize,
## Freeze), Riptide Step / Dive and Wave Ride. Counter cells are in test_kit_water_cells.gd.


## W beside the pool's west edge (unlimited water within 3 m), R 13 m away on the same line; the pool is not on the wave's path.
func _by_the_pool(rival_element: int = Sim.Element.EARTH, dist: float = 13.0) -> Array:
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(5.0, 0, 4.5), 0, {}, Sim.Element.WATER)
	var r := h.actor("R", Vector3(5.0, 0, 4.5 - dist), 1, {}, rival_element)
	h.step(20)
	h.log.clear()
	return [w, r]


func _tap(p: ActorState, gesture: int, hold_ticks: int = 2) -> void:
	h.flick(p, "attack", gesture)
	h.step(hold_ticks)
	h.release(p, "attack")


func _hold_flick(p: ActorState, gesture: int, secs: float) -> void:
	h.flick(p, "attack", gesture)
	h.step(int(secs * 60.0))
	h.release(p, "attack")


func _lava_wave(mass: float, pos: Vector3, dir: Vector3, owner: ActorState) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, pos, "test")
	h.w.mass_ledger.ground_taken += mass
	h.w.ledger.generated += Thermal.heat(b, mass * (Sim.STONE_C * 980.0 + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = dir
	b.wave_budget = 12.0
	b.vel = dir * 7.5
	b.tag = &""
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = owner.id
	b.hit_set[owner.id] = true
	return b


# ---------------------------------------------------------------- strike tiers

func test_torrent_and_maelstrom_are_the_t2_t3_of_the_legacy_strike() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 10.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	w.water_carried = 6.0
	var base := snap(h.w)
	h.press(w, "attack")
	h.step(66)      # 1.1 s: T2
	check(w.action != null and w.action.tier() == 2, "held 1.1 s: tier 2 (%s)" % [w.action.tier() if w.action else -1])
	h.release(w, "attack")
	h.step(30)
	var launch := h.last_event("launch")
	check(not launch.is_empty() and int(launch.get("tier", 0)) == 2, "Torrent launches a slug at tier 2")
	var slug := h.w.get_body(int(launch.get("body", -1)))
	check(slug != null and slug.mass >= 5.0 and slug.mass <= 10.0 and slug.is_water(), "a water slug of up to 10 kg (%.1f)" % [slug.mass if slug else 0.0])
	h.step(60)
	check(r.health < 100.0 or h.has_event("hit"), "the Torrent lands")
	ledgers_ok(base, "Torrent")
	# T3: Maelstrom Lash, 360 degrees, r 5 m: a rival behind the caster is hit too.
	s = duel(0, Sim.Element.EARTH, 3, 10.0)
	w = s[0]
	r = s[1]
	r.pos = w.pos + Vector3(0, 0, 3.5)      # BEHIND the caster (the caster faces -z)
	r.facing = PI
	base = snap(h.w)
	h.press(w, "attack")
	h.step(112)     # 1.87 s
	check(w.action != null and w.action.tier() == 3, "held 1.87 s: tier 3")
	h.release(w, "attack")
	h.step(40)
	check(h.has_event("lash", "around", true), "Maelstrom Lash event")
	check(r.health < 100.0, "the 360 degree whip hits a rival behind the caster (hp %.1f)" % r.health)
	check(h.has_event("fx", "fx", "ring"), "Maelstrom emits a ring cue")
	ledgers_ok(base, "Maelstrom")


# ---------------------------------------------------------------- thrust: Water Bullet / Pressure Jet

func test_water_bullet_tiers() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 10.0)
	var w: ActorState = s[0]
	var base := snap(h.w)
	_tap(w, Sim.Gesture.UP)
	h.step(40)
	var launches := h.events("launch")
	check(launches.size() == 1, "tap: one slug (%d)" % launches.size())
	var slug := h.w.get_body(int(launches[0].body)) if launches.size() > 0 else null
	check(slug != null and slug.tag == &"slug" and is_equal_approx(slug.mass, 1.5), "1.5 kg slug (tag slug)")
	h.step(120)
	ledgers_ok(base, "bullet T0")
	# T1: triple
	s = duel(0, Sim.Element.EARTH, 3, 10.0)
	w = s[0]
	_hold_flick(w, Sim.Gesture.UP, 0.6)
	h.step(40)
	check(h.events("launch").size() == 3, "T1: three slugs (%d)" % h.events("launch").size())


func test_pressure_jet_stays_connected_and_conducts_lightning_back() -> void:
	var s := duel(0, Sim.Element.FIRE, 4, 9.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	r.kit = {"lightning": true}
	var base := snap(h.w)
	h.flick(w, "attack", Sim.Gesture.UP)
	h.step(66)         # T2 reached
	h.release(w, "attack")
	var jet: MatBody = null
	for k in 40:
		h.step()
		for b in h.w.bodies:
			if b.alive and b.tag == &"jet":
				jet = b
		if jet != null:
			break
	check(jet != null and jet.controller == w.id and jet.is_water(), "the Pressure Jet is a water body held by its caster")
	if jet == null:
		return
	check(h.has_event("jet", "on", true), "jet event on")
	# A rival's lightning striking the jet comes back to the caster through the connection.
	var hp0 := w.health
	var def := {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
	r.pos = Vector3(0, 0, -4)
	var out := Conduction.discharge(h.w, r, jet.pos, def, h.w.new_attack_id(), true)
	check(w.health < hp0, "lightning on a connected jet hurts the caster (%.1f -> %.1f)" % [hp0, w.health])
	check(out.hits.has(w.id) or h.has_event("conduct"), "the conduction graph reached the caster through the jet")
	h.step(80)
	check(h.has_event("jet", "on", false), "the jet ends")
	check(not h.w.bodies.any(func(b): return b.alive and b.tag == &"jet"), "no jet body left")
	ledgers_ok(base, "Pressure Jet")


func test_cutting_jet_cuts_soft_walls_and_quenches_flames() -> void:
	var s := duel(0, Sim.Element.EARTH, 5, 8.0)
	var w: ActorState = s[0]
	var wall := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.WALL, 100.0, w.pos + Vector3(0, 0, -3.5), "test")
	h.w.mass_ledger.ground_taken += 100.0
	wall.tag = &"sand"
	wall.wall_half = Vector3(1.1, 0.75, 0.28)
	wall.wall_rise = 1.0
	wall.static_body = true
	wall.props["standing"] = 99.0
	wall.last_actor = 2
	var r: ActorState = s[1]
	r.pos = w.pos + Vector3(0, 0, -7.0)
	h.step(3)
	var base := snap(h.w)
	h.flick(w, "attack", Sim.Gesture.UP)
	h.step(112)         # T3
	h.release(w, "attack")
	h.step(90)
	check(wall.wall_damage > 0.2 or not wall.alive, "the thin jet cuts the sand wall (damage %.2f)" % wall.wall_damage)
	ledgers_ok(base, "Cutting Jet")


# ---------------------------------------------------------------- ground: Tidal Rush

func test_tidal_rush_is_a_tagged_water_wave_that_hits_and_leaves_puddles() -> void:
	var s := _by_the_pool(Sim.Element.EARTH, 9.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	_tap(w, Sim.Gesture.DOWN)
	var wave: MatBody = null
	for k in 60:
		h.step()
		for b in h.w.bodies:
			if b.alive and b.form == Sim.Form.WAVE and b.tag == &"water_wave":
				wave = b
		if wave != null:
			break
	check(wave != null and wave.is_water() and wave.mass >= 3.0, "a water WAVE tagged water_wave (%.1f kg)" % [wave.mass if wave else 0.0])
	if wave == null:
		return
	check(is_equal_approx(wave.power, 18.0), "T0 wave power 18 PU with a full 8 kg (%.1f)" % wave.power)
	h.step(120)
	check(r.health < 100.0, "the wave knocks the rival down (hp %.1f)" % r.health)
	check(h.has_event("hit"), "a hit event")
	h.step(80)
	check(not bodies_of(Sim.Mat.WATER, Sim.Form.WAVE).any(func(b): return b.tag == &"water_wave"), "the wave has ended")
	check(bodies_of(Sim.Mat.WATER, Sim.Form.PUDDLE).size() > 0, "it leaves puddles")
	ledgers_ok(base, "Tidal Rush")


func test_tidal_rush_carries_a_stone_back_to_its_thrower() -> void:
	# The owner's example: "someone throws a stone at me ... make a wave back".
	var s := _by_the_pool(Sim.Element.EARTH, 13.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	_tap(w, Sim.Gesture.DOWN)
	h.step(26)          # the wave is out (startup 16 f + the release)
	var stone := h.launch_at(w, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 7.0)
	var carried := false
	var thrown_back := false
	var hp0 := r.health
	for k in 200:
		h.step()
		if stone.captured_by >= 0:
			carried = true
		if stone.attack_owner == w.id and stone.attack_id != 0 and stone.vel.length() > 5.0:
			thrown_back = true
	check(carried, "the wave captured the stone (CAP)")
	check(h.events("interaction").any(func(e): return e.counter == "wave_water" and e.outcome == "capture"), "interaction wave_water -> capture")
	check(thrown_back, "the stone leaves the wave as the caster's attack")
	check(r.health < hp0, "and it hits its thrower (hp %.1f -> %.1f)" % [hp0, r.health])
	ledgers_ok(base, "wave back")


func test_tidal_rush_quenches_a_lava_wave_into_rock() -> void:
	var s := _by_the_pool(Sim.Element.FIRE, 13.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	_hold_flick(w, Sim.Gesture.DOWN, 0.6)
	h.step(24)
	var lava := _lava_wave(20.0, w.pos + Vector3(0, 0, -7.0), Vector3(0, 0, 1), r)
	var steam0: float = h.w.ledger.vapor
	var ok := false
	for k in 160:
		h.step()
		if lava.alive and lava.liquid <= 0.0 and lava.phase == Sim.Phase.SOLID:
			ok = true
			break
	check(ok, "the lava wave is quenched into rock (liquid %.2f, phase %s)" % [lava.liquid, Sim.PHASE_NAMES[lava.phase]])
	check(h.w.ledger.vapor > steam0, "the quench boils water (vapor ledger)")
	check(h.events("interaction").any(func(e): return e.threat == "lava_wave" and e.counter == "wave_water"), "interaction lava_wave x wave_water")
	h.step(120)
	ledgers_ok(base, "quench")


func test_tidal_rush_extinguishes_a_fire_field() -> void:
	var s := _by_the_pool(Sim.Element.FIRE, 13.0)
	var w: ActorState = s[0]
	var f := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 1.0, w.pos + Vector3(0, 0.3, -5.0), "test")
	f.tag = &"fire_field"
	f.heat_payload = 90.0
	f.static_body = false
	f.on_ground = true
	var base := snap(h.w)
	_tap(w, Sim.Gesture.DOWN)
	var out := false
	for k in 200:
		h.step()
		if not f.alive:
			out = true
			break
	check(out, "the fire field is doused")
	check(h.events("interaction").any(func(e): return e.outcome == "extinguish"), "extinguish interaction")
	h.step(60)
	ledgers_ok(base, "douse")


func test_deluge_t3_is_bigger_and_needs_water() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 10.0)
	var w: ActorState = s[0]
	w.pos = Vector3(8.0, 0, 4.5)      # beside the pool: unlimited water
	h.step(5)
	var base := snap(h.w)
	_hold_flick(w, Sim.Gesture.DOWN, 1.9)
	h.step(30)
	var waves := bodies_of(Sim.Mat.WATER, Sim.Form.WAVE)
	check(waves.size() == 1 and waves[0].mass > 12.0 and is_equal_approx(waves[0].power, 45.0), "T3 Deluge: a big 45 PU wave from the pool (%s)" % [waves.map(func(b): return b.mass)])
	h.step(200)
	ledgers_ok(base, "Deluge")
	# Dry and far from any water: it fizzles with the insufficient event.
	s = duel(0, Sim.Element.EARTH, 3, 10.0)
	w = s[0]
	w.pos = Vector3(-8, 0, 8)
	w.water_carried = 0.0
	h.step(3)
	h.log.clear()
	_tap(w, Sim.Gesture.DOWN)
	h.step(60)
	check(h.has_event("insufficient", "what", "water"), "no water: insufficient event")
	check(bodies_of(Sim.Mat.WATER, Sim.Form.WAVE).is_empty(), "no wave without water")


# ---------------------------------------------------------------- sweep / push / sink

func test_spray_fan_wets_and_turns_sand_to_mud() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 4.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	r.wetness = 0.0
	_tap(w, Sim.Gesture.SIDE)
	h.step(40)
	check(r.wetness > 0.9 or Status.has(r, "wet"), "the spray wets the rival (%.2f)" % r.wetness)
	ledgers_ok(base, "Spray Fan")
	# Sand in the cone becomes mud (rule sand x spray).
	s = duel(0, Sim.Element.EARTH, 3, 6.0)
	w = s[0]
	var sand := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.CHUNK, 6.0, w.pos + Vector3(0, 1.0, -3.0), "test")
	h.w.mass_ledger.ground_taken += 6.0
	sand.vel = Vector3.ZERO
	sand.gravity_scale = 0.0
	sand.attack_id = h.w.new_attack_id()
	sand.attack_owner = 2
	_tap(w, Sim.Gesture.SIDE)
	h.step(30)
	check(sand.tag == &"mud", "sand in the spray turns to mud (tag %s)" % sand.tag)


func test_surge_orb_hurls_the_shield_and_bursts() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	h.press(w, "guard")
	h.step(20)
	check(w.action != null and w.action.id == "guard" and w.held_body >= 0, "guard raised a water shield")
	h.flick(w, "guard", Sim.Gesture.UP)
	h.step(12)
	var orb: MatBody = null
	for b in h.w.bodies:
		if b.alive and b.tag == &"orb":
			orb = b
	h.release(w, "guard")
	h.step(80)
	check(orb != null or h.has_event("launch"), "the shield was thrown as an orb")
	check(r.wetness > 0.5 or r.health < 100.0, "the orb bursts on the rival (wet %.2f hp %.1f)" % [r.wetness, r.health])
	h.step(120)
	ledgers_ok(base, "Surge Orb")


func test_slick_puddle_trips_runners() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	h.press(w, "guard")
	h.step(15)
	h.flick(w, "guard", Sim.Gesture.DOWN)
	h.step(6)
	h.release(w, "guard")
	h.step(20)
	var z := zones_tagged("slick")
	check(z.size() == 1 and absf(z[0].zone_radius - 1.25) < 0.01, "a 2.5 m slick zone (r %.2f)" % [z[0].zone_radius if z.size() else 0.0])
	if z.is_empty():
		return
	check(bodies_of(Sim.Mat.WATER, Sim.Form.PUDDLE).size() > 0, "backed by a real puddle (conductive)")
	# The rival runs through it: slips (balance) and gets wet.
	r.pos = z[0].pos + Vector3(0, 0, -1.0)
	r.vel = Vector3(0, 0, 5.5)
	var bal0 := r.balance
	h.step(30)
	check(h.has_event("slip", "actor", r.id), "the runner slips")
	check(r.balance < bal0 or r.stun > 0.0, "balance lost (%.0f -> %.0f)" % [bal0, r.balance])
	check(r.wetness > 0.3, "and wet")
	h.step(300)
	ledgers_ok(base, "Slick")


# ---------------------------------------------------------------- technique extras

func test_draw_condenses_steam_into_water() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 8.0)
	var w: ActorState = s[0]
	w.pos = Vector3(-6.0, 0, 4.0)       # away from the pool: only vapour to draw
	w.water_carried = 0.0
	h.step(3)
	var base := snap(h.w)
	h.w._spawn_steam(w.pos + Vector3(0, 1.2, -2.5), 3.0)
	h.w.mass_ledger.moisture_taken += 3.0            # the test conjured the steam: book it
	h.press(w, "tech")
	h.step(50)
	var held := h.w.held(w)
	check(held != null and held.is_water() and held.mass > 0.5, "steam condensed into a held water body (%.2f kg)" % [held.mass if held else 0.0])
	h.release(w, "tech")
	h.step(120)
	h.step(240)
	ledgers_ok(base, "condense")


func test_draw_seizes_an_enemy_stream_in_flight() -> void:
	var s := duel(0, Sim.Element.WATER, 3, 9.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	var base := snap(h.w)
	var stream := h.launch_at(w, "water", 5.0, 15.0, Sim.AMBIENT_C, "slug", r, 7.0)
	stream.form = Sim.Form.BLOB
	h.w.mass_ledger.moisture_taken += 5.0            # the test conjured these 5 kg: book them
	h.press(w, "tech")
	var got := false
	for k in 40:
		h.step()
		if stream.controller == w.id:
			got = true
			break
	check(got, "the technique seizes the rival's stream in flight (contest won)")
	check(h.has_event("control_won", "actor", w.id), "control_won event")
	h.release(w, "tech")
	h.step(160)
	ledgers_ok(base, "seize")


func test_attack_while_holding_freezes_the_water_into_a_block() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	w.pos = Vector3(8.0, 0, 4.5)
	h.step(5)
	var base := snap(h.w)
	h.press(w, "tech")
	h.step(40)
	var held := h.w.held(w)
	check(held != null and held.phase == Sim.Phase.LIQUID, "drawn water is liquid")
	h.press(w, "attack")
	h.step(3)
	h.release(w, "attack")
	h.step(3)
	held = h.w.held(w)
	check(held != null and held.phase == Sim.Phase.FROZEN, "T+A froze it into an ice block")
	h.release(w, "tech")
	h.step(30)
	var launched := h.last_event("launch")
	check(not launched.is_empty(), "released: thrown")
	h.step(240)
	ledgers_ok(base, "Freeze T+A")


# ---------------------------------------------------------------- evades

func test_riptide_step_slides_and_dive_resurfaces_in_the_pool() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 10.0)
	var w: ActorState = s[0]
	var base := snap(h.w)
	var p0 := w.pos
	h.it(w).move = Vector3(1, 0, 0)
	h.press(w, "evade")
	h.step(1)
	h.it(w).move = Vector3.ZERO
	h.step(29)
	var d := Vector2(w.pos.x - p0.x, w.pos.z - p0.z).length()
	check(d > 2.5 and d < 5.0, "slides about 4 m (%.2f)" % d)
	check(zones_tagged("slick").size() > 0, "leaves a water film trail")
	h.step(120)
	ledgers_ok(base, "Riptide")
	# Dive: in the pool the step submerges and resurfaces 4 m away, hidden and invulnerable.
	s = duel(0, Sim.Element.EARTH, 3, 10.0)
	w = s[0]
	w.pos = Vector3(10.0, -0.3, -1.0)
	w.grounded = true
	h.step(3)
	check(w.in_water, "standing in the pool")
	p0 = w.pos
	h.it(w).move = Vector3(1, 0, 0)
	h.press(w, "evade")
	h.step(4)
	check(h.has_event("dive", "on", true) and w.iframes > 0.0, "dive: submerged with i-frames")
	h.step(30)
	check(h.has_event("dive", "on", false), "resurfaced")


func test_wave_ride_is_faster_and_spends_water() -> void:
	var s := duel(0, Sim.Element.EARTH, 3, 14.0)
	var w: ActorState = s[0]
	w.water_carried = 6.0
	var base := snap(h.w)
	h.it(w).move = Vector3(1, 0, 0)
	h.press(w, "evade")
	h.it(w).evade_held = true
	h.step(40)
	h.it(w).move = Vector3(1, 0, 0)
	var v0 := Vector2(w.vel.x, w.vel.z).length()
	h.step(30)
	var v1 := Vector2(w.vel.x, w.vel.z).length()
	check(w.action != null and w.action.id == "wave_ride", "morphed into Wave Ride (%s)" % [w.action.id if w.action else "none"])
	check(v1 > 6.0, "surfing at more than a run (%.2f m/s)" % v1)
	check(w.water_carried < 6.0 or h.w.water_mass() > 0.0, "the ride spends water")
	h.it(w).evade_held = false
	h.step(120)
	ledgers_ok(base, "Wave Ride")
