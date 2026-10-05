extends FireKitTest
## Fire / Blue (sub 1): Searing Beam T2 melts a flying 20 kg stone ("turn it into lava"); White Core T3 melts through a
## stone wall (slump ~1 s); Smelter slumps it in ~1 s ("melt the wall": molten face exists, the rest crumbles, ledgers
## exact); Blue Furrow melts the ground into a lava channel; Blue Aegis and Corona melt small metal / ice; Kiln burns
## whoever seizes the superheated body.


func test_searing_beam_t2_melts_a_flying_stone_into_a_falling_magma_blob() -> void:
	var pr := duel(1, Sim.Element.EARTH, 5, 10.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	h.press(p, "attack")
	h.step(56)
	var st := h.launch_at(p, Sim.Mat.STONE, 20.0, 17.0, Sim.AMBIENT_C, "", r, 7.0)
	h.w.mass_ledger.ground_taken += 20.0
	var base := snap(h.w)
	h.step(8)
	check(p.action != null and p.action.tier() == 2, "charged to T2 (Searing Beam)")
	h.release(p, "attack")
	h.step(3)
	check(st.liquid >= 0.8, "the stone is molten in flight (liquid %.2f)" % st.liquid)
	check(h.has_event("melt_in_flight"), "melt_in_flight event")
	h.step(60)
	check(not h.events("hit").any(func(e): return e.actor == p.id), "it falls short of the caster")
	check(st.alive and st.is_stone() and st.pos.distance_to(p.pos) > 1.0, "a magma blob on the ground in front")
	ledgers_ok(base, "searing beam")
	fx_catalogued("searing beam")


func test_white_core_t3_melts_through_a_stone_wall_in_about_one_second() -> void:
	var pr := duel(1, Sim.Element.EARTH, 5, 9.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	var wall := bulwark(r, Vector3(0, 0, p.pos.z - 3.5), p.facing)
	var base := snap(h.w)
	h.press(p, "attack")
	h.step(112)
	check(p.action != null and p.action.tier() == 3, "charged to T3 (White Core)")
	h.release(p, "attack")
	var t0 := h.w.tick
	h.until(func(): return h.has_event("slump"), 120)
	var dt := float(h.w.tick - t0) / 60.0
	check(h.has_event("slump") and not wall.alive, "the wall slumped (%.2f s)" % dt)
	check(dt > 0.6 and dt < 1.2, "in about 1 s (%.2f)" % dt)
	h.step(60)
	ledgers_ok(base, "white core")


func test_smelter_slumps_a_bulwark_in_about_one_second() -> void:
	var pr := duel(1, Sim.Element.EARTH, 5, 9.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	var wall := bulwark(r, Vector3(0, 0, p.pos.z - 3.0), p.facing)
	var base := snap(h.w)
	h.press(p, "tech")
	var t0 := h.w.tick
	h.until(func(): return h.has_event("slump"), 160)
	var dt := float(h.w.tick - t0) / 60.0
	check(h.has_event("slump") and not wall.alive, "the wall slumped")
	check(dt > 0.95 and dt < 1.35, "in about 1 s incl. the 0.2 s startup (%.2f)" % dt)
	var face := h.w.get_body(int(h.last_event("slump").get("body", -1)))
	check(face != null and face.alive and face.liquid >= 0.5, "the molten face body exists")
	check(h.has_event("wall_crumble"), "the rest crumbled")
	h.release(p, "tech")
	h.step(30)
	ledgers_ok(base, "smelter")


func test_blue_furrow_melts_the_ground_into_a_lava_channel() -> void:
	var pr := duel(1, Sim.Element.EARTH, 5, 8.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	var base := snap(h.w)
	var taken: float = h.w.mass_ledger.ground_taken
	h.flick(p, "attack", Sim.Gesture.DOWN)
	h.step(3)
	h.release(p, "attack")
	h.until(func(): return h.has_event("magma_rift"), 40)
	var ev := h.last_event("magma_rift")
	var b := h.w.get_body(int(ev.get("body", -1)))
	check(b != null and b.is_stone() and b.form == Sim.Form.WAVE and b.liquid >= 0.99, "a molten lava channel")
	near(h.w.mass_ledger.ground_taken - taken, 8.0, 1e-6, "its stone came from the ground")
	check(String(Interactions.classify(b)) == "lava_wave", "it is a lava wave (Earth / Magma Surge can push it)")
	h.until(func(): return r.health < 100.0, 90)
	check(r.health < 100.0, "it runs into the rival")
	h.step(60)
	ledgers_ok(base, "blue furrow")


func test_blue_aegis_melts_small_metal_and_still_blocks_stones() -> void:
	var pr := duel(1, Sim.Element.EARTH, 5, 10.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	h.press(p, "guard")
	h.step(20)
	var m := h.launch_at(p, Sim.Mat.METAL, 6.0, 20.0, Sim.AMBIENT_C, "", r, 6.0)
	var base := snap(h.w)
	var f0 := p.focus
	h.step(30)
	check(m.alive and m.liquid > 0.5 and m.mat == Sim.Mat.METAL, "the 6 kg metal is molten (%.2f)" % m.liquid)
	check(p.health == 100.0, "no damage")
	check(f0 - p.focus > 10.0, "the Aegis paid the heat (%.1f Focus incl. upkeep)" % (f0 - p.focus))
	ledgers_ok(base, "aegis metal")
	var st := h.launch_at(p, Sim.Mat.STONE, 20.0, 17.0, Sim.AMBIENT_C, "", r, 6.0)
	h.w.mass_ledger.ground_taken += 20.0
	h.step(30)
	check(p.health < 100.0 and p.health > 95.0, "a 20 kg stone is only blocked (chip %.1f)" % (100.0 - p.health))
	h.release(p, "guard")
	h.step(10)


func test_corona_melts_an_incoming_ice_shard() -> void:
	var pr := duel(1, Sim.Element.EARTH, 5, 10.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	h.flick(p, "attack", Sim.Gesture.SIDE)
	h.step(3)
	h.release(p, "attack")
	h.until(func(): return not zones_tagged("corona").is_empty(), 30)
	check(zones_tagged("corona").size() == 1, "a corona around the fighter")
	var ice := h.launch_at(p, Sim.Mat.WATER, 4.0, 18.0, -5.0, "", r, 5.0)
	var e1 := ice.thermal_energy()
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	h.w.ledger.freeze_dump += ice.thermal_energy() - e1
	var base := snap(h.w)
	h.step(30)
	check(not h.events("hit").any(func(e): return e.actor == p.id), "the shard never lands")
	check(not ice.alive or ice.phase == Sim.Phase.LIQUID, "it melted in the ring")
	h.step(60)
	ledgers_ok(base, "corona ice")


func test_kiln_burns_whoever_seizes_the_superheated_stone() -> void:
	var pr := duel(1, Sim.Element.EARTH, 5, 6.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	var st := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, p.pos + p.forward() * 1.6 + Vector3(0, 0.3, 0), "test")
	h.w.mass_ledger.ground_taken += 20.0
	st.on_ground = true
	var base := snap(h.w)
	h.press(p, "guard")
	h.step(8)
	h.flick(p, "guard", Sim.Gesture.DOWN)
	h.step(40)
	h.release(p, "guard")
	check(h.has_event("kiln") and st.temp >= 600.0, "the stone is superheated (%.0f °C)" % st.temp)
	h.step(5)
	h.w.take_control(r, st, 0.9, "seize")
	h.step(3)
	check(h.has_event("kiln_burn"), "seizing it burns the rival")
	check(r.health < 100.0 and st.controller != r.id, "hurt (%.1f) and the stone dropped" % r.health)
	h.step(30)
	ledgers_ok(base, "kiln")
