extends SceneTree
## Sim step timing soak (headless): tools/scripts/godot.sh --headless -s res://tests/sim/perf_core.gd [-- seconds]
## Two fighters with random input over the legacy kits and the test kit of test_core_verbs (every verb),
## both at 60 Hz for N seconds (default 120). Prints mean / p95 / p99 / max step time in ms.

func _init() -> void:
	var secs := 120
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		secs = int(args[0])
	var kit: GDScript = load("res://tests/sim/test_core_verbs.gd")
	Moves.ensure()
	kit.register_test_kit()
	var h := SimHarness.new(42)
	var rng := RandomNumberGenerator.new()
	rng.seed = 4242
	var a := h.actor("A", Vector3(-2, 0, 4), 0, {"magma": true, "heat_draw": true, "lightning": true, "glide": true}, 0)
	var b := h.actor("B", Vector3(2, 0, -4), 1, {"magma": true, "heat_draw": true, "lightning": true, "glide": true}, 2)
	var next := {a.id: 0, b.id: 0}
	var until := {a.id: 0, b.id: 0}
	var times: Array[float] = []
	var ticks := secs * 60
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
				if r < 0.3:
					h.press(x, "attack")
					if rng.randf() < 0.5:
						it.attack_gesture = 1 + rng.randi() % 3
					hold = rng.randi() % 120
				elif r < 0.45:
					h.press(x, "guard")
					hold = 6 + rng.randi() % 80
				elif r < 0.6:
					h.press(x, "tech")
					hold = 6 + rng.randi() % 120
				elif r < 0.7:
					h.press(x, "evade")
					it.evade_held = rng.randf() < 0.5
					it.move = Vector3(rng.randf() - 0.5, 0, rng.randf() - 0.5)
					hold = rng.randi() % 50
				elif r < 0.8:
					h.element(x, rng.randi() % 4)
				elif r < 0.9:
					h.sub(x, rng.randi() % 4)
				else:
					var ang := rng.randf() * TAU
					it.move = Vector3(sin(ang), 0, cos(ang))
				until[x.id] = h.w.tick + hold
			if x.health < 25.0:
				x.health = Sim.HEALTH_MAX
		var t0 := Time.get_ticks_usec()
		h.w.step(h.intents)
		times.append(float(Time.get_ticks_usec() - t0) / 1000.0)
		h.w.take_events()
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
	var sorted := times.duplicate()
	sorted.sort()
	var mean := 0.0
	for t in times:
		mean += t
	mean /= times.size()
	print("sim step over %d ticks (%d s): mean %.3f ms  p95 %.3f ms  p99 %.3f ms  max %.3f ms  bodies %d" % [
		ticks, secs, mean, sorted[int(sorted.size() * 0.95)], sorted[int(sorted.size() * 0.99)], sorted[-1], h.w.alive_count()])
	quit(0)
