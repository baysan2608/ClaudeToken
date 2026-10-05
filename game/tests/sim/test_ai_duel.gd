extends TestCase
## 120 s headless AI-vs-AI duel across all four elements and sub-elements (master vs adept presets):
## the planner drives every kit through the real intent path; the run must keep the ledgers balanced,
## stay finite, leave no fighter stuck in an action or a hold, and actually exercise many moves.

const DUEL_TICKS := 7200          # 120 s
const STUCK_TICKS := 300          # 5 s busy without holding a button
const HOLD_TICKS := 600           # 10 s holding the same button
const MASS_TOL := 1e-3

static var _cache := {}


class Duel:
	var h: SimHarness
	var brains := {}
	var moves := {}
	var elements := {}
	var subs := {}
	var events := {}
	var outcomes := {}
	var violations := {}
	var worst := {"energy": 0.0, "earth": 0.0, "water": 0.0, "metal": 0.0, "plant": 0.0}
	var longest_busy := {}
	var longest_hold := {}
	var e0 := 0.0
	var m0 := {}
	var hits := {}
	var decisions := 0

	func note(cat: String, msg: String) -> void:
		if not violations.has(cat):
			violations[cat] = []
		if (violations[cat] as Array).size() < 5:
			violations[cat].append(msg)


func _run(seed_value: int) -> Duel:
	if _cache.has(seed_value):
		return _cache[seed_value]
	var d := Duel.new()
	var h := SimHarness.new(seed_value)
	d.h = h
	var a := h.actor("A", Vector3(0, 0, 7), 0, {}, Sim.Element.EARTH)
	var b := h.actor("B", Vector3(0, 0, -7), 1, {}, Sim.Element.FIRE)
	var ba := AiBrain.new(h.w, a, {}, seed_value * 2 + 1)
	var bb := AiBrain.new(h.w, b, {}, seed_value * 2 + 2)
	ba.configure({"preset": "master", "elements": [0, 1, 2, 3]})
	bb.configure({"preset": "adept", "elements": [0, 1, 2, 3], "subs": {0: [0, 1, 2, 3], 1: [0, 1, 2, 3], 2: [0, 1, 2, 3], 3: [0, 1, 2, 3]}})
	d.brains = {a.id: ba, b.id: bb}
	var w := h.w
	d.e0 = w.system_energy() - w.ledger_balance()
	d.m0 = {"earth": w.earth_mass(), "water": w.water_mass(), "metal": w.metal_mass(), "plant": w.plant_mass()}
	var busy := {a.id: 0, b.id: 0}
	var held := {a.id: [0, ""], b.id: [0, ""]}
	for x in [a, b]:
		d.longest_busy[x.id] = 0
		d.longest_hold[x.id] = 0
		d.hits[x.id] = 0
	var last_plan := {a.id: "", b.id: ""}
	for k in DUEL_TICKS:
		for x in [a, b]:
			var br: AiBrain = d.brains[x.id]
			h.intents[x.id] = br.think(Sim.DT)
			var lp := String(br.last_plan.get("key", ""))
			if lp != "" and lp != last_plan[x.id]:
				last_plan[x.id] = lp
				d.decisions += 1
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			d.events[e.type] = int(d.events.get(e.type, 0)) + 1
			if e.type == "action" and e.phase == "startup":
				d.moves[e.move] = int(d.moves.get(e.move, 0)) + 1
			elif e.type == "element":
				d.elements[int(e.element)] = true
				d.subs["%d/%d" % [int(e.element), int(e.get("sub", 0))]] = true
			elif e.type == "interaction":
				d.outcomes[e.outcome] = int(d.outcomes.get(e.outcome, 0)) + 1
			elif e.type == "hit" and d.hits.has(e.actor):
				d.hits[e.actor] += 1
		# Keep both fighters in the fight: health never regenerates and 0 HP actors stop acting.
		for x in [a, b]:
			if x.health < 30.0:
				x.health = 100.0
		# Ledgers.
		var drift := absf(w.system_energy() - w.ledger_balance() - d.e0)
		d.worst.energy = maxf(float(d.worst.energy), drift)
		for m in ["earth", "water", "metal", "plant"]:
			var cur := 0.0
			match m:
				"earth":
					cur = w.earth_mass()
				"water":
					cur = w.water_mass()
				"metal":
					cur = w.metal_mass()
				"plant":
					cur = w.plant_mass()
			d.worst[m] = maxf(float(d.worst[m]), absf(cur - float(d.m0[m])))
		# Finite state, stuck actions, endless holds.
		for x in [a, b]:
			if not (is_finite(x.pos.x) and is_finite(x.pos.y) and is_finite(x.pos.z) and is_finite(x.focus) and is_finite(x.health)):
				d.note("nan_actor", "%s at tick %d" % [x.name, w.tick])
			if absf(x.pos.x) > 17.0 or absf(x.pos.z) > 17.0 or x.pos.y < -2.0:
				d.note("out_of_arena", "%s %s" % [x.name, str(x.pos)])
			var it := h.it(x)
			var holding := it.attack_held or it.guard_held or it.tech_held or it.evade_held
			busy[x.id] = busy[x.id] + 1 if (x.action != null and not holding and x.stun <= 0.0) else 0
			d.longest_busy[x.id] = maxi(d.longest_busy[x.id], busy[x.id])
			if busy[x.id] == STUCK_TICKS + 1:
				d.note("stuck_action", "%s busy 5 s in %s (%s)" % [x.name, x.action.id, x.action.phase_name()])
			var hk := "%s%s%s%s" % ["A" if it.attack_held else "", "G" if it.guard_held else "", "T" if it.tech_held else "", "E" if it.evade_held else ""]
			if hk != "" and hk == String(held[x.id][1]):
				held[x.id][0] = int(held[x.id][0]) + 1
			else:
				held[x.id] = [1 if hk != "" else 0, hk]
			d.longest_hold[x.id] = maxi(d.longest_hold[x.id], int(held[x.id][0]))
			if int(held[x.id][0]) == HOLD_TICKS + 1:
				d.note("endless_hold", "%s holds %s for 10 s (%s)" % [x.name, hk, (d.brains[x.id] as AiBrain).debug_state])
		for body in w.bodies:
			if body.alive and not (is_finite(body.pos.x) and is_finite(body.mass) and is_finite(body.temp)):
				d.note("nan_body", body.describe())
	_cache[seed_value] = d
	return d


func _report(d: Duel, cats: Array) -> void:
	for c in cats:
		var list: Array = d.violations.get(c, [])
		check(list.is_empty(), "%s: %s" % [c, "; ".join(list)])


func test_duel_runs_two_minutes_and_exercises_the_kits() -> void:
	var d := _run(31)
	check(d.h.w.tick == DUEL_TICKS, "ran %d ticks" % d.h.w.tick)
	note("moves used (%d): %s" % [d.moves.size(), str(d.moves)])
	note("elements %s, subs %d, outcomes %s, hits %s, counter decisions %d, chains %d, weaves %d, morphs %d" % [str(d.elements.keys()),
		d.subs.size(), str(d.outcomes), str(d.hits), d.decisions, int(d.events.get("chain", 0)), int(d.events.get("weave", 0)), int(d.events.get("morph", 0))])
	check(d.moves.size() >= 20, "a broad set of moves was used (%d distinct)" % d.moves.size())
	check(d.elements.size() >= 4, "all four elements were selected (%s)" % str(d.elements.keys()))
	check(d.subs.size() >= 10, "many sub-elements were selected (%d)" % d.subs.size())
	check(int(d.events.get("interaction", 0)) > 20, "counters met threats (%d interactions)" % int(d.events.get("interaction", 0)))
	check(d.decisions >= 20, "both brains decided on many threats (%d)" % d.decisions)
	check(int(d.hits.values()[0]) + int(d.hits.values()[1]) >= 5, "attacks landed (%s)" % str(d.hits))


func test_duel_keeps_the_ledgers_balanced() -> void:
	var d := _run(31)
	var w := d.h.w
	note("worst drift: %s" % str(d.worst))
	check(float(d.worst.energy) <= 0.05 * maxf(1.0, w.ledger.generated + w.ledger.ambient), "energy ledger drift %.4f HU (generated %.0f)" % [d.worst.energy, w.ledger.generated])
	check(float(d.worst.earth) <= MASS_TOL * maxf(100.0, w.mass_ledger.ground_taken), "earth mass drift %.6f kg of %.0f handled" % [d.worst.earth, w.mass_ledger.ground_taken])
	check(float(d.worst.water) <= MASS_TOL * maxf(100.0, float(d.m0.water)), "water mass drift %.6f kg" % d.worst.water)
	check(float(d.worst.metal) <= MASS_TOL * 100.0, "metal mass drift %.6f kg" % d.worst.metal)
	check(float(d.worst.plant) <= MASS_TOL * 100.0, "plant mass drift %.6f kg" % d.worst.plant)


func test_duel_has_no_stuck_actions_or_endless_holds() -> void:
	var d := _run(31)
	_report(d, ["stuck_action", "endless_hold", "nan_actor", "nan_body", "out_of_arena"])
	note("longest busy %s, longest hold %s" % [str(d.longest_busy), str(d.longest_hold)])
