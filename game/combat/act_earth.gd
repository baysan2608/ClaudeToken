class_name ActEarth
extends RefCounted
## Earth: stone shot (tap) / heavy heave (hold), seize-aim-throw technique.
## Verbs: acquire (rip or seize), shape (hold), accelerate (throw), redirect (guard).

const LOOSE_RADIUS := 2.6


static func _stone_filter(b: MatBody) -> bool:
	# Legality through the engine (legacy cells stone* x grip_stone: reclaim).
	return b.form != Sim.Form.WALL and b.phase != Sim.Phase.MOLTEN and b.form != Sim.Form.WAVE and Interactions.allows(b, &"grip_stone")


static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	inst.data["face"] = w.aim_dir(a, _it)
	if inst.id == "earth_attack":
		if not w.spend_focus(a, float(inst.def.cost)):
			_fizzle(w, a, inst, "focus")
			return
		# Prefer a loose stone at the feet (reuse material), else rip one from the ground.
		var loose := _loose_stone_near(w, a)
		if loose != null:
			w.take_control(a, loose, 0.9, "acquire")
		else:
			_rip(w, a, Sim.STONE_SHOT_MASS)
		w.emit("telegraph", {"actor": a.id, "move": "earth_attack", "body": a.held_body, "time": inst.def.startup})


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.id == "earth_attack":
		if inst.data.get("fizzle", false):
			return ActionInst.P.RECOVERY
		return w.attack_after_startup(a, inst, it)
	return ActionInst.P.CHANNEL


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.CHARGE and inst.id == "earth_attack":
		# Heavy: gather more stone from the ground (mass budget, extra Focus).
		var b := w.held(a)
		var extra := float(inst.def.heavy_mass) - (b.mass if b != null else 0.0)
		if b != null and extra > 0.0 and w.spend_focus(a, float(inst.def.heavy_cost) - float(inst.def.cost)):
			# Ground stone arrives at ambient: total heat is unchanged, so temperature dilutes.
			var e := b.thermal_energy()
			b.mass += extra
			w._set_energy(b, e)
			b.update_radius()
			w.mass_ledger.ground_taken += extra
			w.emit("acquire", {"actor": a.id, "body": b.id, "mass": extra})
		elif extra > 0.0:
			# The heave can't be paid: it stays a light shot (like water/air), mass-scaled as usual.
			inst.heavy = false
			if b != null:
				w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "earth_heavy"})
		if inst.heavy:
			w.emit("telegraph", {"actor": a.id, "move": "earth_heavy", "body": a.held_body, "time": inst.def.heavy_min})
	elif p == ActionInst.P.ACTIVE:
		var b := w.held(a)
		if b == null:
			return
		var heavy := inst.heavy
		var spd := float(inst.def.get("heavy_speed", inst.def.speed)) if heavy else float(inst.def.speed)
		var dmg := float(inst.def.get("heavy_damage", inst.def.damage)) if heavy else float(inst.def.damage)
		var bal := float(inst.def.get("heavy_balance", inst.def.balance)) if heavy else float(inst.def.balance)
		var mass_scale := sqrt(b.mass / Sim.STONE_SHOT_MASS) if not heavy else 1.0
		var target := _throw_target(w, a, inst)
		var v := launch_vel(b.pos, target, spd)
		w.release_body(a, v, true, dmg * mass_scale, bal * mass_scale)
		w.emit("launch", {"actor": a.id, "body": b.id, "speed": spd, "heavy": heavy})


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var b := w.held(a)
	match inst.id:
		"earth_attack":
			if not it.attack_held:
				inst.data["released"] = true
			if b != null:
				var rise := clampf(inst.total / maxf(float(inst.def.startup), 0.01), 0.0, 1.0)
				b.hold_point = a.pos + a.forward() * 0.85 + Vector3(0, lerpf(0.35, 1.45, ease(rise, 0.5)), 0)
			inst.data["face"] = w.aim_dir(a, it)
			if inst.phase == ActionInst.P.CHARGE:
				if b == null:
					w.finish_action(a, inst)
					return
				if inst.data.get("released", false) and inst.total >= float(inst.def.heavy_min):
					w.set_phase(a, inst, ActionInst.P.ACTIVE)
		"earth_tech":
			if it.tech_cancel and (inst.phase == ActionInst.P.STARTUP or inst.phase == ActionInst.P.CHANNEL):
				# Honoured from the first frame: a cancel during startup never seizes or throws.
				_drop(w, a)
				w.emit("cancel", {"actor": a.id, "move": inst.id})
				w.set_phase(a, inst, ActionInst.P.RECOVERY)
				return
			if inst.phase != ActionInst.P.CHANNEL:
				return
			inst.data["aim"] = w.aim_dir(a, it)
			inst.data["aim_active"] = it.aim_active
			inst.data["face"] = inst.data.aim
			if b == null:
				_seek(w, a, inst, it)
				if inst.phase == ActionInst.P.CHANNEL and not it.tech_held and w.held(a) == null:
					w.emit("whiff", {"actor": a.id, "move": inst.id})
					w.set_phase(a, inst, ActionInst.P.RECOVERY)
				return
			var dir: Vector3 = inst.data.aim
			# Held at chest height between the hands (earth_hold), nudged toward the aim.
			b.hold_point = a.pos + Vector3(0, 1.2, 0) + a.forward() * (0.3 + b.radius) + dir * 0.15
			if not it.tech_held:
				w.set_phase(a, inst, ActionInst.P.ACTIVE)
				_throw_held(w, a, inst)


static func on_interrupt(w: CombatWorld, a: ActorState, _inst: ActionInst, _reason: String) -> void:
	_drop(w, a)


# ---------------------------------------------------------------------------

static func _seek(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var reach := float(inst.def.reach)
	var tid: int = inst.data.get("target", -1)
	var tb := w.get_body(tid)
	if tb == null or not tb.alive or not _stone_filter(tb) or tb.controller == a.id:
		tb = w.find_body(a, w.aim_dir(a, it), reach, 65.0, _stone_filter)
		if tb != null:
			inst.data["target"] = tb.id
			w.emit("target_body", {"actor": a.id, "body": tb.id})
	if tb != null:
		if tb.mass > a.max_control_mass:
			# Too heavy: the technique visibly fails (whiff -> recovery). It does not go on to
			# rip a ground stone instead; that is only for when there is nothing to seize.
			w.request_grip(a, tb, 0.0, "seize")   # emits control_fail (mass)
			w.emit("whiff", {"actor": a.id, "move": inst.id, "body": tb.id})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
			return
		var s := w.grip_strength(a, tb, 0.85, reach)
		w.request_grip(a, tb, s, "seize")
		return
	# Nothing to seize: rip a stone out of the ground after a short pull.
	if inst.t >= float(inst.def.rip_time):
		if w.spend_focus(a, float(inst.def.cost)):
			_rip(w, a, Sim.STONE_SHOT_MASS)
		else:
			w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func _throw_held(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.held(a)
	if b == null:
		return
	var mass_scale := sqrt(b.mass / Sim.STONE_SHOT_MASS)
	var target := _throw_target(w, a, inst)
	var spd := float(inst.def.speed) / maxf(1.0, mass_scale * 0.8)
	var v := launch_vel(b.pos, target, spd)
	w.release_body(a, v, true, float(inst.def.damage) * mass_scale, float(inst.def.balance) * mass_scale)
	w.emit("launch", {"actor": a.id, "body": b.id, "speed": spd})


static func _throw_target(w: CombatWorld, a: ActorState, inst: ActionInst) -> Vector3:
	var t := w.get_actor(a.lock_target)
	if inst.data.get("aim_active", false):
		var dir: Vector3 = inst.data.aim
		# Aim drag picks a direction; if the lock target is roughly there, still lead it.
		if t != null:
			var to := t.pos - a.pos
			to.y = 0
			if to.normalized().dot(dir) > 0.97:
				return t.chest() + t.vel * 0.25
		return a.chest() + dir * 14.0
	if t != null:
		return t.chest() + Vector3(t.vel.x, 0, t.vel.z) * 0.2
	return a.chest() + a.forward() * 14.0


static func _rip(w: CombatWorld, a: ActorState, mass: float) -> MatBody:
	var p := a.pos + a.forward() * 0.85
	p.y = w.arena.ground_height(p.x, p.z, a.pos.y) - 0.1
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, mass, p, "ground@%.1f,%.1f" % [p.x, p.z])
	w.mass_ledger.ground_taken += mass
	b.max_life = Sim.REMNANT_LIFETIME
	w.take_control(a, b, 0.9, "rip")
	w.emit("rip", {"actor": a.id, "body": b.id})
	return b


static func _loose_stone_near(w: CombatWorld, a: ActorState) -> MatBody:
	var best: MatBody = null
	for b in w.bodies:
		if not b.alive or not _stone_filter(b) or b.controller >= 0 or b.attack_id != 0:
			continue
		if b.mass > 25.0 or not b.on_ground:
			continue
		var d := Vector2(b.pos.x - a.pos.x, b.pos.z - a.pos.z).length()
		if d < LOOSE_RADIUS and (best == null or b.id < best.id):
			best = b
	return best


static func _drop(w: CombatWorld, a: ActorState) -> void:
	if w.held(a) != null:
		w.release_body(a, Vector3(0, -1.0, 0), false)


static func _fizzle(w: CombatWorld, a: ActorState, inst: ActionInst, what: String) -> void:
	inst.data["fizzle"] = true
	w.emit("insufficient", {"actor": a.id, "what": what, "move": inst.id})


## Launch velocity that reaches `to` from `from` with the given horizontal speed.
static func launch_vel(from: Vector3, to: Vector3, h_speed: float) -> Vector3:
	var d := to - from
	var flat := Vector3(d.x, 0, d.z)
	var dist := flat.length()
	if dist < 0.01:
		return Vector3(0, h_speed, 0)
	var t := dist / h_speed
	var vy := d.y / t + 0.5 * Sim.GRAVITY * t
	return flat / dist * h_speed + Vector3(0, vy, 0)
