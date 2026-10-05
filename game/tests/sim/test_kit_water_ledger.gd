extends WaterKitTest
## Scripted 60 s exchanges between Water fighters (all four sub-elements, random input like a player, plus a few
## deliberate set-pieces): the energy ledger and every mass ledger stay exact (water incl. mist, steam, plant and ambient
## moisture; plant; earth; metal), the body cap holds, nobody gets stuck, every fx key is catalogued and the same seed
## plays the same way (docs/MOVESET.md §15.9).

const SOAK_TICKS := 3600


class Soak:
	var h: SimHarness
	var rng := RandomNumberGenerator.new()
	var next := {}
	var hold_until := {}
	var base := {}
	var worst := {}
	var bad: Array[String] = []
	var busy := {}
	var fx_keys := {}
	var outcomes := {}
	var counts := {}
	var moves := {}
	var max_bodies := 0


func _input(s: Soak, x: ActorState, other: ActorState, rival_subs: bool) -> void:
	var hh := s.h
	var it := hh.it(x)
	var t := hh.w.tick
	if t >= int(s.hold_until.get(x.id, 0)):
		it.attack_held = false
		it.guard_held = false
		it.tech_held = false
		it.evade_held = false
	if t < int(s.next.get(x.id, 0)):
		return
	s.next[x.id] = t + 4 + s.rng.randi() % 30
	var r := s.rng.randf()
	if s.rng.randf() < 0.5:
		hh.aim(x, other.pos - x.pos)
	var hold := 0
	if r < 0.32:
		hh.press(x, "attack")
		if s.rng.randf() < 0.55:
			it.attack_gesture = 1 + s.rng.randi() % 3
		hold = 0 if s.rng.randf() < 0.3 else 5 + s.rng.randi() % 130
	elif r < 0.47:
		hh.press(x, "guard")
		hold = 6 + s.rng.randi() % 90
	elif r < 0.55 and x.action != null and x.action.id == "guard":
		it.guard_gesture = 1 + s.rng.randi() % 2
		it.guard_held = true
	elif r < 0.68:
		hh.press(x, "tech")
		hold = 6 + s.rng.randi() % 120
	elif r < 0.78:
		hh.press(x, "evade")
		it.evade_held = s.rng.randf() < 0.5
		it.move = Vector3(s.rng.randf() - 0.5, 0, s.rng.randf() - 0.5)
		hold = 2 + s.rng.randi() % 60
	elif r < 0.86 and x.element == Sim.Element.WATER:
		hh.sub(x, s.rng.randi() % 4)
	elif r < 0.9 and rival_subs:
		hh.element(x, s.rng.randi() % 4)
	else:
		var a := s.rng.randf() * TAU
		it.move = Vector3(sin(a), 0, cos(a)) * s.rng.randf()
	s.hold_until[x.id] = t + hold


func _bad(s: Soak, msg: String) -> void:
	if s.bad.size() < 8:
		s.bad.append("t%d: %s" % [s.h.w.tick, msg])


func _run(seed_value: int, ticks: int, rival_element: int) -> Soak:
	var s := Soak.new()
	s.h = SimHarness.new(seed_value)
	s.rng.seed = seed_value * 31 + 7
	var a := s.h.actor("A", Vector3(-2, 0, 6), 0, {"glide": true, "lightning": true}, Sim.Element.WATER)
	var b := s.h.actor("B", Vector3(2, 0, -6), 1, {"glide": true, "lightning": true}, rival_element)
	a.subs = [0, 0, 0, 0]
	b.subs = [0, 0, 0, 0]
	var w := s.h.w
	s.base = snap(w)
	for k in ticks:
		for x in w.actors:
			_input(s, x, b if x == a else a, rival_element != Sim.Element.WATER)
			if x.health < 25.0:
				x.health = Sim.HEALTH_MAX
		var pre_e := w.system_energy() - w.ledger_balance()
		s.h.step()
		var post_e := w.system_energy() - w.ledger_balance()
		if absf(post_e - pre_e) > 1e-3 and s.bad.size() < 6:
			var types := []
			for e in s.h.log:
				if not (e.type in ["fx", "spawn"]):
					types.append("%s:%s" % [e.type, e.get("move", e.get("outcome", e.get("phase", e.get("reason", ""))))])
			_bad(s, "energy jump %.2f: %s" % [post_e - pre_e, ", ".join(types)])
		for e in s.h.log:
			s.counts[e.type] = int(s.counts.get(e.type, 0)) + 1
			if e.type == "fx":
				s.fx_keys["%s/%s/%s" % [e.fx, e.mat, e.shape]] = true
			elif e.type == "interaction":
				s.outcomes[e.outcome] = true
			elif e.type == "action" and e.phase == "startup":
				s.moves[String(e.move)] = true
		s.h.log.clear()
		var alive := 0
		for bd in w.bodies:
			if bd.alive:
				alive += 1
				if not (is_finite(bd.pos.x) and is_finite(bd.pos.y) and is_finite(bd.mass) and is_finite(bd.temp)) or bd.mass < -1e-9:
					_bad(s, "body %s" % bd.describe())
		s.max_bodies = maxi(s.max_bodies, alive)
		for x in w.actors:
			if not (is_finite(x.pos.x) and is_finite(x.pos.z)) or absf(x.pos.x) > 16.0 or absf(x.pos.z) > 16.0:
				_bad(s, "%s out of the arena %s" % [x.name, x.pos])
			if x.focus < -1e-9 or x.focus > 100.0 + 1e-9 or x.water_carried < -1e-9 or x.water_carried > 6.0 + 1e-6:
				_bad(s, "%s resources focus %.3f water %.3f" % [x.name, x.focus, x.water_carried])
			var it := s.h.it(x)
			var holding := it.attack_held or it.guard_held or it.tech_held or it.evade_held
			s.busy[x.id] = int(s.busy.get(x.id, 0)) + 1 if (x.action != null and not holding) else 0
			if int(s.busy[x.id]) == 300:
				_bad(s, "%s stuck in %s (%s)" % [x.name, x.action.id, x.action.phase_name()])
		if k % 60 == 59:
			var now := snap(w)
			for key in s.base:
				var d := absf(float(now[key]) - float(s.base[key]))
				s.worst[key] = maxf(float(s.worst.get(key, 0.0)), d)
	return s


func _hash(w: CombatWorld) -> int:
	var parts: Array = [w.tick, w.ledger.duplicate(), w.mass_ledger.duplicate()]
	for x in w.actors:
		parts.append([x.pos, x.vel, x.health, x.focus, x.balance, x.water_carried, x.stance, x.status.keys(), x.action.id if x.action else "", x.subs])
	for b in w.bodies:
		if b.alive:
			parts.append([b.id, b.mat, b.form, b.tag, b.mass, b.temp, b.liquid, b.pos, b.vel, b.tier, b.zone_radius, b.heat_payload])
	return hash(parts)


func _check_soak(s: Soak, label: String) -> void:
	check(s.bad.is_empty(), "%s violations: %s" % [label, "; ".join(s.bad)])
	near(float(s.worst.get("energy", 0.0)), 0.0, 1e-3, "%s energy ledger" % label)
	for key in ["water", "earth", "metal", "plant"]:
		near(float(s.worst.get(key, 0.0)), 0.0, 1e-5, "%s %s mass ledger" % [label, key])
	check(s.max_bodies <= Sim.MAX_BODIES + 2, "%s body count bounded (%d)" % [label, s.max_bodies])
	for fk in s.fx_keys:
		var parts: PackedStringArray = String(fk).split("/")
		check(FxEvents.is_known("fx", parts[0]) and FxEvents.is_known("mat", parts[1]) and (FxEvents.is_known("shape", parts[2]) or MODE_SHAPES.has(parts[2])),
			"%s catalogued fx key %s" % [label, fk])
	for o in s.outcomes:
		check(FxEvents.is_known("outcome", String(o)), "%s catalogued outcome %s" % [label, o])


func test_sixty_second_water_mirror_match_keeps_every_ledger_exact() -> void:
	var holder := SimHarness.new(1)
	var s := _run(41, SOAK_TICKS, Sim.Element.WATER)
	_check_soak(s, "mirror")
	check(s.moves.size() >= 14, "the soak played many different moves (%d: %s)" % [s.moves.size(), s.moves.keys()])
	note("mirror: moves %d, events: fx %d interaction %d zone %d status %d; max bodies %d; worst %s" % [s.moves.size(), int(s.counts.get("fx", 0)),
		int(s.counts.get("interaction", 0)), int(s.counts.get("zone", 0)), int(s.counts.get("status", 0)), s.max_bodies, s.worst])
	note("outcomes seen: %s" % [s.outcomes.keys()])
	holder = null


func test_sixty_second_exchanges_against_the_other_elements_keep_every_ledger_exact() -> void:
	for el in [Sim.Element.EARTH, Sim.Element.FIRE, Sim.Element.AIR]:
		var s := _run(50 + el, SOAK_TICKS, el)
		_check_soak(s, "vs %s" % Sim.ELEMENT_NAMES[el])
		note("vs %s: moves %d, outcomes %s, worst %s" % [Sim.ELEMENT_NAMES[el], s.moves.size(), s.outcomes.keys(), s.worst])


func test_water_kit_is_deterministic_for_a_seed() -> void:
	var a := _run(5, 1200, Sim.Element.WATER)
	var b := _run(5, 1200, Sim.Element.WATER)
	var c := _run(6, 1200, Sim.Element.WATER)
	check(_hash(a.h.w) == _hash(b.h.w), "same seed: same state hash")
	check(_hash(a.h.w) != _hash(c.h.w), "a different seed plays differently")
