extends TestCase
## Core combat rules: hit deduplication, evade i-frames, guard facing, perfect Earth guard,
## guard mashing, element switching, the input buffer, recovery cancels, knockdown/getup and
## Focus bounds. Scenarios use real inputs through SimHarness; stones that must arrive at an
## exact moment are spawned as projectiles with a gravity-compensated launch (ActEarth.launch_vel).

var h: SimHarness
var p: ActorState
var o: ActorState


func _duel(p_elem: int = Sim.Element.FIRE, o_elem: int = Sim.Element.EARTH, p_pos := Vector3(0, 0, 6), o_pos := Vector3(0, 0, -6), seed_value: int = 2) -> void:
	h = SimHarness.new(seed_value)
	p = h.actor("P", p_pos, 0, {}, p_elem)
	o = h.actor("O", o_pos, 1, {}, o_elem)
	h.step(25)   # actors turn to face each other
	h.log.clear()


func _stone(from: Vector3, to: Vector3, owner: ActorState, speed: float = 17.0, mass: float = 20.0) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, mass, from, "scenario")
	b.vel = ActEarth.launch_vel(from, to, speed)
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = owner.id
	b.damage = 12.0
	b.balance_damage = 24.0
	b.hit_set[owner.id] = true
	return b


func _events_for(type: String, actor: ActorState) -> Array:
	return h.events(type).filter(func(e): return e.get("actor", -1) == actor.id)


## hit_actor()/spend_focus() called directly queue events on the world: move them into the harness log.
func _flush() -> void:
	h.log.append_array(h.w.take_events())


func _hit_info(attacker: ActorState, damage: float, balance: float, kind: String = "stone") -> Dictionary:
	return {"attacker": attacker.id, "attack_id": h.w.new_attack_id(), "damage": damage, "balance": balance,
		"kind": kind, "from": attacker.pos}


# ================================================================ hit deduplication

func test_hit_actor_deduplicates_by_attack_id() -> void:
	_duel()
	var info := _hit_info(o, 10.0, 5.0)
	check(h.w.hit_actor(p, info) == "hit", "first application hits")
	near(p.health, 90.0, 1e-9, "health after one hit")
	for k in 5:
		check(h.w.hit_actor(p, info) == "dup", "repeat %d with the same attack id is a dup" % k)
	near(p.health, 90.0, 1e-9, "no extra damage from repeats")
	check(h.w.hit_actor(p, _hit_info(o, 10.0, 5.0)) == "hit", "a new attack id hits again")
	near(p.health, 80.0, 1e-9, "second attack applied")
	# Evaded attacks are deduplicated too: one 'evaded' event, then silence.
	p.iframes = 1.0
	var ev := _hit_info(o, 10.0, 5.0)
	check(h.w.hit_actor(p, ev) == "evaded", "i-frames evade")
	check(h.w.hit_actor(p, ev) == "dup", "the same evaded attack never re-triggers")
	_flush()
	check(h.events("evaded").size() == 1, "one evaded event, got %d" % h.events("evaded").size())


func test_fast_stone_overlapping_an_evading_actor_for_several_ticks_reports_once() -> void:
	_duel(Sim.Element.FIRE, Sim.Element.EARTH, Vector3(0, 0, 0), Vector3(0, 0, -14))
	p.iframes = 10.0
	var s := _stone(Vector3(0, 1.25, -6), p.chest(), o)
	var overlap_ticks := 0
	for k in 60:
		h.step()
		if s.alive and h.w._touches_actor(s, p, 0.0):
			overlap_ticks += 1
	check(overlap_ticks >= 2, "setup: the stone overlapped P for %d ticks" % overlap_ticks)
	check(_events_for("evaded", p).size() == 1, "exactly one evaded event, got %d" % _events_for("evaded", p).size())
	check(_events_for("hit", p).is_empty(), "no hit")
	near(p.health, 100.0, 1e-9, "no damage")


func test_stone_overlapping_a_target_for_several_ticks_hits_once() -> void:
	_duel(Sim.Element.FIRE, Sim.Element.EARTH, Vector3(0, 0, 0), Vector3(0, 0, -14))
	var s := _stone(Vector3(0, 1.25, -6), p.chest(), o)
	var overlap_ticks := 0
	for k in 90:
		h.step()
		if s.alive and h.w._touches_actor(s, p, 0.0):
			overlap_ticks += 1
	check(overlap_ticks >= 2, "setup: the stone overlapped P for %d ticks" % overlap_ticks)
	check(_events_for("hit", p).size() == 1, "exactly one hit, got %d" % _events_for("hit", p).size())
	near(p.health, 100.0 - 12.0, 1e-9, "damage applied once (%.1f)" % p.health)
	check(s.attack_id == 0, "the stone is inert after hitting")


# ================================================================ evade i-frames

## Outcome of an evade pressed `delay` ticks after a stone was thrown at P: "evaded", "hit" or "none".
func _evade_trial(delay: int, toward: bool) -> String:
	_duel()
	h.press(o, "attack")
	h.step()
	h.release(o, "attack")
	h.until(func(): return h.has_event("launch"), 60)
	h.step(delay)
	if toward:
		h.it(p).move = Vector3(0, 0, -1)
	h.press(p, "evade")
	h.step()
	h.it(p).move = Vector3.ZERO
	h.step(100)
	var ev := _events_for("evaded", p).size()
	var hit := _events_for("hit", p).size()
	if hit > 0:
		check(ev == 0, "delay %d: a stone that hits was not also evaded" % delay)
		check(hit == 1, "delay %d: hit exactly once (%d)" % [delay, hit])
		return "hit"
	if ev > 0:
		check(ev == 1, "delay %d: exactly one evaded event (%d)" % [delay, ev])
		check(p.health == 100.0, "delay %d: no damage after evading" % delay)
		return "evaded"
	return "none"


func test_evade_iframes_make_a_timed_stone_pass() -> void:
	for toward in [false, true]:
		var outcomes: Array[String] = []
		for d in range(0, 56, 1):
			outcomes.append(_evade_trial(d, toward))
		var first := outcomes.find("evaded")
		var last := outcomes.rfind("evaded")
		var label := "dash toward" if toward else "backstep"
		check(first >= 0, "%s: some timing evades the stone" % label)
		if first < 0:
			continue
		for k in range(first, last + 1):
			check(outcomes[k] == "evaded", "%s: the evade window is contiguous (delay %d is %s)" % [label, k, outcomes[k]])
		check(outcomes[0] == "hit", "%s: evading far too early gets hit" % label)
		check(outcomes[outcomes.size() - 1] == "hit", "%s: evading too late gets hit" % label)
		var width := last - first + 1
		note("%s: stone evaded for press delays %d..%d (%d ticks)" % [label, first, last, width])
		check(width >= (8 if toward else 2), "%s: the window covers the i-frames (%d ticks)" % [label, width])
		check(width <= 30, "%s: i-frames do not make a huge window (%d)" % [label, width])
		check(not outcomes.has("none"), "%s: every timing resolves to evaded or hit" % label)


func test_evade_iframes_last_the_documented_duration() -> void:
	_duel()
	check(p.iframes == 0.0, "no i-frames before evading")
	h.press(p, "evade")
	h.step()
	var frames := float(Moves.DEFS.evade.iframes)
	check(p.iframes > 0.0 and p.iframes <= frames, "i-frames start with the evade (%.3f)" % p.iframes)
	var ticks := 1
	while p.iframes > 0.0 and ticks < 60:
		h.step()
		ticks += 1
	check(absf(float(ticks) * Sim.DT - frames) <= 2.0 * Sim.DT, "i-frames last about %.2f s (%d ticks)" % [frames, ticks])
	check(p.action != null or p.iframes == 0.0, "the dash outlasts the i-frames")
	check(h.w.hit_actor(p, _hit_info(o, 10.0, 5.0)) == "hit", "after the i-frames a hit lands")


# ================================================================ guard facing

func _guard_trial(angle_deg: float, guard: bool, guard_after_spawn: int = -40) -> Dictionary:
	# P (Fire: no wall or shield) faces -z towards O. A stone arrives from `angle_deg` around P
	# (0 = from the front, 180 = from behind), 4 m away, ~14 ticks of flight. The guard is pressed
	# `guard_after_spawn` ticks after the stone appears (negative = that many ticks before).
	_duel(Sim.Element.FIRE, Sim.Element.EARTH, Vector3(0, 0, 0), Vector3(0, 0, -14))
	var a := deg_to_rad(angle_deg)
	var from := Vector3(sin(a) * 4.0, 1.25, -cos(a) * 4.0)
	if guard and guard_after_spawn < 0:
		h.press(p, "guard")
		h.step(-guard_after_spawn)
	_stone(from, p.chest(), o)
	if guard and guard_after_spawn >= 0:
		h.step(guard_after_spawn)
		h.press(p, "guard")
	h.step(40)
	return {"health": p.health, "balance": p.balance,
		"block": _events_for("block", p).size(), "hit": _events_for("hit", p).size(),
		"perfect": h.events("perfect_deflect").size()}


func test_guard_blocks_from_the_front_with_reduced_damage() -> void:
	var open := _guard_trial(0.0, false)
	near(open.health, 88.0, 1e-9, "unguarded stone deals its 12 damage")
	check(open.hit == 1 and open.block == 0, "unguarded: a hit, no block")
	var g := _guard_trial(0.0, true)
	check(g.block == 1 and g.hit == 0, "guarded from the front: blocked (block %d hit %d)" % [g.block, g.hit])
	check(g.health > open.health and g.health < 100.0, "chip damage only (%.2f)" % g.health)
	near(100.0 - g.health, 12.0 * 0.12, 1e-6, "block takes 12%% of the damage")
	check(100.0 - g.balance < 24.0, "and less than the full balance damage (%.1f)" % (100.0 - g.balance))
	check(g.perfect == 0, "a guard that has been up for 0.67 s is not a perfect guard")


func test_guard_fails_when_the_attack_comes_from_behind() -> void:
	var back := _guard_trial(180.0, true)
	check(back.hit == 1 and back.block == 0, "guarded from behind: a clean hit (hit %d block %d)" % [back.hit, back.block])
	near(back.health, 88.0, 1e-9, "full damage from behind")


func test_guard_facing_cone_boundary() -> void:
	# The guard covers every direction with dot(forward, direction to attacker) > -0.15.
	var rows := []
	for angle in range(0, 181, 15):
		var g := _guard_trial(float(angle), true)
		var covered: bool = cos(deg_to_rad(float(angle))) > -0.15
		rows.append("%d:%s" % [angle, "block" if g.block > 0 else "hit"])
		check((g.block > 0) == covered, "angle %d: block=%s, expected %s" % [angle, g.block > 0, covered])
		check((g.hit > 0) == (not covered), "angle %d: hit=%s, expected %s" % [angle, g.hit > 0, not covered])
	note(" ".join(rows))
	# Right at the edge of the cone (cos = -0.15 -> 98.6 degrees).
	var inside := _guard_trial(97.0, true)
	var outside := _guard_trial(100.0, true)
	check(inside.block > 0 and outside.hit > 0, "97 degrees still blocks, 100 degrees does not")


func test_perfect_guard_negates_a_non_earth_stone_completely() -> void:
	# Fire guard pressed 3 ticks before contact: perfect deflect, no damage at all.
	var g := _guard_trial(0.0, true, 6)   # guard up 8 ticks (0.13 s) before contact
	check(g.perfect >= 1, "perfect_deflect reported")
	near(g.health, 100.0, 1e-9, "a perfect guard takes no damage")
	check(g.hit == 0 and g.block == 0, "neither a hit nor a plain block")
	# 14 ticks (0.23 s) of guard before contact is outside the 0.18 s window: an ordinary block.
	var early := _guard_trial(0.0, true, 0)
	check(early.perfect == 0 and early.block == 1, "a guard raised 0.23 s before contact is only a block")
	near(early.health, 100.0 - 12.0 * 0.12, 1e-6, "with chip damage")


# ================================================================ perfect Earth guard

## P (Earth) presses guard; a stone is launched from `dist` m away at the same tick. O (the
## thrower) stands `o_dist` m behind the stone's origin line.
func _earth_trial(dist: float, o_dist: float = 12.0, mash: bool = false) -> Dictionary:
	_duel(Sim.Element.EARTH, Sim.Element.EARTH, Vector3(0, 0, 0), Vector3(0, 0, -o_dist))
	var s: MatBody
	if mash:
		h.press(p, "guard")
		h.step()
		h.release(p, "guard")
		h.step(2)
		h.press(p, "guard")   # buffered behind the first guard's recovery: a mashed second guard
		s = _stone(Vector3(0, 1.25, -dist), p.chest(), o)
		h.step()
	else:
		h.press(p, "guard")
		s = _stone(Vector3(0, 1.25, -dist), p.chest(), o)
		h.step()
	var closest := 99.0
	for k in 120:
		h.step()
		if s.alive and s.attack_owner == p.id:
			closest = minf(closest, s.pos.distance_to(o.chest()))
	var redirects := h.events("perfect_deflect").filter(func(e): return e.get("verb", "") == "redirect")
	return {"stone": s, "redirects": redirects.size(),
		"p_hits": _events_for("hit", p).size(), "o_hits": _events_for("hit", o).size(),
		"walls_blocked": h.events("block").filter(func(e): return e.kind == "wall").size(),
		"crumbles": h.events("wall_crumble").size(), "closest": closest}


func test_perfect_earth_guard_redirects_the_stone_to_the_thrower() -> void:
	var rows: Array[Dictionary] = []
	var dists: Array[float] = []
	var d := 2.0
	while d <= 9.01:
		rows.append(_earth_trial(d, 6.0))
		dists.append(d)
		d += 0.5
	var first := -1
	var last := -1
	for k in rows.size():
		if rows[k].redirects > 0:
			if first < 0:
				first = k
			last = k
	check(first >= 0, "some timing redirects the stone")
	if first < 0:
		return
	for k in range(first, last + 1):
		check(rows[k].redirects > 0, "the perfect window is contiguous (dist %.1f)" % dists[k])
	for k in range(first, last + 1):
		var r: Dictionary = rows[k]
		check(r.redirects == 1, "dist %.1f: exactly one redirect" % dists[k])
		check(r.p_hits == 0, "dist %.1f: the guard took no damage" % dists[k])
		check(r.o_hits >= 1, "dist %.1f: the redirected stone hit the thrower (closest %.2f m)" % [dists[k], r.closest])
		var s: MatBody = r.stone
		check(s.attack_owner == p.id or s.attack_id == 0, "dist %.1f: the stone changed hands" % dists[k])
		near(s.mass, 20.0, 1e-9, "dist %.1f: stone mass unchanged" % dists[k])
	note("perfect redirect window: stone launched %.1f..%.1f m away" % [dists[first], dists[last]])
	# Pressing guard much earlier than the window: the wall just absorbs the stone.
	var early := rows[rows.size() - 1]
	check(early.redirects == 0, "an early guard is not perfect")
	check(early.p_hits == 0 and early.walls_blocked >= 1, "the early guard's wall blocks the stone")
	check(early.o_hits == 0, "and nothing is sent back")
	# Pressing too late: the stone arrives before the wall exists.
	var late := rows[0]
	check(late.redirects == 0 or late.p_hits == 0, "a guard that is too late never both fails and redirects")


func test_redirected_stone_reaches_a_thrower_at_duel_distance() -> void:
	# The lab's fighters start 14 m apart; a redirect must be able to cross a 12 m duel.
	var best := 99.0
	var hit := false
	var d := 3.0
	while d <= 4.6:
		var r := _earth_trial(d, 12.0)
		if r.redirects > 0:
			best = minf(best, r.closest)
			hit = hit or r.o_hits > 0
		d += 0.25
	check(best < 99.0, "setup: some timing redirects")
	check(hit, "a redirected stone must be able to hit a thrower 12 m away (it came within %.2f m)" % best)


func test_wall_blocked_stone_does_not_grind_the_wall_down() -> void:
	# Sweep guard timings so that stones reach a wall in every stage of its rise. A single 20 kg
	# stone must never crumble the wall or register more than one impact.
	var bad: Array[String] = []
	var d := 2.0
	while d <= 10.01:
		for mash in [false, true]:
			var r := _earth_trial(d, 12.0, mash)
			if r.walls_blocked > 2 or r.crumbles > 0:
				bad.append("%.2f%s(blocks %d, crumbles %d)" % [d, "m" if mash else "", r.walls_blocked, r.crumbles])
		d += 0.25
	check(bad.is_empty(), "stones stuck against / inside a rising wall and ground it down: %s" % ", ".join(bad))


func test_inert_stone_never_damages_a_wall() -> void:
	# Only attacks hurt walls: an inert stone that ends up inside a raised wall's volume must not
	# register impacts every tick (and must not hang frozen in mid-air).
	_duel(Sim.Element.EARTH, Sim.Element.EARTH, Vector3(0, 0, 0), Vector3(0, 0, -12))
	h.press(p, "guard")
	h.step(15)
	var wall := h.w.get_body(p.wall_body)
	check(wall != null and wall.wall_rise >= 1.0, "setup: a full wall")
	if wall == null:
		return
	var s := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, wall.pos + Vector3(0, 0.9, 0), "scenario")
	check(s.attack_id == 0, "setup: an inert stone inside the wall volume")
	var start_y := s.pos.y
	h.step(120)
	check(wall.alive and wall.wall_damage < 0.05, "the inert stone did not grind the wall (damage %.3f, alive %s)" % [wall.wall_damage, wall.alive])
	check(h.events("block").filter(func(e): return e.kind == "wall").size() <= 1, "no repeated block events (%d)" % h.events("block").filter(func(e): return e.kind == "wall").size())
	check(not s.alive or s.pos.y < start_y - 0.2, "the stone is not frozen in mid-air (y %.2f -> %.2f)" % [start_y, s.pos.y])


# ================================================================ guard mashing

func test_guard_mashing_never_yields_a_perfect_deflect() -> void:
	var control_perfect := 0
	var mashed_perfect := 0
	var mashed_trials := 0
	var d := 2.0
	while d <= 9.01:
		var single := _earth_trial(d, 6.0, false)
		control_perfect += single.redirects
		var m := _earth_trial(d, 6.0, true)
		mashed_perfect += m.redirects
		mashed_trials += 1
		check(m.redirects == 0, "mashed guard, stone from %.2f m: no perfect deflect" % d)
		d += 0.25
	check(control_perfect > 0, "control: a single clean guard does get perfect deflects over the same sweep (%d)" % control_perfect)
	check(mashed_trials > 10 and mashed_perfect == 0, "no mashed sweep point was perfect (%d of %d)" % [mashed_perfect, mashed_trials])


func test_guard_mash_lock_boundary() -> void:
	# Two guard presses closer than GUARD_MASH_LOCK flag the second guard; a longer gap does not.
	for case in [[12, true], [18, true], [26, false], [40, false]]:
		_duel(Sim.Element.FIRE, Sim.Element.EARTH, Vector3(0, 0, 0), Vector3(0, 0, -14))
		h.press(p, "guard")
		h.step()
		h.release(p, "guard")
		h.step(case[0] - 1)
		h.press(p, "guard")
		h.step()
		check(p.guarding, "gap %d: second guard is up" % case[0])
		var mashed: bool = p.action != null and p.action.data.get("mashed", false)
		check(mashed == case[1], "gap %d ticks (%.2f s): mashed=%s, expected %s" % [case[0], float(case[0]) * Sim.DT, mashed, case[1]])
		check(h.w.perfect_guard(p) == (not case[1]), "gap %d: perfect window available=%s" % [case[0], h.w.perfect_guard(p)])


# ================================================================ element switching

func test_switching_element_during_recovery_keeps_the_recovery_and_changes_the_next_action() -> void:
	# Baseline: when does a tap earth attack finish?
	_duel(Sim.Element.EARTH)
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	h.until(func(): return p.action == null, 120)
	var done_tick: int = h.events("action").filter(func(e): return e.actor == p.id and e.phase == "done").back().tick
	# Same attack, but switch element while the action is in recovery.
	_duel(Sim.Element.EARTH)
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	var reached := h.until(func(): return p.action != null and p.action.phase == ActionInst.P.RECOVERY, 120)
	check(reached > 0, "the attack reached recovery")
	var inst := p.action
	var t_before := inst.t
	h.element(p, Sim.Element.FIRE)
	h.step()
	check(p.element == Sim.Element.FIRE, "the selected element changes immediately")
	check(p.action == inst and inst.phase == ActionInst.P.RECOVERY, "the running recovery is not cancelled")
	check(inst.id == "earth_attack" and inst.element == Sim.Element.EARTH, "the running action keeps its own element")
	check(inst.t > t_before, "recovery keeps ticking (%.3f -> %.3f)" % [t_before, inst.t])
	check(not h.has_event("interrupt"), "nothing was interrupted")
	h.until(func(): return p.action == null, 60)
	var done2: int = h.events("action").filter(func(e): return e.actor == p.id and e.phase == "done").back().tick
	check(done2 == done_tick, "recovery ends on the same tick as without the switch (%d vs %d)" % [done2, done_tick])
	# The next action uses the new element.
	h.press(p, "attack")
	h.step()
	check(p.action != null and p.action.id == "fire_attack", "next attack is a fire attack (%s)" % [p.action.id if p.action else "none"])
	h.release(p, "attack")
	h.step(30)


func test_element_switch_while_charging_does_not_change_the_running_action() -> void:
	_duel(Sim.Element.EARTH)
	h.press(p, "attack")
	h.step(5)
	h.element(p, Sim.Element.WATER)
	h.step()
	check(p.element == Sim.Element.WATER, "switched")
	check(p.action != null and p.action.id == "earth_attack", "the attack in progress stays an earth attack")
	h.release(p, "attack")
	h.until(func(): return h.has_event("launch"), 60)
	check(h.last_event("launch").get("kind", "stone") != "ice", "it launches a stone, not ice")
	var b := h.w.get_body(int(h.last_event("launch").body))
	check(b != null and b.is_stone(), "the thrown body is stone")


func test_element_switch_rules() -> void:
	_duel(Sim.Element.EARTH)
	h.element(p, Sim.Element.EARTH)
	h.step()
	check(not h.has_event("element"), "selecting the current element is a no-op")
	p.elements[Sim.Element.AIR] = false
	h.element(p, Sim.Element.AIR)
	h.step()
	check(p.element == Sim.Element.EARTH and not h.has_event("element"), "a locked element cannot be selected")
	h.element(p, Sim.Element.WATER)
	h.step()
	check(p.element == Sim.Element.WATER and h.events("element").size() == 1, "an unlocked element can")


# ================================================================ input buffer

func _tap_attack() -> void:
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")


## Starts a second tap attack `k` ticks before the first one's recovery finishes; returns the tick at
## which the second action started, or -1 if it never did.
func _buffer_trial(k: int, finish_tick: int) -> int:
	_duel(Sim.Element.EARTH)
	_tap_attack()
	while h.w.tick < finish_tick - k:
		h.step()
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	h.step(90)
	var starts := h.events("action").filter(func(e): return e.actor == p.id and e.move == "earth_attack" and e.phase == "startup")
	if starts.size() < 2:
		return -1
	return int(starts[1].tick)


func test_attack_pressed_in_recovery_is_buffered_only_within_buffer_time() -> void:
	_duel(Sim.Element.EARTH)
	_tap_attack()
	h.step(120)
	var done := h.events("action").filter(func(e): return e.actor == p.id and e.move == "earth_attack" and e.phase == "done")
	check(done.size() == 1, "baseline: one finished action")
	var finish_tick: int = done[0].tick
	var window := int(floor(Moves.BUFFER_TIME * Sim.HZ))
	var started: Array[bool] = []
	var rows := []
	for k in range(0, 25):
		var t := _buffer_trial(k, finish_tick)
		started.append(t >= 0)
		rows.append("%d:%s" % [k, "run" if t >= 0 else "drop"])
		if t >= 0:
			check(t == finish_tick + 1, "pressed %d ticks before the end: starts right when recovery ends (tick %d, end %d)" % [k, t, finish_tick])
	note(" ".join(rows))
	var threshold := -1
	for k in started.size():
		if not started[k]:
			threshold = k
			break
	check(threshold > 0, "presses far from the end are not buffered")
	for k in range(threshold, started.size()):
		check(not started[k], "once the buffer window is exceeded it stays exceeded (k=%d)" % k)
	check(absi(threshold - window) <= 1, "buffer window is BUFFER_TIME (%d ticks): first dropped press at k=%d" % [window, threshold])
	for k in range(0, maxi(threshold - 2, 0)):
		check(started[k], "pressed %d ticks before the end: executes" % k)


func test_press_after_recovery_starts_immediately_and_stale_buffer_is_cleared() -> void:
	_duel(Sim.Element.EARTH)
	_tap_attack()
	h.step(10)
	h.press(p, "attack")   # far too early: buffered, then dropped
	h.step()
	h.release(p, "attack")
	check(p.buffered == "attack", "the early press is buffered")
	h.step(20)
	check(p.buffered == "", "the stale buffered press was cleared")
	check(h.events("action").filter(func(e): return e.actor == p.id and e.move == "earth_attack" and e.phase == "startup").size() == 1, "and never executed")
	h.until(func(): return p.action == null, 120)
	var before := h.events("action").filter(func(e): return e.actor == p.id and e.phase == "startup").size()
	_tap_attack()
	h.step()
	check(h.events("action").filter(func(e): return e.actor == p.id and e.phase == "startup").size() == before + 1, "a press with nothing running starts at once")


func test_guard_and_evade_cancel_recovery_only_after_the_cancel_fraction() -> void:
	# earth_attack: recovery 0.30 s, cancel 0.6. A guard/evade press cancels only in the last 40%.
	var cancel := float(Moves.DEFS.earth_attack.cancel)
	var rec := float(Moves.DEFS.earth_attack.recovery)
	for press_what in ["guard", "evade"]:
		for frac in [0.2, 0.9]:
			_duel(Sim.Element.EARTH)
			_tap_attack()
			h.until(func(): return p.action != null and p.action.phase == ActionInst.P.RECOVERY, 120)
			var inst := p.action
			while inst.t < rec * frac - 0.5 * Sim.DT and p.action == inst:
				h.step()
			h.press(p, press_what)
			h.step()
			var cancelled := h.events("interrupt").any(func(e): return e.actor == p.id and e.reason == "cancel:" + press_what)
			var expected: bool = frac >= cancel
			check(cancelled == expected, "%s at %.0f%% of recovery: cancelled=%s, expected %s" % [press_what, frac * 100.0, cancelled, expected])
			if expected:
				check(p.action != null and p.action.id == press_what or press_what == "evade", "%s action started" % press_what)
			h.release(p, press_what)
			h.step(60)
			check(p.action == null and p.stun == 0.0, "%s at %.0f%%: back to idle" % [press_what, frac * 100.0])


# ================================================================ knockdown & getup

func test_knockdown_getup_and_return_to_idle() -> void:
	_duel()
	p.balance = 30.0
	var res := h.w.hit_actor(p, _hit_info(o, 5.0, 30.0))
	check(res == "knockdown", "balance reaching 0 knocks down (%s)" % res)
	check(p.stun_kind == "knockdown" and p.stun > 0.0, "stunned, kind knockdown (%s %.2f)" % [p.stun_kind, p.stun])
	check(p.balance == 45.0, "balance is restored for the get-up (%.0f)" % p.balance)
	check(p.action == null, "nothing running")
	# A press while down is not executed and does not linger.
	h.step(5)
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	check(p.action == null, "cannot act while knocked down")
	var timeline := {}
	var ticks := 0
	var getup_tick := -1
	var free_tick := -1
	var iframes_during_getup := true
	var guard_on_getup := false
	while ticks < 400:
		h.step()
		ticks += 1
		if p.stun_kind == "getup":
			if getup_tick < 0:
				getup_tick = ticks
			if p.iframes <= 0.0:
				iframes_during_getup = false
		if free_tick < 0 and p.stun == 0.0 and p.stun_kind == "":
			free_tick = ticks
			break
	check(getup_tick > 0, "a getup phase follows the knockdown")
	check(h.has_event("getup"), "getup event emitted")
	check(iframes_during_getup, "getting up grants i-frames")
	check(free_tick > getup_tick, "then the actor is free")
	var total := float(free_tick + 6) * Sim.DT
	check(total > 1.5 and total < 2.4, "knockdown (1.1 s) + getup (0.75 s): free after %.2f s" % total)
	check(p.action == null and p.stun == 0.0 and p.iframes == 0.0, "idle state after getting up (action %s stun %.2f iframes %.2f)" % [p.action, p.stun, p.iframes])
	check(p.buffered == "", "no stale buffered input")
	var hp := p.health
	check(h.w.hit_actor(p, _hit_info(o, 5.0, 5.0)) == "hit", "vulnerable again after the getup")
	check(p.health < hp, "damage applies again")
	h.step(40)
	h.press(p, "attack")
	h.step()
	check(p.action != null, "and the actor can act again")


func test_hits_during_getup_are_evaded() -> void:
	_duel()
	p.balance = 10.0
	h.w.hit_actor(p, _hit_info(o, 5.0, 10.0))
	h.until(func(): return p.stun_kind == "getup", 200)
	check(p.stun_kind == "getup", "reached the getup")
	var hp := p.health
	check(h.w.hit_actor(p, _hit_info(o, 30.0, 60.0)) == "evaded", "a hit during the getup is evaded")
	near(p.health, hp, 1e-9, "no damage while getting up")
	check(p.stun_kind == "getup", "the getup is not interrupted")
	h.until(func(): return p.stun == 0.0, 120)
	check(p.stun == 0.0 and p.action == null, "free afterwards")


func test_knockdown_threshold_is_exact() -> void:
	for case in [[30.0, "knockdown"], [29.99, "heavy"], [10.0, "light"]]:
		_duel()
		p.balance = 30.0
		var res := h.w.hit_actor(p, _hit_info(o, 1.0, case[0]))
		var expected: String = case[1]
		if expected == "knockdown":
			check(res == "knockdown" and p.stun_kind == "knockdown", "balance 30 - 30 -> knockdown (%s)" % res)
		else:
			check(res == "hit" and p.stun_kind == expected, "balance 30 - %.2f -> %s stagger (%s, %s)" % [case[0], expected, res, p.stun_kind])
			check(p.balance > 0.0, "balance stays positive (%.2f)" % p.balance)


func test_hit_while_knocked_down_still_ends_in_a_free_actor() -> void:
	_duel()
	p.balance = 10.0
	h.w.hit_actor(p, _hit_info(o, 5.0, 10.0))
	h.step(20)
	h.w.hit_actor(p, _hit_info(o, 5.0, 5.0))   # hit while lying down
	h.w.hit_actor(p, _hit_info(o, 5.0, 40.0))
	var free := h.until(func(): return p.stun == 0.0 and p.action == null, 600)
	check(free > 0, "the actor eventually gets free (%d ticks)" % free)
	check(p.stun_kind == "" or p.stun_kind == "getup" and p.stun == 0.0, "no dangling stun kind (%s)" % p.stun_kind)
	check(p.balance > 0.0 and p.health > 0.0, "balance %.1f health %.1f" % [p.balance, p.health])
	h.press(p, "attack")
	h.step()
	check(p.action != null, "and can act")


# ================================================================ Focus bounds

func test_spend_focus_never_goes_negative() -> void:
	_duel()
	p.focus = 10.0
	check(not h.w.spend_focus(p, 50.0), "cannot spend more than available")
	near(p.focus, 10.0, 1e-9, "a refused spend changes nothing")
	check(h.w.spend_focus(p, 10.0), "can spend exactly everything")
	check(p.focus == 0.0, "focus is exactly 0, not negative (%.12f)" % p.focus)
	check(h.w.spend_focus(p, 0.0) and h.w.spend_focus(p, -3.0), "non-positive spends are free")
	check(p.focus == 0.0, "still 0")
	check(not h.w.spend_focus(p, 0.5), "nothing left to spend")
	p.focus = 5.0
	var paid := h.w.pay_heat(p, 1000.0)   # partial payment must stop at 0 Focus
	check(p.focus >= 0.0, "pay_heat never drives focus below 0 (%.6f)" % p.focus)
	near(paid, 50.0, 1e-6, "paid only what 5 Focus can buy (%.2f HU)" % paid)
	check(h.w.pay_heat(p, 1000.0, false) == 0.0 and p.focus >= 0.0, "a refused full payment costs nothing")


func test_focus_regen_delay_rate_and_cap() -> void:
	_duel()
	h.w.spend_focus(p, 40.0)
	var f0 := p.focus
	near(f0, 60.0, 1e-9, "setup: 60 Focus")
	h.step(int(Sim.FOCUS_REGEN_DELAY * Sim.HZ) - 3)
	near(p.focus, f0, 1e-9, "no regeneration during the delay")
	h.step(10)
	check(p.focus > f0, "regeneration starts after the delay (%.2f)" % p.focus)
	var f1 := p.focus
	h.step(60)
	near(p.focus - f1, Sim.FOCUS_REGEN, 0.3, "idle regen is %.0f per second (%.2f)" % [Sim.FOCUS_REGEN, p.focus - f1])
	h.step(60 * 10)
	check(p.focus == Sim.FOCUS_MAX, "regen stops at FOCUS_MAX (%.4f)" % p.focus)
	# A new spend restarts the delay.
	h.w.spend_focus(p, 10.0)
	h.step(10)
	near(p.focus, Sim.FOCUS_MAX - 10.0, 1e-9, "spending restarts the delay")


func test_low_focus_actions_fizzle_without_negative_focus() -> void:
	# Earth attack costs 7 Focus: with 3 it fizzles and takes nothing from the ground.
	_duel(Sim.Element.EARTH)
	p.focus = 3.0
	p.focus_idle = 0.0
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	h.step(60)
	check(h.events("insufficient").any(func(e): return e.actor == p.id and e.what == "focus"), "told about the missing Focus")
	check(h.events("rip").is_empty() and h.w.mass_ledger.ground_taken == 0.0, "no stone was ripped without Focus")
	check(p.focus >= 3.0, "the fizzle did not spend Focus (%.3f)" % p.focus)
	check(p.action == null, "the fizzled action finished")
	# Exactly the cost works.
	_duel(Sim.Element.EARTH)
	p.focus = float(Moves.DEFS.earth_attack.cost)
	p.focus_idle = 0.0
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	h.step(30)
	check(h.has_event("rip"), "exactly 7 Focus is enough")
	check(p.focus >= 0.0 and p.focus < 1.0, "and leaves none (%.3f)" % p.focus)
	# Heavy surcharge: 10 Focus pays the 7 base but not the extra 7.
	_duel(Sim.Element.EARTH)
	p.focus = 10.0
	p.focus_idle = 0.0
	h.press(p, "attack")
	h.step(60)
	check(p.focus >= 0.0, "focus stays >= 0 while charging a heavy (%.3f)" % p.focus)
	var b := h.w.held(p)
	check(b != null and absf(b.mass - 20.0) < 1e-9, "the heavy gather failed: the stone stays 20 kg (%s)" % [b.mass if b else -1.0])
	h.release(p, "attack")
	h.step(60)
	check(p.focus >= 0.0 and p.action == null, "clean exit")


func test_evade_and_fire_with_no_focus() -> void:
	_duel()
	p.focus = 0.0
	p.focus_idle = 0.0
	h.press(p, "evade")
	h.step()
	check(p.iframes > 0.0, "an evade with no Focus still works (i-frames)")
	check(p.focus >= 0.0, "and does not drive Focus negative (%.3f)" % p.focus)
	h.step(60)
	# Fire attack needs 60 HU: no Focus and no reserve -> fizzle.
	_duel()
	p.focus = 0.0
	p.focus_idle = 0.0
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	h.step(40)
	check(h.events("insufficient").any(func(e): return e.actor == p.id), "a flare without Focus or reserve is refused")
	check(not h.has_event("flare") and p.focus >= 0.0, "no flare, focus >= 0")
	# With a full heat reserve the same attack is paid from the reserve first, Focus untouched.
	_duel()
	p.focus = 0.0
	p.focus_idle = 0.0
	p.heat_reserve = 100.0
	h.press(p, "attack")
	h.step()
	h.release(p, "attack")
	h.step(40)
	check(h.has_event("flare"), "reserve alone can pay for a flare")
	near(p.heat_reserve, 100.0 - 60.0 - Sim.RESERVE_DISSIPATE * 40.0 * Sim.DT, 1.5, "reserve paid the 60 HU (%.1f left)" % p.heat_reserve)
	check(p.focus >= 0.0 and h.w.ledger.generated == 0.0, "no Focus was converted to heat")


func test_focus_stays_in_bounds_under_button_mashing() -> void:
	_duel(Sim.Element.EARTH, Sim.Element.FIRE)
	p.focus = 25.0
	var rng := RandomNumberGenerator.new()
	rng.seed = 99
	var lowest := 999.0
	var highest := -999.0
	var kinds := ["attack", "tech", "guard", "evade"]
	for k in 1500:
		if k % 7 == 0:
			var what: String = kinds[rng.randi() % 4]
			h.press(p, what)
		if k % 11 == 0:
			h.release(p, "attack")
			h.release(p, "tech")
			h.release(p, "guard")
		if k % 400 == 0:
			h.element(p, rng.randi() % 4)
		h.step()
		lowest = minf(lowest, p.focus)
		highest = maxf(highest, p.focus)
		if p.focus < 0.0 or p.focus > Sim.FOCUS_MAX + 1e-9:
			check(false, "focus %.4f out of bounds at tick %d" % [p.focus, h.w.tick])
			break
	check(lowest >= 0.0 and highest <= Sim.FOCUS_MAX + 1e-9, "focus stayed within 0..%.0f (min %.3f max %.3f)" % [Sim.FOCUS_MAX, lowest, highest])
	check(lowest < 25.0, "the mashing actually spent Focus (min %.2f)" % lowest)
