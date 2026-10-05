class_name VerbProjectile
extends RefCounted
## Verb "projectile": spawn (or take) bodies and launch them (MOVESET §15.7).
## Params (Charge.param, tier ladder):
##   source  ground | waterskin | metal | moisture | heat | held | none   (ledger of the material)
##   mat (stone water metal sand glass plant fire air; default from source), tag, form (chunk shard blob stream)
##   frozen (water -> ice), mass (kg per piece), count, spread (deg, whole fan), speed (m/s),
##   mass_speed (true: speed x (20/m)^0.4), gravity (scale), arc (lob onto the target), homing (deg/s),
##   pierce (n), ricochet (n), on_impact (shatter stick burst puddle sprout zone), impact_* (radius power
##   pieces zone life damage), damage, balance, heat (HU payload, fire), temp, charge (E), power (P, air),
##   life (s, air/fire bodies), hit_status, hit_status_t, stagger (s between pieces: 0 = together).


## Fires the action's projectiles. Returns the bodies (also in inst.data.bodies).
static func fire(w: CombatWorld, a: ActorState, inst: ActionInst, over: Dictionary = {}) -> Array[MatBody]:
	var P := func(k: String, dflt: Variant) -> Variant:
		return over[k] if over.has(k) else Charge.param(inst, k, dflt)
	var out: Array[MatBody] = []
	var source := String(P.call("source", "none"))
	var count := maxi(1, int(P.call("count", 1)))
	var mass := float(P.call("mass", 5.0))
	var mat := Verbs.mat_id(P.call("mat", _source_mat(source)))
	var speed := float(P.call("speed", 20.0))
	var gscale := float(P.call("gravity", 1.0))
	var spread := deg_to_rad(float(P.call("spread", 20.0 if count > 1 else 0.0)))
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	var target := Verbs.target_point(w, a, inst, float(P.call("reach", 14.0)))
	var held: MatBody = null
	if source == "held":
		held = w.held(a)
		if held == null:
			held = w.get_body(int(inst.data.get("morph_body", -1)))
			if held != null and (not held.alive or held.controller >= 0 and held.controller != a.id):
				held = null
		if held == null:
			w.emit("insufficient", {"actor": a.id, "what": "material", "move": inst.id})
			return out
		if held.controller == a.id:
			a.held_body = -1
			held.controller = -1
		mass = held.mass / float(count)
	var heat_total := 0.0
	if source == "heat":
		heat_total = Verbs.take_heat(inst)
		if heat_total <= 0.0:
			heat_total = w.pay_heat(a, float(P.call("heat", 60.0)))
	for k in count:
		var b: MatBody = null
		if held != null:
			b = held if k == count - 1 else w.split_body(held, mass, held.pos)
		else:
			b = spawn(w, a, inst, source, mat, mass, heat_total / float(count), P)
		if b == null:
			break
		b.tag = StringName(String(P.call("tag", String(b.tag))))
		var form := String(P.call("form", ""))
		if form != "":
			b.form = Sim.FORM_NAMES.find(form) if Sim.FORM_NAMES.has(form) else b.form
		if bool(P.call("frozen", false)) and b.is_water():
			var e0 := b.thermal_energy()
			b.liquid = 0.0
			b.temp = -5.0
			b.phase = Sim.Phase.FROZEN
			w.ledger.freeze_dump += b.thermal_energy() - e0
			if form == "":
				b.form = Sim.Form.SHARD
		b.gravity_scale = gscale
		for key in ["homing", "pierce", "ricochet", "on_impact", "impact_radius", "impact_power", "impact_pieces",
				"impact_zone", "impact_life", "impact_damage", "hit_status", "hit_status_t"]:
			var v: Variant = P.call(key, null)
			if v != null:
				b.props[key] = v
		if P.call("charge", null) != null:
			b.charge = float(P.call("charge", 0.0))
		if mat == Sim.Mat.AIR or mat == Sim.Mat.FIRE:
			b.power = float(P.call("power", b.power))
			b.max_life = float(P.call("life", 2.0))
		elif b.max_life < 0.0:
			b.max_life = Sim.REMNANT_LIFETIME
		var spd := speed
		if bool(P.call("mass_speed", false)):
			spd *= pow(20.0 / maxf(b.mass, 0.5), 0.4)
		var ang := 0.0 if count == 1 else lerpf(-spread * 0.5, spread * 0.5, float(k) / float(count - 1))
		var d2 := dir.rotated(Vector3.UP, ang)
		b.pos = a.hand_point() + d2 * 0.35 + Vector3(0, 0.05 * k, 0)
		var aim_to := target if count == 1 else a.chest() + (target - a.chest()).rotated(Vector3.UP, ang)
		if bool(P.call("arc", false)) or gscale > 0.0:
			b.vel = Verbs.launch_vel(b.pos, aim_to, spd, gscale)
		else:
			b.vel = (aim_to - b.pos).normalized() * spd
		b.on_ground = false
		Verbs.arm(w, a, inst, b, float(P.call("damage", 8.0)), float(P.call("balance", 16.0)))
		out.append(b)
		w.emit("launch", {"actor": a.id, "body": b.id, "speed": spd, "kind": String(b.tag) if b.tag != &"" else Sim.MAT_NAMES[b.mat], "tier": inst.tier()})
		Verbs.fx(w, a, inst, "release", {"body": b.id, "pos": b.pos, "dir": b.vel.normalized(), "power": b.mass * spd / 20.0})
	inst.data["bodies"] = out.map(func(x): return x.id)
	return out


static func _source_mat(source: String) -> String:
	match source:
		"waterskin", "moisture":
			return "water"
		"metal":
			return "metal"
		"heat":
			return "fire"
		"none":
			return "air"
	return "stone"


## A new body from a source, booked in its ledger. Returns null when the source is empty.
static func spawn(w: CombatWorld, a: ActorState, inst: ActionInst, source: String, mat: int, mass: float, heat: float, P: Callable) -> MatBody:
	var p := a.hand_point()
	var origin := "%s:%d" % [source, a.id]
	match source:
		"ground":
			p = a.pos + a.forward() * 0.85
			origin = "ground@%.1f,%.1f" % [p.x, p.z]
			w.mass_ledger.ground_taken += mass
		"waterskin":
			var m := minf(mass, a.water_carried)
			if m < 0.05:
				if a.in_water:
					Verbs._take_pool(w, a, mass)
					m = minf(mass, a.water_carried)
				if m < 0.05:
					w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
					return null
			a.water_carried -= m
			mass = m
		"metal":
			var mm := minf(mass, a.metal_carried)
			if mm < 0.05:
				w.emit("insufficient", {"actor": a.id, "what": "metal", "move": inst.id})
				return null
			a.metal_carried -= mm
			mass = mm
		"moisture":
			w.mass_ledger.moisture_taken += mass
	var b := w.spawn_body(mat, Sim.Form.CHUNK, mass, p, origin, float(P.call("temp", Sim.AMBIENT_C)))
	if mat == Sim.Mat.FIRE:
		b.heat_payload = heat
	elif heat > 0.0:
		w.heat_body(b, heat)
	if source == "waterskin" and mat == Sim.Mat.WATER:
		b.form = Sim.Form.BLOB
	b.update_radius()
	return b


## props.on_impact: shatter | stick | burst | puddle | sprout | zone (once per body).
static func on_impact(w: CombatWorld, b: MatBody, what: String) -> void:
	var kind := String(b.props.get("on_impact", ""))
	if kind == "":
		return
	var hook: Variant = Moves.DEFS.get(String(b.props.get("move", "")), {}).get("hook_impact")
	if hook is Callable and (hook as Callable).is_valid() and bool((hook as Callable).call(w, b, what)):
		b.props.erase("on_impact")
		return
	b.props.erase("on_impact")
	var owner := w.get_actor(b.attack_owner)
	match kind:
		"shatter":
			var n := int(b.props.get("impact_pieces", 3))
			var v := b.vel
			w.emit("shatter", {"body": b.id, "mass": b.mass, "on": what})
			b.attack_id = 0
			var piece := b.mass / float(n)
			for k in n - 1:
				var ang := TAU * float(k + 1) / float(n)
				var off := Vector3(cos(ang), 0.3, sin(ang)) * 0.3
				var c := w.split_body(b, piece, b.pos + off)
				c.vel = v * 0.2 + off.normalized() * 4.0
				c.attack_id = 0
				c.max_life = Sim.REMNANT_LIFETIME
			b.vel = v * 0.2 + Vector3(0, 2.0, 0)
		"stick":
			b.vel = Vector3.ZERO
			b.on_ground = true
			b.attack_id = 0
			b.gravity_scale = 0.0
			b.static_body = what != "actor"
			if b.mat == Sim.Mat.METAL:
				b.tag = &"rod"   # an embedded lance relays lightning
			w.emit("stick", {"body": b.id, "on": what})
		"burst":
			VerbVolume.burst_at(w, owner, null, b.pos, {"radius": float(b.props.get("impact_radius", 2.0)),
				"power": float(b.props.get("impact_power", 8.0)), "damage": float(b.props.get("impact_damage", 6.0)),
				"cls": "blast" if b.mat != Sim.Mat.FIRE else "flame", "heat_hu": b.heat_payload if b.mat == Sim.Mat.FIRE else 0.0,
				"mat": FxEvents.mat_of(b)})
			if b.mat == Sim.Mat.FIRE:
				b.heat_payload = 0.0   # spent by the burst (booked there)
				w.decay_body(b, "burst")
		"puddle":
			if b.is_water():
				b.phase = Sim.Phase.LIQUID if b.liquid > 0.5 else b.phase
				w._water_to_puddle(b)
		"sprout", "zone":
			var tag := StringName(String(b.props.get("impact_zone", "zone")))
			var r := float(b.props.get("impact_radius", 1.0))
			var life := float(b.props.get("impact_life", 3.0))
			if kind == "sprout":
				b.form = Sim.Form.ZONE
				b.tag = tag
				b.zone_radius = r
				b.radius = r
				b.attack_id = 0
				b.vel = Vector3.ZERO
				b.gravity_scale = 0.0
				b.owner = owner.id if owner != null else -1
				b.max_life = b.age + life
				b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3)
				FxEvents.zone(w, b, "open")
			else:
				w.spawn_zone(tag, Vector3(b.pos.x, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3), b.pos.z), r,
					owner.id if owner != null else -1, float(b.props.get("impact_power", 0.0)), Sim.Mat.AIR, 0.0, life)
