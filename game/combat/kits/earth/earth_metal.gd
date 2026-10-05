class_name EarthMetal
extends RefCounted
## Earth / Metal (sub 1) - docs/MOVESET.md §7.2. Edges, magnetism, conduction.
## Resource: the metal satchel (ActorState.metal_carried, 12 kg). Thrown metal stays on the field
## (props.metal_owner) and can be Recalled (Lodestone Grip with nothing to grip); the arena metal plate
## is an unlimited scrap source (10 kg per rip, 1.2 s cooldown, ledger metal_taken). Metal conducts.
## Mass ledger: satchel <-> field moves keep metal_mass() exact (to_satchel never books metal_returned).

const SATCHEL_MAX := 30.0
const SCRAP_MASS := 10.0
const SCRAP_COOLDOWN := 1.2
const RECALL_SPEED := 20.0
const ORBIT_R := 1.25
const PLATE_HOT_C := 300.0

static var _scrap_tick := {}       # "world|actor" -> tick of the last plate rip (cooldown)


static func register() -> void:
	# ---------------------------------------------------------------- strike: Razor Disc
	KitEarth.reg("razor_disc", 1, "strike", {"name": "Razor Disc", "part": "metal",
		"desc": "A 2 kg disc from the satchel @24 m/s, homing 10°/s. Hold: pincer (2 curving), ricochet (3 bounce once), Disc Storm (5 orbit you 0.6 s, then fire).",
		"verb": "projectile", "startup": KitEarth.f(10), "active": KitEarth.f(4), "recovery": KitEarth.f(16), "cancel": 0.6, "chain": 0.25,
		"cost": 5.0, "metal_cost": 2.0, "source": "metal", "mat": "metal", "tag": "disc", "mass": 2.0, "speed": 24.0, "gravity": 0.0,
		"homing": 10.0, "damage": 9.0, "balance": 10.0,
		"tiers": {"t1": {"count": 2, "spread": 40.0, "homing": 30.0, "cost_add": 2.0}, "t2": {"count": 3, "spread": 30.0, "ricochet": 1},
			"t3": {"count": 5, "spread": 60.0, "orbit": 0.6, "homing": 40.0}},
		"threat": {"cls": "metal", "power": [2.4, 2.4, 2.4, 2.4]},
		"hook_execute": Callable(EarthMetal, "disc_execute"),
		"anim": "fire_jab", "fx": {"mat": "metal", "shape": "disc"},
		"ai": {"role": "poke", "range": [2.0, 14.0], "tags": ["projectile", "homing", "recallable", "cuts_vines"]}})
	# ---------------------------------------------------------------- thrust: Iron Lance / Railspike
	KitEarth.reg("iron_lance", 1, "thrust", {"name": "Iron Lance", "part": "metal",
		"desc": "A 6 kg lance @30 m/s; T2 pierces one body; T3 Railspike 12 kg @42. Embeds in stone and glass walls as a conductive rod.",
		"verb": "projectile", "startup": KitEarth.f(14), "active": KitEarth.f(4), "recovery": KitEarth.f(20), "cancel": 0.6, "chain": 0.25,
		"cost": 8.0, "metal_cost": 6.0, "source": "metal", "mat": "metal", "tag": "lance", "mass": 6.0, "speed": 30.0, "gravity": 0.15,
		"damage": 12.0, "balance": 20.0, "on_impact": "stick",
		"tiers": {"t1": {"mass": 8.0, "damage": 14.0, "cost_add": 2.0}, "t2": {"mass": 10.0, "pierce": 1, "damage": 16.0},
			"t3": {"mass": 12.0, "speed": 42.0, "pierce": 1, "damage": 22.0, "balance": 40.0}},
		"threat": {"cls": "metal", "power": [9.0, 12.0, 15.0, 25.0]},
		"hook_execute": Callable(EarthMetal, "owned_execute"), "hook_impact": Callable(EarthMetal, "lance_impact"),
		"anim": "earth_throw", "fx": {"mat": "metal", "shape": "lance"},
		"ai": {"role": "poke", "range": [3.0, 16.0], "tags": ["projectile", "pierce", "makes_rod", "recallable"]}})
	# ---------------------------------------------------------------- ground: Lodestone Line / Iron Garden
	KitEarth.reg("lodestone_line", 1, "ground", {"name": "Lodestone Line", "part": "metal",
		"desc": "Iron filings snake 8 m along the ground and spring into caltrops r 2 m for 4 s (slow 40 %, chip 2/s, a conductor node). T3 Iron Garden r 4 m.",
		"verb": "ground_line", "startup": KitEarth.f(14), "active": KitEarth.f(6), "recovery": KitEarth.f(22), "cancel": 0.6, "chain": 0.25,
		"cost": 8.0, "metal_cost": 3.0, "source": "metal", "mat": "metal", "tag": "spike_line", "mass": 3.0, "speed": 12.0, "budget": 8.0,
		"width": 1.2, "damage": 6.0, "balance": 14.0, "knock": 1.0, "lift": 1.5, "kind": "metal",
		"zone_r": 2.0, "zone_life": 4.0, "zone_power": 10.0,
		"tiers": {"t1": {"zone_r": 2.5, "zone_power": 14.0, "cost_add": 2.0}, "t2": {"zone_r": 3.0, "zone_power": 18.0},
			"t3": {"zone_r": 4.0, "zone_power": 24.0, "mass": 5.0}},
		"counter": {"cls": "caltrops", "power": [10.0, 14.0, 18.0, 24.0]},
		"hook_execute": Callable(EarthMetal, "line_execute"),
		"anim": "mv_ground_slap", "fx": {"mat": "metal", "release": "erupt"},
		"ai": {"role": "zone", "range": [2.0, 9.0], "tags": ["ground_line", "trap", "slow", "conductor"]}})
	# ---------------------------------------------------------------- sweep: Chain Arc
	KitEarth.reg("chain_arc", 1, "sweep", {"name": "Chain Arc", "part": "metal",
		"desc": "A 5 m chain sweeps 120°: yanks light fighters 2 m toward you and wraps stones / metal <= 20 kg out of the air to your feet. T2 heavy yank, T3 Whirling Chain 360°.",
		"verb": "cone", "startup": KitEarth.f(12), "active": KitEarth.f(8), "recovery": KitEarth.f(20), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "cls": "chain", "channel": "K", "range": 5.0, "angle": 60.0, "power": 10.0, "damage": 7.0, "balance": 18.0,
		"knock": 4.0, "lift": 1.0,
		"tiers": {"t1": {"range": 7.0, "power": 16.0, "cost_add": 2.0}, "t2": {"range": 7.0, "power": 24.0, "knock": 7.0, "balance": 30.0},
			"t3": {"range": 7.0, "angle": 180.0, "power": 32.0, "knock": 7.0, "damage": 10.0}},
		"counter": {"cls": "chain", "power": [10.0, 16.0, 24.0, 32.0]},
		"hook_execute": Callable(EarthMetal, "chain_execute"),
		"anim": "water_whip", "fx": {"mat": "metal"},
		"ai": {"role": "counter", "range": [0.0, 7.0], "tags": ["yank", "capture", "vs_light_solids", "area"]}})
	# ---------------------------------------------------------------- guard: Aegis Plate / Magnet Catch
	KitEarth.reg("aegis_plate", 1, "guard", {"name": "Aegis Plate", "part": "metal", "module": "kit_earth",
		"desc": "Hold a 6 kg plate from the satchel (CP 20). Heats under flames (>= 300 °C it is dropped); grounds bolts on stone, conducts them into you in water or on the plate. Perfect: Magnet Catch (metal into the satchel), stones deflect.",
		"verb": "barrier", "barrier": "held", "mat": "metal", "tag": "plate", "source": "metal", "mass": 6.0, "cost": 8.0,
		"counter": {"cls": "plate_metal"},
		"anim": "guard", "fx": {"mat": "metal", "shape": "plate"},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["guard", "vs_blades", "vs_stone", "magnet_catch", "heats"]}})
	# ---------------------------------------------------------------- push: Plate Rush
	KitEarth.reg("plate_rush", 1, "push", {"name": "Plate Rush", "part": "metal",
		"desc": "Hurl the held plate spinning @20 m/s.",
		"verb": "projectile", "startup": KitEarth.f(10), "active": KitEarth.f(4), "recovery": KitEarth.f(18), "cancel": 0.6,
		"cost": 4.0, "source": "held", "mat": "metal", "tag": "plate", "speed": 20.0, "gravity": 0.3, "damage": 12.0, "balance": 20.0,
		"threat": {"cls": "metal"},
		"hook_execute": Callable(EarthMetal, "owned_execute"),
		"anim": "earth_throw", "fx": {"mat": "metal", "shape": "plate"},
		"ai": {"role": "poke", "range": [2.0, 12.0], "tags": ["projectile", "from_guard"]}})
	# ---------------------------------------------------------------- sink: Rod Plant
	KitEarth.reg("rod_plant", 1, "sink", {"name": "Rod Plant", "part": "metal",
		"desc": "Plant a 3 kg rod: for 8 s bolts within 6 m are drawn to it and grounded (capacity 60; above it the rod melts). A rod touching a puddle conducts into it.",
		"verb": "zone", "startup": KitEarth.f(8), "active": KitEarth.f(4), "recovery": KitEarth.f(16), "cancel": 0.6,
		"cost": 6.0, "metal": 3.0, "radius": 6.0, "life": 8.0, "power": 60.0,
		"counter": {"cls": "rod", "power": [60.0]},
		"hook_execute": Callable(EarthMetal, "rod_execute"),
		"anim": "mv_overhead_slam", "fx": {"mat": "metal", "shape": "rod", "release": "erupt"},
		"ai": {"role": "counter", "range": [0.0, 6.0], "tags": ["vs_lightning", "ground", "from_guard"]}})
	# ---------------------------------------------------------------- tech: Lodestone Grip / Reforge / Recall
	KitEarth.reg("lodestone_grip", 1, "tech", {"name": "Lodestone Grip", "part": "metal", "module": "kit_earth",
		"desc": "Seize metal <= 80 kg at 10 m (grip x1.3, steals the rival's discs, lances and plates); on the arena plate rip 10 kg of scrap. T+A Reforge: lance -> 3 shards -> disc. Nothing to grip: Recall - every piece you own flies back (and hits what is in between).",
		"verb": "grip", "startup": KitEarth.f(8), "active": KitEarth.f(4), "recovery": KitEarth.f(18), "cancel": 0.5,
		"cost": 6.0, "ccls": "grip_metal", "reach": 10.0, "cone": 60.0, "base": 0.85, "grip_mult": 1.3, "max_mass": 80.0,
		"rip_time": 0.28, "speed": 22.0, "damage": 12.0, "balance": 22.0, "gravity": 0.3, "shape_cost": 3.0, "spread": 24.0,
		"mode_label": "MAGNET", "counter": {"cls": "grip_metal"},
		"anim": "earth_hold", "anim_active": "earth_throw", "fx": {"mat": "metal"},
		"ai": {"role": "counter", "range": [0.0, 10.0], "tags": ["reclaim", "steal", "recall", "vs_metal"]}})
	# ---------------------------------------------------------------- evade: Magnet Glide
	KitEarth.reg("magnet_glide", 1, "evade", {"name": "Magnet Glide", "part": "metal", "module": "kit_earth",
		"desc": "Dash 6 m toward the nearest metal within 8 m (rod, plate, disc...), else a 3 m sidestep.",
		"verb": "dash", "startup": 0.0, "active": KitEarth.f(14), "recovery": KitEarth.f(6), "cost": 5.0,
		"distance": 3.0, "iframes": KitEarth.f(10), "dir": "stick", "reach": 8.0,
		"anim": "air_dash", "fx": {"mat": "metal"},
		"ai": {"role": "mobility", "range": [0.0, 8.0], "tags": ["dash", "to_metal"]}})
	# ---------------------------------------------------------------- evade_hold: Iron Stance
	KitEarth.reg("iron_stance", 1, "evade_hold", {"name": "Iron Stance", "part": "metal", "module": "kit_earth", "aura_mat": "metal",
		"desc": "While held: anchored (no knockback / pull / lift) and 25 % armor; speed x0.5.",
		"verb": "stance", "startup": 0.0, "active": KitEarth.f(10), "recovery": KitEarth.f(8), "cost": 0.0,
		"stance": "iron", "armor": 0.25, "anchored": true, "anchor_cp": 30.0, "speed_mult": 0.5, "upkeep": 6.0,
		"counter": {"cls": "anchor", "power": [30.0]},
		"anim": "guard", "fx": {"mat": "metal", "aura": ""},
		"ai": {"role": "mobility", "range": [0.0, 3.0], "tags": ["anchor", "armor", "vs_tornado", "vs_suction"]}})
	for tag in [&"disc", &"lance", &"plate", &"rod"]:
		CombatWorld.register_body_tick(tag, Callable(EarthMetal, "metal_tick"))
	CombatWorld.register_body_tick(&"caltrops", Callable(EarthMetal, "metal_tick"))
	CombatWorld.register_tech_preview(Sim.Element.EARTH, 1, Callable(EarthMetal, "preview"))


# ================================================================ satchel and ownership

## Moves a metal body into the fighter's satchel (field -> satchel, metal_mass exact). Overflow beyond
## SATCHEL_MAX stays on the field at the fighter's feet. Its heat leaves the ledger (booked removed).
static func to_satchel(w: CombatWorld, a: ActorState, b: MatBody, why: String) -> void:
	if b == null or not b.alive or b.mat != Sim.Mat.METAL:
		return
	var room := maxf(0.0, SATCHEL_MAX - a.metal_carried)
	var take := minf(room, b.mass)
	if take < b.mass - 1e-6:
		var rest := w.split_body(b, b.mass - take, a.pos + a.forward() * 0.6 + Vector3(0, 0.3, 0))
		rest.form = Sim.Form.CHUNK
		rest.vel = Vector3.ZERO
		rest.attack_id = 0
		rest.static_body = false
		rest.props.erase("recall")
	if b.form == Sim.Form.ZONE:
		FxEvents.zone(w, b, "close")
	w.ledger.removed += b.thermal_energy()
	a.metal_carried += b.mass
	w.emit("satchel", {"actor": a.id, "body": b.id, "mass": b.mass, "why": why, "carried": a.metal_carried})
	b.mass = 0.0
	b.heat_payload = 0.0
	w.release_captured(b)
	w.remove_body(b, why)


static func mark(b: MatBody, a: ActorState) -> void:
	if b != null and b.mat == Sim.Mat.METAL:
		b.props["metal_owner"] = a.id


## Every metal piece a fighter owns on the field (Recall).
static func owned(w: CombatWorld, a: ActorState) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in w.bodies:
		if not b.alive or b.mat != Sim.Mat.METAL or b.controller == a.id or b.props.has("recall"):
			continue
		if int(b.props.get("metal_owner", -1)) == a.id:
			out.append(b)
	return out


## hook_execute for projectile moves that leave metal on the field: fire, then mark ownership.
static func owned_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	for b in VerbProjectile.fire(w, a, inst):
		mark(b, a)
	return true


# ================================================================ Razor Disc

static func disc_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var bodies := VerbProjectile.fire(w, a, inst)
	var orbit := float(Charge.param(inst, "orbit", 0.0))
	var k := 0
	for b in bodies:
		mark(b, a)
		b.spin = 40.0
		if orbit > 0.0:
			b.props["orbit_until"] = b.age + orbit
			b.props["orbit_owner"] = a.id
			b.props["orbit_ang"] = TAU * float(k) / float(maxi(1, bodies.size()))
			b.props["orbit_speed"] = float(Charge.param(inst, "speed", 24.0))
		k += 1
	return true


# ================================================================ Iron Lance

## A lance that hits an arena solid or the ground sticks there as a rod (conductive node); against a
## fighter it just drops (default handling).
static func lance_impact(_w: CombatWorld, b: MatBody, what: String) -> bool:
	if what == "actor":
		return true
	return false


# ================================================================ Lodestone Line

static func line_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var b := VerbGroundLine.launch(w, a, inst)
	if b != null:
		mark(b, a)
		b.props["zone_r"] = float(Charge.param(inst, "zone_r", 2.0))
		b.props["zone_life"] = float(Charge.param(inst, "zone_life", 4.0))
		b.props["zone_power"] = float(Charge.param(inst, "zone_power", 10.0))
	return true


## Iron filings (spike_line, metal): at the end of the line (or when blocked) they spring into a caltrops
## ZONE - the filings themselves (metal stays on the field: a conductor node, recallable).
static func filings_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.form == Sim.Form.ZONE:
		return metal_tick(w, b, _dt)
	if b.form == Sim.Form.WAVE and b.wave_budget > 0.05:
		return false
	if b.props.has("recall"):
		return metal_tick(w, b, _dt)
	var owner := b.attack_owner if b.attack_owner >= 0 else int(b.props.get("metal_owner", -1))
	var r := float(b.props.get("zone_r", 2.0))
	b.form = Sim.Form.ZONE
	b.tag = &"caltrops"
	b.zone_radius = r
	b.radius = r
	b.owner = owner
	b.power = float(b.props.get("zone_power", 10.0))
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.static_body = true
	b.gravity_scale = 0.0
	b.wave_path = PackedVector3Array()
	b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4)
	b.max_life = -1.0
	b.props["life_end"] = b.age + float(b.props.get("zone_life", 4.0))
	b.props["ccls"] = "caltrops"
	b.props["actor_status"] = "slowed"
	b.props["status_t"] = 0.3
	b.props["dps"] = 2.0
	b.props["ground_only"] = true
	b.props["height"] = 1.0
	b.props["rate"] = 1.0
	b.props["spare_owner"] = true
	w.release_captured(b)
	FxEvents.zone(w, b, "open")
	FxEvents.fx(w, "erupt", "metal", {"actor": owner, "body": b.id, "pos": b.pos, "radius": r, "element": 0, "sub": 1})
	return true


# ================================================================ body behaviour (disc, lance, plate, rod, caltrops)

static func metal_tick(w: CombatWorld, b: MatBody, dt: float) -> bool:
	if b.mat != Sim.Mat.METAL:
		return false
	if b.props.has("recall"):
		return _recall_step(w, b, dt)
	if b.props.has("orbit_until"):
		return _orbit_step(w, b, dt)
	if b.form == Sim.Form.ZONE:
		if b.age >= float(b.props.get("life_end", INF)):
			_zone_expire(w, b)
			return true
		return false
	if b.attack_id != 0:
		b.spin = 40.0 if b.tag == &"disc" else (25.0 if b.tag == &"plate" else 0.0)
	else:
		b.spin = 0.0
	return false


## Field / caltrops time is up: the metal stays as a loose scrap piece (recallable), or a planted rod.
static func _zone_expire(w: CombatWorld, b: MatBody) -> void:
	FxEvents.zone(w, b, "close")
	var planted := b.tag == &"rod"
	b.form = Sim.Form.CHUNK
	b.zone_radius = 0.0
	b.power = 0.0
	for k in ["ccls", "barrier", "actor_status", "status_t", "dps", "ground_only", "height", "rate", "spare_owner", "life_end"]:
		b.props.erase(k)
	b.tag = &"rod" if planted else &"plate"
	b.static_body = planted
	b.on_ground = true
	b.update_radius()
	b.max_life = b.age + Sim.REMNANT_LIFETIME


static func _orbit_step(w: CombatWorld, b: MatBody, dt: float) -> bool:
	var a := w.get_actor(int(b.props.get("orbit_owner", -1)))
	if a == null or b.attack_id == 0:
		b.props.erase("orbit_until")
		return false
	var ang := float(b.props.get("orbit_ang", 0.0)) + 8.0 * dt
	b.props["orbit_ang"] = ang
	var p := a.chest() + Vector3(cos(ang), 0.0, sin(ang)) * ORBIT_R
	b.vel = (p - b.pos) / dt
	b.pos = p
	b.spin = 40.0
	if b.age >= float(b.props.orbit_until):
		b.props.erase("orbit_until")
		var t := w.get_actor(a.lock_target)
		var to := (t.chest() if t != null else a.chest() + a.forward() * 14.0) - b.pos
		b.vel = to.normalized() * float(b.props.get("orbit_speed", 24.0))
		b.gravity_scale = 0.0
		w.emit("launch", {"actor": a.id, "body": b.id, "speed": b.vel.length(), "kind": "disc_storm"})
	return true


static func _recall_step(w: CombatWorld, b: MatBody, dt: float) -> bool:
	var a := w.get_actor(int(b.props.get("recall", -1)))
	if a == null or a.health <= 0.0:
		b.props.erase("recall")
		b.attack_id = 0
		return false
	var to := a.chest() - b.pos
	if to.length() <= 0.9:
		to_satchel(w, a, b, "recalled")
		return true
	b.vel = to.normalized() * RECALL_SPEED
	b.pos += b.vel * dt
	b.spin = 30.0
	return true


## Every piece the fighter owns flies back to them as their attack (hits whatever is in between).
static func recall(w: CombatWorld, a: ActorState, inst: ActionInst, pieces: Array[MatBody]) -> void:
	for b in pieces:
		if b.form == Sim.Form.ZONE:
			FxEvents.zone(w, b, "close")
			b.form = Sim.Form.CHUNK
			b.zone_radius = 0.0
			b.power = 0.0
			for k in ["ccls", "barrier", "actor_status", "status_t", "dps", "ground_only", "height", "rate", "spare_owner", "life_end"]:
				b.props.erase(k)
			b.update_radius()
		if b.tag != &"disc" and b.tag != &"lance" and b.tag != &"plate":
			b.tag = &"plate"
		b.static_body = false
		b.gravity_scale = 0.0
		b.on_ground = false
		b.props.erase("embedded_in")
		b.props.erase("on_impact")
		b.props["recall"] = a.id
		Verbs.arm(w, a, inst, b, 8.0 * clampf(sqrt(b.mass / 6.0), 0.6, 1.5), 14.0)
		b.vel = (a.chest() - b.pos).normalized() * RECALL_SPEED
		w.emit("recall", {"actor": a.id, "body": b.id})
		Verbs.fx(w, a, inst, "release", {"body": b.id, "pos": b.pos, "dir": b.vel.normalized(), "shape": String(b.tag)})


# ================================================================ Chain Arc

static func chain_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var rng := float(Charge.param(inst, "range", 5.0))
	var ang := float(Charge.param(inst, "angle", 60.0))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var v := Agent.of_volume(w, a, inst, &"chain", a.chest(), dir, {"K": float(Charge.param(inst, "power", 10.0))})
	v.data["knock"] = float(Charge.param(inst, "knock", 4.0))
	Verbs.fx(w, a, inst, "cone", {"length": rng, "angle": ang, "power": v.power})
	var cos_lim := cos(deg_to_rad(ang))
	for b in w.bodies.duplicate():
		if not b.alive or b.static_body or b.controller >= 0 or b.captured_by >= 0 or b == w.pool:
			continue
		if b.form == Sim.Form.WALL or b.form == Sim.Form.ZONE or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE or b.form == Sim.Form.WAVE:
			continue
		var to := KitEarth.flat(b.pos - a.chest())
		if to.length() > rng + b.radius or (to.length() > 0.5 and to.normalized().dot(dir) < cos_lim):
			continue
		Interactions.resolve(w, Agent.of_body(w, b, a), v, {"site": "volume"}, Interactions.PASS_RULE)
	for t in w.actors_in_cone(a, dir, rng, ang):
		var to2 := KitEarth.flat(t.pos - a.pos)
		var res := w.hit_actor(t, VerbVolume._hit_info(a, inst, v, a.chest(), -to2.normalized() if to2.length() > 0.1 else -dir))
		VerbVolume._after_hit(w, a, inst, t, res)
	return true


# ================================================================ Aegis Plate (guard spec, module kit_earth)

static func _guard_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	VerbBarrier.start(w, a, inst, it)
	var hb := w.get_body(int(inst.data.get("held_barrier", -1)))
	if hb != null and hb.alive:
		mark(hb, a)
		inst.data["shield"] = true   # the guard's own held material: ActCommon keeps it (no drop)


static func _guard_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	VerbBarrier.tick(w, a, inst, it)
	var hb := w.get_body(int(inst.data.get("held_barrier", -1)))
	if hb == null or not hb.alive or hb.controller != a.id:
		return
	if hb.temp >= PLATE_HOT_C:
		# Red-hot: dropped (and it burns the hands that held it).
		w.release_body(a, a.forward() * 1.5 + Vector3(0, 1.0, 0), false)
		inst.data["held_barrier"] = -1
		inst.data.erase("shield")
		if not Status.immune(a, "burn"):
			a.health = maxf(0.0, a.health - 3.0)
			w.emit("burn", {"actor": a.id, "body": hb.id})
		w.emit("drop", {"actor": a.id, "body": hb.id, "why": "red_hot", "temp": hb.temp})


# ================================================================ module lifecycle (aegis spec, lodestone_grip, magnet_glide)

static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.id == "guard":
		_guard_start(w, a, inst, it)
		return
	match inst.id:
		"magnet_glide":
			_glide_start(w, a, inst, it)
		"rod_plant":
			# The plate kept from the guard goes back into the satchel first (the rod comes from there).
			var hb := w.held(a)
			if hb != null and hb.mat == Sim.Mat.METAL and hb.tag == &"plate":
				to_satchel(w, a, hb, "returned")
			Verbs.on_start(w, a, inst, it)
		_:
			Verbs.on_start(w, a, inst, it)


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	if inst.id == "magnet_glide":
		return ActionInst.P.ACTIVE
	return Verbs.after_startup(w, a, inst, it)


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if inst.id == "guard":
		if p == ActionInst.P.RECOVERY:
			VerbBarrier.end(w, a, inst, "release")
		return
	match inst.id:
		"magnet_glide":
			if p == ActionInst.P.RECOVERY:
				inst.data["controls_motion"] = false
		_:
			Verbs.on_phase(w, a, inst, p)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.id == "guard":
		_guard_tick(w, a, inst, it)
		return
	match inst.id:
		"magnet_glide":
			if inst.phase == ActionInst.P.ACTIVE:
				VerbMotion.dash_tick(w, a, inst)
		"lodestone_grip":
			if inst.phase == ActionInst.P.CHANNEL:
				_grip_tick(w, a, inst, it)
		_:
			Verbs.on_tick(w, a, inst, it)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	if inst.id == "guard":
		VerbBarrier.end(w, a, inst, reason)
		return
	match inst.id:
		"magnet_glide":
			inst.data["controls_motion"] = false
		_:
			Verbs.on_interrupt(w, a, inst, reason)


# ---------------------------------------------------------------- Magnet Glide

static func _glide_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if not Verbs.pay(w, a, inst, "start"):
		inst.data["fizzle"] = true
		return
	var reach := float(inst.def.get("reach", 8.0))
	var best: MatBody = null
	var bd := INF
	for b in w.bodies:
		if not b.alive or b.mat != Sim.Mat.METAL or b.controller == a.id:
			continue
		var d := KitEarth.flat_dist(b.pos, a.pos)
		if d < 1.2 or d > reach:
			continue
		if d < bd - 1e-6 or (absf(d - bd) <= 1e-6 and best != null and b.id < best.id):
			best = b
			bd = d
	var spec := inst.def.duplicate()
	if best != null:
		spec["distance"] = clampf(bd - 0.9, 1.0, 6.0)
		spec["dir"] = "aim"
		inst.data["aim"] = KitEarth.flat(best.pos - a.pos).normalized()
		inst.data["glide_to"] = best.id
	inst.data["spec_def"] = spec
	VerbMotion.dash_start(w, a, inst, it)
	Verbs.fx(w, a, inst, "trail", {"dir": inst.data.get("dir", a.forward()), "length": float(spec.distance), "body": best.id if best != null else -1})


# ---------------------------------------------------------------- Lodestone Grip / Reforge / Recall

const REFORGE := ["lance", "shards", "disc"]


static func _grip_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var d := inst.def
	if it.tech_cancel:
		VerbGrip.drop(w, a, inst)
		w.emit("cancel", {"actor": a.id, "move": inst.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["aim"] = w.aim_dir(a, it)
	inst.data["aim_active"] = it.aim_active
	inst.data["face"] = inst.data.aim
	inst.data["aim_point"] = w.aim_point(a, it)
	var b := w.held(a)
	if b != null:
		b.static_body = false
		b.props.erase("embedded_in")
		if b.form == Sim.Form.ZONE:
			_zone_expire(w, b)
			b.static_body = false
		mark(b, a)
		var dir: Vector3 = inst.data.aim
		b.hold_point = a.pos + Vector3(0, 1.2, 0) + a.forward() * (0.3 + b.radius) + dir * 0.15
		if it.attack_pressed:
			_reforge(w, a, inst, b)
		if not it.tech_held:
			w.set_phase(a, inst, ActionInst.P.ACTIVE)
			_throw(w, a, inst)
		return
	# Nothing in hand: seek metal (incoming preferred), else Recall, else rip scrap from the plate.
	var reach := float(d.reach)
	var f := func(x: MatBody) -> bool:
		return x.controller != a.id and x.mat == Sim.Mat.METAL and x.form != Sim.Form.POOL and x.captured_by < 0 \
			and not x.props.has("recall") and Interactions.allows(x, &"grip_metal")
	var tb := w.get_body(int(inst.data.get("target", -1)))
	if tb == null or not tb.alive or not f.call(tb):
		tb = w.find_body(a, inst.data.aim, reach, float(d.get("cone", 60.0)), f)
		if tb != null:
			inst.data["target"] = tb.id
			w.emit("target_body", {"actor": a.id, "body": tb.id})
	if tb != null:
		if tb.mass > minf(a.max_control_mass, float(d.get("max_mass", 80.0))):
			w.request_grip(a, tb, 0.0, "magnet")
			w.emit("whiff", {"actor": a.id, "move": inst.id, "body": tb.id})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
			return
		var s := w.grip_strength(a, tb, float(d.get("base", 0.85)), reach) * float(d.get("grip_mult", 1.3))
		w.request_grip(a, tb, s, "magnet")
		return
	var early := inst.t < float(d.get("rip_time", 0.28))
	if inst.data.get("ripped", false) or (early and it.tech_held):
		if not it.tech_held:
			w.emit("whiff", {"actor": a.id, "move": inst.id})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["ripped"] = true
	var mine := owned(w, a)
	if not mine.is_empty():
		recall(w, a, inst, mine)
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	if early:
		w.emit("whiff", {"actor": a.id, "move": inst.id, "reason": "no_metal"})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	if _near_plate(w, a) and _scrap_ready(w, a):
		_scrap_tick["%d|%d" % [w.get_instance_id(), a.id]] = w.tick
		w.mass_ledger.metal_taken += SCRAP_MASS
		var p := a.pos + a.forward() * 0.8 + Vector3(0, 0.3, 0)
		var sb := w.spawn_body(Sim.Mat.METAL, Sim.Form.CHUNK, SCRAP_MASS, p, "plate")
		sb.tag = &"plate"
		sb.max_life = Sim.REMNANT_LIFETIME
		mark(sb, a)
		w.take_control(a, sb, 0.9, "rip")
		w.emit("rip", {"actor": a.id, "body": sb.id, "source": "metal_plate"})
		Verbs.fx(w, a, inst, "erupt", {"pos": p, "body": sb.id, "shape": "plate"})
		return
	w.emit("whiff", {"actor": a.id, "move": inst.id, "reason": "no_metal"})
	w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func _near_plate(w: CombatWorld, a: ActorState) -> bool:
	var mx := clampf(a.pos.x, w.arena.metal_min.x, w.arena.metal_max.x)
	var mz := clampf(a.pos.z, w.arena.metal_min.y, w.arena.metal_max.y)
	return Vector2(mx - a.pos.x, mz - a.pos.z).length() <= VerbGrip.PLATE_REACH


static func _scrap_ready(w: CombatWorld, a: ActorState) -> bool:
	var k := "%d|%d" % [w.get_instance_id(), a.id]
	return not _scrap_tick.has(k) or w.tick - int(_scrap_tick[k]) >= int(SCRAP_COOLDOWN * Sim.HZ) or w.tick < int(_scrap_tick[k])


## T+A Reforge: cycles the held metal lance -> 3 shards -> disc (shape on release).
static func _reforge(w: CombatWorld, a: ActorState, inst: ActionInst, b: MatBody) -> void:
	if not w.spend_focus(a, float(inst.def.get("shape_cost", 3.0))):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id, "reason": "shape"})
		return
	var i := (REFORGE.find(String(inst.data.get("reforge", ""))) + 1) % REFORGE.size()
	var form: String = REFORGE[i]
	inst.data["reforge"] = form
	match form:
		"lance":
			b.tag = &"lance"
			inst.data.erase("split")
		"shards":
			b.tag = &"disc"
			inst.data["split"] = 3
		"disc":
			b.tag = &"disc"
			inst.data.erase("split")
	w.emit("shape", {"actor": a.id, "body": b.id, "shape": "reforge", "to": form})
	Verbs.fx(w, a, inst, "cast", {"body": b.id, "shape": "lance" if form == "lance" else "disc"})


static func _throw(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.held(a)
	if b == null:
		return
	var form := String(inst.data.get("reforge", ""))
	var before: Array = w.bodies.map(func(x: MatBody) -> int: return x.id)
	VerbGrip.throw(w, a, inst)
	for x in w.bodies:
		if x.alive and x.mat == Sim.Mat.METAL and (x == b or not before.has(x.id)) and x.attack_owner == a.id:
			mark(x, a)
			match form:
				"lance":
					x.tag = &"lance"
					x.gravity_scale = 0.15
					x.props["on_impact"] = "stick"
					x.props["move"] = "iron_lance"
				"disc", "shards":
					x.tag = &"disc"
					x.props["homing"] = 15.0


## HUD / AI technique context for Earth/Metal.
static func preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var d: Dictionary = Moves.DEFS.get("lodestone_grip", {})
	var f := func(x: MatBody) -> bool:
		return x.controller != a.id and x.mat == Sim.Mat.METAL and x.form != Sim.Form.POOL and Interactions.allows(x, &"grip_metal")
	var b := w.find_body(a, dir, float(d.get("reach", 10.0)), float(d.get("cone", 60.0)), f)
	if b != null:
		return {"mode": "MAGNET", "body": b.id, "ok": b.mass <= a.max_control_mass, "reason": "" if b.mass <= a.max_control_mass else "mass"}
	if not owned(w, a).is_empty():
		return {"mode": "RECALL", "body": -1, "ok": true, "reason": ""}
	if _near_plate(w, a):
		return {"mode": "RIP", "body": -1, "ok": true, "reason": ""}
	return {"mode": "MAGNET", "body": -1, "ok": false, "reason": "target"}


# ================================================================ Rod Plant

static func rod_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	# A plate kept from the guard goes back into the satchel (the rod is planted with both hands).
	var hb := w.held(a)
	if hb != null and hb.mat == Sim.Mat.METAL and hb.tag == &"plate":
		to_satchel(w, a, hb, "returned")
	var mass := float(inst.data.get("metal_paid", 0.0))
	if mass <= 0.0:
		KitEarth.fizzle(w, a, inst, "metal")
		return true
	inst.data["metal_paid"] = 0.0
	var p := KitEarth.ground_point(w, a, inst.data.get("face", a.forward()), 1.5)
	var z := w.spawn_zone(&"rod", p, float(Charge.param(inst, "radius", 6.0)), a.id, float(Charge.param(inst, "power", 60.0)),
		Sim.Mat.METAL, mass, -1.0, "metal:%d" % a.id)
	z.static_body = true
	z.sub = 1
	z.props["ccls"] = "rod"
	z.props["barrier"] = true
	z.props["height"] = 3.5
	z.props["rate"] = 1.0
	z.props["life_end"] = float(Charge.param(inst, "life", 8.0))
	mark(z, a)
	Verbs.fx(w, a, inst, "release", {"pos": p, "body": z.id, "radius": z.zone_radius})
	inst.data["zone"] = z.id
	return true


## A grounded bolt's charge spreads from the rod into a puddle (or the pool / plate) it touches: fighters
## standing in that water take a conducted shock.
static func rod_spread(w: CombatWorld, t: Agent, rod: MatBody, e: float) -> void:
	if rod == null or not rod.alive or e <= 0.0:
		return
	var src := t.actor
	var victims: Array[ActorState] = []
	for pd in w.bodies:
		if not pd.alive or pd.form != Sim.Form.PUDDLE or pd.phase != Sim.Phase.LIQUID:
			continue
		if KitEarth.flat_dist(pd.pos, rod.pos) > pd.radius + 0.6:
			continue
		for a in w.actors:
			if a.health > 0.0 and not victims.has(a) and w.puddle_at(a.pos) == pd and not Status.immune(a, "conduct"):
				victims.append(a)
	if w.arena.in_pool(rod.pos.x, rod.pos.z) or (KitEarth.flat_dist(rod.pos, Vector3(clampf(rod.pos.x, w.arena.pool_min.x, w.arena.pool_max.x), 0, clampf(rod.pos.z, w.arena.pool_min.y, w.arena.pool_max.y))) < 0.6):
		for a in w.actors:
			if a.in_water and a.health > 0.0 and not victims.has(a) and not Status.immune(a, "conduct"):
				victims.append(a)
	if victims.is_empty():
		return
	var aid := w.new_attack_id()
	for a in victims:
		w.hit_actor(a, {"attacker": src.id if src != null else -1, "attack_id": aid, "damage": e * 0.6 / float(victims.size()),
			"balance": 18.0, "kind": "lightning", "from": rod.pos, "unblockable": true, "power": e})
	w.emit("conduct", {"actor": src.id if src != null else -1, "nodes": ["rod:%d" % rod.id], "victims": victims.map(func(x: ActorState) -> int: return x.id)})
