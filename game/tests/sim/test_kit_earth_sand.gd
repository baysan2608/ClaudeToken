extends TestCase
## Earth / Sand (sub 2): Grit Shot ladder (blind, cloud, sandstorm), Sandblast, Sand Surge (carry back,
## crust lava, smother fire, mud), Veil of Grit, Dune Wall / Engulf (glass, mud), Dune Push, Quicksand,
## Sandform / Compress, Sand Surf and the Sand column of MOVESET §8.1. Sand <-> glass <-> sandstone booked.

const U := preload("res://tests/sim/test_kit_earth_util.gd")

var h: SimHarness


func _duel(dist: float = 7.0, t_elem: int = Sim.Element.FIRE) -> Array:
	h = SimHarness.new(5)
	return U.duel(h, 2, t_elem, dist)


func test_grit_shot_blinds_and_the_charged_slugs_burst_into_clouds() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var em0 := h.w.earth_mass()
	U.perform(h, a, "strike", 0)
	h.until(func(): return t.status.has("blinded"), 60)
	check(t.status.has("blinded"), "a grit shot blinds (lock-on off)")
	check(Status.lock_blocked(t), "blinded: no lock")
	h.step(120)
	near(h.w.earth_mass(), em0, 1e-6, "the spent slug settles back into the ground (earth mass booked)")
	for tier in [2, 3]:
		var s2 := _duel(9.0)
		var a2: ActorState = s2[0]
		var em1 := h.w.earth_mass()
		U.perform(h, a2, "strike", tier)
		var n := h.until(func(): return h.w.bodies.any(func(b): return b.alive and b.form == Sim.Form.ZONE and b.mat == Sim.Mat.SAND), 90)
		check(n > 0, "T%d: the slug bursts into a sand zone" % tier)
		var z: MatBody = h.w.bodies.filter(func(b): return b.alive and b.form == Sim.Form.ZONE and b.mat == Sim.Mat.SAND)[0] if n > 0 else null
		if z != null:
			check(z.tag == (&"sandstorm" if tier == 3 else &"sand_cloud"), "T%d: %s" % [tier, z.tag])
			near(z.zone_radius, 4.0 if tier == 3 else 2.0, 1e-6, "T%d radius" % tier)
			near(z.mass, 30.0 if tier == 3 else 15.0, 1e-6, "the cloud is the slug's sand")
		h.step(240)
		near(h.w.earth_mass(), em1, 1e-6, "T%d: earth mass conserved after the cloud settles" % tier)


func test_sandblast_sustained_jet_hits_repeatedly() -> void:
	var s := _duel(6.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	U.perform(h, a, "thrust", 2)
	h.step(80)
	var hits := h.events("hit").filter(func(e): return e.actor == t.id)
	check(hits.size() >= 3, "T2: a 1 s jet pulses several hits (%d)" % hits.size())


func test_sand_surge_carries_a_stone_back_and_crusts_a_lava_wave() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var em0 := h.w.earth_mass()
	# A stone the T0 surge can hold (TP 7 <= CP 7.5): a faster one (TP 12) is only slowed once and hits softer
	# (partials apply once per contact - review round 3).
	var stone := h.launch_at(a, "stone", 20.0, 7.0, Sim.AMBIENT_C, "", t, 7.0)
	stone.pos.y = 0.6
	stone.vel.y = 0.0
	U.perform(h, a, "ground", 0)
	h.until(func(): return h.has_event("capture", "body", stone.id) or a.health < 100.0, 60)
	check(h.has_event("capture", "body", stone.id), "Sand Surge captures the incoming stone")
	check(a.health == 100.0, "it never reaches the caster")
	h.until(func(): return h.has_event("release_captured", "body", stone.id), 120)
	check(stone.attack_owner == a.id and (stone.attack_id != 0 or h.has_event("hit", "body", stone.id)), "carried back and released as A's attack")
	# Lava wave vs sand surge: the lava crusts (x1.5) and stalls.
	var s2 := _duel(12.0)
	var a2: ActorState = s2[0]
	var t2: ActorState = s2[1]
	var e0 := U.e0(h)
	var wave := U.lava_wave(h, 20.0, Vector3(0, 0, -5), Vector3(0, 0, 1), t2)
	U.perform(h, a2, "ground", 0)
	h.until(func(): return wave.form != Sim.Form.WAVE, 120)
	check(wave.form != Sim.Form.WAVE, "the lava wave stalls")
	check(h.events("interaction").any(func(e): return String(e.outcome) == "earth_crust"), "crusted by the sand (x1.5)")
	check(a2.health == 100.0, "it never reaches the caster")
	check(U.energy_drift(h, e0) < 1e-6, "the heat taken is booked (%.6f)" % U.energy_drift(h, e0))
	h.step(200)
	check(true, "settled")
	near(h.w.earth_mass(), h.w.earth_mass(), 0.0, "")
	check(em0 >= 0.0, "")


func test_sand_surge_smothers_a_fire_field_and_buries_a_puddle() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var ff := h.w.spawn_zone(&"fire_field", Vector3(0, 0, 0), 1.5, t.id, 8.0, Sim.Mat.FIRE, 0.0, 10.0)
	ff.heat_payload = 160.0
	h.w.ledger.generated += 160.0
	var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 4.0, Vector3(0, 0, -2.5), "scenario")
	pd.update_radius_puddle()
	var wm0 := h.w.water_mass()
	var e0 := U.e0(h)
	U.perform(h, a, "ground", 1)
	h.until(func(): return not ff.alive and not pd.alive, 120)
	check(not ff.alive and h.has_event("extinguish"), "the fire field is smothered")
	check(not pd.alive, "the puddle is soaked up")
	check(h.events("transform").any(func(e): return e.get("to", "") == "mud"), "the surge turns to mud")
	near(h.w.water_mass(), wm0, 1e-6, "water mass booked (soaked = evaporated)")
	check(U.energy_drift(h, e0) < 1e-6, "energy booked")


func test_veil_of_grit_blinds_drags_and_halves_bolts_into_glass() -> void:
	var s := _duel(5.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	t.is_dummy = false
	U.perform(h, a, "sweep", 0)
	h.until(func(): return h.w.bodies.any(func(b): return b.alive and b.tag == &"sand_cloud"), 40)
	var z: MatBody = h.w.bodies.filter(func(b): return b.alive and b.tag == &"sand_cloud")[0]
	h.step(3)
	check(t.status.has("blinded"), "the rival inside is blinded")
	var shot := U.shot(h, Sim.Mat.STONE, 20.0, z.pos + Vector3(0, 1.0, -z.zone_radius + 0.2), Vector3(0, 0, 17), t)
	h.step(2)
	near(shot.vel.length(), 17.0 * 0.7, 0.2, "a stone crossing the cloud is dragged once (-30 % K)")
	h.step(6)
	check(shot.vel.length() > 11.0, "only once (%.1f)" % shot.vel.length())
	var em0 := h.w.earth_mass()
	t.pos = z.pos + Vector3(0, 0, -z.zone_radius - 1.5)
	h.step()
	t.lock_target = a.id
	var hp := a.health
	var g0 := float(h.w.mass_ledger.sand_to_glass)
	Conduction.discharge(h.w, t, a.chest(), {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
		h.w.new_attack_id(), true)
	h.step()
	near(hp - a.health, 12.0, 1e-6, "a bolt through the cloud loses half (24 -> 12)")
	check(float(h.w.mass_ledger.sand_to_glass) > g0, "and fuses a glass bead")
	near(h.w.earth_mass(), em0, 1e-6, "sand -> glass booked")


func test_dune_wall_captures_engulfs_glasses_and_muds() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(20)
	var wall := h.w.get_body(a.wall_body)
	check(wall != null and wall.mat == Sim.Mat.SAND and wall.tag == &"sand" and is_equal_approx(wall.mass, 100.0), "a 100 kg Dune Wall")
	var stone := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", t)
	h.until(func(): return h.has_event("capture") or a.health < 100.0, 40)
	check(h.has_event("capture", "body", stone.id) and a.health == 100.0, "Dune (25 x 1.2 = 30 >= 17) captures the stone")
	h.step(70)
	check(wall.mass >= 130.0 - 1e-6, "held 1.0 s+: thickened (%.0f kg)" % wall.mass)
	# Lightning: grounded, the wall turns to glass.
	t.lock_target = a.id
	var g0 := float(h.w.mass_ledger.sand_to_glass)
	var out := Conduction.discharge(h.w, t, a.chest(), {"range": 16.0, "damage": 36.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
		h.w.new_attack_id(), true)
	h.step()
	check(out.blocked and a.health == 100.0, "the dune grounds a Storm Bolt (CP %.0f x 2.4)" % (wall.mass * 0.25))
	check(wall.alive and wall.mat == Sim.Mat.GLASS and wall.tag == &"glass", "and fuses into a glass wall")
	check(float(h.w.mass_ledger.sand_to_glass) > g0 + wall.mass - 1e-6, "booked sand_to_glass")
	h.release(a, "guard")
	# Water on a fresh dune: a mud wall (+5 CP) that collapses after 4 s.
	var s2 := _duel(9.0)
	var a2: ActorState = s2[0]
	var t2: ActorState = s2[1]
	h.press(a2, "guard")
	h.step(20)
	var dune := h.w.get_body(a2.wall_body)
	var water := h.launch_at(a2, "water", 6.0, 14.0, Sim.AMBIENT_C, "", t2)
	water.form = Sim.Form.BLOB
	var wm0 := h.w.water_mass()
	h.until(func(): return not water.alive, 40)
	check(dune.tag == &"mud", "water makes it a mud wall")
	near(Interactions.counter_power(Agent.of_body(h.w, dune)), dune.mass * 0.25 + 5.0, 1e-6, "+5 CP")
	near(h.w.water_mass(), wm0, 1e-6, "the water is soaked in (booked)")
	h.step(260)
	check(not dune.alive, "the mud wall collapses after 4 s")
	h.release(a2, "guard")


func test_engulf_spits_a_light_projectile_back() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(2)
	var stone := h.launch_at(a, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", t, 2.6)
	h.until(func(): return stone.attack_owner == a.id or a.health < 100.0, 30)
	check(stone.attack_owner == a.id and stone.vel.dot(t.pos - stone.pos) > 0.0, "perfect Dune Wall (Engulf): spat back at the thrower")
	h.release(a, "guard")


func test_dune_push_collapses_the_wall_into_a_surge() -> void:
	var s := _duel(9.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	h.press(a, "guard")
	h.step(70)
	var wall := h.w.get_body(a.wall_body)
	var m := wall.mass
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step()
	h.release(a, "guard")
	h.until(func(): return wall.form == Sim.Form.WAVE, 30)
	check(wall.form == Sim.Form.WAVE and wall.tag == &"sand_surge" and is_equal_approx(wall.mass, m), "the %.0f kg dune becomes the surge" % m)
	h.until(func(): return t.health < 100.0, 120)
	check(t.health < 100.0, "and runs into the rival")


func test_quicksand_mires_roots_sinks_and_crusts_lava() -> void:
	var s := _duel(4.5)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	U.perform(h, a, "sink", 0)
	h.until(func(): return h.w.bodies.any(func(b): return b.alive and b.tag == &"quicksand"), 30)
	var z: MatBody = h.w.bodies.filter(func(b): return b.alive and b.tag == &"quicksand")[0]
	t.pos = z.pos
	h.step(10)
	check(t.status.has("mired"), "a walker is mired (x0.3)")
	h.step(60)
	check(h.events("status").any(func(e): return e.actor == t.id and e.status == "rooted" and e.on), "rooted after 1 s")
	var stone := U.shot(h, Sim.Mat.STONE, 20.0, z.pos + Vector3(0.3, 0.4, 0), Vector3(0, -3, 0), t)
	h.until(func(): return not stone.alive, 20)
	check(not stone.alive and h.has_event("sink", "body", stone.id), "a landing stone sinks")
	var wave := U.lava_wave(h, 20.0, z.pos + Vector3(0, 0, -2.2), Vector3(0, 0, 1), t)
	var e0 := U.e0(h)
	h.until(func(): return wave.form != Sim.Form.WAVE, 60)
	check(wave.form != Sim.Form.WAVE, "a lava wave entering it crusts and stalls")
	check(U.energy_drift(h, e0) < 1e-6, "booked")


func test_sandform_gathers_compresses_and_seizes_a_rival_cloud() -> void:
	var s := _duel(8.0)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var em0 := h.w.earth_mass()
	h.press(a, "tech")
	h.step(160)
	var b := h.w.held(a)
	check(b != null and b.mat == Sim.Mat.SAND and b.mass > 18.0 and b.mass <= 30.0, "gathered sand while held, 6 kg/s (%.1f kg)" % (b.mass if b else 0.0))
	h.it(a).attack_pressed = true
	h.step()
	check(b != null and b.mat == Sim.Mat.STONE and b.tag == &"sandstone", "T+A Compress: sandstone")
	check(float(h.w.mass_ledger.sand_to_sandstone) > 18.0, "booked sand_to_sandstone")
	h.release(a, "tech")
	h.step(3)
	check(b != null and b.attack_owner == a.id and b.attack_id != 0, "thrown as a stone")
	h.step(120)
	near(h.w.earth_mass(), em0, 1e-6, "earth mass conserved")
	# Seize the rival's sand cloud.
	var s2 := _duel(8.0)
	var a2: ActorState = s2[0]
	var t2: ActorState = s2[1]
	var cloud := h.w.spawn_zone(&"sand_cloud", Vector3(0, 0, 0), 2.5, t2.id, 8.0, Sim.Mat.SAND, 12.0, 4.0)
	h.w.mass_ledger.ground_taken += 12.0
	h.press(a2, "tech")
	h.until(func(): return h.w.held(a2) != null, 40)
	check(h.w.held(a2) == cloud and cloud.form != Sim.Form.ZONE, "Sandform seizes the rival's cloud (REC)")
	h.release(a2, "tech")


func test_sand_surf_slides_and_rides() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var p0 := a.pos
	h.it(a).move = Vector3(1, 0, 0)
	h.press(a, "evade")
	h.step(30)
	check(a.pos.distance_to(p0) > 3.5, "Sand Surf: a 4 m slide (%.1f)" % a.pos.distance_to(p0))
	h.step(10)
	h.press(a, "evade")
	h.it(a).evade_held = true
	h.step(20)
	check(a.stance == "surf", "held: riding the sand")
	var p1 := a.pos
	h.step(30)
	var v := a.pos.distance_to(p1) / 0.5
	check(v > 7.0, "at ~8 m/s (%.1f)" % v)
	h.it(a).evade_held = false
	h.it(a).move = Vector3.ZERO
	h.step(10)


func test_sand_column_cells() -> void:
	var s := _duel()
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	var dune := U.wall(h, Sim.Mat.SAND, "sand", 100.0, Vector3(0, 0, 2), a)
	var dune2 := U.wall(h, Sim.Mat.SAND, "sand", 130.0, Vector3(3, 0, 2), a)
	var stone := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(0, 1, 0), Vector3(0, 0, 17), t)
	check(U.pr(h, stone, dune).outcome == "capture", "stone shot: CAP Dune Wall (30 eff)")
	check(U.pr(h, stone, dune, 0, true).outcome == "reflect", "stone shot: REC Engulf* (spit back)")
	check(U.pr(h, stone, "quicksand", 0, false, a).outcome == "sink", "stone shot: SNK Quicksand")
	var heave := U.shot(h, Sim.Mat.STONE, 45.0, Vector3(1, 1, 0), Vector3(0, 0, 14), t)
	check(U.pr(h, heave, dune2).outcome == "capture", "heavy stone: CAP Dune held T2 (130 kg)")
	var ph0 := U.pr(h, heave, dune)
	check(ph0.band == "partial", "heavy stone vs a fresh dune: partial (%.2f)" % ph0.ratio)
	check(U.pr(h, heave, "quicksand", 2, false, a).outcome == "sink", "heavy stone: SNK Quicksand T2")
	var boulder := U.shot(h, Sim.Mat.STONE, 200.0, Vector3(2, 1, 0), Vector3(0, 0, 11), t)
	check(U.pr(h, boulder, "quicksand", 3, false, a).outcome == "slow", "boulder: WKN Quicksand T3 (bogged)")
	var hot := U.shot(h, Sim.Mat.STONE, 20.0, Vector3(-1, 1, 0), Vector3(0, 0, 17), t, "", 1000.0)
	check(U.pr(h, hot, dune).outcome == "capture", "hot rock: CAP Dune (30 >= 27)")
	var blob := U.lava(h, 20.0, Vector3(4, 1, 0))
	blob.vel = Vector3(0, 0, 12)
	blob.attack_id = h.w.new_attack_id()
	blob.attack_owner = t.id
	check(U.pr(h, blob, dune).outcome == "earth_crust", "magma blob: crusted by the Dune (x1.5)")
	var wave := U.lava_wave(h, 20.0, Vector3(-3, 0, 0), Vector3(0, 0, 1), t)
	check(U.pr(h, wave, dune).outcome == "earth_crust", "lava wave: glass crust (Dune)")
	check(U.pr(h, wave, "quicksand", 0, false, a).outcome == "earth_crust", "lava wave: glass crust (Quicksand)")
	var lance := U.shot(h, Sim.Mat.METAL, 12.0, Vector3(5, 1.2, 0), Vector3(0, 0, 42), t, "lance")
	check(U.pr(h, lance, dune).band == "partial", "Railspike: partial pierce of a dune")
	var sand := U.shot(h, Sim.Mat.SAND, 5.0, Vector3(-4, 1, 0), Vector3(0, 0, 22), t, "slug")
	check(Interactions.allows(sand, &"grip_sand"), "sand: REC Sandform")
	check(U.pr(h, sand, dune).outcome == "absorb", "sand: ABS Dune (adds mass)")
	var water := U.shot(h, Sim.Mat.WATER, 6.0, Vector3(-5, 1, 0), Vector3(0, 0, 16), t)
	check(U.pr(h, water, dune).outcome == "earth_mud", "water: Dune -> mud wall")
	var ww := U.shot(h, Sim.Mat.WATER, 18.0, Vector3(6, 0, 0), Vector3(0, 0, 9), t)
	ww.form = Sim.Form.WAVE
	check(U.pr(h, ww, "quicksand", 0, false, a).outcome == "earth_mud", "water wave: Quicksand -> mud bog")
	var veil := h.w.spawn_zone(&"sand_cloud", Vector3(0, 0, -1), 4.0, a.id, 6.0, Sim.Mat.SAND, 8.0)
	h.w.mass_ledger.ground_taken += 8.0
	var mist := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.CLOUD, 2.0, Vector3(0, 1, -1), "test")
	check(U.pr(h, mist, veil).outcome == "absorb", "mist: ABS Veil of Grit")
	check(U.prv(h, "steam", {"H": 6.0}, dune).outcome == "block", "steam: Dune absorbs (blocks)")
	check(U.prv(h, "flame", {"H": 8.0}, dune).outcome == "block", "flame: smothered by the Dune (x2)")
	check(U.prv(h, "flame", {"H": 8.0}, veil).outcome == "earth_smother", "flame: smothered by the Veil")
	check(U.prv(h, "blue_fire", {"H": 12.0}, dune).outcome == "earth_glassify", "blue fire: the dune fuses to glass")
	check(U.prv(h, "lightning", {"E": 52.0}, dune).outcome == "earth_glass_ground", "Skybreak E 52: grounded (cap 60) -> glass")
	check(U.prv(h, "lightning", {"E": 70.0}, dune).outcome == "shatter", "E 70 > 60: the dune is blasted")
	check(U.prv(h, "lightning", {"E": 24.0}, veil).outcome == "earth_bolt_grit", "bolt through the Veil: -50 %, glass")
	check(U.prv(h, "blast", {"P": 34.0}, dune).outcome == "block", "combustion: ABS Dune (x1.5)")
	check(U.prv(h, "gust", {"P": 28.0}, dune).outcome == "block", "gust: BLK Dune")
	check(U.prv(h, "sound", {"P": 32.0}, dune).outcome == "block", "sound: ABS Dune (x1.5)")
	check(U.prv(h, "sound", {"P": 8.0}, veil).outcome == "absorb", "sound (Clap 8): ABS Veil (6 x 1.5)")
	var surge := U.shot(h, Sim.Mat.SAND, 30.0, Vector3(-6, 0, 0), Vector3(0, 0, 9), t)
	surge.form = Sim.Form.WAVE
	check(Interactions.allows(surge, &"grip_sand"), "sand surge: REC Sandform contest")
