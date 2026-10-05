extends TestCase
## Earth kit: every move of the four sub-elements (MOVESET §7.1-§7.4) at T0..T3 starts, pays its cost,
## ends cleanly and emits only catalogued fx keys; every def carries the §15.2 schema.

const U := preload("res://tests/sim/test_kit_earth_util.gd")
const SUBS := ["Stone", "Metal", "Sand", "Magma"]


func test_every_slot_is_bound_with_a_complete_def() -> void:
	Moves.ensure()
	for s in 4:
		for slot in Sim.SLOTS:
			var id := Moves.resolve(Sim.Element.EARTH, s, slot)
			check(id != "", "%s %s is bound" % [SUBS[s], slot])
			if id == "" or id == "guard" or id == "evade":
				continue
			var d: Dictionary = Moves.DEFS[id]
			if s > 0 or not Moves.BASE_DEFS.has(id):
				check(int(d.get("sub", -1)) == s and String(d.get("slot", "")) == slot, "%s: sub / slot" % id)
			for k in ["name", "desc", "anim", "fx", "ai"]:
				check(d.has(k), "%s has %s" % [id, k])
			check(FxEvents.is_known("mat", String(d.get("fx", {}).get("mat", ""))), "%s fx mat" % id)
			check(FxEvents.is_known("shape", String(d.get("fx", {}).get("shape", ""))), "%s fx shape" % id)
			var ai: Dictionary = d.get("ai", {})
			check(ai.has("role") and ai.has("range") and ai.has("tags"), "%s ai metadata" % id)
			check(["poke", "zone", "counter", "finisher", "mobility", "setup"].has(String(ai.get("role", ""))), "%s ai role" % id)
	# The guard specs of subs 1-3 are real defs (sub 0 keeps the legacy Bulwark).
	for s in [1, 2, 3]:
		var g := Moves.resolve(Sim.Element.EARTH, s, "guard")
		check(g != "guard" and Moves.DEFS.has(g), "%s guard spec" % SUBS[s])
	var n := 0
	for s in 4:
		n += Moves.list(Sim.Element.EARTH, s).size()
	check(n >= 40, "Earth binds %d moves across 4 sub-elements" % n)


func test_anim_clips_exist() -> void:
	var f := FileAccess.open("res://assets/characters/fighter_clips.json", FileAccess.READ)
	var data: Variant = JSON.parse_string(f.get_as_text()) if f != null else null
	var clips: Dictionary = data.get("clips", data) if data is Dictionary else {}
	Moves.ensure()
	for s in 4:
		for id in Moves.list(Sim.Element.EARTH, s):
			if Moves.BASE_DEFS.has(id) and not ["earth_attack", "earth_tech"].has(id):
				continue
			var d: Dictionary = Moves.DEFS[id]
			for k in ["anim", "anim_active", "anim_hold"]:
				if d.has(k):
					check(clips.has(String(d[k])), "%s.%s = %s is an existing clip" % [id, k, d[k]])


func _run_sub(s: int, slots: Array) -> void:
	for slot in slots:
		var id := Moves.resolve(Sim.Element.EARTH, s, slot)
		var d: Dictionary = Moves.DEFS.get(id, {})
		var mt := 3 if ["strike", "thrust", "ground", "sweep", "push", "sink"].has(slot) else (Charge.max_tier(d) if slot == "guard" else 0)
		if slot == "push" or slot == "sink":
			mt = mini(3, maxi(Charge.max_tier(d), 1))
		for tier in mt + 1:
			var r := U.run_move(s, slot, tier, Callable(self, "_prep_%d" % s))
			check(r.started, "%s %s T%d starts (%s)" % [SUBS[s], slot, tier, r.move])
			check(r.paid, "%s %s T%d pays" % [SUBS[s], slot, tier])
			check(r.ended, "%s %s T%d ends cleanly" % [SUBS[s], slot, tier])
			check((r.bad_fx as Array).is_empty(), "%s %s T%d fx keys catalogued: %s" % [SUBS[s], slot, tier, r.bad_fx])
			if not Moves.BASE_DEFS.has(String(r.move)):   # the legacy guard / evade keep their legacy events
				check(int(r.fx) > 0, "%s %s T%d emits fx" % [SUBS[s], slot, tier])
			if ["strike", "thrust", "ground", "sweep"].has(slot) and Charge.max_tier(d) >= tier:
				check(int(r.tier) >= tier, "%s %s T%d: charge event reached tier %d" % [SUBS[s], slot, tier, r.tier])


# Material the technique / push moves need on the field.
func _prep_0(h: SimHarness, a: ActorState, t: ActorState) -> void:
	U.shot(h, Sim.Mat.STONE, 20.0, Vector3(0, 1.2, 0.5), Vector3.ZERO, t)


func _prep_1(h: SimHarness, a: ActorState, t: ActorState) -> void:
	var d := U.shot(h, Sim.Mat.METAL, 2.0, Vector3(0.3, 1.0, 1.0), Vector3.ZERO, null, "disc")
	d.attack_id = 0
	d.on_ground = true


func _prep_2(_h: SimHarness, _a: ActorState, _t: ActorState) -> void:
	pass


func _prep_3(h: SimHarness, a: ActorState, _t: ActorState) -> void:
	U.lava(h, 12.0, Vector3(0.5, 0.2, 2.0))


func test_stone_moves_t0_to_t3() -> void:
	_run_sub(0, Sim.SLOTS)


func test_metal_moves_t0_to_t3() -> void:
	_run_sub(1, Sim.SLOTS)


func test_sand_moves_t0_to_t3() -> void:
	_run_sub(2, Sim.SLOTS)


func test_magma_moves_t0_to_t3() -> void:
	_run_sub(3, Sim.SLOTS)
