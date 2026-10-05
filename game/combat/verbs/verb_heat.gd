class_name VerbHeat
extends RefCounted
## Verb "ranged_heat" (technique: Scorch / Smelter / Kiln, MOVESET §5.4, §7.9-7.10): heat a wall or a
## body at range without a grip, paid like Fire (reserve first, then Focus at 10 HU/Focus), applied
## through CombatWorld.heat_body (what the target can't take is booked as spent).
## WALL SLUMP: on a wall, a `slump_fraction` (25 %) face is split off on the heated side and heated;
## at liquid >= `slump_at` (0.5) it slumps into a molten body on that side and the rest crumbles
## (2 rubble stones, the remainder sinks) - ready to be poured back (Magma Surge).
## Params: range, cone, rate (HU/s), slump_fraction, slump_at, target_temp (kiln: stop at >= °C), los.


static func start(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	var rng_m := float(Charge.param(inst, "range", 6.0))
	var dir: Vector3 = inst.data.get("aim", a.forward())
	var cone := cos(deg_to_rad(float(Charge.param(inst, "cone", 40.0))))
	var best: MatBody = null
	var bd := INF
	for b in w.bodies:
		if not b.alive or b.form != Sim.Form.WALL or b.wall_rise < 0.3:
			continue
		var to := b.pos - a.pos
		to.y = 0.0
		var d := to.length()
		if d > rng_m + b.wall_half.x or (d > 0.5 and to.normalized().dot(dir) < cone):
			continue
		if d < bd or (is_equal_approx(d, bd) and best != null and b.id < best.id):
			best = b
			bd = d
	if best == null:
		best = w.find_body(a, dir, rng_m, float(Charge.param(inst, "cone", 40.0)), func(b: MatBody) -> bool:
			return b.controller != a.id and b.form != Sim.Form.POOL and b.form != Sim.Form.ZONE and b.mass >= 0.5 \
				and not b.static_body and Interactions.allows(b, &"heat_ranged"))
	if best == null:
		w.emit("whiff", {"actor": a.id, "move": inst.id})
		inst.data["fizzle"] = true
		return
	inst.data["target"] = best.id
	w.emit("telegraph", {"actor": a.id, "move": inst.id, "body": best.id, "time": 0.0})


static func tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if it.tech_cancel or not Charge.held(inst, it):
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var t := w.get_body(int(inst.data.get("target", -1)))
	var face := w.get_body(int(inst.data.get("face_id", -1)))
	if t == null or not t.alive:
		if face != null and face.alive:
			face.static_body = false
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var rng_m := float(Charge.param(inst, "range", 6.0))
	if a.chest().distance_to(t.pos) > rng_m + 2.0:
		w.emit("draw_break", {"actor": a.id, "body": t.id, "reason": "range"})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var to := t.pos - a.pos
	to.y = 0.0
	if to.length() > 0.2:
		inst.data["face"] = to.normalized()
	var want := float(Charge.param(inst, "rate", 300.0)) * Sim.DT
	var paid := w.pay_heat(a, want)
	if paid < want * 0.5:
		if not inst.data.get("starved", false):
			inst.data["starved"] = true
			w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		if paid <= 0.0:
			return
	if t.form == Sim.Form.WALL:
		if face == null or not face.alive:
			face = _split_face(w, a, inst, t)
		var used := w.heat_body(face, paid)
		w.ledger.spent += paid - used
		if w.tick % 6 == 0:
			w.emit("heating", {"actor": a.id, "body": face.id, "liquid": face.liquid, "temp": face.temp, "wall": t.id})
		if face.liquid >= float(Charge.param(inst, "slump_at", 0.5)):
			_slump(w, a, inst, t, face)
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var used2 := w.heat_body(t, paid)
	w.ledger.spent += paid - used2
	t.touch(a.id, "heat", w.tick)
	if w.tick % 6 == 0:
		w.emit("heating", {"actor": a.id, "body": t.id, "liquid": t.liquid, "temp": t.temp})
	var tt := float(Charge.param(inst, "target_temp", 0.0))
	if tt > 0.0 and t.temp >= tt:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)


## The heated shell of a wall: a split child kept in place on the caster's side.
static func _split_face(w: CombatWorld, a: ActorState, inst: ActionInst, wall: MatBody) -> MatBody:
	var n := Vector3(sin(wall.wall_yaw), 0.0, cos(wall.wall_yaw))
	if n.dot(a.pos - wall.pos) < 0.0:
		n = -n
	var m := wall.mass * float(Charge.param(inst, "slump_fraction", 0.25))
	var p := wall.pos + n * (wall.wall_half.z + 0.05) + Vector3(0, wall.wall_half.y, 0)
	var f := w.split_body(wall, m, p)
	f.form = Sim.Form.CHUNK
	f.static_body = true
	f.vel = Vector3.ZERO
	f.props["face_of"] = wall.id
	f.props["face_n"] = n
	f.update_radius()
	inst.data["face_id"] = f.id
	w.emit("wall_face", {"actor": a.id, "wall": wall.id, "body": f.id, "mass": m})
	return f


static func _slump(w: CombatWorld, a: ActorState, inst: ActionInst, wall: MatBody, face: MatBody) -> void:
	var n: Vector3 = face.props.get("face_n", Vector3.FORWARD)
	face.static_body = false
	face.form = Sim.Form.BLOB if face.liquid > 0.0 else Sim.Form.CHUNK
	face.pos = wall.pos + n * (wall.wall_half.z + 0.6)
	face.pos.y = w.arena.ground_height(face.pos.x, face.pos.z, wall.pos.y + 0.4) + 0.05
	face.vel = Vector3.ZERO
	face.on_ground = true
	face.max_life = Sim.REMNANT_LIFETIME
	face.props.erase("face_of")
	inst.data["slumped"] = true
	w.emit("slump", {"actor": a.id, "wall": wall.id, "body": face.id, "mass": face.mass, "liquid": face.liquid})
	w.emit("transform", {"body": face.id, "at": face.pos, "from": "wall", "to": "molten", "why": "slump"})
	Verbs.fx(w, a, inst, "splash", {"pos": face.pos, "body": face.id})
	w._crumble_wall(wall)


## Channel over without a slump: the face rejoins its wall (energy and mass conserved), or falls.
static func end(w: CombatWorld, _a: ActorState, inst: ActionInst) -> void:
	var face := w.get_body(int(inst.data.get("face_id", -1)))
	inst.data.erase("face_id")
	if face == null or not face.alive or inst.data.get("slumped", false):
		return
	var wall := w.get_body(int(face.props.get("face_of", -1)))
	face.props.erase("face_of")
	if wall != null and wall.alive and face.mat == wall.mat:
		w.merge_bodies(wall, face)
		wall.vel = Vector3.ZERO
	else:
		face.static_body = false
