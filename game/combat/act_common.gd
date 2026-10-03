class_name ActCommon
extends RefCounted
## Evade, air dash and guard (with the Earth wall and Water shield variants).

const WALL_COST := 8.0
const WALL_DIST := 1.25


static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"evade", "air_dash":
			var dir := it.move
			dir.y = 0.0
			if dir.length() < 0.2:
				dir = -a.forward()   # neutral evade = backstep
			dir = dir.normalized()
			inst.data["dir"] = dir
			inst.data["controls_motion"] = true
			inst.data["face"] = a.forward()
			w.spend_focus(a, minf(a.focus, float(inst.def.cost)))
			a.iframes = float(inst.def.iframes)
			# Which clip: relative to facing.
			var f := a.forward()
			var r := f.cross(Vector3.UP)
			var fd := dir.dot(f)
			var rd := dir.dot(r)
			var side := "back"
			if absf(fd) >= absf(rd):
				side = "fwd" if fd > 0.0 else "back"
			else:
				side = "r" if rd < 0.0 else "l"
			inst.data["side"] = side
			w.emit("evade", {"actor": a.id, "dir": dir, "side": side, "dash": inst.id == "air_dash"})
		"guard":
			var since := float(w.tick - a.guard_press_tick) * Sim.DT
			a.guard_press_tick = w.tick
			a.guarding = true
			if since < Moves.GUARD_MASH_LOCK:
				inst.data["mashed"] = true   # mashing guard never opens a perfect window
			a.guard_tick = w.tick
			if a.element == Sim.Element.EARTH and a.grounded:
				_raise_wall(w, a, inst)
			else:
				a.wall_body = -1   # only a grounded Earth guard keeps a wall: the previous one sinks
				if a.element == Sim.Element.WATER:
					_water_shield(w, a, inst)
			# Held material (water kept from a cancelled technique) that did not become the
			# shield is dropped, like any interrupted action's material.
			if w.held(a) != null and not inst.data.get("shield", false):
				w.release_body(a, Vector3(0, -1, 0), false)
			w.emit("guard", {"actor": a.id, "element": a.element, "wall": a.wall_body})


static func after_startup(_w: CombatWorld, _a: ActorState, inst: ActionInst, _it: ActorIntent) -> int:
	if inst.id == "guard":
		return ActionInst.P.CHANNEL
	return ActionInst.P.ACTIVE


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if inst.id == "guard" and p == ActionInst.P.RECOVERY:
		_end_guard(w, a, inst)
	if (inst.id == "evade" or inst.id == "air_dash") and p == ActionInst.P.RECOVERY:
		inst.data["controls_motion"] = false


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"evade", "air_dash":
			if inst.phase == ActionInst.P.ACTIVE:
				var dur := float(inst.def.active)
				var dist := float(inst.def.distance)
				var x := clampf(inst.t / dur, 0.0, 1.0)
				# Ease-out velocity profile; integrates to `dist` over the active window.
				var spd := 2.0 * dist / dur * (1.0 - x)
				var dir: Vector3 = inst.data.dir
				a.vel.x = dir.x * spd
				a.vel.z = dir.z * spd
		"guard":
			if inst.phase == ActionInst.P.CHANNEL:
				var shield := w.held(a)
				if shield != null:
					shield.hold_point = a.chest() + a.forward() * 0.75
				if not it.guard_held:
					w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, _reason: String) -> void:
	if inst.id == "guard":
		_end_guard(w, a, inst)


static func _end_guard(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	a.guarding = false
	if inst.data.get("shield", false):
		var b := w.held(a)
		if b != null and b.is_water():
			# Shield water returns to the waterskin; overflow falls as a puddle.
			var back := minf(b.mass, 6.0 - a.water_carried)
			# The waterskin holds water at ambient: the heat (or cold) of the returned kg leaves the ledger.
			w.ledger.removed += back * (Sim.WATER_C * (b.temp - Sim.AMBIENT_C) - Sim.WATER_LATENT_FUSION * (1.0 - b.liquid))
			a.water_carried += back
			b.mass -= back
			if b.mass <= 0.01:
				a.held_body = -1
				b.controller = -1
				w.decay_body(b, "absorbed")   # books any sliver left over
			else:
				w.release_body(a, Vector3.ZERO, false)
		inst.data["shield"] = false


static func _raise_wall(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var old := w.get_body(a.wall_body)
	if old != null and old.alive:
		# One wall at a time: the old wall stops being maintained and sinks.
		a.wall_body = -1
	if not w.spend_focus(a, WALL_COST):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "wall"})
		return
	var p := a.pos + a.forward() * WALL_DIST
	p.y = w.arena.ground_height(p.x, p.z, a.pos.y)
	if absf(p.y - a.pos.y) > 0.3:
		return   # no flat ground in front: plain guard
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, Sim.WALL_MASS, p, "ground@%.1f,%.1f" % [p.x, p.z])
	w.mass_ledger.ground_taken += Sim.WALL_MASS
	b.wall_yaw = a.facing
	b.wall_half = Vector3(1.1, 0.75, 0.28)
	b.wall_rise = 0.0
	b.static_body = true
	b.touch(a.id, "wall", w.tick)
	a.wall_body = b.id
	inst.data["wall"] = b.id
	w.emit("wall", {"actor": a.id, "body": b.id})


static func _water_shield(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.held(a)
	if b == null and a.water_carried >= 1.5:
		b = w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, a.water_carried, a.chest() + a.forward() * 0.75, "waterskin:%d" % a.id)
		a.water_carried = 0.0
		w.take_control(a, b, 0.9, "shield")
	if b != null and b.is_water() and b.phase == Sim.Phase.LIQUID:
		b.form = Sim.Form.BLOB
		inst.data["shield"] = true
		w.emit("shield", {"actor": a.id, "body": b.id})
