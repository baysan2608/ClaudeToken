extends FireKitTest
## Fire / Combustion (sub 3): detonations by tier, what blasts do to the world (shatter ice, snuff fire fields, disperse
## clouds), the environment around a blast (a vacuum suppresses it, vapour halves it, the inrush after a vacuum collapses
## boosts it, a fuse in a tornado makes a fire tornado), Spark Mines, Reactive Blast, Smother Blast.


func _hold_strike(p: ActorState, ticks: int) -> void:
	h.press(p, "attack")
	h.step(ticks)
	h.release(p, "attack")
	h.step(2)


func test_pop_burst_blast_detonation_by_tier() -> void:
	for tier in 4:
		var dist: float = [1.6, 6.0, 9.0, 12.0][tier]
		var pr := duel(3, Sim.Element.EARTH, 5, dist)
		var p: ActorState = pr[0]
		var r: ActorState = pr[1]
		r.is_dummy = true
		var base := snap(h.w)
		var f0 := p.focus
		var ticks := 3 if tier == 0 else int(Charge.tier_times(Moves.DEFS.pop)[tier - 1] * 60.0) + 4
		_hold_strike(p, ticks)
		var spent := (f0 - p.focus) * 10.0
		h.step(45)
		check(r.health < 100.0, "T%d: the detonation at %.0f m hits the rival (%.1f)" % [tier, dist, r.health])
		var bursts := h.events("fx").filter(func(e): return e.fx == "burst" and e.mat == "blast")
		check(not bursts.is_empty(), "T%d: a blast burst cue" % tier)
		if not bursts.is_empty():
			near(float(bursts[0].radius), [2.0, 2.0, 3.0, 4.5][tier], 1e-6, "T%d radius" % tier)
		var hu: float = float(Moves.DEFS.pop.heat) + float(Charge.pget(Moves.DEFS.pop, tier, "heat_add", 0.0)) * (1.0 if tier > 0 else 0.0)
		check(spent >= hu - 1.0, "T%d: paid %.0f HU (%.0f)" % [tier, hu, spent])
		ledgers_ok(base, "pop T%d" % tier)
		fx_catalogued("pop T%d" % tier)


func test_blasts_shatter_ice_and_snuff_a_fire_field() -> void:
	var pr := duel(3, Sim.Element.WATER, 5, 8.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	var field := FireUtil.spawn_field(h.w, r.id, p.pos + p.forward() * 6.0, 1.5, 5.0, 200.0)
	h.w.ledger.generated += 200.0
	var side := p.forward().cross(Vector3.UP).normalized()
	var ice := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.CHUNK, 4.0, p.pos + p.forward() * 6.0 + side * 1.9 + Vector3(0, 1.0, 0), "test", -5.0)
	ice.gravity_scale = 0.0
	ice.vel = -p.forward() * 0.6
	ice.attack_id = h.w.new_attack_id()
	ice.attack_owner = r.id
	var e1 := ice.thermal_energy()
	ice.liquid = 0.0
	ice.phase = Sim.Phase.FROZEN
	h.w.ledger.freeze_dump += ice.thermal_energy() - e1
	var base := snap(h.w)
	h.aim(p, p.forward())
	_hold_strike(p, 28)            # Burst (T1) at 6 m
	h.step(30)
	check(not field.alive and h.events("extinguish").any(func(e): return e.get("body", -1) == field.id), "the blast snuffs the fire field")
	check(h.events("shatter").any(func(e): return e.get("body", -1) == ice.id), "and shatters the ice")
	ledgers_ok(base, "blast vs field and ice")


func test_a_null_zone_suppresses_a_detonation() -> void:
	for tag in ["null_bubble", "t_null"]:
		var pr := duel(3, Sim.Element.AIR, 5, 6.0)
		var p: ActorState = pr[0]
		var r: ActorState = pr[1]
		r.is_dummy = true
		h.begin_scope()
		if tag == "t_null":
			Interactions.register_tag_class(&"t_null", &"vacuum", &"t_null")
		h.spawn_zone(tag, r.pos, 2.5, r, 14.0)
		var base := snap(h.w)
		_hold_strike(p, 28)
		h.step(30)
		check(h.has_event("blast_suppressed"), "%s: the blast is suppressed" % tag)
		check(r.health == 100.0, "%s: the rival inside is untouched" % tag)
		ledgers_ok(base, "suppressed")
		h.end_scope()


func test_vapour_halves_a_blast_and_the_vacuum_inrush_boosts_it() -> void:
	var dmg := {}
	for env in ["clear", "fog"]:
		var pr := duel(3, Sim.Element.WATER, 5, 6.0)
		var p: ActorState = pr[0]
		var r: ActorState = pr[1]
		r.is_dummy = true
		if env == "fog":
			h.spawn_zone("fog", r.pos, 3.0, r, 0.0)
		_hold_strike(p, 28)
		h.step(30)
		dmg[env] = 100.0 - r.health
	near(dmg.fog, dmg.clear * 0.5, 0.01, "mist halves the blast (%.1f vs %.1f)" % [dmg.fog, dmg.clear])
	# Fuse right after a vacuum well collapses next to it: x1.5.
	var pr2 := duel(3, Sim.Element.AIR, 5, 6.0)
	var p2: ActorState = pr2[0]
	var r2: ActorState = pr2[1]
	r2.is_dummy = true
	var well := h.spawn_zone("vacuum_well", r2.pos + Vector3(2.5, 0, 0), 1.5, r2, 12.0)
	h.press(p2, "tech")
	h.step(30)
	h.w.close_zone(well, "collapsed")
	h.step(2)
	h.release(p2, "tech")
	h.step(6)
	var fe := h.last_event("fuse")
	check(not fe.is_empty() and (fe.mods as Array).has("inrush"), "the inrush boosts the fuse (%s)" % [fe.get("mods")])


func test_fuse_inside_a_tornado_makes_a_fire_tornado() -> void:
	var pr := duel(3, Sim.Element.AIR, 5, 7.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	var tor := h.spawn_zone("tornado", r.pos + Vector3(0, 0, 1.5), 2.0, r, 25.0)
	tor.max_life = 6.0
	var base := snap(h.w)
	h.press(p, "tech")
	h.step(40)
	h.release(p, "tech")
	h.step(6)
	check(h.has_event("infuse") and tor.props.get("fire", false), "the tornado is set alight")
	var f := zones_tagged("fire_field")
	check(f.size() == 1 and int(f[0].props.get("follow", -1)) == tor.id, "a fire field rides the tornado")
	h.step(40)
	check(r.status.has("burning") or h.events("status").any(func(e): return e.actor == r.id and e.status == "burning"), "the fire tornado burns")
	ledgers_ok(base, "fire tornado")


func test_spark_mine_proximity_and_remote_detonation() -> void:
	var pr := duel(3, Sim.Element.EARTH, 5, 9.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	var base := snap(h.w)
	h.aim(p, p.forward().rotated(Vector3.UP, 0.9))
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step(3)
	h.release(p, "attack")
	h.until(func(): return h.events("stick").any(func(e): return e.get("mine", false)), 120)
	var mines := bodies_of(Sim.Mat.FIRE).filter(func(b): return b.props.get("mine", false))
	check(mines.size() == 1, "a mine is stuck on the ground")
	if mines.size() == 1:
		r.is_dummy = false
		r.pos = mines[0].pos + Vector3(2.5, 0, 0)
		h.it(r).move = Vector3(-1, 0, 0)
		h.until(func(): return h.has_event("ember_pop"), 90)
		h.it(r).move = Vector3.ZERO
		check(h.last_event("ember_pop").get("why", "") == "proximity" and r.health < 100.0, "it pops when the rival walks in")
	ledgers_ok(base, "mine proximity")
	# Remote: a second flick up detonates it.
	pr = duel(3, Sim.Element.EARTH, 5, 9.0)
	p = pr[0]
	r = pr[1]
	r.is_dummy = true
	h.aim(p, p.forward().rotated(Vector3.UP, 0.9))
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step(3)
	h.release(p, "attack")
	h.until(func(): return h.events("stick").any(func(e): return e.get("mine", false)), 120)
	h.step(40)
	var f0 := p.focus
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step(3)
	h.release(p, "attack")
	h.step(30)
	check(h.events("ember_pop").any(func(e): return e.why == "remote"), "a second flick up detonates it")
	check(p.focus >= f0 - 0.01, "the detonation costs nothing")


func test_reactive_blast_deflects_light_solids_and_reflects_on_a_perfect() -> void:
	for perfect in [false, true]:
		var pr := duel(3, Sim.Element.EARTH, 5, 10.0)
		var p: ActorState = pr[0]
		var r: ActorState = pr[1]
		var st := h.launch_at(p, Sim.Mat.STONE, 20.0, 17.0, Sim.AMBIENT_C, "", r, 6.0)
		h.w.mass_ledger.ground_taken += 20.0
		var base := snap(h.w)
		if not perfect:
			h.press(p, "guard")
		var f0 := p.focus
		for k in 30:
			if perfect and st.pos.distance_to(p.chest()) < 2.4 and not p.guarding:
				h.press(p, "guard")
			h.step()
		check(p.health == 100.0, "perfect=%s: no damage" % perfect)
		check(h.has_event("reactive_blast"), "perfect=%s: the guard detonated" % perfect)
		near(f0 - p.focus, FireCombustion.REACTIVE_COST, 0.5, "perfect=%s: 8 Focus per trigger" % perfect)
		if perfect:
			check(st.attack_owner == p.id, "the stone now flies for the guard (reflected)")
			check(st.vel.dot(r.pos - p.pos) > 0.0, "back toward the thrower")
		h.release(p, "guard")
		h.step(20)
		ledgers_ok(base, "reactive perfect=%s" % perfect)


func test_smother_blast_snuffs_fields_around_and_jumps() -> void:
	var pr := duel(3, Sim.Element.EARTH, 5, 8.0)
	var p: ActorState = pr[0]
	var fields: Array[MatBody] = []
	for k in 3:
		fields.append(FireUtil.spawn_field(h.w, 2, p.pos + Vector3(cos(k * 2.0), 0, sin(k * 2.0)) * 2.5, 1.2, 6.0, 120.0))
		h.w.ledger.generated += 120.0
	var base := snap(h.w)
	var y0 := p.pos.y
	h.press(p, "guard")
	h.step(8)
	h.flick(p, "guard", Sim.Gesture.DOWN)
	var top := y0
	for k in 40:
		h.step()
		top = maxf(top, p.pos.y)
	h.release(p, "guard")
	check(fields.all(func(f): return not f.alive), "every fire field within 4 m is snuffed")
	check(top > y0 + 1.2, "the blast launches the fighter (%.2f m)" % (top - y0))
	h.step(40)
	ledgers_ok(base, "smother")
