class_name WaterRules
extends RefCounted
## The Water column of the counter matrix (docs/MOVESET.md §8.2): the rule cells of the Water counter classes,
## the custom outcomes they need (prefix "water_"), statuses, tag classes and the body / zone hooks of fog, steam,
## ice floors and walls. Owned by the Water kit; the core owns the legacy cells (water shield x legacy threats,
## puddle / pool environment, lash tiers 0-1) which are never touched here.
##
## Counter classes encoded here: shield_water (non-legacy threats), wave_water, water_jet (tiers 2-3), spray,
## grip_water, wall_ice, freeze, rime, frost, grip_ice, screen_steam, fog, condense, grip_vapor, wall_vine,
## grip_vine, briar.
## Every heat / mass change goes through the CombatWorld ledger helpers (heat_body, boil_water, split_body,
## merge_bodies, decay_body, convert_mat, grow_plant, burn_plant) or WaterUtil's booked helpers.

## Every cell registered by the kit: "threat|counter" -> {id, ref: {...}} (docs, Lab matrix viewer, tests).
static var CELLS := {}

## Reference threats of MOVESET §8.2: threat power (PU) and mass (kg).
const REF := {
	"stone": {"tp": 17.0, "mass": 20.0}, "stone_heavy": {"tp": 31.5, "mass": 45.0}, "boulder": {"tp": 110.0, "mass": 200.0},
	"hot_rock": {"tp": 26.8, "mass": 20.0}, "magma": {"tp": 35.0, "mass": 20.0}, "lava_wave": {"tp": 27.3, "mass": 20.0},
	"metal": {"tp": 12.0, "mass": 6.0}, "sand": {"tp": 10.0, "mass": 8.0}, "sand_cloud": {"tp": 8.0, "mass": 6.0},
	"sand_surge": {"tp": 20.0, "mass": 14.0}, "water": {"tp": 9.6, "mass": 12.0}, "water_wave": {"tp": 24.0, "mass": 14.0},
	"ice": {"tp": 9.0, "mass": 6.0}, "mist": {"tp": 3.0, "mass": 2.0}, "steam": {"tp": 6.0, "mass": 1.0},
	"vine": {"tp": 14.0, "mass": 10.0}, "flame": {"tp": 8.0, "mass": 0.0}, "blue_fire": {"tp": 16.0, "mass": 0.0},
	"lightning": {"tp": 24.0, "mass": 0.0}, "blast": {"tp": 16.0, "mass": 0.0}, "gust": {"tp": 11.0, "mass": 0.0},
	"tornado": {"tp": 30.0, "mass": 0.0}, "vacuum": {"tp": 18.0, "mass": 0.0}, "sound": {"tp": 16.0, "mass": 0.0},
	"puddle": {"tp": 6.0, "mass": 3.0}, "fire_field": {"tp": 8.0, "mass": 0.0},
}

const STONES := ["stone", "stone_heavy", "boulder"]


static func _s(frames: float) -> float:
	return frames / 60.0


## One rule cell. ref (optional): {move, tier, perfect, expect, cp, tp, mass} -> the test checks that threat `tp`
## (default REF) met by the move's counter at that tier yields `expect`.
static func _cell(t: String, c: String, rule: Dictionary, ref: Dictionary = {}) -> void:
	var r := rule.duplicate(true)
	r["owner"] = "water"
	if not r.has("id"):
		r["id"] = "water_%s_%s" % [c, t]
	if Interactions.add_rule(t, c, r):
		CELLS["%s|%s%s" % [t, c, "|" + str(r.tiers) if r.has("tiers") else ""]] = {"id": r.id, "threat": t, "counter": c, "ref": ref,
			"tiers": r.get("tiers", [])}
	else:
		push_warning("WaterRules: cell %s|%s refused (legacy)" % [t, c])


static func register() -> void:
	CELLS.clear()
	_statuses()
	_tags()
	_outcomes()
	_hooks()
	_water_column()
	_ice_column()
	_mist_column()
	_plant_column()


# ================================================================ statuses, tags

static func _statuses() -> void:
	CombatWorld.register_status("fogbound", {"hidden": true, "ai_perception": 0.25})
	CombatWorld.register_status("fogwalk", {"hidden": true, "ai_perception": 0.3})
	CombatWorld.register_status("scalded", {"dps": 2.5})
	CombatWorld.register_status("surfing", {})
	CombatWorld.register_status("skating", {"friction": 0.35})
	CombatWorld.register_status("icegrip", {"friction": 8.0})      # your own ice never slides you
	CombatWorld.register_status("entangled", {"speed": 0.45})
	CombatWorld.register_status("hooked", {"speed": 0.8})
	CombatWorld.register_status("veiled", {"hidden": true, "ai_perception": 0.25})


static func _tags() -> void:
	Interactions.register_tag_class(&"rime", &"frost", &"rime")
	Interactions.register_tag_class(&"roots", &"vine", &"wave_vine")
	Interactions.register_tag_class(&"ridge", &"wall_ice", &"wall_ice")


# ================================================================ outcomes

static func _outcomes() -> void:
	Interactions.register_outcome("water_carry", Callable(WaterRules, "o_carry"))
	Interactions.register_outcome("water_ridge", Callable(WaterRules, "o_ridge"))
	Interactions.register_outcome("water_freeze", Callable(WaterRules, "o_freeze"))
	Interactions.register_outcome("water_skin", Callable(WaterRules, "o_skin"))
	Interactions.register_outcome("water_hot_block", Callable(WaterRules, "o_hot_block"))
	Interactions.register_outcome("water_dampen", Callable(WaterRules, "o_dampen"))
	Interactions.register_outcome("water_condense_in", Callable(WaterRules, "o_condense_in"))
	Interactions.register_outcome("water_brittle", Callable(WaterRules, "o_brittle"))
	Interactions.register_outcome("water_feed", Callable(WaterRules, "o_feed"))
	Interactions.register_outcome("water_drown", Callable(WaterRules, "o_drown"))
	Interactions.register_outcome("water_melt", Callable(WaterRules, "o_melt"))
	Interactions.register_outcome("water_sling", Callable(WaterRules, "o_sling"))
	Interactions.register_outcome("water_quench", Callable(WaterRules, "o_quench"))
	Interactions.register_outcome("water_burn", Callable(WaterRules, "o_burn"))


## The interaction event carries a catalogued outcome name (FxEvents.OUTCOMES) and the target material in `to`.
static func _report(res: Dictionary, outcome: String, to: String = "") -> void:
	res["outcome"] = outcome
	if to != "":
		res["to"] = to


## Tidal Rush meets a loose or incoming solid: the wave carries it (captured, harmless) toward the rival; when
## the wave touches a rival (or ends) it is thrown at them as the wave owner's attack (MOVESET "make a wave back").
static func o_carry(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	if t.body == null or c.body == null or not t.body.alive or not c.body.alive:
		return false
	var b := t.body
	var wave := c.body
	if wave.form != Sim.Form.WAVE:
		return false
	if int(b.props.get("no_carry_until", -1)) > w.tick:
		res.pass_scale = 1.0
		return true
	var spd := float(r.get("release_speed", 14.0))
	var cap := Outcomes.capture(w, t, c, res, {"max_captured": int(r.get("max_captured", 4)), "release_speed": spd}, ctx)
	if not cap:
		return false
	b.props["release_damage"] = float(r.get("release_damage", 12.0))
	b.props["carried_by"] = wave.id
	b.props["carry_from"] = b.attack_owner
	_report(res, "capture")
	return true


## A water wave becomes an ice ridge: a WALL (tag ridge) standing `stand` seconds. Same body, same mass: frozen
## through freeze_body (heat booked in freeze_dump).
static func o_ridge(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or not b.is_water() or b.form != Sim.Form.WAVE:
		return false
	make_ridge(w, b, c.actor, float(r.get("stand", 6.0)))
	res.stopped = true
	res.pass_scale = 0.0
	_report(res, "transform", "ridge")
	return true


static func make_ridge(w: CombatWorld, b: MatBody, owner: ActorState, stand: float) -> void:
	var dir := b.wave_dir if b.wave_dir.length() > 0.01 else Vector3.FORWARD
	WaterUtil.freeze_body(w, b)
	w.release_captured(b)
	b.form = Sim.Form.WALL
	b.tag = &"ridge"
	b.wall_yaw = atan2(dir.x, dir.z) + PI * 0.5
	b.wall_half = Vector3(maxf(b.wave_width * 0.5, 1.0), 0.75, 0.32)
	b.wall_rise = 0.0
	b.wall_damage = 0.0
	b.static_body = true
	b.attack_id = 0
	b.vel = Vector3.ZERO
	b.wave_path = PackedVector3Array()
	b.hardness = 0.44
	b.props["rise_time"] = 0.22
	b.props["standing"] = stand
	b.props["source"] = "moisture"
	b.max_life = -1.0
	b.age = 0.0
	b.touch(owner.id if owner != null else -1, "wall", w.tick)
	b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3)
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "wave", "to": "ridge", "why": "frozen"})
	w.emit("wall", {"actor": owner.id if owner != null else -1, "body": b.id, "tag": "ridge"})


## Freezes the threat body (water -> ice, steam -> frost) and stops it: Flash Freeze, frost walls on water.
static func o_freeze(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive:
		# A water volume (jet, lash, spray): the heat leaves and the volume stops.
		res.stopped = true
		res.pass_scale = 0.0
		_report(res, "transform", "ice")
		return t.kind == "volume"
	if not WaterUtil.freeze_any(w, b):
		return false
	_report(res, "transform", "ice")
	if b.form == Sim.Form.STREAM or b.form == Sim.Form.BLOB:
		b.form = Sim.Form.SHARD
		b.update_radius()
	b.attack_id = 0
	b.vel = Vector3(0, -1.0, 0) if bool(r.get("drop", true)) else b.vel * 0.2
	b.max_life = Sim.REMNANT_LIFETIME
	w.emit("transform", {"body": b.id, "at": b.pos, "from": String(t.cls), "to": "ice", "why": "frozen"})
	res.stopped = true
	res.pass_scale = 0.0
	if c.kind == "guard":
		res.result = w.guard_chip(c.actor, ctx.get("info", {}), {"chip": 0.0, "bal": 0.0, "knock": 0.0}, t)
	return true


## The counter's owner takes the threat's water into their waterskin (Condense, Swallow Current).
static func o_skin(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or c.actor == null:
		if t.kind == "volume":
			res.stopped = true
			res.pass_scale = 0.0
			w.ledger.spent += maxf(0.0, t.heat)   # the volume's heat leaves with it (booked)
			t.heat = 0.0
			_report(res, "absorb")
			return true
		return false
	if not (b.is_water() or b.mat == Sim.Mat.STEAM) or b.form == Sim.Form.POOL:
		return false
	WaterUtil.absorb_into_skin(w, c.actor, b)
	_report(res, "absorb")
	res.stopped = true
	res.pass_scale = 0.0
	if c.kind == "guard":
		res.result = w.guard_chip(c.actor, ctx.get("info", {}), {"chip": 0.0, "bal": 0.0, "knock": 0.0}, t)
	return true


## Hot threat meets a water / ice barrier: up to `hu` of its heat goes into the barrier (melting or boiling it,
## ledger vapor), then the barrier blocks. Volumes give `heat_share` of their heat budget.
static func o_hot_block(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var cb := c.body
	if cb != null and cb.alive and (cb.is_water() or cb.mat == Sim.Mat.PLANT):
		if t.body != null and t.body.alive:
			var hu := minf(float(r.get("hu", 150.0)), maxf(0.0, t.body.thermal_energy() - t.body.heat_payload))
			WaterUtil.transfer_heat(w, t.body, cb, hu)
		elif t.heat > 0.0:
			var used := w.heat_body(cb, t.heat * float(r.get("heat_share", 0.6)))
			t.heat -= used
			res.heat_used = float(res.heat_used) + used
	_report(res, "block")
	if t.kind == "body" and t.body != null:
		return Outcomes.block(w, t, c, res, r, ctx)
	res.stopped = true
	res.pass_scale = 0.0
	if c.kind == "guard":
		res.result = w.guard_chip(c.actor, ctx.get("info", {}), r, t)
	return true


## Fog / steam dampen a fire body or volume passing through: `k` of its heat and speed is lost (booked ambient).
static func o_dampen(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var k := float(r.get("k", 0.5))
	_report(res, "weaken")
	res.pass_scale = 1.0 - k
	res.stopped = false
	var b := t.body
	if b != null and b.alive:
		b.vel *= 1.0 - k * 0.5
		if b.heat_payload > 0.0:
			var lost := b.heat_payload * k
			b.heat_payload -= lost
			w.ledger.ambient -= lost
		elif b.thermal_energy() > 0.0 and b.mat != Sim.Mat.WATER:
			var got := -Thermal.heat(b, -b.thermal_energy() * k * 0.5)
			w.ledger.ambient -= got
	elif t.heat > 0.0:
		var lost2 := t.heat * k
		t.heat -= lost2
		w.ledger.spent += lost2
	return true


## Steam / mist met by a water shield: condenses into it (mass added, exact energy), volumes lose their heat to it.
static func o_condense_in(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	var cb := c.body
	if b != null and b.alive and cb != null and cb.alive and cb.is_water() and cb.form != Sim.Form.POOL and cb != b:
		var kg := b.mass
		var e := cb.thermal_energy()
		w.ledger.removed += b.thermal_energy()
		b.mass = 0.0
		cb.liquid = (cb.liquid * cb.mass + kg) / (cb.mass + kg)
		cb.mass += kg
		w._set_energy(cb, e)
		cb.update_radius()
		if b.form == Sim.Form.ZONE:
			w.close_zone(b, "condensed")
		else:
			w.remove_body(b, "condensed")
		res.stopped = true
		res.pass_scale = 0.0
		_report(res, "absorb")
		return true
	if b == null:
		if cb != null and cb.alive and t.heat > 0.0:
			var used := w.heat_body(cb, t.heat * float(r.get("heat_share", 0.4)))
			t.heat -= used
			res.heat_used = float(res.heat_used) + used
		res.stopped = true
		res.pass_scale = 0.0
		_report(res, "absorb")
		if c.kind == "guard":
			res.result = w.guard_chip(c.actor, ctx.get("info", {}), {"chip": 0.0, "bal": 0.0, "knock": 0.0}, t)
		return true
	return false


## Frost makes vines brittle: hardness collapses, any hit crumbles them (props.brittle).
static func o_brittle(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or b.mat != Sim.Mat.PLANT:
		return false
	b.props["brittle"] = true
	b.hardness = 0.08
	var e0 := b.thermal_energy()
	b.temp = minf(b.temp, 0.0)          # frost: the vine is cold now; the heat it lost goes to the environment (booked)
	w.ledger.freeze_dump += b.thermal_energy() - e0
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "vine", "to": "brittle", "why": "frozen"})
	_report(res, "transform", "brittle")
	res.pass_scale = 1.0 - float(r.get("slow", 0.0))
	return true


## A vine barrier drinks water: the threat's water becomes vine (booked water_to_plant), the wall gains mass.
static func o_feed(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	var cb := c.body
	if b == null or not b.alive or cb == null or not cb.alive or not b.is_water() or b.form == Sim.Form.POOL:
		if t.kind == "volume":
			res.stopped = true
			res.pass_scale = 0.0
			return true
		return false
	var take := minf(b.mass, float(r.get("max_kg", 8.0)))
	w.ledger.removed += b.thermal_energy() * take / maxf(b.mass, 1e-9)
	b.mass -= take
	w.mass_ledger.water_to_plant += take
	var e := cb.thermal_energy()
	cb.mass += take
	if cb.props.has("plant_seen"):
		cb.props["plant_seen"] = float(cb.props.plant_seen) + take       # booked right here (do not book it twice)
	w._set_energy(cb, e)
	if cb.form != Sim.Form.WALL:
		cb.update_radius()
	if b.mass <= 0.05:
		w.decay_body(b, "drunk")
	else:
		b.update_radius()
		b.vel *= 0.3
		b.attack_id = 0
	res.stopped = true
	res.pass_scale = 0.0
	_report(res, "absorb")
	if c.kind == "guard":
		res.result = w.guard_chip(c.actor, ctx.get("info", {}), {"chip": 0.0, "bal": 0.0, "knock": 0.0}, t)
	return true


## Tidal Rush "drowns" a tornado: the vortex collapses (zone closes) when the wave's water mass out-powers it.
static func o_drown(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var ok := Outcomes.neutralize(w, t, Agent.new(), res, r, ctx)
	_report(res, "neutralize")
	return ok


## Melts a frozen counter / threat by the given heat (Steam Screen on ice shards: ice becomes water).
static func o_melt(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or not b.is_water() or b.phase != Sim.Phase.FROZEN:
		return false
	var hu := float(r.get("hu", 40.0))
	var used := w.heat_body(b, hu)
	w.ledger.generated += used     # the screen's own steam heat (paid by its caster as spent heat) melts the ice
	res.heat_used = float(res.heat_used) + used
	res.pass_scale = 0.8
	_report(res, "transform", "water")
	return true


## A water body (wave, rime sheet) cools a molten / hot threat: up to cp_eff x 20 HU leave the threat and go into
## the water through heat_body (it warms, then boils: ledger vapor). A small wave cannot quench a big lava wave: the
## heat it can take is what it is made of (counter strength scales with the water mass). Sets the rock when solid.
static func o_quench(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	var cb := c.body
	if b == null or not b.alive or cb == null or not cb.alive or not cb.is_water():
		return false
	var hu := minf(float(res.cp_eff) * Interactions.HU_PER_PU * float(r.get("share", 1.0)), maxf(0.0, b.thermal_energy() - b.heat_payload))
	var used := WaterUtil.transfer_heat(w, b, cb, hu)
	res.heat_used = float(res.heat_used) + used
	var to := String(r.get("to", "rock"))
	if b.alive and b.liquid <= 1e-6 and Materials.is_fusible(b.mat):
		if to == "obsidian":
			b.tag = &"obsidian"
		_report(res, "transform", to)
	else:
		_report(res, "weaken")
	res.stopped = false
	res.pass_scale = 1.0 if b.liquid > 0.0 else 0.0
	return true


## Fire meets vine: the vine burns x3 (CombatWorld.burn_plant, ledger burned) and the fire passes on, weaker. Vine
## walls take wall damage as they go; the threat keeps the heat it did not use.
static func o_burn(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var cb := c.body
	if cb == null or not cb.alive or cb.mat != Sim.Mat.PLANT:
		return false
	var tp := float(res.tp)
	var kg := clampf(float(r.get("burn_kg", 3.0)) * (1.0 + tp / 20.0), 0.5, cb.mass)
	w.burn_plant(cb, kg)
	if cb.alive and cb.form == Sim.Form.WALL:
		cb.wall_damage_add(float(r.get("wall_damage", 0.12)))
		if cb.wall_damage >= 1.0:
			w._crumble_wall(cb)
	var b := t.body
	if b != null and b.alive:
		if b.heat_payload > 0.0:
			var lost := b.heat_payload * float(r.get("k", 0.15))
			b.heat_payload -= lost
			w.ledger.ambient -= lost
	elif t.heat > 0.0:
		var lost2 := t.heat * float(r.get("k", 0.15))
		t.heat -= lost2
		w.ledger.ambient -= lost2   # heat spent burning the lattice (booked like the body branch)
	res.pass_scale = float(r.get("pass", 0.8))
	res.stopped = false
	_report(res, "transform", "ash")
	return true


## Catch & Sling (perfect Living Lattice): the caught body is thrown back at its sender with the lattice owner's name on it.
static func o_sling(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or c.actor == null:
		return false
	var tgt := w.get_actor(b.attack_owner)
	var ca := c.actor
	var spd := maxf(Vector2(b.vel.x, b.vel.z).length() * float(r.get("speed_mult", 0.9)), float(r.get("min_speed", 12.0)))
	b.vel = Verbs.launch_vel(b.pos, tgt.chest() if tgt != null and tgt != ca else ca.chest() + ca.forward() * 8.0, spd, 0.6)
	b.attack_id = w.new_attack_id()
	b.attack_owner = ca.id
	b.hit_set.clear()
	b.hit_set[ca.id] = true
	b.residual_owner = ca.id
	b.residual_authority = Interactions.cohesion(b.tier)
	b.touch(ca.id, "sling", w.tick)
	res.stopped = true
	res.pass_scale = 0.0
	res.result = "perfect"
	_report(res, "redirect")
	w.emit("perfect_deflect", {"actor": ca.id, "body": b.id, "verb": "sling", "kind": FxEvents.mat_of(b)})
	return true


# ================================================================ hooks (zones and bodies)

static func _hooks() -> void:
	CombatWorld.register_zone_effect(&"fog", Callable(WaterRules, "fog_zone"))
	CombatWorld.register_zone_effect(&"mist", Callable(WaterRules, "fog_zone"))
	CombatWorld.register_zone_effect(&"ice_floor", Callable(WaterRules, "ice_floor_zone"))
	CombatWorld.register_zone_effect(&"steam", Callable(WaterRules, "steam_zone"))
	CombatWorld.register_zone_effect(&"steam_screen", Callable(WaterRules, "steam_zone"))
	CombatWorld.register_body_tick(&"ice", Callable(WaterIce, "wall_tick"))
	CombatWorld.register_body_tick(&"ridge", Callable(WaterIce, "wall_tick"))
	CombatWorld.register_body_tick(&"rime", Callable(WaterIce, "rime_tick"))


## Fog and mist: everyone inside is concealed (lock-on breaks beyond 2 m, AI perception +0.25 s).
static func fog_zone(w: CombatWorld, z: MatBody, _dt: float) -> void:
	if w.tick % 4 != 0:
		return
	for a in w.actors_in_zone(z):
		Status.apply(w, a, "fogbound", 0.3, 1.0, z.owner)


## Ice floors: the owner's own ice never slides them; wet fighters on it are chilled.
static func ice_floor_zone(w: CombatWorld, z: MatBody, _dt: float) -> void:
	if w.tick % 3 != 0:
		return
	for a in w.actors_in_zone(z):
		if a.id == z.owner and not z.props.get("skate", false):
			Status.apply(w, a, "icegrip", 0.12, 1.0, z.owner)


## Steam: scalds whoever stands in it (not the owner), a little each time.
static func steam_zone(w: CombatWorld, z: MatBody, _dt: float) -> void:
	if w.tick % 6 != 0:
		return
	for a in w.actors_in_zone(z):
		if a.id != z.owner and not z.props.get("harmless", false):
			Status.apply(w, a, "scalded", 0.4, 1.0, z.owner)


# ================================================================ cells: Water column

static func _water_column() -> void:
	# ---- Tidal Rush (wave_water): T0..T3 power 18 / 24 / 32 / 45
	var tr := {"move": "tidal_rush"}
	_cell("stone", "wave_water", {"outcome": "water_carry", "partial": "slow", "fail": "overwhelm", "factor": 0.6, "release_damage": 12.0},
		{"move": "tidal_rush", "tier": 0, "expect": "water_carry"})
	_cell("stone_heavy", "wave_water", {"outcome": "water_carry", "partial": "slow", "fail": "overwhelm", "factor": 0.6, "release_damage": 18.0,
		"release_speed": 12.0}, {"move": "tidal_rush", "tier": 2, "expect": "water_carry"})
	_cell("boulder", "wave_water", {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.65}, {"move": "tidal_rush", "tier": 3, "expect": "slow"})
	_cell("metal", "wave_water", {"outcome": "water_carry", "partial": "slow", "fail": "pass", "factor": 0.7}, {"move": "tidal_rush", "tier": 0,
		"expect": "water_carry"})
	_cell("hot_rock", "wave_water", {"outcome": "water_quench", "to": "rock", "eff": 2.5, "partial": "water_quench", "fail": "water_quench"},
		{"move": "tidal_rush", "tier": 0, "expect": "water_quench"})
	_cell("magma", "wave_water", {"outcome": "water_quench", "to": "rock", "eff": 2.0, "partial": "water_quench", "fail": "water_quench"},
		{"move": "tidal_rush", "tier": 0, "expect": "water_quench"})
	_cell("lava_wave", "wave_water", {"outcome": "water_quench", "to": "rock", "eff": 1.5, "partial": "water_quench", "fail": "water_quench"},
		{"move": "tidal_rush", "tier": 1, "expect": "water_quench"})
	_cell("sand", "wave_water", {"outcome": "transform", "to": "mud", "eff": 1.5, "partial": "transform", "fail": "pass"},
		{"move": "tidal_rush", "tier": 0, "expect": "transform"})
	_cell("sand_cloud", "wave_water", {"outcome": "transform", "to": "mud", "eff": 1.5, "partial": "transform", "fail": "pass"},
		{"move": "tidal_rush", "tier": 0, "expect": "transform"})
	_cell("sand_surge", "wave_water", {"outcome": "transform", "to": "mud", "eff": 1.5, "partial": "slow", "fail": "overwhelm", "factor": 0.6},
		{"move": "tidal_rush", "tier": 1, "expect": "transform"})
	_cell("water_wave", "wave_water", {"outcome": "clash", "partial": "clash", "fail": "clash"}, {"move": "tidal_rush", "tier": 1, "expect": "clash"})
	_cell("fire_field", "wave_water", {"bands": [[0.0, "extinguish"]], "full_at": 0.0, "id": "water_douse_field"},
		{"move": "tidal_rush", "tier": 0, "expect": "extinguish"})
	_cell("flame", "wave_water", {"outcome": "extinguish", "partial": "weaken", "fail": "overwhelm", "eff": 2.5},
		{"move": "tidal_rush", "tier": 0, "expect": "extinguish"})
	_cell("ember", "wave_water", {"bands": [[0.0, "extinguish"]], "full_at": 0.0})
	_cell("tornado", "wave_water", {"outcome": "water_drown", "partial": "weaken", "fail": "overwhelm"},
		{"move": "tidal_rush", "tier": 2, "expect": "water_drown"})
	_cell("vine", "wave_water", {"outcome": "capture", "partial": "slow", "fail": "overwhelm", "release_speed": 9.0},
		{"move": "tidal_rush", "tier": 0, "expect": "capture"})
	# ---- Pressure Jet / Cutting Jet / Maelstrom (water_jet, tiers 2-3; tiers 0-1 are the legacy lash)
	for t in ["stone", "metal", "ice", "glass", "sand", "hot_rock", "stone_heavy"]:
		_cell(t, "water_jet", {"tiers": [2, 3], "outcome": "deflect", "partial": "bend", "fail": "pass", "side": 0.5, "up": 1.5, "verb": "jet",
			"eff": 1.0}, {"move": "water_bullet", "tier": 3, "expect": "deflect" if t == "metal" else ""} if t == "metal" else {})
	_cell("flame", "water_jet", {"bands": [[0.0, "extinguish"]], "full_at": 0.0, "tiers": [2, 3], "id": "water_jet_douse"})
	_cell("fire_field", "water_jet", {"bands": [[0.0, "extinguish"]], "full_at": 0.0, "tiers": [2, 3], "id": "water_jet_douse_field"})
	_cell("steam", "water_jet", {"bands": [[0.0, "disperse"]], "full_at": 0.0, "tiers": [2, 3]})
	# ---- Spray Fan (spray): wets, cools, mud
	_cell("sand", "spray", {"outcome": "transform", "to": "mud", "eff": 2.0, "partial": "transform", "fail": "pass"},
		{"move": "spray_fan", "tier": 0, "expect": "transform"})
	_cell("sand_cloud", "spray", {"outcome": "transform", "to": "mud", "eff": 2.0, "partial": "transform", "fail": "pass"})
	_cell("hot_rock", "spray", {"outcome": "transform", "to": "rock", "eff": 2.5, "partial": "weaken", "fail": "pass", "hu_per_pu": 8.0},
		{"move": "spray_fan", "tier": 3, "expect": "transform"})
	_cell("hot_rock", "spray", {"tiers": [0, 1], "outcome": "transform", "to": "rock", "eff": 2.5, "partial": "weaken", "fail": "pass", "hu_per_pu": 8.0,
		"id": "water_spray_cools_rock_weak"}, {"move": "spray_fan", "tier": 1, "expect": "weaken"})
	_cell("ember", "spray", {"bands": [[0.0, "extinguish"]], "full_at": 0.0}, {"move": "spray_fan", "tier": 0, "expect": "extinguish"})
	_cell("fire_field", "spray", {"outcome": "extinguish", "partial": "weaken", "fail": "pass", "eff": 2.5},
		{"move": "spray_fan", "tier": 1, "expect": "extinguish"})
	_cell("flame", "spray", {"outcome": "extinguish", "partial": "weaken", "fail": "pass", "eff": 2.5}, {"move": "spray_fan", "tier": 0, "expect": "extinguish"})
	# ---- Draw & Shape (grip_water): the technique reclaims water, vapour and a rival's stream / wave in flight
	_cell("water", "grip_water", {"outcome": "reclaim", "bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "water_tech", "tier": 0, "expect": "reclaim"})
	_cell("water_wave", "grip_water", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "water_tech", "tier": 0, "expect": "reclaim"})
	_cell("steam", "grip_water", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "water_tech", "tier": 0, "expect": "reclaim"})
	_cell("mist", "grip_water", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "water_tech", "tier": 0, "expect": "reclaim"})
	# ---- Water shield, non-legacy threats (the legacy cells stay the core's)
	_cell("metal", "shield_water", {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.6}, {"move": "guard", "tier": 0, "expect": "slow"})
	_cell("glass", "shield_water", {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.6})
	_cell("sand", "shield_water", {"outcome": "transform", "to": "mud", "eff": 2.0, "partial": "transform", "fail": "overwhelm"})
	_cell("sand_cloud", "shield_water", {"outcome": "transform", "to": "mud", "eff": 2.0, "partial": "transform", "fail": "overwhelm"})
	_cell("sand_surge", "shield_water", {"outcome": "weaken", "partial": "weaken", "fail": "overwhelm"})
	_cell("water_wave", "shield_water", {"bands": [[0.0, "weaken"]], "full_at": 0.0}, {"move": "guard", "tier": 0, "expect": "weaken"})
	_cell("steam", "shield_water", {"bands": [[0.0, "water_condense_in"]], "full_at": 0.0, "heat_share": 0.4}, {"move": "guard", "tier": 0, "expect": "water_condense_in"})
	_cell("mist", "shield_water", {"bands": [[0.0, "water_condense_in"]], "full_at": 0.0}, {"move": "guard", "tier": 0, "expect": "water_condense_in"})
	_cell("vine", "shield_water", {"bands": [[0.0, "amplify"]], "full_at": 0.0, "amp": 1.2}, {"move": "guard", "tier": 0, "expect": "amplify"})
	_cell("blue_fire", "shield_water", {"bands": [[0.0, "weaken"]], "full_at": 0.0, "heat_mult": 1.2}, {"move": "guard", "tier": 0, "expect": "weaken"})
	_cell("fire_field", "shield_water", {"bands": [[0.0, "extinguish"]], "full_at": 0.0})
	_cell("blast", "shield_water", {"bands": [[0.0, "weaken"]], "full_at": 0.0}, {"move": "guard", "tier": 0, "expect": "weaken"})
	_cell("tornado", "shield_water", {"bands": [[0.0, "weaken"]], "full_at": 0.0})
	_cell("vacuum", "shield_water", {"bands": [[0.0, "overwhelm"]], "full_at": 0.0}, {"move": "guard", "tier": 0, "expect": "overwhelm"})
	_cell("sound", "shield_water", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "guard", "tier": 0, "expect": "pass"})


# ================================================================ cells: Ice column

static func _ice_column() -> void:
	# ---- Ice Wall (wall_ice): CP = mass x 0.44 (50 kg -> 22 ... 80 kg -> 35); perfect = Flash Freeze
	_cell("stone", "wall_ice", {"outcome": "block", "perfect": "capture", "partial": "weaken", "fail": "overwhelm", "max_captured": 4},
		{"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("stone_heavy", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"}, {"move": "ice_wall", "tier": 3, "expect": "block"})
	_cell("stone_heavy", "wall_ice", {"tiers": [0, 1], "outcome": "block", "partial": "weaken", "fail": "overwhelm", "id": "water_icewall_heavy_weak"},
		{"move": "ice_wall", "tier": 0, "expect": "weaken"})
	_cell("boulder", "wall_ice", {"bands": [[0.0, "overwhelm"], [1.0, "block"]]}, {"move": "ice_wall", "tier": 3, "expect": "overwhelm"})
	_cell("hot_rock", "wall_ice", {"outcome": "water_hot_block", "partial": "water_hot_block", "fail": "overwhelm", "hu": 120.0, "eff": 0.9},
		{"move": "ice_wall", "tier": 0, "expect": "water_hot_block"})
	_cell("magma", "wall_ice", {"outcome": "water_hot_block", "partial": "water_hot_block", "fail": "overwhelm", "hu": 260.0, "eff": 0.8},
		{"move": "ice_wall", "tier": 2, "expect": "water_hot_block"})
	_cell("lava_wave", "wall_ice", {"outcome": "water_hot_block", "partial": "water_hot_block", "fail": "overwhelm", "hu": 300.0, "eff": 0.7},
		{"move": "ice_wall", "tier": 2, "expect": "water_hot_block"})
	_cell("metal", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"}, {"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("sand", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"}, {"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("sand_cloud", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	_cell("sand_surge", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"}, {"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("glass", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	_cell("water", "wall_ice", {"outcome": "block", "perfect": "water_freeze", "partial": "weaken", "fail": "overwhelm"},
		{"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("water_wave", "wall_ice", {"bands": [[0.0, "block"]], "full_at": 0.0, "perfect": "water_ridge", "stand": 6.0},
		{"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("ice", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"}, {"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("steam", "wall_ice", {"outcome": "water_freeze", "partial": "water_freeze", "fail": "pass", "eff": 2.0},
		{"move": "ice_wall", "tier": 0, "expect": "water_freeze"})
	_cell("mist", "wall_ice", {"outcome": "water_freeze", "partial": "water_freeze", "fail": "pass", "eff": 2.0})
	_cell("vine", "wall_ice", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	_cell("flame", "wall_ice", {"outcome": "water_hot_block", "partial": "water_hot_block", "fail": "overwhelm", "eff": 0.6, "hu": 100.0,
		"heat_share": 0.8}, {"move": "ice_wall", "tier": 0, "expect": "water_hot_block"})
	_cell("fire_field", "wall_ice", {"outcome": "water_hot_block", "partial": "water_hot_block", "fail": "overwhelm", "eff": 0.6, "heat_share": 0.8})
	_cell("blue_fire", "wall_ice", {"outcome": "water_hot_block", "partial": "water_hot_block", "fail": "overwhelm", "eff": 0.35, "hu": 200.0,
		"heat_share": 1.0}, {"move": "ice_wall", "tier": 0, "expect": "overwhelm"})
	_cell("blast", "wall_ice", {"bands": [[0.0, "shatter"], [1.0, "block"]], "eff": 0.5}, {"move": "ice_wall", "tier": 0, "expect": "shatter"})
	_cell("gust", "wall_ice", {"bands": [[0.0, "block"]], "full_at": 0.0}, {"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("tornado", "wall_ice", {"bands": [[0.0, "block"]], "full_at": 0.0}, {"move": "ice_wall", "tier": 0, "expect": "block"})
	_cell("vacuum", "wall_ice", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "ice_wall", "tier": 0, "expect": "pass"})
	_cell("sound", "wall_ice", {"bands": [[0.0, "shatter"], [1.0, "block"]], "eff": 0.4}, {"move": "ice_wall", "tier": 0, "expect": "shatter"})
	# ---- Rime Path (rime): ground line / ice floor zone: T0..T3 power 10 / 14 / 20 / 30
	_cell("puddle", "rime", {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "ice"}, {"move": "rime_path", "tier": 0, "expect": "transform"})
	_cell("water_wave", "rime", {"outcome": "water_ridge", "partial": "water_ridge", "fail": "overwhelm", "partial_at": 0.3, "stand": 6.0},
		{"move": "rime_path", "tier": 1, "expect": "water_ridge"})
	_cell("lava_wave", "rime", {"outcome": "water_quench", "to": "obsidian", "partial": "water_quench", "fail": "water_quench", "eff": 1.2},
		{"move": "rime_path", "tier": 2, "expect": "water_quench"})
	_cell("magma", "rime", {"outcome": "water_quench", "to": "obsidian", "partial": "water_quench", "fail": "water_quench", "eff": 1.2})
	_cell("hot_rock", "rime", {"outcome": "water_quench", "to": "rock", "partial": "water_quench", "fail": "water_quench", "eff": 1.2})
	_cell("water", "rime", {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "ice"})
	_cell("mist", "rime", {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "snow"})
	_cell("steam", "rime", {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "snow"})
	_cell("flame", "rime", {"outcome": "weaken", "partial": "weaken", "fail": "pass", "eff": 1.0})
	# ---- Hoarfrost (frost, a volume): chills, freezes streams, weakens flames, cools rock, brittles vines
	_cell("water", "frost", {"bands": [[0.0, "water_freeze"]], "full_at": 0.0, "drop": false}, {"move": "hoarfrost_fan", "tier": 0, "expect": "water_freeze"})
	_cell("water_wave", "frost", {"outcome": "water_ridge", "partial": "weaken", "fail": "pass", "stand": 5.0})
	_cell("puddle", "frost", {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "ice"})
	_cell("mist", "frost", {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "snow"}, {"move": "hoarfrost_fan", "tier": 0, "expect": "transform"})
	_cell("steam", "frost", {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "snow"})
	_cell("flame", "frost", {"outcome": "weaken", "partial": "weaken", "fail": "pass", "eff": 1.0}, {"move": "hoarfrost_fan", "tier": 1, "expect": "weaken"})
	_cell("hot_rock", "frost", {"outcome": "transform", "to": "rock", "partial": "weaken", "fail": "pass", "hu_per_pu": 20.0, "eff": 1.5},
		{"move": "hoarfrost_fan", "tier": 3, "expect": "transform"})
	_cell("vine", "frost", {"bands": [[0.0, "water_brittle"]], "full_at": 0.0, "slow": 0.3}, {"move": "hoarfrost_fan", "tier": 0, "expect": "water_brittle"})
	_cell("sand_cloud", "frost", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	# ---- Flash Freeze / freeze class (the perfect-timed Ice Wall and Freeze-Draw)
	_cell("water", "freeze", {"bands": [[0.0, "water_freeze"]], "full_at": 0.0}, {"move": "freeze_draw", "tier": 0, "expect": "water_freeze"})
	_cell("water_wave", "freeze", {"bands": [[0.0, "water_ridge"]], "full_at": 0.0, "stand": 6.0}, {"move": "freeze_draw", "tier": 0, "expect": "water_ridge"})
	_cell("steam", "freeze", {"bands": [[0.0, "water_freeze"]], "full_at": 0.0})
	_cell("mist", "freeze", {"bands": [[0.0, "water_freeze"]], "full_at": 0.0})
	# ---- Freeze-Draw (grip_ice): seizes ice and (frozen on the way) water
	_cell("ice", "grip_ice", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "freeze_draw", "tier": 0, "expect": "reclaim"})
	_cell("water", "grip_ice", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "freeze_draw", "tier": 0, "expect": "reclaim"})
	_cell("water_wave", "grip_ice", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "freeze_draw", "tier": 0, "expect": "reclaim"})
	_cell("puddle", "grip_ice", {"bands": [[0.0, "reclaim"]], "full_at": 0.0})
	_cell("*", "grip_ice", {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "water_grip_ice_other"})


# ================================================================ cells: Mist column

static func _mist_column() -> void:
	# ---- Steam Screen (screen_steam): CP 10 (zone / guard power); perfect = Condense
	for t in ["hot_rock", "magma"]:
		_cell(t, "screen_steam", {"outcome": "weaken", "partial": "weaken", "fail": "pass", "eff": 1.0, "partial_at": 0.0}, {"move": "steam_screen", "tier": 0,
			"expect": "weaken" if t == "hot_rock" else "weaken", "tp": 26.8 if t == "hot_rock" else 35.0})
	_cell("lava_wave", "screen_steam", {"bands": [[0.0, "weaken"]], "full_at": 0.0}, {"move": "steam_screen", "tier": 0, "expect": "weaken"})
	for t in ["stone", "metal", "glass", "stone_heavy", "sand", "ice"]:
		var rr := {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.85}
		if t == "ice":
			rr = {"bands": [[0.0, "water_melt"]], "full_at": 0.0, "hu": 30.0, "eff": 1.5}
		_cell(t, "screen_steam", rr, {"move": "steam_screen", "tier": 0, "expect": "slow" if t != "ice" else "water_melt"} if t in ["stone", "ice"] else {})
	_cell("water", "screen_steam", {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.9})
	_cell("water_wave", "screen_steam", {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.9})
	_cell("steam", "screen_steam", {"outcome": "absorb", "perfect": "water_skin", "partial": "absorb", "fail": "pass"}, {"move": "steam_screen", "tier": 0, "expect": "absorb"})
	_cell("mist", "screen_steam", {"outcome": "absorb", "perfect": "water_skin", "partial": "absorb", "fail": "pass"})
	_cell("flame", "screen_steam", {"outcome": "water_dampen", "perfect": "extinguish", "partial": "water_dampen", "fail": "pass", "eff": 1.5, "k": 0.5},
		{"move": "steam_screen", "tier": 0, "expect": "water_dampen"})
	_cell("fire_field", "screen_steam", {"outcome": "water_dampen", "partial": "water_dampen", "fail": "pass", "eff": 1.5, "k": 0.5})
	_cell("blue_fire", "screen_steam", {"bands": [[0.0, "water_dampen"]], "full_at": 0.0, "k": 0.25}, {"move": "steam_screen", "tier": 0, "expect": "water_dampen"})
	_cell("blast", "screen_steam", {"bands": [[0.0, "water_dampen"]], "full_at": 0.0, "eff": 1.5, "k": 0.4}, {"move": "steam_screen", "tier": 0, "expect": "water_dampen"})
	_cell("vine", "screen_steam", {"bands": [[0.0, "weaken"]], "full_at": 0.0})
	_cell("sound", "screen_steam", {"bands": [[0.0, "absorb"]], "full_at": 0.0, "eff": 1.2})
	_cell("gust", "screen_steam", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "steam_screen", "tier": 0, "expect": "pass"})
	_cell("vacuum", "screen_steam", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "steam_screen", "tier": 0, "expect": "pass"})
	_cell("tornado", "screen_steam", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	# ---- Creeping Fog (fog zone / mist): conceals, dampens fire and blasts, muffles sound, turns sand to mud rain
	_cell("flame", "fog", {"bands": [[0.0, "water_dampen"]], "full_at": 0.0, "k": 0.5, "eff": 1.5}, {"move": "creeping_fog", "tier": 0, "expect": "water_dampen"})
	_cell("fire_field", "fog", {"bands": [[0.0, "water_dampen"]], "full_at": 0.0, "k": 0.5})
	_cell("ember", "fog", {"bands": [[0.0, "extinguish"]], "full_at": 0.0})
	_cell("blue_fire", "fog", {"bands": [[0.0, "water_dampen"]], "full_at": 0.0, "k": 0.3})
	_cell("hot_rock", "fog", {"bands": [[0.0, "weaken"]], "full_at": 0.0}, {"move": "creeping_fog", "tier": 0, "expect": "weaken"})
	_cell("magma", "fog", {"bands": [[0.0, "weaken"]], "full_at": 0.0})
	_cell("lava_wave", "fog", {"bands": [[0.0, "weaken"]], "full_at": 0.0}, {"move": "creeping_fog", "tier": 0, "expect": "weaken"})
	_cell("blast", "fog", {"bands": [[0.0, "water_dampen"]], "full_at": 0.0, "k": 0.4, "eff": 1.5}, {"move": "creeping_fog", "tier": 0, "expect": "water_dampen"})
	_cell("sound", "fog", {"bands": [[0.0, "absorb"]], "full_at": 0.0, "eff": 1.2}, {"move": "creeping_fog", "tier": 0, "expect": "absorb"})
	_cell("sand", "fog", {"outcome": "transform", "to": "mud", "eff": 1.5, "partial": "transform", "fail": "pass"}, {"move": "creeping_fog", "tier": 0, "expect": "transform"})
	_cell("sand_cloud", "fog", {"outcome": "transform", "to": "mud", "eff": 1.5, "partial": "transform", "fail": "pass"})
	_cell("sand_surge", "fog", {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.8}, {"move": "creeping_fog", "tier": 0, "expect": "slow"})
	for t in ["stone", "stone_heavy", "metal", "ice", "glass", "boulder", "water", "water_wave", "vine"]:
		_cell(t, "fog", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "creeping_fog", "tier": 0, "expect": "pass"} if t == "stone" else {})
	_cell("gust", "fog", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("tornado", "fog", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("lightning", "fog", {"bands": [[0.0, "conduct"]], "full_at": 0.0, "factor": 0.6}, {"move": "creeping_fog", "tier": 0, "expect": "conduct"})
	# ---- Condense (condense class): the perfect Steam Screen and Vapor Draw T+A
	for t in ["water", "water_wave", "steam", "mist"]:
		_cell(t, "condense", {"bands": [[0.0, "water_skin"]], "full_at": 0.0}, {"move": "steam_screen", "tier": 0, "perfect": true, "expect": "water_skin"} if t == "steam" else {})
	# ---- Vapor Draw (grip_vapor)
	_cell("steam", "grip_vapor", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "vapor_draw", "tier": 0, "expect": "reclaim"})
	_cell("mist", "grip_vapor", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "vapor_draw", "tier": 0, "expect": "reclaim"})
	_cell("*", "grip_vapor", {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "water_grip_vapor_other"})


# ================================================================ cells: Plant column

static func _plant_column() -> void:
	# ---- Living Lattice (wall_vine): CP 40 kg x 0.4 = 16; captures solids x1.3, drinks water, burns x3
	for t in ["stone"]:
		_cell(t, "wall_vine", {"outcome": "capture", "perfect": "water_sling", "partial": "weaken", "fail": "overwhelm", "eff": 1.3, "max_captured": 4,
			"fallback": "block"}, {"move": "living_lattice", "tier": 1, "expect": "capture", "tp": 17.0})
	_cell("stone_heavy", "wall_vine", {"outcome": "weaken", "partial": "weaken", "fail": "overwhelm", "eff": 1.3}, {"move": "living_lattice", "tier": 0, "expect": "weaken"})
	_cell("boulder", "wall_vine", {"bands": [[0.0, "overwhelm"], [1.0, "block"]]}, {"move": "living_lattice", "tier": 0, "expect": "overwhelm"})
	_cell("hot_rock", "wall_vine", {"bands": [[0.0, "water_burn"]], "full_at": 0.0, "burn_kg": 4.0, "pass": 1.0}, {"move": "living_lattice", "tier": 0, "expect": "water_burn"})
	_cell("magma", "wall_vine", {"bands": [[0.0, "water_burn"]], "full_at": 0.0, "burn_kg": 6.0, "pass": 1.0}, {"move": "living_lattice", "tier": 0, "expect": "water_burn"})
	_cell("lava_wave", "wall_vine", {"bands": [[0.0, "water_burn"]], "full_at": 0.0, "burn_kg": 6.0, "pass": 1.0}, {"move": "living_lattice", "tier": 0, "expect": "water_burn"})
	_cell("metal", "wall_vine", {"outcome": "capture", "partial": "weaken", "fail": "overwhelm", "eff": 0.7, "fallback": "block"}, {"move": "living_lattice", "tier": 0, "expect": "weaken"})
	_cell("sand", "wall_vine", {"outcome": "capture", "partial": "capture", "fail": "overwhelm", "eff": 1.3, "fallback": "block"}, {"move": "living_lattice", "tier": 0, "expect": "capture"})
	_cell("sand_cloud", "wall_vine", {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	_cell("sand_surge", "wall_vine", {"outcome": "capture", "partial": "weaken", "fail": "overwhelm", "eff": 1.5, "fallback": "block"}, {"move": "living_lattice", "tier": 0, "expect": "capture"})
	_cell("water", "wall_vine", {"bands": [[0.0, "water_feed"]], "full_at": 0.0, "max_kg": 8.0}, {"move": "living_lattice", "tier": 0, "expect": "water_feed"})
	_cell("water_wave", "wall_vine", {"outcome": "water_feed", "partial": "weaken", "fail": "overwhelm", "max_kg": 10.0}, {"move": "living_lattice", "tier": 3, "expect": "water_feed"})
	_cell("ice", "wall_vine", {"outcome": "capture", "partial": "weaken", "fail": "overwhelm", "fallback": "block"}, {"move": "living_lattice", "tier": 0, "expect": "capture"})
	_cell("mist", "wall_vine", {"bands": [[0.0, "absorb"]], "full_at": 0.0})
	_cell("steam", "wall_vine", {"bands": [[0.0, "overwhelm"]], "full_at": 0.0}, {"move": "living_lattice", "tier": 0, "expect": "overwhelm"})
	_cell("vine", "wall_vine", {"outcome": "clash", "partial": "clash", "fail": "clash"})
	_cell("flame", "wall_vine", {"bands": [[0.0, "water_burn"]], "full_at": 0.0, "burn_kg": 3.0}, {"move": "living_lattice", "tier": 0, "expect": "water_burn"})
	_cell("fire_field", "wall_vine", {"bands": [[0.0, "water_burn"]], "full_at": 0.0, "burn_kg": 2.0})
	_cell("ember", "wall_vine", {"bands": [[0.0, "water_burn"]], "full_at": 0.0, "burn_kg": 1.5})
	_cell("blue_fire", "wall_vine", {"bands": [[0.0, "water_burn"]], "full_at": 0.0, "burn_kg": 5.0, "k": 0.1}, {"move": "living_lattice", "tier": 0, "expect": "water_burn"})
	_cell("blast", "wall_vine", {"bands": [[0.0, "overwhelm"]], "full_at": 0.0}, {"move": "living_lattice", "tier": 0, "expect": "overwhelm"})
	_cell("gust", "wall_vine", {"outcome": "block", "partial": "block", "fail": "weaken", "eff": 1.0}, {"move": "living_lattice", "tier": 0, "expect": "block"})
	_cell("sound", "wall_vine", {"bands": [[0.0, "absorb"]], "full_at": 0.0}, {"move": "living_lattice", "tier": 0, "expect": "absorb"})
	_cell("tornado", "wall_vine", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("vacuum", "wall_vine", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	# Dry vines block sparks; wet / living vines ground the bolt (props.wet): a conduction-safe answer
	_cell("lightning", "wall_vine", {"bands": [[0.0, "shatter"], [1.0, "ground"]], "absorb_on_fail": 0.5, "eff": 1.0}, {"move": "living_lattice", "tier": 0, "expect": "ground", "tp": 14.0})
	# ---- Vinegrip (grip_vine): bodies <= 40 kg at 9 m
	for t in ["stone", "stone_heavy", "metal", "ice", "glass", "sand", "hot_rock"]:
		_cell(t, "grip_vine", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "vinegrip", "tier": 0, "expect": "reclaim"} if t == "stone" else {})
	_cell("vine", "grip_vine", {"bands": [[0.0, "reclaim"]], "full_at": 0.0}, {"move": "vinegrip", "tier": 0, "expect": "reclaim"})
	_cell("water", "grip_vine", {"bands": [[0.0, "reclaim"]], "full_at": 0.0})
	_cell("*", "grip_vine", {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "water_grip_vine_other"})
	# ---- Thicket Fan zone (briar): slows and catches small projectiles
	for t in ["stone", "metal", "ice", "glass", "sand"]:
		_cell(t, "briar", {"bands": [[0.0, "slow"]], "full_at": 0.0, "factor": 0.6})
	# ---- Root Snare wave (wave_vine)
	_cell("*", "wave_vine", {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "water_roots_pass"})
