class_name AirGust
extends RefCounted
## Air / Gust (sub 0, the legacy kit) - docs/MOVESET.md §7.13, §8.4 (Gust column), §5.4 (owner examples).
##
## Legacy moves keep their T0 / T1 behaviour exactly (palm gust, cyclone push, updraft + glide, air dash, Wind Guard):
##   * air_attack gains T2 Gale (8 m 50°, P 18) and T3 Hurricane Palm (10 m 60°, P 28): the lava rule - T0 / T1
##     can't touch a lava wave, T2 crusts and slows it, T3 stalls a 20 kg wave and sets it into rock (convective
##     cooling, booked as ambient); a 45 kg wave survives. ActAir runs the volume; the cells are in AirRules.
##   * air_tech is context-sensitive: a light body (<= 30 kg: stone, water, ice, sand, steam, mist, fireball, ember)
##     in the aim cone within 9 m -> Wind Grip (morphs into gust_grip: seize, drag-aim, fling; a gripped fireball
##     grows +20 % - fed); otherwise the legacy updraft / glide.
##   * the Wind Guard (legacy guard) keeps its cells; the Gust column adds the rest in AirRules (Return Wind = perfect).
## New slots: thrust Wind Crescent, ground Dust Devil Line, sweep Crosswind, push Wall of Wind, sink Downdraft,
## evade_hold Tailwind.

const E := 3
const SUB := 0
const GRIP_REACH := 9.0
const GRIP_CONE := 40.0           # half angle (deg) of the technique's context cone
const FIRE_FEED := 1.2            # a gripped fireball is fed +20 %


static func _s(frames: float) -> float:
	return frames / 60.0


static func register() -> void:
	_extend_legacy()
	_thrust()
	_ground()
	_sweep()
	_push_sink()
	_tech()
	_evade_hold()
	CombatWorld.register_body_tick(&"crescent", Callable(AirGust, "crescent_tick"))
	CombatWorld.register_tech_preview(E, SUB, Callable(AirGust, "tech_preview"))


# ================================================================ legacy extension

## Metadata and tier data on the legacy defs. Only keys the BASE def does not have are added: T0 / T1 numbers
## (range, cone, knock, damage, balance, cost, timings) stay exactly today's.
static func _extend_legacy() -> void:
	var d: Dictionary = Moves.DEFS.air_attack
	var add := {"name": "Palm Gust", "slot": "strike", "sub": SUB,
		"desc": "A palm of wind: turns light shots and disperses clouds. Hold: Cyclone Push (legacy), then Gale (8 m, 50 deg) and Hurricane Palm (10 m, 60 deg) - strong enough to crust and stall a lava wave. Pressure 7 / 11 / 18 / 28 against what it meets.",
		"tiers": {
			"t1": {"charge_drain": 0.0, "power": 11.0},
			"t2": {"range": 8.0, "cone": 50.0, "knock": 14.0, "damage": 9.0, "balance": 44.0, "power": 18.0, "cost_add": 6.0, "charge_drain": 8.0},
			"t3": {"range": 10.0, "cone": 60.0, "knock": 18.0, "damage": 14.0, "balance": 62.0, "power": 28.0, "cost_add": 12.0},
		},
		"counter": {"cls": "gust", "power": [7.0, 11.0, 18.0, 28.0]},
		"threat": {"cls": "gust", "power": [7.0, 11.0, 18.0, 28.0]},
		"fx": {"mat": "wind"},
		"ai": {"role": "poke", "range": [1.0, 10.0], "tags": ["cone", "deflect_light", "disperse", "vs_lava_t3", "knockback"]}}
	for k in add:
		if not Moves.BASE_DEFS.air_attack.has(k):
			d[k] = add[k]
	var t: Dictionary = Moves.DEFS.air_tech
	var tadd := {"name": "Updraft / Wind Grip", "slot": "tech", "sub": SUB,
		"desc": "Context: a light body (<= 30 kg, fireballs too) in the aim cone within 9 m -> Wind Grip (seize it, drag-aim, release to fling; a gripped fireball grows 20 %). Otherwise Updraft: lift 2 m, keep holding to glide.",
		"contexts": ["updraft", "glide", "gust_grip"],
		"counter": {"cls": "grip_wind"},
		"fx": {"mat": "wind"},
		"ai": {"role": "counter", "range": [0.0, 9.0], "tags": ["reclaim", "light_only", "mobility", "vs_fireball", "vs_stone"]}}
	for k in tadd:
		if not Moves.BASE_DEFS.air_tech.has(k):
			t[k] = tadd[k]
	var g: Dictionary = Moves.DEFS.air_dash
	var gadd := {"name": "Air Dash", "slot": "evade", "sub": SUB,
		"desc": "A 4.6 m burst of wind with i-frames (legacy). Hold EVADE: Tailwind run.",
		"fx": {"mat": "wind"}, "ai": {"role": "mobility", "range": [0.0, 5.0], "tags": ["dash", "iframes"]}}
	for k in gadd:
		if not Moves.BASE_DEFS.air_dash.has(k):
			g[k] = gadd[k]


# ================================================================ thrust: Wind Crescent / crossed / wide / Scythe

static func _thrust() -> void:
	KitAir.reg("gust_crescent", SUB, "thrust", {"name": "Wind Crescent",
		"desc": "A crescent of cutting wind @22 m/s. Deflects the light shots it meets and cuts vines (x2). Hold: two crossing crescents, a 2 m wide one, then the Wind Scythe that pierces two targets.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "source": "none", "mat": "air", "tag": "crescent", "mass": 0.0, "speed": 22.0, "gravity": 0.0,
		"power": 8.0, "life": 1.4, "damage": 8.0, "balance": 14.0, "count": 1, "radius": 0.5,
		"tiers": {
			"t1": {"count": 2, "power": 11.0, "damage": 9.0, "cost_add": 2.0},
			"t2": {"count": 1, "power": 15.0, "damage": 12.0, "radius": 1.0, "cost_add": 4.0},
			"t3": {"count": 1, "power": 22.0, "damage": 16.0, "radius": 1.0, "pierce": 2, "cost_add": 8.0},
		},
		"hook_execute": Callable(AirGust, "crescent_execute"),
		"counter": {"cls": "crescent", "power": [8.0, 11.0, 15.0, 22.0]}, "threat": {"cls": "gust", "power": [8.0, 11.0, 15.0, 22.0]},
		"anim": "air_push", "anim_active": "mv_palm_thrust", "fx": {"mat": "wind", "shape": "crescent", "release": "release"},
		"ai": {"role": "poke", "range": [3.0, 20.0], "tags": ["projectile", "cuts_vines", "deflects_light", "pierce", "ranged"]}})


## One crescent per piece, all aimed at the same point so a pair crosses on the target. T2+ is a wide one.
static func crescent_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var count := maxi(1, int(Charge.param(inst, "count", 1)))
	var dir := AirUtil.aim_flat(a, inst)
	var target := Verbs.target_point(w, a, inst, 16.0)
	var perp := dir.cross(Vector3.UP).normalized()
	var speed := float(Charge.param(inst, "speed", 22.0))
	var radius := float(Charge.param(inst, "radius", 0.5))
	var ids: Array = []
	for k in count:
		var made := VerbProjectile.fire(w, a, inst, {"count": 1})
		if made.is_empty():
			continue
		var b: MatBody = made[0]
		var off := 0.0 if count == 1 else (-0.7 if k == 0 else 0.7)
		b.pos += perp * off
		b.radius = radius
		b.vel = (target - b.pos).normalized() * speed
		b.spin = 14.0
		ids.append(b.id)
	inst.data["bodies"] = ids
	return true


## A crescent meets what it passes: light shots are deflected / bent, vines (loose and walls) are cut (cells x|crescent).
static func crescent_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if not b.alive or b.attack_id == 0 or b.form == Sim.Form.ZONE:
		return false
	var owner := w.get_actor(b.attack_owner)
	var c: Agent = null
	for o in w.bodies:
		if o == b or not o.alive or o.captured_by >= 0 or o.controller >= 0:
			continue
		var wallish := o.form == Sim.Form.WALL
		if not wallish and (o.static_body or o.form == Sim.Form.ZONE or o.form == Sim.Form.POOL or o.form == Sim.Form.PUDDLE):
			continue
		var live_shot := o.is_projectile() and o.attack_owner != b.attack_owner and o.mat != Sim.Mat.AIR
		var vine := o.mat == Sim.Mat.PLANT and o.attack_owner != b.attack_owner
		if not (live_shot or vine):
			continue
		var reach: float = b.radius + (maxf(o.wall_half.x, o.wall_half.z) if wallish else o.radius) + 0.3
		if b.pos.distance_to(o.pos) > reach:
			continue
		var key := "%d|%d" % [b.id, o.id]
		if w.tick - int(w._zone_pairs.get(key, -100000)) < 8:
			continue
		w._zone_pairs[key] = w.tick
		if c == null:
			c = Agent.of_body(w, b)
			c.actor = owner
		Interactions.resolve(w, Agent.of_body(w, o, owner), c, {"site": "crescent"}, Interactions.PASS_RULE)
		if not b.alive:
			break
	return false


# ================================================================ ground: Dust Devil Line

static func _ground() -> void:
	KitAir.reg("gust_dust_line", SUB, "ground", {"name": "Dust Devil Line",
		"desc": "A low line of wind races 14 m/s along the ground: trips (-30 balance), knocks up 3, raises dust (light blind). Hold: wider, stronger, longer.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(12), "active": _s(6), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "source": "none", "mat": "air", "tag": "dust_line", "mass": 0.0, "speed": 14.0, "budget": 10.0,
		"width": 1.6, "damage": 5.0, "balance": 30.0, "knock": 3.0, "lift": 3.0, "kind": "air", "steer": 10.0, "power": 10.0,
		"hit_status": "blinded", "hit_status_t": 1.0,
		"tiers": {
			"t1": {"power": 14.0, "budget": 11.0, "width": 1.9, "cost_add": 2.0, "hit_status_t": 1.4},
			"t2": {"power": 18.0, "budget": 12.0, "width": 2.4, "damage": 8.0, "cost_add": 4.0, "hit_status_t": 1.8},
			"t3": {"power": 24.0, "budget": 14.0, "width": 3.0, "damage": 11.0, "balance": 40.0, "cost_add": 8.0, "hit_status_t": 2.2},
		},
		"counter": {"cls": "gust", "power": [10.0, 14.0, 18.0, 24.0]}, "threat": {"cls": "gust", "power": [10.0, 14.0, 18.0, 24.0]},
		"anim": "mv_ground_slap", "fx": {"mat": "wind", "release": "release"},
		"ai": {"role": "zone", "range": [2.0, 14.0], "tags": ["ground_line", "trips", "blinds", "ranged"]}})


# ================================================================ sweep: Crosswind

static func _sweep() -> void:
	KitAir.reg("gust_crosswind", SUB, "sweep", {"name": "Crosswind",
		"desc": "A lateral sweep of wind (120 deg, 5 m): shoves fighters sideways and curves the projectiles in flight by up to 40 deg - how far depends on how heavy and fast they are.",
		"module": "verbs", "verb": "cone",
		"startup": _s(10), "active": _s(8), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "cls": "gust", "power": 8.0, "range": 5.0, "angle": 60.0, "damage": 4.0, "balance": 16.0, "knock": 6.0, "lift": 0.6,
		"tiers": {
			"t1": {"power": 12.0, "range": 6.0, "knock": 7.0, "balance": 20.0, "cost_add": 2.0},
			"t2": {"power": 16.0, "range": 7.0, "knock": 8.0, "damage": 7.0, "balance": 26.0, "cost_add": 4.0},
			"t3": {"power": 22.0, "range": 8.0, "knock": 10.0, "damage": 10.0, "balance": 34.0, "cost_add": 8.0},
		},
		"hook_execute": Callable(AirGust, "crosswind_execute"),
		"counter": {"cls": "crosswind", "power": [8.0, 12.0, 16.0, 22.0]}, "threat": {"cls": "gust", "power": [8.0, 12.0, 16.0, 22.0]},
		"anim": "air_gust", "anim_active": "mv_sweep_low", "fx": {"mat": "wind", "shape": "fan", "cast": "cast", "release": "cone"},
		"ai": {"role": "poke", "range": [1.0, 7.0], "tags": ["cone", "bends_projectiles", "shove", "area"]}})


static func crosswind_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var rng_m := float(Charge.param(inst, "range", 5.0))
	var half := float(Charge.param(inst, "angle", 60.0))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var power := float(Charge.param(inst, "power", 8.0))
	# The volume is gust pressure to fighters and their guards; loose bodies meet it as the class "crosswind"
	# (cells x|crosswind: bend, by how much the wind out-powers the body).
	var v := Agent.of_volume(w, a, inst, &"gust", a.chest(), dir, {"P": power})
	v.ccls = &"crosswind"
	v.data["knock"] = float(Charge.param(inst, "knock", 6.0))
	Verbs.fx(w, a, inst, "cone", {"length": rng_m, "angle": half, "power": v.power})
	var side := dir.cross(Vector3.UP).normalized()
	for b in w.bodies:
		if not b.alive or b == w.pool or b.controller == a.id or b.captured_by >= 0 or b.static_body:
			continue
		if b.form == Sim.Form.WALL or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE or b.form == Sim.Form.ZONE:
			continue
		if not AirUtil.in_cone(a.chest(), dir, b.pos, rng_m, half, b.radius):
			continue
		Interactions.resolve(w, Agent.of_body(w, b, a), v, {"site": "gust"}, Interactions.PASS_RULE)
	for t in w.actors_in_cone(a, dir, rng_m, half):
		var rel := t.pos - a.pos
		var sgn := 1.0 if rel.dot(side) >= 0.0 else -1.0
		var bal := float(Charge.param(inst, "balance", 16.0)) * (1.5 if Status.has(t, "flight") else 1.0)
		var info := {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "damage", 4.0)), "balance": bal,
			"knock": side * sgn * float(v.data.knock) + Vector3(0, float(Charge.param(inst, "lift", 0.6)), 0), "kind": "air",
			"from": a.chest(), "agent": v, "power": v.power, "tier": v.tier, "mat": "wind"}
		w.hit_actor(t, info)
	return true


# ================================================================ push: Wall of Wind / sink: Downdraft

static func _push_sink() -> void:
	KitAir.reg("gust_wall", SUB, "push", {"name": "Wall of Wind",
		"desc": "From the guard: send the wind forward as a moving wall (8 m/s, 3 m wide, 6 m). Shoves fighters, turns light projectiles, carries clouds along.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(8), "active": _s(30), "recovery": _s(16), "cancel": 0.6,
		"cost": 6.0, "source": "none", "mat": "air", "tag": "wind_wall", "mass": 0.0, "speed": 8.0, "budget": 6.0,
		"width": 3.0, "damage": 4.0, "balance": 22.0, "knock": 6.0, "lift": 1.0, "kind": "air", "steer": 0.0, "power": 14.0,
		"counter": {"cls": "gust", "power": [14.0]}, "threat": {"cls": "gust", "power": [14.0]},
		"anim": "mv_push_two_hand", "fx": {"mat": "wind", "shape": "fan", "release": "release"},
		"ai": {"role": "counter", "range": [0.0, 7.0], "tags": ["from_guard", "wall", "pushes_actors", "carries_clouds", "deflects_light"]}})
	KitAir.reg("gust_downdraft", SUB, "sink", {"name": "Downdraft",
		"desc": "From the guard: a blast of wind driven down (r 3 m, P 12): slams airborne enemies to the ground (anti-flight), flattens fire fields, clears clouds.",
		"module": "verbs", "verb": "burst",
		"startup": _s(6), "active": _s(4), "recovery": _s(14), "cancel": 0.6,
		"cost": 5.0, "at": "self", "radius": 3.0, "power": 12.0, "damage": 5.0, "balance": 18.0, "knock": 4.0, "lift": -8.0, "cls": "gust",
		"hook_execute": Callable(AirGust, "downdraft_execute"),
		"counter": {"cls": "gust", "power": [12.0]}, "threat": {"cls": "gust", "power": [12.0]},
		"anim": "mv_overhead_slam", "fx": {"mat": "wind", "shape": "down", "release": "burst"},
		"ai": {"role": "counter", "range": [0.0, 3.5], "tags": ["from_guard", "anti_air", "anti_flight", "clears_clouds", "flattens_fire"]}})


static func downdraft_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var r := float(Charge.param(inst, "radius", 3.0))
	var prm := {"radius": r, "power": float(Charge.param(inst, "power", 12.0)), "damage": float(Charge.param(inst, "damage", 5.0)),
		"balance": float(Charge.param(inst, "balance", 18.0)), "knock": float(Charge.param(inst, "knock", 4.0)),
		"lift": float(Charge.param(inst, "lift", -8.0)), "cls": "gust", "mat": "wind", "heat_hu": 0.0}
	var p := a.pos + Vector3(0, 0.6, 0)
	# Airborne enemies are slammed down before the blast reaches them (flight ends, the fall is fast).
	for t in AirUtil.foes_near(w, a, a.pos + Vector3(0, 1.0, 0), r + 1.2):
		if AirUtil.airborne(t) and not Status.immune(t, "knockback"):
			AirSound.end_flight(w, t, "slammed")
			t.vel.y = -16.0
			t.grounded = false
			t.balance = maxf(0.0, t.balance - 25.0)
			t.balance_idle = 0.0
			w.emit("slam", {"actor": t.id, "by": a.id})
	VerbVolume.burst_at(w, a, inst, p, prm)
	return true


# ================================================================ tech: Wind Grip (context of the legacy updraft)

static func _tech() -> void:
	# A context move of air_tech (unbound: the technique slot stays the legacy updraft; the Lab lists it as a context).
	KitAir.reg("gust_grip", SUB, "tech", {"name": "Wind Grip",
		"desc": "Seize a light body (<= 30 kg: stone, water, ice, sand, steam, mist, a fireball) in the aim cone within 9 m with wind. Drag-aim, release to fling it. A gripped fireball is fed: +20 % heat.",
		"module": "verbs", "verb": "grip",
		"startup": _s(6), "active": _s(4), "recovery": _s(15), "cancel": 0.5, "move_channel": 0.6,
		"cost": 6.0, "ccls": "grip_wind", "reach": GRIP_REACH, "cone": GRIP_CONE, "base": 0.9, "grip_mult": 1.0, "max_mass": AirUtil.LIGHT,
		"rip_source": "none", "speed": 20.0, "damage": 10.0, "balance": 20.0, "gravity": 0.25, "upkeep": 1.5, "mode_label": "WIND GRIP",
		"hook_tick": Callable(AirGust, "grip_tick"),
		"counter": {"cls": "grip_wind"}, "threat": {"cls": "gust"},
		"anim": "air_push", "anim_hold": "mv_wide_draw", "anim_active": "mv_palm_thrust", "fx": {"mat": "wind", "release": "release"},
		"ai": {"role": "counter", "range": [0.0, 9.0], "tags": ["reclaim", "light_only", "vs_fireball", "vs_stone", "vs_sand", "vs_water"]}}, false)


## The body a Wind Grip would seize (hostile shots first), or null.
static func grip_target(w: CombatWorld, a: ActorState, dir: Vector3) -> MatBody:
	return w.find_body(a, dir, GRIP_REACH, GRIP_CONE, func(b: MatBody) -> bool:
		return b.controller != a.id and AirUtil.carriable(b) and Interactions.allows(b, &"grip_wind"))


## Each tick of a Wind Grip: a seized fireball is fed once (+20 % heat, booked as created: fantasy oxygen).
static func grip_tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	var b := w.held(a)
	if b == null or inst.data.get("fed", false):
		return
	inst.data["fed"] = true
	if b.mat == Sim.Mat.FIRE and b.heat_payload > 0.0 and not b.props.get("fed", false):
		var add := b.heat_payload * (FIRE_FEED - 1.0)
		b.heat_payload += add
		w.ledger.generated += add
		b.props["fed"] = true
		w.emit("fed", {"body": b.id, "by": a.id, "add": add, "payload": b.heat_payload})


static func tech_preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	if a.grounded:
		var b := grip_target(w, a, dir)
		if b != null:
			return {"mode": "WIND GRIP", "body": b.id, "ok": true, "reason": ""}
		return {"mode": "UPDRAFT", "body": -1, "ok": true, "reason": ""}
	return {"mode": "GLIDE", "body": -1, "ok": true, "reason": ""}


# ================================================================ evade_hold: Tailwind

static func _evade_hold() -> void:
	KitAir.reg("gust_tailwind", SUB, "evade_hold", {"name": "Tailwind",
		"desc": "Hold EVADE: a wind at your back, x1.3 run speed while held (6 Focus/s).",
		"module": "verbs", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(8), "cancel": 0.5,
		"cost": 0.0, "kind": "run", "speed_mult": 1.3, "upkeep": 6.0,
		"anim": "run", "fx": {"mat": "wind", "aura": "aura"},
		"ai": {"role": "mobility", "range": [0.0, 20.0], "tags": ["run", "chase", "retreat"]}})
