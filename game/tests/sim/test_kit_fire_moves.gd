extends FireKitTest
## Every Fire move (four sub-elements, every slot) through the real input path at T0-T3: it starts, pays, emits its
## catalogued fx cues and charge events, ends cleanly (no stuck action), and the energy / mass ledgers stay exact.

const HOLD_EXTRA := 4   # ticks past a tier time


## Presses the slot's input like a player (desktop: press + gesture on one tick), holds for the tier, releases.
func _play(p: ActorState, slot: String, tier: int) -> void:
	var it := h.it(p)
	var hold := 3
	var id := Moves.resolve(Sim.Element.FIRE, p.sub_of(Sim.Element.FIRE), slot)
	var d: Dictionary = Moves.DEFS.get(id, {})
	if tier > 0:
		hold = int(ceil(float(Charge.tier_times(d)[tier - 1]) * Sim.HZ)) + HOLD_EXTRA
	match slot:
		"strike", "thrust", "ground", "sweep":
			var g: int = int({"strike": 0, "thrust": Sim.Gesture.UP, "ground": Sim.Gesture.DOWN, "sweep": Sim.Gesture.SIDE}[slot])
			if g == 0:
				h.press(p, "attack")
			else:
				h.flick(p, "attack", g)
			h.step(hold)
			h.release(p, "attack")
		"guard":
			h.press(p, "guard")
			h.step(maxi(hold, 30))
			h.release(p, "guard")
		"push", "sink":
			h.press(p, "guard")
			h.step(12)
			h.flick(p, "guard", Sim.Gesture.UP if slot == "push" else Sim.Gesture.DOWN)
			h.step(hold)
			h.release(p, "guard")
		"tech":
			h.press(p, "tech")
			h.step(maxi(hold, 40))
			h.release(p, "tech")
		"evade":
			it.move = Vector3(1, 0, 0)
			h.press(p, "evade")
			h.step(2)
			it.move = Vector3.ZERO
		"evade_hold":
			it.move = Vector3(1, 0, 0)
			h.evade_hold(p, 40)
			it.move = Vector3.ZERO
	h.step(1)


func _sub_case(sub: int) -> void:
	var names := ["Flame", "Blue", "Lightning", "Combustion"]
	for slot in Sim.SLOTS:
		var id := Moves.resolve(Sim.Element.FIRE, sub, slot)
		if id == "":
			continue
		var d: Dictionary = Moves.DEFS[id]
		var tiers: Array = [0] if not Sim.ATTACK_SLOTS.has(slot) else range(Charge.max_tier(d) + 1)
		for tier in tiers:
			var pr := duel(sub, Sim.Element.EARTH, 7, 7.0, {"magma": true, "heat_draw": true})
			var p: ActorState = pr[0]
			var r: ActorState = pr[1]
			r.is_dummy = true
			# Things for techniques and guards to work on: a loose stone, a puddle, a stone thrown at us.
			var st := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, p.pos + Vector3(0, 0.3, -2.5), "test")
			h.w.mass_ledger.ground_taken += 20.0
			st.on_ground = true
			var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 3.0, p.pos + Vector3(1.2, 0.0, -1.0), "test")
			pd.update_radius_puddle()
			p.heat_reserve = 200.0
			var base := snap(h.w)
			var f0 := p.focus
			var res0 := p.heat_reserve
			var label := "%s %s %s T%d" % [names[sub], slot, id, tier]
			if slot == "guard":
				h.launch_at(p, Sim.Mat.STONE, 20.0, 17.0, Sim.AMBIENT_C, "", r, 7.0)
				h.w.mass_ledger.ground_taken += 20.0
				base = snap(h.w)
			_play(p, slot, tier)
			var started := h.events("action").any(func(e): return e.actor == p.id and (e.move == id or (slot == "guard" and e.move == "guard")))
			check(started, "%s: started" % label)
			h.until(func(): return p.action == null, 400)
			check(p.action == null, "%s: ends cleanly (still %s)" % [label, p.action.id + "/" + p.action.phase_name() if p.action else ""])
			var paid := (f0 - p.focus) * Sim.HU_PER_FOCUS + (res0 - p.heat_reserve)
			if float(d.get("heat", 0.0)) > 0.0 or float(d.get("cost", 0.0)) > 0.0:
				check(paid > 0.0 or slot == "evade_hold", "%s: paid something (%.1f HU equivalent)" % [label, paid])
			if tier > 0:
				check(h.events("charge").any(func(e): return e.actor == p.id and int(e.tier) == tier), "%s: charge event for T%d" % [label, tier])
			if not ["fire_tech", "evade"].has(id):   # legacy moves keep their legacy events (thermal, evade)
				check(h.events("fx").any(func(e): return e.get("actor", -1) == p.id) or h.has_event("lightning") or h.has_event("flare"),
					"%s: fx cues" % label)
			ledgers_ok(base, label, 1e-3)
			fx_catalogued(label)


func test_flame_moves_t0_t3() -> void:
	_sub_case(0)


func test_blue_moves_t0_t3() -> void:
	_sub_case(1)


func test_lightning_moves_t0_t3() -> void:
	_sub_case(2)


func test_combustion_moves_t0_t3() -> void:
	_sub_case(3)


func test_every_slot_is_bound_for_every_sub_element_with_a_complete_def() -> void:
	Moves.ensure()
	var keys := ["name", "desc", "slot", "anim", "fx", "ai"]
	for sub in 4:
		for slot in Sim.SLOTS:
			var id := Moves.resolve(Sim.Element.FIRE, sub, slot)
			check(id != "", "sub %d %s bound" % [sub, slot])
			if id == "" or sub == 0 and ["strike", "tech", "evade"].has(slot):
				continue
			var d: Dictionary = Moves.DEFS[id]
			for k in keys:
				check(d.has(k), "%s has %s" % [id, k])
			check((d.get("fx", {}) as Dictionary).has("mat") and FxEvents.is_known("mat", String(d.fx.mat)), "%s fx mat" % id)
			check(FxEvents.is_known("shape", String(d.get("fx", {}).get("shape", ""))), "%s fx shape" % id)
			for ak in ["anim", "anim_active", "anim_hold", "anim_charge", "anim_t2", "anim_t3"]:
				if d.has(ak):
					check(_clip_exists(String(d[ak])), "%s.%s clip '%s' exists" % [id, ak, d[ak]])
			if Sim.ATTACK_SLOTS.has(slot) and slot != "strike" or slot == "strike" and sub > 0:
				check(Charge.max_tier(d) == 3, "%s has T0-T3" % id)
			if Sim.ATTACK_SLOTS.has(slot) or slot == "guard":
				check(d.has("counter"), "%s counter metadata" % id)


static var _clips := {}


func _clip_exists(nm: String) -> bool:
	if _clips.is_empty():
		var f := FileAccess.open("res://assets/characters/fighter_clips.json", FileAccess.READ)
		var j: Variant = JSON.parse_string(f.get_as_text()) if f != null else {}
		var c: Variant = j.get("clips", j) if j is Dictionary else {}
		if c is Dictionary:
			for k in c:
				_clips[k] = true
		elif c is Array:
			for x in c:
				_clips[String(x.get("name", ""))] = true
	return _clips.has(nm)
