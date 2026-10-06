extends TestCase
## Integration: every element can answer every element (docs/MOVESET.md §8). For each thrown / travelling
## threat of the Lab spawner, a rival who owns one element (all four sub-elements) is asked for its counters
## through the same planner the 1v1 AI uses (AiPlanner.counters -> Interactions.predict). Each element must
## have a non-evade answer with a full or partial band, and the answer must be physical: weak answers to
## heavy / hot threats are partial or fail, never full.

const THREATS := ["stone_20", "stone_45", "hot_rock", "magma_blob", "lava_wave", "metal_disc", "metal_lance", "metal_plate",
	"sand_slug", "sand_surge", "water_blob", "water_stream", "water_wave", "ice_shard", "fireball", "comet", "fire_line",
	"wind_crescent", "tornado", "spike_line", "tremor"]


## {h, p, o, ai}: p throws, o (the AI) owns `element` with all four subs.
func _setup(element: int, sub: int) -> Dictionary:
	var h := SimHarness.new(5)
	var p := h.actor("thrower", Vector3(0, 0, 6.0), 0, {}, Sim.Element.EARTH)
	var o := h.actor("answer", Vector3(0, 0, -6.0), 1, {"heat_draw": true, "magma": true, "redirect_current": true}, element)
	o.subs[element] = sub
	var ai := AiBrain.new(h.w, o, {}, 5)
	ai.configure({"preset": "master", "elements": [element], "subs": {element: [0, 1, 2, 3]}, "drill": "passive",
		"counter": 1.0, "misjudge": 0.0})
	h.step(5)
	return {"h": h, "p": p, "o": o, "ai": ai}


func _answers(element: int, threat_id: String) -> Array:
	var d := _setup(element, 0)
	var h: SimHarness = d.h
	var res := SpawnCatalog.spawn(h.w, threat_id, {}, d.o, d.p, Vector3.ZERO, true)
	if not res.ok:
		return []
	h.step(3)
	var best: Array = []
	for b in res.bodies:
		var mb := b as MatBody
		if mb == null or not mb.alive:
			continue
		var th := AiPlanner.body_threat(h.w, d.o, mb)
		if th.is_empty():
			continue
		var ai: AiBrain = d.ai
		var prm := ai._planner_params()
		prm["only"] = []
		for opt in AiPlanner.counters(h.w, d.o, th, ai.kit, prm, 0.0):
			if String(opt.slot) != "evade":
				best.append(opt)
		break
	return best


func test_every_element_answers_every_thrown_threat() -> void:
	var gaps: Array[String] = []
	for tid in THREATS:
		var row: Array[String] = []
		for e in 4:
			var opts := _answers(e, tid)
			var good := opts.filter(func(o): return String(o.band) == "full" or String(o.band) == "partial")
			good.sort_custom(func(x, y): return float(x.value) > float(y.value))
			if good.is_empty():
				gaps.append("%s vs %s" % [Sim.ELEMENT_NAMES[e], tid])
				row.append("%s: -" % Sim.ELEMENT_NAMES[e])
			else:
				var g: Dictionary = good[0]
				row.append("%s: %s %s/%s r%.2f" % [Sim.ELEMENT_NAMES[e], g.label, g.outcome, g.band, float(g.ratio)])
		note("%-13s %s" % [tid, " | ".join(row)])
	check(gaps.is_empty(), "every element has a full/partial answer: missing %s" % [gaps])


func _thrown_agent(d: Dictionary, threat_id: String, params: Dictionary = {}) -> Agent:
	var h: SimHarness = d.h
	var res := SpawnCatalog.spawn(h.w, threat_id, params, d.o, d.p, Vector3.ZERO, true)
	h.step(3)
	for b in res.bodies:
		var th := AiPlanner.body_threat(h.w, d.o, b)
		if not th.is_empty():
			return th.agent
	return null


func test_counter_strength_scales_with_the_threat() -> void:
	# Lava (a molten blob) vs wind: a palm gust (T0) only bends it; the T3 Hurricane sets it to rock.
	var d := _setup(Sim.Element.AIR, 0)
	var ag := _thrown_agent(d, "magma_blob")
	check(ag != null, "magma blob is a threat")
	if ag != null:
		var t0 := Interactions.predict(d.h.w, ag, Agent.of_move(d.h.w, d.o, "air_attack", 0, false))
		var t3 := Interactions.predict(d.h.w, ag, Agent.of_move(d.h.w, d.o, "air_attack", 3, false))
		check(String(t0.outcome) != "redirect" and String(t0.outcome) != "transform", "a palm gust does not stop lava (%s)" % t0.outcome)
		check(String(t3.band) != "fail" and float(t3.ratio) > float(t0.ratio) * 3.0 and String(t3.rule_id).begins_with("air_"),
			"a hurricane palm (T3) cools / weakens the 25 kg blob (%s/%s r%.2f vs T0 r%.2f)" % [t3.outcome, t3.band, t3.ratio, t0.ratio])
	# A lighter splash of lava is set to rock outright by the same hurricane.
	var dl := _setup(Sim.Element.AIR, 0)
	var lt := _thrown_agent(dl, "magma_blob", {"mass": 8.0, "speed": 9.0})
	if check(lt != null, "light blob is a threat"):
		var t3l := Interactions.predict(dl.h.w, lt, Agent.of_move(dl.h.w, dl.o, "air_attack", 3, false))
		check(String(t3l.outcome) == "transform" and String(t3l.to) == "rock", "hurricane sets 8 kg of lava to rock (%s/%s r%.2f)" % [t3l.outcome, t3l.band, t3l.ratio])
	# A palm gust (7 x2 = 14) only bends a 20 kg thrown stone (TP 17, MOVESET §5.4); the cyclone push (11 x2 = 22)
	# sends it back.
	var d2 := _setup(Sim.Element.AIR, 0)
	var st := _thrown_agent(d2, "stone_20")
	if check(st != null, "stone is a threat"):
		var r0 := Interactions.predict(d2.h.w, st, Agent.of_move(d2.h.w, d2.o, "air_attack", 0, false))
		var r1c := Interactions.predict(d2.h.w, st, Agent.of_move(d2.h.w, d2.o, "air_attack", 1, false))
		check(String(r0.outcome) != "redirect" and String(r1c.outcome) == "redirect",
			"palm gust bends a 20 kg stone (%s r%.2f), cyclone sends it back (%s r%.2f)" % [r0.outcome, r0.ratio, r1c.outcome, r1c.ratio])
	# A heavier stone needs more: 80 kg is only bent by the same gust.
	var d3 := _setup(Sim.Element.AIR, 0)
	var hv := _thrown_agent(d3, "stone_80")
	if check(hv != null, "80 kg stone is a threat"):
		var r1 := Interactions.predict(d3.h.w, hv, Agent.of_move(d3.h.w, d3.o, "air_attack", 0, false))
		check(String(r1.outcome) != "redirect", "an 80 kg stone is not sent back by a palm gust (%s)" % r1.outcome)
