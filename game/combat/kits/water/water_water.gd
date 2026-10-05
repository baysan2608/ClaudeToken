class_name WaterWater
extends RefCounted
## Water / Water (sub 0, docs/MOVESET.md §7.5). The legacy kit (lash / ice lance, draw-shape-release, the
## waterskin shield) keeps its T0/T1 behaviour exactly (act_water.gd); this file adds the tiers T2 Torrent and
## T3 Maelstrom Lash (via ActWater), the thrust Water Bullet / Pressure Jet / Cutting Jet, the ground Tidal
## Rush (a water wave that carries solids back, quenches lava, douses fire), Spray Fan, Surge Orb (push),
## Slick (sink), Riptide Step / Dive (evade) and Wave Ride (evade hold).
##
## Slot bindings (element 1, sub 0): strike water_attack (legacy), thrust water_bullet, ground tidal_rush,
## sweep spray_fan, guard guard (legacy shield), push surge_orb, sink slick, tech water_tech (legacy),
## evade riptide_step, evade_hold wave_ride.

const E := 1


static func _s(frames: float) -> float:
	return frames / 60.0


static func register() -> void:
	_extend_legacy()
	_thrust()
	_ground()
	_sweep()
	_push_sink()
	_mobility()
	Moves.bind(E, 0, "thrust", "water_bullet")
	Moves.bind(E, 0, "ground", "tidal_rush")
	Moves.bind(E, 0, "sweep", "spray_fan")
	Moves.bind(E, 0, "push", "surge_orb")
	Moves.bind(E, 0, "sink", "slick")
	Moves.bind(E, 0, "evade", "riptide_step")
	Moves.bind(E, 0, "evade_hold", "wave_ride")
	KitWater.handle("water_bullet", {"tick": Callable(WaterWater, "bullet_tick"), "phase": Callable(WaterWater, "bullet_phase"),
		"interrupt": Callable(WaterWater, "bullet_interrupt")})
	KitWater.handle("slick", {"start": Callable(WaterWater, "slick_start"), "after": Callable(WaterWater, "after_active")})
	KitWater.handle("riptide_step", {"start": Callable(WaterWater, "riptide_start"), "after": Callable(WaterWater, "after_active"),
		"tick": Callable(WaterWater, "evade_tick"), "phase": Callable(WaterWater, "evade_phase")})
	CombatWorld.register_zone_effect(&"slick", Callable(WaterWater, "slick_zone"))
	CombatWorld.register_body_tick(&"water_wave", Callable(WaterWater, "wave_tick"))
	CombatWorld.register_tech_preview(E, 0, Callable(WaterWater, "tech_preview"))


## The legacy strike gains T2 Torrent and T3 Maelstrom Lash. The base values stay untouched (tests pin
## them); the added `tiers` data only matters for holds of 1.0 s and longer (T1 still fires at 0.45 s and
## does not drain Focus: charge_drain 0 on t1, exactly like today).
static func _extend_legacy() -> void:
	var d: Dictionary = Moves.DEFS["water_attack"]
	d["name"] = "Lash / Ice Lance / Torrent / Maelstrom"
	d["desc"] = "Tap: wetting lash. Hold: ice lance (T1). Hold longer: Torrent, a 10 kg water slug (T2), then Maelstrom Lash, a 360 degree whip (T3)."
	d["element"] = E
	d["sub"] = 0
	d["slot"] = "strike"
	d["chain"] = 0.25
	d["tiers"] = {
		"t1": {"charge_drain": 0.0},
		"t2": {"charge_drain": 8.0, "torrent_kg": 10.0, "torrent_speed": 20.0, "torrent_damage": 16.0, "torrent_balance": 34.0,
			"torrent_knock": 6.0, "cost_add": 5.0},
		"t3": {"charge_drain": 8.0, "maelstrom_range": 5.0, "maelstrom_power": 18.0, "maelstrom_damage": 18.0,
			"maelstrom_balance": 40.0, "maelstrom_knock": 8.0, "maelstrom_kg": 2.0, "cost_add": 5.0},
	}
	d["counter"] = {"cls": "water_jet", "power": [8.0, 5.7, 16.0, 18.0]}
	d["threat"] = {"cls": "water"}
	d["fx"] = {"mat": "water", "shape": ""}
	d["ai"] = {"role": "poke", "range": [1.0, 6.0], "tags": ["wet", "deflect_light"]}
	d["anim_t2"] = "water_whip"
	d["anim_t3"] = "water_whip"
	var t: Dictionary = Moves.DEFS["water_tech"]
	t["name"] = "Draw & Shape"
	t["desc"] = "Hold: draw water from the pool, a puddle, vapour (condense it) or an enemy stream in flight; drag to aim; release to fire. Attack while holding: freeze it into an ice block."
	t["element"] = E
	t["sub"] = 0
	t["slot"] = "tech"
	t["fx"] = {"mat": "water", "shape": ""}
	t["counter"] = {"cls": "grip_water"}
	t["ai"] = {"role": "counter", "range": [0.0, 7.5], "tags": ["reclaim", "condense", "freeze"]}
	var g: Dictionary = Moves.DEFS["guard"]
	if not g.has("name"):
		g["name"] = "Guard"


# ------------------------------------------------------------------ thrust: Water Bullet / Pressure Jet / Cutting Jet

static func _thrust() -> void:
	Moves.register("water_bullet", {
		"element": E, "sub": 0, "slot": "thrust", "name": "Water Bullet",
		"desc": "A fast water slug. Held: a triple volley, then the Pressure Jet (a jet still connected to you: lightning on it comes back to you), then the Cutting Jet.",
		"module": "kit_water", "verb": "projectile",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "source": "waterskin", "mat": "water", "mass": 1.5, "speed": 30.0, "gravity": 0.25, "tag": "slug",
		"damage": 6.0, "balance": 10.0, "reach": 16.0,
		"tiers": {
			"t1": {"count": 3, "spread": 9.0, "cost_add": 2.0, "damage": 5.0},
			"t2": {"jet_kg": 3.0, "jet_t": 0.8, "jet_range": 10.0, "jet_width": 0.5, "jet_power": 10.0, "jet_dmg": 2.5,
				"jet_knock": 3.0, "jet_balance": 4.0, "cost_add": 3.0},
			"t3": {"jet_kg": 3.0, "jet_t": 1.0, "jet_range": 10.0, "jet_width": 0.25, "jet_power": 16.0, "jet_dmg": 4.0,
				"jet_knock": 2.0, "jet_balance": 6.0, "cut": true, "cost_add": 4.0},
		},
		"hook_execute": Callable(WaterWater, "bullet_execute"),
		"counter": {"cls": "water_jet", "power": [2.3, 6.9, 10.0, 16.0]}, "threat": {"cls": "water"},
		"anim": "water_whip", "fx": {"mat": "water", "shape": "needles"},
		"ai": {"role": "poke", "range": [3.0, 14.0], "tags": ["wet", "quench", "conducts_back"]},
	})


static func bullet_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	if inst.tier() < 2:
		return false           # T0 / T1: the generic projectile verb (slug / triple)
	return WaterJet.start(w, a, inst)


static func bullet_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action == inst:
		WaterJet.tick(w, a, inst)


static func bullet_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		WaterJet.end(w, a, inst)
	Verbs.on_phase(w, a, inst, p)


static func bullet_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	WaterJet.end(w, a, inst)
	Verbs.on_interrupt(w, a, inst, reason)


# ------------------------------------------------------------------ ground: Tidal Rush

static func _ground() -> void:
	Moves.register("tidal_rush", {
		"element": E, "sub": 0, "slot": "ground", "name": "Tidal Rush",
		"desc": "A ground wave of water. It carries loose or incoming solids back at the rival, quenches lava into rock, douses fire fields and leaves puddles. Held: bigger, a Breaker, then the Deluge from the pool.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(16), "active": _s(6), "recovery": _s(22), "cancel": 0.6, "chain": 0.25,
		"cost": 8.0, "source": "none", "mat": "water", "mass": 8.0, "tag": "water_wave", "speed": 9.0, "budget": 10.0,
		"width": 2.0, "power": 18.0, "channel": "K", "damage": 12.0, "balance": 40.0, "knock": 6.0, "lift": 3.0,
		"take_reach": 3.0, "kind": "water", "steer": 0.0,
		"tiers": {
			"t1": {"mass": 10.0, "budget": 12.0, "power": 24.0, "width": 2.4, "cost_add": 3.0, "damage": 14.0, "balance": 44.0},
			"t2": {"mass": 14.0, "budget": 12.0, "power": 32.0, "width": 3.2, "knock": 7.0, "cost_add": 5.0, "damage": 16.0, "balance": 48.0, "tall": true},
			"t3": {"mass": 22.0, "budget": 14.0, "power": 45.0, "width": 4.0, "speed": 8.5, "knock": 8.0, "lift": 4.0, "take_reach": 6.0,
				"cost_add": 7.0, "damage": 20.0, "balance": 55.0},
		},
		"hook_execute": Callable(WaterWater, "tidal_execute"),
		"counter": {"cls": "wave_water", "power": [18.0, 24.0, 32.0, 45.0]}, "threat": {"cls": "water_wave"},
		"anim": "water_whip", "anim_active": "water_whip", "fx": {"mat": "water", "shape": "ground"},
		"ai": {"role": "counter", "range": [3.0, 12.0], "tags": ["carry_back", "quench", "douse", "knockdown"]},
	})


static func tidal_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var want := float(Charge.param(inst, "mass", 8.0))
	var reach := float(Charge.param(inst, "take_reach", 3.0))
	var got := WaterUtil.take(w, a, want, reach)
	if got < 3.0:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return true
	VerbGroundLine.launch(w, a, inst, {"source": "none", "mass": got, "mat": "water"})
	return true


## Body tick of water waves (tag water_wave): a wave meeting an enemy wave of another material resolves it
## through the rules (lava quenched, sand turned to mud ...) whichever wave the core's clash pass lists first,
## and drowns tornado zones it runs into.
static func wave_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.form != Sim.Form.WAVE or b.attack_id == 0:
		return false
	for o in w.bodies:
		if o == b or not o.alive or o.attack_id == 0 or o.attack_owner == b.attack_owner:
			continue
		var close := false
		if o.form == Sim.Form.WAVE:
			if o.mat == b.mat and o.tag == b.tag:
				continue   # water vs water: the core clash merges them
			close = Vector2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() <= (o.wave_width + b.wave_width) * 0.5
		if not close:
			continue
		var ctr := Agent.of_body(w, b)
		ctr.actor = w.get_actor(b.attack_owner)
		Interactions.resolve(w, Agent.of_body(w, o), ctr, {"site": "clash"}, Interactions.CLASH_RULE)
		if not b.alive or b.form != Sim.Form.WAVE:
			return false
	for z in w.bodies:
		if z.alive and z.form == Sim.Form.ZONE and z.owner != b.attack_owner and (z.tag == &"tornado" or z.tag == &"eddy") \
				and Vector2(z.pos.x - b.pos.x, z.pos.z - b.pos.z).length() <= z.zone_radius + b.wave_width * 0.5:
			var key := "%d|%d|drown" % [b.id, z.id]
			if w.tick - int(w._zone_pairs.get(key, -100000)) < 6:
				continue
			w._zone_pairs[key] = w.tick
			var ctr2 := Agent.of_body(w, b)
			ctr2.actor = w.get_actor(b.attack_owner)
			Interactions.resolve(w, Agent.of_body(w, z), ctr2, {"site": "wave", "continuous": true}, Interactions.PASS_RULE)
			if not b.alive or b.form != Sim.Form.WAVE:
				return false
	return false


# ------------------------------------------------------------------ sweep: Spray Fan

static func _sweep() -> void:
	Moves.register("spray_fan", {
		"element": E, "sub": 0, "slot": "sweep", "name": "Spray Fan",
		"desc": "A fan of droplets: wets the rival (lightning x1.5), douses embers, cools hot rock, turns sand to mud. The Maelstrom tier leaves a mist cloud.",
		"module": "verbs", "verb": "cone",
		"startup": _s(10), "active": _s(6), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "cls": "spray", "channel": "P", "range": 5.0, "angle": 45.0, "power": 4.0, "damage": 3.0, "balance": 8.0,
		"knock": 2.0, "lift": 0.5, "wet": true, "spray_kg": 1.0,
		"tiers": {
			"t1": {"power": 6.0, "range": 5.5, "cost_add": 2.0},
			"t2": {"power": 9.0, "range": 6.0, "damage": 4.0, "cost_add": 3.0},
			"t3": {"power": 12.0, "range": 6.5, "damage": 5.0, "spray_kg": 2.0, "mist": true, "cost_add": 4.0},
		},
		"hook_execute": Callable(WaterWater, "spray_execute"),
		"counter": {"cls": "spray", "power": [4.0, 6.0, 9.0, 12.0]}, "threat": {"cls": "water"},
		"anim": "water_whip", "fx": {"mat": "water", "shape": "fan"},
		"ai": {"role": "setup", "range": [1.5, 6.0], "tags": ["wet", "douse", "mud", "cool"]},
	})


static func spray_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var kg := float(Charge.param(inst, "spray_kg", 1.0))
	var got := WaterUtil.take(w, a, kg)
	if got < 0.5:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return true
	VerbVolume.cone(w, a, inst)
	var dir: Vector3 = inst.data.get("face", a.forward())
	var rng_m := float(Charge.param(inst, "range", 5.0))
	var land := WaterUtil.ground_at(w, a.pos + dir * rng_m * 0.6)
	var rest := got
	if bool(Charge.param(inst, "mist", false)):
		var mist_kg := minf(0.6, rest * 0.3)
		rest -= mist_kg
		var z := WaterUtil.zone(w, "mist", land + Vector3(0, 0.1, 0), 2.2, a.id, 3.0,
			{"actor_status": "concealed", "status_t": 0.4, "spare_owner": false, "height": 2.2}, Sim.Mat.STEAM, mist_kg)
		z.tier = inst.tier()
		z.sub = inst.sub
	WaterUtil.make_puddle(w, rest, land)
	return true


# ------------------------------------------------------------------ push / sink: Surge Orb, Slick

static func _push_sink() -> void:
	Moves.register("surge_orb", {
		"element": E, "sub": 0, "slot": "push", "name": "Surge Orb",
		"desc": "Guard flick up: hurl the water shield as an orb that bursts into a wetting splash and knocks back.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(8), "active": _s(4), "recovery": _s(16), "cancel": 0.6,
		"cost": 4.0, "source": "held", "mat": "water", "speed": 14.0, "gravity": 0.5, "tag": "orb", "damage": 6.0, "balance": 20.0,
		"on_impact": "burst", "impact_radius": 2.0, "impact_power": 5.0,
		"hook_execute": Callable(WaterWater, "orb_execute"), "hook_impact": Callable(WaterWater, "orb_impact"),
		"counter": {"cls": "water_jet", "power": 5.0}, "threat": {"cls": "water"},
		"anim": "water_whip", "fx": {"mat": "water", "shape": "small"},
		"ai": {"role": "poke", "range": [2.0, 10.0], "tags": ["wet", "knockback"]},
	})
	Moves.register("slick", {
		"element": E, "sub": 0, "slot": "sink", "name": "Slick",
		"desc": "Guard flick down: pour the shield as a 2.5 m puddle in front. Runners slip (-20 balance) and get wet. A puddle conducts lightning (careful) and freezes under Ice.",
		"module": "kit_water", "verb": "zone",
		"startup": _s(6), "active": _s(4), "recovery": _s(14), "cancel": 0.6,
		"cost": 3.0, "zone_life": 5.0, "zone_radius": 1.25, "slip": 20.0,
		"threat": {"cls": "puddle"}, "anim": "water_draw", "fx": {"mat": "water", "shape": "down", "cast": "splash"},
		"ai": {"role": "setup", "range": [1.5, 4.0], "tags": ["slip", "conductor", "wet"]},
	})


static func orb_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	if w.held(a) != null or w.get_body(int(inst.data.get("held", -1))) != null:
		return false   # throw the shield itself
	var got := WaterUtil.take(w, a, 2.0)
	if got < 0.8:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return true
	var b := w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, got, a.hand_point(), "waterskin:%d" % a.id)
	w.take_control(a, b, 0.9, "orb")
	return false


static func orb_impact(w: CombatWorld, b: MatBody, _what: String) -> bool:
	var owner := w.get_actor(b.attack_owner)
	var r := 2.0
	FxEvents.fx(w, "burst", "water", {"actor": b.attack_owner, "pos": b.pos, "radius": r, "power": 5.0, "move": "surge_orb", "tier": b.tier, "body": b.id})
	for t in w.actors:
		if t.health <= 0.0 or (owner != null and (t == owner or t.team == owner.team)):
			continue
		if t.chest().distance_to(b.pos) > r + Sim.ACTOR_RADIUS:
			continue
		var kd := (t.pos - b.pos)
		kd.y = 0.0
		kd = kd.normalized() if kd.length() > 0.05 else Vector3.FORWARD
		var res := w.hit_actor(t, {"attacker": b.attack_owner, "attack_id": b.attack_id + 100000 + t.id, "damage": 5.0, "balance": 14.0,
			"knock": kd * 5.0 + Vector3(0, 1.0, 0), "kind": "water", "from": b.pos, "power": 5.0, "mat": "water", "tier": b.tier})
		if res == "hit" or res == "knockdown" or res == "block":
			t.wetness = 1.0
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.form = Sim.Form.STREAM
	w._water_to_puddle(b)
	return true


static func after_active(_w: CombatWorld, _a: ActorState, _inst: ActionInst, _it: ActorIntent) -> int:
	return ActionInst.P.ACTIVE


static func slick_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	inst.data["face"] = w.aim_dir(a, it)
	if not w.spend_focus(a, float(inst.def.cost)):
		inst.data["fizzle"] = true
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		return
	var dir: Vector3 = inst.data.face
	var p := WaterUtil.ground_at(w, a.pos + dir * 2.4)
	var kg := 0.0
	var held := w.held(a)
	if held == null:
		held = w.get_body(int(inst.data.get("held", -1)))
	if held != null and held.alive and held.is_water() and held.phase == Sim.Phase.LIQUID:
		kg = held.mass
		if a.held_body == held.id:
			a.held_body = -1
		held.controller = -1
		held.vel = Vector3.ZERO
		held.pos = p + Vector3(0, 0.3, 0)
		held.form = Sim.Form.STREAM
		w._water_to_puddle(held)
	else:
		kg = WaterUtil.take(w, a, 2.0)
		if kg < 0.5:
			WaterUtil.give_back(w, a, kg, a.pos)
			w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
			inst.data["fizzle"] = true
			return
		WaterUtil.make_puddle(w, kg, p)
	var z := WaterUtil.zone(w, "slick", p + Vector3(0, 0.05, 0), float(inst.def.zone_radius), a.id, float(inst.def.zone_life),
		{"slip": float(inst.def.slip), "height": 0.8, "w_kind": "slick"})
	z.tier = inst.tier()
	z.sub = inst.sub
	Verbs.fx(w, a, inst, "cast", {"pos": p, "radius": 1.25, "body": z.id})
	inst.data["zone"] = z.id


## Slick zone: fighters (not the owner) standing in it slide (slick status), get wet and slip when running.
static func slick_zone(w: CombatWorld, z: MatBody, _dt: float) -> void:
	if not z.props.has("w_kind"):
		return
	for t in w.actors_in_zone(z):
		if t.id == z.owner or not t.grounded:
			continue
		Status.apply(w, t, "slick", 0.25, 1.0, z.owner)
		t.wetness = maxf(t.wetness, 0.6)
		var spd := Vector2(t.vel.x, t.vel.z).length()
		var key := "slip%d" % t.id
		if spd > 3.0 and w.tick - int(z.props.get(key, -1000)) > 90:
			z.props[key] = w.tick
			t.balance = maxf(0.0, t.balance - float(z.props.get("slip", 20.0)))
			t.balance_idle = 0.0
			w.emit("slip", {"actor": t.id, "zone": z.id})
			if t.balance <= 0.0:
				w._stagger(t, "knockdown", 1.1, {})
				t.balance = 45.0


# ------------------------------------------------------------------ evades: Riptide Step / Dive, Wave Ride

static func _mobility() -> void:
	Moves.register("riptide_step", {
		"element": E, "sub": 0, "slot": "evade", "name": "Riptide Step",
		"desc": "A slide on a film of water (leaves a slippery trail). In the pool: Dive - submerge and resurface 4 m away.",
		"module": "kit_water", "verb": "dash",
		"startup": 0.0, "active": _s(16), "recovery": _s(8), "cancel": 0.5, "cost": 4.0, "distance": 4.0, "iframes": _s(9),
		"dive_distance": 4.0, "dive_iframes": _s(18),
		"anim": "evade", "fx": {"mat": "water", "shape": ""}, "threat": {"cls": "puddle"},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["slide", "dive"]},
	})
	Moves.register("wave_ride", {
		"element": E, "sub": 0, "slot": "evade_hold", "name": "Wave Ride",
		"desc": "Hold evade: surf a self-made wave at 8 m/s for as long as the water lasts (2 kg/s).",
		"module": "verbs", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(8), "cost": 0.0, "kind": "surf", "speed_mult": 1.45, "status": "surfing",
		"ride_kg": 2.0, "upkeep": 0.0,
		"hook_tick": Callable(WaterWater, "ride_tick"),
		"anim": "glide", "fx": {"mat": "water", "shape": ""},
		"ai": {"role": "mobility", "range": [0.0, 12.0], "tags": ["surf", "approach"]},
	})


static func riptide_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var dive := a.in_water
	var spec := {"dist": float(inst.def.get("dive_distance" if dive else "distance", 4.0)),
		"iframes": float(inst.def.get("dive_iframes" if dive else "iframes", 0.15)), "cost": float(inst.def.cost),
		"hidden": dive, "active": float(inst.def.active)}
	WaterUtil.evade_start(w, a, inst, it, spec)
	inst.data["dive"] = dive
	if dive:
		w.emit("dive", {"actor": a.id, "on": true})
		Verbs.fx(w, a, inst, "splash", {"pos": a.pos, "radius": 1.0})
	else:
		# The water film: a thin slippery patch behind the step (spares the owner).
		var z := WaterUtil.zone(w, "slick", a.pos - Vector3.ZERO, 0.8, a.id, 1.4, {"slip": 10.0, "height": 0.6, "w_kind": "slick"})
		z.tier = 0
		Verbs.fx(w, a, inst, "trail", {"dir": inst.data.dir, "length": float(spec.dist)})


static func evade_tick(_w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	WaterUtil.evade_tick(_w, a, inst)


static func evade_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		WaterUtil.evade_end(inst)
		if inst.data.get("dive", false):
			w.emit("dive", {"actor": a.id, "on": false})
			Verbs.fx(w, a, inst, "splash", {"pos": a.pos, "radius": 1.0})


## Wave Ride upkeep: 2 kg of water per second leaves the waterskin / pool and falls behind as puddles.
static func ride_tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.CHANNEL:
		return
	a.wetness = 1.0
	var acc := float(inst.data.get("ride_acc", 0.0)) + float(Charge.param(inst, "ride_kg", 2.0)) * Sim.DT
	if acc >= 0.5:
		var got := WaterUtil.take(w, a, acc)
		inst.data["ride_acc"] = 0.0
		if got < acc * 0.9:
			WaterUtil.make_puddle(w, got, a.pos)
			w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
			return
		WaterUtil.make_puddle(w, got, a.pos - a.vel.normalized() * 0.8 if a.vel.length() > 0.5 else a.pos)
	else:
		inst.data["ride_acc"] = acc


# ------------------------------------------------------------------ technique preview (HUD / AI)

static func tech_preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var held := w.held(a)
	if held != null and held.is_water():
		return {"mode": "SHAPE", "body": held.id, "ok": true, "reason": ""}
	var reach := float(Moves.DEFS.water_tech.reach)
	var vap := w.find_body(a, dir, reach, 70.0, Callable(ActWater, "vapor_filter"))
	if vap != null:
		return {"mode": "CONDENSE", "body": vap.id, "ok": true, "reason": ""}
	var en := w.find_body(a, dir, reach, 70.0, func(b: MatBody) -> bool: return ActWater.enemy_water(b, a))
	if en != null:
		return {"mode": "SEIZE", "body": en.id, "ok": true, "reason": ""}
	if WaterUtil.available(w, a, reach) >= 1.0:
		return {"mode": "DRAW", "body": -1, "ok": true, "reason": ""}
	return {"mode": "DRAW", "body": -1, "ok": false, "reason": "water"}
