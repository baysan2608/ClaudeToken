class_name WaterMist
extends RefCounted
## Water / Mist (sub 2, docs/MOVESET.md §7.7): vapour. Steam = water + heat: the heat is paid like Fire
## (pay_heat, booked `generated`), the water leaves the waterskin / pool, and what vaporises is booked in the
## vapor ledgers by CombatWorld.heat_body. Fog = water dispersed as droplets: a fog ZONE (MatBody mat STEAM, tag
## fog, mass booked from the waterskin) that conceals, dampens fire and conducts lightning at 60 %.
##
## strike Scald Puff -> Steam Jet -> Geyser -> Boiling Pillars      thrust Fog Lance
## ground Creeping Fog                                              sweep  Veil
## guard  Steam Screen (+ Condense, the perfect)                     push   Steam Blast
## sink   Dew Fall                                                  tech   Vapor Draw (+ Condense)
## evade  Mist Step                                                 hold   Fog Walk

const E := 1
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
	_mobility()
	for pair in [["strike", "scald_puff"], ["thrust", "fog_lance"], ["ground", "creeping_fog"], ["sweep", "veil"],
			["guard", "steam_screen"], ["push", "steam_blast"], ["sink", "dew_fall"], ["tech", "vapor_draw"],
			["evade", "mist_step"], ["evade_hold", "fog_walk"]]:
		Moves.bind(E, SUB, pair[0], pair[1])
	KitWater.handle("vapor_draw", {"tick": Callable(WaterMist, "vapor_tick")})
	KitWater.handle("mist_step", {"start": Callable(WaterMist, "step_start"), "after": Callable(WaterWater, "after_active"),
		"tick": Callable(WaterWater, "evade_tick"), "phase": Callable(WaterMist, "step_phase")})
	KitWater.handle("fog_walk", {"after": Callable(WaterMist, "walk_after"), "phase": Callable(WaterMist, "walk_phase"),
		"interrupt": Callable(WaterMist, "walk_interrupt")})
	CombatWorld.register_body_tick(&"geyser", Callable(WaterMist, "geyser_tick"))


# ------------------------------------------------------------------ strike: Scald Puff / Steam Jet / Geyser / Boiling Pillars

static func _strike() -> void:
	Moves.register("scald_puff", {
		"element": E, "sub": SUB, "slot": "strike", "name": "Scald Puff",
		"desc": "A puff of scalding steam (0.5 kg of water boiled by 40 HU). Held: Steam Jet, then a Geyser that erupts under the target and launches it, then three Boiling Pillars in a line.",
		"module": "verbs", "verb": "cone",
		"startup": _s(8), "active": _s(6), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "heat": 40.0, "cls": "steam", "channel": "H", "range": 3.0, "angle": 20.0, "power": 3.0, "damage": 4.0, "balance": 8.0,
		"knock": 1.5, "lift": 0.4, "status": "scalded", "status_t": 1.5, "steam_kg": 0.5, "obscure": 0.5, "pillars": 0,
		"tiers": {
			"t1": {"range": 5.0, "angle": 24.0, "power": 6.0, "damage": 7.0, "knock": 4.0, "steam_kg": 0.8, "cost_add": 2.0, "heat_add": 40.0},
			"t2": {"power": 12.0, "damage": 14.0, "knock": 4.0, "lift": 8.0, "steam_kg": 1.5, "pillars": 1, "radius": 1.6, "cost_add": 4.0,
				"heat_add": 80.0, "fuse": 0.5, "range": 8.0},
			"t3": {"power": 12.0, "damage": 12.0, "pillars": 3, "steam_kg": 1.2, "cost_add": 7.0, "heat_add": 160.0, "fuse": 0.45, "range": 9.0},
		},
		"hook_execute": Callable(WaterMist, "puff_execute"),
		"counter": {"cls": "steam", "power": [3.0, 6.0, 12.0, 20.0]}, "threat": {"cls": "steam"},
		"anim": "fire_jab", "anim_t2": "water_whip", "fx": {"mat": "steam", "shape": ""},
		"ai": {"role": "poke", "range": [1.0, 9.0], "tags": ["scald", "obscure", "launch"]},
	})


static func puff_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var pillars := int(Charge.param(inst, "pillars", 0))
	if pillars > 0:
		return _geysers(w, a, inst, pillars)
	var dir := WaterUtil.aim_flat(w, a, inst)
	var rng_m := float(Charge.param(inst, "range", 3.0))
	var p := WaterUtil.ground_at(w, a.pos + dir * rng_m * 0.55) + Vector3(0, 0.8, 0)
	var used := WaterUtil.make_steam(w, a, inst, float(Charge.param(inst, "steam_kg", 0.5)), p)
	if used <= 0.0:
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return true
	VerbVolume.cone(w, a, inst)
	var z := WaterUtil.zone(w, "steam", p - Vector3(0, 0.8, 0), 1.2 + 0.1 * float(inst.tier()), a.id, float(Charge.param(inst, "obscure", 0.5)) + 0.4,
		{"actor_status": "blinded", "status_t": float(Charge.param(inst, "obscure", 0.5)), "height": 2.0, "rate": 0.2}, Sim.Mat.AIR, 0.0, 3.0)
	z.tier = inst.tier()
	z.sub = SUB
	return true


## Geysers: a steam pocket of booked water and paid heat is buried at the target; after the fuse it erupts (damage,
## launch, a real steam cloud). The vapor ledger gets exactly the energy that vaporised the water.
static func _geysers(w: CombatWorld, a: ActorState, inst: ActionInst, count: int) -> bool:
	var dir := WaterUtil.aim_flat(w, a, inst)
	var rng_m := float(Charge.param(inst, "range", 8.0))
	var ap: Vector3 = inst.data.get("aim_point", a.chest() + dir * rng_m)
	var flat := Vector3(ap.x - a.pos.x, 0, ap.z - a.pos.z)
	if flat.length() > rng_m:
		flat = flat.normalized() * rng_m
	var centre := a.pos + flat
	var heat := Verbs.take_heat(inst)
	var kg := float(Charge.param(inst, "steam_kg", 1.5))
	var made := 0
	for k in count:
		var off := (float(k) - float(count - 1) * 0.5) * 2.4
		var p := WaterUtil.ground_at(w, centre + dir * off)
		var got := WaterUtil.take(w, a, kg)
		if got < 0.3:
			WaterUtil.give_back(w, a, got, a.pos)
			continue
		var z := w.spawn_zone(&"geyser", p, 1.3, a.id, float(Charge.param(inst, "power", 12.0)), Sim.Mat.STEAM, got, -1.0, "geyser:%d" % a.id)
		z.props["fuse"] = float(Charge.param(inst, "fuse", 0.5)) + 0.18 * float(k)
		z.props["lift"] = float(Charge.param(inst, "lift", 8.0))
		z.props["damage"] = float(Charge.param(inst, "damage", 14.0))
		z.props["knock"] = float(Charge.param(inst, "knock", 4.0))
		z.props["height"] = 3.0
		z.tier = inst.tier()
		z.sub = SUB
		z.heat_payload = heat / float(count)
		made += 1
		Verbs.fx(w, a, inst, "ring", {"pos": p, "radius": 1.3, "body": z.id, "dur": float(z.props.fuse)})
	if made == 0:
		w.ledger.spent += heat
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
	elif made < count:
		w.ledger.spent += heat * float(count - made) / float(count)
	return true


static func geyser_tick(w: CombatWorld, z: MatBody, _dt: float) -> bool:
	if z.form != Sim.Form.ZONE:
		return false
	if z.age < float(z.props.get("fuse", 0.5)):
		return true
	var owner := w.get_actor(z.owner)
	var kg := z.mass
	var ve := Thermal.vapor_energy(kg)
	var use := minf(z.heat_payload, ve)
	var f := use / ve if ve > 1e-9 else 0.0
	w.ledger.vapor += use
	z.heat_payload -= use
	z.mass = 0.0
	var steam := kg * f
	if steam > 0.02:
		w._spawn_steam(z.pos + Vector3(0, 0.5, 0), steam)
	if kg - steam > 0.02:
		WaterUtil.make_puddle(w, kg - steam, z.pos)
	var rest := z.heat_payload
	z.heat_payload = 0.0
	VerbVolume.burst_at(w, owner, null, z.pos + Vector3(0, 0.9, 0), {"radius": 1.6, "power": z.power, "damage": float(z.props.get("damage", 14.0)),
		"balance": 26.0, "knock": float(z.props.get("knock", 4.0)), "lift": float(z.props.get("lift", 8.0)), "cls": "steam", "heat_hu": rest,
		"mat": "steam", "status": "scalded", "status_t": 1.5})
	FxEvents.fx(w, "erupt", "steam", {"actor": z.owner, "pos": z.pos, "radius": 1.6, "height": 4.0, "power": z.power, "move": "scald_puff", "tier": z.tier, "body": z.id})
	var sz := WaterUtil.zone(w, "steam", z.pos, 1.5, z.owner, 1.2, {"actor_status": "blinded", "status_t": 0.6, "height": 3.0, "rate": 0.2},
		Sim.Mat.AIR, 0.0, 6.0)
	sz.tier = z.tier
	w.close_zone(z, "erupted")
	return true


# ------------------------------------------------------------------ thrust: Fog Lance

static func _thrust() -> void:
	Moves.register("fog_lance", {
		"element": E, "sub": SUB, "slot": "thrust", "name": "Fog Lance",
		"desc": "A cold mist bolt, 12 m: wets, chills and blinds for 0.8 s. A cheap setup for lightning or frost; a puff of mist hangs where it ends.",
		"module": "verbs", "verb": "beam",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "cls": "frost", "channel": "P", "range": 12.0, "width": 0.7, "power": 4.0, "damage": 3.0, "balance": 8.0, "knock": 1.0, "lift": 0.2,
		"status": "chilled", "status_t": 1.5, "wet": true, "blind_t": 0.8, "mist_kg": 0.5,
		"tiers": {
			"t1": {"power": 6.0, "damage": 4.0, "blind_t": 1.0, "cost_add": 2.0},
			"t2": {"power": 8.0, "damage": 5.0, "blind_t": 1.2, "width": 1.0, "cost_add": 3.0, "mist_kg": 0.8},
			"t3": {"power": 10.0, "damage": 7.0, "blind_t": 1.5, "width": 1.4, "cost_add": 5.0, "mist_kg": 1.2, "pierce": true},
		},
		"hook_execute": Callable(WaterMist, "lance_execute"),
		"counter": {"cls": "frost", "power": [4.0, 6.0, 8.0, 10.0]}, "threat": {"cls": "frost"},
		"anim": "water_whip", "fx": {"mat": "mist", "shape": ""},
		"ai": {"role": "setup", "range": [3.0, 12.0], "tags": ["wet", "chill", "blind"]},
	})


static func lance_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var kg := float(Charge.param(inst, "mist_kg", 0.5))
	var got := WaterUtil.take(w, a, kg)
	if got < 0.2:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return true
	VerbVolume.beam(w, a, inst)
	var dir := WaterUtil.aim_flat(w, a, inst)
	var rng_m := float(Charge.param(inst, "range", 12.0))
	var end := a.hand_point() + dir * rng_m
	var blind := float(Charge.param(inst, "blind_t", 0.8))
	var hit_end := -1.0
	for t in w.actors:
		if t == a or t.team == a.team or not t.hits_taken.has(inst.attack_id):
			continue
		if t.last_result == "hit" or t.last_result == "knockdown":
			Status.apply(w, t, "blinded", blind, 1.0, a.id)
			t.wetness = 1.0
			hit_end = Vector2(t.pos.x - a.pos.x, t.pos.z - a.pos.z).length()
	if hit_end > 0.0:
		end = a.hand_point() + dir * hit_end
	var z := WaterUtil.zone(w, "mist", WaterUtil.ground_at(w, end), 1.0, a.id, 1.4, {"actor_status": "concealed", "status_t": 0.4, "spare_owner": false,
		"height": 2.0}, Sim.Mat.STEAM, got)
	z.tier = inst.tier()
	z.sub = SUB
	return true


# ------------------------------------------------------------------ ground: Creeping Fog

static func _ground() -> void:
	Moves.register("creeping_fog", {
		"element": E, "sub": SUB, "slot": "ground", "name": "Creeping Fog",
		"desc": "Mist rolls forward at 5 m/s and settles as a fog zone (r 4 m, 6 s; the Deluge-sized T3 r 6 m). Inside, lock-on breaks beyond 2 m, fire and blasts are dampened, and lightning cast into it hits everyone inside at 60%.",
		"module": "verbs", "verb": "zone",
		"startup": _s(14), "active": _s(6), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "fog_kg": 2.0, "radius": 4.0, "life": 6.0, "power": 6.0, "speed": 5.0,
		"tiers": {
			"t1": {"radius": 4.5, "power": 8.0, "cost_add": 2.0, "fog_kg": 2.5},
			"t2": {"radius": 5.0, "power": 10.0, "cost_add": 4.0, "fog_kg": 3.0, "life": 7.0},
			"t3": {"radius": 6.0, "power": 12.0, "cost_add": 6.0, "fog_kg": 4.0, "life": 8.0},
		},
		"hook_execute": Callable(WaterMist, "fog_execute"),
		"counter": {"cls": "fog", "power": [6.0, 8.0, 10.0, 12.0]}, "threat": {"cls": "mist"},
		"anim": "water_draw", "fx": {"mat": "mist", "shape": "ground", "cast": "ring"},
		"ai": {"role": "zone", "range": [3.0, 10.0], "tags": ["conceal", "dampen_fire", "conducts"]},
	})


static func fog_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var want := float(Charge.param(inst, "fog_kg", 2.0))
	var got := WaterUtil.take(w, a, want, 3.0)
	if got < 0.6:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return true
	var dir := WaterUtil.aim_flat(w, a, inst)
	var p := WaterUtil.ground_at(w, a.pos + dir * 2.0)
	var z := WaterUtil.zone(w, "fog", p, float(Charge.param(inst, "radius", 4.0)), a.id, float(Charge.param(inst, "life", 6.0)),
		{"actor_status": "concealed", "status_t": 0.4, "spare_owner": false, "height": 3.0, "rate": 0.15, "drag": 1.6}, Sim.Mat.STEAM, got,
		float(Charge.param(inst, "power", 6.0)))
	z.vel = dir * float(Charge.param(inst, "speed", 5.0))
	z.tier = inst.tier()
	z.sub = SUB
	Verbs.fx(w, a, inst, "ring", {"pos": p, "radius": z.zone_radius, "body": z.id, "dir": dir})
	return true


# ------------------------------------------------------------------ sweep: Veil

static func _sweep() -> void:
	Moves.register("veil", {
		"element": E, "sub": SUB, "slot": "sweep", "name": "Veil",
		"desc": "A mist curtain wraps around you for 3 s: hard to target (lock-on breaks beyond 2 m) and fire and blasts are dampened inside.",
		"module": "verbs", "verb": "none",
		"startup": _s(10), "active": _s(8), "recovery": _s(16), "cancel": 0.6, "chain": 0.25, "cost": 5.0, "veil_kg": 1.0, "radius": 2.2, "life": 3.0,
		"tiers": {"t1": {"life": 3.5, "cost_add": 2.0}, "t2": {"life": 4.0, "radius": 2.6, "cost_add": 4.0}, "t3": {"life": 5.0, "radius": 3.0, "cost_add": 6.0, "veil_kg": 1.5}},
		"hook_execute": Callable(WaterMist, "veil_execute"),
		"counter": {"cls": "fog", "power": [6.0, 7.0, 8.0, 9.0]}, "threat": {"cls": "mist"},
		"anim": "water_hold", "fx": {"mat": "mist", "shape": "", "cast": "ring"},
		"ai": {"role": "setup", "range": [0.0, 3.0], "tags": ["conceal", "hide"]},
	})


static func veil_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var got := WaterUtil.take(w, a, float(Charge.param(inst, "veil_kg", 1.0)))
	if got < 0.4:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return true
	var z := WaterUtil.zone(w, "mist", a.pos, float(Charge.param(inst, "radius", 2.2)), a.id, float(Charge.param(inst, "life", 3.0)),
		{"actor_status": "concealed", "status_t": 0.4, "spare_owner": false, "height": 2.6, "attach": a.id, "attach_off": Vector3.ZERO, "rate": 0.15},
		Sim.Mat.STEAM, got, 7.0)
	z.tier = inst.tier()
	z.sub = SUB
	Verbs.fx(w, a, inst, "ring", {"pos": a.pos, "radius": z.zone_radius, "body": z.id})
	return true


# ------------------------------------------------------------------ guard: Steam Screen / Condense

static func _guard() -> void:
	Moves.register("steam_screen", {
		"element": E, "sub": SUB, "slot": "guard", "name": "Steam Screen",
		"desc": "A hot steam wall around you: flames lose heat, solids slow, ice melts, walkers are scalded and sight lines break. A perfect guard (Condense) pulls water, steam and mist into your waterskin and snuffs flames.",
		"module": "verbs", "verb": "barrier", "barrier": "zone", "tag": "steam_screen", "mat": "steam", "stops_bolts": false, "radius": 1.7, "height": 2.4,
		"cost": 6.0, "heat": 60.0, "upkeep": 2.0, "move_channel": 0.4,
		"tiers": {"t1": {"radius": 1.8}, "t2": {"radius": 2.0}, "t3": {"radius": 2.2}},
		"counter": {"cls": "screen_steam", "power": [10.0, 12.0, 15.0, 18.0]}, "threat": {"cls": "steam"},
		"anim": "water_shield", "fx": {"mat": "steam", "shape": "open"},
		"ai": {"role": "counter", "range": [0.0, 8.0], "tags": ["dampen_fire", "condense", "conceal"]},
	})


# ------------------------------------------------------------------ push / sink: Steam Blast, Dew Fall

static func _push_sink() -> void:
	Moves.register("steam_blast", {
		"element": E, "sub": SUB, "slot": "push", "name": "Steam Blast",
		"desc": "Guard flick up: the screen bursts forward in a 5 m cone of steam.",
		"module": "verbs", "verb": "cone",
		"startup": _s(8), "active": _s(6), "recovery": _s(16), "cancel": 0.6, "cost": 4.0, "heat": 60.0, "cls": "steam", "channel": "P", "range": 5.0,
		"angle": 35.0, "power": 6.0, "damage": 8.0, "balance": 20.0, "knock": 5.0, "lift": 0.8, "status": "scalded", "status_t": 1.5,
		"hook_execute": Callable(WaterMist, "blast_execute"),
		"counter": {"cls": "steam", "power": 9.0}, "threat": {"cls": "steam"},
		"anim": "mv_push_two_hand", "fx": {"mat": "steam", "shape": ""},
		"ai": {"role": "poke", "range": [0.0, 5.0], "tags": ["scald", "knockback"]},
	})
	Moves.register("dew_fall", {
		"element": E, "sub": SUB, "slot": "sink", "name": "Dew Fall",
		"desc": "Guard flick down: all mist and steam within 6 m condenses into rain and falls as puddles. Clears your own fog; sets up conduction or freezing.",
		"module": "verbs", "verb": "none",
		"startup": _s(6), "active": _s(6), "recovery": _s(12), "cancel": 0.6, "cost": 3.0, "radius": 6.0,
		"hook_execute": Callable(WaterMist, "dew_execute"),
		"anim": "water_draw", "fx": {"mat": "mist", "shape": "down", "cast": "burst"}, "threat": {"cls": "water"},
		"ai": {"role": "setup", "range": [0.0, 6.0], "tags": ["clear_fog", "puddles"]},
	})


static func blast_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	for z in WaterUtil.zones_of(w, a, &"steam_screen"):
		w.close_zone(z, "burst")
	VerbVolume.cone(w, a, inst)
	var dir := WaterUtil.aim_flat(w, a, inst)
	var z2 := WaterUtil.zone(w, "steam", a.pos + dir * 2.5, 1.6, a.id, 0.8, {"actor_status": "blinded", "status_t": 0.4, "height": 2.2}, Sim.Mat.AIR, 0.0, 4.0)
	z2.tier = inst.tier()
	return true


static func dew_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var r := float(Charge.param(inst, "radius", 6.0))
	var n := 0
	var kg := 0.0
	for b in w.bodies.duplicate():
		if not b.alive or not ActWater.vapor_filter(b) or b.form == Sim.Form.POOL:
			continue
		if Vector2(b.pos.x - a.pos.x, b.pos.z - a.pos.z).length() > r + b.radius:
			continue
		kg += b.mass
		WaterUtil.rain_down(w, b, WaterUtil.ground_at(w, b.pos))
		n += 1
	Verbs.fx(w, a, inst, "burst", {"pos": a.pos + Vector3(0, 1.0, 0), "radius": r, "power": kg})
	w.emit("dew", {"actor": a.id, "bodies": n, "kg": kg})
	return true


# ------------------------------------------------------------------ tech: Vapor Draw / Condense

static func _tech() -> void:
	Moves.register("vapor_draw", {
		"element": E, "sub": SUB, "slot": "tech", "name": "Vapor Draw",
		"desc": "Hold: draw mist, fog or steam (anyone's) into a held vapour ball; release hurls it as a steam bomb (scald cloud, 2.5 m). Attack while holding: Condense it into a water blob (throw it, or switch to Ice and freeze it).",
		"module": "kit_water", "verb": "grip",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.5, "cost": 5.0,
		"ccls": "grip_vapor", "reach": 7.5, "cone": 70.0, "base": 0.85, "rip_source": "none", "speed": 16.0, "damage": 6.0, "balance": 16.0,
		"gravity": 0.2, "shape": "condense", "shape_cost": 3.0, "mode_label": "VAPOR", "draw_rate": 6.0, "max_draw": 8.0,
		"hook_impact": Callable(WaterMist, "ball_impact"),
		"counter": {"cls": "grip_vapor"}, "threat": {"cls": "steam"},
		"anim": "water_draw", "anim_hold": "water_hold", "anim_active": "water_whip", "fx": {"mat": "mist", "shape": ""},
		"ai": {"role": "counter", "range": [0.0, 7.5], "tags": ["reclaim", "condense", "steam_bomb"]},
	})


## The grip verb plus drawing from fog / mist ZONES (the generic grip only seizes loose bodies): vapour flows from the
## zone into a held STEAM ball (mass booked: the zone gives it, water_mass() counts both).
static func vapor_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action != inst or inst.phase != ActionInst.P.CHANNEL:
		return
	var held := w.held(a)
	if held != null and held.mat != Sim.Mat.STEAM:
		return
	if held != null and not held.props.has("on_impact"):
		held.props["on_impact"] = "burst"
		held.props["move"] = inst.id
	if not it.tech_held:
		return
	var reach := float(Charge.param(inst, "reach", 7.5))
	var maxm := float(Charge.param(inst, "max_draw", 8.0))
	if held != null and held.mass >= maxm - 0.01:
		return
	var zone := w.find_body(a, w.aim_dir(a, it), reach, 70.0, func(b: MatBody) -> bool: return b.form == Sim.Form.ZONE and ActWater.vapor_filter(b))
	if zone == null:
		return
	var take := minf(minf(float(Charge.param(inst, "draw_rate", 6.0)) * Sim.DT, zone.mass), maxm - (held.mass if held != null else 0.0))
	if take < 0.005:
		return
	if held == null:
		held = w.spawn_body(Sim.Mat.STEAM, Sim.Form.CLOUD, take, zone.pos, "vapor:%d" % zone.id)
		held.max_life = 6.0
		held.lineage.append(zone.id)
		w.take_control(a, held, 0.9, "draw")
		held.props["on_impact"] = "burst"
		held.props["move"] = inst.id
	else:
		held.mass += take
	zone.mass -= take
	var m0 := float(zone.props.get("mass0", zone.mass + take))
	zone.props["mass0"] = m0
	var r0 := float(zone.props.get("radius0", zone.zone_radius))
	zone.props["radius0"] = r0
	zone.zone_radius = maxf(0.5, r0 * sqrt(maxf(zone.mass, 0.0) / m0))
	zone.radius = zone.zone_radius
	if zone.mass <= 0.04:
		var rest := zone.mass
		zone.mass = 0.0
		held.mass += rest
		w.close_zone(zone, "drawn")
	if w.tick % 8 == 0:
		w.emit("draw_water", {"actor": a.id, "body": held.id, "from": zone.id, "at": zone.pos, "vapor": true})


## The steam bomb lands: a scald cloud of 2.5 m (booked: the ball's vapour stays a steam cloud).
static func ball_impact(w: CombatWorld, b: MatBody, _what: String) -> bool:
	var owner := w.get_actor(b.attack_owner)
	VerbVolume.burst_at(w, owner, null, b.pos, {"radius": 2.5, "power": 6.0, "damage": 8.0, "balance": 18.0, "knock": 3.0, "lift": 1.0,
		"cls": "steam", "mat": "steam", "status": "scalded", "status_t": 1.5})
	var z := WaterUtil.zone(w, "steam", b.pos, 2.2, b.attack_owner, 1.5, {"actor_status": "blinded", "status_t": 0.5, "height": 2.4}, Sim.Mat.AIR, 0.0, 4.0)
	z.tier = b.tier
	b.vel = Vector3.ZERO
	b.attack_id = 0
	return true


# ------------------------------------------------------------------ evades: Mist Step, Fog Walk

static func _mobility() -> void:
	Moves.register("mist_step", {
		"element": E, "sub": SUB, "slot": "evade", "name": "Mist Step",
		"desc": "Dissolve and reform 4 m away: a puff of fog at both ends, hidden while you move.",
		"module": "kit_water", "verb": "dash",
		"startup": 0.0, "active": _s(15), "recovery": _s(8), "cancel": 0.5, "cost": 5.0, "distance": 4.0, "iframes": _s(15),
		"anim": "evade", "fx": {"mat": "mist", "shape": ""}, "threat": {"cls": "mist"},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["dissolve", "conceal"]},
	})
	Moves.register("fog_walk", {
		"element": E, "sub": SUB, "slot": "evade_hold", "name": "Fog Walk",
		"desc": "Hold evade: walk inside your own mist, untargetable by lock-on beyond 2 m and hard for the AI to read; speed x0.7.",
		"module": "kit_water", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(8), "cost": 0.0, "kind": "walk", "speed_mult": 0.7, "status": "fogwalk", "upkeep": 6.0,
		"anim": "walk", "fx": {"mat": "mist", "shape": ""}, "threat": {"cls": "mist"},
		"ai": {"role": "mobility", "range": [0.0, 12.0], "tags": ["conceal", "approach"]},
	})


static func step_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	WaterUtil.evade_start(w, a, inst, it, {"dist": float(inst.def.distance), "iframes": float(inst.def.iframes), "cost": float(inst.def.cost),
		"active": float(inst.def.active), "hidden": true, "hidden_t": float(inst.def.active) + 0.2})
	var z := WaterUtil.zone(w, "mist", a.pos, 1.3, a.id, 1.0, {"actor_status": "concealed", "status_t": 0.3, "spare_owner": false, "height": 2.2}, Sim.Mat.AIR, 0.0, 5.0)
	z.sub = SUB
	Verbs.fx(w, a, inst, "burst", {"pos": a.pos + Vector3(0, 1.0, 0), "radius": 1.3})


static func step_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		WaterUtil.evade_end(inst)
		var z := WaterUtil.zone(w, "mist", a.pos, 1.3, a.id, 1.0, {"actor_status": "concealed", "status_t": 0.3, "spare_owner": false, "height": 2.2},
			Sim.Mat.AIR, 0.0, 5.0)
		z.sub = SUB
		Verbs.fx(w, a, inst, "burst", {"pos": a.pos + Vector3(0, 1.0, 0), "radius": 1.3})


static func walk_after(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	var nxt := Verbs.after_startup(w, a, inst, it)
	if nxt == ActionInst.P.CHANNEL and a.action == inst and not inst.data.has("zone_id"):
		var z := WaterUtil.zone(w, "mist", a.pos, 1.5, a.id, -1.0, {"actor_status": "concealed", "status_t": 0.4, "spare_owner": false, "height": 2.4,
			"attach": a.id, "attach_off": Vector3.ZERO, "rate": 0.15}, Sim.Mat.AIR, 0.0, 5.0)
		z.sub = SUB
		inst.data["zone_id"] = z.id
	return nxt


static func _walk_end(w: CombatWorld, inst: ActionInst) -> void:
	var z := w.get_body(int(inst.data.get("zone_id", -1)))
	inst.data.erase("zone_id")
	if z != null and z.alive:
		w.close_zone(z, "walk_end")


static func walk_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		_walk_end(w, inst)
	Verbs.on_phase(w, a, inst, p)


static func walk_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	_walk_end(w, inst)
	Verbs.on_interrupt(w, a, inst, reason)
