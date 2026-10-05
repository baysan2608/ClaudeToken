class_name EarthSand
extends RefCounted
## Earth / Sand (sub 2) - docs/MOVESET.md §7.3. Grit, smothering, flow.
## Sand is ground grit (ledger ground_taken; it returns - decays to ground_returned - as it settles).
## It fuses to GLASS with lightning or blue fire, becomes MUD with water, SANDSTONE (mat STONE, booked
## sand_to_sandstone) when compressed. earth_mass() counts stone + sand + glass, so every conversion is exact.

const SETTLE_TIME := 1.2          # a spent sand body (slug, surge heap) settles back into the ground
const GATHER_RATE := 6.0          # Sandform: kg/s gathered while held
const GATHER_MAX := 30.0
const GATHER_FOCUS := 2.0         # Focus/s while gathering


static func register() -> void:
	Status.register("mired", {"speed": 0.3})
	# ---------------------------------------------------------------- strike: Grit Shot ... Dune Breaker
	KitEarth.reg("grit_shot", 2, "strike", {"name": "Grit Shot", "part": "sand",
		"desc": "A 5 kg sand slug @22 that bursts on hit (blind 1 s). Hold: 3 slugs, Sand Cannon (15 kg, bursts into a 2 m cloud), Dune Breaker (30 kg, a 4 m sandstorm for 3 s).",
		"verb": "projectile", "startup": KitEarth.f(10), "active": KitEarth.f(4), "recovery": KitEarth.f(16), "cancel": 0.6, "chain": 0.25,
		"cost": 5.0, "source": "ground", "mat": "sand", "tag": "slug", "mass": 5.0, "speed": 22.0, "gravity": 0.5,
		"damage": 6.0, "balance": 12.0, "hit_status": "blinded", "hit_status_t": 1.0, "on_impact": "burst",
		"tiers": {"t1": {"count": 3, "spread": 16.0, "cost_add": 2.0}, "t2": {"count": 1, "mass": 15.0, "speed": 20.0, "cloud_r": 2.0, "cloud_life": 2.5, "damage": 10.0, "balance": 22.0},
			"t3": {"count": 1, "mass": 30.0, "speed": 18.0, "cloud_r": 4.0, "cloud_life": 3.0, "storm": true, "damage": 14.0, "balance": 34.0}},
		"threat": {"cls": "sand", "power": [5.5, 8.0, 13.5, 27.0]},
		"hook_execute": Callable(EarthSand, "slug_execute"), "hook_impact": Callable(EarthSand, "slug_impact"),
		"anim": "earth_lift", "anim_active": "earth_throw", "fx": {"mat": "sand", "impact": "burst"},
		"ai": {"role": "poke", "range": [2.0, 14.0], "tags": ["projectile", "blind", "makes_cloud"]}})
	# ---------------------------------------------------------------- thrust: Sandblast / Scour
	KitEarth.reg("sandblast", 2, "thrust", {"name": "Sandblast", "part": "sand",
		"desc": "An abrasive jet 8 m: abrades barriers (glass, ice, vines x2), dries wet targets. Hold: 10 m, a 1 s sustained jet, Scour (2 s, cuts ice walls and vines).",
		"verb": "beam", "startup": KitEarth.f(12), "active": KitEarth.f(10), "recovery": KitEarth.f(18), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "cls": "sand", "channel": "P", "power": 6.0, "range": 8.0, "width": 0.5, "damage": 5.0, "balance": 10.0, "knock": 1.5,
		"tiers": {"t1": {"power": 9.0, "range": 10.0, "cost_add": 2.0}, "t2": {"power": 14.0, "range": 10.0, "pulse": 0.2, "active_t": 1.0, "damage": 4.0},
			"t3": {"power": 20.0, "range": 10.0, "pulse": 0.2, "active_t": 2.0, "damage": 5.0}},
		"counter": {"cls": "sand", "power": [6.0, 9.0, 14.0, 20.0]},
		"hook_execute": Callable(EarthSand, "blast_execute"),
		"anim": "fire_release", "fx": {"mat": "sand"},
		"ai": {"role": "poke", "range": [1.0, 10.0], "tags": ["beam", "abrade", "dries"]}})
	# ---------------------------------------------------------------- ground: Sand Surge / Desert Tide
	KitEarth.reg("sand_surge", 2, "ground", {"name": "Sand Surge", "part": "sand",
		"desc": "A sand wave 9 m/s, 2 m wide, 10 m: knockdown, carries loose solids back, buries puddles (mud), smothers fire fields, crusts lava (x1.5). Hold: wider, tall (stops projectiles), Desert Tide (4 m, 14 m).",
		"verb": "ground_line", "startup": KitEarth.f(16), "active": KitEarth.f(6), "recovery": KitEarth.f(22), "cancel": 0.6, "chain": 0.25,
		"cost": 9.0, "source": "ground", "mat": "sand", "tag": "sand_surge", "mass": 30.0, "speed": 9.0, "budget": 10.0, "width": 2.0,
		"damage": 10.0, "balance": 35.0, "knock": 3.0, "lift": 2.5, "steer": 15.0, "kind": "sand",
		"tiers": {"t1": {"mass": 40.0, "width": 2.5, "cost_add": 3.0}, "t2": {"mass": 53.0, "width": 2.5, "balance": 45.0},
			"t3": {"mass": 75.0, "width": 4.0, "budget": 14.0, "balance": 55.0, "damage": 14.0}},
		"counter": {"cls": "wave_sand", "power": [13.5, 18.0, 24.0, 34.0]}, "threat": {"cls": "sand_surge"},
		"anim": "earth_wall", "fx": {"mat": "sand"},
		"ai": {"role": "zone", "range": [2.0, 12.0], "tags": ["ground_line", "knockdown", "carries_back", "vs_lava_wave", "vs_fire_field"]}})
	# ---------------------------------------------------------------- sweep: Veil of Grit
	KitEarth.reg("veil_of_grit", 2, "sweep", {"name": "Veil of Grit", "part": "sand",
		"desc": "A sand cloud ahead (r 4 m, 2.5 s): blocks lock-on, smothers flames (x2), drags projectiles (-30 % K), halves bolts that cross it and fuses glass. T3: an 8 m sandstorm.",
		"verb": "zone", "startup": KitEarth.f(12), "active": KitEarth.f(8), "recovery": KitEarth.f(18), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "tag": "sand_cloud", "mat": "sand", "source": "ground", "mass": 8.0, "at": "ahead", "distance": 2.5, "radius": 4.0,
		"life": 2.5, "power": 6.0, "channel": "P", "barrier": true, "height": 3.0,
		"tiers": {"t1": {"radius": 5.0, "power": 9.0, "mass": 10.0, "cost_add": 2.0}, "t2": {"radius": 6.0, "life": 4.0, "power": 12.0, "mass": 12.0},
			"t3": {"radius": 8.0, "life": 4.0, "power": 16.0, "mass": 16.0, "tag": "sandstorm", "distance": 3.5}},
		"counter": {"cls": "sand_cloud", "power": [6.0, 9.0, 12.0, 16.0]},
		"anim": "water_whip", "fx": {"mat": "sand"},
		"ai": {"role": "setup", "range": [0.0, 8.0], "tags": ["zone", "conceal", "vs_flame", "vs_lightning", "vs_projectiles"]}})
	# ---------------------------------------------------------------- guard: Dune Wall / Engulf
	KitEarth.reg("dune_wall", 2, "guard", {"name": "Dune Wall", "part": "sand",
		"desc": "A porous 100 kg sand wall (held: 130 / 160 kg, CP 25 / 32 / 40): captures solids (x1.2), smothers fire (x2), absorbs blasts and sound (x1.5), grounds bolts (cap 60) and turns to glass; water makes it a mud wall. Perfect Engulf: a projectile <= 30 kg is swallowed and spat back.",
		"verb": "barrier", "barrier": "wall", "mat": "sand", "tag": "sand", "source": "ground", "mass": 100.0, "rise": 0.14,
		"half": Vector3(1.2, 0.8, 0.4), "cost": 7.0, "tier_times": [0.6, 1.0, 1.8],
		"tiers": {"t1": {"mass": 115.0, "charge_drain": 0.0}, "t2": {"mass": 130.0}, "t3": {"mass": 160.0}},
		"barrier_cls": "wall_sand",
		"anim": "earth_wall", "fx": {"mat": "sand"},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["wall", "vs_fire", "vs_lightning", "vs_sound", "engulf", "weak_to_water"]}})
	# ---------------------------------------------------------------- push: Dune Push
	KitEarth.reg("dune_push", 2, "push", {"name": "Dune Push", "part": "sand", "module": "kit_earth",
		"desc": "The dune collapses forward into a Sand Surge (its mass - the wall's hold - is the wave's power).",
		"verb": "ground_line", "startup": KitEarth.f(8), "active": KitEarth.f(6), "recovery": KitEarth.f(18), "cancel": 0.6,
		"cost": 4.0, "tag": "sand_surge", "mat": "sand", "speed": 9.0, "budget": 10.0, "width": 2.5, "damage": 12.0, "balance": 40.0,
		"knock": 3.5, "lift": 2.5, "steer": 15.0, "kind": "sand",
		"counter": {"cls": "wave_sand"}, "threat": {"cls": "sand_surge"},
		"anim": "earth_heavy", "fx": {"mat": "sand"},
		"ai": {"role": "counter", "range": [1.0, 10.0], "tags": ["needs_wall", "ground_line", "from_guard"]}})
	# ---------------------------------------------------------------- sink: Quicksand
	KitEarth.reg("quicksand", 2, "sink", {"name": "Quicksand", "part": "sand",
		"desc": "A 3 m pit in front for 5 s (strength by guard hold, CP 18 / 26 / 36 / 50): walkers x0.3 speed, rooted after 1 s; landing solids sink; lava entering crusts to glass; water poured in makes a mud bog.",
		"verb": "zone", "startup": KitEarth.f(8), "active": KitEarth.f(4), "recovery": KitEarth.f(16), "cancel": 0.6,
		"cost": 8.0, "tag": "quicksand", "mat": "sand", "source": "ground", "mass": 20.0, "at": "ahead", "distance": 2.4, "radius": 1.5,
		"life": 5.0, "power": 18.0, "height": 0.8, "rate": 0.1,
		"tiers": {"t1": {"power": 26.0, "radius": 1.6}, "t2": {"power": 36.0, "radius": 1.8}, "t3": {"power": 50.0, "radius": 2.0, "life": 6.0}},
		"counter": {"cls": "quicksand", "power": [18.0, 26.0, 36.0, 50.0]},
		"hook_execute": Callable(EarthSand, "pit_execute"),
		"anim": "earth_wall", "fx": {"mat": "sand"},
		"ai": {"role": "zone", "range": [1.0, 4.0], "tags": ["trap", "root", "sink", "vs_lava_wave", "from_guard"]}})
	# ---------------------------------------------------------------- tech: Sandform / Compress
	KitEarth.reg("sandform", 2, "tech", {"name": "Sandform", "part": "sand", "module": "kit_earth",
		"desc": "Gather sand from the ground (6 kg/s up to 30 kg) or seize the rival's sand (slugs, surges, clouds); release = a sand spear. T+A Compress: sandstone (stone, same mass) - throw it as a stone.",
		"verb": "grip", "startup": KitEarth.f(10), "active": KitEarth.f(4), "recovery": KitEarth.f(18), "cancel": 0.5,
		"cost": 4.0, "ccls": "grip_sand", "reach": 8.0, "cone": 60.0, "base": 0.85, "grip_mult": 1.2, "max_mass": 60.0,
		"rip_source": "ground", "rip_mat": "sand", "rip_mass": 6.0, "rip_time": 0.25, "speed": 22.0, "damage": 10.0, "balance": 22.0,
		"gravity": 0.5, "shape": "", "t_a": "compress", "shape_cost": 3.0, "mode_label": "SAND", "counter": {"cls": "grip_sand"},
		"hook_impact": Callable(EarthSand, "slug_impact"),
		"anim": "earth_hold", "anim_active": "earth_throw", "fx": {"mat": "sand"},
		"ai": {"role": "counter", "range": [0.0, 8.0], "tags": ["reclaim", "vs_sand", "compress", "gather"]}})
	# ---------------------------------------------------------------- evade: Sand Surf (tap)
	KitEarth.reg("sand_surf", 2, "evade", {"name": "Sand Surf", "part": "sand",
		"desc": "A 4 m slide on a skin of sand.",
		"verb": "dash", "startup": 0.0, "active": KitEarth.f(16), "recovery": KitEarth.f(8), "cost": 5.0, "distance": 4.0,
		"iframes": KitEarth.f(9), "dir": "stick",
		"anim": "evade_fwd", "fx": {"mat": "sand"},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["dash"]}})
	# ---------------------------------------------------------------- evade_hold: Sand Surf (ride)
	KitEarth.reg("sand_ride", 2, "evade_hold", {"name": "Sand Surf (ride)", "part": "sand", "module": "kit_earth", "aura_mat": "sand",
		"desc": "Ride a self-made sand wave at 8 m/s while held (10 Focus/s).",
		"verb": "mode", "startup": 0.0, "active": KitEarth.f(10), "recovery": KitEarth.f(8), "cost": 0.0, "kind": "surf", "speed_mult": 1.45,
		"upkeep": 10.0,
		"anim": "glide", "fx": {"mat": "sand", "aura": ""},
		"ai": {"role": "mobility", "range": [0.0, 10.0], "tags": ["mode", "fast"]}})
	CombatWorld.register_body_tick(&"sand_surge", Callable(EarthSand, "surge_tick"))
	CombatWorld.register_body_tick(&"slug", Callable(EarthSand, "slug_tick"))
	CombatWorld.register_body_tick(&"mud", Callable(EarthSand, "mud_tick"))
	CombatWorld.register_zone_effect(&"sand_cloud", Callable(EarthSand, "cloud_effect"))
	CombatWorld.register_zone_effect(&"sandstorm", Callable(EarthSand, "cloud_effect"))
	CombatWorld.register_zone_effect(&"quicksand", Callable(EarthSand, "quicksand_effect"))
	CombatWorld.register_tech_preview(Sim.Element.EARTH, 2, Callable(EarthSand, "preview"))


# ================================================================ Grit Shot / slugs

static func slug_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	for b in VerbProjectile.fire(w, a, inst):
		b.props["cloud_r"] = float(Charge.param(inst, "cloud_r", 0.0))
		b.props["cloud_life"] = float(Charge.param(inst, "cloud_life", 2.5))
		b.props["storm"] = bool(Charge.param(inst, "storm", false))
	return true


## A sand slug bursts: a blinding puff (T0/T1), or its sand becomes a cloud / sandstorm zone (T2/T3).
static func slug_impact(w: CombatWorld, b: MatBody, _what: String) -> bool:
	if b.mat != Sim.Mat.SAND or not b.alive:
		return false
	var owner := w.get_actor(b.attack_owner)
	var r := float(b.props.get("cloud_r", 0.0))
	if r > 0.0:
		var storm := bool(b.props.get("storm", false))
		b.form = Sim.Form.ZONE
		b.tag = &"sandstorm" if storm else &"sand_cloud"
		b.zone_radius = r
		b.radius = r
		b.owner = owner.id if owner != null else -1
		b.power = 12.0 if storm else 8.0
		b.vel = Vector3.ZERO
		b.attack_id = 0
		b.gravity_scale = 0.0
		b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3)
		b.max_life = b.age + float(b.props.get("cloud_life", 2.5))
		b.props["barrier"] = true
		b.props["height"] = 3.0
		b.props["drag"] = 6.0
		b.props["channel"] = "P"
		FxEvents.zone(w, b, "open")
		FxEvents.fx(w, "burst", "sand", {"actor": b.owner, "body": b.id, "pos": b.pos, "radius": r, "power": b.power, "tier": b.tier,
			"element": 0, "sub": 2})
		return true
	VerbVolume.burst_at(w, owner, null, b.pos, {"radius": 1.2, "power": 4.0, "damage": 2.0, "balance": 6.0, "knock": 1.0, "lift": 0.5,
		"cls": "sand", "mat": "sand", "status": "blinded", "status_t": 1.0})
	b.attack_id = 0
	b.vel *= 0.2
	b.props["settle"] = true
	b.max_life = b.age + SETTLE_TIME
	return true


## A spent slug that lies on the ground settles back into it (decay -> ground_returned).
static func slug_tick(_w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.mat == Sim.Mat.SAND and b.attack_id == 0 and b.form != Sim.Form.ZONE and b.controller < 0 and not b.props.has("settle") and b.on_ground:
		b.props["settle"] = true
		b.max_life = b.age + SETTLE_TIME
	return false


# ================================================================ Sandblast

static func blast_execute(_w: CombatWorld, _a: ActorState, inst: ActionInst) -> bool:
	var at := float(Charge.param(inst, "active_t", 0.0))
	if at > 0.0:
		inst.data["active"] = at
	return false


# ================================================================ Sand Surge

## Per tick of a sand surge: it smothers fire fields under its front and buries puddles into mud; when
## it settles, its heap goes back into the ground.
static func surge_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.mat != Sim.Mat.SAND:
		return false
	if b.form != Sim.Form.WAVE:
		if not b.props.has("settle") and b.controller < 0 and b.form != Sim.Form.ZONE:
			b.props["settle"] = true
			b.max_life = b.age + SETTLE_TIME
		return false
	var counter := Agent.of_body(w, b)
	counter.actor = w.get_actor(b.attack_owner)
	for z in w.bodies.duplicate():
		if not z.alive or z == b or z.form != Sim.Form.ZONE or z.mat != Sim.Mat.FIRE:
			continue
		if KitEarth.flat_dist(z.pos, b.pos) > b.wave_width * 0.5 + z.zone_radius:
			continue
		var key := "smother_%d" % z.id
		if b.props.has(key):
			continue
		b.props[key] = true
		Interactions.resolve(w, Agent.of_body(w, z), counter, {"site": "wave"})
	var pd := w.puddle_at(b.pos)
	if pd != null and not b.props.has("mud_%d" % pd.id):
		b.props["mud_%d" % pd.id] = true
		Interactions.resolve(w, Agent.of_body(w, pd), Agent.of_body(w, b), {"site": "wave"})
	return false


# ================================================================ clouds and pits (zone effects)

## Veil of Grit / sandstorm: fighters inside (but the owner) are blinded; the owner inside is concealed.
## A sandstorm also abrades (chip 2/s).
static func cloud_effect(w: CombatWorld, z: MatBody, dt: float) -> void:
	for a in w.actors_in_zone(z):
		if a.id == z.owner:
			Status.apply(w, a, "concealed", 0.25, 1.0, a.id)
			continue
		Status.apply(w, a, "blinded", 0.35, 1.0, z.owner)
		if z.tag == &"sandstorm":
			a.health = maxf(0.0, a.health - 2.0 * dt)


## Quicksand: walkers are mired (x0.3); after 1 s inside (0.6 s in a mud bog) they are rooted briefly.
static func quicksand_effect(w: CombatWorld, z: MatBody, dt: float) -> void:
	var mud := bool(z.props.get("mud", false))
	for a in w.actors:
		var k := "in_%d" % a.id
		if a.health <= 0.0 or a.id == z.owner or not a.grounded or a.flying or Status.immune(a, "ground"):
			z.props.erase(k)
			continue
		if KitEarth.flat_dist(a.pos, z.pos) > z.zone_radius + Sim.ACTOR_RADIUS * 0.5 or absf(a.pos.y - z.pos.y) > 0.6:
			z.props.erase(k)
			continue
		Status.apply(w, a, "mired", 0.2, 1.0, z.owner)
		var t := float(z.props.get(k, 0.0)) + dt
		if t >= (0.6 if mud else 1.0):
			Status.apply(w, a, "rooted", 0.9 if mud else 0.6, 1.0, z.owner)
			t = -0.6
		z.props[k] = t


static func pit_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	inst.data["tier"] = KitEarth.guard_tier(inst)
	var z := VerbZone.spawn(w, a, inst)
	z.static_body = true
	z.props["spare_owner"] = true
	return true


## Mud wall: collapses 4 s after it turned to mud.
static func mud_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.form == Sim.Form.WALL and b.props.get("mud", false) and float(w.tick - int(b.props.get("mud_tick", w.tick))) * Sim.DT >= 4.0:
		w._crumble_wall(b)
		return true
	return false


# ================================================================ module lifecycle (dune_push, sandform)

static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"dune_push":
			inst.data["face"] = w.aim_dir(a, it)
			inst.data["aim"] = inst.data.face
			var wall := w.get_body(int(inst.data.get("wall", a.wall_body)))
			if wall == null or not wall.alive or wall.form != Sim.Form.WALL or wall.mat != Sim.Mat.SAND or wall.last_actor != a.id:
				KitEarth.fizzle(w, a, inst, "material")
				return
			if not Verbs.pay(w, a, inst, "start"):
				inst.data["fizzle"] = true
				return
			inst.data["dune"] = wall.id
			inst.data["keep_wall"] = wall.id
			Verbs.fx(w, a, inst, "cast", {"body": wall.id, "pos": wall.pos})
		_:
			Verbs.on_start(w, a, inst, it)


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	if inst.id == "dune_push":
		return ActionInst.P.ACTIVE
	return Verbs.after_startup(w, a, inst, it)


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	match inst.id:
		"dune_push":
			if p == ActionInst.P.ACTIVE:
				_dune_push(w, a, inst)
			elif p == ActionInst.P.RECOVERY:
				inst.data["keep_wall"] = -1
		_:
			Verbs.on_phase(w, a, inst, p)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"dune_push":
			pass
		"sandform":
			if inst.phase == ActionInst.P.CHANNEL:
				_sandform_tick(w, a, inst, it)
		_:
			Verbs.on_tick(w, a, inst, it)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	match inst.id:
		"dune_push":
			inst.data["keep_wall"] = -1
		_:
			Verbs.on_interrupt(w, a, inst, reason)


## The dune collapses forward: the wall body itself becomes the surge (mass = the dune's hold).
static func _dune_push(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var wall := w.get_body(int(inst.data.get("dune", -1)))
	if wall == null or not wall.alive or wall.form != Sim.Form.WALL:
		return
	if a.wall_body == wall.id:
		a.wall_body = -1
	wall.static_body = false
	wall.form = Sim.Form.CHUNK      # no longer a wall: the pour start isn't blocked by itself
	wall.wall_rise = 0.0
	wall.props.erase("standing")
	w.release_captured(wall)
	inst.data["morph_body"] = wall.id
	inst.data["keep_wall"] = -1
	var tier := 0
	for k in [1, 2, 3]:
		if wall.mass >= [0.0, 115.0, 130.0, 160.0][k] - 1e-6:
			tier = k
	inst.data["tier"] = tier
	var b := VerbGroundLine.launch(w, a, inst, {"source": "held", "mat": "sand", "tag": "sand_surge",
		"width": 2.5 + 0.5 * float(tier), "budget": 10.0 + 1.0 * float(tier)})
	if b != null:
		b.props.erase("mud")
		w.emit("transform", {"body": b.id, "at": b.pos, "from": "wall", "to": "sand_surge", "why": "dune_push"})


# ---------------------------------------------------------------- Sandform

static func _sandform_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var b := w.held(a)
	if b == null and not it.tech_cancel:
		# Seize a sand cloud (zone) of the rival in the aim cone: it gathers into a held sand body.
		var z := _cloud_in_reach(w, a, inst)
		if z != null and inst.t >= 0.1:
			var s := w.grip_strength(a, z, 0.85, float(inst.def.reach)) * float(inst.def.get("grip_mult", 1.2))
			var auth := Interactions.cohesion(z.tier) if z.owner >= 0 and z.owner != a.id else 0.0
			if s > auth + CombatWorld.GRIP_MARGIN or auth <= 0.0:
				FxEvents.zone(w, z, "close")
				z.form = Sim.Form.BLOB
				z.tag = &"slug"
				z.zone_radius = 0.0
				z.power = 0.0
				z.max_life = -1.0
				for k in ["barrier", "height", "drag", "channel"]:
					z.props.erase(k)
				z.update_radius()
				w.take_control(a, z, 0.9, "seize")
				w.emit("reclaim", {"actor": a.id, "body": z.id, "what": "sand_cloud"})
				return
	if b != null and it.attack_pressed and not inst.data.get("shaped", false) and b.mat == Sim.Mat.SAND:
		_compress(w, a, inst, b)
	VerbGrip.tick(w, a, inst, it)
	b = w.held(a)
	if b != null and a.action == inst and inst.phase == ActionInst.P.CHANNEL and it.tech_held:
		if b.mat == Sim.Mat.SAND and b.mass < GATHER_MAX:
			# Gather: more sand rises from the ground into the held body (booked ground_taken).
			if w.spend_focus(a, GATHER_FOCUS * Sim.DT):
				var add := minf(GATHER_RATE * Sim.DT, GATHER_MAX - b.mass)
				var e := b.thermal_energy()
				b.mass += add
				w._set_energy(b, e)
				b.update_radius()
				w.mass_ledger.ground_taken += add
		b.tag = &"sandstone" if b.mat == Sim.Mat.STONE else &"slug"
	if b == null and a.action == inst and inst.phase != ActionInst.P.CHANNEL:
		# Released: the thrown slug bursts like a grit shot.
		for x in w.bodies:
			if x.alive and x.attack_owner == a.id and int(x.props.get("src_attack", -1)) == inst.attack_id and x.mat == Sim.Mat.SAND:
				x.props["on_impact"] = "burst"
				x.props["hit_status"] = "blinded"
				x.props["hit_status_t"] = 1.0
				if x.mass >= 20.0:
					x.props["cloud_r"] = 2.0
					x.props["cloud_life"] = 2.5


## T+A Compress: the held sand becomes sandstone (mat STONE, same mass and heat; booked sand_to_sandstone).
static func _compress(w: CombatWorld, a: ActorState, inst: ActionInst, b: MatBody) -> void:
	if not w.spend_focus(a, float(inst.def.get("shape_cost", 3.0))):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id, "reason": "shape"})
		return
	inst.data["shaped"] = true
	w.convert_mat(b, Sim.Mat.STONE, "sand_to_sandstone")
	b.tag = &"sandstone"
	w.emit("shape", {"actor": a.id, "body": b.id, "shape": "compress"})
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "sand", "to": "sandstone", "why": "compress"})
	Verbs.fx(w, a, inst, "cast", {"body": b.id, "shape": "small"})


static func _cloud_in_reach(w: CombatWorld, a: ActorState, inst: ActionInst) -> MatBody:
	var dir: Vector3 = inst.data.get("aim", a.forward())
	var best: MatBody = null
	var bd := INF
	for z in w.bodies:
		if not z.alive or z.form != Sim.Form.ZONE or z.mat != Sim.Mat.SAND or z.tag == &"quicksand":
			continue
		var to := KitEarth.flat(z.pos - a.pos)
		var d := to.length()
		if d > float(inst.def.reach) + z.zone_radius or (d > 0.6 and to.normalized().dot(dir) < 0.5):
			continue
		if d < bd:
			best = z
			bd = d
	return best


static func preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var d: Dictionary = Moves.DEFS.get("sandform", {})
	var pv := VerbGrip.preview(w, a, d, dir)
	pv["mode"] = "SAND" if int(pv.get("body", -1)) >= 0 else "GATHER"
	pv["ok"] = true
	return pv
