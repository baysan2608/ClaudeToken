class_name AirOutcomes
extends RefCounted
## Custom outcomes of the Gust column (prefix "air_"). Signature (like Outcomes): (w, threat, counter, res, rule, ctx) -> bool,
## false = declined (the rule's fallback runs). Each handler reports a catalogued outcome name in res.outcome (the
## interaction event) and moves heat / mass only through the CombatWorld ledger helpers.
##   air_cool    convective cooling of a hot body (booked ambient), then a core outcome per band (rule then_full/partial/fail)
##   air_cut     a crescent cuts vines: a loose vine is halved (the cut half returns to the plant ledger), a vine wall takes damage
##   air_split   a water wave struck by a hurricane splits into two narrower waves fanned apart
##   air_shrink  a partial against a zone (tornado, vacuum): it loses CP_eff of its power and shrinks
##   air_shatter the core shatter, once: the pieces are marked (props.shattered) so a volume that sweeps a list of bodies
##               the shatter just extended does not shatter the fragments again (and again)


static func register() -> void:
	Interactions.register_outcome("air_cool", Callable(AirOutcomes, "o_cool"))
	Interactions.register_outcome("air_cut", Callable(AirOutcomes, "o_cut"))
	Interactions.register_outcome("air_split", Callable(AirOutcomes, "o_split"))
	Interactions.register_outcome("air_shrink", Callable(AirOutcomes, "o_shrink"))
	Interactions.register_outcome("air_shatter", Callable(AirOutcomes, "o_shatter"))


static func report(res: Dictionary, outcome: String, to: String = "") -> void:
	res["outcome"] = outcome
	if to != "":
		res["to"] = to


## Fraction of the threat left after the counter took `absorb` x CP_eff.
static func left(res: Dictionary, absorb: float = 1.0) -> float:
	var tp := float(res.tp)
	if tp <= 1e-6:
		return 0.0
	return clampf((tp - absorb * float(res.cp_eff)) / tp, 0.0, 1.0)


static func o_cool(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b != null and b.alive and b.mat != Sim.Mat.FIRE and b.thermal_energy() > 0.0:
		var want := float(res.cp_eff) * Interactions.HU_PER_PU * float(r.get("cool", 0.5))
		var got := -Thermal.heat(b, -minf(want, b.thermal_energy()))
		w.ledger.ambient -= got          # convective cooling by wind: booked as ambient
		res.heat_used = float(res.heat_used) + got
	var then := String(r.get("then_" + String(res.band), r.get("then", "deflect")))
	var ok := Outcomes.apply(w, then, t, c, res, r, ctx)
	report(res, then)
	return ok


static func o_cut(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or b.mat != Sim.Mat.PLANT:
		return false
	if b.form == Sim.Form.WALL:
		var cpw := maxf(1.0, b.mass * Materials.hardness(b))
		b.wall_damage_add(float(res.cp_eff) / cpw * float(r.get("wall_k", 0.6)))
		w.emit("wall_cut", {"body": b.id, "by": Outcomes._id(c), "damage": b.wall_damage})
		if b.wall_damage >= 1.0:
			var owner := w.get_actor(b.last_actor)
			if owner != null and owner.wall_body == b.id:
				owner.wall_body = -1
			w.decay_body(b, "cut")           # the woven vines fall apart: plant_returned
		res.stopped = false
		res.pass_scale = 1.0
	else:
		var cut := w.split_body(b, b.mass * 0.5, b.pos)
		cut.attack_id = 0
		w.decay_body(cut, "cut")             # the severed half returns to the plant ledger
		b.attack_id = 0
		b.vel *= 0.3
		res.stopped = true
		res.pass_scale = 0.0
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "vine", "to": "cut", "why": "cut"})
	report(res, "transform", "cut")
	return true


static func o_split(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or b.form != Sim.Form.WAVE or b.mass < 2.0:
		return false
	var ang := deg_to_rad(float(r.get("angle", 28.0)))
	var child := w.split_body(b, b.mass * 0.5, b.pos)
	child.form = Sim.Form.WAVE
	child.props = b.props.duplicate(true)
	child.wave_dir = b.wave_dir.rotated(Vector3.UP, ang)
	b.wave_dir = b.wave_dir.rotated(Vector3.UP, -ang)
	child.wave_budget = b.wave_budget * 0.6
	b.wave_budget *= 0.6
	child.wave_width = b.wave_width * 0.7
	b.wave_width *= 0.7
	child.wave_path = PackedVector3Array([child.pos])
	child.max_life = -1.0
	child.attack_id = w.new_attack_id()
	child.attack_owner = b.attack_owner
	child.hit_set = b.hit_set.duplicate()
	child.damage = b.damage * 0.6
	child.balance_damage = b.balance_damage * 0.6
	child.vel = child.wave_dir * b.vel.length()
	b.damage *= 0.6
	b.balance_damage *= 0.6
	res.stopped = false
	res.pass_scale = 0.6
	report(res, "weaken")
	w.emit("split_wave", {"body": b.id, "child": child.id})
	return true


static func o_shrink(_w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var f := left(res)
	res.pass_scale = f
	res.stopped = false
	var z := t.body
	if z != null and z.alive and (z.form == Sim.Form.ZONE or z.form == Sim.Form.CLOUD):
		z.power *= f
		if z.zone_radius > 0.0:
			z.zone_radius = maxf(0.8, z.zone_radius * sqrt(maxf(f, 0.05)))
			z.radius = z.zone_radius
	report(res, "weaken")
	return true


static func o_shatter(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive:
		return Outcomes.shatter(w, t, c, res, r, ctx)
	if b.props.get("shattered", false):
		return false                         # fragments are not shattered again
	var n0 := w.bodies.size()
	var ok := Outcomes.shatter(w, t, c, res, r, ctx)
	b.props["shattered"] = true
	b.gravity_scale = 1.0                        # a stone that was flying (gravity 0) falls once it is broken
	for i in range(n0, w.bodies.size()):
		w.bodies[i].props["shattered"] = true
		w.bodies[i].gravity_scale = 1.0
	report(res, "shatter")
	return ok
