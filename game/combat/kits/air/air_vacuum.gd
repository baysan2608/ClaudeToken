class_name AirVacuum
extends RefCounted
## Air / Vacuum (sub 2) - pressure and absence. docs/MOVESET.md §7.15, §8.4 (Vacuum column), §9.3 combos 17, 21.
##
## strike  Pressure Palm -> Air Cannon -> Implode -> Collapse      thrust Suction Line       ground Pressure Mine
## sweep   Vacuum Arc                                              guard  Null Bubble (+ Vacuum Catch)
## push    Pressure Wave                                           sink   Anchor
## tech    Vacuum Well (suction zone; release = collapse + air inrush)
## evade   Pressure Hop                                            hold   Slipstream
##
## A vacuum holds no fire, carries no sound and insulates against lightning; solids and liquids pass straight
## through it. The Vacuum Well pulls projectiles and clouds (steam condenses to water, sand compresses to
## sandstone - both booked), drags fighters (-40 % moving away) and puts fire out; when it collapses it crushes
## what it held and leaves an air inrush zone (tag inrush) that Fire's detonations read for x1.5 (combo 21).

const E := 3
const SUB := 2


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
	_evade()
	CombatWorld.register_body_tick(&"vacuum_well", Callable(AirVacuum, "well_tick"))
	CombatWorld.register_zone_effect(&"vacuum_well", Callable(AirVacuum, "well_effect"))
	CombatWorld.register_zone_effect(&"null_bubble", Callable(AirVacuum, "bubble_effect"))
	CombatWorld.register_zone_effect(&"mine", Callable(AirVacuum, "mine_effect"))
	CombatWorld.register_tech_preview(E, SUB, Callable(AirVacuum, "tech_preview"))


# ================================================================ strike: Pressure Palm / Air Cannon / Implode / Collapse

static func _strike() -> void:
	KitAir.reg("vacuum_palm", SUB, "strike", {"name": "Pressure Palm / Air Cannon / Implode / Collapse",
		"desc": "A point-blank burst of pressure (knock 8, -30 balance). Hold: a 12 m pressure bullet (Air Cannon), then Implode - a vacuum point 8 m ahead that pulls 3 m for 0.4 s and collapses - and Collapse (r 4 m, crush -60 balance). Flames inside a vacuum die; sound inside dies.",
		"module": "verbs", "verb": "burst",
		"startup": _s(8), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "at": "ahead", "distance": 1.3, "radius": 1.7, "power": 10.0, "damage": 6.0, "balance": 30.0, "knock": 8.0, "lift": 1.5, "cls": "blast",
		"tiers": {
			"t1": {"range": 12.0, "width": 0.5, "power": 14.0, "damage": 10.0, "balance": 30.0, "knock": 6.0, "cost_add": 4.0},
			"t2": {"implode": true, "range": 8.0, "radius": 3.0, "power": 22.0, "damage": 12.0, "balance": 40.0, "pull_speed": 7.5, "collapse_at": 0.4, "cost_add": 8.0},
			"t3": {"radius": 4.0, "power": 30.0, "damage": 18.0, "balance": 60.0, "pull_speed": 8.5, "cost_add": 14.0},
		},
		"hook_execute": Callable(AirVacuum, "palm_execute"),
		"counter": {"cls": "blast", "power": [10.0, 14.0, 22.0, 30.0]}, "threat": {"cls": "blast", "power": [10.0, 14.0, 22.0, 30.0]},
		"anim": "air_push", "anim_active": "mv_push_two_hand", "fx": {"mat": "vacuum", "shape": "", "release": "burst"},
		"ai": {"role": "poke", "range": [0.5, 12.0], "tags": ["burst", "pull", "crush", "vs_fire", "vs_sound"]}})


static func palm_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var tier := inst.tier()
	if tier == 0:
		return false                                   # the burst verb
	if tier == 1:
		VerbVolume.beam(w, a, inst)
		return true
	var dir := AirUtil.aim_flat(a, inst)
	var rng_m := float(Charge.param(inst, "range", 8.0))
	var p := a.pos + dir * rng_m
	var t := w.get_actor(a.lock_target)
	if t != null and not inst.data.get("aim_active", false):
		var to := t.pos - a.pos
		to.y = 0.0
		if to.length() < rng_m + 2.0:
			p = a.pos + to.normalized() * maxf(2.0, to.length() - 0.5)
	var z := spawn_well(w, a, inst, p, float(Charge.param(inst, "radius", 3.0)), float(Charge.param(inst, "power", 22.0)))
	z.props["collapse_at"] = 0.4 if tier == 2 else 0.45
	z.props["pull_speed"] = float(Charge.param(inst, "pull_speed", 7.5))
	z.props["crush_balance"] = float(Charge.param(inst, "balance", 40.0))
	z.props["crush_damage"] = float(Charge.param(inst, "damage", 12.0))
	inst.data["bodies"] = [z.id]
	return true


# ================================================================ the Vacuum Well zone

static func spawn_well(w: CombatWorld, a: ActorState, inst: ActionInst, p: Vector3, radius: float, power: float) -> MatBody:
	var z := AirUtil.zone(w, "vacuum_well", p, radius, a.id, power, -1.0, {"height": 4.0, "rate": 0.1}, inst.tier(), SUB)
	Verbs.fx(w, a, inst, "ring", {"pos": z.pos, "radius": radius, "body": z.id, "power": power})
	return z


## An implosion point collapses on schedule (props.collapse_at); a summoned well waits for its caster's release.
static func well_tick(w: CombatWorld, z: MatBody, _dt: float) -> bool:
	if z.form != Sim.Form.ZONE or not z.alive:
		return false
	var at := float(z.props.get("collapse_at", -1.0))
	if at >= 0.0 and z.age >= at:
		collapse(w, z)
		return true
	return false


## Pulls the projectiles, clouds and light bodies near it, drags the fighters (-40 % moving away); what enters the zone
## is met through the rules (captured, compressed, snuffed).
static func well_effect(w: CombatWorld, z: MatBody, dt: float) -> void:
	z.spin = 0.0
	var reach := z.zone_radius * 1.8
	var centre := z.pos + Vector3(0, 1.0, 0)
	var pull_a := 8.0 + 0.6 * z.power
	for b in w.bodies:
		if b == z or not b.alive or b.static_body or b.controller >= 0 or b.captured_by >= 0 or b.mass > 80.0:
			continue
		if b.form == Sim.Form.WALL or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE or b.form == Sim.Form.ZONE or b.mat == Sim.Mat.AIR:
			continue
		var to := centre - b.pos
		var d := to.length()
		if d > reach or d < 0.3:
			continue
		var k := 1.0 if b.mass <= AirUtil.LIGHT else 0.4
		b.vel += to.normalized() * pull_a * (1.0 - d / reach) * k * dt
		b.on_ground = false
	var inward_speed := float(z.props.get("pull_speed", 3.5))
	for t in w.actors:
		if t.health <= 0.0 or t.id == z.owner:
			continue
		var rel := z.pos - t.pos
		rel.y = 0.0
		var d2 := rel.length()
		if d2 > reach or d2 < 0.2:
			continue
		var inward := rel.normalized()
		var falloff := 1.0 - d2 / reach
		var vout := -t.vel.dot(inward)
		if vout > 0.0:
			AirUtil.shove(t, inward * vout * 0.4, "pull")       # -40 % moving away
		var vin := t.vel.dot(inward)
		if vin < inward_speed * falloff:
			AirUtil.shove(t, inward * (inward_speed * falloff - vin), "pull")


## The well collapses: captured bodies drop, the air rushes back in (crush -balance, an inrush zone), the zone closes.
static func collapse(w: CombatWorld, z: MatBody) -> void:
	if not z.alive:
		return
	var owner := w.get_actor(z.owner)
	var r := z.zone_radius
	w.release_captured(z)
	var prm := {"radius": r * 1.15, "power": z.power, "damage": float(z.props.get("crush_damage", 8.0)),
		"balance": float(z.props.get("crush_balance", 30.0) if z.props.has("crush_balance") else 20.0 + 0.4 * z.power),
		"knock": 2.0, "lift": 0.5, "cls": "blast", "mat": "vacuum", "heat_hu": 0.0}
	VerbVolume.burst_at(w, owner, null, z.pos + Vector3(0, 1.0, 0), prm)
	AirUtil.spawn_inrush(w, z.pos, r, z.power, z.owner)
	w.emit("collapse", {"body": z.id, "pos": z.pos, "radius": r, "power": z.power, "owner": z.owner})
	w.close_zone(z, "collapse")


# ================================================================ thrust: Suction Line

static func _thrust() -> void:
	KitAir.reg("vacuum_suction", SUB, "thrust", {"name": "Suction Line",
		"desc": "A suction beam (10 m): pulls the fighter it meets 2 m toward you - or pulls YOU to a wall or anchor. Light projectiles (<= 12 kg) it meets are yanked into your hand.",
		"module": "verbs", "verb": "beam",
		"startup": _s(10), "active": _s(12), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "range": 10.0, "width": 0.7, "cls": "vacuum", "power": 8.0, "damage": 4.0, "balance": 14.0, "pull": 7.5,
		"tiers": {
			"t1": {"power": 12.0, "pull": 8.5, "damage": 6.0, "cost_add": 2.0},
			"t2": {"power": 16.0, "pull": 9.5, "damage": 8.0, "range": 11.0, "cost_add": 4.0},
			"t3": {"power": 22.0, "pull": 11.0, "damage": 11.0, "range": 12.0, "cost_add": 8.0},
		},
		"hook_execute": Callable(AirVacuum, "suction_execute"),
		"counter": {"cls": "suction", "power": [8.0, 12.0, 16.0, 22.0]}, "threat": {"cls": "vacuum", "power": [8.0, 12.0, 16.0, 22.0]},
		"anim": "mv_push_two_hand", "anim_active": "mv_wide_draw", "fx": {"mat": "vacuum", "shape": "", "release": "beam"},
		"ai": {"role": "setup", "range": [2.0, 12.0], "tags": ["beam", "pull", "reclaim_light", "grapple", "vs_metal"]}})


static func suction_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var rng_m := float(Charge.param(inst, "range", 10.0))
	var width := float(Charge.param(inst, "width", 0.7))
	var dir := AirUtil.aim_flat(a, inst)
	var start := a.hand_point() + Vector3(0, 0.2, 0)
	var end := start + dir * rng_m
	var power := float(Charge.param(inst, "power", 8.0))
	var v := Agent.of_volume(w, a, inst, &"vacuum", start, dir, {"P": power})
	v.ccls = &"suction"
	# the nearest of: a barrier / the arena (an anchor to be pulled to), a light body, a fighter
	var stop_t := 1.0
	var anchor := Vector3.ZERO
	var has_anchor := false
	for hb in Conduction.barriers_on(w, start, end):
		stop_t = float(hb.t)
		anchor = start.lerp(end, stop_t)
		has_anchor = true
		break
	var seg_len := rng_m * stop_t
	var best_t := INF
	var best_actor: ActorState = null
	var best_body: MatBody = null
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0:
			continue
		var tt := (t.chest() - start).dot(dir)
		if tt < 0.0 or tt > seg_len:
			continue
		if (start + dir * tt).distance_to(t.chest()) <= width + Sim.ACTOR_RADIUS + 0.3 and tt < best_t:
			best_t = tt
			best_actor = t
			best_body = null
	for b in w.bodies:
		if not AirUtil.carriable(b, 80.0) or b.controller == a.id or b.mat == Sim.Mat.AIR:
			continue
		var tb := (b.pos - start).dot(dir)
		if tb < 0.0 or tb > seg_len:
			continue
		if (start + dir * tb).distance_to(b.pos) <= width + b.radius and tb < best_t:
			best_t = tb
			best_body = b
			best_actor = null
	var end_pt := start + dir * (best_t if best_t < INF else seg_len)
	Verbs.fx(w, a, inst, "beam", {"length": start.distance_to(end_pt), "path": PackedVector3Array([start, end_pt]), "power": power})
	if best_body != null:
		Interactions.resolve(w, Agent.of_body(w, best_body, a), v, {"site": "suction"}, Interactions.PASS_RULE)
	elif best_actor != null:
		var to_me := a.pos - best_actor.pos
		to_me.y = 0.0
		var pull := float(Charge.param(inst, "pull", 7.5))
		w.hit_actor(best_actor, {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "damage", 4.0)),
			"balance": float(Charge.param(inst, "balance", 14.0)) * (1.5 if Status.has(best_actor, "flight") else 1.0),
			"knock": to_me.normalized() * pull + Vector3(0, 0.6, 0), "kind": "vacuum", "from": start, "agent": v, "power": power, "tier": v.tier, "mat": "vacuum"})
	elif has_anchor and not Status.immune(a, "pull"):
		var d := Vector2(anchor.x - a.pos.x, anchor.z - a.pos.z).length()
		var sp := clampf(sqrt(84.0 * maxf(d - 1.4, 0.0)), 6.0, 20.0)
		var dv := Vector3(anchor.x - a.pos.x, 0.0, anchor.z - a.pos.z).normalized() * sp
		a.vel.x = dv.x
		a.vel.z = dv.z
		w.emit("grapple", {"actor": a.id, "to": anchor, "speed": sp})
	return true


# ================================================================ ground: Pressure Mine

static func _ground() -> void:
	KitAir.reg("vacuum_mine", SUB, "ground", {"name": "Pressure Mine",
		"desc": "A pressure trap (r 1.2 m, 10 s) buried 2 m ahead. A fighter who steps on it is blown 7 m up. Hold: stronger.",
		"module": "verbs", "verb": "zone",
		"startup": _s(10), "active": _s(4), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "tag": "mine", "mat": "air", "at": "ahead", "distance": 2.2, "radius": 1.2, "life": 10.0, "power": 12.0, "height": 1.6, "rate": 99.0,
		"lift": 7.0, "damage": 8.0, "balance": 24.0,
		"tiers": {
			"t1": {"power": 16.0, "lift": 8.0, "damage": 10.0, "cost_add": 2.0},
			"t2": {"power": 22.0, "lift": 9.0, "damage": 13.0, "balance": 32.0, "cost_add": 4.0},
			"t3": {"power": 28.0, "lift": 10.0, "damage": 17.0, "balance": 42.0, "radius": 1.6, "cost_add": 8.0},
		},
		"hook_execute": Callable(AirVacuum, "mine_execute"),
		"counter": {"cls": "vacuum", "power": [12.0, 16.0, 22.0, 28.0]}, "threat": {"cls": "blast", "power": [12.0, 16.0, 22.0, 28.0]},
		"anim": "mv_ground_slap", "fx": {"mat": "vacuum", "shape": "ground", "release": "ring"},
		"ai": {"role": "zone", "range": [1.5, 8.0], "tags": ["trap", "launcher", "zone"]}})


static func mine_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var z := VerbZone.spawn(w, a, inst)
	z.sub = SUB
	z.props["lift"] = float(Charge.param(inst, "lift", 7.0))
	z.props["damage"] = float(Charge.param(inst, "damage", 8.0))
	z.props["balance"] = float(Charge.param(inst, "balance", 24.0))
	z.props["rate"] = AirUtil.NO_PAIR
	inst.data["bodies"] = [z.id]
	return true


## Armed after 0.4 s; the first fighter of another team within its radius sets it off (burst: lift 7 m, knock, balance).
static func mine_effect(w: CombatWorld, z: MatBody, _dt: float) -> void:
	if z.age < 0.4:
		return
	var owner := w.get_actor(z.owner)
	for t in w.actors:
		if t.health <= 0.0 or t.id == z.owner or (owner != null and t.team == owner.team):
			continue
		if Vector2(t.pos.x - z.pos.x, t.pos.z - z.pos.z).length() > z.zone_radius + Sim.ACTOR_RADIUS or t.pos.y > z.pos.y + 1.2:
			continue
		var prm := {"radius": z.zone_radius + 0.9, "power": z.power, "damage": float(z.props.get("damage", 8.0)),
			"balance": float(z.props.get("balance", 24.0)), "knock": 3.0, "lift": float(z.props.get("lift", 7.0)), "cls": "blast", "mat": "vacuum", "heat_hu": 0.0}
		VerbVolume.burst_at(w, owner, null, z.pos + Vector3(0, 0.4, 0), prm)
		w.emit("mine_burst", {"body": z.id, "actor": t.id, "pos": z.pos})
		w.close_zone(z, "triggered")
		return


# ================================================================ sweep: Vacuum Arc

static func _sweep() -> void:
	KitAir.reg("vacuum_arc", SUB, "sweep", {"name": "Vacuum Arc",
		"desc": "A near-vacuum crescent (100 deg, 4 m): snuffs the flames in the arc (x2), silences sound, and pulls what it meets a little toward you.",
		"module": "verbs", "verb": "cone",
		"startup": _s(8), "active": _s(6), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "cls": "vacuum", "power": 8.0, "range": 4.0, "angle": 50.0, "damage": 5.0, "balance": 14.0, "knock": -3.5, "lift": 0.3,
		"tiers": {
			"t1": {"power": 10.0, "range": 4.5, "damage": 6.0, "cost_add": 2.0},
			"t2": {"power": 13.0, "range": 5.0, "damage": 8.0, "knock": -4.5, "cost_add": 4.0},
			"t3": {"power": 17.0, "range": 6.0, "damage": 11.0, "knock": -5.5, "cost_add": 8.0},
		},
		"counter": {"cls": "vacuum", "power": [8.0, 10.0, 13.0, 17.0]}, "threat": {"cls": "vacuum", "power": [8.0, 10.0, 13.0, 17.0]},
		"anim": "mv_sweep_low", "anim_active": "air_gust", "fx": {"mat": "vacuum", "shape": "fan", "release": "cone"},
		"ai": {"role": "counter", "range": [0.5, 5.0], "tags": ["cone", "vs_fire", "vs_sound", "pull", "area"]}})


# ================================================================ guard: Null Bubble / Vacuum Catch

static func _guard() -> void:
	KitAir.reg("vacuum_bubble", SUB, "guard", {"name": "Null Bubble",
		"desc": "A vacuum shell around you (r 1.8 m, CP 14): fire and combustion die in it (x2), sound is nullified (x3), mist and steam collapse into water, lightning with E <= 1.5 x CP is blocked (a stronger bolt is weakened). Solids and liquids simply pass. Perfect: Vacuum Catch - a light projectile is caught and spat back. Flick up: Pressure Wave. Flick down: Anchor. Weak to a gale that fills it (x1.2).",
		"module": "verbs", "verb": "barrier", "barrier": "zone", "tag": "null_bubble", "mat": "air", "radius": 1.8, "height": 2.6,
		"stops_bolts": true, "upkeep": 6.0, "move_channel": 0.5, "startup": 0.0, "active": 0.0, "recovery": _s(10), "cost": 6.0,
		"tiers": {"t1": {"radius": 2.0}, "t2": {"radius": 2.2}, "t3": {"radius": 2.5}},
		"counter": {"cls": "bubble_null", "power": [14.0, 16.0, 19.0, 22.0], "perfect": "air_spit"}, "threat": {"cls": "vacuum"},
		"anim": "guard", "fx": {"mat": "vacuum", "aura": "aura"},
		"ai": {"role": "counter", "range": [0.0, 6.0], "tags": ["vs_fire", "vs_sound", "vs_lightning", "vs_combustion", "vs_steam", "solids_pass"]}})


static func bubble_effect(w: CombatWorld, z: MatBody, _dt: float) -> void:
	z.spin = 0.0
	AirUtil.guard_zone_effect(w, z)


# ================================================================ push: Pressure Wave / sink: Anchor

static func _push_sink() -> void:
	KitAir.reg("vacuum_wave", SUB, "push", {"name": "Pressure Wave",
		"desc": "From the Null Bubble: the bubble collapses outward as a ring (r 3 m, P 16): shoves everything near you back.",
		"module": "verbs", "verb": "burst",
		"startup": _s(8), "active": _s(4), "recovery": _s(16), "cancel": 0.6,
		"cost": 4.0, "at": "self", "radius": 3.0, "power": 16.0, "damage": 6.0, "balance": 24.0, "knock": 9.0, "lift": 1.0, "cls": "blast",
		"hook_execute": Callable(AirVacuum, "wave_execute"),
		"counter": {"cls": "blast", "power": [16.0]}, "threat": {"cls": "blast", "power": [16.0]},
		"anim": "mv_push_two_hand", "fx": {"mat": "vacuum", "shape": "open", "release": "ring"},
		"ai": {"role": "counter", "range": [0.0, 3.5], "tags": ["from_guard", "ring", "knockback", "area"]}})
	KitAir.reg("vacuum_anchor", SUB, "sink", {"name": "Anchor",
		"desc": "From the Null Bubble: a low-pressure anchor while held (CP 35): immune to knockback, pulls and tornado lift. You barely move (x0.3).",
		"module": "verbs", "verb": "stance",
		"startup": _s(4), "active": 0.0, "recovery": _s(10), "cancel": 0.5,
		"cost": 4.0, "stance": "anchor", "anchored": true, "anchor_cp": 35.0, "speed_mult": 0.3, "upkeep": 4.0, "held": true,
		"counter": {"cls": "anchor", "power": [35.0]}, "threat": {"cls": "blast"},
		"anim": "guard", "fx": {"mat": "vacuum", "aura": "aura"},
		"ai": {"role": "counter", "range": [0.0, 1.0], "tags": ["from_guard", "anchor", "vs_pull", "vs_tornado", "vs_knockback"]}})


static func wave_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var p := a.pos + Vector3(0, 1.0, 0)
	var bubbles := AirUtil.zones_of(w, a.id, "null_bubble")
	for z in bubbles:
		p = z.pos + Vector3(0, 1.0, 0)
		w.close_zone(z, "pressure_wave")
	var prm := {"radius": float(Charge.param(inst, "radius", 3.0)), "power": float(Charge.param(inst, "power", 16.0)),
		"damage": float(Charge.param(inst, "damage", 6.0)), "balance": float(Charge.param(inst, "balance", 24.0)),
		"knock": float(Charge.param(inst, "knock", 9.0)), "lift": float(Charge.param(inst, "lift", 1.0)), "cls": "blast", "mat": "vacuum", "heat_hu": 0.0}
	VerbVolume.burst_at(w, a, inst, p, prm)
	return true


# ================================================================ tech: Vacuum Well

static func _tech() -> void:
	KitAir.reg("vacuum_well", SUB, "tech", {"name": "Vacuum Well",
		"desc": "A suction zone at the aim point (9 m, r 3 m) while you hold: it pulls projectiles out of the air (they hang in the well), pulls and compresses clouds (steam -> water, sand -> sandstone), drags fighters (-40 % moving away) and puts fire out. Release: it collapses - what it held is crushed and the air rushes in (a combustion there x1.5). Hold longer: stronger, wider.",
		"module": "kit_air", "verb": "summon",
		"startup": _s(12), "active": 0.0, "recovery": _s(16), "cancel": 0.5, "move_channel": 0.6,
		"cost": 8.0, "upkeep": 8.0, "tag": "vacuum_well", "mat": "air", "at": "aim", "range": 9.0, "radius": 3.0, "power": 10.0, "height": 4.0,
		"steer_speed": 0.0, "linger": 0.0, "pull_speed": 3.5,
		"tiers": {
			"t1": {"power": 14.0, "radius": 3.0},
			"t2": {"power": 20.0, "radius": 3.5},
			"t3": {"power": 28.0, "radius": 4.0},
		},
		"counter": {"cls": "vacuum_well", "power": [10.0, 14.0, 20.0, 28.0]}, "threat": {"cls": "vacuum", "power": [10.0, 14.0, 20.0, 28.0]},
		"anim": "mv_wide_draw", "anim_hold": "glide", "fx": {"mat": "vacuum", "release": "ring", "ring": "ring"},
		"ai": {"role": "zone", "range": [2.0, 9.0], "tags": ["summon", "pull", "compress", "suppress_fire", "then_collapse", "inrush"]}})
	KitAir.handle("vacuum_well", {"phase": Callable(AirVacuum, "well_phase"), "interrupt": Callable(AirVacuum, "well_interrupt")})


static func _collapse_held(w: CombatWorld, inst: ActionInst) -> void:
	var z := w.get_body(int(inst.data.get("summon", -1)))
	inst.data.erase("summon")
	if z != null and z.alive:
		z.props["crush_balance"] = 20.0 + 0.4 * z.power
		z.props["crush_damage"] = 4.0 + 0.4 * z.power
		collapse(w, z)


static func well_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		_collapse_held(w, inst)
	Verbs.on_phase(w, a, inst, p)


static func well_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	_collapse_held(w, inst)
	Verbs.on_interrupt(w, a, inst, reason)


static func tech_preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var ok := a.focus >= 8.0
	return {"mode": "WELL", "body": -1, "ok": ok, "reason": "" if ok else "focus"}


# ================================================================ evade: Pressure Hop / hold: Slipstream

static func _evade() -> void:
	KitAir.reg("vacuum_hop", SUB, "evade", {"name": "Pressure Hop",
		"desc": "A leap 3 m up and forward (9 i-frames) on a cushion of low pressure: a float landing without landing lag.",
		"module": "kit_air", "verb": "dash",
		"startup": 0.0, "active": _s(18), "recovery": _s(6), "cancel": 0.5,
		"cost": 5.0, "distance": 3.0, "iframes": _s(9), "dir": "stick", "up": 8.0,
		"anim": "jump", "fx": {"mat": "vacuum", "trail": "trail"},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["dash", "iframes", "vertical", "vs_ground_line"]}})
	KitAir.handle("vacuum_hop", {"phase": Callable(AirVacuum, "hop_phase")})
	KitAir.reg("vacuum_slipstream", SUB, "evade_hold", {"name": "Slipstream",
		"desc": "Hold EVADE: x1.3 speed in a low-pressure wake; projectiles behind you are slowed (6 Focus/s).",
		"module": "kit_air", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(8), "cancel": 0.5,
		"cost": 0.0, "kind": "run", "speed_mult": 1.3, "upkeep": 6.0,
		"anim": "run", "fx": {"mat": "vacuum", "aura": "aura"},
		"ai": {"role": "mobility", "range": [0.0, 20.0], "tags": ["run", "retreat", "slows_projectiles_behind"]}})
	KitAir.handle("vacuum_slipstream", {"tick": Callable(AirVacuum, "slipstream_tick")})


## The hop lands softly: still airborne at the end of the dash, the fighter floats down (no landing lag).
static func hop_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	Verbs.on_phase(w, a, inst, p)
	if p == ActionInst.P.RECOVERY and not a.grounded:
		a.gliding = true


## Projectiles behind the fighter are slowed (the wake): x0.96 per tick inside 3.5 m behind the direction of travel.
static func slipstream_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if inst.phase != ActionInst.P.CHANNEL:
		return
	var mv := Vector3(a.vel.x, 0.0, a.vel.z)
	if mv.length() < 1.0:
		return
	var back := -mv.normalized()
	for b in w.bodies:
		if not AirUtil.hostile_shot(b, a) or b.mat == Sim.Mat.AIR:
			continue
		var rel := b.pos - a.chest()
		if rel.length() <= 3.5 and rel.dot(back) > 0.0:
			b.vel *= 0.96


# ================================================================ rules: the Vacuum column (vacuum_well, bubble_null, suction, vacuum)

static func register_rules() -> void:
	Interactions.register_outcome("air_compress", Callable(AirVacuum, "o_compress"))
	Interactions.register_outcome("air_snuff", Callable(AirVacuum, "o_snuff"))
	Interactions.register_outcome("air_spit", Callable(AirVacuum, "o_spit"))
	_well_cells()
	_bubble_cells()
	_suction_cells()
	_arc_cells()


static func _well_cells() -> void:
	var c := "vacuum_well"
	var cap := {"bands": [[0.0, "weaken"], [0.5, "capture"]], "inert": "capture", "inert_else": "pass", "max_captured": 6}
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "water"]:
		AirRules._cell(t, c, cap, {"move": "vacuum_well", "tier": 1, "expect": "capture"} if t in ["stone", "water", "ice"] else {})
	AirRules._cell("sand_surge", c, cap, {"move": "vacuum_well", "tier": 2, "expect": "capture"})
	AirRules._cell("stone_heavy", c, {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 1.0}, {"move": "vacuum_well", "tier": 1, "expect": "weaken"})
	AirRules._cell("boulder", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "vacuum_well", "tier": 3, "expect": "pass"})
	AirRules._cell("magma", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "vacuum_well", "tier": 3, "expect": "pass"})
	AirRules._cell("lava_wave", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "vacuum_well", "tier": 3, "expect": "pass"})
	AirRules._cell("water_wave", c, {"outcome": "weaken", "partial": "weaken", "fail": "weaken"}, {"move": "vacuum_well", "tier": 2, "expect": "weaken"})
	# clouds are pulled in and compressed: steam / mist -> water, sand -> a sandstone chunk (booked conversions)
	for t in ["steam", "mist", "sand", "sand_cloud"]:
		AirRules._cell(t, c, {"bands": [[0.0, "air_compress"]], "full_at": 0.0}, {"move": "vacuum_well", "tier": 1, "expect": "air_compress"} if t in ["steam", "sand_cloud"] else {})
	AirRules._cell("vine", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "vacuum_well", "tier": 1, "expect": "pass"})
	# fire needs air: a well suppresses it (x2)
	for t in ["flame", "blue_fire", "ember", "fire_field"]:
		AirRules._cell(t, c, {"outcome": "air_snuff", "partial": "weaken", "fail": "weaken", "eff": 2.0}, {"move": "vacuum_well", "tier": 1, "expect": "air_snuff"} if t == "flame" else {})
	AirRules._cell("blast", c, {"outcome": "air_snuff", "partial": "weaken", "fail": "weaken", "eff": 2.0}, {"move": "vacuum_well", "tier": 2, "expect": "air_snuff", "tp": 16.0})
	AirRules._cell("lightning", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "vacuum_well", "tier": 1, "expect": "pass"})
	AirRules._cell("gust", c, {"bands": [[0.0, "absorb"]], "eff": 1.2, "full_at": 0.0}, {"move": "vacuum_well", "tier": 1, "expect": "absorb"})
	AirRules._cell("tornado", c, {"bands": [[0.0, "pass"], [0.5, "air_shrink"], [1.0, "air_contest"]], "eff": 1.2}, {"move": "vacuum_well", "tier": 3, "expect": "air_contest", "tp": 25.0})
	AirRules._cell("vacuum", c, {"bands": [[0.0, "air_contest"]], "full_at": 0.0}, {"move": "vacuum_well", "tier": 1, "expect": "air_contest"})
	AirRules._cell("sound", c, {"outcome": "absorb", "partial": "weaken", "fail": "weaken", "eff": 3.0}, {"move": "vacuum_well", "tier": 1, "expect": "absorb"})


static func _bubble_cells() -> void:
	var c := "bubble_null"
	var spit := {"bands": [[0.0, "pass"]], "full_at": 0.0, "perfect": "air_spit", "aura": true, "fallback": "pass"}
	# solids and liquids pass; a perfect bubble catches a light projectile and spits it back (Vacuum Catch)
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "water"]:
		AirRules._cell(t, c, spit, {"move": "vacuum_bubble", "tier": 0, "expect": "pass"} if t == "stone" else {})
	for t in ["stone_heavy", "boulder", "magma", "lava_wave", "sand_surge", "water_wave", "vine", "sand_cloud"]:
		AirRules._cell(t, c, {"bands": [[0.0, "pass"]], "full_at": 0.0, "aura": true}, {"move": "vacuum_bubble", "tier": 0, "expect": "pass"} if t in ["boulder", "lava_wave"] else {})
	# no air, no fire: flames and combustion die at the shell (x2)
	for t in ["flame", "blue_fire", "ember", "fire_field"]:
		AirRules._cell(t, c, {"outcome": "extinguish", "partial": "weaken", "fail": "weaken", "eff": 2.0, "aura": true},
			{"move": "vacuum_bubble", "tier": 0, "expect": "extinguish"} if t in ["flame", "blue_fire"] else {})
	AirRules._cell("blast", c, {"outcome": "extinguish", "partial": "weaken", "fail": "weaken", "eff": 2.0, "aura": true},
		{"move": "vacuum_bubble", "tier": 0, "expect": "extinguish", "tp": 16.0})
	# no medium, no sound: nullified (x3)
	AirRules._cell("sound", c, {"outcome": "absorb", "partial": "weaken", "fail": "weaken", "eff": 3.0, "aura": true},
		{"move": "vacuum_bubble", "tier": 0, "expect": "absorb", "tp": 16.0})
	# mist and steam collapse into water
	AirRules._cell("mist", c, {"bands": [[0.0, "air_compress"]], "full_at": 0.0, "aura": true}, {"move": "vacuum_bubble", "tier": 0, "expect": "air_compress"})
	AirRules._cell("steam", c, {"bands": [[0.0, "air_compress"]], "full_at": 0.0, "aura": true}, {"move": "vacuum_bubble", "tier": 0, "expect": "air_compress"})
	# lightning: a vacuum insulates - blocks a bolt with E <= 1.5 x CP (CP 14 -> 21), weakens a stronger one
	AirRules._cell("lightning", c, {"bands": [[0.0, "pass"], [0.5, "weaken"], [1.0, "block"]], "eff": 1.5, "aura": true},
		{"move": "vacuum_bubble", "tier": 0, "expect": "block", "tp": 20.0})
	# wind: absorbed (x1.5); a tornado or a vacuum overwhelms it; counter-vortex merges
	AirRules._cell("gust", c, {"bands": [[0.0, "absorb"]], "full_at": 0.0, "eff": 1.5, "aura": true}, {"move": "vacuum_bubble", "tier": 0, "expect": "absorb"})
	AirRules._cell("tornado", c, {"bands": [[0.0, "overwhelm"], [1.0, "block"]], "aura": true}, {"move": "vacuum_bubble", "tier": 0, "expect": "overwhelm", "tp": 30.0})
	AirRules._cell("vacuum", c, {"bands": [[0.0, "air_contest"]], "full_at": 0.0, "aura": true}, {"move": "vacuum_bubble", "tier": 0, "expect": "air_contest"})


static func _suction_cells() -> void:
	var c := "suction"
	# light projectiles (<= 12 kg) are yanked into your hand; heavier ones are only pulled off line
	var yank := {"bands": [[0.0, "bend"], [0.5, "reclaim"]], "when": {"mass_max": 12.0}, "else": "bend", "bend_impulse": 40.0, "inert": "reclaim", "inert_else": "pass", "authority": 0.9}
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "water", "stone_heavy"]:
		AirRules._cell(t, c, yank, {"move": "vacuum_suction", "tier": 0, "expect": "reclaim", "mass": 6.0, "tp": 12.0} if t == "metal" else {})
	AirRules._cell("*", c, {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "air_suction_default"})


static func _arc_cells() -> void:
	var c := "vacuum"
	# the Vacuum Arc (a volume of class vacuum): snuffs flames (x2), silences sound, collapses vapour, leaves solids alone
	for t in ["flame", "blue_fire", "ember"]:
		AirRules._cell(t, c, {"outcome": "extinguish", "partial": "weaken", "fail": "weaken", "eff": 2.0},
			{"move": "vacuum_arc", "tier": 0, "expect": "extinguish"} if t == "flame" else {})
	AirRules._cell("sound", c, {"outcome": "absorb", "partial": "weaken", "fail": "weaken", "eff": 3.0}, {"move": "vacuum_arc", "tier": 0, "expect": "absorb", "tp": 8.0})
	AirRules._cells(["mist", "steam"], c, {"bands": [[0.0, "air_compress"]], "full_at": 0.0})
	AirRules._cell("blast", c, {"outcome": "extinguish", "partial": "weaken", "fail": "weaken", "eff": 2.0}, {"move": "vacuum_arc", "tier": 2, "expect": "extinguish", "tp": 16.0})
	AirRules._cell("*", c, {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "air_vacuum_default"})


## Mist / steam collapse into water (booked), sand compresses into a sandstone chunk (sand_to_sandstone).
static func o_compress(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive:
		return false
	if b.mat == Sim.Mat.SAND:
		w.convert_mat(b, Sim.Mat.STONE, "sand_to_sandstone")
		b.tag = &"sandstone"
		b.form = Sim.Form.CHUNK
		b.max_life = Sim.REMNANT_LIFETIME
		b.attack_id = 0
		b.vel *= 0.3
		AirOutcomes.report(res, "transform", "sandstone")
	elif AirUtil.condense(w, b):
		AirOutcomes.report(res, "transform", "water")
	else:
		return false
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Fire / blasts inside a vacuum go out: bodies and zones are put out (their heat leaves through the ledger), volumes lose their heat budget.
static func o_snuff(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	var b := t.body
	if b != null and b.alive:
		if b.form == Sim.Form.ZONE:
			w.close_zone(b, "extinguished")
		else:
			w.emit("extinguish", {"body": b.id})
			w.decay_body(b, "extinguished")
	else:
		t.heat = 0.0
	AirOutcomes.report(res, "extinguish")
	return true


## Vacuum Catch: a perfect bubble catches a light projectile (<= 30 kg) and spits it straight back as the caster's attack.
static func o_spit(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or b.mass > AirUtil.LIGHT or not t.hostile or c.actor == null:
		return false
	var ok := Outcomes.reflect(w, t, c, res, r, ctx)
	if ok:
		w.emit("vacuum_catch", {"actor": c.actor.id, "body": b.id})
		AirOutcomes.report(res, "reflect")
	return ok
