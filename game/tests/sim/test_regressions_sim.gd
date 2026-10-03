extends TestCase
## Regressions for verified simulation findings: grip requests that outlive their action, technique
## cancels during startup / buffer, a stale buffered press eating a guard, held water and walls
## outliving their guard, pours through thin walls, lava at rest in water, remnant lifetime and trim
## order, guard element switching, attacker Balance below 0, unpaid Earth heaves, and the
## waterskin / steam-cap ledger holes. Each scenario failed before its fix.

var h: SimHarness


func _events_for(type: String, a: ActorState) -> Array:
	return h.events(type).filter(func(e): return e.get("actor", -1) == a.id)


## Energy identity residual: system_energy() - ledger_balance() must stay constant.
func _ident() -> float:
	return h.w.system_energy() - h.w.ledger_balance()


# ================================================================ grips outliving their action

func test_earth_tech_tap_with_a_stone_in_reach_leaves_no_orphan_hold() -> void:
	for hold in [1, 4, 8]:
		h = SimHarness.new(1)
		var a := h.actor("A", Vector3(0, 0, 6), 0, {}, Sim.Element.EARTH)
		h.actor("T", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
		var s := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(1.0, 0.2, 3.0), "scenario")
		h.step(30)
		var rest := s.pos
		h.press(a, "tech")
		h.step(hold)
		h.release(a, "tech")
		h.step(60)
		check(_events_for("whiff", a).size() == 1, "hold %d: the tap whiffs" % hold)
		check(_events_for("control_won", a).is_empty(), "hold %d: no grip is granted after the whiff" % hold)
		check(a.held_body == -1 and s.controller == -1, "hold %d: no orphan hold (held %d, stone ctl %d)" % [hold, a.held_body, s.controller])
		check(s.pos.distance_to(rest) < 0.05, "hold %d: the stone stays where it lay (%s, was %s)" % [hold, s.pos, rest])


func test_magma_grip_on_the_last_window_tick_whiffs_without_holding() -> void:
	h = SimHarness.new(11)
	var p := h.actor("P", Vector3(0, 0, 6), 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	var o := h.actor("O", Vector3(0, 0, -8), 1, {}, Sim.Element.EARTH)
	h.step(30)
	h.press(o, "attack")
	h.step()
	h.release(o, "attack")
	h.until(func(): return h.has_event("launch"), 60)
	var st := h.w.get_body(int(h.last_event("launch").body))
	h.step(6)
	h.log.clear()
	h.press(p, "tech")
	var stale := false
	for k in 90:
		h.step()
		if st.controller == p.id and (p.action == null or p.action.phase != ActionInst.P.CHANNEL):
			stale = true
	check(_events_for("whiff", p).size() == 1, "setup: the stone reached P's grip range only after the window")
	check(_events_for("control_won", p).is_empty(), "no grip granted on the whiff tick")
	check(not stale, "P never holds the stone outside the channel")
	check(p.held_body == -1, "P holds nothing")


func test_grip_is_withdrawn_when_a_later_actor_staggers_the_requester_that_tick() -> void:
	# End to end: B (higher id) flares A on the tick A's seize is queued.
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 0), 0, {}, Sim.Element.EARTH)
	var b := h.actor("B", Vector3(0, 0, -2.5), 1, {}, Sim.Element.FIRE)
	var s := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(1.5, 0.2, -1.5), "scenario")
	h.step(30)
	h.press(a, "tech")
	h.step(3)
	h.press(b, "attack")
	h.step()
	h.release(b, "attack")
	var orphan := false
	for k in 60:
		h.step()
		if s.controller == a.id and (a.action == null or a.action.id != "earth_tech"):
			orphan = true
	check(_events_for("stagger", a).size() >= 1, "setup: B's flare staggered A")
	check(not orphan, "A never holds the stone without its technique")
	# White box, same ordering forced: A channels, a grip is queued, then a hit interrupts A.
	for staggered in [false, true]:
		h = SimHarness.new(1)
		var c := h.actor("C", Vector3(0, 0, 6), 0, {}, Sim.Element.EARTH)
		h.actor("T", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
		var behind := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0, 0.2, 8.0), "scenario")
		h.step(30)
		h.press(c, "tech")
		h.until(func(): return c.action != null and c.action.phase == ActionInst.P.CHANNEL, 30)
		h.w.request_grip(c, behind, 0.9, "seize")
		if staggered:
			h.w._stagger(c, "light", 0.26, {})
		h.w._resolve_grips()
		if staggered:
			check(behind.controller == -1 and c.held_body == -1, "a staggered requester gets nothing (ctl %d)" % behind.controller)
		else:
			check(behind.controller == c.id, "control: an uninterrupted request is granted")
			check(behind.hold_point.distance_to(c.hand_point()) < 1e-4, "a newly won body homes to the hand, not a stale point (%s)" % behind.hold_point)


# ================================================================ technique cancels

func _cancel_case(elem: int, kit: Dictionary, setup: Callable, same_tick: bool) -> ActorState:
	h = SimHarness.new(3)
	var p := h.actor("P", Vector3(-8, 0, 8), 0, kit, elem)
	var o := h.actor("O", Vector3(-8, 0, -4), 1, {}, Sim.Element.EARTH)
	o.is_dummy = true
	setup.call(p)
	h.step(20)
	h.log.clear()
	h.press(p, "tech")
	if not same_tick:
		h.step(3)   # inside every technique's startup
	h.cancel_tech(p)
	h.step(90)
	return p


func _stone_ahead(p: ActorState) -> void:
	h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, p.pos + Vector3(0, 0.3, -3.0), "scenario")


func test_technique_cancel_during_startup_or_with_the_press_never_commits() -> void:
	var cases := [
		["water", Sim.Element.WATER, {}, func(_p: ActorState): pass],
		["fire vent", Sim.Element.FIRE, {}, func(p: ActorState): p.heat_reserve = 120.0],
		["fire heat", Sim.Element.FIRE, {"magma": true}, _stone_ahead],
		["earth", Sim.Element.EARTH, {}, _stone_ahead],
		["air", Sim.Element.AIR, {}, func(_p: ActorState): pass],
	]
	for c in cases:
		for same_tick in [false, true]:
			var label := "%s%s" % [c[0], " (press+cancel)" if same_tick else ""]
			var p := _cancel_case(c[1], c[2], c[3], same_tick)
			for bad in ["launch", "vent", "control_won", "updraft", "rip"]:
				check(_events_for(bad, p).is_empty(), "%s: a cancel never commits (%s)" % [label, bad])
			check(p.held_body == -1, "%s: nothing held" % label)
			check(p.grounded, "%s: still on the ground" % label)
			if same_tick:
				check(_events_for("action", p).is_empty(), "%s: a press arriving with its cancel starts nothing" % label)
			else:
				check(_events_for("cancel", p).size() == 1, "%s: the cancel is acknowledged" % label)


func test_buffered_technique_press_is_dropped_by_a_cancel() -> void:
	for cancel in [false, true]:
		h = SimHarness.new(6)
		var p := h.actor("P", Vector3(-8, 0, 8), 0, {}, Sim.Element.WATER)
		var o := h.actor("O", Vector3(-8, 0, -4), 1, {}, Sim.Element.EARTH)
		o.is_dummy = true
		h.step(20)
		h.press(p, "attack")
		h.release(p, "attack")
		h.step()
		h.until(func(): return p.action != null and p.action.phase == ActionInst.P.RECOVERY and p.action.t >= float(p.action.def.recovery) - 0.10, 120)
		h.log.clear()
		h.press(p, "tech")
		h.step()
		check(p.buffered == "tech", "setup: the technique press is buffered during the recovery")
		h.step(2)
		if cancel:
			h.cancel_tech(p)
		h.step(40)
		var started := h.events("action").any(func(e): return e.actor == p.id and e.move == "water_tech")
		if cancel:
			check(not started, "the cancelled buffered technique never starts")
			check(_events_for("launch", p).is_empty(), "nothing is fired")
		else:
			check(started, "control: without the cancel the buffered technique starts")


# ================================================================ buffer vs guard

func test_guard_pressed_after_a_buffered_attack_is_not_eaten() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	h.actor("T", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
	h.step(30)
	h.press(a, "attack")
	h.step(1)
	h.release(a, "attack")
	h.until(func(): return a.action != null and a.action.phase == ActionInst.P.RECOVERY, 60)
	h.step(4)
	h.press(a, "attack")   # mashed during the uncancellable part of the recovery: buffered
	h.it(a).attack_held = false
	h.step(1)
	check(a.buffered == "attack", "setup: the attack is buffered")
	h.until(func(): return a.action.t / float(a.action.def.recovery) >= float(a.action.def.cancel), 30)
	h.press(a, "guard")
	h.step(1)
	check(a.action != null and a.action.id == "guard", "the guard starts at the cancel point")
	for k in 20:
		h.step(1)
		if a.action == null or a.action.id != "guard" or not a.guarding:
			break
	check(a.action != null and a.action.id == "guard" and a.guarding, "the held guard keeps running (%s)" % (a.action.id if a.action else "none"))


# ================================================================ guard leftovers

func test_water_kept_through_a_non_water_guard_is_dropped() -> void:
	h = SimHarness.new(3)
	var a := h.actor("A", Vector3(0, 0, 6), 0, {}, Sim.Element.WATER)
	h.actor("O", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
	h.step(5)
	h.press(a, "tech")
	h.step(30)
	var b := h.w.held(a)
	if not check(b != null and b.is_water(), "setup: A holds drawn water"):
		return
	h.element(a, Sim.Element.EARTH)
	h.step(1)
	h.press(a, "guard")   # guard cancels the technique; an Earth guard makes no shield
	h.it(a).tech_held = false
	h.step(10)
	check(b.controller == -1 and a.held_body == -1, "the Earth guard drops the water (ctl %d)" % b.controller)
	h.release(a, "guard")
	h.step(20)
	check(a.action == null and a.held_body == -1, "nothing is held after the guard")


func test_non_earth_guard_lets_the_previous_wall_sink() -> void:
	h = SimHarness.new(3)
	var a := h.actor("A", Vector3(0, 0, 6), 0, {}, Sim.Element.EARTH)
	h.actor("O", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
	h.step(5)
	h.press(a, "guard")
	h.step(15)
	var wall := h.w.get_body(a.wall_body)
	if not check(wall != null and wall.wall_rise >= 1.0, "setup: the Earth guard raised a wall"):
		return
	h.release(a, "guard")
	h.element(a, Sim.Element.FIRE)
	h.step(7)
	h.press(a, "guard")   # Fire guard (buffered past the guard recovery) while the old wall sinks
	h.step(60)
	check(a.guarding and a.action != null and a.action.element == Sim.Element.FIRE, "setup: the Fire guard is up")
	check(a.wall_body == -1, "a Fire guard owns no wall (%d)" % a.wall_body)
	check(not wall.alive, "the old wall sank instead of being maintained (rise %.2f)" % wall.wall_rise)


func test_guard_rules_use_the_element_the_guard_started_with() -> void:
	for switch_to in [-1, Sim.Element.AIR]:
		h = SimHarness.new(1)
		var g := h.actor("G", Vector3(0, 0, 6), 0, {}, Sim.Element.FIRE)
		var o := h.actor("O", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
		h.step(30)
		h.press(g, "guard")   # a plain Fire guard
		h.step(30)            # well past the perfect window
		if switch_to >= 0:
			h.element(g, switch_to)
		h.press(o, "attack")
		h.step(1)
		h.release(o, "attack")
		h.until(func(): return h.has_event("launch"), 60)
		h.until(func(): return h.has_event("block") or h.has_event("deflect") or h.has_event("hit"), 90)
		var label := "switched to Air" if switch_to >= 0 else "no switch"
		check(g.action != null and g.action.id == "guard" and g.action.element == Sim.Element.FIRE, "%s: setup: the Fire guard is still running" % label)
		check(h.has_event("block") and not h.has_event("deflect"), "%s: the stone is blocked, not air-deflected" % label)
		check(g.health < 100.0, "%s: block chip damage applies (%.1f)" % [label, g.health])


func test_perfect_guard_that_empties_the_attackers_balance_knocks_them_down() -> void:
	h = SimHarness.new(1)
	var att := h.actor("ATT", Vector3(0, 0, 0), 0, {}, Sim.Element.FIRE)
	var d := h.actor("DEF", Vector3(0, 0, -2.0), 1, {}, Sim.Element.FIRE)
	h.step(60)
	att.balance = 10.0
	att.balance_idle = 0.0
	h.press(d, "guard")
	h.step(1)
	h.press(att, "attack")
	h.step(1)
	h.release(att, "attack")
	h.step(12)
	check(h.events("perfect_deflect").size() == 1, "setup: the flare met a perfect guard")
	check(att.balance >= 0.0, "Balance never goes below 0 (%.1f)" % att.balance)
	check(att.stun_kind == "knockdown", "Balance 0 knocks the attacker down (stun '%s')" % att.stun_kind)


# ================================================================ earth heavy cost

func test_earth_heavy_that_cannot_be_paid_is_a_light_shot() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	var t := h.actor("T", Vector3(0, 0, -4), 1, {}, Sim.Element.FIRE)
	h.step(60)
	a.focus = 10.0   # pays the 7 Focus shot, not the 7 more for the heave
	h.press(a, "attack")
	h.step(40)       # held past heavy_min
	h.release(a, "attack")
	h.until(func(): return h.has_event("launch"), 60)
	var launch := h.last_event("launch")
	var b := h.w.get_body(int(launch.body))
	check(_events_for("insufficient", a).any(func(e): return e.get("move", "") == "earth_heavy"), "the unpaid heave is reported")
	check(not launch.heavy, "it launches as a light shot")
	near(b.mass, Sim.STONE_SHOT_MASS, 1e-6, "no extra stone gathered")
	near(b.damage, float(Moves.DEFS.earth_attack.damage), 1e-6, "light damage")
	near(b.balance_damage, float(Moves.DEFS.earth_attack.balance), 1e-6, "light balance damage")
	h.until(func(): return _events_for("hit", t).size() > 0, 120)
	near(t.health, 100.0 - float(Moves.DEFS.earth_attack.damage), 1e-6, "T takes light damage")


# ================================================================ pour vs walls

func test_pour_point_blank_into_a_wall_stays_on_this_side() -> void:
	# Arena cover wall (z -1.25..-0.75): P pours south from just north of it.
	h = SimHarness.new(1)
	var p := h.actor("P", Vector3(-3.75, 0, -0.2), 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	var e := h.actor("E", Vector3(-3.75, 0, -6), 1, {}, Sim.Element.EARTH)
	var st := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(-3.75, 0.2, 0.3), "scenario")
	h.step(25)
	h.aim(p, Vector3(0, 0, -1))
	h.press(p, "tech")
	if not check(h.until(func(): return st.phase == Sim.Phase.MOLTEN, 240) > 0, "setup: the stone melted"):
		return
	h.release(p, "tech")
	h.until(func(): return h.has_event("wave_blocked") or st.form == Sim.Form.WAVE, 60)
	h.step(240)
	check(h.has_event("wave_blocked"), "the pour is blocked by the wall")
	check(st.pos.z > -0.75, "the lava stayed north of the wall (z %.2f)" % st.pos.z)
	check(e.health == 100.0 and _events_for("hit", e).is_empty(), "E behind the wall is untouched (%.1f)" % e.health)
	# Earth wall: E raises it 1.25 m in front; P pours into it from just the other side.
	h = SimHarness.new(1)
	p = h.actor("P", Vector3(0, 0, -2.9), 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	e = h.actor("E", Vector3(0, 0, -5.0), 1, {}, Sim.Element.EARTH)
	st = h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0, 0.2, -1.6), "scenario")
	h.step(25)
	h.press(e, "guard")
	h.aim(p, Vector3(0, 0, 1))
	h.press(p, "tech")
	if not check(h.until(func(): return st.phase == Sim.Phase.MOLTEN, 240) > 0, "setup: the stone melted"):
		return
	h.aim(p, Vector3(0, 0, -1))
	h.step(20)
	var wall := h.w.get_body(e.wall_body)
	if not check(wall != null and wall.wall_rise >= 1.0, "setup: E's wall is up"):
		return
	h.release(p, "tech")
	h.until(func(): return h.has_event("wave_blocked") or st.form == Sim.Form.WAVE, 60)
	h.step(120)
	check(st.pos.z > wall.pos.z + wall.wall_half.z, "the lava stayed on P's side of the earth wall (z %.2f, wall face %.2f)" % [st.pos.z, wall.pos.z + wall.wall_half.z])


# ================================================================ quench at rest

func test_lava_resting_in_the_pool_or_on_a_puddle_quenches() -> void:
	# P melts a stone while standing in the pool, is hit and drops the blob into the water.
	h = SimHarness.new(1)
	var p := h.actor("P", Vector3(10, 0, -1), 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	var o := h.actor("O", Vector3(10, 0, -9), 1, {}, Sim.Element.EARTH)
	var s := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(10, 0.0, -2.2), "scenario")
	h.step(30)
	h.aim(p, Vector3(0, 0, -1))
	h.press(p, "tech")
	if not check(h.until(func(): return s.phase == Sim.Phase.MOLTEN, 240) > 0, "setup: the stone melted"):
		return
	h.w.hit_actor(p, {"attacker": o.id, "attack_id": h.w.new_attack_id(), "damage": 5.0, "balance": 30.0, "kind": "stone", "from": o.pos})
	h.log.clear()
	var pool0 := h.w.pool.mass
	h.step(30)
	check(s.controller == -1 and h.w.arena.in_pool(s.pos.x, s.pos.z), "setup: the blob lies in the pool")
	check(s.phase == Sim.Phase.SOLID, "lava in the pool sets within 0.5 s (%s)" % s.describe())
	check(h.has_event("steam") and h.w.pool.mass < pool0, "the pool flash-boils (pool %.2f -> %.2f kg)" % [pool0, h.w.pool.mass])
	# A molten blob at rest on a puddle.
	h = SimHarness.new(2)
	var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 20.0, Vector3(-2, 0.0, 4.0), "scenario")
	pd.update_radius_puddle()
	pd.on_ground = true
	var blob := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.BLOB, 20.0, Vector3(-2, 0.06, 4.0), "scenario", Sim.STONE_MELT_C)
	blob.liquid = 1.0
	blob.phase = Sim.Phase.MOLTEN
	blob.on_ground = true
	var e0 := _ident()
	h.step(30)
	check(blob.phase == Sim.Phase.SOLID, "lava resting on a puddle sets within 0.5 s (%s)" % blob.describe())
	check(pd.mass < 20.0 and h.has_event("steam"), "the puddle boils (%.2f kg)" % pd.mass)
	near(_ident(), e0, 1e-6, "quenching keeps the energy identity")


# ================================================================ lifetime, trim, caps, ledgers

func test_reused_old_stone_is_not_removed_mid_flight() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, -4), 0, {}, Sim.Element.EARTH)
	var t := h.actor("T", Vector3(0, 0, 6), 1, {}, Sim.Element.EARTH)
	var s := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0.8, 0.2, -4.5), "scenario")
	s.max_life = Sim.REMNANT_LIFETIME
	h.step(30)
	s.age = Sim.REMNANT_LIFETIME - 0.1   # a remnant about to decay, lying at A's feet
	h.press(a, "attack")
	h.step(1)
	h.release(a, "attack")
	h.until(func(): return h.has_event("launch"), 60)
	check(int(h.last_event("launch").body) == s.id, "setup: A reused the old stone")
	h.until(func(): return not s.alive or _events_for("hit", t).size() > 0, 120)
	check(s.alive, "the thrown stone is not removed by lifetime (%s)" % str(h.events("despawn")))
	check(_events_for("hit", t).size() == 1, "and it reaches T")
	# A live projectile past its lifetime is never decayed mid-flight.
	var p2 := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 5.0, Vector3(8, 3, 8), "scenario")
	p2.max_life = 1.0
	p2.age = 5.0
	p2.attack_id = h.w.new_attack_id()
	p2.attack_owner = a.id
	p2.vel = Vector3(0, 2, 0)
	h.step(1)
	check(p2.alive, "an attacking body outlives max_life while it flies")


func test_trim_decays_the_longest_lying_remnant_not_a_reused_stone() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 6), 0, {}, Sim.Element.EARTH)
	var t := h.actor("T", Vector3(0, 0, -6), 1, {}, Sim.Element.FIRE)
	# One loose stone at A's feet (lowest id) and five remnants far away: 6 = at the cap.
	var mine := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0.8, 0.2, 6.5), "scenario")
	var others: Array[MatBody] = []
	for i in 5:
		others.append(h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(-12 + i * 1.2, 0.2, 12), "scenario"))
	h.step(20 * 60)   # all of them lie untouched for 20 s
	h.it(t).move = Vector3(1, 0, 0)
	h.press(a, "attack")
	h.step(1)
	h.release(a, "attack")
	h.until(func(): return h.has_event("launch"), 60)
	check(int(h.last_event("launch").body) == mine.id, "setup: A reused the loose stone")
	h.until(func(): return mine.on_ground and mine.attack_id == 0, 120)
	h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(10, 0.2, 12), "scenario")   # a 7th remnant
	h.step(40)
	check(mine.alive, "the stone that just landed survives the trim")
	check(others.filter(func(b: MatBody): return not b.alive).size() == 1, "one long-lying remnant decays instead")


func test_waterskin_transfers_keep_the_energy_identity() -> void:
	# Shield water drawn from a 0 degC slushy puddle returns to the skin (which holds water at ambient).
	h = SimHarness.new(5)
	var a := h.actor("D", Vector3(-4, 0, 5), 0, {}, Sim.Element.WATER)
	h.step(5)
	var puddle := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 4.0, Vector3(-4, 0, 6.5), "scenario")
	puddle.update_radius_puddle()
	puddle.temp = 0.0
	puddle.liquid = 0.7
	a.water_carried = 0.0
	var e0 := _ident()
	var m0 := h.w.water_mass()
	h.press(a, "tech")
	h.step(40)
	var held := h.w.held(a)
	if not check(held != null and held.thermal_energy() < -1.0, "setup: A holds cold slush"):
		return
	h.press(a, "guard")
	h.it(a).tech_held = false
	h.step(10)
	h.release(a, "guard")
	h.step(10)
	check(a.water_carried > 1.0 and a.held_body == -1, "setup: the shield water went back to the skin (%.2f kg)" % a.water_carried)
	near(_ident(), e0, 1e-6, "energy identity after the skin takes the water")
	near(h.w.water_mass(), m0, 1e-6, "water mass conserved")
	# Refilling the skin from a warm pool.
	h = SimHarness.new(5)
	var b := h.actor("B", Vector3(10, 0, -1), 0, {}, Sim.Element.WATER)
	b.water_carried = 0.0
	h.w.pool.temp = 40.0
	e0 = _ident()
	h.step(5)
	check(b.water_carried >= 6.0 - 1e-6, "setup: the skin refilled in the pool")
	near(_ident(), e0, 1e-6, "energy identity after a refill from a warm pool")


func test_cap_decaying_a_steam_cloud_records_its_water() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 6), 0, {}, Sim.Element.FIRE)
	h.step(2)
	var w := h.w
	w._spawn_steam(Vector3(0, 1, 0), 2.0)   # the earliest eligible body
	var water0 := w.water_mass()
	for i in 30:   # live projectiles fill the cap (never decayed)
		var s := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 5.0, Vector3(-10 + i * 0.6, 2.0, 0), "scenario")
		s.attack_id = w.new_attack_id()
		s.attack_owner = a.id
		s.vel = Vector3(0, 0, 1)
		w.mass_ledger.ground_taken += 5.0
	w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 5.0, Vector3(5, 2, 5), "scenario")
	w.mass_ledger.ground_taken += 5.0
	check(w.events.any(func(e): return e.type == "despawn" and e.reason == "cap"), "setup: the cap decayed a body")
	near(w.water_mass(), water0, 1e-9, "the decayed cloud's water is recorded")
