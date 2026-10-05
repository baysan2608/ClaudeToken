extends FireKitTest
## 60 s scripted exchanges: a Fire fighter (all four sub-elements, random input like a player: taps, holds to every
## tier, gestures, guards with push / sink, techniques, evades) against a rival of each element. The energy ledger and
## every mass ledger stay exact every tick, the body cap holds, every fx key is catalogued, nobody gets stuck; the same
## seed plays the same way (docs/MOVESET.md §15.9).

const SOAK_TICKS := 3600


class Soak:
	var h: SimHarness
	var rng := RandomNumberGenerator.new()
	var next := {}
	var hold_until := {}
	var bad: Array[String] = []
	var max_bodies := 0
	var moves := {}


func _input(s: Soak, x: ActorState, other: ActorState) -> void:
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
	if s.rng.randf() < 0.5:
		hh.aim(x, other.pos - x.pos)
	var r := s.rng.randf()
	var hold := 0
	if r < 0.34:
		hh.press(x, "attack")
		if s.rng.randf() < 0.55:
			it.attack_gesture = 1 + s.rng.randi() % 3
		hold = 0 if s.rng.randf() < 0.3 else 5 + s.rng.randi() % 140
	elif r < 0.48:
		hh.press(x, "guard")
		hold = 6 + s.rng.randi() % 90
	elif r < 0.56 and x.action != null and x.action.id == "guard":
		it.guard_gesture = 1 + s.rng.randi() % 2
		it.guard_held = true
		hold = 4 + s.rng.randi() % 30
	elif r < 0.68:
		hh.press(x, "tech")
		hold = 6 + s.rng.randi() % 120
	elif r < 0.77:
		hh.press(x, "evade")
		it.evade_held = s.rng.randf() < 0.5
		it.move = Vector3(s.rng.randf() - 0.5, 0, s.rng.randf() - 0.5)
		hold = 2 + s.rng.randi() % 60
	elif r < 0.86 and x.element == Sim.Element.FIRE:
		hh.sub(x, s.rng.randi() % 4)
	else:
		var a := s.rng.randf() * TAU
		it.move = Vector3(sin(a), 0, cos(a)) * s.rng.randf()
	s.hold_until[x.id] = t + hold


func _run(seed_value: int, ticks: int, rival_element: int) -> Soak:
	var s := Soak.new()
	s.h = SimHarness.new(seed_value)
	s.rng.seed = seed_value * 977 + rival_element
	var a := s.h.actor("F", Vector3(-1, 0, 5), 0, {"magma": true, "heat_draw": true}, Sim.Element.FIRE)
	var b := s.h.actor("R", Vector3(1, 0, -4), 1, {"magma": true, "heat_draw": true, "lightning": true}, rival_element)
	var base := snap(s.h.w)
	for k in ticks:
		_input(s, a, b)
		_input(s, b, a)
		a.health = maxf(a.health, 30.0)
		b.health = maxf(b.health, 30.0)
		s.h.step()
		for e in s.h.log:
			if e.type == "action" and e.get("actor", -1) == a.id:
				s.moves[e.move] = true
		s.max_bodies = maxi(s.max_bodies, s.h.w.alive_count())
		if k % 6 == 0:
			var now := snap(s.h.w)
			for key in base:
				var tol := 1e-3 if key == "energy" else 1e-6
				if absf(float(now[key]) - float(base[key])) > tol and s.bad.size() < 6:
					s.bad.append("t%d %s %.5f -> %.5f" % [s.h.w.tick, key, base[key], now[key]])
					base[key] = now[key]
		if k % 600 == 599:
			s.h.log.clear()
	return s


func test_sixty_second_exchanges_keep_every_ledger_exact() -> void:
	var names := ["Earth", "Water", "Fire", "Air"]
	var all_moves := {}
	for el in 4:
		var s := _run(11 + el, SOAK_TICKS, el)
		h = s.h
		check(s.bad.is_empty(), "vs %s: ledger drift %s" % [names[el], s.bad])
		check(s.max_bodies <= Sim.MAX_BODIES, "vs %s: body cap holds (%d)" % [names[el], s.max_bodies])
		fx_catalogued("vs %s" % names[el])
		for m in s.moves:
			all_moves[m] = true
	var fire_moves := 0
	for sub in 4:
		for id in KitFire.move_ids(sub):
			if all_moves.has(id):
				fire_moves += 1
	check(fire_moves >= 25, "the soak played most Fire moves (%d)" % fire_moves)
	note("moves played: %d Fire kit moves" % fire_moves)


func _hash(s: Soak) -> String:
	var parts := PackedStringArray()
	for a in s.h.w.actors:
		parts.append("%s:%.4f,%.4f,%.4f|%.3f|%.3f|%.3f" % [a.name, a.pos.x, a.pos.y, a.pos.z, a.health, a.focus, a.heat_reserve])
	for b in s.h.w.bodies:
		if b.alive:
			parts.append("%d:%d:%s:%.4f:%.3f:%.3f" % [b.id, b.mat, b.tag, b.mass, b.pos.x, b.heat_payload])
	return ";".join(parts)


func test_fire_kit_is_deterministic_for_a_seed() -> void:
	var a := _run(42, 900, Sim.Element.WATER)
	var b := _run(42, 900, Sim.Element.WATER)
	check(_hash(a) == _hash(b), "same seed, same world after 15 s")
	var c := _run(43, 900, Sim.Element.WATER)
	check(_hash(a) != _hash(c), "another seed plays differently")
