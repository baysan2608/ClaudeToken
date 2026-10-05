extends AirKitTest
## Air / Sound (sub 3): Clap -> Shout -> Roar -> Resonance (disrupt, shatter, deafen), Sound Lance bank shots,
## Tremor Hum, Echo Ring, Sound Barrier + Echo Return, Thunder Step, Ground Ping, Flight, Boom Step, Hover,
## the Sound column of the counter matrix.

const MOVES := ["sound_clap", "sound_lance", "sound_tremor", "sound_echo_ring", "sound_barrier", "sound_thunder_step", "sound_ping",
	"sound_flight", "sound_boom_step", "sound_hover"]


func _duel(dist: float = 10.0) -> Array:
	return duel(3, Sim.Element.EARTH, 3, dist)


func _ice_shard(pos: Vector3, mass: float = 4.0) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.SHARD, mass, pos, "test")
	h.w.mass_ledger.moisture_taken += mass
	var e0 := b.thermal_energy()
	b.liquid = 0.0
	b.temp = -5.0
	b.phase = Sim.Phase.FROZEN
	h.w.ledger.freeze_dump += b.thermal_energy() - e0
	b.gravity_scale = 0.0
	return b


func test_every_sound_move_has_a_def_and_a_binding() -> void:
	Moves.ensure()
	var clips: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://assets/characters/fighter_clips.json"))
	for id in MOVES:
		check(Moves.DEFS.has(id), "%s registered" % id)
		if not Moves.DEFS.has(id):
			continue
		var d: Dictionary = Moves.DEFS[id]
		for k in ["name", "desc", "slot", "sub", "element", "startup", "recovery", "cost", "anim", "fx", "ai"]:
			check(d.has(k), "%s has %s" % [id, k])
		check(int(d.sub) == 3 and int(d.element) == 3, "%s is Air/Sound" % id)
		for ck in ["anim", "anim_active", "anim_hold"]:
			if d.has(ck):
				check(clips.has(String(d[ck])), "%s: clip %s exists" % [id, d[ck]])
		if ["strike", "thrust", "ground", "sweep", "tech", "guard"].has(String(d.slot)):
			check(d.has("tiers") and (d.tiers as Dictionary).has("t3"), "%s has tiers up to t3" % id)
			check(d.has("counter") and d.has("threat"), "%s has counter + threat" % id)
		check(Moves.slot_of(3, 3, id) == String(d.slot), "%s bound to %s" % [id, d.slot])
	for slot in Sim.SLOTS:
		var id := Moves.resolve(3, 3, slot)
		check(id != "" and int(Moves.DEFS[id].get("sub", 0)) == 3, "Air/Sound %s bound (%s)" % [slot, id])


func test_clap_shout_roar_resonance_tiers() -> void:
	var want := [[3.0, 30.0, 8.0], [7.0, 15.0, 14.0], [10.0, 18.0, 22.0], [12.0, 22.0, 32.0]]
	for tier in 4:
		var s := _duel(2.5)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		var hp := r.health
		var f0 := r.focus
		run_move(a, "sound_clap", tier, 1 if tier == 0 else 30, 50)
		var fx := h.events("fx").filter(func(e): return e.fx == "cone" and e.mat == "sound")
		check(fx.size() >= 1, "T%d: a sound cone fx" % tier)
		if fx.size() >= 1:
			near(float(fx[0].length), want[tier][0], 1e-6, "T%d range" % tier)
			near(float(fx[0].angle), want[tier][1], 1e-6, "T%d half angle" % tier)
			near(float(fx[0].power), want[tier][2], 1e-6, "T%d pressure" % tier)
		check(r.health < hp, "T%d hits the rival at 2.5 m (%.1f)" % [tier, r.health])
		if tier == 0:
			check(h.has_event("status", "status", "dazed"), "Clap dazes (0.15 s)")
		if tier >= 2:
			check(f0 - r.focus > 4.0 or r.focus < f0, "T%d: -8 Focus on hit (%.1f -> %.1f)" % [tier, f0, r.focus])
		if tier == 3:
			check(Status.has(r, "deafened"), "Resonance deafens")
		check(a.action == null, "T%d ends cleanly" % tier)
		fx_catalogued("clap T%d" % tier)


func test_costs_per_tier() -> void:
	var costs := [4.0, 8.0, 12.0, 18.0]
	for tier in 4:
		var s := _duel(14.0)
		var a: ActorState = s[0]
		var f0 := a.focus
		var fmin := f0
		var it := h.it(a)
		h.w.start_action(a, "sound_clap", it, {"slot": "strike", "tier": tier, "charge_frozen": true})
		it.attack_held = tier > 0
		for k in 30:
			h.step()
			fmin = minf(fmin, a.focus)
		it.attack_held = false
		for k in 40:
			h.step()
			fmin = minf(fmin, a.focus)
		near(f0 - fmin, costs[tier], 1.2, "T%d cost" % tier)


func test_sound_disrupts_a_charge_when_it_reaches_the_cohesion() -> void:
	# cohesion 6 + 4 x tier: a Clap (8) breaks a T0 charge, not a T1 one (10); a Shout (14) breaks a T1 charge, not a T3 one (18)
	var cases := [[0, 0, true], [1, 0, false], [1, 1, true], [3, 1, false], [3, 3, true]]    # [charge tier, clap tier, broken]
	for cs in cases:
		var s := _duel(2.5)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		r.is_dummy = false
		var it := h.it(r)
		var inst := h.w.start_action(r, "earth_attack", it, {"slot": "strike", "tier": int(cs[0]), "charge_frozen": true})
		it.attack_held = true
		h.step(30)
		check(r.action == inst and r.action.phase == ActionInst.P.CHARGE, "setup: R is charging T%d" % cs[0])
		h.log.clear()
		run_move(a, "sound_clap", int(cs[1]), 1 if int(cs[1]) == 0 else 30, 14)
		var broken := h.has_event("disrupt", "actor", r.id)
		check(broken == bool(cs[2]), "charge T%d vs sound tier %d: %s" % [cs[0], cs[1], "disrupted" if cs[2] else "holds"])
		it.attack_held = false


func test_roar_shatters_ice_and_a_stone_shot_and_resonance_a_heavy_one() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var shard := _ice_shard(Vector3(0, 1.25, a.pos.z - 2.0))
	var base := snap(h.w)
	run_move(a, "sound_clap", 1, 30, 14)
	check(h.events("interaction").any(func(e): return String(e.threat) == "ice" and String(e.counter) == "sound" and e.outcome == "shatter"), "Shout shatters ice (x2.5)")
	h.step(10)
	ledgers_ok(base, "ice shatter", 1e-5)
	# a stone shot in flight: Roar (22 vs 17) shatters it
	s = _duel(12.0)
	a = s[0]
	r = s[1]
	var holder := {}
	var spawn := func() -> void:
		holder["stone"] = h.launch_at(a, "stone", 20.0, 6.0, Sim.AMBIENT_C, "", r, 5.0)
		holder["base"] = snap(h.w)
	run_when(a, "sound_clap", 2, spawn, 30, 12)
	check(h.events("interaction").any(func(e): return String(e.threat) == "stone" and String(e.counter) == "sound" and e.outcome == "shatter"), "Roar shatters a stone shot (resonance)")
	check(h.has_event("shatter"), "shatter event")
	ledgers_ok(holder.base, "stone shatter", 1e-5)


func test_sound_lance_banks_off_the_cover_wall() -> void:
	# A south of the cover wall, aiming at its face; the reflected pulse goes on to R where a straight shot would miss.
	h = SimHarness.new(3)
	var a := h.actor("A", Vector3(-1.0, 0, 3.0), 0, {}, Sim.Element.AIR)
	a.subs[3] = 3
	var r := h.actor("R", Vector3(-6.5, 0, 3.0), 1, {}, Sim.Element.EARTH)
	r.is_dummy = true
	h.step(10)
	h.log.clear()
	var d := Vector3(-2.75, 0, -3.75).normalized()
	h.aim(a, d)
	var hp := r.health
	run_move(a, "sound_lance", 0, 1, 30)
	check(h.has_event("ricochet"), "the pulse bounces off the wall (ricochet event)")
	check(r.health < hp, "and the reflected pulse hits R (%.1f)" % r.health)
	var beams := h.events("fx").filter(func(e): return e.fx == "beam" and e.mat == "sound")
	check(beams.size() == 1 and (beams[0].path as PackedVector3Array).size() >= 3, "the beam fx carries the bent path")
	# straight at R it would not need the wall; away from the wall with nothing in line: no hit
	var s2 := _duel(10.0)
	var a2: ActorState = s2[0]
	var r2: ActorState = s2[1]
	h.aim(a2, Vector3(1, 0, 0))
	run_move(a2, "sound_lance", 0, 1, 30)
	check(r2.health == 100.0, "a lance aimed away from R misses")


func test_sound_lance_bounces_off_a_bulwark_and_the_arena_wall_and_passes_mist() -> void:
	h = SimHarness.new(3)
	var a := h.actor("A", Vector3(0, 0, 8), 0, {}, Sim.Element.AIR)
	a.subs[3] = 3
	var r := h.actor("R", Vector3(6, 0, 8), 1, {}, Sim.Element.EARTH)
	r.is_dummy = true
	# a stone wall to A's north; aim at it at 45 deg: reflects east toward R
	var wall := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, 120.0, Vector3(3.0, 0, 3.0), "test")
	h.w.mass_ledger.ground_taken += 120.0
	wall.wall_yaw = 0.0
	wall.wall_half = Vector3(3.0, 1.0, 0.3)
	wall.wall_rise = 1.0
	wall.static_body = true
	wall.props["standing"] = 999.0
	h.step(10)
	h.log.clear()
	h.aim(a, Vector3(3.0, 0, -4.7).normalized())
	var hp := r.health
	run_move(a, "sound_lance", 0, 1, 30)
	check(h.has_event("ricochet"), "reflected by the stone wall")
	check(r.health < hp, "bank shot off a stone wall reaches R")
	# mist in the line does not stop the pulse
	var s := _duel(10.0)
	var a2: ActorState = s[0]
	var r2: ActorState = s[1]
	var mist := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.CLOUD, 2.0, Vector3(0, 1.25, a2.pos.z - 3.0), "test")
	h.w.mass_ledger.moisture_taken += 2.0
	mist.max_life = 30.0
	mist.gravity_scale = 0.0
	run_move(a2, "sound_lance", 0, 1, 30)
	check(r2.health < 100.0, "the lance passes through mist (and hits R: %.1f)" % r2.health)


func test_tremor_hum_knocks_over_cracks_walls_and_pops_stones() -> void:
	var s := _duel(12.0)
	var a: ActorState = s[0]
	var wall := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, 60.0, Vector3(0, 0, a.pos.z - 6.0), "test")
	h.w.mass_ledger.ground_taken += 60.0
	wall.wall_yaw = 0.0
	wall.wall_half = Vector3(2.5, 0.75, 0.28)
	wall.wall_rise = 1.0
	wall.static_body = true
	wall.props["standing"] = 999.0
	var stone := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 8.0, Vector3(0, 0.2, a.pos.z - 3.0), "test")
	h.w.mass_ledger.ground_taken += 8.0
	stone.on_ground = true
	var base := snap(h.w)
	run_move(a, "sound_tremor", 0, 1, 14)
	var ws := bodies_tagged("tremor")
	check(ws.size() == 1 and ws[0].form == Sim.Form.WAVE and ws[0].mat == Sim.Mat.AIR, "an AIR wave (tag tremor)")
	near(float(ws[0].props.speed), 18.0, 1e-6, "18 m/s")
	h.step(60)
	check(h.has_event("pop"), "the loose stone is popped into the air")
	check(wall.wall_damage > 0.0 or not wall.alive, "the wall is cracked (damage %.2f)" % wall.wall_damage)
	ledgers_ok(base, "tremor", 1e-4)
	# knocks the rival over
	var s2 := _duel(7.0)
	var a2: ActorState = s2[0]
	var r2: ActorState = s2[1]
	run_move(a2, "sound_tremor", 0, 1, 80)
	check(r2.balance < 80.0 or r2.health < 100.0, "the rival is knocked about (balance %.0f health %.0f)" % [r2.balance, r2.health])


func test_echo_ring_reveals_the_hidden_and_interrupts_channels() -> void:
	var s := _duel(3.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = false
	Status.apply(h.w, r, "concealed", 5.0, 1.0, r.id)
	Status.apply(h.w, r, "fogwalk", 5.0, 1.0, r.id)
	var it := h.it(r)
	var inst := h.w.start_action(r, "earth_tech", it, {"slot": "tech", "tier": 0})
	it.tech_held = true
	h.step(20)
	check(r.action != null and r.action.phase == ActionInst.P.CHANNEL, "setup: R holds a technique (channel)")
	run_move(a, "sound_echo_ring", 0, 1, 24)
	check(not Status.has(r, "concealed") and not Status.has(r, "fogwalk"), "hiding is revealed")
	check(Status.has(r, "revealed"), "marked revealed")
	check(h.has_event("revealed", "actor", r.id), "revealed event")
	check(h.has_event("disrupt", "actor", r.id), "and the channel is interrupted (P 10 >= 6)")
	it.tech_held = false
	var far := _duel(9.0)
	run_move(far[0], "sound_echo_ring", 0, 1, 24)
	check(far[1].health == 100.0, "outside r 4 m: nothing")


func test_sound_barrier_shatters_ice_and_a_perfect_one_throws_sound_back() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	h.press(a, "guard")
	h.step(30)
	var base := snap(h.w)
	var shard := _ice_shard(a.chest() + a.forward() * 6.0)
	shard.vel = (a.chest() - shard.pos).normalized() * 24.0
	shard.attack_id = h.w.new_attack_id()
	shard.attack_owner = r.id
	shard.hit_set[r.id] = true
	base = snap(h.w)
	h.step(40)
	check(h.events("interaction").any(func(e): return String(e.threat) == "ice" and String(e.counter) == "barrier_sound" and e.outcome == "shatter"), "ice shatters against the barrier")
	check(a.health == 100.0, "A is unhurt")
	ledgers_ok(base, "barrier ice", 1e-5)
	h.release(a, "guard")
	h.step(30)
	# Echo Return: a perfect guard against sound hits the source with its own sound
	var s2 := _duel(4.0)
	var a2: ActorState = s2[0]
	var r2: ActorState = s2[1]
	r2.is_dummy = false
	h.press(a2, "guard")
	h.step(2)
	var hp := r2.health
	var v := Agent.of_volume(h.w, r2, null, &"sound", r2.chest(), (a2.chest() - r2.chest()).normalized(), {"P": 10.0})
	h.w.hit_actor(a2, {"attacker": r2.id, "attack_id": h.w.new_attack_id(), "damage": 10.0, "balance": 20.0, "knock": Vector3(0, 0, 4.0), "kind": "sound",
		"from": r2.chest(), "agent": v, "power": 10.0})
	check(a2.health == 100.0, "Echo Return: A takes nothing")
	check(r2.health < hp or h.has_event("reflect"), "and the sound goes back at its source (R %.1f)" % r2.health)


func test_thunder_step_and_ground_ping_from_the_guard() -> void:
	var s := _duel(5.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	r.pos = a.pos + Vector3(0, 0, -3.5)
	h.press(a, "guard")
	h.step(20)
	h.flick(a, "guard", Sim.Gesture.UP)
	h.step(3)
	check(a.action != null and a.action.id == "sound_thunder_step", "guard flick up = Thunder Step")
	h.release(a, "guard")
	var z0 := a.pos.z
	h.step(30)
	check(z0 - a.pos.z > 2.5, "dashes ~4 m forward (%.1f)" % (z0 - a.pos.z))
	check(r.health < 100.0 or r.balance < 100.0, "and booms into the rival")
	# Ground Ping
	var s2 := _duel(8.0)
	var a2: ActorState = s2[0]
	var r2: ActorState = s2[1]
	r2.pos = a2.pos + Vector3(0, 0, -5.0)
	Status.apply(h.w, r2, "concealed", 6.0, 1.0, r2.id)
	var surge := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.WAVE, 14.0, Vector3(a2.pos.x, 0, a2.pos.z - 7.0), "test")
	h.w.mass_ledger.ground_taken += 14.0
	surge.tag = &"sand_surge"
	surge.wave_dir = Vector3(0, 0, 1)
	surge.wave_budget = 14.0
	surge.wave_width = 2.0
	surge.power = 8.0
	surge.vel = Vector3(0, 0, 7.5)
	surge.props["speed"] = 7.5
	surge.attack_id = h.w.new_attack_id()
	surge.attack_owner = r2.id
	surge.hit_set[r2.id] = true
	var base := snap(h.w)
	h.press(a2, "guard")
	h.step(20)
	h.flick(a2, "guard", Sim.Gesture.DOWN)
	h.step(3)
	check(a2.action != null and a2.action.id == "sound_ping", "guard flick down = Ground Ping")
	h.release(a2, "guard")
	h.step(20)
	check(h.has_event("wave_disrupted") or surge.form != Sim.Form.WAVE, "the sand surge is stilled by the ping")
	check(not Status.has(r2, "concealed"), "the burrower is revealed")
	ledgers_ok(base, "ping", 1e-5)


func test_flight_hovers_keeps_every_attack_and_ends_with_a_glide() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	var f0 := a.focus
	h.press(a, "tech")
	h.step(1)
	h.release(a, "tech")
	h.step(60)
	check(Status.has(a, "flight") and a.flying and a.stance == "flight", "Flight is on")
	check(a.pos.y > 1.9 and a.pos.y < 2.5, "rises to ~2.2 m (%.2f)" % a.pos.y)
	check(a.action == null, "the flight move itself is over: all attacks are usable")
	check(not zones_tagged("flight_field").is_empty(), "driven by a flight field attached to the fighter")
	check(f0 - a.focus > 6.0, "paid 6 + 10/s upkeep (%.1f)" % (f0 - a.focus))
	# ground lines pass under (a tagged wave under the flyer does not hit)
	var wave := h.w.spawn_body(Sim.Mat.AIR, Sim.Form.WAVE, 0.0, Vector3(a.pos.x, 0, a.pos.z + 2.0), "test")
	wave.tag = &"dust_line"
	wave.wave_dir = Vector3(0, 0, -1)
	wave.wave_budget = 6.0
	wave.wave_width = 2.0
	wave.props["speed"] = 8.0
	wave.attack_id = h.w.new_attack_id()
	wave.attack_owner = r.id
	wave.damage = 10.0
	wave.balance_damage = 30.0
	wave.hit_set[r.id] = true
	var hp := a.health
	h.step(40)
	check(a.health == hp, "a ground line passes under a flyer")
	# an attack works while flying
	h.press(a, "attack")
	h.step(1)
	h.release(a, "attack")
	h.step(2)
	check(a.action != null and a.action.id == "sound_clap", "Clap while flying")
	h.step(40)
	# press again: land
	h.press(a, "tech")
	h.step(2)
	h.release(a, "tech")
	h.step(14)
	check(not Status.has(a, "flight") and not a.flying, "pressing again ends the flight")
	h.step(120)
	check(a.grounded and a.pos.y < 0.2, "and glides down to the ground (%.2f)" % a.pos.y)
	check(zones_tagged("flight_field").is_empty(), "the field is gone")


func test_flight_is_vulnerable_to_gusts_and_a_downdraft_and_costs_focus() -> void:
	# a flyer loses x1.5 balance to the same Palm Gust
	var losses := []
	for flying in [false, true]:
		h = SimHarness.new(3)
		var a := h.actor("A", Vector3(0, 0, 3), 0, {}, Sim.Element.AIR)
		var r := h.actor("R", Vector3(0, 0, 0), 1, {}, Sim.Element.AIR)
		a.subs[3] = 0
		r.subs[3] = 3
		r.is_dummy = flying == false
		h.step(20)
		if flying:
			r.is_dummy = false
			h.press(r, "tech")
			h.step(1)
			h.release(r, "tech")
			h.step(60)
			check(Status.has(r, "flight"), "R flies")
		var b0 := r.balance
		run_move(a, "air_attack", 0, 1, 20)
		losses.append(b0 - r.balance)
	check(float(losses[1]) > float(losses[0]) * 1.3, "a flyer loses x1.5 balance to the gust (%.1f vs %.1f)" % [losses[1], losses[0]])
	# a Downdraft slams a flyer to the ground and ends the flight
	var s := duel(0, Sim.Element.AIR, 3, 3.0)
	var a2: ActorState = s[0]
	var r2: ActorState = s[1]
	r2.subs[3] = 3
	r2.is_dummy = false
	h.press(r2, "tech")
	h.step(1)
	h.release(r2, "tech")
	h.step(60)
	check(r2.flying, "R flies")
	h.press(a2, "guard")
	h.step(20)
	h.flick(a2, "guard", Sim.Gesture.DOWN)
	h.release(a2, "guard")
	h.step(20)
	check(not r2.flying and not Status.has(r2, "flight"), "Downdraft ends the flight")
	# no Focus: the flight ends
	var s3 := _duel(10.0)
	var a3: ActorState = s3[0]
	h.press(a3, "tech")
	h.step(1)
	h.release(a3, "tech")
	h.step(30)
	a3.focus = 0.4
	h.step(30)
	check(not a3.flying and h.has_event("insufficient"), "out of Focus: the flight ends")


func test_boom_step_and_hover() -> void:
	var s := _duel(4.0)
	var a: ActorState = s[0]
	var r: ActorState = s[1]
	r.pos = a.pos + Vector3(0, 0, -5.5)
	h.it(a).move = Vector3(0, 0, -1)
	var z0 := a.pos.z
	h.press(a, "evade")
	h.step(30)
	check(a.action == null or a.action.id == "sound_boom_step", "the evade is the Boom Step")
	check(h.has_event("evade", "move", "sound_boom_step"), "evade event")
	check(z0 - a.pos.z > 4.5, "a long dash (%.1f m)" % (z0 - a.pos.z))
	check(r.balance < 100.0 or r.health < 100.0 or r.vel.length() > 0.0, "ends in a boom that hits what is near")
	# Hover: hold the current height
	var s2 := _duel(10.0)
	var a2: ActorState = s2[0]
	a2.pos.y = 2.0
	a2.grounded = false
	a2.vel.y = 0.0
	h.press(a2, "evade")
	h.it(a2).evade_held = true
	h.step(16)
	check(a2.action != null and a2.action.id == "sound_hover", "held 0.2 s: Hover")
	h.step(60)
	check(a2.pos.y > 1.5 and a2.pos.y < 2.5, "holds its height (%.2f)" % a2.pos.y)
	h.it(a2).evade_held = false
	h.step(30)


func test_sound_column_cells_at_reference_powers() -> void:
	var s := _duel(10.0)
	var a: ActorState = s[0]
	var clap := func(tier: int) -> Agent:
		return Agent.of_move(h.w, a, "sound_clap", tier, false)
	check(Interactions.predict(h.w, threat(h.w, "ice", 9.0, 6.0), clap.call(0)).outcome == "air_shatter", "Clap shatters ice (x2.5)")
	check(Interactions.predict(h.w, threat(h.w, "glass", 9.0, 6.0), clap.call(0)).outcome == "air_shatter", "and glass")
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), clap.call(2)).outcome == "air_shatter", "Roar T2 (22 vs 17) shatters a stone shot")
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), clap.call(1)).outcome == "bend", "Shout T1 (14 vs 17, 0.82) only bends it")
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), clap.call(0)).outcome == "pass", "Clap T0 (8 vs 17, 0.47) does nothing")
	check(Interactions.predict(h.w, threat(h.w, "stone_heavy", 31.5, 45.0), clap.call(3)).outcome == "air_shatter", "Resonance T3 (32 vs 31.5) shatters a heavy stone")
	check(Interactions.predict(h.w, threat(h.w, "hot_rock", 26.8, 20.0), clap.call(2)).outcome == "air_shatter", "hot rock is brittle (x1.3): Roar shatters it")
	for t in ["boulder", "magma", "lava_wave"]:
		check(Interactions.predict(h.w, threat(h.w, t, 60.0, 50.0), clap.call(3)).outcome == "pass", "%s: FAIL (sound does nothing)" % t)
	check(Interactions.predict(h.w, threat(h.w, "sand", 10.0, 8.0), clap.call(2)).outcome == "pass", "sand absorbs sound")
	check(Interactions.predict(h.w, threat(h.w, "sand_cloud", 8.0, 6.0), clap.call(3)).outcome == "weaken", "Roar T3 weakens a sand cloud")
	check(Interactions.predict(h.w, threat(h.w, "mist", 3.0, 2.0), clap.call(2)).outcome == "air_compress", "Roar rains out mist")
	check(Interactions.predict(h.w, threat(h.w, "mist", 3.0, 2.0), clap.call(0)).outcome == "pass", "a Clap does not")
	check(Interactions.predict(h.w, threat(h.w, "water", 9.6, 12.0), clap.call(2)).outcome == "weaken", "Roar atomises a stream")
	check(Interactions.predict(h.w, threat(h.w, "water", 9.6, 12.0), clap.call(1)).outcome == "pass", "Shout passes through water")
	check(Interactions.predict(h.w, threat(h.w, "flame", 8.0, 0.0, "H"), clap.call(2)).outcome == "extinguish", "Roar blows a small flame out")
	check(Interactions.predict(h.w, threat(h.w, "blue_fire", 16.0, 0.0, "H"), clap.call(3)).outcome == "pass", "blue fire does not care")
	check(Interactions.predict(h.w, threat(h.w, "lightning", 24.0, 0.0, "E"), clap.call(3)).outcome == "pass", "lightning passes")
	check(Interactions.predict(h.w, threat(h.w, "blast", 16.0, 0.0, "P"), clap.call(2)).outcome == "weaken", "sound weakens a blast (x0.6)")
	check(Interactions.predict(h.w, threat(h.w, "tornado", 30.0, 0.0, "P"), clap.call(3)).outcome == "air_shrink", "Resonance weakens a tornado (x0.6)")
	check(Interactions.predict(h.w, threat(h.w, "vacuum", 18.0, 0.0, "P"), clap.call(3)).outcome == "pass", "no medium in a vacuum")
	# the barrier
	var bar := Agent.of_move(h.w, a, "sound_barrier", 0, false)
	check(bar.ccls == &"barrier_sound" and absf(bar.power - 12.0) < 1e-6, "Sound Barrier CP 12")
	check(Interactions.predict(h.w, threat(h.w, "ice", 9.0, 6.0), bar).outcome == "air_shatter", "ice shatters on it")
	check(Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), bar).outcome == "bend", "a stone shot is only bent (0.7)")
	check(Interactions.predict(h.w, threat(h.w, "boulder", 110.0, 200.0), bar).outcome == "overwhelm", "a boulder ignores it")
	check(Interactions.predict(h.w, threat(h.w, "vacuum", 18.0, 0.0, "P"), bar).outcome == "overwhelm", "a vacuum nullifies it")
	check(Interactions.predict(h.w, threat(h.w, "sound", 10.0, 0.0, "P"), bar).outcome == "absorb", "sound is cancelled")
	var perf := Agent.of_move(h.w, a, "sound_barrier", 0, true)
	check(Interactions.predict(h.w, threat(h.w, "sound", 10.0, 0.0, "P"), perf).outcome == "reflect", "Echo Return: reflected")
	# the ping
	check(Interactions.predict(h.w, threat(h.w, "sand_surge", 20.0, 14.0), Agent.of_move(h.w, a, "sound_ping", 1, false)).outcome == "air_still", "Ground Ping T1 (x1.5 on a sand surge) stills it")
	check(Interactions.predict(h.w, threat(h.w, "lava_wave", 27.3, 20.0), Agent.of_move(h.w, a, "sound_ping", 3, false)).outcome == "pass", "but lava is none of its business")
	check(Interactions.predict(h.w, threat(h.w, "sand_surge", 34.0, 14.0), Agent.of_move(h.w, a, "sound_ping", 0, false)).outcome == "weaken", "a big surge is only weakened by a weak ping (partial band)")


func test_sound_kit_is_deterministic_and_ledgers_balance() -> void:
	var hashes := []
	for k in 2:
		var s := _duel(8.0)
		var a: ActorState = s[0]
		var r: ActorState = s[1]
		var base := snap(h.w)
		h.launch_at(a, "stone", 20.0, 8.0, Sim.AMBIENT_C, "", r, 6.0)
		run_move(a, "sound_clap", 2, 30, 30)
		run_move(a, "sound_lance", 1, 20, 30)
		run_move(a, "sound_tremor", 1, 1, 60)
		ledgers_ok(base, "sound exchange", 1e-5)
		finite_world("sound")
		hashes.append("%s|%s|%d|%.4f|%.4f" % [str(a.pos), str(r.pos), h.w.bodies.size(), a.focus, r.health])
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: %s vs %s" % [hashes[0], hashes[1]])
