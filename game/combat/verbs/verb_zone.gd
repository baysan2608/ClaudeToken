class_name VerbZone
extends RefCounted
## Verbs "zone" (a ZONE body at self / feet / ahead / aim) and "summon" (a zone at the aim point
## whose tier grows with the technique hold, steered while held; release lets it linger).
## Params: tag, radius, life, power, channel, mat, mass + source (sand cloud from the ground ...), at
## (self feet ahead aim), distance, range, attach (follows the caster), height, walk_height, friction,
## surface, actor_status, status_t, status_mag, dps, ground_only, rate, barrier, ccls, cls,
## walk_speed (toward the caster's target), spare_owner, drag.
## summon adds: steer_speed (m/s toward the aim point while held), linger (s after release), upkeep (Focus/s).

const ZONE_PROPS := ["height", "walk_height", "friction", "surface", "actor_status", "status_t", "status_mag", "dps",
	"ground_only", "rate", "barrier", "ccls", "cls", "walk_speed", "spare_owner", "drag", "channel"]


static func _point(w: CombatWorld, a: ActorState, inst: ActionInst) -> Vector3:
	var at := String(Charge.param(inst, "at", "ahead"))
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	var p := a.pos
	match at:
		"ahead":
			p = a.pos + dir * float(Charge.param(inst, "distance", 3.0))
		"aim":
			var ap: Vector3 = inst.data.get("aim_point", a.chest() + dir * 8.0)
			var rmax := float(Charge.param(inst, "range", 10.0))
			var flat := Vector3(ap.x - a.pos.x, 0, ap.z - a.pos.z)
			if flat.length() > rmax:
				flat = flat.normalized() * rmax
			p = a.pos + flat
	p.y = w.arena.ground_height(p.x, p.z, a.pos.y + 0.5)
	return p


static func spawn(w: CombatWorld, a: ActorState, inst: ActionInst, over: Dictionary = {}) -> MatBody:
	var P := func(k: String, dflt: Variant) -> Variant:
		return over[k] if over.has(k) else Charge.param(inst, k, dflt)
	var p: Vector3 = over.get("pos", _point(w, a, inst))
	var mat := Verbs.mat_id(P.call("mat", "air"))
	var mass := float(P.call("mass", 0.0))
	var source := String(P.call("source", "none"))
	if mass > 0.0:
		match source:
			"ground":
				w.mass_ledger.ground_taken += mass
			"waterskin":
				mass = minf(mass, a.water_carried)
				a.water_carried -= mass
			"moisture":
				w.mass_ledger.moisture_taken += mass
			"metal":
				mass = minf(mass, a.metal_carried)
				a.metal_carried -= mass
			_:
				if mat != Sim.Mat.AIR and mat != Sim.Mat.FIRE:
					mass = 0.0   # material zones need a booked source
	var z := w.spawn_zone(StringName(String(P.call("tag", "zone"))), p, float(P.call("radius", 2.0)), a.id,
		float(P.call("power", 0.0)), mat, mass, float(P.call("life", 3.0)), "%s:%d" % [source, a.id])
	z.tier = inst.tier()
	z.sub = inst.sub
	for k in ZONE_PROPS:
		var v: Variant = P.call(k, null)
		if v != null:
			z.props[k] = v
	if bool(P.call("attach", false)):
		z.props["attach"] = a.id
		z.props["attach_off"] = p - a.pos
	if z.props.has("walk_speed"):
		z.props["walk_target"] = a.lock_target
	if mat == Sim.Mat.FIRE:
		z.heat_payload = Verbs.take_heat(inst)
	Verbs.fx(w, a, inst, "ring", {"pos": p, "radius": z.zone_radius, "body": z.id, "power": z.power})
	inst.data["zone"] = z.id
	return z


static func summon_start(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	var z := spawn(w, a, inst)
	z.max_life = -1.0
	inst.data["summon"] = z.id


static func summon_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var z := w.get_body(int(inst.data.get("summon", -1)))
	if z == null or not z.alive:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	if it.tech_cancel or not Charge.held(inst, it):
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var upkeep := float(Charge.param(inst, "upkeep", 0.0)) * Sim.DT
	if upkeep > 0.0 and not w.spend_focus(a, upkeep):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	# Tier from the technique hold (Charge.tick): radius and power follow the tier data.
	z.tier = inst.tier()
	z.zone_radius = float(Charge.param(inst, "radius", z.zone_radius))
	z.radius = z.zone_radius
	z.power = float(Charge.param(inst, "power", z.power))
	inst.data["aim"] = w.aim_dir(a, it)
	inst.data["aim_point"] = w.aim_point(a, it)
	var steer := float(Charge.param(inst, "steer_speed", 0.0))
	if steer > 0.0:
		var to := _point(w, a, inst) - z.pos
		to.y = 0.0
		if to.length() > 0.1:
			z.vel = to.normalized() * minf(steer, to.length() / Sim.DT)
			z.pos += z.vel * Sim.DT
			z.pos.y = w.arena.ground_height(z.pos.x, z.pos.z, z.pos.y + 0.5)


static func summon_release(w: CombatWorld, _a: ActorState, inst: ActionInst) -> void:
	var z := w.get_body(int(inst.data.get("summon", -1)))
	inst.data.erase("summon")
	if z == null or not z.alive:
		return
	z.max_life = z.age + float(Charge.param(inst, "linger", 2.0))
	z.vel = Vector3.ZERO
	if z.props.has("walk_speed"):
		z.props["walk_target"] = int(z.props.get("walk_target", -1))
	w.emit("summon_release", {"body": z.id, "tier": z.tier})
