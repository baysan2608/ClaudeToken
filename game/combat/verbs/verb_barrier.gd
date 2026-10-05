class_name VerbBarrier
extends RefCounted
## Verb "barrier": the sub-element guard spec (the action stays "guard"; inst.data.spec_def is the
## spec). barrier = wall | held | aura | zone:
##   wall: a WALL body (mat, tag, mass, hardness, half: Vector3 extents, dist, rise time) raised in front;
##         counter class from the wall (Interactions.counter_class) unless counter.cls is given
##   held: a body held at the chest (mat, tag, mass, source: waterskin | metal | ground | moisture)
##   aura: nothing spawned; the actor's guard class is counter.cls with counter.power[tier]
##   zone: a ZONE attached to the fighter (tag, radius, height, props.barrier: stops bolts)
## Guards thicken with hold time: on a tier-up the wall / held body grows to Charge.param("mass")
## (booked from its source). upkeep: Focus/s while up. Push/sink moves started from the guard keep
## the wall (keep_wall) and the held body.


static func _spec(inst: ActionInst) -> Dictionary:
	return inst.data.get("spec_def", {})


static func start(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	var spec := _spec(inst)
	if not Verbs.pay(w, a, inst, "start"):
		inst.data["barrier_failed"] = true
		return
	var kind := String(spec.get("barrier", "aura"))
	inst.data["bt"] = 0
	match kind:
		"wall":
			if a.grounded:
				_raise_wall(w, a, inst, spec)
		"held":
			_take_held(w, a, inst, spec)
		"zone":
			var z := w.spawn_zone(StringName(String(spec.get("tag", "barrier"))), a.pos, float(Charge.param(inst, "radius", 1.8)), a.id,
				float(Charge.counter_power(spec, 0)), Verbs.mat_id(spec.get("mat", "air")), 0.0, -1.0)
			z.props["attach"] = a.id
			z.props["barrier"] = bool(spec.get("stops_bolts", true))
			z.props["height"] = float(spec.get("height", 2.4))
			if spec.get("counter", {}).has("cls"):
				z.props["ccls"] = String(spec.counter.cls)
			inst.data["zone"] = z.id
	Verbs.fx(w, a, inst, "aura", {"on": true})


static func _raise_wall(w: CombatWorld, a: ActorState, inst: ActionInst, spec: Dictionary) -> void:
	var mass := float(Charge.param(inst, "mass", 100.0))
	var p := a.pos + a.forward() * float(spec.get("dist", ActCommon.WALL_DIST))
	p.y = w.arena.ground_height(p.x, p.z, a.pos.y)
	if absf(p.y - a.pos.y) > 0.3:
		return   # no flat ground in front: guard without a wall
	var mat := Verbs.mat_id(spec.get("mat", "stone"))
	var source := String(spec.get("source", "ground"))
	if not _take_source(w, a, source, mass):
		w.emit("insufficient", {"actor": a.id, "what": source, "move": inst.data.get("spec", "")})
		return
	var b := w.spawn_body(mat, Sim.Form.WALL, mass, p, "%s@%.1f,%.1f" % [source, p.x, p.z])
	if mat == Sim.Mat.WATER:
		var e0 := b.thermal_energy()
		b.liquid = 0.0
		b.temp = -5.0
		b.phase = Sim.Phase.FROZEN
		w.ledger.freeze_dump += b.thermal_energy() - e0   # freezing dumps the water's heat (water technique rule)
	b.tag = StringName(String(spec.get("tag", "")))
	b.wall_yaw = a.facing
	b.wall_half = spec.get("half", Vector3(1.1, 0.75, 0.28))
	b.wall_rise = 0.0
	b.static_body = true
	b.sub = inst.sub
	b.props["rise_time"] = float(spec.get("rise", 0.14))
	b.props["source"] = source
	if spec.has("hardness"):
		b.hardness = float(spec.hardness)
	if spec.get("counter", {}).has("cls"):
		b.props["ccls"] = String(spec.counter.cls)
	b.touch(a.id, "wall", w.tick)
	a.wall_body = b.id
	inst.data["wall"] = b.id
	w.emit("wall", {"actor": a.id, "body": b.id, "tag": String(b.tag)})


## Books `mass` of a barrier's material from its source. False when the source is short.
static func _take_source(w: CombatWorld, a: ActorState, source: String, mass: float) -> bool:
	match source:
		"ground":
			w.mass_ledger.ground_taken += mass
		"waterskin":
			if a.water_carried + 1e-6 < mass:
				return false
			a.water_carried -= mass
		"metal":
			if a.metal_carried + 1e-6 < mass:
				return false
			a.metal_carried -= mass
		"moisture":
			w.mass_ledger.moisture_taken += mass
	return true


static func _give_back(w: CombatWorld, a: ActorState, source: String, b: MatBody, kg: float) -> void:
	match source:
		"ground":
			w.mass_ledger.ground_returned += kg
		"waterskin":
			a.water_carried += kg
		"metal":
			a.metal_carried += kg
		"moisture":
			w.mass_ledger.moisture_taken -= kg
	w.ledger.removed += b.thermal_energy() * kg / maxf(b.mass, 1e-9)
	b.mass -= kg


static func _take_held(w: CombatWorld, a: ActorState, inst: ActionInst, spec: Dictionary) -> void:
	var mat := Verbs.mat_id(spec.get("mat", "water"))
	var source := String(spec.get("source", "waterskin"))
	var b := w.held(a)
	if b != null and b.mat != mat:
		w.release_body(a, Vector3(0, -1, 0), false)
		b = null
	if b == null:
		var mass := float(Charge.param(inst, "mass", 6.0))
		if source == "waterskin":
			mass = minf(mass, a.water_carried)
		elif source == "metal":
			mass = minf(mass, a.metal_carried)
		if mass < 0.5 or not _take_source(w, a, source, mass):
			w.emit("insufficient", {"actor": a.id, "what": source, "move": inst.data.get("spec", "")})
			return
		b = w.spawn_body(mat, Sim.Form.BLOB, mass, a.chest() + a.forward() * 0.75, "%s:%d" % [source, a.id])
		w.take_control(a, b, 0.9, "shield")
	b.tag = StringName(String(spec.get("tag", String(b.tag))))
	if spec.has("hardness"):
		b.hardness = float(spec.hardness)
	b.props["source"] = source
	if spec.get("counter", {}).has("cls"):
		b.props["ccls"] = String(spec.counter.cls)
	inst.data["held_barrier"] = b.id
	if mat == Sim.Mat.WATER:
		inst.data["shield"] = true   # legacy shield semantics (steam block, conduction)
	w.emit("shield", {"actor": a.id, "body": b.id, "tag": String(b.tag)})


static func tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.CHANNEL:
		return
	var spec := _spec(inst)
	var upkeep := float(Charge.param(inst, "upkeep", 0.0)) * Sim.DT
	if upkeep > 0.0 and not w.spend_focus(a, upkeep):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.data.get("spec", "")})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var hb := w.get_body(int(inst.data.get("held_barrier", -1)))
	if hb != null and hb.alive and hb.controller == a.id:
		hb.hold_point = a.chest() + a.forward() * 0.75
	# Thicken with hold time (tier-ups from Charge.tick).
	var t := inst.tier()
	if t > int(inst.data.get("bt", 0)):
		inst.data["bt"] = t
		var want := float(Charge.param(inst, "mass", 0.0))
		var body := w.get_body(int(inst.data.get("wall", -1)))
		if body == null:
			body = hb
		if body != null and body.alive and want > body.mass:
			var extra := want - body.mass
			var src := String(body.props.get("source", spec.get("source", "ground")))
			if _take_source(w, a, src, extra):
				var e := body.thermal_energy()
				body.mass += extra
				w._set_energy(body, e)   # new material arrives at ambient: total heat unchanged
				if body.form != Sim.Form.WALL:
					body.update_radius()
				w.emit("barrier_grow", {"actor": a.id, "body": body.id, "mass": body.mass, "tier": t})
		var z := w.get_body(int(inst.data.get("zone", -1)))
		if z != null and z.alive:
			z.zone_radius = float(Charge.param(inst, "radius", z.zone_radius))
			z.radius = z.zone_radius
			z.power = maxf(z.power, Charge.counter_power(spec, t))


## Guard released / interrupted: held material goes back to its source (or drops), zones close;
## a push/sink started from the guard keeps them (reason push / sink).
static func end(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	if inst.data.get("barrier_ended", false):
		return
	inst.data["barrier_ended"] = true
	Verbs.fx(w, a, inst, "aura", {"on": false})
	w.ledger.spent += Verbs.take_heat(inst)
	var keep := reason == "push" or reason == "sink"
	var z := w.get_body(int(inst.data.get("zone", -1)))
	if z != null and z.alive and not keep:
		w.close_zone(z, "guard_end")
	var hb := w.get_body(int(inst.data.get("held_barrier", -1)))
	if hb != null and hb.alive and hb.controller == a.id and not keep:
		var src := String(hb.props.get("source", "waterskin"))
		if src == "waterskin" and hb.is_water():
			var back := minf(hb.mass, 6.0 - a.water_carried)
			_give_back(w, a, src, hb, back)
		elif src != "waterskin":
			_give_back(w, a, src, hb, hb.mass)
		if hb.mass <= 0.01:
			a.held_body = -1
			hb.controller = -1
			w.decay_body(hb, "returned")
		else:
			w.release_body(a, Vector3(0, -1, 0), false)
		inst.data["shield"] = false


## A free-standing barrier from an attack/sink move (spikes, ridge): verb "barrier" outside a guard.
static func raise_free(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var spec := inst.def
	var mass := float(Charge.param(inst, "mass", 60.0))
	var p := a.pos + a.forward() * float(Charge.param(inst, "dist", 2.0))
	p.y = w.arena.ground_height(p.x, p.z, a.pos.y)
	var source := String(spec.get("source", "ground"))
	if not _take_source(w, a, source, mass):
		return
	var b := w.spawn_body(Verbs.mat_id(spec.get("mat", "stone")), Sim.Form.WALL, mass, p, "%s@%.1f,%.1f" % [source, p.x, p.z])
	b.tag = StringName(String(Charge.param(inst, "tag", "spikes")))
	b.wall_yaw = a.facing
	b.wall_half = Charge.param(inst, "half", Vector3(1.1, 0.5, 0.3))
	b.static_body = true
	b.props["standing"] = float(Charge.param(inst, "life", 1.5))
	b.props["rise_time"] = float(Charge.param(inst, "rise", 0.1))
	b.props["source"] = source
	if spec.has("hardness"):
		b.hardness = float(spec.hardness)
	b.touch(a.id, "wall", w.tick)
	Verbs.fx(w, a, inst, "erupt", {"pos": p, "body": b.id})
