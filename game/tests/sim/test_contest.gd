extends TestCase
## Grip contests: two Earth actors fighting over the same loose stone.
## Rules under test (CombatWorld._resolve_grips / take_control / release_body):
##  - requests are resolved after all actors acted, sorted by (body id, strength desc, actor id)
##  - exactly one controller per body; losers get control_fail("contest")
##  - an unheld body can be taken with any strength > 0.05; a held body only by a
##    challenger that beats the holder's authority by GRIP_MARGIN (hysteresis)
##  - a thrown body keeps a decaying residual authority (RESIDUAL_START / RESIDUAL_DECAY)
##    that third parties must beat (plus margin) until it expires.

var h: SimHarness
var a: ActorState
var b: ActorState
var stone: MatBody

## earth_tech seizes with base strength 0.85 over its reach (see ActEarth._seek).
const SEIZE_BASE := 0.85


func _setup(seed_value: int = 1, a_pos := Vector3(0, 0, 1.5), b_pos := Vector3(0, 0, -1.5),
		mass: float = 20.0, stone_pos := Vector3(0, 0.2, 0)) -> void:
	h = SimHarness.new(seed_value)
	a = h.actor("A", a_pos, 0, {}, Sim.Element.EARTH)
	b = h.actor("B", b_pos, 1, {}, Sim.Element.EARTH)
	stone = h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, mass, stone_pos, "scenario")
	h.step(20)   # the stone settles on the ground, the actors turn to face each other
	h.log.clear()


func _reach() -> float:
	return float(Moves.DEFS.earth_tech.reach)


func _unique_ids() -> bool:
	var seen := {}
	for body in h.w.bodies:
		if body.alive:
			if seen.has(body.id):
				return false
			seen[body.id] = true
	return true


func _count(type: String, key: String, value: Variant) -> int:
	var n := 0
	for e in h.events(type):
		if e.get(key) == value:
			n += 1
	return n


# ---------------------------------------------------------------- same-tick contest

func _run_same_tick(seed_value: int) -> Dictionary:
	_setup(seed_value)
	h.press(a, "tech")
	h.press(b, "tech")
	h.step(30)
	return {"winner": stone.controller, "log": str(h.log)}


func test_same_tick_contest_has_exactly_one_controller() -> void:
	_setup()
	var bodies_before := h.w.alive_count()
	var mass_before := stone.mass
	h.press(a, "tech")
	h.press(b, "tech")
	h.step(30)
	check(stone.controller == a.id or stone.controller == b.id, "someone controls the stone (ctl=%d)" % stone.controller)
	var a_has := a.held_body == stone.id
	var b_has := b.held_body == stone.id
	check(a_has != b_has, "exactly one actor holds the stone (a=%s b=%s)" % [a_has, b_has])
	var winner := a if a_has else b
	var loser := b if a_has else a
	check(stone.controller == winner.id, "stone.controller agrees with the holder")
	check(loser.held_body == -1, "loser holds nothing")
	check(_count("control_won", "body", stone.id) == 1, "exactly one control_won for the stone, got %d" % _count("control_won", "body", stone.id))
	var loser_fails := h.events("control_fail").filter(func(e): return e.actor == loser.id and e.body == stone.id and e.reason == "contest")
	check(loser_fails.size() >= 1, "loser got a control_fail(contest) event")
	var winner_fails := h.events("control_fail").filter(func(e): return e.actor == winner.id)
	check(winner_fails.is_empty(), "winner never got a control_fail")
	check(h.events("control_lost").is_empty(), "nobody lost control in a fresh contest")
	near(stone.mass, mass_before, 1e-9, "stone mass unchanged by the contest")
	check(h.w.alive_count() == bodies_before, "no body created or destroyed (%d -> %d)" % [bodies_before, h.w.alive_count()])
	check(_unique_ids(), "no duplicate body ids")
	near(h.w.stone_mass(), mass_before, 1e-9, "stone mass ledger: nothing ripped from the ground")
	check(h.events("rip").is_empty(), "the loser did not rip a second stone out of the ground")


func test_equal_strength_tie_goes_to_lowest_actor_id() -> void:
	# Symmetric geometry: both actors are 1.5 m from the stone, so strengths tie exactly.
	_setup()
	var sa := h.w.grip_strength(a, stone, SEIZE_BASE, _reach())
	var sb := h.w.grip_strength(b, stone, SEIZE_BASE, _reach())
	near(sa, sb, 1e-4, "symmetric setup gives equal grip strength")
	h.press(b, "tech")   # press order must not matter, only the documented sort
	h.press(a, "tech")
	h.step(30)
	check(stone.controller == a.id, "tie is broken by the lower actor id (ctl=%d)" % stone.controller)


func test_loser_never_duplicates_the_stone_while_contesting() -> void:
	_setup()
	h.press(a, "tech")
	h.press(b, "tech")
	h.step(150)   # far longer than rip_time: a loser with no target would rip a new stone
	check(h.events("rip").is_empty(), "no rip events during a long contest")
	check(h.w.alive_count() == 2, "still pool + one stone (%d bodies)" % h.w.alive_count())
	near(h.w.mass_ledger.ground_taken, 0.0, 1e-9, "nothing was taken from the ground")
	check(_unique_ids(), "no duplicate body ids")
	check(stone.controller == a.id and b.held_body == -1, "the holder keeps the stone for the whole contest")


func test_same_tick_winner_is_deterministic() -> void:
	var first := _run_same_tick(5)
	for k in 3:
		var again := _run_same_tick(5)
		check(again.winner == first.winner, "same seed, run %d: same winner" % k)
		check(again.log == first.log, "same seed, run %d: identical event log" % k)
	# Contests use no randomness: other seeds resolve the same way.
	for s in [1, 7, 99, 12345]:
		var other := _run_same_tick(s)
		check(other.winner == first.winner, "seed %d: same winner (%d vs %d)" % [s, other.winner, first.winner])


func test_closer_actor_wins_simultaneous_contest_even_with_higher_id() -> void:
	# A (id 1) is far from the stone, B (id 2) close: strength decides before the id tie-break.
	_setup(1, Vector3(0, 0, 6.0), Vector3(0, 0, -1.5))
	var sa := h.w.grip_strength(a, stone, SEIZE_BASE, _reach())
	var sb := h.w.grip_strength(b, stone, SEIZE_BASE, _reach())
	check(sb > sa + 0.05, "setup: B grips clearly stronger (%.3f vs %.3f)" % [sb, sa])
	h.press(a, "tech")
	h.press(b, "tech")
	h.step(30)
	check(stone.controller == b.id, "the stronger (closer) actor wins (ctl=%d)" % stone.controller)
	check(h.events("control_fail").any(func(e): return e.actor == a.id and e.reason == "contest"), "A got control_fail(contest)")


func test_mass_limit_boundary_blocks_grip_for_both() -> void:
	# max_control_mass is 80 kg: exactly 80 is allowed, anything above fails for everyone.
	_setup(1, Vector3(0, 0, 1.5), Vector3(0, 0, -1.5), 80.0)
	h.press(a, "tech")
	h.step(30)
	check(stone.controller == a.id, "an 80 kg stone (== limit) can be seized")
	_setup(1, Vector3(0, 0, 1.5), Vector3(0, 0, -1.5), 80.5)
	h.press(a, "tech")
	h.press(b, "tech")
	h.step(30)
	check(stone.controller == -1, "an 80.5 kg stone cannot be seized by anyone")
	var fails := h.events("control_fail").filter(func(e): return e.body == stone.id and e.reason == "mass")
	check(fails.size() >= 2, "both actors are told the stone is too heavy (%d events)" % fails.size())
	near(stone.mass, 80.5, 1e-9, "the boulder is untouched")


# ---------------------------------------------------------------- hysteresis

## A grabs the stone alone from z_a, then B (standing next to A's hand) challenges.
func _steal_trial(z_a: float) -> Dictionary:
	_setup(1, Vector3(0, 0, z_a), Vector3(1.0, 0, z_a - 2.0), 10.0, Vector3(0, 0.2, 0))
	var s_a_pre := h.w.grip_strength(a, stone, SEIZE_BASE, _reach())
	h.press(a, "tech")
	var got := h.until(func(): return stone.controller == a.id, 60)
	h.step(40)   # stone settles at A's hands
	var out := {"z": z_a, "a_got": got > 0, "s_a": s_a_pre, "auth": stone.authority}
	if got <= 0:
		return out
	out["s_b"] = h.w.grip_strength(b, stone, SEIZE_BASE, _reach())
	h.press(b, "tech")
	h.step(40)
	out["stole"] = stone.controller == b.id
	out["fails"] = h.events("control_fail").filter(func(e): return e.actor == b.id and e.reason == "contest").size()
	out["lost"] = h.events("control_lost").size()
	return out


func test_challenger_must_beat_holder_by_margin() -> void:
	var trials: Array[Dictionary] = []
	var z := 2.0
	while z <= 7.01:
		trials.append(_steal_trial(z))
		z += 0.25
	var stolen := 0
	var within_margin := 0
	var last_stole := false
	var monotonic := true
	for t in trials:
		if not check(t.a_got, "z=%.2f: the lone holder acquires the stone" % t.z):
			continue
		near(t.auth, t.s_a, 0.02, "z=%.2f: holder authority equals its grip strength at seize time" % t.z)
		var needed: float = t.s_a + CombatWorld.GRIP_MARGIN
		var should_steal: bool = t.s_b > needed
		check(t.stole == should_steal, "z=%.2f: steal=%s but challenger %.3f vs holder %.3f (+margin %.2f = %.3f)" % [
			t.z, t.stole, t.s_b, t.s_a, CombatWorld.GRIP_MARGIN, needed])
		note("z=%.2f s_a=%.3f s_b=%.3f stole=%s fails=%d" % [t.z, t.s_a, t.s_b, t.stole, t.fails])
		if t.stole:
			stolen += 1
		elif t.s_b > t.s_a:
			within_margin += 1
			check(t.fails >= 1, "z=%.2f: a challenger inside the margin is told control_fail(contest)" % t.z)
			check(t.lost == 0, "z=%.2f: the holder never lost control inside the margin" % t.z)
		if last_stole and not t.stole:
			monotonic = false
		last_stole = last_stole or t.stole
	check(monotonic, "once the holder is weak enough to be stolen from, weaker holders (farther) are too")
	check(stolen >= 1, "sweep contains a successful steal (%d)" % stolen)
	check(within_margin >= 1, "sweep contains a stronger-but-inside-margin challenger that fails (%d)" % within_margin)
	check(stolen < trials.size(), "sweep contains failed steals")


func test_holder_losing_control_gets_control_lost_and_clean_state() -> void:
	# A seizes from far away (weak authority); B stands at A's hands and takes it.
	var t := _steal_trial(6.5)
	check(t.get("stole", false), "setup: B took the stone (s_b %.3f vs s_a %.3f)" % [t.get("s_b", -1.0), t.s_a])
	var lost := h.events("control_lost").filter(func(e): return e.actor == a.id and e.body == stone.id)
	check(lost.size() == 1, "A receives exactly one control_lost, got %d" % lost.size())
	if lost.size() > 0:
		check(lost[0].reason == "contest" and lost[0].by == b.id, "control_lost names the contest and the winner")
		var won := h.events("control_won").filter(func(e): return e.actor == b.id and e.body == stone.id)
		check(won.size() == 1, "B receives exactly one control_won")
		if won.size() > 0:
			check(won[0].tick == lost[0].tick, "loss and win are the same tick")
	check(a.held_body == -1, "A holds nothing after the steal")
	check(b.held_body == stone.id and stone.controller == b.id, "B is the single controller")
	check(h.w.held(a) == null and h.w.held(b) == stone, "held() agrees")
	check(_unique_ids() and h.w.alive_count() == 2, "no duplicate bodies")
	# A is still channelling its technique with nothing in hand: releasing must unwind cleanly.
	h.release(a, "tech")
	var done := h.until(func(): return a.action == null, 90)
	check(done > 0, "A's action finishes after releasing (no stuck channel)")
	check(a.stun == 0.0 and not a.guarding and a.held_body == -1, "A is fully idle: stun %.2f guard %s held %d" % [a.stun, a.guarding, a.held_body])
	check(stone.controller == b.id, "A's cleanup did not touch B's stone")
	# B can still use the stolen stone normally.
	h.aim(b, Vector3(0, 0, 1))
	h.release(b, "tech")
	var launched := h.until(func(): return h.has_event("launch"), 60)
	check(launched > 0, "B throws the stolen stone")
	check(stone.attack_owner == b.id, "stone is B's attack")


# ---------------------------------------------------------------- residual authority

## A (Earth) throws a stone with a tap attack while C (third party) is already trying to seize it.
## The thrown stone is pinned in place right after the launch so the grip strength of C stays
## constant and only the residual authority changes (test scaffolding for the flight).
func _residual_world(c_z: float, c_press_after_launch: int) -> Dictionary:
	h = SimHarness.new(3)
	a = h.actor("A", Vector3(0, 0, 3.0), 0, {}, Sim.Element.EARTH)
	b = h.actor("C", Vector3(0, 0, c_z), 1, {}, Sim.Element.EARTH)
	stone = h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0.3, 0.2, 2.2), "scenario")
	h.step(20)
	h.log.clear()
	var out := {}
	if c_press_after_launch < 0:
		h.press(b, "tech")   # C is already contesting while A winds up
	h.press(a, "attack")
	h.step()
	h.release(a, "attack")
	var t := h.until(func(): return h.has_event("launch"), 60)
	out["launched"] = t > 0
	if t <= 0:
		return out
	var launch_tick: int = h.last_event("launch").tick
	# Pin the projectile where it was released.
	stone.vel = Vector3.ZERO
	stone.on_ground = true
	out["launch_tick"] = launch_tick
	out["residual_owner"] = stone.residual_owner
	out["residual0"] = stone.residual_authority
	out["s_c"] = h.w.grip_strength(b, stone, SEIZE_BASE, _reach())
	if c_press_after_launch >= 0:
		h.step(c_press_after_launch)
		out["residual_at_press"] = stone.residual_authority
		out["owner_at_press"] = stone.residual_owner
		h.press(b, "tech")
	var won_tick := -1
	for k in 120:
		h.step()
		if won_tick < 0 and stone.controller == b.id:
			won_tick = h.w.tick
			out["residual_after_win"] = stone.residual_authority
			out["owner_after_win"] = stone.residual_owner
			break
	out["won_tick"] = won_tick
	out["fails_after_launch"] = h.events("control_fail").filter(func(e): return e.actor == b.id and e.body == stone.id and e.reason == "contest" and e.tick >= launch_tick).size()
	return out


func test_thrown_stone_residual_authority_delays_third_party_regrab() -> void:
	# C is 5.65 m from the pinned stone: strong enough to take an unheld stone, too weak to beat
	# the thrower's residual authority plus margin until it has decayed.
	var r := _residual_world(-3.5, -1)
	check(r.get("launched", false), "A launched the stone")
	check(r.residual_owner == a.id, "thrown stone remembers its thrower as residual owner")
	check(r.residual0 > CombatWorld.RESIDUAL_START - 0.1 and r.residual0 <= CombatWorld.RESIDUAL_START, "residual starts near RESIDUAL_START (%.3f)" % r.residual0)
	var s_c: float = r.s_c
	check(s_c > 0.05 and s_c + 0.001 < CombatWorld.RESIDUAL_START + CombatWorld.GRIP_MARGIN, "setup: C is strong enough alone but weaker than residual+margin (%.3f)" % s_c)
	check(r.fails_after_launch >= 1, "C's immediate attempts fail with control_fail(contest) (%d)" % r.fails_after_launch)
	check(r.won_tick > 0, "C does get the stone once the residual has decayed")
	# Earliest tick C can win: residual must have decayed below strength - margin.
	var need_decay: float = CombatWorld.RESIDUAL_START - (s_c - CombatWorld.GRIP_MARGIN)
	var min_ticks := int(floor(need_decay / CombatWorld.RESIDUAL_DECAY / Sim.DT)) - 2
	var waited: int = r.won_tick - r.launch_tick
	note("s_c %.3f residual0 %.3f waited %d ticks (min %d), fails %d" % [s_c, r.residual0, waited, min_ticks, r.fails_after_launch])
	check(waited >= min_ticks, "C had to wait for the decay: %d ticks (need >= ~%d)" % [waited, min_ticks])
	check(waited <= int(CombatWorld.RESIDUAL_START / CombatWorld.RESIDUAL_DECAY / Sim.DT) + 3, "C wins no later than full decay (%d ticks)" % waited)
	check(r.owner_after_win == -1 and r.residual_after_win == 0.0, "taking control clears the residual")


func test_regrab_after_residual_decay_is_immediate() -> void:
	# Same geometry, but C only starts well after the residual has expired.
	var ticks_to_zero := int(ceil(CombatWorld.RESIDUAL_START / CombatWorld.RESIDUAL_DECAY / Sim.DT)) + 2
	var r := _residual_world(-3.5, ticks_to_zero)
	check(r.get("launched", false), "A launched the stone")
	check(r.owner_at_press == -1 and r.residual_at_press == 0.0, "residual fully decayed after %d ticks (owner %d, %.3f)" % [ticks_to_zero, r.owner_at_press, r.residual_at_press])
	check(r.won_tick > 0, "C takes the stone")
	check(r.fails_after_launch == 0, "no contest failures once the residual is gone (%d)" % r.fails_after_launch)


func test_close_third_party_beats_residual_authority() -> void:
	# A challenger standing at the thrower's hands is stronger than residual + margin even at once.
	var r := _residual_world(1.2, -1)
	check(r.get("launched", false), "A launched the stone")
	check(r.s_c > CombatWorld.RESIDUAL_START + CombatWorld.GRIP_MARGIN - 0.05, "setup: C is near the stone (%.3f)" % r.s_c)
	check(r.won_tick > 0, "a close, strong third party can take a just-thrown stone")
	if r.won_tick > 0:
		check(r.won_tick - r.launch_tick <= 8, "and does so within a few ticks (%d)" % (r.won_tick - r.launch_tick))
