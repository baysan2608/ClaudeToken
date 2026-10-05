class_name WaterIce
extends RefCounted
## Water / Ice (sub 1, docs/MOVESET.md §7.6): solid water, brittle and insulating. Its water comes from ambient
## moisture (fantasy, booked in mass_ledger.moisture_taken) or any water source. Everything freezes through
## WaterUtil.freeze_body (heat booked in freeze_dump).
##
## strike Frost Shard -> Ice Lance -> Glacier Spear -> Frost Comet      thrust Icicle Volley
## ground Rime Path -> Frozen Fangs -> Glacier Ridge                    sweep  Hoarfrost Fan
## guard  Ice Wall (+ Flash Freeze, the perfect)                         push   Glacier Shove
## sink   Frost Floor                                                    tech   Freeze-Draw (+ Shatter)
## evade  Ice Glide                                                      hold   Skate

const E := 1
const SUB := 1


static func _s(frames: float) -> float:
	return frames / 60.0


static func register() -> void:
	_strike()
	_thrust()
	_ground()
	_sweep()
	_guard()
	_push_sink()
	_tech()
	_mobility()
	for pair in [["strike", "frost_shard"], ["thrust", "icicle_volley"], ["ground", "rime_path"], ["sweep", "hoarfrost_fan"],
			["guard", "ice_wall"], ["push", "glacier_shove"], ["sink", "frost_floor"], ["tech", "freeze_draw"],
			["evade", "ice_glide"], ["evade_hold", "skate"]]:
		Moves.bind(E, SUB, pair[0], pair[1])
	KitWater.handle("ice_wall", {"start": Callable(WaterIce, "wall_start")})
	KitWater.handle("ice_glide", {"start": Callable(WaterIce, "glide_start"), "after": Callable(WaterWater, "after_active"),
		"tick": Callable(WaterWater, "evade_tick"), "phase": Callable(WaterWater, "evade_phase")})
	KitWater.handle("freeze_draw", {"tick": Callable(WaterIce, "freeze_draw_tick")})
	KitWater.handle("skate", {"after": Callable(WaterIce, "skate_after"), "phase": Callable(WaterIce, "skate_phase"),
		"interrupt": Callable(WaterIce, "skate_interrupt")})


# ------------------------------------------------------------------ strike: Frost Shard ladder

static func _strike() -> void:
	Moves.register("frost_shard", {
		"element": E, "sub": SUB, "slot": "strike", "name": "Frost Shard",
		"desc": "A quick ice needle that chills. Held: Ice Lance, then the Glacier Spear (pierces one body and stands as an ice spike), then the Frost Comet, lobbed, which shatters into six shards.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "source": "moisture", "mat": "water", "frozen": true, "mass": 1.0, "speed": 26.0, "gravity": 0.25, "tag": "needle",
		"damage": 6.0, "balance": 10.0, "hit_status": "chilled", "hit_status_t": 1.5,
		"tiers": {
			"t1": {"mass": 4.0, "speed": 28.0, "tag": "lance", "damage": 12.0, "balance": 22.0, "cost_add": 2.0, "hit_status_t": 2.0},
			"t2": {"mass": 8.0, "speed": 30.0, "tag": "spear", "form": "chunk", "pierce": 1, "on_impact": "stick", "damage": 18.0, "balance": 32.0, "cost_add": 4.0,
				"hit_status_t": 2.5, "gravity": 0.15},
			"t3": {"mass": 15.0, "speed": 20.0, "tag": "comet", "form": "shard", "arc": true, "gravity": 1.0, "on_impact": "shatter", "impact_pieces": 6,
				"damage": 26.0, "balance": 46.0, "cost_add": 7.0, "hit_status_t": 3.0, "pierce": 0},
		},
		"hook_impact": Callable(WaterIce, "spear_impact"),
		"counter": {"cls": "frost", "power": [1.3, 5.7, 12.0, 15.0]}, "threat": {"cls": "ice"},
		"anim": "water_freeze", "fx": {"mat": "ice", "shape": "needles"},
		"ai": {"role": "poke", "range": [3.0, 16.0], "tags": ["chill", "pierce", "shatter"]},
	})


## T2 spear: after piercing a body (or on the ground) it stands as an ice spike for 3 s; other tiers use the generic impact.
static func spear_impact(w: CombatWorld, b: MatBody, what: String) -> bool:
	if b.tier != 2:
		return false
	b.vel = Vector3.ZERO
	b.on_ground = true
	b.attack_id = 0
	b.gravity_scale = 0.0
	b.static_body = what != "actor"
	b.max_life = b.age + 3.0
	b.pos.y = maxf(b.pos.y, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3) + 0.2)
	w.emit("stick", {"body": b.id, "on": what})
	return true


# ------------------------------------------------------------------ thrust: Icicle Volley

static func _thrust() -> void:
	Moves.register("icicle_volley", {
		"element": E, "sub": SUB, "slot": "thrust", "name": "Icicle Volley",
		"desc": "Needles of ice in a tight line: 3, held 5, 7, then 12.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(10), "active": _s(6), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 5.0, "source": "moisture", "mat": "water", "frozen": true, "mass": 0.8, "speed": 32.0, "gravity": 0.1, "tag": "needle",
		"count": 3, "spread": 8.0, "damage": 3.5, "balance": 6.0, "hit_status": "chilled", "hit_status_t": 0.8,
		"tiers": {
			"t1": {"count": 5, "spread": 10.0, "cost_add": 2.0},
			"t2": {"count": 7, "spread": 12.0, "cost_add": 4.0},
			"t3": {"count": 12, "spread": 18.0, "cost_add": 7.0, "damage": 3.0},
		},
		"counter": {"cls": "frost", "power": [1.3, 1.3, 1.3, 1.3]}, "threat": {"cls": "ice"},
		"anim": "fire_jab", "fx": {"mat": "ice", "shape": "needles"},
		"ai": {"role": "poke", "range": [4.0, 16.0], "tags": ["volley", "chill"]},
	})


# ------------------------------------------------------------------ ground: Rime Path

static func _ground() -> void:
	Moves.register("rime_path", {
		"element": E, "sub": SUB, "slot": "ground", "name": "Rime Path",
		"desc": "An ice sheet races along the ground and leaves a slick ice floor. It freezes puddles (no more conduction), turns a water wave into an ice ridge, crusts lava and makes a walkable strip over the pool. Held: longer ice, spikes, then a standing glacier ridge.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(14), "active": _s(6), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "source": "moisture", "mat": "water", "mass": 3.0, "tag": "rime", "speed": 12.0, "budget": 12.0, "width": 1.5,
		"power": 10.0, "channel": "C", "damage": 8.0, "balance": 22.0, "knock": 3.0, "lift": 1.0, "kind": "frost",
		"hit_status": "chilled", "hit_status_t": 1.5, "ice_life": 3.0, "spikes": false,
		"tiers": {
			"t1": {"power": 14.0, "ice_life": 6.0, "mass": 4.0, "budget": 13.0, "damage": 10.0, "cost_add": 3.0},
			"t2": {"power": 20.0, "ice_life": 6.0, "mass": 5.0, "width": 1.8, "spikes": true, "damage": 14.0, "cost_add": 5.0},
			"t3": {"power": 30.0, "ice_life": 6.0, "mass": 8.0, "width": 3.0, "ridge": true, "damage": 18.0, "balance": 40.0, "cost_add": 8.0, "budget": 10.0},
		},
		"hook_execute": Callable(WaterIce, "rime_execute"),
		"counter": {"cls": "rime", "power": [10.0, 14.0, 20.0, 30.0]}, "threat": {"cls": "frost"},
		"anim": "mv_ground_slap", "anim_active": "water_whip", "fx": {"mat": "ice", "shape": "ground"},
		"ai": {"role": "zone", "range": [3.0, 12.0], "tags": ["slick", "freeze_puddle", "freeze_wave", "crust_lava"]},
	})


static func rime_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var b := VerbGroundLine.launch(w, a, inst)
	if b == null:
		return true
	WaterUtil.freeze_body(w, b)
	b.props["ice_life"] = float(Charge.param(inst, "ice_life", 3.0))
	b.props["spikes"] = bool(Charge.param(inst, "spikes", false))
	b.props["ridge"] = bool(Charge.param(inst, "ridge", false))
	b.props["last_zone"] = b.pos
	return true


## Body tick of rime waves: the ice floor it leaves behind (a walkable strip over the pool), contact with other
## waves (water -> ridge, lava -> crust) and, at T3, the ridge that stands where it ends.
static func rime_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.form != Sim.Form.WAVE or b.attack_id == 0:
		return false
	var last: Vector3 = b.props.get("last_zone", b.pos)
	if Vector2(b.pos.x - last.x, b.pos.z - last.z).length() >= 1.0:
		b.props["last_zone"] = b.pos
		var z := ice_zone(w, b.pos, 0.95 + b.wave_width * 0.2, b.attack_owner, float(b.props.get("ice_life", 3.0)), float(b.power))
		z.tier = b.tier
		if bool(b.props.get("spikes", false)):
			z.props["dps"] = 2.5
			z.props["actor_status"] = "slowed"
			z.props["status_t"] = 0.4
			z.props["ground_only"] = true
	WaterWater.wave_contacts(w, b)
	if not b.alive or b.form != Sim.Form.WAVE:
		return false
	if bool(b.props.get("ridge", false)) and b.wave_budget < 0.6:
		WaterRules.make_ridge(w, b, w.get_actor(b.attack_owner), 8.0)
		return true
	return false


## An ice_floor ZONE at p: slick for everyone but its owner, a walkable surface at pool level over the pool.
static func ice_zone(w: CombatWorld, p: Vector3, radius: float, owner_id: int, life: float, power: float = 10.0) -> MatBody:
	var z := WaterUtil.zone(w, "ice_floor", p, radius, owner_id, life, {"friction": 0.12, "surface": "ice", "walk_height": -p.y,
		"spare_owner": true, "height": 1.2, "rate": 0.25}, Sim.Mat.AIR, 0.0, power)
	z.sub = SUB
	return z


# ------------------------------------------------------------------ sweep: Hoarfrost Fan

static func _sweep() -> void:
	Moves.register("hoarfrost_fan", {
		"element": E, "sub": SUB, "slot": "sweep", "name": "Hoarfrost Fan",
		"desc": "A freezing mist fan: chills (-30% speed), freezes WET targets solid (rooted), freezes streams in the air and puddles, cools hot rock and makes vines brittle.",
		"module": "verbs", "verb": "cone",
		"startup": _s(10), "active": _s(8), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 5.0, "cls": "frost", "channel": "C", "range": 5.0, "angle": 45.0, "power": 6.0, "damage": 3.0, "balance": 10.0,
		"knock": 1.5, "lift": 0.3, "status": "chilled", "status_t": 2.0, "freeze_t": 0.8,
		"tiers": {
			"t1": {"power": 9.0, "range": 5.5, "cost_add": 2.0, "freeze_t": 1.0},
			"t2": {"power": 13.0, "range": 6.0, "damage": 5.0, "cost_add": 4.0, "freeze_t": 1.2, "status_t": 2.5},
			"t3": {"power": 18.0, "range": 6.5, "angle": 55.0, "damage": 7.0, "cost_add": 6.0, "freeze_t": 1.5, "status_t": 3.0},
		},
		"hook_execute": Callable(WaterIce, "hoarfrost_execute"),
		"counter": {"cls": "frost", "power": [6.0, 9.0, 13.0, 18.0]}, "threat": {"cls": "frost"},
		"anim": "water_freeze", "fx": {"mat": "ice", "shape": "fan"},
		"ai": {"role": "setup", "range": [1.5, 6.5], "tags": ["freeze_wet", "chill", "brittle", "cool"]},
	})


static func hoarfrost_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	VerbVolume.cone(w, a, inst)
	var rng_m := float(Charge.param(inst, "range", 5.0))
	var ang := float(Charge.param(inst, "angle", 45.0))
	var dir: Vector3 = inst.data.get("face", a.forward())
	for t in w.actors_in_cone(a, dir, rng_m, ang):
		if not t.hits_taken.has(inst.attack_id):
			continue
		if (t.last_result == "hit" or t.last_result == "knockdown") and t.wetness > Status.WET_AT:
			# The water on them freezes: rooted until it cracks.
			Status.apply(w, t, "frozen", float(Charge.param(inst, "freeze_t", 0.8)), 1.0, a.id)
			t.wetness = 0.2
			w.emit("transform", {"actor": t.id, "at": t.pos, "from": "wet", "to": "frozen", "why": "frost"})
	# Puddles in the fan freeze (they are barriers to the volume code, so the cone never meets them as threats).
	var cos_lim := cos(deg_to_rad(ang))
	for b in w.bodies:
		if b.alive and b.form == Sim.Form.PUDDLE and b.phase == Sim.Phase.LIQUID:
			var to := b.pos - a.chest()
			var flat := Vector3(to.x, 0, to.z)
			if flat.length() <= rng_m + b.radius and (flat.length() < 0.5 or flat.normalized().dot(dir) >= cos_lim):
				WaterUtil.freeze_body(w, b)
				w.emit("transform", {"body": b.id, "at": b.pos, "from": "water", "to": "ice", "why": "frozen"})
	return true


# ------------------------------------------------------------------ guard: Ice Wall / Flash Freeze

static func _guard() -> void:
	Moves.register("ice_wall", {
		"element": E, "sub": SUB, "slot": "guard", "name": "Ice Wall",
		"desc": "A 50 kg wall of ice from the air's moisture (70 kg beside water); held it thickens. It stops solids and bolts (E up to 1.5 x its power: an insulator), freezes water that hits it and melts under fire. A perfect guard (Flash Freeze) freezes water, steam and mist solid and frost-locks light stones to the wall. Storm bolts, sound and blasts shatter it.",
		"module": "kit_water", "verb": "barrier", "barrier": "wall", "mat": "water", "tag": "ice", "source": "moisture", "mass": 50.0, "hardness": 0.44,
		"half": Vector3(1.2, 0.85, 0.3), "dist": 1.35, "rise": _s(8), "cost": 8.0, "move_channel": 0.35,
		"tiers": {"t1": {"mass": 50.0}, "t2": {"mass": 65.0}, "t3": {"mass": 80.0}},
		"counter": {"cls": "wall_ice"}, "threat": {"cls": "wall_ice"},
		"anim": "water_freeze", "fx": {"mat": "ice", "shape": "plate"},
		"ai": {"role": "counter", "range": [0.0, 14.0], "tags": ["insulator", "flash_freeze", "block"]},
	})


## Starts the guard (the verbs raise the wall); beside the pool or a puddle it is 20 kg thicker (booked moisture).
static func wall_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_start(w, a, inst, it)
	var wall := w.get_body(int(inst.data.get("wall", -1)))
	if wall != null and wall.alive and WaterUtil.near_water(w, a.pos, 3.0):
		var e := wall.thermal_energy()
		w.mass_ledger.moisture_taken += 20.0
		wall.mass += 20.0
		w._set_energy(wall, e)
		w.emit("barrier_grow", {"actor": a.id, "body": wall.id, "mass": wall.mass, "tier": 0, "near_water": true})


## Body tick of ice walls and ridges: a melted wall (fire, lava) slumps into a puddle; Glacier Shove slides it.
static func wall_tick(w: CombatWorld, b: MatBody, dt: float) -> bool:
	if b.form != Sim.Form.WALL:
		return false
	if b.is_water() and b.phase == Sim.Phase.LIQUID and b.mass > 0.0:
		var owner := w.get_actor(b.last_actor)
		if owner != null and owner.wall_body == b.id:
			owner.wall_body = -1
		w.release_captured(b)
		b.static_body = false
		b.wall_rise = 1.0
		b.form = Sim.Form.PUDDLE
		b.tag = &""
		b.hardness = -1.0
		w._water_to_puddle(b)
		w.emit("wall_crumble", {"body": b.id, "melted": true})
		return true
	if b.props.has("slide"):
		_slide(w, b, dt)
	return false


static func _slide(w: CombatWorld, b: MatBody, dt: float) -> void:
	var s: Dictionary = b.props.slide
	var dir: Vector3 = s.dir
	var step := dir * float(s.speed) * dt
	var np := b.pos + step
	var up := Vector3(0, 0.6, 0)
	if w.arena.segment_hit(b.pos + up, np + up, 0.5) >= 0.0 or absf(w.arena.ground_height(np.x, np.z, b.pos.y + 0.3) - b.pos.y) > 0.3:
		b.props.erase("slide")
		return
	b.pos = np
	s["left"] = float(s.left) - step.length()
	var owner := w.get_actor(int(s.owner))
	var hit: Dictionary = s.hit
	for t in w.actors:
		if t == owner or hit.has(t.id) or t.health <= 0.0 or (owner != null and t.team == owner.team):
			continue
		if w.point_in_wall(t.pos + Vector3(0, 0.9, 0), b, 0.5):
			hit[t.id] = true
			var res := w.hit_actor(t, {"attacker": int(s.owner), "attack_id": int(s.attack_id), "damage": float(s.get("damage", 10.0)),
				"balance": float(s.get("balance", 30.0)), "knock": dir * 6.0 + Vector3(0, 2.0, 0), "kind": "ice" if b.is_water() else "plant",
				"from": b.pos - dir, "power": float(s.get("power", 22.0)), "tier": b.tier, "mat": "ice" if b.is_water() else "plant"})
			if (res == "hit" or res == "knockdown") and s.has("status"):
				Status.apply(w, t, String(s.status), float(s.get("status_t", 0.8)), 1.0, int(s.owner))
	if float(s.left) <= 0.0:
		b.props.erase("slide")


# ------------------------------------------------------------------ push / sink: Glacier Shove, Frost Floor

static func _push_sink() -> void:
	Moves.register("glacier_shove", {
		"element": E, "sub": SUB, "slot": "push", "name": "Glacier Shove",
		"desc": "Guard flick up: the ice wall slides forward 6 m on its own sheet, knocking back whoever it meets.",
		"module": "verbs", "verb": "none",
		"startup": _s(10), "active": _s(18), "recovery": _s(18), "cancel": 0.6, "cost": 4.0,
		"hook_execute": Callable(WaterIce, "shove_execute"),
		"counter": {"cls": "wall_ice", "power": 22.0}, "threat": {"cls": "ice"},
		"anim": "mv_front_kick", "fx": {"mat": "ice", "shape": "plate", "cast": "trail"},
		"ai": {"role": "poke", "range": [1.5, 8.0], "tags": ["push", "knockback"]},
	})
	Moves.register("frost_floor", {
		"element": E, "sub": SUB, "slot": "sink", "name": "Frost Floor",
		"desc": "Guard flick down: a 3 m ice floor around you for 5 s. Enemies slide on it, your puddles freeze (lightning-safe).",
		"module": "verbs", "verb": "zone",
		"startup": _s(6), "active": _s(4), "recovery": _s(14), "cancel": 0.6, "cost": 5.0,
		"tag": "ice_floor", "at": "self", "radius": 3.0, "life": 5.0, "power": 12.0, "friction": 0.12, "surface": "ice", "walk_height": 0.0,
		"height": 1.2, "rate": 0.25,
		"anim": "mv_ground_slap", "fx": {"mat": "ice", "shape": "down", "cast": "ring"},
		"threat": {"cls": "frost"},
		"ai": {"role": "setup", "range": [0.0, 3.0], "tags": ["slick", "freeze_puddle"]},
	})


static func shove_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var wall := w.get_body(int(inst.data.get("wall", a.wall_body)))
	if wall == null or not wall.alive or wall.form != Sim.Form.WALL or (wall.tag != &"ice" and wall.tag != &"ridge"):
		w.emit("insufficient", {"actor": a.id, "what": "wall", "move": inst.id})
		return true
	var dir := WaterUtil.aim_flat(w, a, inst)
	wall.props["slide"] = {"dir": dir, "left": 6.0, "speed": 10.0, "owner": a.id, "hit": {}, "attack_id": w.new_attack_id()}
	wall.props["standing"] = 4.0
	wall.age = 0.0
	wall.tier = inst.tier()
	Verbs.fx(w, a, inst, "cast", {"body": wall.id, "dir": dir, "length": 6.0})
	return true


# ------------------------------------------------------------------ tech: Freeze-Draw / Shatter

static func _tech() -> void:
	Moves.register("freeze_draw", {
		"element": E, "sub": SUB, "slot": "tech", "name": "Freeze-Draw",
		"desc": "Hold: draw water from the air as a growing ice block (up to 12 kg) or seize incoming ice or water (it freezes in your hand); drag to aim, release to throw. Attack while holding: Shatter, the block bursts into six shards.",
		"module": "kit_water", "verb": "grip",
		"startup": _s(12), "active": _s(4), "recovery": _s(18), "cancel": 0.5, "cost": 6.0,
		"ccls": "grip_ice", "reach": 7.5, "cone": 60.0, "base": 0.85, "rip_time": 0.28, "rip_source": "moisture", "rip_mat": "water", "rip_mass": 4.0,
		"speed": 18.0, "damage": 12.0, "balance": 28.0, "gravity": 0.8, "shape": "split", "pieces": 6, "spread": 40.0, "shape_cost": 3.0,
		"mode_label": "FREEZE", "max_draw": 12.0, "grow_rate": 4.0, "upkeep": 0.0,
		"counter": {"cls": "grip_ice"}, "threat": {"cls": "ice"},
		"anim": "water_draw", "anim_hold": "water_hold", "anim_active": "water_whip", "fx": {"mat": "ice", "shape": ""},
		"ai": {"role": "counter", "range": [0.0, 7.5], "tags": ["reclaim", "freeze", "shatter"]},
	})


## The grip verb plus the ice: water in the hand freezes into a block that grows from ambient moisture up to 12 kg.
static func freeze_draw_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action != inst or inst.phase != ActionInst.P.CHANNEL:
		return
	var b := w.held(a)
	if b == null or not b.is_water():
		return
	if b.phase == Sim.Phase.LIQUID:
		WaterUtil.freeze_body(w, b)
		b.form = Sim.Form.SHARD
		b.update_radius()
		w.emit("transform", {"body": b.id, "at": b.pos, "from": "water", "to": "ice", "why": "frozen"})
	var maxm := float(Charge.param(inst, "max_draw", 12.0))
	if b.phase == Sim.Phase.FROZEN and b.mass < maxm and not inst.data.get("shaped", false):
		var dm := minf(float(Charge.param(inst, "grow_rate", 4.0)) * Sim.DT, maxm - b.mass)
		w.mass_ledger.moisture_taken += dm
		var e_before := b.thermal_energy()
		b.mass += dm                      # the new kg arrives as ice at the block's own state: the dump is the energy it gains
		w.ledger.freeze_dump += b.thermal_energy() - e_before
		b.update_radius()


# ------------------------------------------------------------------ evades: Ice Glide, Skate

static func _mobility() -> void:
	Moves.register("ice_glide", {
		"element": E, "sub": SUB, "slot": "evade", "name": "Ice Glide",
		"desc": "A 5 m slide on a strip of ice that forms under you and lingers behind.",
		"module": "kit_water", "verb": "dash",
		"startup": 0.0, "active": _s(14), "recovery": _s(6), "cancel": 0.5, "cost": 4.0, "distance": 5.0, "iframes": _s(9),
		"anim": "evade", "fx": {"mat": "ice", "shape": ""}, "threat": {"cls": "frost"},
		"ai": {"role": "mobility", "range": [0.0, 5.0], "tags": ["slide", "slick_trail"]},
	})
	Moves.register("skate", {
		"element": E, "sub": SUB, "slot": "evade_hold", "name": "Skate",
		"desc": "Hold evade: ice forms under your feet; run at 7 m/s with almost no friction, across the pool too (frozen water won't conduct).",
		"module": "kit_water", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(6), "cost": 0.0, "kind": "skate", "speed_mult": 1.27, "status": "skating", "upkeep": 8.0,
		"anim": "run", "fx": {"mat": "ice", "shape": ""}, "threat": {"cls": "frost"},
		"ai": {"role": "mobility", "range": [0.0, 14.0], "tags": ["skate", "cross_pool"]},
	})


static func glide_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	WaterUtil.evade_start(w, a, inst, it, {"dist": float(inst.def.distance), "iframes": float(inst.def.iframes), "cost": float(inst.def.cost),
		"active": float(inst.def.active)})
	var dir: Vector3 = inst.data.dir
	for k in 3:
		var z := ice_zone(w, WaterUtil.ground_at(w, a.pos + dir * (1.4 * float(k + 1) - 0.4)), 1.2, a.id, 1.6, 8.0)
		z.tier = 0
	Verbs.fx(w, a, inst, "trail", {"dir": dir, "length": float(inst.def.distance)})


static func skate_after(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	var nxt := Verbs.after_startup(w, a, inst, it)
	if nxt == ActionInst.P.CHANNEL and a.action == inst and not inst.data.get("zone_id"):
		var z := WaterUtil.zone(w, "ice_floor", a.pos, 1.5, a.id, -1.0, {"friction": 0.3, "surface": "ice", "walk_height": 0.0, "spare_owner": true,
			"height": 1.4, "attach": a.id, "attach_off": Vector3.ZERO, "skate": true, "rate": 0.25}, Sim.Mat.AIR, 0.0, 10.0)
		z.sub = SUB
		inst.data["zone_id"] = z.id
	return nxt


static func _skate_end(w: CombatWorld, inst: ActionInst) -> void:
	var z := w.get_body(int(inst.data.get("zone_id", -1)))
	inst.data.erase("zone_id")
	if z != null and z.alive:
		w.close_zone(z, "skate_end")


static func skate_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		_skate_end(w, inst)
	Verbs.on_phase(w, a, inst, p)


static func skate_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	_skate_end(w, inst)
	Verbs.on_interrupt(w, a, inst, reason)
