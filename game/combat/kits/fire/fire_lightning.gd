class_name FireLightning
extends RefCounted
## Fire / Lightning (sub 2, docs/MOVESET.md §7.11): instant, conductive, ignores mass. Focus costs; the Static Ward
## stores up to 60 static (ActorState.static_charge). The conduction graph (core/conduction.gd, owned by this kit) gains
## charged bodies, relays and rails.
##   strike  Spark (T0, 5 m arc) -> Bolt (T1 0.65 s, legacy stats) -> Storm Bolt (T2 1.2 s: blasts through barriers
##           with grounding < E, continues with E - 0.5 CP, forks to 2 conductors) -> Skybreak (T3 1.8 s: from above
##           after a 0.4 s telegraph, walls between ignored, thunder deafens 0.3 s within 4 m)
##   thrust  Rail Arc (follows conductors in its path: through a connected water jet into its holder)
##   ground  Ground Current -> Storm Grid (conductive ground only; frozen puddles / ice floors stop it; Grounding immune)
##   sweep   Arc Fan · guard Static Ward · push Static Burst · sink Grounding · tech Conductor's Hand -> Arc Link
##   evade   Arc Step · evade_hold Overcharge

const E := 2
const SUB := 2
const STATIC_MAX := 60.0
const CHARGE_DECAY := 4.0      # E/s a charged body loses
const SKYBREAK_DELAY := 0.4


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
	for pair in [["strike", "spark"], ["thrust", "rail_arc"], ["ground", "ground_current"], ["sweep", "arc_fan"],
			["guard", "static_ward"], ["push", "static_burst"], ["sink", "grounding"], ["tech", "conductors_hand"],
			["evade", "arc_step"], ["evade_hold", "overcharge"]]:
		Moves.bind(E, SUB, pair[0], pair[1])
	KitFire.handle("spark", {"phase": Callable(FireLightning, "spark_phase")})
	KitFire.handle("conductors_hand", {"after": Callable(FireLightning, "hand_after"), "tick": Callable(FireLightning, "hand_tick"),
		"phase": Callable(FireLightning, "hand_phase")})
	KitFire.handle("overcharge", {"tick": Callable(FireLightning, "overcharge_tick")})
	KitFire.handle("static_ward", {"tick": Callable(FireLightning, "ward_tick")})
	CombatWorld.register_body_tick(&"ground_current", Callable(FireLightning, "current_tick"))
	CombatWorld.register_zone_effect(&"static_field", Callable(FireLightning, "static_field_tick"))
	_static_field_cells()


static func _static_field_cells() -> void:
	for t in ["water", "water_wave", "metal", "molten_metal"]:
		FireRules._cell(t, "static_field", {"bands": [[0.0, "fire_charge_body"]], "full_at": 0.0, "share": 0.5},
			{"move": "ground_current", "tier": 3, "expect": "fire_charge_body"})


# ------------------------------------------------------------------ strike: Spark -> Bolt -> Storm Bolt -> Skybreak

static func _strike() -> void:
	Moves.register("spark", {
		"element": E, "sub": SUB, "slot": "strike", "name": "Spark / Bolt / Storm Bolt / Skybreak",
		"desc": "Tap: a 5 m spark to the nearest target or conductor (chains 2 m to wet fighters and metal). Hold 0.65 s: the Bolt (14 m, conducts). Hold 1.2 s: the Storm Bolt blasts through barriers it out-powers and forks. Hold 1.8 s: Skybreak strikes the target from above (walls don't matter) and its thunder deafens.",
		"module": "kit_fire", "verb": "beam", "cls": "lightning",
		"startup": _s(6), "active": _s(4), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"tier_times": [0.65, 1.2, 1.8], "heavy_min": 0.65, "cost": 6.0,
		"range": 5.0, "damage": 6.0, "balance": 12.0, "E": 10.0, "conduct_budget": 8.0, "max_hops": 2, "chain_r": 2.0,
		"tiers": {
			"t1": {"charge_drain": 0.0, "cost_add": 16.0, "range": 14.0, "damage": 24.0, "balance": 40.0, "E": 24.0,
				"conduct_budget": 26.0, "max_hops": 4},
			"t2": {"charge_drain": 8.0, "cost_add": 24.0, "range": 16.0, "damage": 30.0, "balance": 50.0, "E": 36.0,
				"conduct_budget": 30.0, "max_hops": 5, "forks": 2},
			"t3": {"charge_drain": 8.0, "cost_add": 34.0, "range": 18.0, "damage": 38.0, "balance": 70.0, "E": 52.0,
				"conduct_budget": 34.0, "max_hops": 5, "sky": true, "deafen_r": 4.0, "deafen_t": 0.3},
		},
		"counter": {"cls": "lightning", "power": [10.0, 24.0, 36.0, 52.0]}, "threat": {"cls": "lightning"},
		"anim": "fire_jab", "anim_charge": "lightning_charge", "anim_active": "lightning_release",
		"fx": {"mat": "lightning", "shape": ""},
		"ai": {"role": "finisher", "range": [0.0, 16.0], "tags": ["instant", "conducts", "blast_through_t2", "from_above_t3", "wet_x1_5"]},
	})


static func _def_for(inst: ActionInst, aim: Vector3) -> Dictionary:
	var d := {}
	for k in ["range", "damage", "balance", "E", "conduct_budget", "max_hops", "forks"]:
		var v: Variant = Charge.param(inst, k, null)
		if v != null:
			d[k] = v
	d["tier"] = inst.tier()
	d["meet_bodies"] = true
	d["aim"] = aim
	return d


static func spark_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p != ActionInst.P.ACTIVE:
		Verbs.on_phase(w, a, inst, p)
		return
	var tier := inst.tier()
	var aim: Vector3 = inst.data.get("aim_point", a.chest() + a.forward() * 10.0)
	var def := _def_for(inst, aim)
	match tier:
		0:
			_spark(w, a, inst, def)
		3:
			_skybreak(w, a, inst, def)
		_:
			var out := Conduction.discharge(w, a, aim, def, inst.attack_id, true)
			Verbs.fx(w, a, inst, "beam", {"path": out.path, "length": float(def.range), "power": float(out.get("e", def.E))})


## T0: a short arc to the locked target if within 5 m, else to the nearest conductor; it chains 2 m to wet fighters
## and conductive bodies (the stun is short).
static func _spark(w: CombatWorld, a: ActorState, inst: ActionInst, def: Dictionary) -> void:
	var rng_m := float(def.range)
	var t := w.get_actor(a.lock_target)
	if t != null and t.chest().distance_to(a.hand_point()) <= rng_m + 0.5:
		def["force_target"] = t.id
	else:
		var b := w.find_body(a, inst.data.get("aim", a.forward()), rng_m, 70.0, func(x: MatBody) -> bool:
			return x.controller != a.id and (Materials.conducts(x) or (x.form == Sim.Form.PUDDLE and x.phase == Sim.Phase.LIQUID)) and x.form != Sim.Form.POOL)
		if b != null:
			def["aim"] = b.pos
	var out := Conduction.discharge(w, a, def.aim, def, inst.attack_id, false)
	Verbs.fx(w, a, inst, "beam", {"path": out.path, "length": rng_m, "power": float(def.E), "shape": "small"})
	var hit_end: Vector3 = (out.path as PackedVector3Array)[(out.path as PackedVector3Array).size() - 1]
	for h in out.hits:
		var ha := w.get_actor(int(h))
		if ha != null:
			Status.apply(w, ha, "shocked", 0.35, 1.0, a.id)
	# Chain 2 m: wet fighters and conductive bodies near the strike.
	var cr := float(Charge.param(inst, "chain_r", 2.0))
	for o in w.actors:
		if o == a or o.team == a.team or o.health <= 0.0 or (out.hits as Array).has(o.id) or o.wetness <= Status.WET_AT:
			continue
		if o.chest().distance_to(hit_end) <= cr + Sim.ACTOR_RADIUS and not Status.immune(o, "conduct"):
			w.hit_actor(o, {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(def.damage) * 0.5, "balance": 8.0,
				"kind": "lightning", "from": hit_end, "unblockable": true})
			Status.apply(w, o, "shocked", 0.3, 1.0, a.id)
			w.emit("lightning", {"actor": a.id, "path": PackedVector3Array([hit_end, o.chest()]), "arcs": [], "blocked": false, "hits": [o.id], "chain": true})
	for b in w.bodies:
		if b.alive and b.mat == Sim.Mat.METAL and b.pos.distance_to(hit_end) <= cr + b.radius:
			charge_body(w, b, float(def.E) * 0.5, a.id)


## T3 Skybreak: a 0.4 s telegraph at the target, then the bolt falls from 12 m above it (walls between the fighters
## don't matter; a roof or a held shield above still would). Thunder deafens everyone within 4 m.
static func _skybreak(w: CombatWorld, a: ActorState, inst: ActionInst, def: Dictionary) -> void:
	var t := w.get_actor(a.lock_target)
	var at: Vector3 = t.pos if t != null and t.pos.distance_to(a.pos) <= float(def.range) + 2.0 else FireUtil.aim_ground(w, a, inst, float(def.range))
	var z := w.spawn_zone(&"static_field", at, 1.4, a.id, float(def.E), Sim.Mat.AIR, 0.0, -1.0, "skybreak:%d" % a.id)
	z.charge = 0.0
	z.props["skybreak"] = true
	z.props["delay"] = SKYBREAK_DELAY
	z.props["target"] = t.id if t != null else -1
	z.props["def"] = def
	z.props["attack_id"] = inst.attack_id
	z.props["deafen_r"] = float(Charge.param(inst, "deafen_r", 4.0))
	z.props["deafen_t"] = float(Charge.param(inst, "deafen_t", 0.3))
	z.props["spare_owner"] = true
	w.emit("telegraph", {"actor": a.id, "move": "skybreak", "pos": at, "time": SKYBREAK_DELAY, "body": z.id})
	FxEvents.fx_for(w, a, inst, "cast", "lightning", {"pos": at + Vector3(0, 0.05, 0), "radius": 1.4, "dur": SKYBREAK_DELAY, "shape": "down"})


static func _sky_strike(w: CombatWorld, z: MatBody) -> void:
	var a := w.get_actor(z.owner)
	var def: Dictionary = z.props.get("def", {})
	w.close_zone(z, "struck")
	if a == null or def.is_empty():
		return
	var t := w.get_actor(int(z.props.get("target", -1)))
	var at := z.pos
	if t != null and t.health > 0.0 and t.pos.distance_to(z.pos) < 3.0:
		at = t.pos   # the strike follows the mark within reach: dodge it by leaving the circle
	var d := def.duplicate()
	d["start"] = at + Vector3(0, 12.0, 0)
	d["range"] = 13.5
	if t != null and Vector2(t.pos.x - at.x, t.pos.z - at.z).length() < 1.2:
		d["force_target"] = t.id
	var out := Conduction.discharge(w, a, at + Vector3(0, 1.0, 0), d, int(z.props.get("attack_id", w.new_attack_id())), false)
	FxEvents.fx(w, "beam", "lightning", {"actor": a.id, "pos": d.start, "path": out.path, "length": 12.0, "power": float(d.E),
		"tier": 3, "move": "spark", "element": E, "sub": SUB, "shape": "down"})
	FxEvents.fx(w, "burst", "lightning", {"actor": a.id, "pos": at + Vector3(0, 0.2, 0), "radius": float(z.props.get("deafen_r", 4.0)),
		"power": float(d.E), "tier": 3, "move": "spark", "element": E, "sub": SUB, "shape": "ground"})
	for o in w.actors:
		if o.health <= 0.0 or o.pos.distance_to(at) > float(z.props.get("deafen_r", 4.0)):
			continue
		Status.apply(w, o, "deafened", float(z.props.get("deafen_t", 0.3)), 1.0, a.id)
	w.emit("thunder", {"actor": a.id, "pos": at, "radius": float(z.props.get("deafen_r", 4.0))})


# ------------------------------------------------------------------ thrust: Rail Arc

static func _thrust() -> void:
	Moves.register("rail_arc", {
		"element": E, "sub": SUB, "slot": "thrust", "name": "Rail Arc",
		"desc": "A straight 16 m arc that follows the conductors it crosses: through a water jet or stream still connected to its caster, through metal, through a charged body.",
		"module": "verbs", "verb": "beam", "cls": "lightning",
		"startup": _s(8), "active": _s(4), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 10.0, "range": 16.0, "damage": 12.0, "balance": 20.0, "E": 14.0, "conduct_budget": 14.0, "max_hops": 4,
		"tiers": {
			"t1": {"cost_add": 4.0, "damage": 16.0, "E": 20.0, "conduct_budget": 18.0},
			"t2": {"cost_add": 8.0, "damage": 20.0, "E": 28.0, "conduct_budget": 22.0, "balance": 28.0},
			"t3": {"cost_add": 12.0, "damage": 26.0, "E": 36.0, "conduct_budget": 28.0, "balance": 36.0, "max_hops": 6},
		},
		"hook_execute": Callable(FireLightning, "rail_execute"),
		"counter": {"cls": "lightning", "power": [14.0, 20.0, 28.0, 36.0]}, "threat": {"cls": "lightning"},
		"anim": "lightning_release", "anim_t3": "mv_palm_thrust", "fx": {"mat": "lightning", "shape": "rod"},
		"ai": {"role": "counter", "range": [2.0, 16.0], "tags": ["instant", "punish_jet", "punish_held_water", "follows_conductors"]},
	})


static func rail_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	var t := w.get_actor(a.lock_target)
	if t != null and not inst.data.get("aim_active", false):
		dir = (t.chest() - (a.hand_point() + Vector3(0, 0.25, 0))).normalized()
	var def := _def_for(inst, Vector3.ZERO)
	var out := Conduction.rail(w, a, dir, def, inst.attack_id)
	Verbs.fx(w, a, inst, "beam", {"path": out.path, "length": float(def.range), "power": float(out.get("e", def.E))})
	return true


# ------------------------------------------------------------------ ground: Ground Current -> Storm Grid

static func _ground() -> void:
	Moves.register("ground_current", {
		"element": E, "sub": SUB, "slot": "ground", "name": "Ground Current / Surge / Network / Storm Grid",
		"desc": "A current races 20 m/s along conductive ground only (puddles, the pool's edge, the metal plate, caltrops, mud); it dies 2 m onto dry stone. Frozen puddles and ice floors stop it; Grounding is immune. T3 Storm Grid electrifies every connected conductive surface for 1.5 s.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(12), "active": _s(6), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"cost": 12.0, "source": "none", "mat": "air", "tag": "ground_current", "mass": 0.2, "speed": 20.0, "budget": 14.0,
		"width": 1.2, "damage": 10.0, "balance": 22.0, "knock": 1.0, "lift": 0.5, "kind": "lightning", "steer": 0.0,
		"hit_status": "shocked", "hit_status_t": 0.4, "E": 12.0, "dry_max": 2.0,
		"tiers": {
			"t1": {"cost_add": 4.0, "E": 18.0, "damage": 13.0, "budget": 16.0},
			"t2": {"cost_add": 8.0, "E": 24.0, "damage": 16.0, "budget": 18.0, "dry_max": 2.5},
			"t3": {"cost_add": 14.0, "E": 32.0, "damage": 18.0, "budget": 20.0, "grid": true, "grid_t": 1.5},
		},
		"hook_execute": Callable(FireLightning, "current_execute"),
		"counter": {"cls": "ground_current", "power": [12.0, 18.0, 24.0, 32.0]}, "threat": {"cls": "lightning"},
		"anim": "mv_ground_slap", "anim_active": "lightning_release", "fx": {"mat": "lightning", "shape": "ground"},
		"ai": {"role": "zone", "range": [2.0, 18.0], "tags": ["ground", "needs_conductive_ground", "punish_wet", "grid_t3"]},
	})


static func current_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var b := VerbGroundLine.launch(w, a, inst, {"source": "none", "mat": "air"})
	if b == null:
		return true
	b.charge = float(Charge.param(inst, "E", 12.0))
	b.power = b.charge
	b.props["channel"] = "E"
	b.props["dry"] = 0.0
	b.props["dry_max"] = float(Charge.param(inst, "dry_max", 2.0))
	if bool(Charge.param(inst, "grid", false)):
		b.props["grid"] = true
		b.props["grid_t"] = float(Charge.param(inst, "grid_t", 1.5))
	b.props["budget_hits"] = float(Charge.param(inst, "damage", 10.0))
	for o in w.actors:
		if Status.immune(o, "conduct"):
			b.hit_set[o.id] = true
	return true


## Ground current: alive only on conductive ground (2 m grace onto dry stone); insulated ground stops it; Grounding
## fighters are skipped; T3 Storm Grid electrifies the whole connected network once it reaches it.
static func current_tick(w: CombatWorld, b: MatBody, dt: float) -> bool:
	if b.form != Sim.Form.WAVE or b.attack_id == 0:
		return false
	for o in w.actors:
		if Status.immune(o, "conduct") or o.flying:
			b.hit_set[o.id] = true
	var node := Conduction.surface_node_at(w, b.pos + Vector3(0, 0.05, 0))
	var frozen := false
	for o in w.bodies:
		if not o.alive or o == b:
			continue
		if o.form == Sim.Form.PUDDLE and o.phase == Sim.Phase.FROZEN and Vector2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() < o.radius:
			frozen = true
		elif o.form == Sim.Form.ZONE and (o.tag == &"ice_floor" or Materials.insulates(o)) and w._in_zone(o, b.pos, 0.1):
			frozen = true
		elif node == "" and o.form == Sim.Form.ZONE and (Materials.conducts(o) or o.tag == &"mud" or o.tag == &"caltrops") and w._in_zone(o, b.pos, 0.1):
			node = "body:%d" % o.id
		elif node == "" and o.form != Sim.Form.ZONE and o.on_ground and Materials.conducts(o) and Vector2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() < o.radius + 0.3:
			node = "body:%d" % o.id
	if frozen and node == "":
		w.emit("insulated", {"body": b.id, "at": b.pos, "by": "ice"})
		w.decay_body(b, "insulated")
		return true
	if node != "":
		b.props["dry"] = 0.0
		if b.props.get("grid", false) and not b.props.get("gridded", false):
			b.props["gridded"] = true
			_storm_grid(w, b, node)
	else:
		b.props["dry"] = float(b.props.get("dry", 0.0)) + float(b.props.get("speed", 20.0)) * dt
		if float(b.props.dry) > float(b.props.get("dry_max", 2.0)):
			w.emit("current_grounded", {"body": b.id, "at": b.pos})
			w.decay_body(b, "grounded")
			return true
	if w.tick % 4 == 0:
		FxEvents.fx(w, "trail", "lightning", {"actor": b.attack_owner, "body": b.id, "pos": b.pos, "dir": b.wave_dir, "length": 1.5,
			"power": b.charge, "shape": "ground", "tier": b.tier})
	return false


## Storm Grid: every surface connected to the reached node is live for grid_t: fighters on it are shocked once (the
## bounded conduct budget), puddles / the plate carry a short static field.
static func _storm_grid(w: CombatWorld, b: MatBody, node: String) -> void:
	var owner := w.get_actor(b.attack_owner)
	if owner == null:
		return
	var reached := Conduction.bfs(Conduction.build_graph(w), [node], 8)
	var n := 0
	for k in reached.keys():
		var p := Conduction.node_point(w, String(k), b.pos)
		if n < 4 and (String(k).begins_with("puddle:") or String(k) == "metal"):
			var z := w.spawn_zone(&"static_field", p, 1.6, owner.id, b.charge * 0.5, Sim.Mat.AIR, 0.0, float(b.props.get("grid_t", 1.5)))
			z.charge = b.charge * 0.5
			z.props["shock"] = true
			n += 1
	var out := {"hits": [], "arcs": []}
	Conduction._conduct_from(w, owner, out, [node], b.pos, b.charge, 8, b.attack_id)
	w.emit("storm_grid", {"actor": owner.id, "nodes": reached.keys(), "victims": out.hits})


# ------------------------------------------------------------------ sweep: Arc Fan

static func _sweep() -> void:
	Moves.register("arc_fan", {
		"element": E, "sub": SUB, "slot": "sweep", "name": "Arc Fan",
		"desc": "Three forks of lightning, 6 m over 60 degrees; each one chains through what it strikes.",
		"module": "verbs", "verb": "beam", "cls": "lightning",
		"startup": _s(10), "active": _s(4), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 12.0, "range": 6.0, "damage": 8.0, "balance": 14.0, "E": 8.0, "conduct_budget": 6.0, "max_hops": 2, "forks_n": 3, "fan": 60.0,
		"tiers": {
			"t1": {"cost_add": 4.0, "damage": 10.0, "E": 10.0, "range": 7.0},
			"t2": {"cost_add": 8.0, "forks_n": 4, "fan": 80.0, "damage": 11.0, "E": 12.0, "range": 8.0},
			"t3": {"cost_add": 12.0, "forks_n": 5, "fan": 100.0, "damage": 12.0, "E": 14.0, "range": 9.0},
		},
		"hook_execute": Callable(FireLightning, "fan_execute"),
		"counter": {"cls": "lightning", "power": [8.0, 10.0, 12.0, 14.0]}, "threat": {"cls": "lightning"},
		"anim": "mv_wide_draw", "anim_active": "lightning_release", "fx": {"mat": "lightning", "shape": "fan"},
		"ai": {"role": "zone", "range": [0.0, 7.0], "tags": ["area", "multi", "conducts"]},
	})


static func fan_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var n := int(Charge.param(inst, "forks_n", 3))
	var fan := deg_to_rad(float(Charge.param(inst, "fan", 60.0)))
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	dir.y = 0.0
	dir = dir.normalized()
	var rng_m := float(Charge.param(inst, "range", 6.0))
	var used := {}
	for k in n:
		var ang := lerpf(-fan * 0.5, fan * 0.5, float(k) / float(maxi(1, n - 1)))
		var fd := dir.rotated(Vector3.UP, ang)
		var def := _def_for(inst, a.chest() + fd * rng_m + Vector3(0, -0.8, 0))
		def["meet_bodies"] = false
		var best: ActorState = null
		var bd := INF
		for t in w.actors:
			if t == a or t.team == a.team or t.health <= 0.0 or used.has(t.id):
				continue
			var to := t.pos - a.pos
			to.y = 0.0
			if to.length() > rng_m + Sim.ACTOR_RADIUS or to.normalized().dot(fd) < cos(deg_to_rad(18.0)):
				continue
			if to.length() < bd:
				bd = to.length()
				best = t
		if best != null:
			used[best.id] = true
			def["force_target"] = best.id
		var out := Conduction.discharge(w, a, def.aim, def, w.new_attack_id(), false)
		Verbs.fx(w, a, inst, "beam", {"path": out.path, "length": rng_m, "power": float(def.E), "dir": fd, "shape": "fan"})
	return true


# ------------------------------------------------------------------ guard: Static Ward

static func _guard() -> void:
	Moves.register("static_ward", {
		"element": E, "sub": SUB, "slot": "guard", "name": "Static Ward / Return Current",
		"desc": "A crackling guard (CP 12). A bolt that hits it is half stored as static (up to 60), half taken; metal shots are deflected (x1.5). Perfect with Return Current: the bolt goes back at 80 %; perfect without it: fully absorbed.",
		"module": "kit_fire", "verb": "barrier", "barrier": "aura", "startup": 0.0, "recovery": _s(6),
		"counter": {"cls": "ward_static", "power": [12.0, 12.0, 12.0, 12.0]}, "move_channel": 0.35,
		"anim": "guard", "anim_active": "deflect", "fx": {"mat": "lightning", "shape": ""},
		"ai": {"role": "counter", "range": [0.0, 16.0], "tags": ["absorb_bolt", "deflect_metal", "store_static"]},
	})


static func ward_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action == inst and inst.phase == ActionInst.P.CHANNEL and w.tick % 20 == 0:
		FxEvents.fx_for(w, a, inst, "aura", "lightning", {"on": true, "power": a.static_charge, "shape": "small"})


# ------------------------------------------------------------------ push / sink: Static Burst, Grounding

static func _push_sink() -> void:
	Moves.register("static_burst", {
		"element": E, "sub": SUB, "slot": "push", "name": "Static Burst",
		"desc": "From the guard: release the stored static as a 4 m cone (E = stored, a 0.3 s shock). Empty: a weak shove.",
		"module": "verbs", "verb": "cone", "cls": "lightning", "channel": "E",
		"startup": _s(8), "active": _s(4), "recovery": _s(16), "cancel": 0.6,
		"cost": 4.0, "range": 4.0, "angle": 30.0, "damage": 2.0, "balance": 12.0, "knock": 5.0, "lift": 0.5,
		"status": "shocked", "status_t": 0.3,
		"hook_execute": Callable(FireLightning, "burst_execute"),
		"counter": {"cls": "lightning"}, "threat": {"cls": "lightning"},
		"anim": "mv_push_two_hand", "anim_active": "lightning_release", "fx": {"mat": "lightning", "shape": "open"},
		"ai": {"role": "counter", "range": [0.0, 4.0], "tags": ["after_static_ward", "stun"]},
	})
	Moves.register("grounding", {
		"element": E, "sub": SUB, "slot": "sink", "name": "Grounding",
		"desc": "From the guard: 1.5 s immune to conducted damage (ground currents pass you, conduction skips you); drains the charge of conductors you touch.",
		"module": "verbs", "verb": "stance", "held": false, "stance": "grounding", "status": "grounding",
		"startup": _s(4), "active": 1.5, "recovery": _s(10), "cost": 4.0, "speed_mult": 0.6,
		"hook_tick": Callable(FireLightning, "grounding_tick"),
		"anim": "mv_stomp", "anim_hold": "guard", "fx": {"mat": "lightning", "shape": "ground"},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["immune_conduct", "anti_ground_current"]},
	})


static func burst_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var e := a.static_charge
	a.static_charge = 0.0
	if e < 2.0:
		FireUtil.with_params(inst, {"power": 2.0, "damage": 1.0, "balance": 14.0, "knock": 6.0, "status": ""})
	else:
		FireUtil.with_params(inst, {"power": e, "damage": e * 0.55, "balance": 10.0 + e * 0.8, "knock": 3.0 + e * 0.05})
	VerbVolume.cone(w, a, inst)
	w.emit("static_burst", {"actor": a.id, "e": e})
	return true


static func grounding_tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.ACTIVE and inst.phase != ActionInst.P.CHANNEL:
		return
	for b in w.bodies:
		if b.alive and b.charge > 0.0 and b.form != Sim.Form.WAVE and b.pos.distance_to(a.pos + Vector3(0, 0.6, 0)) < b.radius + 1.0:
			b.charge = 0.0
			w.emit("discharge", {"actor": a.id, "body": b.id})


# ------------------------------------------------------------------ tech: Conductor's Hand -> Arc Link

static func _tech() -> void:
	Moves.register("conductors_hand", {
		"element": E, "sub": SUB, "slot": "tech", "name": "Conductor's Hand / Arc Link",
		"desc": "Hold on a conductor within 10 m (metal, a water body, a puddle, fog, caltrops, a rod or an embedded lance): it stores charge (E by hold tier; decays 4/s; touching it shocks). Release with aim: a bolt jumps you -> the charged body -> the nearest rival within 8 m of it. Bank it around cover.",
		"module": "kit_fire", "verb": "summon",
		"startup": _s(10), "active": 0.0, "recovery": _s(16), "cancel": 0.5,
		"cost": 8.0, "range": 10.0, "damage": 14.0, "balance": 26.0, "E": 12.0, "conduct_budget": 12.0, "max_hops": 4, "relay_range": 8.0,
		"tiers": {
			"t1": {"E": 18.0, "damage": 18.0, "charge_drain": 4.0},
			"t2": {"E": 26.0, "damage": 22.0, "balance": 34.0, "charge_drain": 6.0},
			"t3": {"E": 36.0, "damage": 28.0, "balance": 44.0, "charge_drain": 8.0},
		},
		"counter": {"cls": "lightning", "power": [12.0, 18.0, 26.0, 36.0]}, "threat": {"cls": "lightning"},
		"anim": "lightning_charge", "anim_hold": "lightning_charge", "anim_active": "lightning_release", "fx": {"mat": "lightning", "shape": "rod"},
		"ai": {"role": "setup", "range": [2.0, 10.0], "tags": ["relay", "around_cover", "charge_conductor"]},
	})
	CombatWorld.register_tech_preview(E, SUB, Callable(FireLightning, "hand_preview"))


static func _hand_target(w: CombatWorld, a: ActorState, dir: Vector3) -> MatBody:
	return w.find_body(a, dir, 10.0, 55.0, func(b: MatBody) -> bool:
		return b.form != Sim.Form.POOL and b.controller != a.id and (Materials.conducts(b) or (b.form == Sim.Form.PUDDLE and b.phase == Sim.Phase.LIQUID)))


static func hand_preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var b := _hand_target(w, a, dir)
	if b == null:
		return {"mode": "CHARGE", "body": -1, "ok": false, "reason": "target", "label": "CHARGE"}
	return {"mode": "CHARGE", "body": b.id, "ok": true, "reason": "", "label": "ARC LINK"}


static func hand_after(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	var b := _hand_target(w, a, w.aim_dir(a, it))
	if b == null:
		w.emit("whiff", {"actor": a.id, "move": inst.id})
		return ActionInst.P.RECOVERY
	inst.data["target"] = b.id
	charge_body(w, b, float(Charge.param(inst, "E", 12.0)), a.id)
	w.emit("telegraph", {"actor": a.id, "move": inst.id, "body": b.id, "time": 0.0})
	Verbs.fx(w, a, inst, "beam", {"path": PackedVector3Array([a.hand_point(), b.pos]), "length": a.hand_point().distance_to(b.pos), "shape": "small"})
	return ActionInst.P.CHANNEL


static func hand_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.CHANNEL:
		Verbs.on_tick(w, a, inst, it)
		return
	var b := w.get_body(int(inst.data.get("target", -1)))
	if it.tech_cancel or b == null or not b.alive or a.chest().distance_to(b.pos) > 12.0:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["aim"] = w.aim_dir(a, it)
	inst.data["aim_active"] = it.aim_active
	var e := float(Charge.param(inst, "E", 12.0))
	if b.charge < e:
		charge_body(w, b, e, a.id)
	if w.tick % 10 == 0:
		Verbs.fx(w, a, inst, "beam", {"path": PackedVector3Array([a.hand_point(), b.pos]), "length": a.hand_point().distance_to(b.pos),
			"power": b.charge, "shape": "small", "dur": 0.2})
	if not Charge.held(inst, it):
		inst.data["released"] = true
		w.set_phase(a, inst, ActionInst.P.ACTIVE)


static func hand_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.ACTIVE:
		var b := w.get_body(int(inst.data.get("target", -1)))
		if b != null and b.alive:
			var def := _def_for(inst, Vector3.ZERO)
			def["E"] = maxf(float(def.E), b.charge)
			def["relay_range"] = float(Charge.param(inst, "relay_range", 8.0))
			var out := Conduction.relay(w, a, [b], def, inst.attack_id)
			b.charge = maxf(0.0, b.charge * 0.3)
			Verbs.fx(w, a, inst, "beam", {"path": out.path, "length": 10.0, "power": float(out.get("e", def.E))})
			w.emit("arc_link", {"actor": a.id, "body": b.id, "hits": out.hits, "blocked": out.blocked})
		return
	Verbs.on_phase(w, a, inst, p)


## Charges a conductive body (or a puddle) to `e`: it becomes a live conduction node, marked by a small static field
## that rides it; the charge decays CHARGE_DECAY/s and touching it shocks.
static func charge_body(w: CombatWorld, b: MatBody, e: float, owner_id: int) -> void:
	if b == null or not b.alive or e <= 0.0:
		return
	b.charge = maxf(b.charge, minf(e, 60.0))
	var mark := int(b.props.get("charge_mark", -1))
	var z := w.get_body(mark)
	if z == null or not z.alive:
		z = w.spawn_zone(&"static_field", b.pos, b.radius + 0.4, owner_id, b.charge, Sim.Mat.AIR, 0.0, -1.0, "charge:%d" % b.id)
		z.props["follow"] = b.id
		z.props["spare_owner"] = true
		b.props["charge_mark"] = z.id
	z.power = b.charge
	z.owner = owner_id
	w.emit("charged", {"body": b.id, "e": b.charge, "by": owner_id})


## Static fields: Skybreak marks (strike after the delay), charge marks (ride their body, decay, shock on touch) and
## Storm Grid patches (shock fighters standing on them once).
static func static_field_tick(w: CombatWorld, z: MatBody, dt: float) -> void:
	if z.props.get("skybreak", false):
		if z.age >= float(z.props.get("delay", SKYBREAK_DELAY)):
			_sky_strike(w, z)
		return
	var fid := int(z.props.get("follow", -1))
	if fid >= 0:
		var b := w.get_body(fid)
		if b == null or not b.alive:
			w.close_zone(z, "carrier_gone")
			return
		z.pos = b.pos
		b.charge = maxf(0.0, b.charge - CHARGE_DECAY * dt)
		z.power = b.charge
		if b.charge <= 0.5:
			b.charge = 0.0
			b.props.erase("charge_mark")
			w.close_zone(z, "discharged")
			return
		for a in w.actors:
			if a.health <= 0.0 or a.id == z.owner or Status.immune(a, "conduct"):
				continue
			if b.controller == a.id or a.chest().distance_to(b.pos) < b.radius + 0.6 or a.pos.distance_to(b.pos) < b.radius + 0.4:
				if int(z.props.get("shock_%d" % a.id, -100)) > w.tick - 30:
					continue
				z.props["shock_%d" % a.id] = w.tick
				w.hit_actor(a, {"attacker": z.owner, "attack_id": w.new_attack_id(), "damage": b.charge * 0.3, "balance": 10.0 + b.charge * 0.3,
					"kind": "lightning", "from": b.pos, "unblockable": true})
				Status.apply(w, a, "shocked", 0.3, 1.0, z.owner)
				b.charge *= 0.5
		return
	if z.props.get("shock", false):
		for a in w.actors:
			if a.health <= 0.0 or a.id == z.owner or Status.immune(a, "conduct") or not a.grounded:
				continue
			if not w._in_zone(z, a.pos + Vector3(0, 0.3, 0), Sim.ACTOR_RADIUS) or z.props.has("shocked_%d" % a.id):
				continue
			z.props["shocked_%d" % a.id] = true
			w.hit_actor(a, {"attacker": z.owner, "attack_id": w.new_attack_id(), "damage": z.power * 0.4, "balance": 18.0,
				"kind": "lightning", "from": z.pos, "unblockable": true})
			Status.apply(w, a, "shocked", 0.4, 1.0, z.owner)


# ------------------------------------------------------------------ evade: Arc Step, Overcharge

static func _mobility() -> void:
	Moves.register("arc_step", {
		"element": E, "sub": SUB, "slot": "evade", "name": "Arc Step",
		"desc": "6 m in 0.1 s as a streak of lightning (9 i-frames); the trail shocks whoever it crossed (E 6).",
		"module": "verbs", "verb": "dash", "startup": 0.0, "active": _s(6), "recovery": _s(8),
		"cost": 6.0, "distance": 6.0, "iframes": _s(9), "dir": "stick", "E": 6.0, "damage": 6.0,
		"hook_tick": Callable(FireLightning, "arc_step_tick"),
		"anim": "air_dash", "fx": {"mat": "lightning", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 6.0], "tags": ["dash", "long", "shock_trail"]},
	})
	Moves.register("overcharge", {
		"element": E, "sub": SUB, "slot": "evade_hold", "name": "Overcharge",
		"desc": "Hold evade: +30 % move speed and faster recoveries (x0.8) - but any water that hits you shocks you.",
		"module": "kit_fire", "verb": "mode", "kind": "run", "speed_mult": 1.0, "upkeep": 12.0, "status": "overcharged",
		"startup": 0.0, "active": 0.0, "recovery": _s(6),
		"anim": "run", "fx": {"mat": "lightning", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 10.0], "tags": ["speed", "risky_vs_water"]},
	})


static func arc_step_tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.ACTIVE:
		return
	if not inst.data.has("from"):
		inst.data["from"] = a.pos
	var dur := float(Charge.param(inst, "active", inst.def.active))
	if inst.t + Sim.DT < dur - 1e-6 or inst.data.get("shocked", false):
		return
	inst.data["shocked"] = true
	var p0: Vector3 = inst.data.from
	var p1 := a.pos + Vector3(inst.data.get("dir", Vector3.ZERO)) * 0.6
	var seg := p1 - p0
	seg.y = 0.0
	var ln := maxf(seg.length(), 0.01)
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0 or Status.immune(t, "conduct"):
			continue
		var tt := clampf((t.pos - p0).dot(seg / ln), 0.0, ln)
		var q := p0 + seg / ln * tt
		if Vector2(t.pos.x - q.x, t.pos.z - q.z).length() <= Sim.ACTOR_RADIUS + 0.6:
			var v := Agent.of_volume(w, a, inst, &"lightning", q + Vector3(0, 1.0, 0), seg / ln, {"E": float(Charge.param(inst, "E", 6.0))})
			w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "damage", 6.0)), "balance": 12.0,
				"knock": Vector3(0, 0.5, 0), "kind": "lightning", "from": q, "agent": v, "power": v.power})
			Status.apply(w, t, "shocked", 0.3, 1.0, a.id)
	w.emit("lightning", {"actor": a.id, "path": PackedVector3Array([p0 + Vector3(0, 1.0, 0), a.pos + Vector3(0, 1.0, 0)]), "arcs": [],
		"blocked": false, "hits": [], "trail": true})


static func overcharge_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action != inst or inst.phase != ActionInst.P.CHANNEL:
		return
	if a.wetness > 0.5 and not inst.data.get("self_shock", false):
		inst.data["self_shock"] = true
		w.hit_actor(a, {"attacker": -1, "attack_id": w.new_attack_id(), "damage": 10.0, "balance": 30.0, "knock": Vector3.ZERO,
			"kind": "lightning", "from": a.chest(), "unblockable": true})
		Status.apply(w, a, "shocked", 0.6, 1.0, a.id)
		w.emit("overcharge_short", {"actor": a.id})
		if a.action == inst:
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
	elif w.tick % 12 == 0:
		Verbs.fx(w, a, inst, "aura", {"on": true, "shape": "small", "power": 6.0})
