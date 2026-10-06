extends TestCase
## Lightning: charge, strike, conduction through the pool / metal plate / puddles, barriers,
## redirect, wet bonus and the "one discharge per bolt" rule (Conduction.discharge).
## Bystanders share the caster's team so they are never auto-targeted: the lock target is always T.

const BOLT_COST := 22.0
const DMG := 24.0          # Moves.DEFS.lightning.damage
const BUDGET := 26.0       # Moves.DEFS.lightning.conduct_budget

var h: SimHarness
var c: ActorState
var t: ActorState


func _world(c_pos: Vector3, kit: Dictionary = {"lightning": true}) -> void:
	h = SimHarness.new(1)
	c = h.actor("C", c_pos, 0, kit, Sim.Element.FIRE)


func _add(nm: String, pos: Vector3, team: int, kit: Dictionary = {}, elem: int = Sim.Element.EARTH) -> ActorState:
	return h.actor(nm, pos, team, kit, elem)


func _warm() -> void:
	h.step(20)   # actors turn to face their targets, surfaces update
	h.log.clear()


## Hold attack for hold_ticks (>= 39 charges the bolt), release, resolve the strike tick.
func _strike(hold_ticks: int = 48) -> void:
	h.press(c, "attack")
	h.step(hold_ticks)
	h.release(c, "attack")
	h.step(1)


func _bolt() -> Dictionary:
	return h.last_event("lightning")


func _hits_on(a: ActorState) -> int:
	var n := 0
	for e in h.events("hit"):
		if e.actor == a.id and e.kind == "lightning":
			n += 1
	return n


func _lost(a: ActorState) -> float:
	return Sim.HEALTH_MAX - a.health


# ---------------------------------------------------------------- charging

func test_charge_ready_after_lightning_min_and_bolt_on_release() -> void:
	_world(Vector3(0, 0, 6))
	t = _add("T", Vector3(0, 0, -2), 1)
	_warm()
	h.press(c, "attack")
	var lmin := float(Moves.DEFS.fire_attack.lightning_min)
	var ticks := int(floor(lmin * Sim.HZ)) - 2
	h.step(ticks)
	check(not h.has_event("charge_ready"), "not ready before lightning_min (%d ticks)" % ticks)
	h.step(6)
	check(h.has_event("charge_ready"), "ready shortly after lightning_min")
	check(not h.has_event("lightning"), "nothing fires while still holding")
	check(t.health == 100.0, "no damage while charging")
	h.release(c, "attack")
	h.step(1)
	check(h.has_event("lightning"), "releasing the charged attack strikes")
	check(not h.has_event("flare"), "and does not also flare")


func test_release_before_lightning_min_is_only_a_flare() -> void:
	_world(Vector3(0, 0, 6))
	t = _add("T", Vector3(0, 0, -2), 1)
	_warm()
	_strike(30)   # 0.5 s: past heavy_min (0.4 s) but short of lightning_min (0.65 s)
	h.step(30)
	check(h.has_event("flare") and h.last_event("flare").heavy, "a heavy flare instead")
	check(not h.has_event("lightning") and not h.has_event("charge_ready"), "no bolt")
	check(t.health == 100.0, "T (8 m away, flare range 6.5 m) untouched")


func test_no_bolt_without_the_technique() -> void:
	_world(Vector3(0, 0, 6), {})
	t = _add("T", Vector3(0, 0, -2), 1)
	_warm()
	_strike(48)
	h.step(30)
	check(not h.has_event("charge_ready") and not h.has_event("lightning"), "a fighter without 'lightning' never bolts")
	check(h.has_event("flare"), "the held attack becomes a heavy flare")
	check(t.health == 100.0, "no damage at 8 m")


func test_insufficient_focus_means_no_bolt_boundary() -> void:
	# 22 Focus is exactly the bolt cost. Regen is held off (focus_idle reset) so the value is exact.
	for case in [[22.0, true], [21.9, false], [15.0, false]]:
		_world(Vector3(0, 0, 6))
		t = _add("T", Vector3(0, 0, -2), 1)
		_warm()
		c.focus = case[0]
		c.focus_idle = 0.0
		_strike(41)   # 0.683 s: past lightning_min, before focus regen resumes
		h.step(20)
		var fired: bool = h.has_event("lightning")
		check(fired == case[1], "focus %.1f: bolt fired=%s, expected %s" % [case[0], fired, case[1]])
		if case[1]:
			check(c.focus < 1.0, "a bolt at exactly the cost spends it all (%.2f)" % c.focus)
			check(t.health < 100.0, "and hurts T")
		else:
			check(not h.has_event("charge_ready"), "focus %.1f: never reports a ready charge" % case[0])
			check(t.health == 100.0, "focus %.1f: T untouched" % case[0])
			check(c.focus >= 0.0, "focus never negative")


# ---------------------------------------------------------------- direct hits

func test_dry_target_hit_directly_exactly_once() -> void:
	_world(Vector3(0, 0, 6))
	t = _add("T", Vector3(0, 0, -2), 1)
	var bystander := _add("B", Vector3(5, 0, 0), 0)
	_warm()
	var focus0 := c.focus
	_strike()
	var ev := _bolt()
	check(not ev.is_empty() and ev.hits == [t.id], "the bolt reports exactly T as hit (%s)" % [ev.get("hits")])
	check(not ev.get("blocked", true), "not blocked")
	near(t.health, 100.0 - DMG, 1e-6, "dry T loses the base damage")
	check(_hits_on(t) == 1, "one lightning hit on T, got %d" % _hits_on(t))
	check(bystander.health == 100.0 and c.health == 100.0, "nobody else is hurt")
	check(not h.has_event("conduct"), "dry ground: nothing to conduct through")
	near(c.focus, focus0 - BOLT_COST, 0.5, "the bolt costs %.0f Focus (%.1f -> %.1f)" % [BOLT_COST, focus0, c.focus])
	# No further damage on later ticks, through the whole action and beyond.
	var hp := t.health
	h.step(240)
	near(t.health, hp, 1e-9, "no repeated damage on later ticks")
	check(_hits_on(t) == 1, "still exactly one hit event after 4 s")
	check(h.events("lightning").size() == 1, "exactly one discharge")
	check(c.action == null and c.stun == 0.0, "caster is idle again (no stuck action)")


func test_bolt_range_and_aim_limits() -> void:
	# Out of range (16 m > 14 m): the bolt is wasted.
	_world(Vector3(0, 0, 8))
	t = _add("T", Vector3(0, 0, -8), 1)
	_warm()
	_strike()
	check(t.health == 100.0, "16 m: out of range")
	check(h.has_event("lightning") and _bolt().hits.is_empty(), "the bolt still fires but hits nothing")
	# In range (12 m).
	_world(Vector3(0, 0, 8))
	t = _add("T", Vector3(0, 0, -4), 1)
	_warm()
	_strike()
	check(t.health < 100.0, "12 m: in range")
	# Aimed 15 degrees off the target still locks on; 35 degrees off misses.
	for case in [[15.0, true], [35.0, false]]:
		_world(Vector3(0, 0, 6))
		t = _add("T", Vector3(0, 0, -2), 1)
		_warm()
		var a: float = deg_to_rad(case[0])
		h.aim(c, Vector3(sin(a), 0, -cos(a)))
		_strike()
		check((t.health < 100.0) == case[1], "aim %.0f degrees off: hit=%s, expected %s" % [case[0], t.health < 100.0, case[1]])


func test_wet_target_takes_more_damage_than_dry() -> void:
	var dmg := {}
	for wet in [0.0, 1.0]:
		_world(Vector3(0, 0, 6))
		t = _add("T", Vector3(0, 0, -2), 1)
		_warm()
		h.press(c, "attack")
		h.step(48)
		t.wetness = wet
		h.release(c, "attack")
		h.step(1)
		dmg[wet] = _lost(t)
	near(dmg[0.0], DMG, 1e-6, "dry damage")
	near(dmg[1.0], DMG * 1.5, 1e-6, "soaked damage is 1.5x")
	check(dmg[1.0] > dmg[0.0], "wet takes more than dry")
	# The bonus switches on just above 0.3 wetness (wetness decays ~0.0008 in the strike tick).
	for case in [[0.3015, true], [0.2995, false]]:
		_world(Vector3(0, 0, 6))
		t = _add("T", Vector3(0, 0, -2), 1)
		_warm()
		h.press(c, "attack")
		h.step(48)
		t.wetness = case[0]
		h.release(c, "attack")
		h.step(1)
		var expected := DMG * 1.5 if case[1] else DMG
		near(_lost(t), expected, 1e-6, "wetness %.4f: damage" % case[0])


# ---------------------------------------------------------------- conduction

func test_pool_conducts_to_bystander_but_not_to_the_metal_plate() -> void:
	_world(Vector3(2, 0, -1))
	t = _add("T", Vector3(9.5, 0, -1), 1)
	var in_pool := _add("B", Vector3(11, 0, 1), 0)
	var on_metal := _add("M", Vector3(-9, 0.02, -1), 0)
	var on_stone := _add("S", Vector3(6.0, 0, -1), 0)   # beside the pool, dry
	_warm()
	check(t.in_water and in_pool.in_water, "setup: T and B stand in the pool")
	check(on_metal.surface == "metal" and not on_metal.in_water, "setup: M stands on the metal plate")
	check(not on_stone.in_water, "setup: S stands beside the pool")
	_strike()
	near(_lost(t), DMG * 1.5, 1e-6, "T (soaked) takes the direct hit")
	check(_lost(in_pool) > 0.0, "the bystander in the pool is hurt by conduction (%.1f)" % _lost(in_pool))
	check(on_metal.health == 100.0, "the metal plate is disconnected from the pool: M unhurt")
	check(on_stone.health == 100.0, "a dry bystander next to the pool is unhurt")
	check(c.health == 100.0, "caster unhurt")
	check(_hits_on(t) == 1 and _hits_on(in_pool) == 1, "each victim is hit exactly once")
	var conduct := h.last_event("conduct")
	check(not conduct.is_empty() and conduct.nodes == ["pool"], "graph reached only the pool (%s)" % [conduct.get("nodes")])
	# Conducted damage is the budget (single victim) x soaked bonus.
	near(_lost(in_pool), BUDGET * 1.5, 1e-6, "single conducted victim gets the whole budget, soaked")
	h.step(180)
	check(_hits_on(t) == 1 and _hits_on(in_pool) == 1, "no later ticks add damage")


func test_metal_plate_conducts_between_actors_on_it_but_not_into_the_pool() -> void:
	_world(Vector3(-3.0, 0, 5))
	t = _add("T", Vector3(-9, 0.02, -1), 1)
	var plate_mate := _add("M", Vector3(-10.5, 0.02, 1), 0)
	var in_pool := _add("P", Vector3(9.5, 0, -1), 0)
	_warm()
	check(t.surface == "metal" and plate_mate.surface == "metal", "setup: both stand on the plate")
	_strike()
	check(_lost(t) >= DMG - 1e-6, "T takes the direct hit (%.1f)" % _lost(t))
	check(_lost(plate_mate) > 0.0, "the other fighter on the plate is hurt through the metal")
	check(in_pool.health == 100.0, "the pool is a separate circuit: P unhurt")


func test_puddle_overlapping_the_metal_plate_connects_a_bystander() -> void:
	_world(Vector3(-3.0, 0, 5))
	t = _add("T", Vector3(-9, 0.02, -1), 1)
	# 20 kg puddle (radius ~0.8 m) centred 0.4 m beyond the plate edge (x = -6): it overlaps the plate.
	var puddle := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 20.0, Vector3(-5.6, 0, 1.0), "scenario")
	puddle.update_radius_puddle()
	var wet_b := _add("B", Vector3(-5.2, 0, 1.0), 0)      # inside that puddle
	var dry_b := _add("D", Vector3(-4.2, 0, 3.0), 0)      # on dry stone close by
	var far_puddle := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 20.0, Vector3(2.0, 0, 8.0), "scenario")
	far_puddle.update_radius_puddle()
	var far_b := _add("F", Vector3(2.0, 0, 8.0), 0)       # inside a puddle that touches nothing
	_warm()
	check(wet_b.surface == "puddle" and far_b.surface == "puddle", "setup: B and F stand in puddles")
	check(Conduction.actor_surface_node(h.w, wet_b) == "puddle:%d" % puddle.id, "setup: B is a node of the puddle")
	_strike()
	check(_lost(t) > 0.0, "T on the plate is struck")
	check(_lost(wet_b) > 0.0, "B in the overlapping puddle is connected to the plate and hurt (%.1f)" % _lost(wet_b))
	check(dry_b.health == 100.0, "a dry bystander is not hurt")
	check(far_b.health == 100.0, "a puddle that touches neither plate nor pool is an isolated node")
	var conduct := h.last_event("conduct")
	check(not conduct.is_empty() and conduct.nodes.has("metal") and conduct.nodes.has("puddle:%d" % puddle.id), "graph: metal -> puddle")
	check(not conduct.nodes.has("pool"), "graph does not include the pool")


func test_puddle_chain_to_the_pool_is_limited_to_max_hops() -> void:
	_world(Vector3(3, 0, 6))
	t = _add("T", Vector3(10, 0, -1), 1)
	var max_hops := int(Moves.DEFS.lightning.max_hops)
	var victims: Array[ActorState] = []
	for k in range(1, max_hops + 3):
		var x := 6.6 - 1.4 * float(k - 1)
		var p := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 20.0, Vector3(x, 0, -1), "scenario")
		p.update_radius_puddle()
		victims.append(_add("V%d" % k, Vector3(x, 0, -1), 0))
	_warm()
	_strike()
	check(_lost(t) > 0.0, "T in the pool takes the direct hit")
	for k in victims.size():
		var hops := k + 1   # puddle k+1 is k+1 hops from the pool
		if hops <= max_hops:
			check(_lost(victims[k]) > 0.0, "puddle %d (%d hops) is reached" % [hops, hops])
			check(_hits_on(victims[k]) == 1, "puddle %d victim is hit once" % hops)
		else:
			check(victims[k].health == 100.0, "puddle %d (%d hops) is beyond max_hops=%d" % [hops, hops, max_hops])


func test_frozen_puddle_does_not_conduct() -> void:
	_world(Vector3(3, 0, 6))
	t = _add("T", Vector3(10, 0, -1), 1)
	var ice := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 20.0, Vector3(6.6, 0, -1), "scenario")
	ice.update_radius_puddle()
	ice.liquid = 0.0
	ice.temp = -5.0
	ice.phase = Sim.Phase.FROZEN
	var on_ice := _add("V", Vector3(6.6, 0, -1), 0)
	_warm()
	_strike()
	check(_lost(t) > 0.0, "T in the pool is struck")
	check(on_ice.health == 100.0, "ice never conducts: the actor standing on it is unhurt")


func test_conducted_damage_is_a_bounded_budget_split_between_victims() -> void:
	_world(Vector3(2, 0, -1))
	t = _add("T", Vector3(9.5, 0, -1), 1)
	var bystanders: Array[ActorState] = []
	for i in 8:
		bystanders.append(_add("B%d" % i, Vector3(8.0 + float(i % 4) * 1.3, 0, -3.5 + float(i / 4) * 2.2), 0))
	_warm()
	for b in bystanders:
		check(b.in_water, "setup: %s stands in the pool" % b.name)
	_strike()
	var total := 0.0
	var hit := 0
	for b in bystanders:
		var lost := _lost(b)
		check(_hits_on(b) <= 1, "%s hit at most once (%d)" % [b.name, _hits_on(b)])
		if lost > 0.0:
			hit += 1
			total += lost / 1.5   # undo the soaked bonus
	check(hit >= 1, "someone was hit by conduction")
	check(total <= BUDGET + 1e-6, "conducted damage (%.2f before the wet bonus) stays within the %.0f budget" % [total, BUDGET])
	check(hit < bystanders.size(), "the budget runs out before 8 victims are all hurt (%d hit)" % hit)
	var per_share := total / float(hit)
	check(per_share >= 5.0 - 1e-6, "each conducted hit is at least MIN_SHARE (%.2f)" % per_share)
	near(_lost(t), DMG * 1.5, 1e-6, "direct hit unaffected by the budget")


# ---------------------------------------------------------------- barriers

func test_earth_wall_blocks_the_bolt() -> void:
	_world(Vector3(0, 0, 3))
	t = _add("T", Vector3(0, 0, -7), 1)
	_warm()
	h.press(t, "guard")   # Earth guard raises a wall in front of T
	h.step(12)
	check(t.wall_body >= 0, "setup: T raised a wall")
	_strike()
	var ev := _bolt()
	check(ev.get("blocked", false), "the bolt is stopped by the wall")
	check(t.health == 100.0, "T unhurt behind the wall")
	check(_hits_on(t) == 0, "no hit event")
	check(not h.has_event("conduct"), "nothing conducted")


func test_earth_wall_blocks_the_bolt_at_every_range() -> void:
	# The wall is thin (0.56 m): a barrier test sampling the bolt at a few points can slip through it.
	var leaks: Array[String] = []
	var d := 5.0
	while d <= 13.9:
		_world(Vector3(0, 0, -7.0 + d))
		t = _add("T", Vector3(0, 0, -7), 1)
		_warm()
		h.press(t, "guard")
		h.step(12)
		_strike()
		if not _bolt().get("blocked", false) or t.health < 100.0:
			leaks.append("%.1f" % d)
		d += 0.5
	check(leaks.is_empty(), "bolt passed through the earth wall at caster distances [%s] m" % ", ".join(leaks))


func test_wall_only_blocks_once_it_has_risen() -> void:
	# The wall rises in ~0.14 s (8.4 ticks); the bolt travels at chest height, so a half-risen wall
	# is still too low to stop it, a fully risen one stops it.
	for case in [[1, false], [5, false], [14, true]]:
		_world(Vector3(0, 0, 3))
		t = _add("T", Vector3(0, 0, -7), 1)
		_warm()
		h.press(c, "attack")
		h.step(46)
		h.press(t, "guard")
		h.step(case[0] - 1)
		h.release(c, "attack")
		h.step(1)   # strike lands case[0] ticks after the guard press
		var blocked: bool = _bolt().get("blocked", false)
		check(blocked == case[1], "wall pressed %d ticks before the strike: blocked=%s, expected %s" % [case[0], blocked, case[1]])


func test_cover_wall_between_caster_and_target_blocks_the_bolt() -> void:
	# ArenaMap documents the low cover wall as a "lightning barrier". Both fighters stand on flat
	# ground on opposite sides of it (x -5..-2.5, z -1.25..-0.75).
	_world(Vector3(-3.75, 0, 4))
	t = _add("T", Vector3(-3.75, 0, -5), 1)
	_warm()
	_strike()
	var ev := _bolt()
	check(ev.get("blocked", false), "the cover wall stops the bolt (blocked=%s, T lost %.1f)" % [ev.get("blocked"), _lost(t)])
	check(t.health == 100.0, "T is protected by the cover wall")


func test_tall_arena_solid_blocks_the_bolt() -> void:
	# Control for the barrier mechanism: the 3.2 m pillar in the south-west corner is tall enough.
	_world(Vector3(-13, 0, 9))
	t = _add("T", Vector3(-13, 0, 15), 1)
	_warm()
	_strike()
	check(_bolt().get("blocked", false), "the pillar blocks the bolt")
	check(t.health == 100.0, "T unhurt behind the pillar")
	# Same distance without the pillar in between: hit.
	_world(Vector3(-8, 0, 9))
	t = _add("T", Vector3(-8, 0, 15), 1)
	_warm()
	_strike()
	check(not _bolt().get("blocked", true) and t.health < 100.0, "clear line: T is hit")


# ---------------------------------------------------------------- redirect

## C charges, T (a Fire fighter with the technique unless overridden) presses guard `lead` ticks
## before the strike tick (lead 0 = same tick; inputs are processed before actions).
func _redirect_trial(lead: int, kit: Dictionary = {"redirect_current": true}, elem: int = Sim.Element.FIRE) -> void:
	_world(Vector3(0, 0, 6))
	t = _add("T", Vector3(0, 0, -2), 1, kit, elem)
	_warm()
	h.press(c, "attack")
	h.step(44)
	h.press(t, "guard")
	if lead > 0:
		h.step(1)
		h.step(lead - 1)
		h.release(c, "attack")
	else:
		h.release(c, "attack")
	h.step(1)


func test_perfect_timed_fire_guard_redirects_the_bolt_to_the_caster() -> void:
	for lead in [0, 1, 5, 10]:
		_redirect_trial(lead)
		check(h.has_event("lightning_redirect"), "lead %d: redirect happened" % lead)
		check(h.events("lightning_redirect").size() == 1, "lead %d: exactly one redirect per bolt" % lead)
		near(_lost(c), DMG * 0.8, 1e-6, "lead %d: the caster takes 80%% of the bolt" % lead)
		check(t.health == 100.0, "lead %d: the redirector takes nothing" % lead)
		check(_hits_on(c) == 1 and _hits_on(t) == 0, "lead %d: one lightning hit, on the caster" % lead)
		var hp := c.health
		h.step(120)
		near(c.health, hp, 1e-9, "lead %d: no further damage afterwards" % lead)


func test_late_guard_does_not_redirect() -> void:
	for lead in [11, 12, 20, 60]:   # 11 ticks = 0.183 s > the 0.18 s perfect window
		_redirect_trial(lead)
		check(not h.has_event("lightning_redirect"), "lead %d: no redirect" % lead)
		check(c.health == 100.0, "lead %d: the caster is unharmed" % lead)
		# A normal (late) guard is no answer to lightning: the bolt goes through the raised arm minus the guard's
		# CP 10 (24 -> 14; CoreRules.plain_guard_electric - the grounded stance, the Static Ward and walls are the answers).
		check(t.health < 100.0 - DMG * 0.5 and t.health > 100.0 - DMG * 0.7, "lead %d: E - CP through a normal guard (health %.2f)" % [lead, t.health])
		check(not h.has_event("block"), "lead %d: no clean block" % lead)


func test_no_redirect_without_the_technique_or_the_fire_element() -> void:
	_redirect_trial(3, {}, Sim.Element.FIRE)
	check(not h.has_event("lightning_redirect"), "Fire guard without redirect_current: no redirect")
	check(c.health == 100.0, "caster unharmed")
	check(t.health == 100.0, "a perfect guard still negates the bolt for the defender")
	_redirect_trial(3, {"redirect_current": true}, Sim.Element.EARTH)
	check(not h.has_event("lightning_redirect"), "Earth guard with the technique: no redirect")
	check(c.health == 100.0, "caster unharmed")
	_redirect_trial(3, {"redirect_current": true}, Sim.Element.WATER)
	check(not h.has_event("lightning_redirect"), "Water guard with the technique: no redirect")
	check(c.health == 100.0, "caster unharmed")


func test_guard_mashing_never_redirects() -> void:
	# Guard, release, guard again quickly: the second guard is "mashed" and has no perfect window.
	_world(Vector3(0, 0, 6))
	t = _add("T", Vector3(0, 0, -2), 1, {"redirect_current": true}, Sim.Element.FIRE)
	_warm()
	h.press(c, "attack")
	h.step(30)
	h.press(t, "guard")
	h.step(1)
	h.release(t, "guard")
	h.step(3)
	h.press(t, "guard")   # buffered until the first guard's recovery ends
	var started := h.until(func(): return t.guarding, 30)
	check(started > 0, "second guard started")
	check(t.action != null and t.action.data.get("mashed", false), "the second guard is flagged as mashed")
	h.step(1)
	h.release(c, "attack")
	h.step(1)   # strike lands ~2 ticks after the second guard began
	check(not h.has_event("lightning_redirect"), "no redirect for a mashed guard")
	check(c.health == 100.0, "caster unharmed")
	# Same but with a gap longer than GUARD_MASH_LOCK: the window is valid again.
	_world(Vector3(0, 0, 6))
	t = _add("T", Vector3(0, 0, -2), 1, {"redirect_current": true}, Sim.Element.FIRE)
	_warm()
	h.press(c, "attack")
	h.step(20)
	h.press(t, "guard")
	h.step(1)
	h.release(t, "guard")
	h.step(int(Moves.GUARD_MASH_LOCK * Sim.HZ) + 6)
	h.press(t, "guard")
	h.step(1)
	h.step(1)
	h.release(c, "attack")
	h.step(1)
	check(h.has_event("lightning_redirect"), "after the mash lock expires a fresh perfect guard redirects")
