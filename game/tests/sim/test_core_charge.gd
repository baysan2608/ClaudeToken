extends TestCase
## Moveset engine: unified charge tiers (docs/MOVESET.md §4; COMBAT_SPEC "Engine" §E3).

var h: SimHarness
var p: ActorState


func _setup() -> void:
	h = SimHarness.new(2)
	h.begin_scope()
	Moves.register("t_shot", {"element": 0, "verb": "projectile", "startup": 0.2, "active": 0.05, "recovery": 0.3,
		"cost": 5.0, "heavy_cost": 9.0, "charge_drain": 8.0, "source": "ground", "mat": "stone", "mass": 10.0, "speed": 18.0,
		"tiers": {"t1": {"mass": 16.0}, "t2": {"mass": 22.0}, "t3": {"mass": 30.0, "on_impact": "shatter"}}})
	Moves.bind(0, 1, "strike", "t_shot")
	p = h.actor("P", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	var o := h.actor("O", Vector3(0, 0, -8), 1, {}, Sim.Element.FIRE)
	o.is_dummy = true
	h.sub(p, 1)
	h.step(10)
	h.log.clear()


func test_tier_math_and_param_ladder() -> void:
	var d := {"heavy_min": 0.55, "tiers": {"t1": {"a": 1}, "t3": {"a": 3, "b": 9}}, "a": 0, "b": 5}
	check(Charge.max_tier(d) == 3, "t3 data -> max tier 3")
	check(Charge.tier_times(d) == [0.55, 1.0, 1.8], "heavy_min gates T1 (%s)" % [Charge.tier_times(d)])
	check(Charge.tier_for(d, 0.5) == 0 and Charge.tier_for(d, 0.55) == 1 and Charge.tier_for(d, 1.0) == 2 and Charge.tier_for(d, 5.0) == 3, "tier_for")
	check(Charge.max_tier({"heavy_min": 0.4}) == 1, "legacy charge moves stop at T1")
	check(Charge.max_tier({}) == 0, "no charge data: T0 only")
	check(Charge.tier_times({"tier_times": [0.65, 1.2, 1.8]}) == [0.65, 1.2, 1.8], "per-move tier_times")
	check(Charge.pget(d, 0, "a") == 0, "T0 reads the def")
	check(Charge.pget(d, 1, "a") == 1, "T1 reads t1")
	check(Charge.pget(d, 2, "a") == 1, "T2 inherits t1 (ladder)")
	check(Charge.pget(d, 3, "b") == 9 and Charge.pget(d, 2, "b") == 5, "t3 only at T3")
	check(Charge.pget(d, 3, "zz", 7) == 7, "default")
	check(Charge.counter_power({"counter": {"power": [22, 30, 40, 55]}}, 2) == 40.0, "counter power by tier")
	check(Charge.counter_power({"counter": {"power": 12}}, 3) == 12.0, "scalar counter power")


func test_tiers_events_drain_and_release() -> void:
	_setup()
	var f0 := p.focus
	h.press(p, "attack")
	h.step(115)                              # 1.92 s
	var ev := h.events("charge").filter(func(e): return e.actor == p.id)
	check(ev.size() == 3, "three tier-ups (%d)" % ev.size())
	if ev.size() == 3:
		check(ev[0].tier == 1 and ev[1].tier == 2 and ev[2].tier == 3 and ev[2].ready and not ev[1].ready, "T1 T2 T3, ready at the top")
		check(ev[0].move == "t_shot" and ev[0].sub == 1, "event fields")
		near(float(ev[1].tick - ev[0].tick), 36.0, 1.0, "T1 0.40 s -> T2 1.00 s")
	var prog := Charge.progress(p.action)
	check(prog == Vector2(3, 1.0), "progress at the top (%s)" % prog)
	var drained := f0 - 5.0 - p.focus
	near(drained, 8.0 * (1.92 - 0.40), 0.6, "drain 8 Focus/s from T1 (%.2f)" % drained)
	h.release(p, "attack")
	h.step(2)
	var launch := h.last_event("launch")
	var b := h.w.get_body(int(launch.get("body", -1)))
	check(b != null and is_equal_approx(b.mass, 30.0) and b.tier == 3, "released at T3: 30 kg, tier 3")
	check(b != null and String(b.props.get("on_impact", "")) == "shatter", "T3 property")
	check(b != null and b.residual_authority > 0.8 and b.residual_authority <= 0.9, "cohesion 0.6 + 0.1·3, decaying (%.3f)" % [b.residual_authority if b else 0.0])
	h.end_scope()


func test_stall_on_empty_focus_keeps_the_tier() -> void:
	_setup()
	h.press(p, "attack")
	h.step(30)
	p.focus = 1.0
	h.step(60)
	var ins := h.events("insufficient").filter(func(e): return e.get("reason", "") == "charge")
	check(ins.size() == 1, "one insufficient event (%d)" % ins.size())
	check(p.action != null and p.action.tier() == 1, "stalled at T1 (%d)" % [p.action.tier() if p.action else -1])
	h.release(p, "attack")
	h.step(3)
	check(h.has_event("launch"), "a stalled charge still releases")
	h.end_scope()


func test_hit_interrupts_a_charge_and_spent_focus_is_lost() -> void:
	_setup()
	h.press(p, "attack")
	h.step(40)
	var f := p.focus
	h.w.hit_actor(p, {"attacker": 2, "attack_id": h.w.new_attack_id(), "damage": 5.0, "balance": 30.0, "kind": "stone"})
	h.step()
	check(p.action == null and h.has_event("interrupt"), "interrupted")
	check(p.focus <= f + 0.01, "no refund")
	h.release(p, "attack")
	h.step(20)
	check(not h.events("launch").any(func(e): return e.actor == p.id), "nothing released")
	h.end_scope()


func test_legacy_strikes_stay_t0_t1() -> void:
	h = SimHarness.new(1)
	var f := h.actor("F", Vector3(0, 0, 4), 0, {}, Sim.Element.FIRE)
	h.actor("T", Vector3(0, 0, -4), 1, {}, Sim.Element.EARTH)
	h.step(10)
	h.press(f, "attack")
	h.step(80)
	var ev := h.events("charge").filter(func(e): return e.actor == f.id)
	check(ev.size() == 1 and ev[0].tier == 1, "one tier-up to T1 for a legacy strike (%d)" % ev.size())
	check(not h.has_event("insufficient"), "no drain on legacy strikes")
	near(f.focus, 100.0, 1e-6, "legacy hold costs no extra Focus")
	h.release(f, "attack")
	h.step(5)
	check(h.has_event("flare", "heavy", true), "legacy blaze")
