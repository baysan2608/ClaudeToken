class_name ActFire
extends RefCounted
## Fire: flare jab (tap) / blaze (hold) / lightning (long hold, if learned),
## thermal technique (HEAT a stone into lava, or DRAW heat out of lava/hot rock),
## pour (send held lava along the ground) and vent (dump the heat reserve).
##
## The thermal mode is chosen ONCE at technique press from a fixed rule and shown
## on the HUD before commit; it never silently switches while held.
##   target molten/hot stone not held by me -> DRAW (needs heat_draw)
##   target solid stone                      -> HEAT  (needs magma)
##   target water / ice                      -> HEAT  (boil / melt)
##   no target and heat reserve > 40 HU      -> VENT

const GRIP_WINDOW := 0.25      # after startup, how long the magma grip reaches for a stone
const INCOMING_RANGE := 14.0   # incoming projectiles can be selected from this far
const VENT_MIN := 40.0


static func preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	## What the fire technique would do right now: {mode, body, ok, reason}.
	var d: Dictionary = Moves.DEFS.fire_tech
	var hot := w.find_body(a, dir, float(d.draw_range), 50.0, func(b: MatBody) -> bool:
		return b.is_stone() and b.controller != a.id and (b.liquid > 0.0 or b.temp >= Sim.HOT_ROCK_C) and b.form != Sim.Form.WALL)
	if hot != null:
		if not a.has("heat_draw"):
			return {"mode": "DRAW", "body": hot.id, "ok": false, "reason": "technique"}
		if not w.los(a.chest(), hot.pos + Vector3(0, 0.3, 0)):
			return {"mode": "DRAW", "body": hot.id, "ok": false, "reason": "sight"}
		return {"mode": "DRAW", "body": hot.id, "ok": true, "reason": ""}
	var stone := w.find_body(a, dir, INCOMING_RANGE, 40.0, func(b: MatBody) -> bool:
		return b.is_stone() and b.is_projectile() and b.attack_owner != a.id and b.vel.dot(a.chest() - b.pos) > 0.0)
	if stone == null:
		stone = w.find_body(a, dir, float(d.draw_range), 55.0, func(b: MatBody) -> bool:
			return b.is_stone() and b.form != Sim.Form.WALL and b.controller != a.id and b.phase != Sim.Phase.MOLTEN)
	if stone != null:
		if not a.has("magma"):
			return {"mode": "HEAT", "body": stone.id, "ok": false, "reason": "technique"}
		# Too heavy is still attempted (the grip strains and fails); the HUD warns first.
		return {"mode": "HEAT", "body": stone.id, "ok": true, "reason": "mass" if stone.mass > a.max_control_mass else ""}
	var wet := w.find_body(a, dir, float(d.reach), 45.0, func(b: MatBody) -> bool:
		return b.is_water() and b.form != Sim.Form.POOL and b.controller != a.id)
	if wet != null:
		return {"mode": "HEAT", "body": wet.id, "ok": true, "reason": ""}
	if a.heat_reserve >= VENT_MIN:
		return {"mode": "VENT", "body": -1, "ok": true, "reason": ""}
	return {"mode": "", "body": -1, "ok": false, "reason": "target"}


static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	inst.data["face"] = w.aim_dir(a, it)
	match inst.id:
		"fire_attack":
			if not w.can_pay_heat(a, float(inst.def.cost_hu)):
				inst.data["fizzle"] = true
				w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		"fire_tech":
			var held_b := w.held(a)
			if held_b != null and held_b.is_stone():
				inst.data["mode"] = "HEAT"
				inst.data["target"] = held_b.id
			else:
				var pv := preview(w, a, w.aim_dir(a, it))
				inst.data["mode"] = pv.mode
				inst.data["target"] = pv.body
				if not pv.ok:
					inst.data["fizzle"] = true
					w.emit("insufficient", {"actor": a.id, "what": pv.reason, "move": "thermal", "mode": pv.mode})
			if inst.data.mode == "DRAW":
				inst.data["startup"] = float(inst.def.draw_startup)
				w.emit("telegraph", {"actor": a.id, "move": "heat_draw", "body": inst.data.target, "time": inst.def.draw_startup})
			w.emit("thermal", {"actor": a.id, "mode": inst.data.mode, "body": inst.data.target})
		"pour":
			pass
		"lightning":
			pass
		"vent":
			pass


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	match inst.id:
		"fire_attack":
			if inst.data.get("fizzle", false):
				return ActionInst.P.RECOVERY
			return w.attack_after_startup(a, inst, it)
		"fire_tech":
			if inst.data.get("fizzle", false):
				return ActionInst.P.RECOVERY
			if inst.data.mode == "VENT":
				return ActionInst.P.ACTIVE
			return ActionInst.P.CHANNEL
	return ActionInst.P.ACTIVE


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	match inst.id:
		"fire_attack":
			if p == ActionInst.P.CHARGE and a.has("lightning"):
				w.emit("telegraph", {"actor": a.id, "move": "lightning", "time": inst.def.lightning_min})
			if p == ActionInst.P.ACTIVE:
				_flare(w, a, inst)
		"fire_tech":
			if p == ActionInst.P.ACTIVE and inst.data.mode == "VENT":
				_vent(w, a)
		"pour":
			if p == ActionInst.P.ACTIVE:
				_pour(w, a, inst)
		"lightning":
			if p == ActionInst.P.ACTIVE:
				if not w.spend_focus(a, float(inst.def.cost)):
					w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "lightning"})
					return
				var aim: Vector3 = inst.data.get("aim_point", a.chest() + a.forward() * 10.0)
				Conduction.discharge(w, a, aim, inst.def, inst.attack_id, true)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"fire_attack":
			if not it.attack_held:
				inst.data["released"] = true
			inst.data["face"] = w.aim_dir(a, it)
			if inst.phase == ActionInst.P.CHARGE:
				var can_bolt := a.has("lightning") and a.focus >= float(Moves.DEFS.lightning.cost)
				if can_bolt and inst.total >= float(inst.def.lightning_min) and not inst.data.get("bolt_ready", false):
					inst.data["bolt_ready"] = true
					w.emit("charge_ready", {"actor": a.id, "move": "lightning"})
				if inst.data.get("released", false) and inst.total >= float(inst.def.heavy_min):
					if inst.data.get("bolt_ready", false):
						var aimp := w.aim_point(a, it)
						a.action = null
						w.start_action(a, "lightning", it, {"aim_point": aimp, "face": inst.data.face})
					else:
						w.set_phase(a, inst, ActionInst.P.ACTIVE)
		"fire_tech":
			if inst.phase == ActionInst.P.STARTUP:
				inst.data["face"] = _face_target(w, a, inst)
				return
			if inst.phase != ActionInst.P.CHANNEL:
				return
			if it.tech_cancel:
				ActEarth._drop(w, a)
				w.emit("cancel", {"actor": a.id, "move": inst.id})
				w.set_phase(a, inst, ActionInst.P.RECOVERY)
				return
			inst.data["aim"] = w.aim_dir(a, it)
			inst.data["aim_active"] = it.aim_active
			match String(inst.data.mode):
				"HEAT":
					_heat_tick(w, a, inst, it)
				"DRAW":
					_draw_tick(w, a, inst, it)
		"pour":
			var b := w.held(a)
			if b != null:
				var k := clampf(inst.t / float(inst.def.startup), 0.0, 1.0)
				var dir: Vector3 = inst.data.get("aim", a.forward())
				var g := a.pos + dir * 1.3
				g.y = w.arena.ground_height(g.x, g.z, a.pos.y) + 0.2
				b.hold_point = (a.pos + Vector3(0, 1.35, 0) + dir * 1.1).lerp(g, ease(k, 2.0))


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, _reason: String) -> void:
	if inst.id == "fire_tech" or inst.id == "pour":
		var b := w.held(a)
		if b != null:
			w.emit("conversion_interrupted", {"actor": a.id, "body": b.id, "liquid": b.liquid})
		ActEarth._drop(w, a)


# ---------------------------------------------------------------------------

static func _face_target(w: CombatWorld, a: ActorState, inst: ActionInst) -> Vector3:
	var b := w.get_body(inst.data.get("target", -1))
	if b != null and b.alive:
		var d := b.pos - a.pos
		d.y = 0
		if d.length() > 0.2:
			return d.normalized()
	return a.forward()


static func _heat_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var d := inst.def
	var b := w.get_body(inst.data.get("target", -1))
	if b == null or not b.alive:
		ActEarth._drop(w, a)
		w.emit("whiff", {"actor": a.id, "move": "thermal"})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["face"] = _face_target(w, a, inst)
	if b.is_water():
		_heat_water(w, a, inst, b, it)
		return
	var holding := b.controller == a.id
	if not holding:
		# Magma grip: reach for the stone during the grip window only (early press = whiff).
		var dist := a.chest().distance_to(b.pos)
		if dist <= float(d.reach):
			if b.mass > a.max_control_mass:
				w.request_grip(a, b, 0.0, "magma_grip")   # emits control_fail (mass)
				w.set_phase(a, inst, ActionInst.P.RECOVERY)
				return
			w.request_grip(a, b, w.grip_strength(a, b, float(d.grip), float(d.reach)), "magma_grip")
		if inst.t > GRIP_WINDOW and w.get_body(inst.data.target).controller != a.id:
			w.emit("whiff", {"actor": a.id, "move": "magma_grip", "body": b.id, "dist": dist})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
			return
		if not it.tech_held and inst.t > 0.05:
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	# Holding: shape in front of the hands and pour heat in at a bounded rate.
	b.hold_point = a.pos + Vector3(0, 1.35, 0) + a.forward() * 1.1
	if b.liquid < 1.0:
		var want := float(d.heat_rate) * Sim.DT
		var paid := w.pay_heat(a, want)
		if paid < want * 0.5 and not inst.data.get("starved", false):
			inst.data["starved"] = true
			w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "heat"})
		var used := Thermal.apply_heat(b, paid).x
		w.ledger.spent += paid - used
		b.touch(a.id, "heat", w.tick)
		if w.tick % 6 == 0:
			w.emit("heating", {"actor": a.id, "body": b.id, "liquid": b.liquid, "temp": b.temp})
	if not it.tech_held:
		if b.phase == Sim.Phase.MOLTEN:
			var aim: Vector3 = inst.data.aim
			a.action = null
			w.start_action(a, "pour", it, {"aim": aim, "face": aim, "body": b.id})
		else:
			# Not molten yet: throw the (hot) stone instead.
			w.set_phase(a, inst, ActionInst.P.ACTIVE)
			var tgt := ActEarth._throw_target(w, a, inst)
			w.release_body(a, ActEarth.launch_vel(b.pos, tgt, float(d.speed)), true, float(d.damage), float(d.balance))
			w.emit("launch", {"actor": a.id, "body": b.id, "kind": "hot_stone"})


static func _heat_water(w: CombatWorld, a: ActorState, inst: ActionInst, b: MatBody, it: ActorIntent) -> void:
	if a.chest().distance_to(b.pos) > float(inst.def.reach) or not it.tech_held:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var paid := w.pay_heat(a, float(inst.def.heat_rate) * 0.5 * Sim.DT)
	if b.phase == Sim.Phase.FROZEN or b.liquid < 1.0:
		var used := Thermal.apply_heat(b, paid).x
		w.ledger.spent += paid - used
	else:
		w.boil_water(b, paid, b.pos)
		if b.mass <= 0.05:
			w.decay_body(b, "boiled")
			w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func _draw_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var d := inst.def
	var b := w.get_body(inst.data.get("target", -1))
	if not it.tech_held or b == null or not b.alive:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["face"] = _face_target(w, a, inst)
	var dist := a.chest().distance_to(b.pos)
	if dist > float(d.draw_range) + 1.0:
		w.emit("draw_break", {"actor": a.id, "body": b.id, "reason": "range"})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	if not w.los(a.chest(), b.pos + Vector3(0, 0.3, 0)):
		if w.tick % 10 == 0:
			w.emit("draw_break", {"actor": a.id, "body": b.id, "reason": "sight"})
		return
	var room := Sim.RESERVE_MAX - a.heat_reserve
	if room <= 0.5:
		if not inst.data.get("full", false):
			inst.data["full"] = true
			w.emit("reserve_full", {"actor": a.id})
		return
	var avail := maxf(0.0, b.thermal_energy())
	# Drawing is strongest up close: full rate within 3 m, half at max range.
	var falloff := 1.0 - 0.5 * clampf((dist - 3.0) / (float(d.draw_range) - 3.0), 0.0, 1.0)
	var e := minf(float(d.draw_rate) * falloff * Sim.DT, minf(room, avail))
	var fcost := e / Sim.DRAW_HU_PER_FOCUS
	if a.focus < fcost:
		if not inst.data.get("starved", false):
			inst.data["starved"] = true
			w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "draw"})
		return
	w.spend_focus(a, fcost)
	var taken := -Thermal.apply_heat(b, -e).x
	a.heat_reserve += taken
	b.touch(a.id, "draw", w.tick)
	if w.tick % 6 == 0:
		w.emit("drawing", {"actor": a.id, "body": b.id, "liquid": b.liquid, "temp": b.temp, "reserve": a.heat_reserve})


static func _pour(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.held(a)
	if b == null or not b.is_stone() or b.liquid <= 0.0:
		return
	var d := inst.def
	var dir: Vector3 = inst.data.get("aim", a.forward())
	dir.y = 0
	dir = dir.normalized()
	var start := a.pos + dir * 1.3
	start.y = w.arena.ground_height(start.x, start.z, a.pos.y)
	w.release_body(a, Vector3.ZERO, true, float(d.damage), float(d.balance))
	var old_form := b.form
	b.form = Sim.Form.WAVE
	b.pos = start
	b.wave_dir = dir
	b.wave_budget = float(d.base_budget) + float(d.budget_per_kg) * b.mass
	b.wave_width = 1.1 + b.mass * 0.025
	b.wave_path = PackedVector3Array([start])
	b.max_life = -1.0
	b.age = 0.0
	b.touch(a.id, "pour", w.tick)
	w.emit("transform", {"body": b.id, "from": Sim.FORM_NAMES[old_form], "to": "wave", "why": "poured"})


static func _flare(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var d := inst.def
	var heavy := inst.heavy
	var rng_m := float(d.heavy_range) if heavy else float(d.range)
	var cone := float(d.heavy_cone) if heavy else float(d.cone)
	var cost := float(d.heavy_cost_hu) if heavy else float(d.cost_hu)
	var paid := w.pay_heat(a, cost)
	var dir: Vector3 = inst.data.face
	w.emit("flare", {"actor": a.id, "dir": dir, "range": rng_m, "heavy": heavy, "from_reserve": a.heat_reserve > 0.0})
	var left := paid
	# Water shields in the way take the heat first (steam), protecting their holder.
	var shielded := {}
	for b in w.bodies:
		if not b.alive or not b.is_water() or b.form == Sim.Form.POOL:
			continue
		var to := b.pos - a.chest()
		if to.length() > rng_m or (to.length() > 0.5 and Vector3(to.x, 0, to.z).normalized().dot(dir) < cos(deg_to_rad(cone + 10.0))):
			continue
		var share := left * 0.6
		if b.phase == Sim.Phase.FROZEN:
			var used := Thermal.apply_heat(b, share).x
			left -= used
		else:
			w.boil_water(b, share, b.pos)
			left -= share
			if b.mass <= 0.05:
				w.decay_body(b, "boiled")
		if b.controller >= 0:
			shielded[b.controller] = true
		w.emit("steam_block", {"actor": a.id, "body": b.id})
	for t in w.actors_in_cone(a, dir, rng_m, cone):
		if shielded.has(t.id):
			w.emit("block", {"actor": t.id, "attacker": a.id, "kind": "fire_water"})
			continue
		if t.element == Sim.Element.AIR and t.guarding:
			w.emit("block", {"actor": t.id, "attacker": a.id, "kind": "fire_air"})
			continue
		var res := w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id,
			"damage": d.heavy_damage if heavy else d.damage, "balance": d.heavy_balance if heavy else d.balance,
			"knock": dir * (3.0 if heavy else 1.2), "kind": "fire", "from": a.chest()})
		if res == "perfect" and t.element == Sim.Element.FIRE:
			# Perfect fire guard absorbs part of the flame into the reserve.
			var gain := minf(left * 0.5, Sim.RESERVE_MAX - t.heat_reserve)
			t.heat_reserve += gain
			left -= gain
	# Stones in the cone warm up.
	for b in w.bodies:
		if not b.alive or not b.is_stone() or b.form == Sim.Form.WALL or left <= 0.0:
			continue
		var to := b.pos - a.chest()
		if to.length() < rng_m and (to.length() < 0.5 or Vector3(to.x, 0, to.z).normalized().dot(dir) > cos(deg_to_rad(cone))):
			var used := Thermal.apply_heat(b, left * 0.5).x
			left -= used
	w.ledger.spent += maxf(0.0, left)


static func _vent(w: CombatWorld, a: ActorState) -> void:
	var e := a.heat_reserve
	a.heat_reserve = 0.0
	w.ledger.vented += e
	w.emit("vent", {"actor": a.id, "amount": e})
