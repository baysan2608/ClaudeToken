class_name Scenarios
extends RefCounted
## Resettable lab scenarios. Practice scenarios grant a temporary "practice kit"
## so every interaction can be tried; Free Spar uses the techniques the player
## has actually unlocked through mastery challenges (Progression).

const LIST: Array[Dictionary] = [
	{"id": "molten_exchange", "group": "Flagship", "title": "Molten Exchange",
		"subtitle": "Catch a thrown stone with Fire, melt it, pour lava. Your rival can draw the heat back out.",
		"objective": "FIRE technique on the incoming stone (hold) → release to pour lava",
		"player": {"element": 2, "kit": {"magma": true, "heat_draw": true}},
		"opponent": {"kit": {"heat_draw": true}, "elements": [0, 2], "ai": {"drill": "stone_rain", "interval": 3.4, "counter": 0.85, "aggression": 0.5}}},
	{"id": "lab", "group": "Lab", "title": "Lab",
		"subtitle": "Every element and sub-element, dummies and a rival who throws what you spawn. Dev panel: ` / F2, or Dev in the pause menu.",
		"objective": "LAB: all 16 sub-elements. ` or F2 = dev panel (spawner, move list, combos, matrix, tuning)",
		"player": {"element": 0, "kit": "all", "pos": Vector3(0, 0, 8)},
		"opponent": {"kit": "all", "elements": [0, 1, 2, 3], "pos": Vector3(0, 0, -7), "ai": {"drill": "passive", "aggression": 0.5, "counter": 0.7},
			"ai_default": false},
		"dummies": [Vector3(-5, 0, -4), Vector3(5, 0, -4), Vector3(-2.5, 0, -10)],
		"lab": true},
	{"id": "spar", "group": "Spar", "title": "Free Spar",
		"subtitle": "Full sparring partner. Pick the rival's difficulty and kit below; your techniques are the ones you've mastered.",
		"objective": "Spar. Your kit = techniques you've mastered.",
		"player": {"element": 0, "kit": "progress"},
		"opponent": {"kit": {"heat_draw": true}, "elements": [0, 2], "ai": {"aggression": 0.6, "counter": 0.7}},
		"spar": true},
	{"id": "stone_rain", "group": "Drills", "title": "Stone Rain",
		"subtitle": "Stones on a rhythm: sidestep, guard, time an Earth guard to send them back.",
		"objective": "EVADE + direction, hold GUARD, or tap GUARD just before impact (Earth) to redirect",
		"player": {"element": 0, "kit": {"magma": true}},
		"opponent": {"kit": {}, "elements": [0], "ai": {"drill": "stone_rain", "interval": 2.4, "counter": 0.3}},
		"challenge": {"id": "m_stone_reader", "title": "Stone Reader", "text": "Redirect 3 stones with a timed Earth guard", "count": 3, "unlock": "magma"}},
	{"id": "boulder", "group": "Drills", "title": "Boulder Launcher",
		"subtitle": "A training launcher fires 20, 45 and 200 kg stones. Not everything can be caught or melted.",
		"objective": "Try FIRE technique on each: the 200 kg boulder can't be controlled",
		"player": {"element": 2, "kit": {"magma": true, "heat_draw": true}},
		"launcher": {"pos": Vector3(0, 1.5, -9), "masses": [20.0, 45.0, 200.0], "interval": 3.2}},
	{"id": "lava_paths", "group": "Elements", "title": "Lava Paths",
		"subtitle": "Pour lava off the terrace edge, into the cover wall, or into the pool.",
		"objective": "FIRE technique on a stone, hold until molten, drag to aim, release",
		"player": {"element": 2, "kit": {"magma": true, "heat_draw": true}, "pos": Vector3(0, 0.6, 12)},
		"stones": [Vector3(1.2, 0.6, 11.0), Vector3(-1.2, 0.6, 11.0), Vector3(2.5, 0.6, 12.5)],
		"dummies": [Vector3(0, 0, 4), Vector3(-3.5, 0, -3.0)]},
	{"id": "contest", "group": "Elements", "title": "Tug of Stone",
		"subtitle": "Both of you try to seize the same stones. Distance and timing decide who holds them.",
		"objective": "EARTH technique on the middle stones before your rival does",
		"player": {"element": 0, "kit": {}},
		"stones": [Vector3(0, 0, 0), Vector3(1.5, 0, 0.5), Vector3(-1.5, 0, -0.5)],
		"opponent": {"kit": {}, "elements": [0], "ai": {"drill": "seize", "interval": 1.6, "counter": 0.5}}},
	{"id": "water_ice", "group": "Elements", "title": "Water & Ice",
		"subtitle": "Draw from the pool, lash, freeze a lance, melt ice with fire, boil a shield into steam.",
		"objective": "WATER technique near the pool; hold ATTACK for an ice lance; FIRE melts ice",
		"player": {"element": 1, "kit": {}, "pos": Vector3(5.0, 0, 0)},
		"dummies": [Vector3(4.0, 0, -7), Vector3(0, 0, -5)]},
	{"id": "conduction", "group": "Elements", "title": "Conductors",
		"subtitle": "Lightning follows water and metal only. The pool and the plate are not connected… unless water bridges them.",
		"objective": "FIRE: hold ATTACK until it crackles, release at a target standing in water",
		"player": {"element": 2, "kit": {"lightning": true}, "pos": Vector3(0, 0, 3)},
		"dummies": [Vector3(9, -0.3, -1), Vector3(11, -0.3, 1), Vector3(-9, 0.02, -1), Vector3(-4.0, 0, -6), Vector3(-5.0, 0, 2.6)],
		"puddles": [{"pos": Vector3(-5.2, 0, 2.4), "mass": 14.0}],
		"challenge": {"id": "m_storm_eye", "title": "Storm's Path", "text": "Hit 2 targets with one bolt through water", "count": 1, "unlock": "lightning"}},
	{"id": "redirect", "group": "Elements", "title": "Return the Current",
		"subtitle": "Your rival charges lightning. Fire GUARD at the last instant returns it. Or break line of sight.",
		"objective": "FIRE element, tap GUARD just as the bolt fires",
		"player": {"element": 2, "kit": {"redirect_current": true}},
		"opponent": {"kit": {"lightning": true}, "elements": [2], "ai": {"drill": "lightning", "interval": 3.0, "counter": 0.0}},
		"challenge": {"id": "m_return", "title": "Return the Current", "text": "Redirect a lightning strike", "count": 1, "unlock": "redirect_current"}},
	{"id": "cold_hands", "group": "Mastery", "title": "Cold Hands",
		"subtitle": "Lava pools in the courtyard. Draw their heat until they set into rock; vent when full.",
		"objective": "FIRE technique on lava (DRAW). Technique with no target + full reserve = VENT",
		"player": {"element": 2, "kit": {"heat_draw": true}},
		"lava": [Vector3(-3, 0, 0), Vector3(3, 0, -2), Vector3(0, 0, -5)],
		"challenge": {"id": "m_cold_hands", "title": "Cold Hands", "text": "Set 3 lava pools into rock", "count": 3, "unlock": "heat_draw"}},
	{"id": "updraft", "group": "Mastery", "title": "Updraft Ledge",
		"subtitle": "The high ledge in the corner is out of reach on foot.",
		"objective": "AIR technique: updraft onto the high ledge",
		"player": {"element": 3, "kit": {}, "pos": Vector3(-8, 0, -8)},
		"challenge": {"id": "m_updraft", "title": "Updraft", "text": "Stand on the high ledge", "count": 1, "unlock": "glide"}},
]


## Free Spar rival choices (docs/CONTROLS.md, docs/AI.md): AiPresets names and the kit.
const SPAR_DIFFICULTIES: Array[String] = ["novice", "adept", "master"]
const SPAR_DIFFICULTY_LABELS: Array[String] = ["Easy", "Normal", "Hard"]
## "mixed" = the classic earth + fire rival; an element name = that element, every sub-element;
## "<element>/<sub>" = one sub-element; "all" = all four.
const SPAR_KITS: Array[String] = ["mixed", "earth", "water", "fire", "air", "all"]
const SPAR_KIT_LABELS: Array[String] = ["Earth + Fire", "Earth", "Water", "Fire", "Air", "All four"]
const ELEMENT_KEYS: Array[String] = ["earth", "water", "fire", "air"]


static func get_def(id: String) -> Dictionary:
	for s in LIST:
		if s.id == id:
			return s
	return LIST[0]


static func practice_items(progress: Progression) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for s in LIST:
		var sub: String = s.subtitle
		var ch: Dictionary = s.get("challenge", {})
		if not ch.is_empty():
			var done := progress.is_done(ch.id)
			sub = ("✓ " if done else "◇ ") + ch.text + " → unlocks " + Moves.TECHNIQUES.get(ch.unlock, ch.unlock).split(":")[0] + ". " + sub
		var item := {"id": s.id, "title": "%s · %s" % [s.group, s.title], "subtitle": sub, "locked": false}
		if s.get("spar", false):
			item["options"] = [
				{"key": "spar_difficulty", "label": "Rival", "values": SPAR_DIFFICULTIES, "labels": SPAR_DIFFICULTY_LABELS, "value": progress.spar_difficulty},
				{"key": "spar_kit", "label": "Kit", "values": SPAR_KITS, "labels": SPAR_KIT_LABELS, "value": progress.spar_kit},
			]
		out.append(item)
	out.append({"id": "__lab_mode", "title": "Testing · Lab mode %s" % ("ON" if progress.lab_mode else "OFF"),
		"subtitle": "Toggle: every technique unlocked in Free Spar (for testing; progress is kept)", "locked": false})
	return out


## Builds the world for a scenario. Returns {world, player, opponent, ai_cfg, launcher, def}.
static func build(id: String, progress: Progression, seed_value: int = 1) -> Dictionary:
	var d := get_def(id)
	var w := CombatWorld.new(seed_value)
	var pd: Dictionary = d.player
	var kit: Dictionary
	if pd.kit is String and pd.kit == "all":
		kit = Progression.lab_kit()
	elif pd.kit is String:
		kit = progress.kit()
	else:
		kit = (pd.kit as Dictionary).duplicate()
		kit.merge(progress.kit(), false)
	var ppos: Vector3 = pd.get("pos", w.arena.player_spawn)
	var p := w.add_actor("You", ppos, 0, kit, int(pd.element))
	p.facing = PI
	var o: ActorState = null
	var ai_cfg := {}
	if d.has("opponent"):
		var od: Dictionary = d.opponent
		var okit: Dictionary
		if od.kit is String and od.kit == "all":
			okit = {}
			for t in Progression.ALL:
				okit[t] = true
		else:
			okit = od.kit
		var elements: Array = od.elements
		var spar_sub := -1
		if d.get("spar", false):
			var sk := spar_kit(progress.spar_kit)
			elements = sk.elements
			spar_sub = int(sk.sub)
		o = w.add_actor("Rival", od.get("pos", w.arena.opponent_spawn), 1, okit, int(elements[0]))
		o.elements = [false, false, false, false]
		for e in elements:
			o.elements[e] = true
		ai_cfg = (od.ai as Dictionary).duplicate()
		ai_cfg["elements"] = elements
		if d.get("spar", false):
			# New-style rival: AiBrain.configure() reads these (guarded by has_method in Game).
			ai_cfg["preset"] = progress.spar_difficulty
			if spar_sub >= 0:
				ai_cfg["subs"] = {int(elements[0]): [spar_sub]}
			else:
				ai_cfg["subs"] = {}
		if d.get("lab", false):
			ai_cfg["preset"] = "adept"
			ai_cfg["elements"] = elements
	var k := 0
	for dp in d.get("dummies", []):
		var dm := w.add_actor("Target %d" % (k + 1), dp, 1, {}, 0)
		dm.is_dummy = true
		dm.facing = 0.0
		k += 1
	for sp in d.get("stones", []):
		var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, Sim.STONE_SHOT_MASS, sp + Vector3(0, 0.25, 0), "scenario")
		b.on_ground = true
	for pdl in d.get("puddles", []):
		var b2 := w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, float(pdl.mass), pdl.pos, "scenario")
		b2.update_radius_puddle()
	for lp in d.get("lava", []):
		var b3 := w.spawn_body(Sim.Mat.STONE, Sim.Form.BLOB, 25.0, lp + Vector3(0, 0.1, 0), "vent", Sim.STONE_MELT_C)
		b3.liquid = 1.0
		b3.phase = Sim.Phase.MOLTEN
		b3.on_ground = true
		b3.wave_path = PackedVector3Array([lp + Vector3(-0.6, 0, 0), lp + Vector3(0.6, 0, 0.2)])
	w.take_events()
	return {"world": w, "player": p, "opponent": o, "ai_cfg": ai_cfg, "launcher": d.get("launcher", {}), "def": d}


## "mixed" / "earth".."air" / "all" / "fire/blue" -> {elements: Array, sub: int (-1 = every sub-element)}.
static func spar_kit(key: String) -> Dictionary:
	match key:
		"mixed":
			return {"elements": [0, 2], "sub": -1}
		"all":
			return {"elements": [0, 1, 2, 3], "sub": -1}
	var parts := key.split("/")
	var e := ELEMENT_KEYS.find(parts[0])
	if e < 0:
		return {"elements": [0, 2], "sub": -1}
	var sb := -1
	if parts.size() > 1:
		sb = clampi(int(parts[1]), 0, 3) if parts[1].is_valid_int() else Sim.SUB_NAMES[e].find(String(parts[1]).capitalize())
	return {"elements": [e], "sub": sb}
