extends FireKitTest
## Fire / Lightning (sub 2): "lightning blasts through stone" (Storm Bolt vs a Bulwark arrives with E 21 while the T1
## Bolt is grounded), Skybreak from above, Static Ward absorb / return, Conductor's Hand relays around cover, Rail Arc
## follows a held water jet into its holder, Ground Current lives on conductive ground only.


func _hold_strike(p: ActorState, ticks: int) -> void:
	h.press(p, "attack")
	h.step(ticks)
	h.release(p, "attack")
	h.step(2)


func test_storm_bolt_blasts_through_a_bulwark_and_the_bolt_is_grounded() -> void:
	# Bolt T1 (E 24 <= wall CP 30): grounded, the rival behind is safe.
	var pr := duel(2, Sim.Element.EARTH, 5, 8.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	var wall := bulwark(r, Vector3(0, 0, p.pos.z - 3.0), p.facing)
	_hold_strike(p, 42)
	var ev := h.last_event("lightning")
	check(not ev.is_empty() and ev.blocked, "Bolt T1 is grounded by the wall")
	check(r.health == 100.0 and wall.alive, "rival safe, wall standing")
	# Storm Bolt T2 (E 36 > 30): the wall shatters, the bolt continues with 36 - 0.5 x 30 = 21.
	pr = duel(2, Sim.Element.EARTH, 5, 8.0)
	p = pr[0]
	r = pr[1]
	r.is_dummy = true
	wall = bulwark(r, Vector3(0, 0, p.pos.z - 3.0), p.facing)
	var base := snap(h.w)
	_hold_strike(p, 76)
	ev = h.last_event("lightning")
	check(not ev.is_empty() and not ev.blocked and (ev.hits as Array).has(r.id), "the Storm Bolt reaches the rival")
	near(float(ev.get("e", 0.0)), 21.0, 1e-6, "it arrives with E 21")
	check(not wall.alive and h.has_event("wall_crumble"), "the wall shattered")
	near(100.0 - r.health, 30.0 * 21.0 / 36.0, 0.01, "damage scaled by what is left of the bolt")
	h.step(30)
	ledgers_ok(base, "storm bolt")


func test_skybreak_strikes_from_above_over_a_wall_and_deafens() -> void:
	var pr := duel(2, Sim.Element.EARTH, 5, 8.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	bulwark(r, Vector3(0, 0, p.pos.z - 3.0), p.facing)
	_hold_strike(p, 112)
	check(h.events("telegraph").any(func(e): return e.get("move", "") == "skybreak"), "a 0.4 s telegraph at the target")
	check(r.health == 100.0, "nothing yet")
	h.step(26)
	check(r.health < 100.0 - 30.0, "the strike from above hits behind the wall (%.1f)" % r.health)
	check(r.status.has("deafened") or h.events("status").any(func(e): return e.actor == r.id and e.status == "deafened"), "thunder deafens")
	check(h.has_event("thunder"), "thunder event")


func test_static_ward_absorbs_half_and_static_burst_returns_it() -> void:
	var pr := duel(2, Sim.Element.FIRE, 5, 3.5)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.subs[Sim.Element.FIRE] = 2
	h.press(p, "guard")
	h.step(20)                       # not perfect
	_hold_strike(r, 42)              # the rival's Bolt (E 24)
	check(h.has_event("static_absorb"), "the ward stores the bolt")
	near(p.static_charge, 12.0, 1e-6, "50 % stored as static")
	near(100.0 - p.health, 12.0, 0.01, "50 % taken")
	h.release(p, "guard")
	h.until(func(): return p.stun <= 0.0, 60)
	h.press(p, "guard")
	h.step(6)
	h.flick(p, "guard", Sim.Gesture.UP)
	h.step(30)
	var sb := h.last_event("static_burst")
	check(not sb.is_empty() and absf(float(sb.e) - 12.0) < 1e-6, "Static Burst releases it (%s)" % [sb.get("e")])
	check(r.health < 100.0 and p.static_charge == 0.0, "back at the rival (%.1f)" % r.health)
	h.release(p, "guard")


func test_perfect_static_ward_absorbs_fully_or_returns_with_redirect_current() -> void:
	for redirect in [false, true]:
		var pr := duel(2, Sim.Element.FIRE, 5, 8.0, {"redirect_current": true} if redirect else {})
		var p: ActorState = pr[0]
		var r: ActorState = pr[1]
		r.subs[Sim.Element.FIRE] = 2
		h.press(r, "attack")
		h.step(40)
		h.press(p, "guard")
		h.step(1)
		h.release(r, "attack")
		h.step(3)
		check(p.health == 100.0, "redirect=%s: perfect ward, no damage (%.1f)" % [redirect, p.health])
		if redirect:
			check(h.has_event("lightning_redirect") and r.health < 100.0, "Return Current sends it back (rival %.1f)" % r.health)
			near(100.0 - r.health, 24.0 * 0.8, 0.01, "at 80 %")
		else:
			near(p.static_charge, 24.0, 1e-6, "fully absorbed into static")
		h.release(p, "guard")
		h.step(20)


func test_conductors_hand_relays_a_bolt_around_cover() -> void:
	h = SimHarness.new(6)
	var p := h.actor("F", Vector3(-3.75, 0, 5.0), 0, {}, Sim.Element.FIRE)
	var r := h.actor("R", Vector3(-3.75, 0, -4.0), 1, {}, Sim.Element.EARTH)
	r.is_dummy = true
	p.subs[Sim.Element.FIRE] = 2
	h.step(20)
	# A direct Bolt is stopped by the cover wall.
	_hold_strike(p, 42)
	check(h.last_event("lightning").get("blocked", false) and r.health == 100.0, "the cover wall stops the direct bolt")
	h.step(60)
	var rod := h.w.spawn_body(Sim.Mat.METAL, Sim.Form.CHUNK, 3.0, Vector3(-0.2, 0.25, -1.0), "test")
	rod.tag = &"rod"
	rod.on_ground = true
	rod.static_body = true
	h.log.clear()
	var dir := (rod.pos - p.pos)
	dir.y = 0.0
	h.aim(p, dir)
	h.press(p, "tech")
	h.step(30)
	check(rod.charge > 0.0, "Conductor's Hand charges the rod (%.1f)" % rod.charge)
	h.release(p, "tech")
	h.step(4)
	var al := h.last_event("arc_link")
	check(not al.is_empty() and (al.hits as Array).has(r.id), "Arc Link banks the bolt through the rod into the rival (%s)" % [al])
	check(r.health < 100.0, "behind cover (%.1f)" % r.health)


func test_rail_arc_follows_a_held_water_jet_into_its_holder() -> void:
	h = SimHarness.new(6)
	var p := h.actor("F", Vector3(0, 0, 6.0), 0, {}, Sim.Element.FIRE)
	var r := h.actor("R", Vector3(3.5, 0, -3.0), 1, {}, Sim.Element.WATER)
	r.is_dummy = true
	p.subs[Sim.Element.FIRE] = 2
	h.step(20)
	var jet := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, Vector3(1.6, 1.25, -1.5), "test")
	jet.tag = &"jet"
	h.w.take_control(r, jet, 0.97, "jet")
	jet.hold_point = jet.pos
	jet.radius = 1.8
	h.aim(p, Vector3(0, 0, -1))
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step(3)
	h.release(p, "attack")
	h.until(func(): return h.has_event("lightning"), 40)
	var ev := h.last_event("lightning")
	check(ev.get("rail", false), "a rail arc")
	check(r.health < 100.0 and (ev.hits as Array).has(r.id), "it follows the jet into its holder (%.1f)" % r.health)


func test_ground_current_needs_conductive_ground() -> void:
	# Dry stone: it dies after about 2 m and never reaches a rival 8 m away.
	var pr := duel(2, Sim.Element.EARTH, 5, 8.0)
	var p: ActorState = pr[0]
	var r: ActorState = pr[1]
	r.is_dummy = true
	h.flick(p, "attack", Sim.Gesture.DOWN)
	h.step(3)
	h.release(p, "attack")
	h.step(60)
	check(h.events("current_grounded").size() >= 1 and r.health == 100.0, "on dry stone it grounds out")
	# A trail of puddles to the rival: it races along and shocks them.
	for frozen in [false, true]:
		pr = duel(2, Sim.Element.EARTH, 5, 8.0)
		p = pr[0]
		r = pr[1]
		r.is_dummy = true
		for k in 4:
			var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 6.0, Vector3(0, 0, p.pos.z - 1.6 - 1.9 * float(k)), "test")
			pd.update_radius_puddle()
			pd.radius = 1.1
			if frozen and k == 1:
				pd.liquid = 0.0
				pd.phase = Sim.Phase.FROZEN
		h.flick(p, "attack", Sim.Gesture.DOWN)
		h.step(3)
		h.release(p, "attack")
		h.step(70)
		if frozen:
			check(h.has_event("insulated") and r.health == 100.0, "a frozen puddle stops the current")
		else:
			check(r.health < 100.0, "along wet ground it reaches the rival (%.1f)" % r.health)
	# Grounding makes the rival immune.
	pr = duel(2, Sim.Element.EARTH, 5, 8.0)
	p = pr[0]
	r = pr[1]
	for k in 4:
		var pd2 := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 6.0, Vector3(0, 0, p.pos.z - 1.6 - 1.9 * float(k)), "test")
		pd2.update_radius_puddle()
		pd2.radius = 1.1
	Status.apply(h.w, r, "grounding", 3.0, 1.0, r.id)
	h.flick(p, "attack", Sim.Gesture.DOWN)
	h.step(3)
	h.release(p, "attack")
	h.step(70)
	check(r.health == 100.0, "Grounding is immune (%.1f)" % r.health)


func test_arc_fan_forks_into_two_rivals() -> void:
	h = SimHarness.new(6)
	var p := h.actor("F", Vector3(0, 0, 4.0), 0, {}, Sim.Element.FIRE)
	var r1 := h.actor("R1", Vector3(-2.2, 0, -0.5), 1, {}, Sim.Element.EARTH)
	var r2 := h.actor("R2", Vector3(2.2, 0, -0.5), 1, {}, Sim.Element.EARTH)
	r1.is_dummy = true
	r2.is_dummy = true
	p.subs[Sim.Element.FIRE] = 2
	h.step(20)
	h.aim(p, Vector3(0, 0, -1))
	h.flick(p, "attack", Sim.Gesture.SIDE)
	h.step(3)
	h.release(p, "attack")
	h.step(30)
	check(r1.health < 100.0 and r2.health < 100.0, "both rivals forked (%.1f, %.1f)" % [r1.health, r2.health])
