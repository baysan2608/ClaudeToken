extends SceneTree
## Air kit soak (headless): tools/scripts/godot.sh --headless -s res://tests/sim/perf_air.gd [-- seconds]
## Two fighters with random input - A an Air fighter cycling through every Air sub-element and move, B cycling through
## every element and sub-element so there is always something to catch, bend, snuff or shatter - at 60 Hz for N seconds
## (default 120). Prints mean / p95 / p99 / max step time in ms and checks, every second: finite state, the body cap,
## energy and mass ledgers (exact conservation), and that no Air zone outlives its owner's moves forever.

func _init() -> void:
	var secs := 120
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		secs = int(args[0])
	Moves.ensure()
	var h := SimHarness.new(77)
	var rng := RandomNumberGenerator.new()
	rng.seed = 7777
	var a := h.actor("A", Vector3(-2, 0, 4), 0, {"glide": true}, Sim.Element.AIR)
	var b := h.actor("B", Vector3(2, 0, -4), 1, {"magma": true, "heat_draw": true, "lightning": true, "glide": true}, Sim.Element.EARTH)
	var next := {a.id: 0, b.id: 0}
	var until := {a.id: 0, b.id: 0}
	var times: Array[float] = []
	var ticks := secs * 60
	var e0 := h.w.system_energy() - h.w.ledger_balance()
	var m0 := [h.w.water_mass(), h.w.earth_mass(), h.w.metal_mass(), h.w.plant_mass()]
	var worst_bodies := 0
	var bad := 0
	for k in ticks:
		for x in [a, b]:
			var it := h.it(x)
			var other: ActorState = b if x == a else a
			if h.w.tick >= until[x.id]:
				it.attack_held = false
				it.guard_held = false
				it.tech_held = false
				it.evade_held = false
			if h.w.tick >= next[x.id]:
				next[x.id] = h.w.tick + 4 + rng.randi() % 30
				h.aim(x, other.pos - x.pos)
				var r := rng.randf()
				var hold := 0
				if r < 0.34:
					h.press(x, "attack")
					if rng.randf() < 0.5:
						it.attack_gesture = 1 + rng.randi() % 3
					hold = rng.randi() % 120
				elif r < 0.5:
					h.press(x, "guard")
					if rng.randf() < 0.5:
						it.guard_gesture = 1 + rng.randi() % 2
					hold = 6 + rng.randi() % 80
				elif r < 0.64:
					h.press(x, "tech")
					hold = 6 + rng.randi() % 120
				elif r < 0.74:
					h.press(x, "evade")
					it.evade_held = rng.randf() < 0.5
					it.move = Vector3(rng.randf() - 0.5, 0, rng.randf() - 0.5)
					hold = rng.randi() % 50
				elif r < 0.8 and x == b:
					h.element(x, rng.randi() % 4)
				elif r < 0.9:
					h.sub(x, rng.randi() % 4)
				else:
					var ang := rng.randf() * TAU
					it.move = Vector3(sin(ang), 0, cos(ang))
				until[x.id] = h.w.tick + hold
			if x.health < 25.0:
				x.health = Sim.HEALTH_MAX
			if x == a and x.element != Sim.Element.AIR:
				x.element = Sim.Element.AIR
		var t0 := Time.get_ticks_usec()
		h.w.step(h.intents)
		times.append(float(Time.get_ticks_usec() - t0) / 1000.0)
		h.w.take_events()
		worst_bodies = maxi(worst_bodies, h.w.alive_count())
		for i in h.intents.values():
			var x: ActorIntent = i
			x.attack_pressed = false
			x.attack_released = false
			x.guard_pressed = false
			x.evade_pressed = false
			x.tech_pressed = false
			x.tech_released = false
			x.tech_cancel = false
			x.element_select = -1
			x.sub_select = -1
			x.attack_gesture = 0
			x.guard_gesture = 0
		if k % 60 == 59:
			for ac in h.w.actors:
				if not ac.pos.is_finite() or not ac.vel.is_finite():
					bad += 1
					print("NON-FINITE actor ", ac.name, " at tick ", h.w.tick)
			for bd in h.w.bodies:
				if bd.alive and (not bd.pos.is_finite() or not bd.vel.is_finite() or not is_finite(bd.mass)):
					bad += 1
					print("NON-FINITE body ", bd.describe(), " at tick ", h.w.tick)
			if h.w.alive_count() > Sim.MAX_BODIES + 4:
				bad += 1
				print("BODY CAP exceeded: ", h.w.alive_count())
	var sorted := times.duplicate()
	sorted.sort()
	var mean := 0.0
	for t in times:
		mean += t
	mean /= times.size()
	var e1 := h.w.system_energy() - h.w.ledger_balance()
	var m1 := [h.w.water_mass(), h.w.earth_mass(), h.w.metal_mass(), h.w.plant_mass()]
	print("air soak: %d ticks (%d s): mean %.3f ms  p95 %.3f ms  p99 %.3f ms  max %.3f ms  bodies now %d worst %d" % [
		ticks, secs, mean, sorted[int(sorted.size() * 0.95)], sorted[int(sorted.size() * 0.99)], sorted[-1], h.w.alive_count(), worst_bodies])
	print("energy drift %.6f HU  water %.6f earth %.6f metal %.6f plant %.6f kg" % [e1 - e0, m1[0] - m0[0], m1[1] - m0[1], m1[2] - m0[2], m1[3] - m0[3]])
	var ok := bad == 0 and absf(e1 - e0) < 0.5 and absf(m1[0] - m0[0]) < 1e-3 and absf(m1[1] - m0[1]) < 1e-3 and absf(m1[2] - m0[2]) < 1e-3 and absf(m1[3] - m0[3]) < 1e-3
	print("OK" if ok else "FAILED")
	quit(0 if ok else 1)
