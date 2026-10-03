extends TestCase
## Energy accounting under heat/draw ping-pong, and a 10-minute seeded random-input soak of two
## fully equipped fighters. The soak is run once per seed and cached for the checks below.

const FULL_KIT := {"magma": true, "heat_draw": true, "lightning": true, "redirect_current": true, "glide": true}
const SOAK_TICKS := 36000          # 10 simulated minutes
const STUCK_TICKS := 300           # 5 s
const MASS_REL_TOL := 1e-3

static var _soak_cache := {}


# ================================================================ energy ping-pong

var h: SimHarness
var a: ActorState
var b: ActorState
var stone: MatBody

var _e0 := 0.0
var _prev_focus := {}
var _focus_spent := 0.0
var _max_reserve := 0.0
var _worst_drift := 0.0
var _drift_tick := -1
var _reserve_over := 0.0


func _fire_pair(seed_value: int = 5) -> void:
	h = SimHarness.new(seed_value)
	a = h.actor("A", Vector3(0, 0, 2), 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	b = h.actor("B", Vector3(0, 0, -2), 1, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	stone = h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0, 0.2, 0), "scenario")
	h.step(20)
	h.log.clear()
	_begin_accounting()


func _begin_accounting() -> void:
	_e0 = h.w.system_energy() - h.w.ledger_balance()
	_prev_focus.clear()
	for x in h.w.actors:
		_prev_focus[x.id] = x.focus
	_focus_spent = 0.0
	_max_reserve = 0.0
	_worst_drift = 0.0
	_drift_tick = -1
	_reserve_over = 0.0


## Steps n ticks, checking the energy identity, reserve cap and Focus accounting every tick.
func _run(n: int) -> void:
	for k in n:
		h.step()
		var drift := absf(h.w.system_energy() - h.w.ledger_balance() - _e0)
		if drift > _worst_drift:
			_worst_drift = drift
			_drift_tick = h.w.tick
		for x in h.w.actors:
			_max_reserve = maxf(_max_reserve, x.heat_reserve)
			_reserve_over = maxf(_reserve_over, x.heat_reserve - Sim.RESERVE_MAX)
			var d: float = _prev_focus[x.id] - x.focus
			if d > 0.0:
				_focus_spent += d   # spends and regen never share a tick: a drop is a spend
			_prev_focus[x.id] = x.focus


func _tolerance() -> float:
	return 0.05


func test_heat_draw_pingpong_keeps_the_energy_ledger_balanced() -> void:
	_fire_pair()
	var rounds := 12
	var drew := {a.id: 0.0, b.id: 0.0}
	var spent_reserve := 0.0
	for r in rounds:
		var heater := a if r % 2 == 0 else b
		var drawer := b if r % 2 == 0 else a
		h.log.clear()
		# Heater seizes the stone and pours heat into it (reserve first, then Focus), then lets go.
		h.press(heater, "tech")
		_run(45)
		h.cancel_tech(heater)
		_run(4)
		h.it(heater).tech_cancel = false
		check(stone.alive and stone.controller == -1, "round %d: the stone was dropped, not lost" % r)
		var hot_before := stone.thermal_energy()
		# Drawer pulls the heat back out into its own reserve.
		var reserve0 := drawer.heat_reserve
		h.press(drawer, "tech")
		var peak := reserve0
		for k in 110:
			_run(1)
			peak = maxf(peak, drawer.heat_reserve)
		h.release(drawer, "tech")
		_run(20)
		drew[drawer.id] += maxf(0.0, peak - reserve0)
		check(_worst_drift <= _tolerance(), "round %d: energy ledger drifted by %.5f HU at tick %d" % [r, _worst_drift, _drift_tick])
		check(_reserve_over <= 1e-9, "round %d: a reserve exceeded RESERVE_MAX by %.6f" % [r, _reserve_over])
		# Alternate a flare from the drawer's reserve into the heater: more ledger paths.
		if r % 3 == 2:
			h.press(drawer, "attack")
			_run(1)
			h.release(drawer, "attack")
			_run(30)
		if h.w.get_actor(heater.id).balance < 30.0:
			heater.balance = 100.0
		heater.health = 100.0
		drawer.health = 100.0
	check(_worst_drift <= _tolerance(), "final: energy ledger drift %.5f HU (worst at tick %d)" % [_worst_drift, _drift_tick])
	near(h.w.system_energy() - _e0, h.w.ledger_balance(), _tolerance(), "system_energy delta == ledger_balance at the end")
	check(drew[a.id] > 50.0 and drew[b.id] > 50.0, "both fighters really drew heat (A %.0f HU, B %.0f HU)" % [drew[a.id], drew[b.id]])
	check(_max_reserve > 100.0, "reserves filled during the exchange (max %.0f HU)" % _max_reserve)
	check(_max_reserve <= Sim.RESERVE_MAX + 1e-9, "no reserve above RESERVE_MAX (max %.3f)" % _max_reserve)
	check(h.w.ledger.generated > 0.0 and h.w.ledger.reserve_dissipated > 0.0 and h.w.ledger.ambient < 0.0, "the ledger saw generation, dissipation and air loss %s" % [h.w.ledger])
	var cap := _focus_spent * Sim.HU_PER_FOCUS
	check(h.w.ledger.generated <= cap + 1e-6, "heat created (%.1f HU) never exceeds Focus spent (%.1f) x HU_PER_FOCUS = %.1f" % [h.w.ledger.generated, _focus_spent, cap])
	check(h.w.ledger.generated > 0.0, "some heat was actually created from Focus")
	note("pingpong: generated %.0f HU from %.1f Focus, max reserve %.0f, worst drift %.6f" % [h.w.ledger.generated, _focus_spent, _max_reserve, _worst_drift])
	near(stone.mass, 20.0, 1e-9, "the stone survived with its mass")


func test_draw_stops_at_reserve_cap_and_never_overfills() -> void:
	_fire_pair()
	# A 40 kg blob of molten rock holds ~870 HU, far more than a reserve can take (500 HU).
	h.w.remove_body(stone, "scenario")
	var blob := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.BLOB, 40.0, Vector3(0, 0.2, 0.5), "scenario")
	blob.temp = 1200.0
	blob.liquid = 1.0
	blob.phase = Sim.Phase.MOLTEN
	_begin_accounting()
	var before := blob.thermal_energy()
	h.press(a, "tech")
	_run(30)
	check(a.action != null and a.action.data.get("mode", "") == "DRAW", "A chose DRAW for a molten target")
	_run(300)
	check(_max_reserve > Sim.RESERVE_MAX - 30.0, "the reserve filled to the cap (max %.1f)" % _max_reserve)
	check(_reserve_over <= 1e-9, "the reserve never exceeded RESERVE_MAX (over by %.6f)" % _reserve_over)
	check(a.heat_reserve <= Sim.RESERVE_MAX + 1e-9, "reserve %.2f <= cap" % a.heat_reserve)
	check(blob.thermal_energy() >= 0.0, "the blob keeps non-negative heat")
	check(blob.thermal_energy() < before, "heat left the blob")
	check(_worst_drift <= _tolerance(), "energy ledger drift %.5f HU at tick %d" % [_worst_drift, _drift_tick])
	var drawn_focus := _focus_spent
	check(drawn_focus <= 500.0 / Sim.DRAW_HU_PER_FOCUS * 1.6 + 1.0, "drawing costs only Focus/DRAW_HU_PER_FOCUS (%.1f Focus)" % drawn_focus)
	check(h.w.ledger.generated == 0.0, "drawing creates no heat")


func test_reserve_full_event_is_reported() -> void:
	_fire_pair()
	h.w.remove_body(stone, "scenario")
	var blob := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.BLOB, 40.0, Vector3(0, 0.2, 0.5), "scenario")
	blob.temp = 1200.0
	blob.liquid = 1.0
	blob.phase = Sim.Phase.MOLTEN
	h.press(a, "tech")
	var full := false
	for k in 400:
		h.step()
		if h.has_event("reserve_full"):
			full = true
			break
	check(full, "reserve_full reported once the reserve is topped up")
	check(a.heat_reserve <= Sim.RESERVE_MAX + 1e-9, "at or below the cap (%.2f)" % a.heat_reserve)


func test_vent_dumps_the_reserve_into_the_ledger() -> void:
	# No stone, no water in reach: the thermal technique vents a reserve of at least 40 HU.
	for case in [[300.0, true], [40.0, true], [39.0, false]]:
		h = SimHarness.new(5)
		a = h.actor("A", Vector3(-4, 0, 6), 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
		h.step(5)
		a.heat_reserve = case[0]
		_begin_accounting()
		h.press(a, "tech")
		_run(40)
		h.release(a, "tech")
		_run(60)
		if case[1]:
			check(h.has_event("vent"), "reserve %.0f: vented" % case[0])
			check(a.heat_reserve == 0.0, "reserve %.0f: emptied (%.2f)" % [case[0], a.heat_reserve])
			check(h.w.ledger.vented > case[0] - 12.0 and h.w.ledger.vented <= case[0], "reserve %.0f: ledger.vented %.2f" % [case[0], h.w.ledger.vented])
		else:
			check(not h.has_event("vent"), "reserve %.0f: too little to vent" % case[0])
		check(_worst_drift <= _tolerance(), "reserve %.0f: ledger drift %.5f" % [case[0], _worst_drift])
		check(a.action == null, "reserve %.0f: no stuck action" % case[0])


func test_reserve_passively_dissipates_and_is_accounted() -> void:
	h = SimHarness.new(5)
	a = h.actor("A", Vector3(-4, 0, 6), 0, {}, Sim.Element.FIRE)
	h.step(5)
	a.heat_reserve = 100.0
	_begin_accounting()
	_run(60)
	near(a.heat_reserve, 100.0 - Sim.RESERVE_DISSIPATE, 0.01, "reserve dissipates at RESERVE_DISSIPATE per second")
	check(_worst_drift <= _tolerance(), "ledger drift %.6f" % _worst_drift)
	_run(60 * 20)
	check(a.heat_reserve == 0.0, "reserve eventually empties and never goes negative")
	near(h.w.ledger.reserve_dissipated, 100.0, 1e-6, "all 100 HU went into reserve_dissipated")
	check(_worst_drift <= _tolerance(), "ledger drift %.6f" % _worst_drift)


# ================================================================ targeted energy-leak regressions

func test_heavy_earth_gather_onto_a_warm_stone_creates_no_heat() -> void:
	# A heavy earth attack gathers extra stone from the ground (ambient temperature) into the
	# stone in hand. If the stone in hand is warm, the energy of the combined body must be the
	# sum of the parts, not the warm temperature applied to the new mass.
	h = SimHarness.new(5)
	a = h.actor("A", Vector3(0, 0, 6), 0, {}, Sim.Element.EARTH)
	b = h.actor("B", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
	stone = h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0, 0.2, 5.2), "scenario")
	stone.temp = 400.0
	h.step(20)
	h.log.clear()
	_begin_accounting()
	var heat0 := stone.thermal_energy()
	h.press(a, "attack")
	_run(60)
	check(h.has_event("acquire"), "the heavy attack gathered extra stone")
	check(absf(stone.mass - float(Moves.DEFS.earth_attack.heavy_mass)) < 1e-9, "the stone grew to the heavy mass (%.1f kg)" % stone.mass)
	check(stone.thermal_energy() <= heat0 + 1e-6, "gathering ambient stone cannot add heat: %.2f HU -> %.2f HU" % [heat0, stone.thermal_energy()])
	check(_worst_drift <= _tolerance(), "energy ledger drifted by %.3f HU at tick %d" % [_worst_drift, _drift_tick])
	h.release(a, "attack")
	_run(60)
	check(_worst_drift <= _tolerance(), "energy ledger drift after the throw %.3f HU" % _worst_drift)


func test_drawing_from_a_partially_thawed_puddle_conserves_energy() -> void:
	# A puddle at 0 degC that is 30% ice holds negative latent energy. Moving its water into a
	# stream moves that energy with it (or turns the stream into slush); it must not appear or vanish.
	h = SimHarness.new(5)
	a = h.actor("D", Vector3(-4, 0, 5), 0, {}, Sim.Element.WATER)
	h.step(5)
	var puddle := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 4.0, Vector3(-4, 0, 6.5), "scenario")
	puddle.update_radius_puddle()
	puddle.temp = 0.0
	puddle.liquid = 0.7
	a.water_carried = 0.0
	_begin_accounting()
	var water0 := h.w.water_mass()
	h.press(a, "tech")
	_run(40)
	var held := h.w.held(a)
	check(held != null and held.origin.begins_with("draw:"), "D is holding water drawn from the puddle")
	check(puddle.mass < 4.0, "the puddle was drawn from (%.2f kg left)" % puddle.mass)
	near(h.w.water_mass(), water0, 1e-6, "water mass conserved")
	check(_worst_drift <= _tolerance(), "energy ledger drifted by %.3f HU at tick %d" % [_worst_drift, _drift_tick])


# ================================================================ soak

class Soak:
	extends RefCounted
	var h: SimHarness
	var rng := RandomNumberGenerator.new()
	var hold_until := {}
	var next_decision := {}
	var move_until := {}
	var violations := {}          # category -> Array[String] (first few)
	var event_counts := {}
	var max_bodies := 0
	var worst_stone := 0.0
	var worst_water := 0.0
	var worst_energy := 0.0
	var worst_energy_tick := -1
	var longest_busy := {}        # actor id -> ticks
	var longest_stun := {}
	var busy_streak := {}
	var stun_streak := {}
	var checkpoints: Array[int] = []
	var final_hash := 0
	var min_balance := 0.0
	var water0 := 0.0
	var stone0 := 0.0
	var e0 := 0.0
	var heals := 0

	func note(cat: String, msg: String) -> void:
		if not violations.has(cat):
			violations[cat] = []
		if violations[cat].size() < 4:
			violations[cat].append("t%d: %s" % [h.w.tick, msg])


func _finite(v: Vector3) -> bool:
	return is_finite(v.x) and is_finite(v.y) and is_finite(v.z)


func _make_soak(seed_value: int) -> Soak:
	var s := Soak.new()
	s.h = SimHarness.new(seed_value)
	s.rng.seed = seed_value * 7919 + 13
	var ea := s.rng.randi() % 4
	var eb := s.rng.randi() % 4
	var pa := s.h.actor("A", Vector3(0, 0, 6), 0, FULL_KIT, ea)
	var pb := s.h.actor("B", Vector3(0, 0, -6), 1, FULL_KIT, eb)
	for x in [pa, pb]:
		s.hold_until[x.id] = 0
		s.next_decision[x.id] = 0
		s.move_until[x.id] = 0
		s.longest_busy[x.id] = 0
		s.longest_stun[x.id] = 0
		s.busy_streak[x.id] = 0
		s.stun_streak[x.id] = 0
	s.water0 = s.h.w.water_mass()
	s.stone0 = s.h.w.stone_mass()
	s.e0 = s.h.w.system_energy() - s.h.w.ledger_balance()
	return s


func _random_input(s: Soak, x: ActorState) -> void:
	var h2 := s.h
	var t := h2.w.tick
	var it := h2.it(x)
	if t >= s.hold_until[x.id]:
		if it.attack_held:
			h2.release(x, "attack")
		if it.tech_held:
			if s.rng.randf() < 0.15:
				h2.cancel_tech(x)
			else:
				h2.release(x, "tech")
		if it.guard_held:
			h2.release(x, "guard")
	if t >= s.move_until[x.id]:
		it.move = Vector3.ZERO
	if t < s.next_decision[x.id]:
		return
	s.next_decision[x.id] = t + 3 + s.rng.randi() % 38
	var other: ActorState = h2.w.actors[1] if x.id == 1 else h2.w.actors[0]
	var r := s.rng.randf()
	if s.rng.randf() < 0.35:
		var ang := s.rng.randf() * TAU
		h2.aim(x, Vector3(sin(ang), 0, cos(ang)))
	elif s.rng.randf() < 0.3:
		h2.aim(x, other.pos - x.pos)
	elif s.rng.randf() < 0.2:
		it.aim_active = false
	var hold_ticks := 0
	if r < 0.24:
		h2.press(x, "attack")
		hold_ticks = 0 if s.rng.randf() < 0.4 else 5 + s.rng.randi() % 85
	elif r < 0.46:
		h2.press(x, "tech")
		hold_ticks = 6 + s.rng.randi() % 150
	elif r < 0.60:
		h2.press(x, "guard")
		hold_ticks = 4 + s.rng.randi() % 70
	elif r < 0.72:
		var ang2 := s.rng.randf() * TAU
		it.move = Vector3(sin(ang2), 0, cos(ang2))
		s.move_until[x.id] = t + 2
		h2.press(x, "evade")
	elif r < 0.82:
		h2.element(x, s.rng.randi() % 4)
	elif r < 0.94:
		var ang3 := s.rng.randf() * TAU
		it.move = Vector3(sin(ang3), 0, cos(ang3)) * (0.3 + 0.7 * s.rng.randf())
		s.move_until[x.id] = t + 10 + s.rng.randi() % 80
	s.hold_until[x.id] = t + hold_ticks


func _state_hash(s: Soak) -> int:
	var w := s.h.w
	var parts: Array = [w.tick, w.rng.state, w.ledger.duplicate(), w.mass_ledger.duplicate()]
	for x in w.actors:
		parts.append([x.pos, x.vel, x.facing, x.health, x.balance, x.focus, x.heat_reserve, x.wetness,
			x.water_carried, x.element, x.stun, x.stun_kind, x.iframes, x.guarding, x.held_body, x.wall_body,
			x.action.id if x.action != null else "", x.action.phase if x.action != null else -1])
	for b in w.bodies:
		if b.alive:
			parts.append([b.id, b.mat, b.form, b.phase, b.mass, b.temp, b.liquid, b.pos, b.vel, b.controller,
				b.attack_id, b.wall_rise, b.wave_budget])
	return hash(parts)


func _check_tick(s: Soak) -> void:
	var w := s.h.w
	var half := w.arena.half_size
	for x in w.actors:
		if not _finite(x.pos) or not _finite(x.vel) or not is_finite(x.facing):
			s.note("nan_actor", "%s pos %s vel %s facing %s" % [x.name, x.pos, x.vel, x.facing])
		elif absf(x.pos.x) > half + 1e-6 or absf(x.pos.z) > half + 1e-6 or x.pos.y < -1.0 or x.pos.y > 40.0:
			s.note("out_of_arena", "%s at %s" % [x.name, x.pos])
		if not (x.focus >= 0.0 and x.focus <= Sim.FOCUS_MAX + 1e-9):
			s.note("focus_range", "%s focus %.5f" % [x.name, x.focus])
		if not (x.heat_reserve >= 0.0 and x.heat_reserve <= Sim.RESERVE_MAX + 1e-9):
			s.note("reserve_range", "%s reserve %.5f" % [x.name, x.heat_reserve])
		if not (x.health >= 0.0 and x.health <= Sim.HEALTH_MAX + 1e-9):
			s.note("health_range", "%s health %.5f" % [x.name, x.health])
		if not (x.balance <= Sim.BALANCE_MAX + 1e-9):
			s.note("balance_range", "%s balance %.5f" % [x.name, x.balance])
		s.min_balance = minf(s.min_balance, x.balance)
		if not (x.wetness >= 0.0 and x.wetness <= 1.0 + 1e-9):
			s.note("wetness_range", "%s wetness %.5f" % [x.name, x.wetness])
		if not (x.water_carried >= -1e-9 and x.water_carried <= 6.0 + 1e-9):
			s.note("waterskin_range", "%s carries %.5f" % [x.name, x.water_carried])
		# Stuck detection.
		var it := s.h.it(x)
		var holding := it.attack_held or it.guard_held or it.tech_held
		var busy := x.action != null or x.stun > 0.0
		if busy and not holding:
			s.busy_streak[x.id] += 1
		else:
			s.busy_streak[x.id] = 0
		if x.stun > 0.0:
			s.stun_streak[x.id] += 1
		else:
			s.stun_streak[x.id] = 0
		s.longest_busy[x.id] = maxi(s.longest_busy[x.id], s.busy_streak[x.id])
		s.longest_stun[x.id] = maxi(s.longest_stun[x.id], s.stun_streak[x.id])
		if s.busy_streak[x.id] == STUCK_TICKS + 1:
			s.note("stuck_action", "%s busy for 5 s without holding anything: action %s phase %s stun %.2f (%s)" % [
				x.name, x.action.id if x.action != null else "none", x.action.phase_name() if x.action != null else "-", x.stun, x.stun_kind])
		if s.stun_streak[x.id] == STUCK_TICKS + 1:
			s.note("stuck_stun", "%s stunned for 5 s (%s)" % [x.name, x.stun_kind])
	var alive := 0
	for body in w.bodies:
		if not body.alive:
			continue
		alive += 1
		if not _finite(body.pos) or not _finite(body.vel) or not is_finite(body.mass) or not is_finite(body.temp) or not is_finite(body.liquid):
			s.note("nan_body", body.describe())
		elif body.mass < -1e-9:
			s.note("negative_mass", body.describe())
	s.max_bodies = maxi(s.max_bodies, alive)
	if alive > Sim.MAX_BODIES:
		s.note("body_cap", "%d alive bodies (cap %d)" % [alive, Sim.MAX_BODIES])
	var sd := absf(w.stone_mass() - s.stone0)
	s.worst_stone = maxf(s.worst_stone, sd)
	if sd > MASS_REL_TOL * maxf(1.0, w.mass_ledger.ground_taken):
		s.note("stone_mass", "stone mass drifted %.5f kg (taken %.1f)" % [sd, w.mass_ledger.ground_taken])
	var wd := absf(w.water_mass() - s.water0)
	s.worst_water = maxf(s.worst_water, wd)
	if wd > MASS_REL_TOL * s.water0:
		s.note("water_mass", "water mass drifted %.5f kg" % wd)


func _run_soak(seed_value: int, ticks: int = SOAK_TICKS) -> Soak:
	var s := _make_soak(seed_value)
	var w := s.h.w
	for k in ticks:
		for x in w.actors:
			_random_input(s, x)
			if x.health < 25.0:
				x.health = Sim.HEALTH_MAX   # keep the fight going: the referee revives a loser
				s.heals += 1
		s.h.step()
		for e in s.h.log:
			s.event_counts[e.type] = int(s.event_counts.get(e.type, 0)) + 1
		s.h.log.clear()
		_check_tick(s)
		if k % 600 == 599:
			var d := absf(w.system_energy() - w.ledger_balance() - s.e0)
			if d > s.worst_energy:
				s.worst_energy = d
				s.worst_energy_tick = w.tick
			s.checkpoints.append(_state_hash(s))
	s.final_hash = _state_hash(s)
	return s


func _soak_for(seed_value: int) -> Soak:
	if not _soak_cache.has(seed_value):
		_soak_cache[seed_value] = _run_soak(seed_value)
	return _soak_cache[seed_value]


func _report(s: Soak, cats: Array) -> void:
	for c in cats:
		var list: Array = s.violations.get(c, [])
		check(list.is_empty(), "%s violations: %s" % [c, "; ".join(list)])


const SOAK_SEED := 11


func test_soak_actually_exercises_the_rules() -> void:
	var s := _soak_for(SOAK_SEED)
	check(s.h.w.tick == SOAK_TICKS, "ran %d ticks" % s.h.w.tick)
	var needed := ["action", "hit", "launch", "guard", "evade", "control_won", "flare", "split"]
	for t in needed:
		check(int(s.event_counts.get(t, 0)) > 0, "the soak produced '%s' events (%d)" % [t, int(s.event_counts.get(t, 0))])
	var kinds := 0
	for t in ["lightning", "wall", "shatter", "steam", "thermal", "perfect_deflect", "knockdown", "getup", "wave_settle", "drawing", "updraft", "intercept"]:
		if int(s.event_counts.get(t, 0)) > 0:
			kinds += 1
	check(kinds >= 7, "a broad mix of mechanics fired (%d of 12 spot checks)" % kinds)
	note("events: %s" % [s.event_counts])
	note("max bodies %d, worst stone drift %.6f kg, worst water drift %.6f kg, worst energy drift %.4f HU, min balance %.1f, heals %d" % [
		s.max_bodies, s.worst_stone, s.worst_water, s.worst_energy, s.min_balance, s.heals])


func test_soak_actors_stay_finite_in_the_arena_and_in_range() -> void:
	var s := _soak_for(SOAK_SEED)
	_report(s, ["nan_actor", "nan_body", "out_of_arena", "focus_range", "reserve_range", "health_range",
		"wetness_range", "waterskin_range", "negative_mass"])


func test_soak_body_count_never_exceeds_max_bodies() -> void:
	var s := _soak_for(SOAK_SEED)
	_report(s, ["body_cap"])
	check(s.max_bodies <= Sim.MAX_BODIES, "peak alive bodies %d <= %d" % [s.max_bodies, Sim.MAX_BODIES])
	check(s.max_bodies >= 4, "the soak really created bodies (peak %d)" % s.max_bodies)


func test_soak_conserves_stone_and_water_mass() -> void:
	var s := _soak_for(SOAK_SEED)
	_report(s, ["stone_mass", "water_mass"])
	check(s.worst_stone <= MASS_REL_TOL * maxf(1.0, s.h.w.mass_ledger.ground_taken), "stone mass drift %.6f kg of %.0f kg handled" % [s.worst_stone, s.h.w.mass_ledger.ground_taken])
	check(s.worst_water <= MASS_REL_TOL * s.water0, "water mass drift %.6f kg of %.0f kg" % [s.worst_water, s.water0])
	check(s.h.w.mass_ledger.ground_taken > 100.0, "lots of stone moved through the ledger (%.0f kg)" % s.h.w.mass_ledger.ground_taken)


func test_soak_no_actor_is_stuck_in_an_action_or_stun() -> void:
	var s := _soak_for(SOAK_SEED)
	_report(s, ["stuck_action", "stuck_stun"])
	for id in s.longest_busy.keys():
		check(s.longest_busy[id] <= STUCK_TICKS, "actor %d: longest busy streak without holding a button %d ticks" % [id, s.longest_busy[id]])
		check(s.longest_stun[id] <= STUCK_TICKS, "actor %d: longest stun %d ticks" % [id, s.longest_stun[id]])
	note("longest busy %s, longest stun %s" % [s.longest_busy, s.longest_stun])


func test_soak_energy_ledger_stays_balanced() -> void:
	var s := _soak_for(SOAK_SEED)
	check(s.worst_energy <= 1.0, "energy ledger drift peaked at %.4f HU (tick %d)" % [s.worst_energy, s.worst_energy_tick])
	var w := s.h.w
	near(w.system_energy() - w.ledger_balance() - s.e0, 0.0, 1.0, "final energy identity")


func test_soak_balance_stays_in_its_documented_range() -> void:
	# Sim documents Balance as 0..100.
	var s := _soak_for(SOAK_SEED)
	_report(s, ["balance_range"])
	check(s.min_balance >= -1e-9, "balance never went below 0 (min %.2f)" % s.min_balance)


func test_soak_is_deterministic_for_a_seed() -> void:
	var first := _soak_for(SOAK_SEED)
	var again := _run_soak(SOAK_SEED)
	check(first.checkpoints.size() == again.checkpoints.size(), "same number of checkpoints")
	var diverged := -1
	for k in mini(first.checkpoints.size(), again.checkpoints.size()):
		if first.checkpoints[k] != again.checkpoints[k]:
			diverged = k
			break
	check(diverged < 0, "state hashes diverged at checkpoint %d (tick %d)" % [diverged, (diverged + 1) * 600])
	check(first.final_hash == again.final_hash, "same seed: identical final state hash (%d vs %d)" % [first.final_hash, again.final_hash])
	check(first.event_counts == again.event_counts, "same seed: identical event counts")
	# A different seed must really play differently (the hash is sensitive): compare tick 6000.
	var other := _run_soak(SOAK_SEED + 1, 6000)
	check(other.final_hash != first.checkpoints[9], "a different seed gives a different state at tick 6000")
