extends TestCase
## Earth kit ledgers and determinism: a scripted 60 s exchange between two Earth fighters cycling
## through every sub-element, slot and tier keeps the ground ledger (stone + sand + glass, with the
## sand <-> glass <-> sandstone conversions), the metal ledger (satchels + field + plate scrap), the
## water ledger and the energy identity exact; the same seed gives the same final hash.

const U := preload("res://tests/sim/test_kit_earth_util.gd")
const STEPS := [
	[0, "strike", 0], [1, "strike", 2], [2, "ground", 1], [3, "strike", 2], [0, "ground", 0], [1, "guard", 0],
	[2, "strike", 3], [3, "ground", 0], [0, "sink", 1], [1, "thrust", 1], [2, "guard", 2], [3, "guard", 0],
	[0, "push", 0], [1, "ground", 0], [2, "sweep", 1], [3, "thrust", 3], [0, "tech", 0], [1, "tech", 0],
	[2, "tech", 0], [3, "tech", 0], [0, "thrust", 2], [1, "sweep", 2], [2, "sink", 1], [3, "sink", 1],
	[0, "sweep", 3], [1, "sink", 0], [2, "push", 1], [3, "push", 0], [0, "evade_hold", 0], [1, "evade", 0],
	[2, "thrust", 2], [3, "sweep", 1], [0, "strike", 3], [1, "push", 0], [2, "evade_hold", 0], [3, "evade_hold", 0],
]


## Two Earth fighters run the schedule (offset) for `seconds`; checks run every second.
func _exchange(seed_value: int, seconds: float, checks: bool) -> String:
	var h := SimHarness.new(seed_value)
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	var b := h.actor("B", Vector3(0.5, 0, -4), 1, {}, Sim.Element.EARTH)
	a.facing = PI
	h.step(5)
	var em0 := h.w.earth_mass()
	var mm0 := h.w.metal_mass()
	var wm0 := h.w.water_mass()
	var e0 := U.e0(h)
	var worst := [0.0, 0.0, 0.0, 0.0]
	var ticks := int(seconds * Sim.HZ)
	var k := 0
	var fighters := [a, b]
	while h.w.tick < ticks:
		for i in 2:
			var f: ActorState = fighters[i]
			f.health = maxf(f.health, 40.0)
			f.focus = maxf(f.focus, 30.0)
			if f.action == null and f.stun <= 0.0:
				var st: Array = STEPS[(k + i * 7) % STEPS.size()]
				h.sub(f, int(st[0]))
				h.step()
				_drive(h, f, String(st[1]), int(st[2]))
		k += 1
		h.step(20)
		var de := absf(h.w.earth_mass() - em0)
		var dm := absf(h.w.metal_mass() - mm0)
		var dw := absf(h.w.water_mass() - wm0)
		var dE := U.energy_drift(h, e0)
		worst = [maxf(worst[0], de), maxf(worst[1], dm), maxf(worst[2], dw), maxf(worst[3], dE)]
	if checks:
		check(worst[0] < 1e-6, "ground ledger (stone + sand + glass) exact over %.0f s (worst %.9f)" % [seconds, worst[0]])
		check(worst[1] < 1e-6, "metal ledger exact (worst %.9f)" % worst[1])
		check(worst[2] < 1e-6, "water ledger exact (worst %.9f)" % worst[2])
		check(worst[3] < 1e-3, "energy identity (worst drift %.6f HU)" % worst[3])
		var conv := float(h.w.mass_ledger.sand_to_glass) + float(h.w.mass_ledger.sand_to_sandstone)
		note("ground taken %.0f kg, returned %.0f kg, sand->glass %.0f, sand->sandstone %.0f, metal taken %.0f, bodies %d, ticks %d" % [
			h.w.mass_ledger.ground_taken, h.w.mass_ledger.ground_returned, h.w.mass_ledger.sand_to_glass,
			h.w.mass_ledger.sand_to_sandstone, h.w.mass_ledger.metal_taken, h.w.alive_count(), h.w.tick])
		check(float(h.w.mass_ledger.ground_taken) > 500.0, "the exchange moved a lot of earth")
		check(conv >= 0.0, "conversions booked")
		var used := {}
		for e in h.log:
			if e.type == "action" and e.phase == "startup":
				used[e.move] = true
		note("moves used: %d" % used.size())
		check(used.size() >= 25, "most of the kit was exercised (%d moves)" % used.size())
	return _hash(h)


func _drive(h: SimHarness, f: ActorState, slot: String, tier: int) -> void:
	var hold: int = U.TIER_HOLD[tier]
	match slot:
		"strike":
			h.press(f, "attack")
			h.step(hold)
			h.release(f, "attack")
		"thrust", "ground", "sweep":
			h.flick(f, "attack", U.SLOT_GESTURE[slot])
			h.step(hold)
			h.release(f, "attack")
		"guard":
			h.press(f, "guard")
			h.step(maxi(hold, 30))
			h.release(f, "guard")
		"push", "sink":
			h.press(f, "guard")
			h.step(maxi(hold, 12))
			h.flick(f, "guard", Sim.Gesture.UP if slot == "push" else Sim.Gesture.DOWN)
			h.step()
			h.release(f, "guard")
		"tech":
			h.press(f, "tech")
			h.step(30)
			h.it(f).attack_pressed = true
			h.step(8)
			h.release(f, "tech")
		"evade":
			h.press(f, "evade")
		"evade_hold":
			h.press(f, "evade")
			h.it(f).evade_held = true
			h.step(30)
			h.it(f).evade_held = false


func _hash(h: SimHarness) -> String:
	var parts: Array[String] = ["t%d" % h.w.tick]
	for a in h.w.actors:
		parts.append("%s %.4f %.4f %.4f %.4f %.4f %.4f" % [a.name, a.pos.x, a.pos.z, a.health, a.focus, a.balance, a.metal_carried])
	for b in h.w.bodies:
		if b.alive:
			parts.append("%d %d %d %s %.4f %.3f %.3f %.3f %.3f" % [b.id, b.mat, b.form, b.tag, b.mass, b.temp, b.pos.x, b.pos.y, b.pos.z])
	return "|".join(parts).sha256_text()


func test_scripted_60_s_exchange_keeps_every_ledger() -> void:
	_exchange(11, 60.0, true)


func test_same_seed_same_hash() -> void:
	var h1 := _exchange(23, 20.0, false)
	var h2 := _exchange(23, 20.0, false)
	check(h1 == h2, "deterministic: same seed, same final state")
