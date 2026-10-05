class_name VerbGroundLine
extends RefCounted
## Verb "ground_line": a travelling WAVE-form body with a tag (MOVESET §15.7) that follows the lava-wave
## ground rules (CombatWorld._update_wave): climbs <= 0.3 m, drops off ledges (-1 m budget), walls and
## barriers answer it through the rules, other waves clash, the pool/puddles meet it through the rules.
## Params: tag (water_wave sand_surge rime fire_line ground_current roots tremor dust_line wind_wall funnel
## magma_rift spike_line ...), mat, source (ground waterskin moisture heat none held), mass, speed, budget,
## width, damage, balance, knock, lift, steer (deg/s toward the owner's target), kind (hit kind),
## leave_zone (tag spawned where it ends) + zone_radius / zone_life / zone_power, trail_zone (tag dropped
## along the path) + trail_radius / trail_life, hit_status, hit_status_t, distance (start ahead, 1.3 m).


static func launch(w: CombatWorld, a: ActorState, inst: ActionInst, over: Dictionary = {}) -> MatBody:
	var P := func(k: String, dflt: Variant) -> Variant:
		return over[k] if over.has(k) else Charge.param(inst, k, dflt)
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	dir.y = 0.0
	dir = dir.normalized() if dir.length() > 0.01 else a.forward()
	var ps := ActFire._pour_start(w, a, dir)
	var start: Vector3 = ps.pos
	var source := String(P.call("source", "none"))
	var mat := Verbs.mat_id(P.call("mat", VerbProjectile._source_mat(source)))
	var mass := float(P.call("mass", 8.0))
	var b: MatBody = null
	if source == "held":
		b = w.held(a)
		if b == null:
			b = w.get_body(int(inst.data.get("morph_body", -1)))
		if b == null or not b.alive:
			w.emit("insufficient", {"actor": a.id, "what": "material", "move": inst.id})
			return null
		if b.controller == a.id:
			a.held_body = -1
		b.controller = -1
	else:
		var heat := 0.0
		if source == "heat":
			heat = Verbs.take_heat(inst)
		b = VerbProjectile.spawn(w, a, inst, source, mat, mass, heat, P)
		if b == null:
			return null
	b.form = Sim.Form.WAVE
	b.tag = StringName(String(P.call("tag", "wave")))
	b.pos = start
	b.vel = Vector3.ZERO
	b.wave_dir = dir
	b.wave_budget = float(P.call("budget", 10.0))
	b.wave_width = float(P.call("width", 2.0))
	b.wave_path = PackedVector3Array([start])
	b.max_life = -1.0
	b.age = 0.0
	b.gravity_scale = 0.0
	b.props["speed"] = float(P.call("speed", 9.0))
	b.props["steer"] = float(P.call("steer", 0.0))
	b.props["knock"] = float(P.call("knock", 4.0))
	b.props["lift"] = float(P.call("lift", 3.0))
	for key in ["kind", "leave_zone", "zone_radius", "zone_life", "zone_power", "trail_zone", "trail_radius", "trail_life",
			"hit_status", "hit_status_t", "power", "channel", "viscous"]:
		var v: Variant = P.call(key, null)
		if v != null:
			b.props[key] = v
	if b.props.has("power"):
		b.power = float(b.props.power)
	if mat == Sim.Mat.WATER and b.phase == Sim.Phase.LIQUID:
		b.form = Sim.Form.WAVE
	Verbs.arm(w, a, inst, b, float(P.call("damage", 10.0)), float(P.call("balance", 30.0)))
	b.residual_authority = 0.0
	b.residual_owner = -1
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "ground", "to": "wave", "why": "ground_line"})
	Verbs.fx(w, a, inst, "release", {"body": b.id, "pos": start, "dir": dir, "length": b.wave_budget, "radius": b.wave_width})
	inst.data["bodies"] = [b.id]
	if ps.blocked:
		w.emit("wave_blocked", {"body": b.id, "at": start})
		w._settle_wave(b, "blocked")
	return b


## The wave ended: leave its zone, then its material settles (earth stays as a remnant, water becomes
## a puddle, fire/air/plant energy fades into its zone or away).
static func on_end(w: CombatWorld, b: MatBody, _why: String) -> void:
	if b.props.has("leave_zone"):
		var z := w.spawn_zone(StringName(String(b.props.leave_zone)), Vector3(b.pos.x, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3), b.pos.z),
			float(b.props.get("zone_radius", 2.0)), b.attack_owner, float(b.props.get("zone_power", b.power)), b.mat if b.mat == Sim.Mat.FIRE else Sim.Mat.AIR,
			0.0, float(b.props.get("zone_life", 3.0)))
		if b.mat == Sim.Mat.FIRE:
			z.heat_payload = b.heat_payload
			b.heat_payload = 0.0
	match b.mat:
		Sim.Mat.WATER:
			if b.phase == Sim.Phase.LIQUID:
				w._water_to_puddle(b)
		Sim.Mat.FIRE, Sim.Mat.AIR:
			w.decay_body(b, "faded")
		Sim.Mat.PLANT:
			b.form = Sim.Form.CHUNK


static func leave_trail(w: CombatWorld, b: MatBody) -> void:
	var tag := StringName(String(b.props.get("trail_zone", "")))
	if tag == &"":
		return
	var z := w.spawn_zone(tag, b.pos, float(b.props.get("trail_radius", 0.8)), b.attack_owner, float(b.props.get("zone_power", 0.0)),
		Sim.Mat.AIR, 0.0, float(b.props.get("trail_life", 1.5)))
	z.props["spare_owner"] = true
