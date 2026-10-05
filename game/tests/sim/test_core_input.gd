extends TestCase
## Moveset engine: input to move (sub-element select, gesture slots and morphs, guard push/sink,
## evade_hold, chains) with test-local moves (docs/COMBAT_SPEC.md "Engine" §E2).

var h: SimHarness
var p: ActorState
var o: ActorState


func _setup(elem: int = Sim.Element.EARTH) -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.register("t_strike", {"element": 0, "verb": "projectile", "slot": "strike", "startup": 0.24, "active": 0.06,
		"recovery": 0.3, "cancel": 0.6, "heavy_min": 0.4, "cost": 5.0, "source": "ground", "mat": "stone", "mass": 10.0,
		"speed": 18.0, "tiers": {"t1": {"mass": 16.0}, "t2": {"mass": 22.0}, "t3": {"mass": 30.0}}})
	Moves.register("t_thrust", {"element": 0, "verb": "projectile", "slot": "thrust", "startup": 0.2, "active": 0.05,
		"recovery": 0.3, "cost": 6.0, "source": "ground", "mat": "stone", "mass": 6.0, "speed": 26.0, "gravity": 0.4, "tag": "spear",
		"tiers": {"t1": {"count": 2}, "t2": {"count": 3}}})
	Moves.register("t_wall", {"element": 0, "verb": "barrier", "barrier": "wall", "mat": "sand", "tag": "sand", "mass": 100.0,
		"cost": 7.0, "tiers": {"t1": {"mass": 130.0}, "t2": {"mass": 160.0}}, "counter": {"cls": "wall_sand"}})
	Moves.register("t_push", {"element": 0, "verb": "burst", "startup": 0.1, "active": 0.05, "recovery": 0.2, "at": "ahead",
		"distance": 2.0, "radius": 1.5, "power": 10.0})
	Moves.register("t_sink", {"element": 0, "verb": "zone", "startup": 0.1, "active": 0.05, "recovery": 0.2, "tag": "quicksand",
		"radius": 3.0, "life": 2.0})
	Moves.register("t_glide", {"element": 0, "verb": "mode", "kind": "skate", "speed_mult": 1.3, "upkeep": 4.0, "startup": 0.0,
		"active": 0.0, "recovery": 0.1})
	Moves.bind(0, 1, "strike", "t_strike")
	Moves.bind(0, 1, "thrust", "t_thrust")
	Moves.bind(0, 1, "guard", "t_wall")
	Moves.bind(0, 1, "push", "t_push")
	Moves.bind(0, 1, "sink", "t_sink")
	Moves.bind(0, 1, "evade_hold", "t_glide")
	p = h.actor("P", Vector3(0, 0, 4), 0, {}, elem)
	o = h.actor("O", Vector3(0, 0, -6), 1, {}, Sim.Element.FIRE)
	o.is_dummy = true
	h.step(10)
	h.log.clear()


func _done() -> void:
	h.end_scope()


func test_sub_select_emits_and_affects_the_next_action_only() -> void:
	_setup()
	h.press(p, "attack")
	h.step(2)
	check(p.action != null and p.action.id == "earth_attack" and p.action.sub == 0, "sub 0: legacy strike")
	h.sub(p, 1)
	h.step()
	check(p.sub() == 1, "sub switched")
	var ev := h.last_event("element")
	check(ev.get("sub", -1) == 1 and ev.get("element", -1) == 0, "element event carries the sub (%s)" % [ev])
	check(p.action != null and p.action.id == "earth_attack" and p.action.sub == 0, "the running action keeps its sub")
	h.release(p, "attack")
	h.until(func(): return p.action == null, 120)
	h.step(30)
	h.press(p, "attack")
	h.step(1)
	check(p.action != null and p.action.id == "t_strike" and p.action.sub == 1 and p.action.slot == "strike", "the next action uses sub 1")
	var act := h.events("action").filter(func(e): return e.move == "t_strike")
	check(not act.is_empty() and act[0].get("slot", "") == "strike" and act[0].get("sub", -1) == 1 and act[0].has("tier"), "action events gain sub, slot, tier")
	_done()


func test_press_with_gesture_starts_the_gesture_move() -> void:
	_setup()
	h.sub(p, 1)
	h.step()
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step()
	check(p.action != null and p.action.id == "t_thrust" and p.action.slot == "thrust", "desktop: press + flick on one tick = thrust (%s)" % [p.action.id if p.action else "none"])
	h.release(p, "attack")
	h.until(func(): return h.has_event("launch"), 60)
	var b := h.w.get_body(int(h.last_event("launch").body))
	check(b != null and b.tag == &"spear" and is_equal_approx(b.gravity_scale, 0.4), "the spear flies flat")
	check(b != null and b.attack_owner == p.id and b.tier == 0, "an attack of P at T0")
	_done()


func test_unbound_gesture_falls_back_to_the_strike() -> void:
	_setup()
	h.flick(p, "attack", Sim.Gesture.UP)   # sub 0: no thrust bound in the legacy kit
	h.step()
	check(p.action != null and p.action.id == "earth_attack" and p.action.slot == "strike", "legacy strike")
	_done()


func test_gesture_morph_inside_the_window_keeps_time_and_cost() -> void:
	_setup()
	h.sub(p, 1)
	h.step()
	var f0 := p.focus
	h.press(p, "attack")
	h.step(4)                                # 0.067 s into the strike startup
	check(p.action != null and p.action.id == "t_strike", "strike started")
	var total := p.action.total
	h.flick(p, "attack", Sim.Gesture.UP, false)
	h.step()
	check(p.action != null and p.action.id == "t_thrust", "morphed into the thrust")
	check(h.has_event("morph", "to", "t_thrust"), "morph event")
	check(p.action != null and p.action.total >= total, "elapsed time carried (%.3f >= %.3f)" % [p.action.total if p.action else 0.0, total])
	near(f0 - p.focus, 6.0, 1e-6, "paid Focus carried: 5 paid for the strike + 1 more for the 6-Focus thrust")
	_done()


func test_gesture_after_the_window_does_not_morph() -> void:
	_setup()
	h.sub(p, 1)
	h.step()
	h.press(p, "attack")
	h.step(9)                                # 0.15 s > 0.12 s
	h.flick(p, "attack", Sim.Gesture.UP, false)
	h.step()
	check(p.action != null and p.action.id == "t_strike", "too late: still the strike")
	check(not h.has_event("morph"), "no morph")
	_done()


func test_gesture_at_charge_release_morphs_with_the_tier() -> void:
	_setup()
	h.sub(p, 1)
	h.step()
	h.press(p, "attack")
	h.step(64)                               # 1.07 s: T2
	check(p.action != null and p.action.phase == ActionInst.P.CHARGE and p.action.tier() == 2, "charging at T2 (%d)" % [p.action.tier() if p.action else -1])
	h.release(p, "attack")
	h.flick(p, "attack", Sim.Gesture.UP, false)
	h.step()
	check(h.has_event("morph", "at", "release"), "morph at release")
	h.step(3)
	var launches := h.events("launch").filter(func(e): return e.actor == p.id)
	check(launches.size() == 3, "the thrust fired at the carried T2: 3 spears (%d)" % launches.size())
	for e in launches:
		var b := h.w.get_body(int(e.body))
		check(b != null and b.tier == 2, "spear carries tier 2")
	_done()


func test_guard_spec_barrier_then_push_and_sink() -> void:
	_setup()
	h.sub(p, 1)
	h.step()
	h.press(p, "guard")
	h.step(3)
	check(p.action != null and p.action.id == "guard" and p.action.data.get("spec", "") == "t_wall", "the action stays 'guard' with the spec")
	var wall := h.w.get_body(p.wall_body)
	check(wall != null and wall.mat == Sim.Mat.SAND and wall.tag == &"sand", "the spec raised a sand wall")
	check(wall != null and Interactions.counter_class(wall, h.w) == &"wall_sand", "counter class from the spec")
	h.step(62)                               # held 1.08 s: T2 thickens the wall
	check(wall != null and is_equal_approx(wall.mass, 160.0), "held to T2: 160 kg (%.1f)" % [wall.mass if wall else 0.0])
	h.flick(p, "guard", Sim.Gesture.UP)
	h.step()
	check(p.action != null and p.action.id == "t_push" and p.action.data.get("from_guard", false), "guard flick up = push")
	check(p.action != null and p.action.tier() == 2 and int(p.action.data.keep_wall) == wall.id, "push keeps the guard's tier and wall")
	h.step(5)
	check(wall.alive and wall.wall_rise > 0.9, "the wall is still maintained during the push")
	h.until(func(): return p.action == null, 60)
	# Sink
	h.release(p, "guard")
	h.step(30)
	h.press(p, "guard")
	h.step(3)
	h.flick(p, "guard", Sim.Gesture.DOWN)
	h.step(10)
	check(h.events("zone").any(func(e): return e.kind == "quicksand" and e.phase == "open"), "guard flick down = sink (zone opened)")
	_done()


func test_attack_press_still_cancels_a_guard_into_an_attack() -> void:
	_setup()
	h.press(p, "guard")
	h.step(10)
	h.press(p, "attack")
	h.step()
	check(p.action != null and p.action.id == "earth_attack", "legacy: attack during guard = attack")
	_done()


func test_evade_hold_morphs_after_0_2_s_when_bound() -> void:
	_setup()
	h.sub(p, 1)
	h.step()
	h.evade_hold(p, 6)
	check(not h.has_event("morph"), "a short evade stays an evade")
	h.until(func(): return p.action == null, 60)
	h.step(30)
	h.press(p, "evade")
	h.it(p).evade_held = true
	h.step(14)
	check(p.action != null and p.action.id == "t_glide" and p.action.slot == "evade_hold", "held 0.23 s: evade_hold mode")
	check(p.stance == "skate", "mode stance on")
	h.it(p).evade_held = false
	h.step(10)
	check(p.stance == "" and (p.action == null or p.action.id != "t_glide" or p.action.phase == ActionInst.P.RECOVERY), "released: mode ends")
	# sub 0: nothing bound
	h.sub(p, 0)
	h.step(30)
	h.press(p, "evade")
	h.it(p).evade_held = true
	h.step(14)
	check(p.action == null or p.action.id == "evade", "legacy evade is not morphed")
	h.it(p).evade_held = false
	_done()


func test_chain_window_after_contact() -> void:
	_setup()
	Moves.register("t_jab", {"element": 0, "verb": "cone", "startup": 0.05, "active": 0.05, "recovery": 0.4, "cancel": 0.8,
		"chain": 0.25, "range": 12.0, "angle": 30.0, "damage": 2.0, "balance": 2.0, "cls": "gust", "power": 5.0})
	Moves.bind(0, 1, "strike", "t_jab")
	h.sub(p, 1)
	h.step()
	o.is_dummy = true
	h.press(p, "attack")
	h.release(p, "attack")
	h.until(func(): return p.action != null and p.action.phase == ActionInst.P.RECOVERY, 30)
	check(p.action != null and p.action.data.get("contact", false), "the jab made contact")
	h.step(int(0.4 * 0.3 * 60))              # 30 % of recovery: past chain 0.25, before cancel 0.8
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step()
	check(p.action != null and p.action.id == "t_thrust", "chained into the thrust")
	check(h.has_event("chain"), "chain event")
	check(p.chain.n == 2 and (p.chain.slots as Array).has("thrust"), "string bookkeeping")
	_done()


func test_weave_switches_sub_element_inside_a_chain_for_6_focus() -> void:
	_setup()
	Moves.register("t_jab", {"element": 0, "verb": "cone", "startup": 0.05, "active": 0.05, "recovery": 0.4, "cancel": 0.8,
		"chain": 0.25, "range": 12.0, "angle": 30.0, "damage": 2.0, "balance": 2.0, "cls": "gust", "power": 5.0})
	Moves.bind(0, 1, "strike", "t_jab")
	Moves.bind(0, 2, "thrust", "t_thrust")
	h.sub(p, 1)
	h.step()
	h.press(p, "attack")
	h.release(p, "attack")
	h.until(func(): return p.action != null and p.action.phase == ActionInst.P.RECOVERY, 30)
	h.sub(p, 2)
	h.step(int(0.4 * 0.3 * 60))
	var f := p.focus
	h.flick(p, "attack", Sim.Gesture.UP)
	h.step()
	check(p.action != null and p.action.id == "t_thrust" and p.action.sub == 2, "woven into sub 2's thrust")
	check(h.has_event("weave"), "weave event")
	near(f - p.focus, 6.0 + 6.0, 1e-6, "weave 6 + thrust 6 Focus")
	check(p.chain.weaved, "once per string")
	_done()
