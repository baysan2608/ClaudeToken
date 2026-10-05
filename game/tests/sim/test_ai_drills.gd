extends TestCase
## Config API (AiBrain.configure / PRESETS / describe) and the Lab drills: element:<e>/<sub>, matrix,
## and the legacy drills through configure().


func _brain(opts: Dictionary, seed_value: int = 4, el: int = Sim.Element.EARTH) -> Dictionary:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", Vector3(0, 0, 5), 0, {}, Sim.Element.EARTH)
	var o := h.actor("opponent", Vector3(0, 0, -5), 1, {}, el)
	var ai := AiBrain.new(h.w, o, {}, seed_value)
	ai.configure(opts)
	return {"h": h, "p": p, "o": o, "ai": ai}


func _play(d: Dictionary, secs: float) -> Array:
	var h: SimHarness = d.h
	var o: ActorState = d.o
	var p: ActorState = d.p
	var ai: AiBrain = d.ai
	var acts: Array = []
	for k in int(secs * 60):
		h.intents[o.id] = ai.think(Sim.DT)
		var n0 := h.log.size()
		h.step()
		p.health = 100.0
		o.health = 100.0
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "action" and e.actor == o.id and e.phase == "startup":
				acts.append(e)
	return acts


func test_presets_and_kit_breadth() -> void:
	check(AiBrain.preset_names() == ["novice", "adept", "master"], "three presets")
	check(AiBrain.PRESETS.has("master") and float(AiBrain.PRESETS.master.reaction) < float(AiBrain.PRESETS.novice.reaction), "preset table exposed")
	var n := _brain({"preset": "novice"}, 4, Sim.Element.FIRE)
	var nd: Dictionary = (n.ai as AiBrain).describe()
	check(nd.planner and nd.preset == "novice", "novice configured (%s)" % str(nd))
	check((nd.elements as Array).size() == 1 and int(nd.elements[0]) == Sim.Element.FIRE, "novice: one element, its own (%s)" % str(nd.elements))
	check(((nd.subs as Dictionary)[Sim.Element.FIRE] as Array).size() == 2, "novice: two sub-elements")
	check(is_equal_approx(float(nd.reaction), 0.45) and is_equal_approx(float(nd.counter), 0.3), "novice numbers (MOVESET §13)")
	var a := _brain({"preset": "adept"})
	var ad: Dictionary = (a.ai as AiBrain).describe()
	check((ad.elements as Array).size() == 2, "adept: two elements (%s)" % str(ad.elements))
	var m := _brain({"preset": "master"})
	var md: Dictionary = (m.ai as AiBrain).describe()
	check((md.elements as Array).size() == 4, "master: four elements (%s)" % str(md.elements))
	check((m.o as ActorState).elements == [true, true, true, true], "the master's fighter has every element unlocked")
	var x := _brain({"preset": "master", "elements": [1, 3], "subs": {1: [1, 2], 3: [3]}, "reaction": 0.33})
	var xd: Dictionary = (x.ai as AiBrain).describe()
	check(xd.elements == [1, 3] and xd.subs[1] == [1, 2] and xd.subs[3] == [3], "explicit kit (%s)" % str(xd))
	check(is_equal_approx(float(xd.reaction), 0.33), "a single number can be overridden")
	check((x.o as ActorState).elements == [false, true, false, true], "the fighter carries exactly the configured elements")


func test_element_drill_spams_that_kit() -> void:
	var d := _brain({"preset": "adept", "drill": "element:fire/blue", "interval": 1.2})
	var acts := _play(d, 20.0)
	var ids := {}
	var n := 0
	for e in acts:
		var def: Dictionary = Moves.DEFS.get(e.move, {})
		if int(e.sub) == 1 and int(def.get("element", -1)) == Sim.Element.FIRE:
			n += 1
			ids[e.move] = true
	note("fire/blue drill: %d launches, moves %s" % [n, str(ids.keys())])
	check(n >= 8, "the drill launches Fire/Blue threats on its rhythm (%d)" % n)
	check(ids.size() >= 3, "several of the kit's moves (%s)" % str(ids.keys()))
	check(AiPresets.parse_element_drill("element:2/1") == [2, 1] and AiPresets.parse_element_drill("element:air/sound") == [3, 3], "drill specs parse")


func test_matrix_drill_cycles_threat_classes() -> void:
	var d := _brain({"preset": "master", "drill": "matrix", "interval": 1.0})
	var acts := _play(d, 40.0)
	var classes := {}
	for e in acts:
		var cls := String(Moves.DEFS.get(e.move, {}).get("threat", {}).get("cls", ""))
		if cls != "":
			classes[cls] = true
	note("matrix drill threat classes: %s" % str(classes.keys()))
	check(classes.size() >= 10, "the matrix drill cycles through many threat classes (%d)" % classes.size())


func test_legacy_drills_through_configure() -> void:
	var d := _brain({"preset": "adept", "elements": [0], "drill": "stone_rain", "interval": 1.5})
	var acts := _play(d, 12.0)
	var throws := 0
	for e in acts:
		if e.move == "earth_attack":
			throws += 1
	check(throws >= 5, "stone rain still throws stones on its rhythm (%d)" % throws)
