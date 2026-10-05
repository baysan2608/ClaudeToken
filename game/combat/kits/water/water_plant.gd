class_name WaterPlant
extends RefCounted
## Water / Plant (sub 3, docs/MOVESET.md §7.8, P2): growth, binding, catching. Vines grow from water (1 kg water ->
## 1 kg vine, booked mass_ledger.water_to_plant; ambient moisture is booked in moisture_taken as well, so
## water_mass() and plant_mass() stay exact), burn x3 (WaterRules.o_burn -> CombatWorld.burn_plant, ledger burned),
## are cut by edges and turn brittle when frozen (WaterRules.o_brittle).
##
## strike Bramble Lash -> Briar Thorns -> Snare Slam -> Briar Storm     thrust Burr Shot
## ground Root Snare -> ... -> Strangler Grove                          sweep  Thicket Fan
## guard  Living Lattice (+ Catch & Sling, the perfect)                 push   Lattice Roll
## sink   Deep Roots (anchor)                                           tech   Vinegrip (+ Wrap, Hook)
## evade  Vine Swing                                                    hold   Canopy

const E := 1
const SUB := 3


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
	for pair in [["strike", "bramble_lash"], ["thrust", "burr_shot"], ["ground", "root_snare"], ["sweep", "thicket_fan"],
			["guard", "living_lattice"], ["push", "lattice_roll"], ["sink", "deep_roots"], ["tech", "vinegrip"],
			["evade", "vine_swing"], ["evade_hold", "canopy"]]:
		Moves.bind(E, SUB, pair[0], pair[1])
	KitWater.handle("living_lattice", {"start": Callable(WaterPlant, "lattice_start"), "tick": Callable(WaterPlant, "lattice_tick")})
	KitWater.handle("deep_roots", {"tick": Callable(WaterPlant, "roots_stance_tick")})
	KitWater.handle("vinegrip", {"tick": Callable(WaterPlant, "vinegrip_tick")})
	KitWater.handle("vine_swing", {"start": Callable(WaterPlant, "swing_start"), "after": Callable(WaterWater, "after_active"),
		"tick": Callable(WaterWater, "evade_tick"), "phase": Callable(WaterPlant, "swing_phase")})
	KitWater.handle("canopy", {"tick": Callable(WaterPlant, "canopy_tick")})
	CombatWorld.register_body_tick(&"vine", Callable(WaterPlant, "vine_tick"))
	CombatWorld.register_body_tick(&"roots", Callable(WaterPlant, "roots_tick"))
	Interactions.register_tag_class(&"briar", &"vine", &"briar")


# ------------------------------------------------------------------ helpers

## Water for a vine move: `kg` from the waterskin / pool / puddle (WaterUtil.take).
static func _water(w: CombatWorld, a: ActorState, inst: ActionInst, kg: float, minimum: float = 0.4) -> float:
	var got := WaterUtil.take(w, a, kg)
	if got < minimum:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return 0.0
	return got


## Booked conversion water -> vine: `kg` (already taken from a source) becomes a PLANT body at p living `life` seconds.
static func grow(w: CombatWorld, kg: float, p: Vector3, life: float = -1.0) -> MatBody:
	w.mass_ledger.water_to_plant += kg
	var b := w.spawn_body(Sim.Mat.PLANT, Sim.Form.CHUNK, kg, p, "vine")
	b.max_life = life
	b.on_ground = true
	return b


## A PLANT ZONE (briar, snare): the vine mass is booked through water_to_plant.
static func plant_zone(w: CombatWorld, tag: String, p: Vector3, radius: float, owner_id: int, life: float, kg: float, props: Dictionary,
		power: float = 0.0) -> MatBody:
	w.mass_ledger.water_to_plant += kg
	var z := WaterUtil.zone(w, tag, p, radius, owner_id, life, props, Sim.Mat.PLANT, kg, power)
	z.sub = SUB
	return z


# ------------------------------------------------------------------ strike: Bramble Lash

static func _strike() -> void:
	Moves.register("bramble_lash", {
		"element": E, "sub": SUB, "slot": "strike", "name": "Bramble Lash",
		"desc": "A vine whip, 5 m: yanks light targets 2 m toward you. Held: longer thorns, then a grab-and-slam, then Briar Storm, a 360 degree whip.",
		"module": "verbs", "verb": "cone",
		"startup": _s(10), "active": _s(8), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 5.0, "cls": "vine", "channel": "P", "range": 5.0, "angle": 30.0, "power": 6.0, "damage": 7.0, "balance": 14.0, "knock": 0.5, "lift": 0.2,
		"vine_kg": 1.0, "yank": 7.0, "slam": 0.0, "around": false,
		"tiers": {
			"t1": {"power": 10.0, "range": 6.0, "damage": 10.0, "cost_add": 2.0, "yank": 7.5},
			"t2": {"power": 16.0, "range": 6.0, "damage": 12.0, "slam": 14.0, "cost_add": 4.0, "yank": 8.0, "vine_kg": 1.5},
			"t3": {"power": 24.0, "range": 5.0, "angle": 180.0, "damage": 16.0, "slam": 12.0, "cost_add": 7.0, "around": true, "vine_kg": 2.5, "yank": 6.0},
		},
		"hook_execute": Callable(WaterPlant, "lash_execute"),
		"counter": {"cls": "vine", "power": [6.0, 10.0, 16.0, 24.0]}, "threat": {"cls": "vine"},
		"anim": "water_whip", "fx": {"mat": "plant", "shape": ""},
		"ai": {"role": "poke", "range": [1.5, 6.0], "tags": ["yank", "grab", "burns"]},
	})


static func lash_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var kg := _water(w, a, inst, float(Charge.param(inst, "vine_kg", 1.0)))
	if kg <= 0.0:
		return true
	var dir := WaterUtil.aim_flat(w, a, inst)
	var around := bool(Charge.param(inst, "around", false))
	var rng_m := float(Charge.param(inst, "range", 5.0))
	var ang := float(Charge.param(inst, "angle", 30.0))
	var yank := float(Charge.param(inst, "yank", 7.0))
	var slam := float(Charge.param(inst, "slam", 0.0))
	# the whip itself: a vine volume (cells wall_vine / flame x vine in WaterRules)
	var v := VerbVolume.cone(w, a, inst)
	if around:
		FxEvents.fx_for(w, a, inst, "ring", "plant", {"radius": rng_m, "power": v.power, "pos": a.pos})
	var hit_any := false
	for t in w.actors:
		if t == a or t.team == a.team or not t.hits_taken.has(inst.attack_id):
			continue
		if not (t.last_result == "hit" or t.last_result == "knockdown"):
			continue
		hit_any = true
		var to := a.pos - t.pos
		to.y = 0.0
		var d := to.length()
		if not Status.immune(t, "pull") and d > 1.4:
			t.vel += to.normalized() * minf(yank, d * 4.0)       # about 2 m toward you
			Status.apply(w, t, "hooked", 0.5, 1.0, a.id)
		if slam > 0.0:
			t.balance = maxf(0.0, t.balance - slam)
			t.vel.y = maxf(t.vel.y, 3.0)
			t.grounded = false
			w.emit("slam", {"actor": t.id, "by": a.id})
	if around:
		for t in w.actors:
			if t == a or t.team == a.team or t.health <= 0.0 or t.hits_taken.has(inst.attack_id):
				continue
			if Vector2(t.pos.x - a.pos.x, t.pos.z - a.pos.z).length() > rng_m + Sim.ACTOR_RADIUS:
				continue
			var rd := (t.pos - a.pos)
			rd.y = 0.0
			w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "damage", 16.0)), "balance": 24.0,
				"knock": rd.normalized() * 4.0 + Vector3(0, 1.5, 0), "kind": "plant", "from": a.chest(), "agent": v, "power": v.power, "tier": 3, "mat": "plant"})
	# the vine that did the work lies where the whip ended and withers (booked)
	var end := WaterUtil.ground_at(w, a.pos + dir * rng_m * 0.7)
	grow(w, kg, end + Vector3(0, 0.1, 0), 2.5)
	return true


# ------------------------------------------------------------------ thrust: Burr Shot

static func _thrust() -> void:
	Moves.register("burr_shot", {
		"element": E, "sub": SUB, "slot": "thrust", "name": "Burr Shot",
		"desc": "Three burrs at 25 m/s. Where they land they sprout snares (root 1 s, r 1 m). Held: 5, 7, then a small grove.",
		"module": "verbs", "verb": "none",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "burr_kg": 0.5, "count": 3, "spread": 10.0, "speed": 25.0, "root_t": 1.0, "radius": 1.0, "life": 4.0,
		"tiers": {
			"t1": {"count": 5, "spread": 14.0, "cost_add": 2.0},
			"t2": {"count": 7, "spread": 18.0, "cost_add": 3.0, "root_t": 1.2, "radius": 1.2},
			"t3": {"count": 9, "spread": 24.0, "cost_add": 5.0, "root_t": 1.5, "radius": 1.6, "life": 6.0},
		},
		"hook_execute": Callable(WaterPlant, "burr_execute"), "hook_impact": Callable(WaterPlant, "seed_impact"),
		"counter": {"cls": "vine", "power": [3.0, 3.0, 3.0, 3.0]}, "threat": {"cls": "vine"},
		"anim": "fire_jab", "fx": {"mat": "plant", "shape": "seed"},
		"ai": {"role": "setup", "range": [3.0, 14.0], "tags": ["root", "seed", "zone"]},
	})


static func burr_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var count := int(Charge.param(inst, "count", 3))
	var each := float(Charge.param(inst, "burr_kg", 0.5))
	var got := _water(w, a, inst, each * float(count), each)
	if got <= 0.0:
		return true
	var n := maxi(1, int(floor(got / each + 1e-6)))
	var dir := WaterUtil.aim_flat(w, a, inst)
	var spread := deg_to_rad(float(Charge.param(inst, "spread", 10.0)))
	var spd := float(Charge.param(inst, "speed", 25.0))
	var target := Verbs.target_point(w, a, inst, 14.0)
	var left := got - each * float(n)
	if left > 0.0:
		WaterUtil.give_back(w, a, left, a.pos)
	for k in n:
		var ang := 0.0 if n == 1 else lerpf(-spread * 0.5, spread * 0.5, float(k) / float(n - 1))
		var b := grow(w, each, a.hand_point() + dir.rotated(Vector3.UP, ang) * 0.4, -1.0)
		b.tag = &"seed"
		b.on_ground = false
		b.gravity_scale = 0.3
		b.max_life = 6.0
		var aim_to := a.chest() + (target - a.chest()).rotated(Vector3.UP, ang)
		b.vel = Verbs.launch_vel(b.pos, aim_to, spd, 0.3)
		Verbs.arm(w, a, inst, b, 4.0, 8.0)
		b.props["on_impact"] = "sprout"
		b.props["root_t"] = float(Charge.param(inst, "root_t", 1.0))
		b.props["impact_radius"] = float(Charge.param(inst, "radius", 1.0))
		b.props["impact_life"] = float(Charge.param(inst, "life", 4.0))
		w.emit("launch", {"actor": a.id, "body": b.id, "speed": spd, "kind": "seed", "tier": inst.tier()})
	Verbs.fx(w, a, inst, "release", {"pos": a.hand_point(), "dir": dir, "power": float(n)})
	return true


## A burr lands (or hits a fighter): it sprouts into a snare zone that roots whoever stands in it.
static func seed_impact(w: CombatWorld, b: MatBody, _what: String) -> bool:
	if b.tag != &"seed":
		return false
	var owner := w.get_actor(b.attack_owner)
	var r := float(b.props.get("impact_radius", 1.0))
	b.form = Sim.Form.ZONE
	b.tag = &"snare"
	b.zone_radius = r
	b.radius = r
	b.attack_id = 0
	b.vel = Vector3.ZERO
	b.gravity_scale = 0.0
	b.owner = owner.id if owner != null else -1
	b.max_life = b.age + float(b.props.get("impact_life", 4.0))
	b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3)
	b.props["actor_status"] = "rooted"
	b.props["status_t"] = float(b.props.get("root_t", 1.0))
	b.props["spare_owner"] = true
	b.props["height"] = 1.6
	b.props["rate"] = 0.2
	b.props["ccls"] = "briar"
	FxEvents.zone(w, b, "open")
	return true


# ------------------------------------------------------------------ ground: Root Snare

static func _ground() -> void:
	Moves.register("root_snare", {
		"element": E, "sub": SUB, "slot": "ground", "name": "Root Snare",
		"desc": "Roots race underground at 10 m/s (the ground cracks: a telegraph) and erupt under the target: rooted 1.2 s. Held: longer root, wider, then a Strangler Grove (r 3 m). Flying fighters pass over.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(18), "active": _s(6), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "source": "none", "mat": "plant", "tag": "roots", "mass": 2.0, "speed": 10.0, "budget": 10.0, "width": 1.4, "power": 12.0,
		"damage": 8.0, "balance": 20.0, "knock": 0.5, "lift": 1.5, "kind": "plant", "hit_status": "rooted", "hit_status_t": 1.2, "steer": 25.0,
		"tiers": {
			"t1": {"power": 16.0, "mass": 2.5, "hit_status_t": 1.5, "budget": 11.0, "cost_add": 3.0},
			"t2": {"power": 22.0, "mass": 3.0, "hit_status_t": 1.8, "width": 1.8, "budget": 12.0, "cost_add": 5.0},
			"t3": {"power": 30.0, "mass": 4.0, "hit_status_t": 1.8, "width": 2.2, "grove": 3.0, "budget": 12.0, "cost_add": 8.0},
		},
		"hook_execute": Callable(WaterPlant, "roots_execute"),
		"counter": {"cls": "wave_vine", "power": [12.0, 16.0, 22.0, 30.0]}, "threat": {"cls": "vine"},
		"anim": "mv_ground_slap", "anim_active": "water_whip", "fx": {"mat": "plant", "shape": "ground", "release": "erupt"},
		"ai": {"role": "zone", "range": [3.0, 11.0], "tags": ["root", "telegraph", "ground"]},
	})


static func roots_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var want := float(Charge.param(inst, "mass", 2.0))
	var got := _water(w, a, inst, want, 1.0)
	if got <= 0.0:
		return true
	w.mass_ledger.water_to_plant += got        # the booked water -> vine conversion of the root wave
	var b := VerbGroundLine.launch(w, a, inst, {"source": "none", "mass": got, "mat": "plant"})
	if b == null:
		w.mass_ledger.water_to_plant -= got
		WaterUtil.give_back(w, a, got, a.pos)
		return true
	b.props["grove"] = float(Charge.param(inst, "grove", 0.0))
	b.props["root_t"] = float(Charge.param(inst, "hit_status_t", 1.2))
	return true


## Root waves: the ground cracks ahead of them (telegraph events) and a grove stands where a T3 root ends.
static func roots_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.form != Sim.Form.WAVE:
		return false
	if w.tick % 6 == 0:
		FxEvents.fx(w, "trail", "plant", {"actor": b.attack_owner, "pos": b.pos, "dir": b.wave_dir, "length": 2.5, "shape": "ground",
			"move": "root_snare", "tier": b.tier, "body": b.id})
	var grove := float(b.props.get("grove", 0.0))
	if grove > 0.0 and b.wave_budget < 0.5 and not b.props.get("grove_made", false):
		b.props["grove_made"] = true
		var kg := minf(1.0, b.mass * 0.4)
		b.mass -= kg
		# the grove's vine is part of the root wave's own booked mass: it moves, nothing new is grown
		var z := WaterUtil.zone(w, "briar", WaterUtil.ground_at(w, b.pos), grove, b.attack_owner, 4.0, {"actor_status": "rooted",
			"status_t": float(b.props.get("root_t", 1.5)), "spare_owner": true, "height": 2.0, "rate": 0.2, "dps": 3.0, "ground_only": true},
			Sim.Mat.PLANT, kg, 12.0)
		z.sub = SUB
		z.tier = b.tier
		w.emit("erupt", {"body": z.id, "at": z.pos})
	return false


# ------------------------------------------------------------------ sweep: Thicket Fan

static func _sweep() -> void:
	Moves.register("thicket_fan", {
		"element": E, "sub": SUB, "slot": "sweep", "name": "Thicket Fan",
		"desc": "A thorn thicket 120 degrees wide, 4 m deep, standing 4 s: slows by 40% and catches small projectiles.",
		"module": "verbs", "verb": "none",
		"startup": _s(12), "active": _s(8), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "thicket_kg": 2.0, "radius": 2.4, "life": 4.0, "power": 8.0, "dps": 1.5,
		"tiers": {
			"t1": {"radius": 2.8, "life": 5.0, "cost_add": 2.0, "power": 10.0},
			"t2": {"radius": 3.2, "life": 6.0, "cost_add": 4.0, "power": 13.0, "dps": 2.5},
			"t3": {"radius": 3.8, "life": 8.0, "cost_add": 6.0, "power": 17.0, "dps": 3.5, "thicket_kg": 3.0},
		},
		"hook_execute": Callable(WaterPlant, "thicket_execute"),
		"counter": {"cls": "briar", "power": [8.0, 10.0, 13.0, 17.0]}, "threat": {"cls": "vine"},
		"anim": "water_whip", "fx": {"mat": "plant", "shape": "fan", "cast": "ring"},
		"ai": {"role": "zone", "range": [1.5, 6.0], "tags": ["slow", "catch", "briar"]},
	})


static func thicket_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var kg := _water(w, a, inst, float(Charge.param(inst, "thicket_kg", 2.0)), 0.8)
	if kg <= 0.0:
		return true
	var dir := WaterUtil.aim_flat(w, a, inst)
	var r := float(Charge.param(inst, "radius", 2.4))
	var p := WaterUtil.ground_at(w, a.pos + dir * (r * 0.9 + 0.4))
	var z := plant_zone(w, "briar", p, r, a.id, float(Charge.param(inst, "life", 4.0)), kg, {"actor_status": "slowed", "status_t": 0.4,
		"status_mag": 1.0, "dps": float(Charge.param(inst, "dps", 1.5)), "ground_only": true, "spare_owner": true, "height": 1.8, "rate": 0.15},
		float(Charge.param(inst, "power", 8.0)))
	z.tier = inst.tier()
	Verbs.fx(w, a, inst, "ring", {"pos": p, "radius": r, "body": z.id, "dir": dir})
	return true


# ------------------------------------------------------------------ guard: Living Lattice / Catch & Sling

static func _guard() -> void:
	Moves.register("living_lattice", {
		"element": E, "sub": SUB, "slot": "guard", "name": "Living Lattice",
		"desc": "A 40 kg wall of living vine (CP 16): captures solids, drinks water and grows, burns easily (x3), brittle when frozen. A perfect guard (Catch & Sling) catches the projectile and slings it back.",
		"module": "kit_water", "verb": "barrier", "barrier": "wall", "mat": "plant", "tag": "vine", "source": "moisture", "mass": 40.0, "hardness": 0.4,
		"half": Vector3(1.2, 0.8, 0.3), "dist": 1.35, "rise": _s(8), "cost": 7.0, "move_channel": 0.35,
		"tiers": {"t1": {"mass": 40.0}, "t2": {"mass": 50.0}, "t3": {"mass": 60.0}},
		"counter": {"cls": "wall_vine"}, "threat": {"cls": "wall_vine"},
		"anim": "water_shield", "fx": {"mat": "plant", "shape": "plate"},
		"ai": {"role": "counter", "range": [0.0, 14.0], "tags": ["catch", "sling", "grows"]},
	})


## The wall's mass is vine: moisture -> water -> vine, booked (moisture_taken by the barrier verb, water_to_plant here).
static func _sync_booking(w: CombatWorld, inst: ActionInst) -> void:
	var wall := w.get_body(int(inst.data.get("wall", -1)))
	if wall == null or not wall.alive:
		return
	# Growth since the last look is booked (the barrier verb booked it as moisture); burning and damage only lower the mark.
	var seen := float(wall.props.get("plant_seen", 0.0))
	if wall.mass > seen + 1e-9:
		w.mass_ledger.water_to_plant += wall.mass - seen
	wall.props["plant_seen"] = wall.mass


static func lattice_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_start(w, a, inst, it)
	_sync_booking(w, inst)


static func lattice_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	_sync_booking(w, inst)


# ------------------------------------------------------------------ push / sink: Lattice Roll, Deep Roots

static func _push_sink() -> void:
	Moves.register("lattice_roll", {
		"element": E, "sub": SUB, "slot": "push", "name": "Lattice Roll",
		"desc": "Guard flick up: the lattice rolls forward as a tumble-ball, entangling whoever it meets (0.8 s).",
		"module": "verbs", "verb": "none",
		"startup": _s(10), "active": _s(18), "recovery": _s(16), "cancel": 0.6, "cost": 4.0,
		"hook_execute": Callable(WaterPlant, "roll_execute"),
		"counter": {"cls": "wall_vine", "power": 10.0}, "threat": {"cls": "vine"},
		"anim": "mv_push_two_hand", "fx": {"mat": "plant", "shape": "plate", "cast": "trail"},
		"ai": {"role": "poke", "range": [1.5, 8.0], "tags": ["entangle", "push"]},
	})
	Moves.register("deep_roots", {
		"element": E, "sub": SUB, "slot": "sink", "name": "Deep Roots",
		"desc": "Guard flick down: root yourself (no knockback, pull or lift; anchor CP 35) and drink the puddles under you into the waterskin. Holds as long as you hold guard.",
		"module": "kit_water", "verb": "stance",
		"startup": _s(6), "active": 0.0, "recovery": _s(10), "cancel": 0.6, "cost": 0.0, "upkeep": 5.0, "stance": "roots", "anchored": true, "anchor_cp": 35.0,
		"armor": 0.2, "speed_mult": 0.3,
		"counter": {"cls": "anchor", "power": 35.0}, "threat": {"cls": "vine"},
		"anim": "mv_stomp", "fx": {"mat": "plant", "shape": "", "cast": "aura"},
		"ai": {"role": "counter", "range": [0.0, 0.0], "tags": ["anchor", "drink", "anti_pull"]},
	})


static func roll_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var wall := w.get_body(int(inst.data.get("wall", a.wall_body)))
	if wall == null or not wall.alive or wall.form != Sim.Form.WALL or wall.tag != &"vine":
		w.emit("insufficient", {"actor": a.id, "what": "wall", "move": inst.id})
		return true
	var dir := WaterUtil.aim_flat(w, a, inst)
	wall.props["slide"] = {"dir": dir, "left": 7.0, "speed": 8.0, "owner": a.id, "hit": {}, "attack_id": w.new_attack_id(), "status": "entangled",
		"status_t": 0.8, "damage": 6.0, "balance": 18.0, "power": 10.0}
	wall.props["standing"] = 4.0
	wall.age = 0.0
	wall.tier = inst.tier()
	Verbs.fx(w, a, inst, "cast", {"body": wall.id, "dir": dir, "length": 7.0})
	return true


## Vine walls: Lattice Roll slides them; wet vines (props.wet) conduct weakly; frozen vines are brittle.
static func vine_tick(w: CombatWorld, b: MatBody, dt: float) -> bool:
	if b.form != Sim.Form.WALL:
		return false
	if b.props.has("slide"):
		WaterIce._slide(w, b, dt)
	if b.props.get("brittle", false) and b.wall_damage < 0.9:
		b.wall_damage = maxf(b.wall_damage, 0.6)      # any hit crumbles it
	return false


## Deep Roots: puddles under the fighter are drunk into the waterskin (cap 6 kg).
static func roots_stance_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action != inst or inst.phase != ActionInst.P.CHANNEL or w.tick % 6 != 0:
		return
	var pd := w.puddle_at(a.pos)
	if pd != null and pd.phase == Sim.Phase.LIQUID and a.water_carried < 6.0 and pd.mass > 0.05:
		var take := minf(minf(0.8, pd.mass), 6.0 - a.water_carried)
		w.ledger.removed += pd.thermal_energy() * take / maxf(pd.mass, 1e-9)
		pd.mass -= take
		a.water_carried += take
		if pd.mass <= 0.05:
			var rest := pd.mass
			pd.mass = 0.0
			a.water_carried = minf(6.0, a.water_carried + rest)
			w.ledger.removed += pd.thermal_energy()
			w.remove_body(pd, "drunk")
		else:
			pd.update_radius_puddle()


# ------------------------------------------------------------------ tech: Vinegrip / Wrap / Hook

static func _tech() -> void:
	Moves.register("vinegrip", {
		"element": E, "sub": SUB, "slot": "tech", "name": "Vinegrip",
		"desc": "Hold: lash a vine to a body of up to 40 kg at 9 m (half the usual range falloff: it wins long contests); drag to aim, release to throw. Attack while holding: Wrap, the body roots whoever it hits. On a fighter: Hook, yank 3 m toward you (-20 balance).",
		"module": "kit_water", "verb": "grip",
		"startup": _s(8), "active": _s(4), "recovery": _s(18), "cancel": 0.5, "cost": 5.0,
		"ccls": "grip_vine", "reach": 9.0, "cone": 50.0, "base": 0.95, "grip_mult": 1.15, "max_mass": 40.0, "rip_source": "none", "speed": 18.0,
		"damage": 10.0, "balance": 24.0, "gravity": 0.8, "shape": "retag", "shape_tag": "wrapped", "shape_cost": 3.0, "mode_label": "VINE",
		"hook_range": 9.0, "hook_yank": 7.5, "hook_balance": 20.0, "wrap_t": 1.2,
		"counter": {"cls": "grip_vine"}, "threat": {"cls": "vine"},
		"anim": "earth_hold", "fx": {"mat": "plant", "shape": ""},
		"ai": {"role": "counter", "range": [0.0, 9.0], "tags": ["reclaim", "hook", "wrap", "long_range"]},
	})


static func vinegrip_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	# Hook: nothing to grip but a fighter in the cone -> yank them in (once).
	if inst.phase == ActionInst.P.CHANNEL and w.held(a) == null and not inst.data.get("hooked", false) and inst.t >= 0.12:
		var dir := w.aim_dir(a, it)
		var reach := float(Charge.param(inst, "hook_range", 9.0))
		var body := w.find_body(a, dir, reach, float(Charge.param(inst, "cone", 50.0)), func(b: MatBody) -> bool:
			return b.controller != a.id and b.form != Sim.Form.WALL and b.form != Sim.Form.POOL and b.form != Sim.Form.ZONE \
				and Interactions.allows(b, &"grip_vine"))
		if body == null:
			for t in w.actors_in_cone(a, dir, reach, 20.0):
				if t.health > 0.0 and t.team != a.team and w.los(a.chest(), t.chest()):
					inst.data["hooked"] = true
					var to := a.pos - t.pos
					to.y = 0.0
					if not Status.immune(t, "pull") and to.length() > 1.5:
						t.vel += to.normalized() * float(Charge.param(inst, "hook_yank", 7.5))
						w._stagger(t, "light", 0.4, {})      # the yank slides them in (a stagger slide: v^2 / 18 = 3 m)
					t.balance = maxf(0.0, t.balance - float(Charge.param(inst, "hook_balance", 20.0)))
					t.balance_idle = 0.0
					Status.apply(w, t, "hooked", 0.8, 1.0, a.id)
					w.emit("hook", {"actor": a.id, "target": t.id})
					FxEvents.fx_for(w, a, inst, "beam", "plant", {"pos": a.hand_point(), "length": a.pos.distance_to(t.pos), "dir": dir})
					break
	Verbs.on_tick(w, a, inst, it)
	if a.action != inst:
		return
	var held := w.held(a)
	if held != null and inst.data.get("shaped", false) and not held.props.has("hit_status"):
		held.props["hit_status"] = "rooted"          # Wrap: the body roots whoever it hits
		held.props["hit_status_t"] = float(Charge.param(inst, "wrap_t", 1.2))


# ------------------------------------------------------------------ evades: Vine Swing, Canopy

static func _mobility() -> void:
	Moves.register("vine_swing", {
		"element": E, "sub": SUB, "slot": "evade", "name": "Vine Swing",
		"desc": "Swing on a vine to an anchor (wall, pillar, ledge) within 9 m in the direction you push; with none in reach, a sidestep.",
		"module": "kit_water", "verb": "dash",
		"startup": 0.0, "active": _s(18), "recovery": _s(8), "cancel": 0.5, "cost": 5.0, "distance": 3.0, "iframes": _s(10), "reach": 9.0,
		"anim": "evade", "fx": {"mat": "plant", "shape": ""}, "threat": {"cls": "vine"},
		"ai": {"role": "mobility", "range": [0.0, 9.0], "tags": ["swing", "anchor", "reach_ledge"]},
	})
	Moves.register("canopy", {
		"element": E, "sub": SUB, "slot": "evade_hold", "name": "Canopy",
		"desc": "Hold evade: hang from a canopy of vines and hover 1.5 s, passing over ground attacks.",
		"module": "kit_water", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(10), "cost": 0.0, "kind": "hover", "height": 1.4, "speed_mult": 0.6, "upkeep": 6.0, "hang": 1.5,
		"anim": "glide", "fx": {"mat": "plant", "shape": ""}, "threat": {"cls": "vine"},
		"ai": {"role": "mobility", "range": [0.0, 6.0], "tags": ["hover", "dodge_ground"]},
	})


## The nearest anchor in the direction of travel: the closest point of an arena solid (wall, pillar, ledge) within reach.
static func _anchor(w: CombatWorld, a: ActorState, dir: Vector3, reach: float) -> Dictionary:
	var best := {}
	var bd := reach
	for s in w.arena.solids:
		var mn: Vector3 = s.min
		var mx: Vector3 = s.max
		if String(s.name).ends_with("_wall") and String(s.name) != "cover_wall":
			continue      # the arena boundary is not an anchor
		var q := Vector3(clampf(a.pos.x, mn.x, mx.x), 0.0, clampf(a.pos.z, mn.z, mx.z))
		var to := q - a.pos
		to.y = 0.0
		var d := to.length()
		if d < 1.5 or d > bd or to.normalized().dot(dir) < 0.5:
			continue
		bd = d
		best = {"point": q, "dist": d, "top": mx.y}
	return best


static func swing_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var dir := it.move
	dir.y = 0.0
	dir = dir.normalized() if dir.length() > 0.2 else a.forward()
	var anchor := _anchor(w, a, dir, float(inst.def.get("reach", 9.0)))
	var dist := float(inst.def.distance)
	var steer := dir
	if not anchor.is_empty():
		var to: Vector3 = anchor.point - a.pos
		to.y = 0.0
		dist = maxf(1.5, to.length() - 1.4)
		steer = to.normalized()
		inst.data["swing_anchor"] = anchor.point
		a.vel.y = 5.5 if float(anchor.top) > 0.5 else 3.0
		a.grounded = false
	var intent := ActorIntent.new()
	intent.move = steer
	WaterUtil.evade_start(w, a, inst, intent, {"dist": dist, "iframes": float(inst.def.iframes), "cost": float(inst.def.cost), "active": float(inst.def.active)})
	Verbs.fx(w, a, inst, "beam", {"pos": a.hand_point(), "dir": steer, "length": dist})
	w.emit("swing", {"actor": a.id, "anchored": not anchor.is_empty(), "dist": dist})


static func swing_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		WaterUtil.evade_end(inst)
	Verbs.on_phase(w, a, inst, p)


## Canopy hangs for 1.5 s, then lets go.
static func canopy_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action == inst and inst.phase == ActionInst.P.CHANNEL and inst.t >= float(Charge.param(inst, "hang", 1.5)):
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
