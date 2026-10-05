class_name FireBlue
extends RefCounted
## Fire / Blue (sub 1, docs/MOVESET.md §7.10): concentrated heat. Blue costs 1.5x the heat of Flame for a tier and
## counts x1.5 in the counter rule (its power table); it melts and fuses rather than spreads.
##   strike  Blue Needle -> Blue Lance -> Searing Beam (T2, 0.6 s: melts a flying 20 kg stone) -> White Core (T3,
##           1.0 s: melts through a stone wall, the face slumps in about 1 s; earth's cells fuse sand walls to glass)
##   thrust  Comet Flame (compact blue fireball @26, piercing at T3)
##   ground  Blue Furrow -> Magma Rift (melts the ground into a lava channel: ground_taken + paid heat)
##   sweep   Corona (blue ring around you: melts ice / small metal, burns vines)
##   guard   Blue Aegis (CP 16: ice / metal <= 8 kg melted, flames absorbed x1.5 perfect)
##   push    Flash Over · sink Kiln (superheat a body: whoever seizes it next is burned) · tech Smelter (1.5x Scorch)
##   evade   Shimmer Step · evade_hold Afterburn

const E := 2
const SUB := 1
const PULSE := 0.1
const WALL_FACE_HU := 50.0     # White Core: heat into a wall's face per pulse (a 30 kg face slumps in ~0.9 s)


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
	for pair in [["strike", "blue_needle"], ["thrust", "comet_flame"], ["ground", "blue_furrow"], ["sweep", "corona"],
			["guard", "blue_aegis"], ["push", "flash_over"], ["sink", "kiln"], ["tech", "smelter"], ["evade", "shimmer_step"],
			["evade_hold", "afterburn"]]:
		Moves.bind(E, SUB, pair[0], pair[1])
	KitFire.handle("blue_needle", {"tick": Callable(FireBlue, "needle_tick"), "phase": Callable(FireBlue, "needle_phase"),
		"interrupt": Callable(FireBlue, "needle_interrupt")})
	CombatWorld.register_body_tick(&"magma_rift", Callable(FireBlue, "rift_tick"))
	CombatWorld.register_zone_effect(&"kiln", Callable(FireBlue, "kiln_tick"))
	CombatWorld.register_tech_preview(E, SUB, Callable(FireBlue, "smelter_preview"))
	Interactions.register_tag_class(&"corona", &"blue_fire", &"corona")
	_corona_cells()


# ------------------------------------------------------------------ strike: Blue Needle ladder

static func _strike() -> void:
	Moves.register("blue_needle", {
		"element": E, "sub": SUB, "slot": "strike", "name": "Blue Needle / Blue Lance / Searing Beam / White Core",
		"desc": "A thin blue beam (5 m). Held: an 8 m lance, then the Searing Beam (10 m, 0.6 s: melts a flying stone into a falling magma blob), then White Core (12 m, 1 s: melts through a stone wall, fuses sand walls into glass).",
		"module": "kit_fire", "verb": "beam",
		"startup": _s(8), "active": _s(6), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"heat": 60.0, "range": 5.0, "width": 0.35, "damage": 7.0, "balance": 10.0, "knock": 1.5, "lift": 0.3,
		"status": "burning", "status_t": 0.6,
		"tiers": {
			"t1": {"range": 8.0, "heat_add": 60.0, "damage": 12.0, "balance": 18.0},
			"t2": {"range": 10.0, "heat_add": 160.0, "sustain": 0.6, "sustain_hu": 200.0, "damage": 5.0, "balance": 8.0, "pierce": true},
			"t3": {"range": 12.0, "heat_add": 300.0, "sustain": 1.0, "sustain_hu": 200.0, "damage": 6.0, "balance": 10.0, "pierce": true,
				"melt_walls": true, "width": 0.5},
		},
		"counter": {"cls": "blue_fire", "power": [6.0, 12.0, 22.0, 33.0]}, "threat": {"cls": "blue_fire"},
		"anim": "fire_jab", "anim_charge": "fire_charge", "anim_active": "fire_release", "anim_t3": "mv_palm_thrust",
		"fx": {"mat": "blue", "shape": "lance"},
		"ai": {"role": "poke", "range": [0.0, 12.0], "tags": ["beam", "melt_stone_t2", "melt_wall_t3", "glass_sand"]},
	})


## The beam's release: pays the sustain heat up front (one budget for the whole beam: a stone it meets takes what it
## needs to go molten), then pulses every 0.1 s while active (T2 / T3 sustained).
static func needle_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.ACTIVE:
		var sus := float(Charge.param(inst, "sustain", 0.0))
		if sus > 0.0:
			Verbs.pay(w, a, inst, {"heat": float(Charge.param(inst, "sustain_hu", 0.0))})
			inst.data["active"] = sus
			inst.data["pulses_left"] = int(round(sus / PULSE))
		inst.data["pulse_t"] = 0.0
		inst.data["budget_total"] = float(inst.data.get("heat_paid", 0.0))
		_pulse(w, a, inst)
		return
	if p == ActionInst.P.RECOVERY:
		_end_face(w, inst)
	Verbs.on_phase(w, a, inst, p)


static func needle_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.phase == ActionInst.P.ACTIVE:
		# The sustained beam follows the aim slowly (a held beam can be dragged across a target).
		var want := w.aim_dir(a, it)
		var cur: Vector3 = inst.data.get("face", a.forward())
		inst.data["face"] = cur.slerp(want, 0.06).normalized() if want.length() > 0.1 else cur
		inst.data["pulse_t"] = float(inst.data.get("pulse_t", 0.0)) + Sim.DT
		if float(inst.data.pulse_t) >= PULSE - 1e-6 and int(inst.data.get("pulses_left", 0)) > 0:
			inst.data["pulse_t"] = 0.0
			inst.data["pulses_left"] = int(inst.data.pulses_left) - 1
			inst.attack_id = w.new_attack_id()
			_pulse(w, a, inst)
		return
	Verbs.on_tick(w, a, inst, it)


static func needle_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	_end_face(w, inst)
	Verbs.on_interrupt(w, a, inst, reason)


## A wall face the White Core heated but did not slump rejoins its wall (VerbHeat.end, mass and energy exact).
static func _end_face(w: CombatWorld, inst: ActionInst) -> void:
	if inst.data.has("face_id"):
		VerbHeat.end(w, null, inst)


## One pulse of the blue beam: a blue_fire volume carrying the beam's remaining paid heat. Barriers on the path answer
## it (arena solids stop it; White Core melts a stone wall's face until it slumps); loose bodies are met by the
## blue_fire cells (stones melt from T2); fighters are hit (pierce from T2).
static func _pulse(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var rng_m := float(Charge.param(inst, "range", 5.0))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var start := a.hand_point() + Vector3(0, 0.2, 0)
	var budget := float(inst.data.get("heat_paid", 0.0))
	inst.data["heat_paid"] = 0.0
	var v := Agent.of_volume(w, a, inst, &"blue_fire", start, dir, {"H": budget / Interactions.HU_PER_PU, "heat_hu": budget})
	v.data["knock"] = float(Charge.param(inst, "knock", 1.5))
	var end := start + dir * rng_m
	var stop_t := 1.0
	for hb in Conduction.barriers_on(w, start, end):
		var t := float(hb.t)
		if hb.body == null:
			stop_t = t
			break
		var wall: MatBody = hb.body
		if bool(Charge.param(inst, "melt_walls", false)) and wall.form == Sim.Form.WALL and wall.is_stone():
			_melt_wall(w, a, inst, v, wall)
			stop_t = t
			break
		var r := VerbVolume.meet_body(w, a, v, wall)
		if bool(r.stopped) or float(r.pass_scale) <= 0.0:
			stop_t = t
			break
	end = start.lerp(end, stop_t)
	var seg := end - start
	var seg_len := maxf(seg.length(), 0.01)
	var width := float(Charge.param(inst, "width", 0.35))
	for b: MatBody in w.bodies.duplicate():
		if not b.alive or b.static_body or b.controller == a.id or b.form == Sim.Form.WALL or b.form == Sim.Form.POOL or b.form == Sim.Form.ZONE:
			continue
		var tb := clampf((b.pos - start).dot(dir), 0.0, seg_len)
		if (start + dir * tb).distance_to(b.pos) <= width + b.radius + 0.15:
			VerbVolume.meet_body(w, a, v, b)
	var pierce := bool(Charge.param(inst, "pierce", false))
	var hits: Array = []
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0:
			continue
		var tt := clampf((t.chest() - start).dot(dir), 0.0, seg_len)
		if (start + dir * tt).distance_to(t.chest()) <= width + Sim.ACTOR_RADIUS + 0.35:
			hits.append([tt, t])
	hits.sort_custom(func(x, y): return float(x[0]) < float(y[0]) or (float(x[0]) == float(y[0]) and x[1].id < y[1].id))
	for h in hits:
		var res := w.hit_actor(h[1], VerbVolume._hit_info(a, inst, v, start, dir))
		VerbVolume._after_hit(w, a, inst, h[1], res)
		if not pierce:
			end = start + dir * float(h[0])
			break
	Verbs.fx(w, a, inst, "beam", {"length": start.distance_to(end), "path": PackedVector3Array([start, end]), "power": v.power,
		"dir": dir, "dur": PULSE * 1.5})
	# What the pulse did not use stays in the beam for the next pulse; the last pulse's rest is spent (Verbs RECOVERY).
	inst.data["heat_paid"] = maxf(0.0, v.heat)


## White Core vs a stone wall: the facing shell (25 %) is split off and heated WALL_FACE_HU per pulse; at half molten
## it slumps into a molten body on the caster's side and the rest crumbles (VerbHeat, ledgers exact).
static func _melt_wall(w: CombatWorld, a: ActorState, inst: ActionInst, v: Agent, wall: MatBody) -> void:
	var face := w.get_body(int(inst.data.get("face_id", -1)))
	if face == null or not face.alive:
		face = VerbHeat._split_face(w, a, inst, wall)
	var give := minf(WALL_FACE_HU, v.heat)
	var used := w.heat_body(face, give)
	v.heat -= used
	if w.tick % 6 == 0:
		w.emit("heating", {"actor": a.id, "body": face.id, "liquid": face.liquid, "temp": face.temp, "wall": wall.id})
	if face.liquid >= 0.5:
		VerbHeat._slump(w, a, inst, wall, face)
		inst.data.erase("face_id")


# ------------------------------------------------------------------ thrust: Comet Flame

static func _thrust() -> void:
	Moves.register("comet_flame", {
		"element": E, "sub": SUB, "slot": "thrust", "name": "Comet Flame / Twin Comets / Great Comet / Piercing Comet",
		"desc": "A compact blue fireball @26 m/s. It boils water shields fast and melts ice walls on contact. Held: faster and hotter; T3 pierces fighters.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"heat": 100.0, "source": "heat", "mat": "fire", "tag": "comet", "mass": 0.4, "speed": 26.0, "gravity": 0.05,
		"damage": 9.0, "balance": 14.0, "reach": 18.0, "life": 1.6,
		"on_impact": "burst", "impact_radius": 1.5, "impact_power": 8.0, "impact_damage": 6.0,
		"tiers": {
			"t1": {"count": 2, "spread": 10.0, "heat_add": 50.0, "impact_power": 12.0, "damage": 8.0},
			"t2": {"count": 1, "heat_add": 140.0, "speed": 30.0, "impact_radius": 2.0, "impact_power": 16.0, "damage": 14.0, "balance": 22.0},
			"t3": {"count": 1, "heat_add": 260.0, "speed": 34.0, "pierce": 2, "impact_radius": 2.2, "impact_power": 24.0, "damage": 16.0, "balance": 28.0},
		},
		"hook_impact": Callable(FireFlame, "fireball_impact"),
		"counter": {"cls": "fireball", "power": [8.0, 12.0, 16.0, 24.0]}, "threat": {"cls": "blue_fire"},
		"anim": "fire_jab", "anim_t3": "mv_palm_thrust", "fx": {"mat": "blue", "shape": "comet"},
		"ai": {"role": "poke", "range": [4.0, 18.0], "tags": ["projectile", "fast", "boil_shield", "melt_ice_wall"]},
	})


# ------------------------------------------------------------------ ground: Blue Furrow -> Magma Rift

static func _ground() -> void:
	Moves.register("blue_furrow", {
		"element": E, "sub": SUB, "slot": "ground", "name": "Blue Furrow / Deep Furrow / Lava Channel / Magma Rift",
		"desc": "Melts the ground along a line: a lava channel (1 kg/m from the ground, melted with your heat) that burns what it runs over and can be pushed by Earth / Magma Surge. Held: deeper, then a 6 m wide 12 m rift.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(16), "active": _s(8), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"heat": 200.0, "source": "ground", "mat": "stone", "tag": "magma_rift", "mass": 8.0, "speed": 10.0, "budget": 8.0,
		"width": 1.2, "damage": 12.0, "balance": 30.0, "knock": 2.5, "lift": 2.0, "kind": "lava", "steer": 10.0,
		"hit_status": "burning", "hit_status_t": 1.5,
		"tiers": {
			"t1": {"mass": 10.0, "budget": 9.0, "heat_add": 60.0, "width": 1.6, "damage": 14.0},
			"t2": {"mass": 14.0, "budget": 10.0, "heat_add": 140.0, "width": 2.4, "damage": 16.0, "balance": 40.0},
			"t3": {"mass": 24.0, "budget": 12.0, "heat_add": 320.0, "width": 6.0, "speed": 9.0, "damage": 20.0, "balance": 50.0},
		},
		"hook_execute": Callable(FireBlue, "furrow_execute"),
		"counter": {"cls": "magma_rift", "power": [10.0, 14.0, 20.0, 30.0]}, "threat": {"cls": "lava_wave"},
		"anim": "mv_ground_slap", "anim_active": "pour", "fx": {"mat": "blue", "shape": "ground"},
		"ai": {"role": "zone", "range": [3.0, 12.0], "tags": ["ground", "lava", "magma_surge_combo", "glass_sand_t2"]},
	})


## The ground melts into a lava channel: the stone comes from the ground (ground_taken), the heat is the paid heat.
static func furrow_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var heat := Verbs.take_heat(inst)
	var b := VerbGroundLine.launch(w, a, inst, {"source": "ground", "mat": "stone"})
	if b == null:
		w.ledger.spent += heat
		return true
	var used := w.heat_body(b, heat)
	w.ledger.spent += heat - used
	b.props["viscous"] = false
	b.props["own_walls_pass"] = true
	b.update_radius()
	w.emit("magma_rift", {"actor": a.id, "body": b.id, "mass": b.mass, "liquid": b.liquid, "tier": inst.tier()})
	return true


## Magma rifts resolve contact with enemy sand surges themselves (the core clash pass may list either wave first).
static func rift_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.form != Sim.Form.WAVE or b.attack_id == 0:
		return false
	for o in w.bodies:
		if o == b or not o.alive or o.form != Sim.Form.WAVE or o.attack_owner == b.attack_owner or o.mat != Sim.Mat.SAND:
			continue
		if Vector2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() > (o.wave_width + b.wave_width) * 0.5:
			continue
		if int(b.props.get("met_%d" % o.id, -1000)) > w.tick - 20:
			continue
		b.props["met_%d" % o.id] = w.tick
		var counter := Agent.of_body(w, b)
		counter.actor = w.get_actor(b.attack_owner)
		Interactions.resolve(w, Agent.of_body(w, o, counter.actor), counter, {"site": "clash"})
	return false


# ------------------------------------------------------------------ sweep: Corona

static func _sweep() -> void:
	Moves.register("corona", {
		"element": E, "sub": SUB, "slot": "sweep", "name": "Corona",
		"desc": "A blue ring around you (2.5 m, 1 s): it burns whoever is inside, melts incoming ice and small metal, burns vines. Held: wider and longer (3.5 m).",
		"module": "verbs", "verb": "zone",
		"startup": _s(10), "active": _s(16), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"heat": 120.0, "tag": "corona", "mat": "fire", "at": "self", "attach": true, "radius": 2.5, "life": 1.0, "height": 2.4,
		"actor_status": "burning", "status_t": 0.8, "status_mag": 1.5, "rate": 0.08, "damage": 8.0, "balance": 16.0,
		"tiers": {
			"t1": {"radius": 2.9, "life": 1.2, "heat_add": 60.0, "damage": 10.0},
			"t2": {"radius": 3.2, "life": 1.5, "heat_add": 130.0, "damage": 12.0},
			"t3": {"radius": 3.5, "life": 2.0, "heat_add": 240.0, "damage": 15.0, "balance": 26.0},
		},
		"hook_execute": Callable(FireBlue, "corona_execute"),
		"counter": {"cls": "corona", "power": [6.0, 8.0, 11.0, 16.0]}, "threat": {"cls": "blue_fire"},
		"anim": "mv_spin", "anim_active": "fire_charge", "fx": {"mat": "blue", "shape": "disc"},
		"ai": {"role": "counter", "range": [0.0, 3.5], "tags": ["area", "anti_ice", "anti_metal", "anti_vine", "anti_surround"]},
	})


static func corona_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var z := VerbZone.spawn(w, a, inst, {"pos": a.pos})
	z.props["blue"] = true
	z.props["spare_owner"] = true
	var r := z.zone_radius
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0 or not w._in_zone(z, t.pos + Vector3(0, 0.9, 0), Sim.ACTOR_RADIUS):
			continue
		var d := t.pos - a.pos
		d.y = 0.0
		var v := Agent.of_volume(w, a, inst, &"blue_fire", a.chest(), d.normalized(), {"H": z.heat_payload / 20.0})
		w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "damage", 8.0)),
			"balance": float(Charge.param(inst, "balance", 16.0)), "knock": d.normalized() * 3.0 + Vector3(0, 1.0, 0), "kind": "blue",
			"from": a.chest(), "agent": v, "power": v.power, "tier": inst.tier(), "mat": "blue"})
	w.emit("corona", {"actor": a.id, "body": z.id, "radius": r, "tier": inst.tier()})
	return true


## Corona zones (counter class "corona"): small ice / metal melts (from the corona's own heat), vines burn, mist boils.
static func _corona_cells() -> void:
	for t in ["ice", "metal"]:
		FireRules._cell(t, "corona", {"bands": [[0.0, "fire_aegis_melt"]], "full_at": 0.0, "mass_max": 8.0, "fallback": "pass"},
			{"move": "corona", "tier": 0, "expect": "fire_aegis_melt"})
	FireRules._cell("vine", "corona", {"bands": [[0.0, "fire_burn"]], "full_at": 0.0, "share": 0.4, "burn_kg": 2.0},
		{"move": "corona", "tier": 0, "expect": "fire_burn"})
	for t in ["mist", "water"]:
		FireRules._cell(t, "corona", {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 0.3})
	for t in ["stone", "stone_heavy", "hot_rock", "glass", "sand"]:
		FireRules._cell(t, "corona", {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.1})
	for t in ["blast", "vacuum"]:
		FireRules._cell(t, "corona", {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0})
	FireRules._cell("water_wave", "corona", {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0})


# ------------------------------------------------------------------ guard: Blue Aegis

static func _guard() -> void:
	Moves.register("blue_aegis", {
		"element": E, "sub": SUB, "slot": "guard", "name": "Blue Aegis",
		"desc": "A hot blue aura (CP 16, 12 Focus/s). Ice and metal of 8 kg or less melt before they land; stones arrive as hot rock; flames are absorbed into your reserve (x1.5 on a perfect guard).",
		"module": "kit_fire", "verb": "barrier", "barrier": "aura", "upkeep": 12.0, "startup": 0.0, "recovery": _s(6),
		"counter": {"cls": "aura_blue", "power": [16.0, 16.0, 16.0, 16.0]}, "move_channel": 0.35,
		"anim": "guard", "anim_active": "deflect", "fx": {"mat": "blue", "shape": ""},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["melt_ice", "melt_metal", "absorb_flame"]},
	})


# ------------------------------------------------------------------ push / sink: Flash Over, Kiln

static func _push_sink() -> void:
	Moves.register("flash_over", {
		"element": E, "sub": SUB, "slot": "push", "name": "Flash Over",
		"desc": "From the guard: the aura explodes outward (3 m, H 10, knock 6).",
		"module": "verbs", "verb": "burst", "at": "self",
		"startup": _s(8), "active": _s(4), "recovery": _s(16), "cancel": 0.6,
		"cost": 6.0, "heat": 80.0, "radius": 3.0, "power": 10.0, "cls": "blue_fire", "damage": 10.0, "balance": 26.0, "knock": 6.0, "lift": 2.0,
		"status": "burning", "status_t": 1.0,
		"counter": {"cls": "blue_fire", "power": 10.0}, "threat": {"cls": "blue_fire"},
		"anim": "mv_push_two_hand", "anim_active": "fire_release", "fx": {"mat": "blue", "shape": "open"},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["area", "knockback", "after_guard"]},
	})
	Moves.register("kiln", {
		"element": E, "sub": SUB, "slot": "sink", "name": "Kiln",
		"desc": "From the guard: superheat a stone, metal, sand or glass body within 2.5 m to 600 °C or more (sand fuses to glass). A trap: whoever seizes it next is burned.",
		"module": "verbs", "verb": "burst", "at": "self",
		"startup": _s(8), "active": _s(20), "recovery": _s(14), "cancel": 0.6,
		"cost": 6.0, "heat": 150.0, "range": 2.5, "target_temp": 600.0,
		"hook_execute": Callable(FireBlue, "kiln_execute"),
		"counter": {"cls": "heat_ranged"},
		"anim": "fire_charge", "anim_active": "mv_wide_draw", "fx": {"mat": "blue", "shape": "short"},
		"ai": {"role": "setup", "range": [0.0, 2.5], "tags": ["trap", "anti_reclaim", "glass_sand"]},
	})


static func kiln_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var heat := Verbs.take_heat(inst)
	var b := w.find_body(a, a.forward(), float(Charge.param(inst, "range", 2.5)) + 0.5, 180.0, func(x: MatBody) -> bool:
		return x.controller < 0 and not x.static_body and x.form != Sim.Form.ZONE and x.form != Sim.Form.POOL and x.form != Sim.Form.PUDDLE \
			and x.form != Sim.Form.WALL and (x.is_stone() or x.mat == Sim.Mat.METAL or x.mat == Sim.Mat.SAND or x.mat == Sim.Mat.GLASS))
	if b == null:
		w.ledger.spent += heat
		w.emit("whiff", {"actor": a.id, "move": inst.id})
		return true
	var src := Agent.of_volume(w, a, inst, &"blue_fire", a.chest(), Vector3.ZERO, {"H": heat / 20.0, "heat_hu": heat})
	var tt := float(Charge.param(inst, "target_temp", 600.0))
	var need := maxf(0.0, tt + 110.0 - b.temp) * b.mass * Materials.c(b.mat)
	FireUtil.transfer(w, src, b, minf(need, heat))
	w.ledger.spent += src.heat
	if b.mat == Sim.Mat.SAND:
		w.convert_mat(b, Sim.Mat.GLASS, "sand_to_glass")
		w.emit("transform", {"body": b.id, "at": b.pos, "from": "sand", "to": "glass", "why": "kiln"})
	var z := w.spawn_zone(&"kiln", b.pos, b.radius + 0.3, a.id, 0.0, Sim.Mat.AIR, 0.0, 12.0, "kiln:%d" % a.id)
	z.props["follow"] = b.id
	z.props["spare_owner"] = true
	b.props["kiln"] = a.id
	Verbs.fx(w, a, inst, "beam", {"pos": a.hand_point(), "body": b.id, "length": a.chest().distance_to(b.pos), "shape": "short",
		"path": PackedVector3Array([a.hand_point(), b.pos])})
	w.emit("kiln", {"actor": a.id, "body": b.id, "temp": b.temp})
	return true


## Kiln marker: rides its body; the first fighter other than the caster who seizes it is burned and drops it.
static func kiln_tick(w: CombatWorld, z: MatBody, _dt: float) -> void:
	var b := w.get_body(int(z.props.get("follow", -1)))
	if b == null or not b.alive:
		w.close_zone(z, "kiln_gone")
		return
	z.pos = b.pos
	if b.controller >= 0 and b.controller != z.owner:
		var h := w.get_actor(b.controller)
		if h != null:
			w.release_body(h, Vector3(0, -1, 0), false)
			w.hit_actor(h, {"attacker": z.owner, "attack_id": w.new_attack_id(), "damage": 10.0, "balance": 24.0,
				"knock": -h.forward() * 2.0 + Vector3(0, 1.0, 0), "kind": "blue", "from": b.pos, "unblockable": true, "mat": "blue"})
			Status.apply(w, h, "kiln_burn", 1.5, 1.0, z.owner)
			w.emit("kiln_burn", {"actor": h.id, "body": b.id, "by": z.owner})
			FxEvents.fx(w, "burst", "blue", {"actor": z.owner, "pos": b.pos, "radius": 1.0, "power": 8.0, "shape": "small", "body": b.id})
		b.props.erase("kiln")
		w.close_zone(z, "sprung")
		return
	if b.temp < 300.0:
		b.props.erase("kiln")
		w.close_zone(z, "cooled")


# ------------------------------------------------------------------ tech: Smelter

static func _tech() -> void:
	Moves.register("smelter", {
		"element": E, "sub": SUB, "slot": "tech", "name": "Smelter",
		"desc": "Hold: ranged heat at 6 m without a grip, 450 HU/s (1.5x Scorch). Walls slump (their face melts into lava you can send back), metal melts, sand fuses to glass, ice boils to steam.",
		"module": "verbs", "verb": "ranged_heat",
		"startup": _s(12), "active": 0.0, "recovery": _s(18), "cancel": 0.5,
		"cost": 6.0, "range": 6.0, "cone": 40.0, "rate": 450.0, "slump_fraction": 0.25, "slump_at": 0.5,
		"counter": {"cls": "heat_ranged"},
		"anim": "heat_draw", "anim_hold": "heat_draw", "fx": {"mat": "blue", "shape": "lance"},
		"ai": {"role": "counter", "range": [0.0, 6.0], "tags": ["melt_wall", "melt_metal", "glass_sand", "boil_ice"]},
	})


## Technique context (HUD label, AI): what the Smelter would heat now (a wall first, else a body it can melt).
static func smelter_preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var b := ActFire.scorch_target(w, a, dir)
	if b == null:
		b = w.find_body(a, dir, 6.0, 40.0, func(x: MatBody) -> bool:
			return x.controller != a.id and x.form != Sim.Form.POOL and x.form != Sim.Form.ZONE and x.mass >= 0.5 and not x.static_body \
				and x.mat != Sim.Mat.FIRE and x.mat != Sim.Mat.AIR and Interactions.allows(x, &"heat_ranged"))
	if b == null:
		return {"mode": "SMELT", "body": -1, "ok": false, "reason": "target", "label": "SMELT"}
	return {"mode": "SMELT", "body": b.id, "ok": true, "reason": "", "label": "SMELT"}


# ------------------------------------------------------------------ evade: Shimmer Step, Afterburn

static func _mobility() -> void:
	Moves.register("shimmer_step", {
		"element": E, "sub": SUB, "slot": "evade", "name": "Shimmer Step",
		"desc": "3 m in 0.12 s through a heat haze (7 i-frames).",
		"module": "verbs", "verb": "dash", "startup": 0.0, "active": _s(7), "recovery": _s(8),
		"cost": 5.0, "distance": 3.0, "iframes": _s(7), "dir": "stick",
		"anim": "air_dash", "fx": {"mat": "blue", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 3.0], "tags": ["dash", "fast"]},
	})
	Moves.register("afterburn", {
		"element": E, "sub": SUB, "slot": "evade_hold", "name": "Afterburn",
		"desc": "Hold evade: run at 7 m/s leaving burning blue footprints.",
		"module": "verbs", "verb": "mode", "kind": "run", "speed_mult": 1.3, "upkeep": 8.0,
		"startup": 0.0, "active": 0.0, "recovery": _s(8),
		"hook_tick": Callable(FireBlue, "afterburn_tick"),
		"anim": "run", "fx": {"mat": "blue", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 8.0], "tags": ["run", "trail_fire"]},
	})


static func afterburn_tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.CHANNEL:
		return
	var last: Vector3 = inst.data.get("last_step", a.pos)
	if not inst.data.has("last_step") or Vector2(a.pos.x - last.x, a.pos.z - last.z).length() >= 1.6:
		inst.data["last_step"] = a.pos
		var paid := w.pay_heat(a, 8.0)
		if paid > 0.0:
			var z := FireUtil.spawn_field(w, a.id, a.pos, 0.55, 1.2, paid, true)
			z.props["dps"] = 3.0
