class_name EarthMagma
extends RefCounted
## Earth / Magma (sub 3) - docs/MOVESET.md §7.4. Molten earth under control.
## Heat is paid like Fire (CombatWorld.pay_heat via Verbs.pay: reserve first, then Focus at 10 HU/Focus);
## every HU goes into a body (heat_body), a zone's heat_payload or is booked as spent. Holding molten
## mass costs the existing upkeep (3 Focus/s, insulated: the core's magma-hold rule, enabled while the
## Magma Hold technique runs). Every lava body on the field is material for Magma Surge.

const SURGE_REACH := 6.0
const VEIN_MASS := 8.0
const VEIN_HU := 160.0
const POOL_SET_LIQUID := 0.04     # a lava pool this solid sets into a rock remnant


static func register() -> void:
	Status.register("lava_wade", {"immune": ["burn"], "speed": 0.9})
	# ---------------------------------------------------------------- strike: Ember Clot ... Caldera
	KitEarth.reg("ember_clot", 3, "strike", {"name": "Ember Clot", "part": "magma",
		"desc": "A 4 kg molten glob @18 (burn 2 s). Hold: 3 globs, Magma Bomb (12 kg lobbed, a 2 m lava puddle), Caldera (20 kg lobbed, a 3.5 m lava pool).",
		"verb": "projectile", "startup": KitEarth.f(12), "active": KitEarth.f(4), "recovery": KitEarth.f(18), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "heat": 80.0, "source": "ground", "mat": "stone", "tag": "glob", "mass": 4.0, "speed": 18.0, "gravity": 1.0,
		"damage": 8.0, "balance": 12.0, "hit_status": "burning", "hit_status_t": 2.0,
		"tiers": {"t1": {"count": 3, "spread": 20.0, "heat_add": 160.0, "cost_add": 2.0},
			"t2": {"count": 1, "mass": 12.0, "speed": 14.0, "arc": true, "tag": "bomb", "heat_add": 160.0, "pool_r": 1.0, "damage": 14.0, "balance": 30.0},
			"t3": {"count": 1, "mass": 20.0, "speed": 13.0, "arc": true, "tag": "bomb", "heat_add": 320.0, "pool_r": 1.75, "damage": 18.0, "balance": 40.0}},
		"threat": {"cls": "magma", "power": [7.6, 12.0, 22.0, 35.0]},
		"hook_execute": Callable(EarthMagma, "clot_execute"), "hook_impact": Callable(EarthMagma, "bomb_impact"),
		"anim": "fire_jab", "fx": {"mat": "magma"},
		"ai": {"role": "poke", "range": [2.0, 14.0], "tags": ["projectile", "burn", "makes_lava"]}})
	# ---------------------------------------------------------------- thrust: Lava Lash / Molten Lance
	KitEarth.reg("lava_lash", 3, "thrust", {"name": "Lava Lash", "part": "magma",
		"desc": "A lava whip 6 m 25° (melts ice x3, burns vines x3). Hold: 7 m, a 0.6 s stream, Molten Lance (a 10 m jet that pours 300 HU into a wall face: two lances slump a Bulwark).",
		"verb": "beam", "startup": KitEarth.f(12), "active": KitEarth.f(8), "recovery": KitEarth.f(20), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "heat": 60.0, "cls": "flame", "channel": "H", "power": 6.0, "range": 6.0, "angle": 14.0, "width": 0.6,
		"damage": 8.0, "balance": 12.0, "knock": 2.0, "status": "burning", "status_t": 1.5,
		"tiers": {"t1": {"power": 9.0, "range": 7.0, "heat_add": 30.0, "cost_add": 2.0, "damage": 10.0},
			"t2": {"power": 14.0, "range": 7.0, "pulse": 0.15, "active_t": 0.6, "heat_add": 60.0, "damage": 5.0},
			"t3": {"power": 22.0, "range": 10.0, "pulse": 0.15, "active_t": 1.0, "heat_add": 300.0, "damage": 6.0, "face_hu": 300.0}},
		"counter": {"cls": "flame", "power": [6.0, 9.0, 14.0, 22.0]},
		"hook_execute": Callable(EarthMagma, "lash_execute"),
		"anim": "water_whip", "fx": {"mat": "magma"},
		"ai": {"role": "poke", "range": [1.0, 10.0], "tags": ["cone", "beam", "vs_ice", "vs_vine", "softens_walls"]}})
	# ---------------------------------------------------------------- ground: Magma Surge / Lava Tide
	KitEarth.reg("magma_surge", 3, "ground", {"name": "Magma Surge", "part": "magma",
		"desc": "Re-pour every molten body within 6 m (yours, the rival's, a slumped wall, lava pools, rifts) as a wave toward the aim - push it back on them. No lava near: raise an 8 kg vein (+160 HU). Hold: more budget; T3 Lava Tide merges all of it into one wave.",
		"verb": "ground_line", "startup": KitEarth.f(18), "active": KitEarth.f(6), "recovery": KitEarth.f(22), "cancel": 0.6, "chain": 0.25,
		"cost": 8.0, "reach": SURGE_REACH, "budget_mult": 1.0,
		"tiers": {"t1": {"budget_mult": 1.3, "cost_add": 2.0}, "t2": {"budget_mult": 1.6}, "t3": {"budget_mult": 1.8, "merge": true}},
		"counter": {"cls": "wave_lava"}, "threat": {"cls": "lava_wave"},
		"hook_execute": Callable(EarthMagma, "surge_execute"),
		"anim": "pour", "fx": {"mat": "magma"},
		"ai": {"role": "finisher", "range": [1.0, 12.0], "tags": ["ground_line", "reclaim_lava", "push_back", "needs_lava"]}})
	# ---------------------------------------------------------------- sweep: Spatter Arc
	KitEarth.reg("spatter_arc", 3, "sweep", {"name": "Spatter Arc", "part": "magma",
		"desc": "5 x 1 kg molten droplets fanned 70° (burn, scorch, ignite vines). Hold: 7, 9, then a ring.",
		"verb": "projectile", "startup": KitEarth.f(12), "active": KitEarth.f(6), "recovery": KitEarth.f(18), "cancel": 0.6, "chain": 0.25,
		"cost": 5.0, "heat": 50.0, "source": "ground", "mat": "stone", "tag": "ember", "mass": 1.0, "count": 5, "spread": 70.0, "speed": 13.0,
		"gravity": 1.0, "reach": 6.0, "damage": 3.0, "balance": 5.0, "hit_status": "burning", "hit_status_t": 1.0,
		"tiers": {"t1": {"count": 7, "heat_add": 20.0, "cost_add": 2.0}, "t2": {"count": 9, "heat_add": 40.0, "speed": 15.0, "reach": 7.0},
			"t3": {"count": 12, "spread": 330.0, "heat_add": 70.0, "reach": 6.0}},
		"threat": {"cls": "hot_rock", "power": [2.0, 2.0, 2.0, 2.0]},
		"hook_execute": Callable(EarthMagma, "clot_execute"),
		"anim": "fire_release", "fx": {"mat": "magma", "shape": "fan"},
		"ai": {"role": "poke", "range": [1.0, 7.0], "tags": ["projectile", "area", "burn"]}})
	# ---------------------------------------------------------------- guard: Magma Curtain / Obsidian Set
	KitEarth.reg("magma_curtain", 3, "guard", {"name": "Magma Curtain", "part": "magma", "module": "kit_earth",
		"desc": "A 100 kg stone wall with a molten face (CP 28; set by water: obsidian 38, brittle to sound). Small solids stick in the face and heat; lava is absorbed; flames feed it; water bursts to steam. Perfect: the projectile fuses into the wall.",
		"verb": "barrier", "barrier": "wall", "mat": "stone", "tag": "obsidian", "source": "ground", "mass": 100.0, "hardness": 0.28,
		"rise": 0.14, "half": Vector3(1.1, 0.75, 0.32), "cost": 10.0, "heat": 150.0, "barrier_cls": "wall_obsidian",
		"anim": "earth_wall", "fx": {"mat": "magma"},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["wall", "absorbs_lava", "vs_ice", "vs_flame", "sets_obsidian"]}})
	# ---------------------------------------------------------------- push: Slag Wave
	KitEarth.reg("slag_wave", 3, "push", {"name": "Slag Wave", "part": "magma", "module": "kit_earth",
		"desc": "The curtain's molten face slumps forward as a 15 kg lava wave (+150 HU).",
		"verb": "pour", "startup": KitEarth.f(10), "active": KitEarth.f(6), "recovery": KitEarth.f(18), "cancel": 0.6,
		"cost": 4.0, "heat": 150.0, "mass": 15.0, "threat": {"cls": "lava_wave"},
		"anim": "pour", "fx": {"mat": "magma"},
		"ai": {"role": "counter", "range": [1.0, 8.0], "tags": ["needs_wall", "ground_line", "from_guard"]}})
	# ---------------------------------------------------------------- sink: Melt Pit
	KitEarth.reg("melt_pit", 3, "sink", {"name": "Melt Pit", "part": "magma",
		"desc": "A 2.5 m molten pit in front for 2 s (strength by guard hold, CP 20 / 28 / 38 / 50): landing solids melt into it while its heat lasts; ice boils; walkers burn.",
		"verb": "zone", "startup": KitEarth.f(8), "active": KitEarth.f(4), "recovery": KitEarth.f(16), "cancel": 0.6,
		"cost": 8.0, "heat": 120.0, "tag": "melt_pit", "mat": "air", "at": "ahead", "distance": 2.2, "radius": 1.25, "life": 2.0, "power": 20.0,
		"height": 0.8, "rate": 0.1,
		"tiers": {"t1": {"power": 28.0, "radius": 1.4, "life": 2.5, "pit_heat_add": 60.0}, "t2": {"power": 38.0, "radius": 1.6, "life": 3.0, "pit_heat_add": 120.0},
			"t3": {"power": 50.0, "radius": 1.8, "life": 3.5, "pit_heat_add": 200.0}},
		"counter": {"cls": "melt_pit", "power": [20.0, 28.0, 38.0, 50.0]},
		"hook_execute": Callable(EarthMagma, "pit_execute"),
		"anim": "earth_wall", "fx": {"mat": "magma"},
		"ai": {"role": "zone", "range": [1.0, 4.0], "tags": ["trap", "melt", "vs_ice", "vs_stone", "from_guard"]}})
	# ---------------------------------------------------------------- tech: Magma Hold / Cool & Set / Reverse Tide
	KitEarth.reg("magma_hold", 3, "tech", {"name": "Magma Hold", "part": "magma", "module": "kit_earth",
		"desc": "Seize molten or hot stone at 8 m without burns (blobs, slumped walls, lava pools, the rival's waves); release = pour a wave (molten) or throw (solid). T+A Cool & Set: dump the heat into the ground - solid rock in hand. On an enemy lava wave: a grip contest vs the pourer's authority; win = the wave turns around (Reverse Tide).",
		"verb": "grip", "startup": KitEarth.f(10), "active": KitEarth.f(4), "recovery": KitEarth.f(18), "cancel": 0.5,
		"cost": 6.0, "ccls": "grip_magma", "reach": 8.0, "cone": 60.0, "base": 0.85, "grip_mult": 1.3, "max_mass": 80.0, "rip_source": "none",
		"speed": 16.0, "damage": 14.0, "balance": 30.0, "gravity": 1.0, "shape": "", "t_a": "cool", "shape_cost": 3.0, "mode_label": "MAGMA",
		"counter": {"cls": "grip_magma"},
		"anim": "magma_hold", "anim_active": "pour", "fx": {"mat": "magma"},
		"ai": {"role": "counter", "range": [0.0, 8.0], "tags": ["reclaim", "vs_lava_wave", "reverse_tide", "vs_hot_rock", "cool"]}})
	# ---------------------------------------------------------------- evade: Cinder Step
	KitEarth.reg("cinder_step", 3, "evade", {"name": "Cinder Step", "part": "magma",
		"desc": "A 3.5 m dash leaving an ember trail.",
		"verb": "dash", "startup": 0.0, "active": KitEarth.f(14), "recovery": KitEarth.f(8), "cost": 5.0, "distance": 3.5,
		"iframes": KitEarth.f(9), "dir": "stick",
		"anim": "evade_fwd", "fx": {"mat": "magma"},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["dash"]}})
	# ---------------------------------------------------------------- evade_hold: Lava Wade
	KitEarth.reg("lava_wade", 3, "evade_hold", {"name": "Lava Wade", "part": "magma", "module": "kit_earth", "aura_mat": "magma",
		"desc": "While held: immune to lava and hot-rock burns, walk through lava at x0.6 (10 Focus/s).",
		"verb": "stance", "startup": 0.0, "active": KitEarth.f(10), "recovery": KitEarth.f(8), "cost": 0.0, "stance": "lava_wade",
		"status": "lava_wade", "speed_mult": 0.6, "upkeep": 10.0,
		"anim": "walk", "fx": {"mat": "magma", "aura": ""},
		"ai": {"role": "mobility", "range": [0.0, 6.0], "tags": ["stance", "burn_immune"]}})
	CombatWorld.register_zone_effect(&"lava_pool", Callable(EarthMagma, "pool_effect"))
	CombatWorld.register_zone_effect(&"melt_pit", Callable(EarthMagma, "pit_effect"))
	CombatWorld.register_tech_preview(Sim.Element.EARTH, 3, Callable(EarthMagma, "preview"))


# ================================================================ Ember Clot / Spatter (heat into the globs)

## Fires the globs (ground stone) and moves the paid heat into them (heat_body; what a body can't take
## is spent). Bombs keep their pool radius for the impact.
static func clot_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var bodies := VerbProjectile.fire(w, a, inst)
	var hu := Verbs.take_heat(inst)
	if bodies.is_empty():
		w.ledger.spent += hu
		return true
	var share := hu / float(bodies.size())
	for b in bodies:
		var used := w.heat_body(b, share)
		w.ledger.spent += share - used
		Thermal.update_phase(b)
		if b.liquid > 0.5:
			b.form = Sim.Form.BLOB
		var pr := float(Charge.param(inst, "pool_r", 0.0))
		if pr > 0.0:
			b.props["pool_r"] = pr
			b.props["on_impact"] = "zone"
	return true


## A Magma Bomb / Caldera lands: its lava spreads into a lava pool ZONE (the same body, mass and heat:
## material for Magma Surge). Small globs just land as lava blobs.
static func bomb_impact(w: CombatWorld, b: MatBody, what: String) -> bool:
	var r := float(b.props.get("pool_r", 0.0))
	if r <= 0.0 or not b.alive or not b.is_stone():
		return true
	var owner := w.get_actor(b.attack_owner)
	b.form = Sim.Form.ZONE
	b.tag = &"lava_pool"
	b.zone_radius = r
	b.radius = r
	b.owner = owner.id if owner != null else -1
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.gravity_scale = 0.0
	b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4)
	b.max_life = -1.0
	b.props["height"] = 0.6
	b.props["rate"] = 0.25
	b.props["drag"] = 12.0
	b.props["landed_on"] = what
	FxEvents.zone(w, b, "open")
	FxEvents.fx(w, "splash", "magma", {"actor": b.owner, "body": b.id, "pos": b.pos, "radius": r, "element": 0, "sub": 3})
	return true


## Lava pool: fighters in it burn (unless wading); a set pool becomes a rock remnant.
static func pool_effect(w: CombatWorld, z: MatBody, _dt: float) -> void:
	if not z.is_stone():
		return
	if z.liquid <= POOL_SET_LIQUID:
		FxEvents.zone(w, z, "close")
		z.form = Sim.Form.CHUNK
		z.tag = &""
		z.zone_radius = 0.0
		z.update_radius()
		z.on_ground = true
		z.max_life = z.age + Sim.REMNANT_LIFETIME
		w.emit("transform", {"body": z.id, "at": z.pos, "from": "lava", "to": "rock", "why": "cooled"})
		return
	for a in w.actors:
		if a.health <= 0.0 or not a.grounded or Status.immune(a, "burn"):
			continue
		if KitEarth.flat_dist(a.pos, z.pos) <= z.zone_radius and absf(a.pos.y - z.pos.y) < 0.5:
			Status.apply(w, a, "burning", 0.6, 1.0, z.owner)


# ================================================================ Lava Lash / Molten Lance

static func lash_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	if float(Charge.param(inst, "pulse", 0.0)) <= 0.0:
		VerbVolume.cone(w, a, inst)
		return true
	inst.data["active"] = float(Charge.param(inst, "active_t", 0.6))
	var face_hu := float(Charge.param(inst, "face_hu", 0.0))
	if face_hu > 0.0:
		var dir: Vector3 = inst.data.get("face", a.forward())
		var from := a.chest()
		var to := from + dir * float(Charge.param(inst, "range", 10.0))
		var best: MatBody = null
		var bt := INF
		for b in w.bodies:
			if b.alive and b.form == Sim.Form.WALL and b.wall_rise > 0.5 and b.mat == Sim.Mat.STONE:
				var t := w.wall_segment_t(from, to, b)
				if t >= 0.0 and t < bt:
					bt = t
					best = b
		if best != null:
			var hu := minf(face_hu, float(inst.data.get("heat_paid", 0.0)))
			inst.data["heat_paid"] = float(inst.data.get("heat_paid", 0.0)) - hu
			pour_face(w, a, inst, best, hu)
	return false


## Molten Lance into a stone wall: `hu` goes into the wall (booked); the face shell (25 %) accumulates
## it, and once it holds enough to be half molten the face slumps on the caster's side and the rest
## crumbles (VerbHeat slump) - ready for Magma Surge.
static func pour_face(w: CombatWorld, a: ActorState, inst: ActionInst, wall: MatBody, hu: float) -> void:
	if hu <= 0.0 or not wall.alive:
		return
	var used := w.heat_body(wall, hu)
	w.ledger.spent += hu - used
	wall.props["face_hu"] = float(wall.props.get("face_hu", 0.0)) + used
	w.emit("heating", {"actor": a.id, "body": wall.id, "temp": wall.temp, "wall": wall.id, "face_hu": wall.props.face_hu})
	var shell := wall.mass * 0.25
	var need := shell * (Sim.STONE_C * (Sim.STONE_MELT_C - Sim.AMBIENT_C) + Sim.STONE_LATENT * 0.5)
	if float(wall.props.face_hu) < need:
		return
	var n := Vector3(sin(wall.wall_yaw), 0.0, cos(wall.wall_yaw))
	if n.dot(a.pos - wall.pos) < 0.0:
		n = -n
	var p := wall.pos + n * (wall.wall_half.z + 0.05) + Vector3(0, wall.wall_half.y, 0)
	var face := w.split_body(wall, shell, p)
	face.form = Sim.Form.CHUNK
	face.vel = Vector3.ZERO
	face.props["face_n"] = n
	face.update_radius()
	# The heat poured into the face concentrates there (moved from the rest of the wall, exact): up to
	# 80 % molten when the wall holds that much.
	var want := shell * (Sim.STONE_C * (Sim.STONE_MELT_C - Sim.AMBIENT_C) + Sim.STONE_LATENT * 0.8) - maxf(0.0, face.thermal_energy())
	var avail := maxf(0.0, wall.thermal_energy())
	var move := clampf(want, 0.0, avail)
	if move > 0.0:
		var got := -Thermal.heat(wall, -move)
		var put := w.heat_body(face, got)
		w.ledger.spent += got - put
	Thermal.update_phase(face)
	wall.props.erase("face_hu")
	VerbHeat._slump(w, a, inst, wall, face)


# ================================================================ Magma Surge / Lava Tide

static func surge_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var reach := float(inst.def.get("reach", SURGE_REACH))
	var mult := float(Charge.param(inst, "budget_mult", 1.0))
	var src: Array[MatBody] = []
	for b in w.bodies:
		if not b.alive or not b.is_stone() or b.liquid < 0.25 or b.controller >= 0 or b.captured_by >= 0:
			continue
		if b.form == Sim.Form.WALL or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE:
			continue
		if b.form == Sim.Form.WAVE and b.attack_id != 0 and b.attack_owner != a.id and b.tag != &"magma_rift":
			continue   # a live enemy wave: Reverse Tide (Magma Hold) or a clash, not a re-pour (rift channels can be)
		if b.static_body and not b.props.has("face_of"):
			continue
		if KitEarth.flat_dist(b.pos, a.pos) > reach + b.radius:
			continue
		src.append(b)
	src.sort_custom(func(x: MatBody, y: MatBody) -> bool:
		var dx := KitEarth.flat_dist(x.pos, a.pos)
		var dy := KitEarth.flat_dist(y.pos, a.pos)
		return dx < dy - 1e-6 or (absf(dx - dy) <= 1e-6 and x.id < y.id))
	var target := Verbs.target_point(w, a, inst)
	var waves: Array[MatBody] = []
	if src.is_empty():
		# No lava near: raise an 8 kg vein from the ground and melt it (+160 HU, reserve first).
		var hu := w.pay_heat(a, VEIN_HU, false)
		if hu <= 0.0:
			w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id, "reason": "vein"})
			return true
		var ps := ActFire._pour_start(w, a, inst.data.get("face", a.forward()))
		var v := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, VEIN_MASS, ps.pos, "ground@%.1f,%.1f" % [ps.pos.x, ps.pos.z])
		w.mass_ledger.ground_taken += VEIN_MASS
		var used := w.heat_body(v, hu)
		w.ledger.spent += hu - used
		Thermal.update_phase(v)
		src.append(v)
	elif bool(Charge.param(inst, "merge", false)) and src.size() > 1:
		# Lava Tide: everything flows together into the largest body first.
		var big := src[0]
		for b in src:
			if b.mass > big.mass:
				big = b
		for b in src:
			if b != big and b.alive:
				if b.form == Sim.Form.ZONE:
					FxEvents.zone(w, b, "close")
				w.merge_bodies(big, b)
		src = [big]
	for b in src:
		var dir := KitEarth.flat(target - b.pos)
		if dir.length() < 0.5:
			dir = KitEarth.flat(inst.data.get("face", a.forward()))
		var wv := KitEarth.pour_wave(w, a, inst, b, dir, mult, "magma_surge")
		waves.append(wv)
		Verbs.fx(w, a, inst, "release", {"body": wv.id, "pos": wv.pos, "dir": wv.wave_dir, "length": wv.wave_budget, "radius": wv.wave_width})
	inst.data["bodies"] = waves.map(func(x: MatBody) -> int: return x.id)
	w.emit("magma_surge", {"actor": a.id, "waves": inst.data.bodies, "tier": inst.tier()})
	return true


# ================================================================ Melt Pit

static func pit_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	inst.data["tier"] = KitEarth.guard_tier(inst)
	var extra := float(Charge.param(inst, "pit_heat_add", 0.0))
	if extra > 0.0:
		Verbs.pay(w, a, inst, {"heat": extra})
	var z := VerbZone.spawn(w, a, inst)
	z.static_body = true
	z.heat_payload = Verbs.take_heat(inst)
	z.props["spare_owner"] = true
	return true


## Melt Pit: walkers burn; the pit closes when its heat is spent.
static func pit_effect(w: CombatWorld, z: MatBody, _dt: float) -> void:
	for a in w.actors:
		if a.health <= 0.0 or a.id == z.owner or not a.grounded or Status.immune(a, "burn"):
			continue
		if KitEarth.flat_dist(a.pos, z.pos) <= z.zone_radius and absf(a.pos.y - z.pos.y) < 0.5:
			Status.apply(w, a, "burning", 0.5, 1.0, z.owner)
			Status.apply(w, a, "slowed", 0.2, 1.0, z.owner)


# ================================================================ module lifecycle (curtain spec, slag_wave, magma_hold)

static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.id == "guard":
		VerbBarrier.start(w, a, inst, it)
		var wall := w.get_body(int(inst.data.get("wall", -1)))
		var hu := Verbs.take_heat(inst)
		if wall != null and wall.alive:
			wall.heat_payload += hu   # the molten face (counted by thermal_energy, booked when it sinks)
			wall.props["face"] = true
		else:
			w.ledger.spent += hu
		return
	inst.data["face"] = w.aim_dir(a, it)
	inst.data["aim"] = inst.data.face
	inst.data["aim_active"] = it.aim_active
	inst.data["aim_point"] = w.aim_point(a, it)
	match inst.id:
		"slag_wave":
			var wall := w.get_body(int(inst.data.get("wall", a.wall_body)))
			if wall == null or not wall.alive or wall.form != Sim.Form.WALL or wall.last_actor != a.id or wall.heat_payload <= 1.0:
				KitEarth.fizzle(w, a, inst, "material")
				return
			if not Verbs.pay(w, a, inst, "start"):
				inst.data["fizzle"] = true
				return
			inst.data["curtain"] = wall.id
			inst.data["keep_wall"] = wall.id
			Verbs.fx(w, a, inst, "cast", {"body": wall.id})
		"magma_hold":
			Verbs.on_start(w, a, inst, it)
			if not inst.data.get("fizzle", false) and not a.has("magma"):
				a.kit["magma"] = true          # insulated hold: the core magma-hold upkeep (3 Focus/s)
				inst.data["kit_magma"] = true
		_:
			Verbs.on_start(w, a, inst, it)


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	if inst.id == "slag_wave":
		return ActionInst.P.ACTIVE
	return Verbs.after_startup(w, a, inst, it)


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if inst.id == "guard":
		if p == ActionInst.P.RECOVERY:
			VerbBarrier.end(w, a, inst, "release")
		return
	match inst.id:
		"slag_wave":
			if p == ActionInst.P.ACTIVE:
				_slag(w, a, inst)
			elif p == ActionInst.P.RECOVERY:
				inst.data["keep_wall"] = -1
				w.ledger.spent += Verbs.take_heat(inst)
		"magma_hold":
			Verbs.on_phase(w, a, inst, p)
			if p == ActionInst.P.RECOVERY or p == ActionInst.P.ACTIVE:
				_unkit(a, inst)
		_:
			Verbs.on_phase(w, a, inst, p)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.id == "guard":
		VerbBarrier.tick(w, a, inst, it)
		return
	match inst.id:
		"slag_wave":
			pass
		"magma_hold":
			if inst.phase == ActionInst.P.CHANNEL:
				_hold_tick(w, a, inst, it)
		_:
			Verbs.on_tick(w, a, inst, it)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	if inst.id == "guard":
		VerbBarrier.end(w, a, inst, reason)
		return
	match inst.id:
		"slag_wave":
			inst.data["keep_wall"] = -1
			w.ledger.spent += Verbs.take_heat(inst)
		"magma_hold":
			Verbs.on_interrupt(w, a, inst, reason)
			_unkit(a, inst)
		_:
			Verbs.on_interrupt(w, a, inst, reason)


static func _unkit(a: ActorState, inst: ActionInst) -> void:
	if inst.data.get("kit_magma", false):
		a.kit.erase("magma")
		inst.data["kit_magma"] = false


## Slag Wave: the curtain's molten face (its heat_payload) + 15 kg of the wall pour forward as lava.
static func _slag(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var wall := w.get_body(int(inst.data.get("curtain", -1)))
	if wall == null or not wall.alive:
		return
	var m := minf(float(inst.def.get("mass", 15.0)), wall.mass * 0.5)
	var dir: Vector3 = inst.data.face
	var p := wall.pos + KitEarth.flat(dir).normalized() * (wall.wall_half.z + 0.7)
	var slag := w.split_body(wall, m, p)
	slag.form = Sim.Form.BLOB
	slag.tag = &""
	slag.wall_half = Vector3(1.0, 0.6, 0.25)
	slag.static_body = false
	var hp := wall.heat_payload + slag.heat_payload   # split_body gave the slag its share of the face
	wall.heat_payload = 0.0
	slag.heat_payload = 0.0
	wall.props.erase("face")
	var hu := hp + Verbs.take_heat(inst)
	var used := w.heat_body(slag, hu)
	w.ledger.spent += hu - used
	Thermal.update_phase(slag)
	var wv := KitEarth.pour_wave(w, a, inst, slag, dir, 1.0, "slag_wave")
	Verbs.fx(w, a, inst, "release", {"body": wv.id, "pos": wv.pos, "dir": wv.wave_dir, "length": wv.wave_budget})


# ---------------------------------------------------------------- Magma Hold / Cool & Set / Reverse Tide

static func _hold_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var b := w.held(a)
	if b == null and not it.tech_cancel:
		var tgt := _seek_target(w, a, inst, it)
		if tgt != null and tgt.form == Sim.Form.WAVE and tgt.attack_id != 0 and tgt.attack_owner != a.id:
			_reverse_tide(w, a, inst, tgt)
			return
		if tgt != null and tgt.form == Sim.Form.ZONE:
			# A lava pool gathers into a molten blob that can be seized.
			FxEvents.zone(w, tgt, "close")
			tgt.form = Sim.Form.BLOB
			tgt.tag = &""
			tgt.zone_radius = 0.0
			tgt.update_radius()
		if tgt != null and tgt.static_body and tgt.props.has("face_of"):
			tgt.static_body = false
	if b == null:
		VerbGrip.tick(w, a, inst, it)
		return
	inst.data["aim"] = w.aim_dir(a, it)
	inst.data["aim_active"] = it.aim_active
	inst.data["face"] = inst.data.aim
	inst.data["aim_point"] = w.aim_point(a, it)
	if it.tech_cancel:
		VerbGrip.drop(w, a, inst)
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var dir: Vector3 = inst.data.aim
	b.hold_point = a.pos + Vector3(0, 1.2, 0) + a.forward() * (0.3 + b.radius) + dir * 0.15
	if it.attack_pressed and not inst.data.get("shaped", false):
		_cool(w, a, inst, b)
	if not it.tech_held:
		w.set_phase(a, inst, ActionInst.P.ACTIVE)
		if b.is_stone() and b.liquid > 0.3:
			var tp := Verbs.target_point(w, a, inst)
			var wv := KitEarth.pour_wave(w, a, inst, b, tp - b.pos, 1.0, "magma_hold")
			wv.pos = ActFire._pour_start(w, a, KitEarth.flat(tp - a.pos).normalized()).pos
			wv.wave_path = PackedVector3Array([wv.pos])
			Verbs.fx(w, a, inst, "release", {"body": wv.id, "pos": wv.pos, "dir": wv.wave_dir, "length": wv.wave_budget})
		else:
			VerbGrip.throw(w, a, inst)


## T+A Cool & Set: the held lava's heat is dumped into the ground (booked removed): solid rock in hand.
static func _cool(w: CombatWorld, a: ActorState, inst: ActionInst, b: MatBody) -> void:
	if not w.spend_focus(a, float(inst.def.get("shape_cost", 3.0))):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id, "reason": "shape"})
		return
	inst.data["shaped"] = true
	var e := maxf(0.0, b.thermal_energy() - b.heat_payload)
	var got := -Thermal.heat(b, -e)
	w.ledger.removed += got
	Thermal.update_phase(b)
	if b.form == Sim.Form.BLOB:
		b.form = Sim.Form.CHUNK
	w.emit("shape", {"actor": a.id, "body": b.id, "shape": "cool"})
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "lava", "to": "rock", "why": "cool_and_set"})
	Verbs.fx(w, a, inst, "cast", {"body": b.id, "shape": "ground"})


static func _seek_target(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> MatBody:
	var reach := float(inst.def.reach)
	var dir: Vector3 = inst.data.get("aim", a.forward())
	var f := func(x: MatBody) -> bool:
		if x.controller == a.id or x.form == Sim.Form.POOL or x.form == Sim.Form.WALL or x.captured_by >= 0:
			return false
		if x.form == Sim.Form.ZONE:
			return x.is_stone() and x.tag == &"lava_pool"
		return Interactions.allows(x, &"grip_magma")
	return w.find_body(a, dir, reach, float(inst.def.get("cone", 60.0)), f)


## Reverse Tide: grip contest against the pourer's authority (cohesion 0.6 + 0.1·tier of the wave, plus
## the grip margin). Win: the wave turns around and is now yours. Lose: control_fail, the wave comes on.
static func _reverse_tide(w: CombatWorld, a: ActorState, inst: ActionInst, wave: MatBody) -> void:
	if inst.data.get("tide_tried", -1) == wave.id:
		return
	inst.data["tide_tried"] = wave.id
	var s := w.grip_strength(a, wave, float(inst.def.get("base", 0.85)), float(inst.def.reach)) * float(inst.def.get("grip_mult", 1.3))
	# The pourer's authority: the wave's cohesion plus its momentum of mass (bigger waves are harder to turn).
	var auth := Interactions.cohesion(wave.tier) + 0.25 * clampf(wave.mass / 40.0, 0.0, 1.5)
	var pourer := w.get_actor(wave.attack_owner)
	var ok := s > auth + CombatWorld.GRIP_MARGIN and wave.mass <= a.max_control_mass
	w.emit("interaction", {"threat": "lava_wave", "counter": "grip_magma", "outcome": "reclaim" if ok else "overwhelm",
		"band": "full" if ok else "fail", "ratio": s / maxf(auth + CombatWorld.GRIP_MARGIN, 1e-3), "tp": auth, "cp": s, "perfect": false,
		"pos": wave.pos, "dir": wave.wave_dir, "threat_actor": pourer.id if pourer != null else -1, "counter_actor": a.id,
		"threat_body": wave.id, "counter_body": -1, "to": "", "rule": "reverse_tide", "tier": inst.tier()})
	if not ok:
		w.emit("control_fail", {"actor": a.id, "body": wave.id, "reason": "contest"})
		w.emit("whiff", {"actor": a.id, "move": inst.id, "body": wave.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var back := -wave.wave_dir
	if pourer != null:
		var to := KitEarth.flat(pourer.pos - wave.pos)
		if to.length() > 0.5:
			back = to.normalized()
	KitEarth.pour_wave(w, a, inst, wave, back, 1.0, "reverse_tide")
	w.emit("reverse_tide", {"actor": a.id, "body": wave.id, "from": pourer.id if pourer != null else -1})
	Verbs.fx(w, a, inst, "cast", {"body": wave.id, "pos": wave.pos, "dir": back})
	w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var d: Dictionary = Moves.DEFS.get("magma_hold", {})
	var inst := ActionInst.new()
	inst.def = d
	inst.data["aim"] = dir
	var b := _seek_target(w, a, inst, null)
	if b == null:
		return {"mode": "MAGMA", "body": -1, "ok": false, "reason": "target"}
	if b.form == Sim.Form.WAVE and b.attack_id != 0 and b.attack_owner != a.id:
		return {"mode": "REVERSE", "body": b.id, "ok": true, "reason": ""}
	return {"mode": "MAGMA", "body": b.id, "ok": b.mass <= a.max_control_mass, "reason": "" if b.mass <= a.max_control_mass else "mass"}
