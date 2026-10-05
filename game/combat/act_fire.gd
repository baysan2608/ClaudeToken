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
##   a wall / a body it can't grip in reach  -> SCORCH (ranged heat 300 HU/s: walls slump, metal softens)
##   no target and heat reserve > 40 HU      -> VENT
## Charged strike (moveset, MOVESET §7.9): T2 (1.0 s) Fire Column, T3 (1.8 s) Inferno - both leave a fire field.
## The legacy `lightning` kit flag keeps turning a >= 0.65 s hold into the bolt (tests); without it the hold grows.

const GRIP_WINDOW := 0.25      # after startup, how long the magma grip reaches for a stone
const INCOMING_RANGE := 14.0   # incoming projectiles can be selected from this far
const VENT_MIN := 40.0


static func preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	## What the fire technique would do right now: {mode, body, ok, reason}.
	var d: Dictionary = Moves.DEFS.fire_tech
	# Legality through the engine: draw_heat / heat_grip cells (legacy: hot stone / any stone / water).
	var hot := w.find_body(a, dir, float(d.draw_range), 50.0, func(b: MatBody) -> bool:
		return b.controller != a.id and b.form != Sim.Form.WALL and Interactions.allows(b, &"draw_heat"))
	if hot != null:
		if not a.has("heat_draw"):
			return {"mode": "DRAW", "body": hot.id, "ok": false, "reason": "technique"}
		if not w.los(a.chest(), hot.pos + Vector3(0, 0.3, 0)):
			return {"mode": "DRAW", "body": hot.id, "ok": false, "reason": "sight"}
		return {"mode": "DRAW", "body": hot.id, "ok": true, "reason": ""}
	var stone := w.find_body(a, dir, INCOMING_RANGE, 40.0, func(b: MatBody) -> bool:
		return not b.is_water() and b.is_projectile() and b.attack_owner != a.id and b.vel.dot(a.chest() - b.pos) > 0.0 and Interactions.allows(b, &"heat_grip"))
	if stone == null:
		stone = w.find_body(a, dir, float(d.draw_range), 55.0, func(b: MatBody) -> bool:
			return not b.is_water() and b.form != Sim.Form.WALL and b.controller != a.id and b.phase != Sim.Phase.MOLTEN and Interactions.allows(b, &"heat_grip"))
	if stone != null:
		if not a.has("magma"):
			return {"mode": "HEAT", "body": stone.id, "ok": false, "reason": "technique"}
		# Too heavy is still attempted (the grip strains and fails); the HUD warns first.
		return {"mode": "HEAT", "body": stone.id, "ok": true, "reason": "mass" if stone.mass > a.max_control_mass else ""}
	var wet := w.find_body(a, dir, float(d.reach), 45.0, func(b: MatBody) -> bool:
		return b.is_water() and b.form != Sim.Form.POOL and b.controller != a.id and (Interactions.allows(b, &"heat_grip") or b.form == Sim.Form.CLOUD))
	if wet != null:
		return {"mode": "HEAT", "body": wet.id, "ok": true, "reason": ""}
	var sc := scorch_target(w, a, dir)
	if sc != null:
		return {"mode": "SCORCH", "body": sc.id, "ok": true, "reason": ""}
	if a.heat_reserve >= VENT_MIN:
		return {"mode": "VENT", "body": -1, "ok": true, "reason": ""}
	return {"mode": "", "body": -1, "ok": false, "reason": "target"}


## SCORCH target: a raised wall within 6 m in the aim (nearest), else a loose body the magma grip can't take
## (Interactions heat_ranged cells: metal, sand, glass, ice, heavy stone).
static func scorch_target(w: CombatWorld, a: ActorState, dir: Vector3) -> MatBody:
	var sd: Dictionary = Moves.DEFS.fire_tech.get("scorch", {})
	if sd.is_empty():
		return null
	var rng_m := float(sd.get("range", 6.0))
	var cone := cos(deg_to_rad(float(sd.get("cone", 40.0))))
	var best: MatBody = null
	var bd := INF
	for b in w.bodies:
		if not b.alive or b.form != Sim.Form.WALL or b.wall_rise < 0.3:
			continue
		var to := b.pos - a.pos
		to.y = 0.0
		var dd := to.length()
		if dd > rng_m + b.wall_half.x or (dd > 0.5 and to.normalized().dot(dir) < cone):
			continue
		if dd < bd or (is_equal_approx(dd, bd) and best != null and b.id < best.id):
			best = b
			bd = dd
	if best != null:
		return best
	return w.find_body(a, dir, rng_m, float(sd.get("cone", 40.0)), func(b: MatBody) -> bool:
		return b.controller != a.id and b.form != Sim.Form.POOL and b.form != Sim.Form.ZONE and b.form != Sim.Form.PUDDLE \
			and b.form != Sim.Form.WAVE and b.mass >= 0.5 and not b.static_body and b.mat != Sim.Mat.FIRE and b.mat != Sim.Mat.AIR \
			and b.mat != Sim.Mat.STEAM and Interactions.allows(b, &"heat_ranged") \
			and (not Interactions.allows(b, &"heat_grip") or b.mass > a.max_control_mass))


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
			if inst.data.mode == "SCORCH":
				inst.data["spec_def"] = inst.def.get("scorch", {})
				inst.data["aim"] = w.aim_dir(a, it)
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
			if inst.data.mode == "SCORCH":
				VerbHeat.start(w, a, inst, it)
				if inst.data.get("fizzle", false):
					return ActionInst.P.RECOVERY
				Verbs.fx(w, a, inst, "beam", {"length": 6.0, "body": int(inst.data.get("target", -1)), "dur": 1.5})
			return ActionInst.P.CHANNEL
	return ActionInst.P.ACTIVE


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	match inst.id:
		"fire_attack":
			if p == ActionInst.P.CHARGE and a.has("lightning"):
				w.emit("telegraph", {"actor": a.id, "move": "lightning", "time": inst.def.lightning_min})
			if p == ActionInst.P.ACTIVE:
				if inst.tier() >= 2 and inst.heavy:
					_column(w, a, inst)
				else:
					_flare(w, a, inst)
		"fire_tech":
			if p == ActionInst.P.ACTIVE and inst.data.mode == "VENT":
				_vent(w, a)
			if p == ActionInst.P.RECOVERY and inst.data.get("mode", "") == "SCORCH":
				VerbHeat.end(w, a, inst)
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
			if it.tech_cancel and (inst.phase == ActionInst.P.STARTUP or inst.phase == ActionInst.P.CHANNEL):
				# Honoured from the first frame: a cancel during startup never grips, draws or vents.
				ActEarth._drop(w, a)
				w.emit("cancel", {"actor": a.id, "move": inst.id})
				w.set_phase(a, inst, ActionInst.P.RECOVERY)
				return
			if inst.phase == ActionInst.P.STARTUP:
				inst.data["face"] = _face_target(w, a, inst)
				return
			if inst.phase != ActionInst.P.CHANNEL:
				return
			inst.data["aim"] = w.aim_dir(a, it)
			inst.data["aim_active"] = it.aim_active
			match String(inst.data.mode):
				"HEAT":
					_heat_tick(w, a, inst, it)
				"DRAW":
					_draw_tick(w, a, inst, it)
				"SCORCH":
					VerbHeat.tick(w, a, inst, it)
		"pour":
			var b := w.held(a)
			if b != null:
				var k := clampf(inst.t / float(inst.def.startup), 0.0, 1.0)
				var dir: Vector3 = inst.data.get("aim", a.forward())
				var g: Vector3 = _pour_start(w, a, dir).pos + Vector3(0, 0.2, 0)
				b.hold_point = (a.pos + Vector3(0, 1.05, 0) + dir * (0.3 + b.radius)).lerp(g, ease(k, 2.0))


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, _reason: String) -> void:
	if inst.id == "fire_tech" and inst.data.get("mode", "") == "SCORCH":
		VerbHeat.end(w, a, inst)
		return
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
	# Holding: between the cupped hands (magma_hold pose) and pour heat in at a bounded rate.
	b.hold_point = a.pos + Vector3(0, 1.05, 0) + a.forward() * (0.3 + b.radius)
	if b.liquid < 1.0:
		var want := float(d.heat_rate) * Sim.DT
		# Never spend the Focus needed to keep holding molten mass (≈2 s of upkeep): running dry
		# stalls the conversion partway with the stone still in hand, it doesn't drop it.
		var keep := Sim.HOLD_UPKEEP_FOCUS * 2.0
		var affordable := a.heat_reserve + maxf(0.0, a.focus - keep) * Sim.HU_PER_FOCUS
		want = minf(want, affordable)
		var paid := w.pay_heat(a, want) if want > 0.0 else 0.0
		if paid < float(d.heat_rate) * Sim.DT * 0.5 and not inst.data.get("starved", false):
			inst.data["starved"] = true
			w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "heat"})
		var used := w.heat_body(b, paid)
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
		var used := w.heat_body(b, paid)
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
	var taken := -Thermal.heat(b, -e)
	a.heat_reserve += taken
	b.touch(a.id, "draw", w.tick)
	if w.tick % 6 == 0:
		w.emit("drawing", {"actor": a.id, "body": b.id, "liquid": b.liquid, "temp": b.temp, "reserve": a.heat_reserve})


static func _pour(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.held(a)
	if b != null and not b.is_stone() and Materials.is_fusible(b.mat):
		# Molten metal / fused sand: thrown as a molten blob (it sets as metal / glass where it lands).
		var tgt := ActEarth._throw_target(w, a, inst)
		w.release_body(a, ActEarth.launch_vel(b.pos, tgt, float(inst.def.wave_speed) * 2.0), true, float(inst.def.damage) * 0.7, float(inst.def.balance) * 0.6)
		w.emit("launch", {"actor": a.id, "body": b.id, "kind": "molten_" + Sim.MAT_NAMES[b.mat]})
		return
	if b == null or not b.is_stone() or b.liquid <= 0.0:
		return
	var d := inst.def
	var dir: Vector3 = inst.data.get("aim", a.forward())
	dir.y = 0
	dir = dir.normalized()
	var ps := _pour_start(w, a, dir)
	var start: Vector3 = ps.pos
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
	w.emit("transform", {"body": b.id, "at": b.pos, "from": Sim.FORM_NAMES[old_form], "to": "wave", "why": "poured"})
	if ps.blocked:
		# Poured point-blank into cover: the lava stays on this side and pools there.
		w.emit("wave_blocked", {"body": b.id, "at": start})
		w._settle_wave(b, "blocked")


## Where a pour along dir starts: on the ground 1.3 m ahead, or pulled back short of any wall,
## earth wall or rise in between (never beyond it). Tested level at the pourer's feet, so pouring
## off a ledge is not blocked. Returns {pos, blocked}.
static func _pour_start(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var start := a.pos + dir * 1.3
	var from := a.pos + Vector3(0, 0.2, 0)
	var to := Vector3(start.x, from.y, start.z)
	var hit_t := w.arena.segment_hit(from, to, 0.1)
	var wall_t := w.wall_hit(from, to)
	if wall_t >= 0.0 and (hit_t < 0.0 or wall_t < hit_t):
		hit_t = wall_t
	if hit_t >= 0.0:
		start = a.pos + dir * maxf(0.0, 1.3 * hit_t - 0.25)
	start.y = w.arena.ground_height(start.x, start.z, a.pos.y)
	return {"pos": start, "blocked": hit_t >= 0.0}


static func _flare(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var d := inst.def
	var heavy := inst.heavy
	var rng_m := float(d.heavy_range) if heavy else float(d.range)
	var cone := float(d.heavy_cone) if heavy else float(d.cone)
	var cost := float(d.heavy_cost_hu) if heavy else float(d.cost_hu)
	var paid := w.pay_heat(a, cost)
	var dir: Vector3 = inst.data.face
	w.emit("flare", {"actor": a.id, "dir": dir, "range": rng_m, "heavy": heavy, "from_reserve": a.heat_reserve > 0.0})
	# The flame is a heat volume; flame.heat is the budget still to spend (HU). Every contact is a
	# rule cell (CoreRules._flare / _guards): water takes 60 % first (steam), stones 50 % of what is left.
	var flame := Agent.of_volume(w, a, inst, &"flame", a.chest(), dir, {"H": paid / Interactions.HU_PER_PU, "heat_hu": paid})
	FxEvents.fx_for(w, a, inst, "cone", "flame", {"length": rng_m, "angle": cone, "power": paid / Interactions.HU_PER_PU})
	# Water shields in the way take the heat first (steam), protecting their holder.
	var shielded := {}
	for b in w.bodies:
		if not b.alive or not b.is_water() or b.form == Sim.Form.POOL:
			continue
		var to := b.pos - a.chest()
		if to.length() > rng_m or (to.length() > 0.5 and Vector3(to.x, 0, to.z).normalized().dot(dir) < cos(deg_to_rad(cone + 10.0))):
			continue
		if Interactions.is_barrier(w, b):
			Interactions.resolve(w, flame, Agent.of_body(w, b), {"site": "flare"})
		else:
			Interactions.resolve(w, Agent.of_body(w, b, a), flame, {"site": "flare"})
		if b.controller >= 0:
			shielded[b.controller] = true
	for t in w.actors_in_cone(a, dir, rng_m, cone):
		if shielded.has(t.id):
			w.emit("block", {"actor": t.id, "attacker": a.id, "kind": "fire_water"})
			continue
		var info := {"attacker": a.id, "attack_id": inst.attack_id,
			"damage": d.heavy_damage if heavy else d.damage, "balance": d.heavy_balance if heavy else d.balance,
			"knock": dir * (3.0 if heavy else 1.2), "kind": "fire", "from": a.chest(), "agent": flame}
		if t.guarding:
			# Aura guards (wind wraps the fighter) answer regardless of facing.
			var g := Agent.of_guard(w, t)
			if Interactions.rule(flame.cls, g.ccls, g.tier).get("aura", false):
				Interactions.resolve(w, flame, g, {"info": info, "target": t})
				continue
		# A perfect fire guard keeps half the remaining heat (rule absorb_reserve, inside hit_actor).
		w.hit_actor(t, info)
	# Stones in the cone warm up.
	for b in w.bodies:
		if not b.alive or not b.is_stone() or b.form == Sim.Form.WALL or flame.heat <= 0.0:
			continue
		var to := b.pos - a.chest()
		if to.length() < rng_m and (to.length() < 0.5 or Vector3(to.x, 0, to.z).normalized().dot(dir) > cos(deg_to_rad(cone))):
			Interactions.resolve(w, Agent.of_body(w, b, a), flame, {"site": "flare"})
	w.ledger.spent += maxf(0.0, flame.heat)


## Fire Column (T2) and Inferno (T3): a heat cone like the blaze (water takes the heat first, stones warm, fighters
## burn) that leaves a fire field holding field_share of the paid heat (MOVESET §7.9).
static func _column(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var d := inst.def
	var tier := inst.tier()
	var rng_m := float(Charge.param(inst, "col_range", 7.0))
	var cone := float(Charge.param(inst, "col_cone", 10.0))
	var paid := w.pay_heat(a, float(Charge.param(inst, "col_hu", 300.0)))
	var dir: Vector3 = inst.data.face
	var field_hu := paid * float(Charge.param(inst, "field_share", 0.35))
	var vol_hu := paid - field_hu
	w.emit("flare", {"actor": a.id, "dir": dir, "range": rng_m, "heavy": true, "tier": tier, "from_reserve": a.heat_reserve > 0.0})
	var flame := Agent.of_volume(w, a, inst, &"flame", a.chest(), dir, {"H": vol_hu / Interactions.HU_PER_PU, "heat_hu": vol_hu})
	FxEvents.fx_for(w, a, inst, "cone", "flame", {"length": rng_m, "angle": cone, "power": flame.power,
		"shape": "spear" if tier == 2 else "open"})
	var shielded := {}
	for b in w.bodies.duplicate():
		if not b.alive or b.form == Sim.Form.POOL or b.controller == a.id or (b.static_body and b.form != Sim.Form.WALL):
			continue
		var to := b.pos - a.chest()
		var flat := Vector3(to.x, 0, to.z)
		if flat.length() > rng_m + b.radius or (flat.length() > 0.5 and flat.normalized().dot(dir) < cos(deg_to_rad(cone + 6.0))):
			continue
		VerbVolume.meet_body(w, a, flame, b)
		if b.alive and b.is_water() and b.controller >= 0:
			shielded[b.controller] = true
	for t in w.actors_in_cone(a, dir, rng_m, cone):
		if shielded.has(t.id):
			w.emit("block", {"actor": t.id, "attacker": a.id, "kind": "fire_water"})
			continue
		var info := {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "col_damage", 20.0)),
			"balance": float(Charge.param(inst, "col_balance", 40.0)), "knock": dir * float(Charge.param(inst, "knock", 4.0)) + Vector3(0, 1.0, 0),
			"kind": "fire", "from": a.chest(), "agent": flame, "power": flame.power, "tier": tier, "mat": "flame"}
		var res := ""
		if t.guarding:
			var g := Agent.of_guard(w, t)
			if Interactions.rule(flame.cls, g.ccls, g.tier).get("aura", false):
				Interactions.resolve(w, flame, g, {"info": info, "target": t})
				continue
		res = w.hit_actor(t, info)
		if res == "hit" or res == "knockdown":
			Status.apply(w, t, "burning", 1.5 if tier == 2 else 2.5, 1.0, a.id)
	w.ledger.spent += maxf(0.0, flame.heat)
	var fp := a.pos + dir * rng_m * (0.8 if tier == 2 else 0.55)
	if w.arena.segment_hit(a.chest(), fp + Vector3(0, 0.5, 0)) >= 0.0 or w.wall_hit(a.chest(), fp + Vector3(0, 0.5, 0)) >= 0.0:
		fp = a.pos + dir * minf(2.0, rng_m * 0.3)
	var z := FireUtil.spawn_field(w, a.id, fp, float(Charge.param(inst, "field_r", 1.5)), float(Charge.param(inst, "field_life", 2.0)),
		field_hu, false, tier)
	z.props["dps"] = 3.0 if tier == 2 else 4.0
	w.emit("fire_column", {"actor": a.id, "tier": tier, "field": z.id, "hu": paid})


static func _vent(w: CombatWorld, a: ActorState) -> void:
	var e := a.heat_reserve
	a.heat_reserve = 0.0
	w.ledger.vented += e
	w.emit("vent", {"actor": a.id, "amount": e})
