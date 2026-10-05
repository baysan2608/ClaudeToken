class_name FireRules
extends RefCounted
## The Fire column of the counter matrix (docs/MOVESET.md §8.3): the rule cells of the Fire counter classes, the
## custom outcomes they need (prefix "fire_"), statuses, tag classes and channel hooks. Owned by the Fire kit; the
## core owns the legacy cells (aura_flame x legacy threats incl. Heat Sink and Return Current, flare x water / stones,
## technique legality) which are never replaced here.
##
## Counter classes encoded here: aura_flame (non-legacy threats), flame (non-legacy threats), fire_field (fire fields
## and fire lines), fireball (fireballs, comets as obstacles), heat_grip / draw_heat (new materials), heat_ranged
## (Scorch / Smelter / Kiln), aura_blue (Blue Aegis, Corona), blue_fire (blue volumes), magma_rift (Blue Furrow),
## ward_static (Static Ward, static fields), lightning (bolts meeting bodies on their path), ground_current,
## guard_blast (Reactive Blast), blast (detonations meeting bodies), ember (Spark Mines, Scatter Charges).
## Every heat / mass change goes through the CombatWorld ledger helpers (FireUtil.transfer / pay_into, heat_body,
## boil_water, split_body, decay_body, close_zone, convert_mat).

## Every cell registered by the kit: "threat|counter[|tiers]" -> {id, threat, counter, ref, tiers} (docs, Lab, tests).
static var CELLS := {}

## Reference threats of MOVESET §8.3: threat power (PU) and mass (kg).
const REF := {
	"stone": {"tp": 17.0, "mass": 20.0}, "stone_heavy": {"tp": 31.5, "mass": 45.0}, "boulder": {"tp": 110.0, "mass": 200.0},
	"hot_rock": {"tp": 27.0, "mass": 20.0}, "magma": {"tp": 35.0, "mass": 20.0}, "lava_wave": {"tp": 27.3, "mass": 20.0},
	"metal": {"tp": 8.0, "mass": 6.0}, "sand": {"tp": 10.0, "mass": 8.0}, "sand_cloud": {"tp": 8.0, "mass": 6.0},
	"sand_surge": {"tp": 20.0, "mass": 14.0}, "water": {"tp": 9.6, "mass": 12.0}, "water_wave": {"tp": 24.0, "mass": 14.0},
	"ice": {"tp": 9.0, "mass": 4.0}, "mist": {"tp": 3.0, "mass": 2.0}, "steam": {"tp": 6.0, "mass": 1.0},
	"vine": {"tp": 14.0, "mass": 10.0}, "flame": {"tp": 8.0, "mass": 0.0}, "blue_fire": {"tp": 16.0, "mass": 0.0},
	"lightning": {"tp": 24.0, "mass": 0.0}, "blast": {"tp": 16.0, "mass": 0.0}, "gust": {"tp": 11.0, "mass": 0.0},
	"tornado": {"tp": 30.0, "mass": 0.0}, "vacuum": {"tp": 18.0, "mass": 0.0}, "sound": {"tp": 16.0, "mass": 0.0},
	"fire_field": {"tp": 8.0, "mass": 0.0}, "ember": {"tp": 6.0, "mass": 0.5}, "molten_metal": {"tp": 20.0, "mass": 6.0},
}

const LIGHT_SOLIDS := ["stone", "hot_rock", "metal", "glass", "ice", "sand"]
const ALL_ROWS := ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "molten_metal", "sand",
	"sand_cloud", "sand_surge", "glass", "water", "water_wave", "ice", "mist", "steam", "vine", "flame", "blue_fire",
	"fire_field", "ember", "lightning", "blast", "gust", "tornado", "vacuum", "sound"]


static func _cell(t: String, c: String, rule: Dictionary, ref: Dictionary = {}) -> void:
	var r := rule.duplicate(true)
	r["owner"] = "fire"
	if not r.has("id"):
		r["id"] = "fire_%s_%s" % [c, t]
	var key := "%s|%s%s" % [t, c, "|" + str(r.tiers) if r.has("tiers") else ""]
	if not Interactions.can_add(t, c, r):
		return   # a legacy cell answers this pair (kept exactly)
	if Interactions.add_rule(t, c, r):
		CELLS[key] = {"id": r.id, "threat": t, "counter": c, "ref": ref, "tiers": r.get("tiers", [])}


static func _plain_guard(extra: Dictionary = {}) -> Dictionary:
	var r := {"bands": [[0.0, "block"]], "full_at": 0.0, "perfect": "deflect", "chip": 0.12, "bal": 0.55, "knock": 0.35,
		"perfect_balance": 18.0, "perfect_range": 3.0}
	r.merge(extra, true)
	return r


static func register() -> void:
	CELLS.clear()
	_statuses()
	_tags()
	_outcomes()
	_flame_column()
	_field_column()
	_technique_cells()
	_blue_column()
	_lightning_column()
	_combustion_column()


# ================================================================ statuses, tags, channels

static func _statuses() -> void:
	CombatWorld.register_status("grounding", {"immune": ["conduct"]})
	CombatWorld.register_status("overcharged", {"speed": 1.3, "recovery": 0.8})
	CombatWorld.register_status("kiln_burn", {"dps": 6.0})


static func _tags() -> void:
	Interactions.register_tag_class(&"fire_field", &"fire_field", &"fire_field")
	Interactions.register_tag_class(&"fire_line", &"fire_field", &"fire_field")
	Interactions.register_tag_class(&"fireball", &"flame", &"fireball")
	Interactions.register_tag_class(&"comet", &"blue_fire", &"fireball")
	Interactions.register_tag_class(&"ember", &"ember", &"ember")
	Interactions.register_tag_class(&"magma_rift", &"", &"magma_rift")
	Interactions.register_tag_class(&"ground_current", &"lightning", &"ground_current")
	Interactions.register_tag_class(&"static_field", &"lightning", &"static_field")
	Interactions.register_tag_class(&"corona", &"blue_fire", &"corona")
	Interactions.register_tag_class(&"kiln", &"kiln", &"kiln")
	for tg in [&"fire_field", &"fire_line", &"fireball", &"comet", &"ember", &"corona"]:
		Interactions.register_channels(tg, Callable(FireRules, "_fire_channels"))
	Interactions.register_channels(&"ground_current", Callable(FireRules, "_current_channels"))
	Interactions.register_channels(&"static_field", Callable(FireRules, "_current_channels"))


## Fire bodies and fields: heat in PU is both their threat channel H and their counter power.
static func _fire_channels(_w: CombatWorld, b: MatBody, g: Agent) -> void:
	g.ch.H = maxf(0.0, b.heat_payload) / Interactions.HU_PER_PU
	g.ch.P = 0.0
	g.power = FireUtil.field_power(b)
	g.heat = maxf(0.0, b.heat_payload)
	if b.props.get("blue", false) or b.tag == &"comet" or b.tag == &"corona":
		g.power *= 1.5   # blue fire: x1.5 intensity (MOVESET §7.10)


static func _current_channels(_w: CombatWorld, b: MatBody, g: Agent) -> void:
	g.ch.E = maxf(b.charge, b.power)
	g.ch.P = 0.0
	g.power = maxf(0.1, g.ch.E)


# ================================================================ outcomes

static func _outcomes() -> void:
	for nm in ["heat", "evaporate", "burn", "melt", "snuffed", "dampen", "fanned", "blown", "tornado", "guard_absorb",
			"aegis_melt", "static", "static_full", "reactive", "counter_blast", "body_burst", "charge_body", "disrupt_zone",
			"fill_void", "suppressed", "fulgurite", "glassify", "conduct_owner", "smother"]:
		Interactions.register_outcome("fire_" + nm, Callable(FireRules, "o_" + nm))


## The interaction event carries a catalogued outcome name (FxEvents.OUTCOMES) and the result material in `to`.
static func _report(res: Dictionary, outcome: String, to: String = "") -> void:
	res["outcome"] = outcome
	if to != "":
		res["to"] = to


## The heat source of a cell: the fire side (a volume with a paid budget, a FIRE body / field) - never a guard.
static func _fire_side(t: Agent, c: Agent) -> Agent:
	for g in [c, t]:
		if g == null or g.kind == "guard":
			continue
		if g.body != null and g.body.alive and (g.body.mat == Sim.Mat.FIRE or (g.body.mat == Sim.Mat.AIR and g.body.heat_payload > 0.0)):
			return g
		if g.kind == "volume" and g.heat > 0.0:
			return g
	return null


static func _other_body(t: Agent, src: Agent) -> MatBody:
	if t != src and t.body != null and t.body.alive:
		return t.body
	return null


## Warms the body on the other side with `share` of the fire side's heat (fields: per contact, rate-limited).
static func o_heat(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var src := _fire_side(t, c)
	var b := _other_body(t, src) if src != null else null
	if b == null and src != null and src == t:
		b = c.body
	if src == null or b == null:
		res.pass_scale = 1.0
		return true
	var used := FireUtil.transfer(w, src, b, FireUtil._src_avail(src) * float(r.get("share", 0.3)))
	res.heat_used = float(res.heat_used) + used
	res.pass_scale = 1.0
	_report(res, "heat")
	return true


## Mist / fog / water bodies boil off in the fire (x1.5 for vapour: thin water).
static func o_evaporate(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var src := _fire_side(t, c)
	var b := t.body if t.body != null and t.body.is_water() else (c.body if c.body != null and c.body.is_water() else null)
	if src == null or b == null or not b.alive:
		return false
	var used := FireUtil.transfer(w, src, b, FireUtil._src_avail(src) * float(r.get("share", 0.6)))
	res.heat_used = float(res.heat_used) + used
	if b.alive and b.mass <= 0.1:
		if b.form == Sim.Form.ZONE:
			w.close_zone(b, "evaporated")
		else:
			w.decay_body(b, "evaporated")
	res.pass_scale = 1.0
	_report(res, "transform", "steam")
	return true


## Vines take the heat (x3 efficacy in the rule) and burn above 250 °C (CombatWorld burns them, ledger burned).
static func o_burn(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var src := _fire_side(t, c)
	var b := t.body if t.body != null and t.body.mat == Sim.Mat.PLANT else (c.body if c.body != null and c.body.mat == Sim.Mat.PLANT else null)
	if b == null or not b.alive:
		return false
	if src != null:
		res.heat_used = float(res.heat_used) + FireUtil.transfer(w, src, b, FireUtil._src_avail(src) * float(r.get("share", 0.8)))
	if b.alive and b.temp >= float(Materials.prop(Sim.Mat.PLANT, "ignite", 250.0)):
		w.burn_plant(b, minf(b.mass, float(r.get("burn_kg", 2.0))))
	res.pass_scale = 1.0
	_report(res, "transform", "ash")
	return true


## Concentrated heat melts a solid in flight: it takes what it needs to go molten (or what the beam has left) and,
## softened, falls short (MOVESET §5.4 "turn it into lava": Searing Beam T2 on a flying stone).
static func o_melt(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var src := _fire_side(t, c)
	var b := t.body
	if src == null or b == null or not b.alive or not Materials.is_fusible(b.mat):
		return false
	var need := FireUtil.melt_need(b, float(r.get("to_liquid", 1.0)))
	var used := FireUtil.transfer(w, src, b, need)
	res.heat_used = float(res.heat_used) + used
	if b.alive and b.liquid > 0.15 and not b.on_ground:
		b.vel = Vector3(b.vel.x * float(r.get("keep", 0.35)), minf(b.vel.y, 1.0), b.vel.z * float(r.get("keep", 0.35)))
		b.gravity_scale = maxf(b.gravity_scale, 1.0)
		w.emit("melt_in_flight", {"body": b.id, "liquid": b.liquid, "by": src.actor.id if src.actor != null else -1})
	res.pass_scale = 1.0
	res.stopped = false
	_report(res, "transform", "lava" if b.is_stone() else "molten_metal")
	return true


## The fire side goes out (blast oxygen, vacuum, smothering sand, a dousing wave): its heat leaves the ledger.
static func o_snuffed(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var f: MatBody = null
	for g in [c, t]:
		if g.body != null and g.body.alive and (g.body.mat == Sim.Mat.FIRE or g.body.tag == &"fire_field"):
			f = g.body
			break
	if f == null:
		if t.kind == "volume":
			t.heat = 0.0
			res.stopped = true
			res.pass_scale = 0.0
			_report(res, "extinguish")
			return true
		return false
	w.emit("extinguish", {"body": f.id, "by": String(t.cls) if f == c.body else String(c.ccls)})
	if f.form == Sim.Form.ZONE:
		w.close_zone(f, "snuffed")
	else:
		w.decay_body(f, "snuffed")
	res.pass_scale = float(r.get("pass", 1.0)) if f == c.body else 0.0
	res.stopped = f == t.body
	_report(res, "extinguish")
	return true


## A strong field survives a blast weakened: it loses the blast's share of its heat (to the air).
static func o_dampen(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var f := c.body if c.body != null and c.body.alive and c.body.mat == Sim.Mat.FIRE else null
	if f == null:
		return false
	var k := clampf(float(res.tp) / maxf(float(res.cp_eff), 0.01), 0.0, 1.0)
	var got := -Thermal.heat(f, -f.heat_payload * k)
	w.ledger.ambient -= got
	res.pass_scale = 0.0
	_report(res, "weaken")
	return true


## Weak wind fans a fire (MOVESET §8.3, §5.3 special band): +30 % heat (fantasy oxygen, booked generated), +1 m.
static func o_fanned(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var f := c.body if c.body != null and c.body.alive and c.body.mat == Sim.Mat.FIRE else null
	if f == null:
		return false
	if int(f.props.get("fanned_tick", -100)) > w.tick - 30:
		res.pass_scale = 1.0
		return true
	f.props["fanned_tick"] = w.tick
	var add := f.heat_payload * (float(r.get("amp", 1.3)) - 1.0)
	f.heat_payload += add
	w.ledger.generated += add
	if f.form == Sim.Form.ZONE:
		f.zone_radius = minf(f.zone_radius + 1.0, 6.0)
		f.radius = f.zone_radius
		if t.dir.length() > 0.1:
			f.vel = Vector3(t.dir.x, 0.0, t.dir.z).normalized() * 1.5
			f.props["drag"] = 1.0
	res.pass_scale = 1.0
	_report(res, "amplify")
	return true


## Medium wind blows a fire aside (deflect band): the field is pushed 2 m downwind.
static func o_blown(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var f := c.body if c.body != null and c.body.alive and c.body.mat == Sim.Mat.FIRE else null
	if f == null:
		return false
	var d := t.dir
	if d.length() < 0.1 and t.actor != null:
		d = f.pos - t.actor.pos
	d.y = 0.0
	if d.length() > 0.01 and f.form == Sim.Form.ZONE:
		f.vel = d.normalized() * 6.0
		f.props["drag"] = 3.0
	elif d.length() > 0.01:
		f.vel = d.normalized() * maxf(f.vel.length(), 8.0)
		f.attack_owner = t.actor.id if t.actor != null else f.attack_owner
	w.emit("deflect", {"actor": t.actor.id if t.actor != null else -1, "body": f.id, "verb": "wind", "kind": "fire"})
	res.pass_scale = 1.0
	_report(res, "deflect")
	return true


## A tornado that runs into a fire becomes a fire tornado: the field rides it (a neutral, burning hazard).
static func o_tornado(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var tor := t.body
	var f := c.body if c.body != null and c.body.alive and c.body.mat == Sim.Mat.FIRE else null
	if tor == null or f == null or not tor.alive or f.form != Sim.Form.ZONE:
		return false
	if int(f.props.get("follow", -1)) == tor.id:
		res.pass_scale = 1.0
		return true
	f.props["follow"] = tor.id
	f.props["spare_owner"] = false   # neutral hazard: it burns everyone
	f.max_life = maxf(f.max_life, f.age + 3.0)
	tor.props["fire"] = true
	tor.props["infused"] = "fire"
	w.emit("infuse", {"actor": c.actor.id if c.actor != null else -1, "body": tor.id, "with": "fire", "field": f.id})
	res.pass_scale = 1.0
	_report(res, "amplify", "fire_tornado")
	return true


## A guard of fire takes a flame's heat into the reserve (Blue Aegis x1.5 on a perfect guard, Heat Sink x0.7 vs blue).
static func o_guard_absorb(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	if c.kind != "guard" or c.actor == null:
		res.pass_scale = 1.0
		return true
	var share := float(r.get("share", 0.5)) * (float(r.get("perfect_share", 1.5)) if c.perfect else 1.0)
	var heat := t.heat if t.kind == "volume" else (t.body.heat_payload if t.body != null and t.body.mat == Sim.Mat.FIRE else 0.0)
	var gain := clampf(heat * share, 0.0, Sim.RESERVE_MAX - c.actor.heat_reserve)
	if gain > 0.0:
		if t.kind == "volume":
			t.heat -= gain
		elif t.body != null:
			t.body.heat_payload -= gain
		c.actor.heat_reserve += gain
		res.absorbed = gain
	var info: Dictionary = ctx.get("info", {})
	if c.perfect:
		w.emit("perfect_deflect", {"actor": c.actor.id, "attacker": info.get("attacker", -1), "kind": String(t.cls)})
		c.actor.last_result = "perfect"
		res.result = "perfect"
	else:
		res.result = w.guard_chip(c.actor, info, {"chip": 0.0, "bal": float(r.get("bal", 0.15)), "knock": 0.0, "kind": "heat_sink"}, t)
	res.stopped = true
	res.pass_scale = 0.0
	w.emit("heat_sink", {"actor": c.actor.id, "gain": gain, "cls": String(t.cls)})
	_report(res, "absorb")
	return true


## Blue Aegis / Corona melt small ice and metal (<= 8 kg) before contact: the Aegis pays the heat (reserve, then
## Focus); a corona spends its own heat. Ice goes to water / steam, metal to molten droplets.
static func o_aegis_melt(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or b.mass > float(r.get("mass_max", 8.0)):
		return false
	var need := FireUtil.melt_need(b, 1.0)
	var used := 0.0
	if c.kind == "guard" and c.actor != null:
		if not w.can_pay_heat(c.actor, need * 0.5):
			return false
		used = FireUtil.pay_into(w, c.actor, b, need)
	elif c.body != null and c.body.alive:
		used = FireUtil.transfer(w, c, b, need)
	res.heat_used = float(res.heat_used) + used
	if b.alive:
		b.vel *= 0.25
		b.attack_id = 0
		b.gravity_scale = 1.0
	w.emit("transform", {"body": b.id, "at": b.pos, "from": String(t.cls), "to": "water" if b.is_water() else "molten_metal", "why": "aegis"})
	if c.kind == "guard":
		res.result = w.guard_chip(c.actor, ctx.get("info", {}), {"chip": 0.0, "bal": 0.0, "knock": 0.0, "kind": "aegis_melt"}, t)
	res.stopped = true
	res.pass_scale = 0.0
	_report(res, "transform", "water" if (b.alive and b.is_water()) else "molten_metal")
	return true


## Static Ward (non-perfect): half the bolt is stored as static (<= 60), half lands.
static func o_static(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	if c.kind != "guard" or c.actor == null:
		res.pass_scale = 1.0
		return true
	if not ctx.has("info"):
		res.pass_scale = 1.0
		return true
	var e := float(t.ch.E)
	var share := float(r.get("share", 0.5))
	var store := minf(e * share, FireLightning.STATIC_MAX - c.actor.static_charge)
	c.actor.static_charge += maxf(0.0, store)
	res.absorbed = store
	res.pass_scale = 1.0 - share
	res.knock_scale = 1.0 - share
	w.emit("static_absorb", {"actor": c.actor.id, "stored": store, "static": c.actor.static_charge, "perfect": false})
	FxEvents.fx(w, "aura", "lightning", {"actor": c.actor.id, "pos": c.actor.chest(), "power": c.actor.static_charge, "on": true, "shape": "small"})
	_report(res, "absorb")
	return true


## Static Ward, perfect without redirect_current: the whole bolt is absorbed (stored up to 60).
static func o_static_full(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, ctx: Dictionary) -> bool:
	if c.kind != "guard" or c.actor == null:
		return false
	if not ctx.has("info"):
		res.pass_scale = 1.0   # the redirect pre-check of a discharge: the hit itself is answered later
		return true
	var e := float(t.ch.E)
	var store := minf(e, FireLightning.STATIC_MAX - c.actor.static_charge)
	c.actor.static_charge += maxf(0.0, store)
	res.absorbed = store
	var info: Dictionary = ctx.get("info", {})
	w.emit("perfect_deflect", {"actor": c.actor.id, "attacker": info.get("attacker", -1), "kind": "lightning"})
	w.emit("static_absorb", {"actor": c.actor.id, "stored": store, "static": c.actor.static_charge, "perfect": true})
	c.actor.last_result = "perfect"
	res.result = "perfect"
	res.stopped = true
	res.pass_scale = 0.0
	_report(res, "absorb")
	return true


## Reactive Blast (8 Focus per trigger): light solids are blown aside (perfect: back at the thrower), flames snuffed
## (oxygen), clouds dispersed. Without the Focus it is a plain guard.
static func o_reactive(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	if c.kind != "guard" or c.actor == null:
		return false
	var info: Dictionary = ctx.get("info", {})
	if not w.spend_focus(c.actor, FireCombustion.REACTIVE_COST):
		w.emit("insufficient", {"actor": c.actor.id, "what": "focus", "move": "reactive_blast"})
		res.result = w.guard_chip(c.actor, info, _plain_guard(), t)
		res.stopped = true
		res.pass_scale = 0.0
		_report(res, "block")
		return true
	FxEvents.fx(w, "burst", "blast", {"actor": c.actor.id, "pos": c.actor.chest() + c.actor.forward() * 0.6, "radius": 1.6,
		"power": float(res.cp_eff), "move": "reactive_blast", "element": FireUtil.E, "sub": 3, "shape": "small"})
	w.emit("reactive_blast", {"actor": c.actor.id, "cls": String(t.cls), "perfect": c.perfect})
	if t.kind == "body" and t.body != null and t.body.alive:
		if t.body.mat == Sim.Mat.FIRE:
			return o_snuffed(w, t, c, res, {}, ctx) and _guard_clean(w, c, info, res, t)
		if c.perfect and String(r.get("perfect_body", "reflect")) == "reflect" and t.body.mass <= 30.0:
			Outcomes.reflect(w, t, c, res, {"speed_mult": 1.0}, ctx)
			_report(res, "reflect")
			return true
		Outcomes.deflect(w, t, c, res, {"side": 0.8, "up": 3.0}, ctx)
		_report(res, "deflect")
		return true
	# A volume: flames lose their oxygen, gusts and blasts are met by the counter-blast.
	t.heat = 0.0
	_guard_clean(w, c, info, res, t)
	_report(res, "extinguish" if ["flame", "blue_fire"].has(String(t.cls)) else "block")
	return true


static func _guard_clean(w: CombatWorld, c: Agent, info: Dictionary, res: Dictionary, t: Agent) -> bool:
	if c.perfect:
		w.emit("perfect_deflect", {"actor": c.actor.id, "attacker": info.get("attacker", -1), "kind": String(t.cls)})
		c.actor.last_result = "perfect"
		res.result = "perfect"
	else:
		res.result = w.guard_chip(c.actor, info, {"chip": 0.0, "bal": 0.1, "knock": 0.0, "kind": "reactive"}, t)
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Blast against Reactive Blast: the bigger blast wins (MOVESET §8.3 "clash counter-blast").
static func o_counter_blast(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	if c.kind != "guard" or c.actor == null:
		return false
	var info: Dictionary = ctx.get("info", {})
	if not w.spend_focus(c.actor, FireCombustion.REACTIVE_COST):
		return false
	_guard_clean(w, c, info, res, t)
	var att := w.get_actor(int(info.get("attacker", -1)))
	if att != null and att.pos.distance_to(c.actor.pos) < 4.0:
		var d := att.pos - c.actor.pos
		d.y = 0.0
		att.vel += d.normalized() * 5.0 + Vector3(0, 2.0, 0)
		att.balance -= 12.0
		att.balance_idle = 0.0
	FxEvents.fx(w, "burst", "blast", {"actor": c.actor.id, "pos": c.actor.chest(), "radius": 2.0, "power": float(res.cp_eff),
		"move": "reactive_blast", "element": FireUtil.E, "sub": 3})
	_report(res, "clash")
	return true


## A fire body (fireball, comet, ember) met by something solid or another attack: it bursts there.
static func o_body_burst(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var f := c.body if c.body != null and c.body.alive and c.body.mat == Sim.Mat.FIRE else null
	if f == null:
		return false
	if f.tag == &"ember":
		FireCombustion.pop_ember(w, f, "contact")
	else:
		FireFlame.burst_fire_body(w, f, "clash")
	res.pass_scale = 1.0
	res.stopped = false
	_report(res, "clash")
	return true


## Lightning charges a conductor it meets (metal, water): it becomes a live node (decays 4/s; touching it shocks).
static func o_charge_body(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive:
		return false
	FireLightning.charge_body(w, b, float(c.power if c.power > 0.0 else c.ch.E) * float(r.get("share", 0.5)), c.actor.id if c.actor != null else -1)
	res.pass_scale = float(r.get("pass", 1.0))
	_report(res, "conduct")
	return true


## A blast at least as strong as the tornado tears it apart (MOVESET §8.3 Combustion vs Tornado).
static func o_disrupt_zone(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive or t.body.form != Sim.Form.ZONE:
		return false
	w.emit("disrupt", {"body": t.body.id, "by": "blast"})
	w.close_zone(t.body, "disrupted")
	res.pass_scale = 1.0
	_report(res, "disrupt")
	return true


## "Fill the void": a blast that out-pushes a vacuum fills it and the vacuum collapses (the blast is spent).
static func o_fill_void(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive:
		return false
	w.emit("fill_void", {"body": t.body.id})
	if t.body.form == Sim.Form.ZONE:
		w.close_zone(t.body, "filled")
	res.stopped = true
	res.pass_scale = 0.0
	_report(res, "neutralize")
	return true


## No air, no blast: the detonation inside a vacuum is suppressed.
static func o_suppressed(_w: CombatWorld, _t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	c.heat = 0.0
	res.stopped = true
	res.pass_scale = 0.0
	_report(res, "extinguish")
	return true


## Lightning through sand: the grit fuses to glass (fulgurite) and takes half the bolt.
static func o_fulgurite(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive or t.body.mat != Sim.Mat.SAND:
		return false
	Outcomes.transform(w, t, c, res, {"to": "glass"}, ctx)
	res.pass_scale = float(r.get("pass", 0.5))
	res.stopped = false
	_report(res, "transform", "glass")
	return true


## Blue heat fuses loose sand to glass (the beam pays the fusing heat it has).
static func o_glassify(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or b.mat != Sim.Mat.SAND:
		return false
	var src := _fire_side(t, c)
	if src != null:
		res.heat_used = float(res.heat_used) + FireUtil.transfer(w, src, b, FireUtil._src_avail(src) * float(r.get("share", 0.5)))
	if b.alive and b.mat == Sim.Mat.SAND:
		w.convert_mat(b, Sim.Mat.GLASS, "sand_to_glass")
		w.emit("transform", {"body": b.id, "at": b.pos, "from": "sand", "to": "glass", "why": "blue"})
	res.pass_scale = 1.0
	_report(res, "transform", "glass")
	return true


## A connected conductor (a held water jet, a wave still touching its caster) carries the strike to its owner.
static func o_conduct_owner(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	res["conduct"] = true
	res.pass_scale = float(r.get("factor", 1.0))
	if t.body != null and t.body.alive and t.body.controller >= 0:
		res["conduct_to"] = t.body.controller
	_report(res, "conduct")
	return true


## Sand (and granular clouds) smother a fire on contact.
static func o_smother(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	return o_snuffed(w, t, c, res, r, ctx)


# ================================================================ Flame column

static func _flame_column() -> void:
	# --- aura_flame (Flame Guard / Heat Sink): the legacy cells answer every legacy threat; the rest is a plain
	# guard, except blue fire (Heat Sink x0.7) and new fire bodies.
	for t in ["metal", "molten_metal", "sand", "sand_cloud", "sand_surge", "water_wave", "mist", "steam", "vine", "blast",
			"tornado", "vacuum", "sound", "glass", "ember", "fire_field"]:
		_cell(t, "aura_flame", _plain_guard(), {"move": "flame_guard", "tier": 0, "expect": "block"})
	_cell("blue_fire", "aura_flame", _plain_guard({"perfect": "fire_guard_absorb", "share": 0.35, "perfect_share": 1.0}),
		{"move": "flame_guard", "tier": 0, "perfect": true, "expect": "fire_guard_absorb"})
	# --- flame (flare, blaze, Fire Column, Inferno, Fire Fan / Nova volumes) meeting loose bodies.
	_cell("metal", "flame", {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.3}, {"move": "fire_attack", "tier": 1, "expect": "fire_heat"})
	_cell("molten_metal", "flame", {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.2})
	_cell("sand", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 1, "expect": "pass"})
	_cell("sand_surge", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 1, "expect": "pass"})
	_cell("sand_cloud", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("glass", "flame", {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.3})
	# Water wave: only the Inferno (T3) boils part of it.
	_cell("water_wave", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 1, "expect": "pass"})
	_cell("water_wave", "flame", {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 0.5, "tiers": [3]},
		{"move": "fire_attack", "tier": 3, "expect": "fire_evaporate"})
	_cell("mist", "flame", {"outcome": "fire_evaporate", "partial": "fire_evaporate", "fail": "pass", "eff": 1.5, "share": 0.7},
		{"move": "fire_attack", "tier": 0, "expect": "fire_evaporate", "tp": 3.0})
	_cell("steam", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 1, "expect": "pass"})
	_cell("vine", "flame", {"outcome": "fire_burn", "partial": "fire_burn", "fail": "fire_burn", "eff": 3.0, "share": 0.9, "burn_kg": 3.0},
		{"move": "fire_attack", "tier": 1, "expect": "fire_burn"})
	_cell("fire_field", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("ember", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("tornado", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("vacuum", "flame", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	# --- fireball / comet as an obstacle or a clashing shot: it bursts on contact (its heat warms what it hit).
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "molten_metal", "sand", "glass",
			"ice", "vine", "gust", "blast", "sound", "ember"]:
		_cell(t, "fireball", {"bands": [[0.0, "fire_body_burst"]], "full_at": 0.0}, {"move": "fireball", "tier": 0, "expect": "fire_body_burst"})
	for t in ["water", "water_wave", "mist", "steam"]:
		_cell(t, "fireball", {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0}, {"move": "fireball", "tier": 0, "expect": "fire_snuffed"})
	_cell("vacuum", "fireball", {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0}, {"move": "fireball", "tier": 0, "expect": "fire_snuffed"})
	_cell("sand_cloud", "fireball", {"bands": [[0.0, "fire_smother"]], "full_at": 0.0})
	_cell("flame", "fireball", {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("blue_fire", "fireball", {"bands": [[0.0, "pass"]], "full_at": 0.0})


# ================================================================ fire fields (Fire Column / Inferno / Fire Line / trails)

static func _field_column() -> void:
	var c := "fire_field"
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "metal", "glass", "lava_wave", "magma"]:
		_cell(t, c, {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.12})
	for t in ["water", "ice", "puddle"]:
		_cell(t, c, {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 0.25})
	_cell("mist", c, {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 0.4})
	_cell("vine", c, {"bands": [[0.0, "fire_burn"]], "full_at": 0.0, "share": 0.3, "burn_kg": 1.0})
	# A water wave douses the field (and boils some water on the way).
	_cell("water_wave", c, {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 2, "expect": "fire_snuffed"})
	# Sand smothers it.
	for t in ["sand", "sand_cloud", "sand_surge"]:
		_cell(t, c, {"bands": [[0.0, "fire_smother"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 2, "expect": "fire_smother"})
	# Wind: feeds below 1x, blows it aside 1-2x, snuffs it >= 2x (ratio here = field / gust: inverted bands).
	_cell("gust", c, {"bands": [[0.0, "fire_snuffed"], [0.5, "fire_blown"], [1.0, "fire_fanned"]], "amp": 1.3},
		{"move": "fire_attack", "tier": 2, "expect": "fire_fanned", "tp": 7.0})
	# A tornado in a fire becomes a fire tornado; vacuum and blasts snuff it.
	_cell("tornado", c, {"bands": [[0.0, "fire_tornado"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 2, "expect": "fire_tornado"})
	_cell("vacuum", c, {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0}, {"move": "fire_attack", "tier": 2, "expect": "fire_snuffed"})
	_cell("blast", c, {"bands": [[0.0, "fire_snuffed"], [1.0, "fire_dampen"]], "eff": 0.67},
		{"move": "fire_attack", "tier": 2, "expect": "fire_snuffed"})
	for t in ["flame", "blue_fire", "fire_field", "ember", "lightning", "sound", "steam"]:
		_cell(t, c, {"bands": [[0.0, "pass"]], "full_at": 0.0})


# ================================================================ techniques: heat_grip, draw_heat, heat_ranged

static func _technique_cells() -> void:
	# Thermal (magma grip) also works metal (melts to molten metal) and sand (fuses; it sets as glass).
	_cell("metal", "heat_grip", {"outcome": "transform", "bands": [[0.0, "transform"]], "full_at": 0.0, "to": "molten_metal"},
		{"move": "fire_tech", "tier": 0, "expect": "transform"})
	_cell("sand", "heat_grip", {"outcome": "transform", "bands": [[0.0, "transform"]], "full_at": 0.0, "to": "glass"},
		{"move": "fire_tech", "tier": 0, "expect": "transform"})
	# DRAW also pulls the heat out of molten metal.
	_cell("molten_metal", "draw_heat", {"outcome": "absorb", "bands": [[0.0, "absorb"]], "full_at": 0.0},
		{"move": "fire_tech", "tier": 0, "expect": "absorb"})
	# Scorch / Smelter / Kiln (ranged heat without a grip; VerbHeat applies it, the cells document the result).
	var c := "heat_ranged"
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "magma"]:
		_cell(t, c, {"bands": [[0.0, "heat"]], "full_at": 0.0}, {"move": "smelter", "tier": 0, "expect": "heat"})
	_cell("metal", c, {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "molten_metal"}, {"move": "smelter", "tier": 0, "expect": "transform"})
	_cell("sand", c, {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "glass"}, {"move": "smelter", "tier": 0, "expect": "transform"})
	_cell("ice", c, {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "steam"}, {"move": "smelter", "tier": 0, "expect": "transform"})
	_cell("water", c, {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "steam"})
	_cell("vine", c, {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "ash"})
	for t in ["wall_stone", "wall_obsidian", "wall_glass", "wall_sand", "wall_ice", "plate_metal", "wall_vine", "wall_mud"]:
		_cell(t, c, {"bands": [[0.0, "heat"]], "full_at": 0.0}, {"move": "smelter", "tier": 0, "expect": "heat"})
	for t in ["fire_field", "flame", "blue_fire", "ember", "gust", "tornado", "vacuum", "sound", "lightning", "blast", "steam", "mist"]:
		_cell(t, c, {"bands": [[0.0, "pass"]], "full_at": 0.0})


# ================================================================ Blue column

static func _blue_column() -> void:
	# --- aura_blue (Blue Aegis CP 16; Corona zones).
	var c := "aura_blue"
	_cell("*", c, _plain_guard({"aura": true}))
	for t in ["stone", "stone_heavy", "boulder", "magma", "lava_wave", "sand", "sand_surge", "water", "water_wave", "lightning",
			"blast", "gust", "tornado", "vacuum", "sound", "steam", "glass"]:
		_cell(t, c, _plain_guard({"aura": true}), {"move": "blue_aegis", "tier": 0, "expect": "block"})
	# Stones arrive as hot rock (partial: they lose speed, gain heat) - the guard still blocks.
	_cell("hot_rock", c, _plain_guard({"aura": true}), {"move": "blue_aegis", "tier": 0, "expect": "block"})
	for t in ["metal", "ice"]:
		_cell(t, c, {"outcome": "fire_aegis_melt", "partial": "block", "fail": "block", "fallback": "block", "mass_max": 8.0,
			"chip": 0.12, "bal": 0.55, "knock": 0.35, "aura": true},
			{"move": "blue_aegis", "tier": 0, "expect": "fire_aegis_melt", "tp": 8.0 if t == "metal" else 9.0})
	for t in ["flame", "blue_fire", "fire_field", "ember"]:
		_cell(t, c, {"bands": [[0.0, "fire_guard_absorb"]], "full_at": 0.0, "share": 0.5, "perfect_share": 1.5, "aura": true},
			{"move": "blue_aegis", "tier": 0, "expect": "fire_guard_absorb"})
	_cell("vine", c, {"bands": [[0.0, "fire_burn"]], "full_at": 0.0, "share": 1.0, "burn_kg": 4.0, "aura": true},
		{"move": "blue_aegis", "tier": 0, "expect": "fire_burn"})
	_cell("mist", c, {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 0.6})
	# --- blue_fire (Blue Needle / Lance / Searing Beam / White Core, Flash Over) meeting loose bodies.
	c = "blue_fire"
	_cell("stone", c, {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.5}, {"move": "blue_needle", "tier": 1, "expect": "fire_heat"})
	_cell("stone", c, {"bands": [[0.0, "fire_melt"]], "full_at": 0.0, "tiers": [2, 3]}, {"move": "blue_needle", "tier": 2, "expect": "fire_melt"})
	_cell("stone_heavy", c, {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.5})
	_cell("stone_heavy", c, {"bands": [[0.0, "fire_melt"]], "full_at": 0.0, "tiers": [3]}, {"move": "blue_needle", "tier": 3, "expect": "fire_melt"})
	_cell("boulder", c, {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.6}, {"move": "blue_needle", "tier": 3, "expect": "fire_heat"})
	_cell("hot_rock", c, {"bands": [[0.0, "fire_melt"]], "full_at": 0.0}, {"move": "blue_needle", "tier": 1, "expect": "fire_melt"})
	_cell("magma", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "blue_needle", "tier": 2, "expect": "pass"})
	_cell("lava_wave", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "blue_needle", "tier": 2, "expect": "pass"})
	_cell("metal", c, {"bands": [[0.0, "fire_melt"]], "full_at": 0.0, "to_liquid": 1.0}, {"move": "blue_needle", "tier": 1, "expect": "fire_melt"})
	_cell("sand", c, {"bands": [[0.0, "fire_glassify"]], "full_at": 0.0, "share": 0.5}, {"move": "blue_needle", "tier": 2, "expect": "fire_glassify"})
	_cell("sand_cloud", c, {"bands": [[0.0, "fire_glassify"]], "full_at": 0.0, "share": 0.5})
	_cell("water", c, {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 0.8}, {"move": "blue_needle", "tier": 1, "expect": "fire_evaporate"})
	_cell("water_wave", c, {"outcome": "fire_evaporate", "partial": "fire_evaporate", "fail": "fire_evaporate", "share": 0.6},
		{"move": "blue_needle", "tier": 2, "expect": "fire_evaporate"})
	_cell("ice", c, {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 1.0}, {"move": "blue_needle", "tier": 0, "expect": "fire_evaporate"})
	_cell("mist", c, {"bands": [[0.0, "fire_evaporate"]], "full_at": 0.0, "share": 0.8}, {"move": "blue_needle", "tier": 0, "expect": "fire_evaporate"})
	_cell("steam", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "blue_needle", "tier": 1, "expect": "pass"})
	_cell("vine", c, {"bands": [[0.0, "fire_burn"]], "full_at": 0.0, "share": 1.0, "burn_kg": 5.0}, {"move": "blue_needle", "tier": 0, "expect": "fire_burn"})
	_cell("glass", c, {"bands": [[0.0, "fire_heat"]], "full_at": 0.0, "share": 0.5})
	for t in ["flame", "blue_fire", "fire_field", "ember", "tornado", "vacuum", "gust", "sound"]:
		_cell(t, c, {"bands": [[0.0, "pass"]], "full_at": 0.0})
	# --- Blue Furrow / Magma Rift (lava channel): its front fuses a sand surge to glass from T2.
	_cell("sand_surge", "magma_rift", {"bands": [[0.0, "fire_glassify"]], "full_at": 0.0, "share": 0.4, "tiers": [2, 3]},
		{"move": "blue_furrow", "tier": 2, "expect": "fire_glassify"})
	_cell("sand_surge", "magma_rift", {"outcome": "clash", "partial": "clash", "fail": "clash"})


# ================================================================ Lightning column

static func _lightning_column() -> void:
	# --- ward_static (Static Ward CP 12; static fields): bolts are stored, metal deflected x1.5, the rest a guard.
	var c := "ward_static"
	_cell("*", c, _plain_guard())
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "sand", "sand_surge", "water", "water_wave",
			"ice", "mist", "steam", "vine", "flame", "blue_fire", "blast", "gust", "tornado", "vacuum", "sound", "glass"]:
		_cell(t, c, _plain_guard(), {"move": "static_ward", "tier": 0, "expect": "block"})
	_cell("metal", c, {"outcome": "deflect", "perfect": "reflect", "partial": "block", "fail": "block", "eff": 1.5,
		"chip": 0.12, "bal": 0.55, "knock": 0.35}, {"move": "static_ward", "tier": 0, "expect": "deflect"})
	_cell("lightning", c, {"bands": [[0.0, "fire_static"]], "full_at": 0.0, "share": 0.5, "perfect": "redirect",
		"requires": "redirect_current", "factor": 0.8, "fallback": "fire_static_full", "aura": true},
		{"move": "static_ward", "tier": 0, "expect": "fire_static"})
	# --- lightning (bolts, sparks, arcs) meeting bodies on their path.
	c = "lightning"
	_cell("stone", c, {"inert": "pass", "outcome": "shatter", "partial": "pass", "fail": "pass", "pieces": 3},
		{"move": "spark", "tier": 1, "expect": "shatter"})
	_cell("stone_heavy", c, {"inert": "pass", "outcome": "shatter", "partial": "pass", "fail": "pass", "pieces": 3},
		{"move": "spark", "tier": 2, "expect": "shatter", "tp": 32.0})
	_cell("boulder", c, {"inert": "pass", "bands": [[0.0, "pass"], [0.45, "shatter"]], "pieces": 2}, {"move": "spark", "tier": 3, "expect": "shatter"})
	_cell("hot_rock", c, {"inert": "pass", "outcome": "shatter", "partial": "pass", "fail": "pass", "pieces": 3}, {"move": "spark", "tier": 2, "expect": "shatter"})
	_cell("magma", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "spark", "tier": 2, "expect": "pass"})
	_cell("lava_wave", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "spark", "tier": 2, "expect": "pass"})
	_cell("metal", c, {"bands": [[0.0, "fire_charge_body"]], "full_at": 0.0, "share": 0.5}, {"move": "spark", "tier": 0, "expect": "fire_charge_body"})
	_cell("molten_metal", c, {"bands": [[0.0, "fire_charge_body"]], "full_at": 0.0, "share": 0.5})
	_cell("sand", c, {"bands": [[0.0, "fire_fulgurite"]], "full_at": 0.0, "pass": 0.5}, {"move": "spark", "tier": 1, "expect": "fire_fulgurite"})
	_cell("sand_surge", c, {"bands": [[0.0, "ground"]], "full_at": 0.0, "factor": 0.0}, {"move": "spark", "tier": 1, "expect": "ground"})
	_cell("sand_cloud", c, {"bands": [[0.0, "fire_fulgurite"]], "full_at": 0.0, "pass": 0.5})
	_cell("water", c, {"bands": [[0.0, "fire_conduct_owner"]], "full_at": 0.0}, {"move": "rail_arc", "tier": 0, "expect": "fire_conduct_owner"})
	_cell("water_wave", c, {"bands": [[0.0, "fire_conduct_owner"]], "full_at": 0.0}, {"move": "rail_arc", "tier": 0, "expect": "fire_conduct_owner"})
	_cell("ice", c, {"inert": "pass", "outcome": "shatter", "partial": "shatter", "fail": "pass", "pieces": 3}, {"move": "spark", "tier": 0, "expect": "shatter"})
	_cell("mist", c, {"bands": [[0.0, "conduct"]], "full_at": 0.0, "factor": 0.6}, {"move": "spark", "tier": 1, "expect": "conduct"})
	_cell("steam", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "spark", "tier": 1, "expect": "pass"})
	_cell("vine", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "spark", "tier": 0, "expect": "pass"})
	_cell("vine", c, {"bands": [[0.0, "fire_burn"]], "full_at": 0.0, "share": 0.0, "burn_kg": 2.0, "tiers": [1, 2, 3]},
		{"move": "spark", "tier": 1, "expect": "fire_burn"})
	for t in ["flame", "blue_fire", "fire_field", "ember", "gust", "vacuum", "sound", "blast", "glass"]:
		_cell(t, c, {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("tornado", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "spark", "tier": 1, "expect": "pass"})
	# --- ground_current (Ground Current / Storm Grid waves): water and metal on its way carry it.
	c = "ground_current"
	for t in ["water", "water_wave", "metal", "molten_metal"]:
		_cell(t, c, {"bands": [[0.0, "fire_charge_body"]], "full_at": 0.0, "share": 0.5})
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "sand", "ice", "glass", "vine", "mist", "steam"]:
		_cell(t, c, {"bands": [[0.0, "pass"]], "full_at": 0.0})


# ================================================================ Combustion column

static func _combustion_column() -> void:
	# --- guard_blast (Reactive Blast CP 18, 8 Focus per trigger).
	var c := "guard_blast"
	_cell("*", c, _plain_guard())
	for t in ["stone", "hot_rock", "metal", "glass", "ice", "sand", "water"]:
		_cell(t, c, {"outcome": "fire_reactive", "partial": "block", "fail": "block", "fallback": "block", "eff": 2.0 if t == "sand" else 1.0,
			"chip": 0.12, "bal": 0.55, "knock": 0.35}, {"move": "reactive_blast", "tier": 0, "expect": "block" if t == "hot_rock" else "fire_reactive"})
	for t in ["stone_heavy", "boulder", "magma", "lava_wave", "sand_surge", "water_wave", "lightning", "vine"]:
		_cell(t, c, _plain_guard(), {"move": "reactive_blast", "tier": 0, "expect": "block"})
	_cell("flame", c, {"outcome": "fire_reactive", "partial": "block", "fail": "block", "eff": 1.5, "chip": 0.12, "bal": 0.55, "knock": 0.35},
		{"move": "reactive_blast", "tier": 0, "expect": "fire_reactive"})
	_cell("blue_fire", c, {"outcome": "fire_reactive", "partial": "weaken", "fail": "block", "eff": 1.0, "chip": 0.12, "bal": 0.55, "knock": 0.35},
		{"move": "reactive_blast", "tier": 0, "expect": "fire_reactive", "tp": 16.0})
	for t in ["fire_field", "ember", "mist", "steam", "sand_cloud"]:
		_cell(t, c, {"outcome": "fire_reactive", "partial": "block", "fail": "block", "eff": 2.0, "chip": 0.12, "bal": 0.55, "knock": 0.35})
	_cell("blast", c, {"outcome": "fire_counter_blast", "partial": "weaken", "fail": "block", "fallback": "block",
		"chip": 0.12, "bal": 0.55, "knock": 0.35}, {"move": "reactive_blast", "tier": 0, "expect": "fire_counter_blast"})
	_cell("gust", c, {"outcome": "fire_reactive", "partial": "block", "fail": "block", "chip": 0.12, "bal": 0.55, "knock": 0.35},
		{"move": "reactive_blast", "tier": 0, "expect": "fire_reactive"})
	_cell("sound", c, {"outcome": "block", "partial": "weaken", "fail": "weaken", "eff": 0.6, "chip": 0.12, "bal": 0.55, "knock": 0.35},
		{"move": "reactive_blast", "tier": 0, "expect": "weaken"})
	# --- blast (Pop / Burst / Blast / Detonation, Shockwave, Chain Blasts, mines) meeting loose bodies.
	c = "blast"
	_cell("stone", c, {"inert": "push", "outcome": "deflect", "partial": "bend", "fail": "pass", "side": 0.8, "up": 3.0}, {"move": "pop", "tier": 2, "expect": "deflect"})
	_cell("stone", c, {"inert": "push", "outcome": "shatter", "partial": "deflect", "fail": "pass", "pieces": 3, "tiers": [3]}, {"move": "pop", "tier": 3, "expect": "shatter"})
	_cell("stone_heavy", c, {"inert": "push", "outcome": "weaken", "partial": "weaken", "fail": "pass"}, {"move": "pop", "tier": 2, "expect": "weaken", "tp": 32.0})
	_cell("stone_heavy", c, {"inert": "push", "outcome": "shatter", "partial": "weaken", "fail": "pass", "pieces": 3, "tiers": [3]},
		{"move": "pop", "tier": 3, "expect": "shatter", "tp": 32.0})
	_cell("boulder", c, {"inert": "push", "outcome": "weaken", "partial": "weaken", "fail": "weaken", "full_at": 0.25, "partial_at": 0.0},
		{"move": "pop", "tier": 3, "expect": "weaken"})
	_cell("hot_rock", c, {"inert": "push", "outcome": "deflect", "partial": "bend", "fail": "pass", "side": 0.8, "up": 3.0}, {"move": "pop", "tier": 3, "expect": "deflect"})
	_cell("magma", c, {"inert": "push", "outcome": "shatter", "partial": "weaken", "fail": "pass", "pieces": 3}, {"move": "pop", "tier": 3, "expect": "weaken"})
	_cell("lava_wave", c, {"outcome": "weaken", "partial": "weaken", "fail": "pass"}, {"move": "pop", "tier": 2, "expect": "weaken"})
	_cell("metal", c, {"inert": "push", "outcome": "deflect", "partial": "bend", "fail": "pass", "side": 0.8, "up": 3.0}, {"move": "pop", "tier": 1, "expect": "deflect"})
	_cell("sand", c, {"inert": "push", "outcome": "deflect", "partial": "bend", "fail": "pass", "eff": 2.0, "side": 1.0, "up": 2.0}, {"move": "pop", "tier": 0, "expect": "deflect"})
	_cell("sand_cloud", c, {"outcome": "disperse", "partial": "disperse", "fail": "pass", "eff": 2.0}, {"move": "pop", "tier": 0, "expect": "disperse"})
	_cell("sand_surge", c, {"outcome": "weaken", "partial": "weaken", "fail": "pass"}, {"move": "chain_blasts", "tier": 1, "expect": "weaken"})
	_cell("water", c, {"inert": "push", "outcome": "deflect", "partial": "deflect", "fail": "pass", "side": 1.0, "up": 3.0}, {"move": "pop", "tier": 0, "expect": "deflect"})
	_cell("water_wave", c, {"outcome": "weaken", "partial": "weaken", "fail": "pass"}, {"move": "pop", "tier": 2, "expect": "weaken"})
	_cell("ice", c, {"inert": "push", "outcome": "shatter", "partial": "shatter", "fail": "pass", "eff": 2.0, "pieces": 3}, {"move": "pop", "tier": 0, "expect": "shatter"})
	_cell("glass", c, {"inert": "push", "outcome": "shatter", "partial": "shatter", "fail": "pass", "eff": 2.0, "pieces": 3}, {"move": "pop", "tier": 0, "expect": "shatter"})
	_cell("mist", c, {"outcome": "disperse", "partial": "disperse", "fail": "disperse"}, {"move": "pop", "tier": 0, "expect": "disperse"})
	_cell("steam", c, {"outcome": "disperse", "partial": "disperse", "fail": "pass", "eff": 0.5}, {"move": "pop", "tier": 1, "expect": "disperse"})
	_cell("vine", c, {"inert": "push", "outcome": "shatter", "partial": "weaken", "fail": "pass", "eff": 1.5, "pieces": 2}, {"move": "pop", "tier": 1, "expect": "shatter"})
	_cell("flame", c, {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0, "eff": 1.5}, {"move": "pop", "tier": 0, "expect": "fire_snuffed"})
	_cell("blue_fire", c, {"outcome": "fire_snuffed", "partial": "pass", "fail": "pass"}, {"move": "pop", "tier": 2, "expect": "fire_snuffed"})
	_cell("ember", c, {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0})
	_cell("fire_field", c, {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0})
	# Detonations at a tornado (disrupted if P >= its P) or in a vacuum (suppressed; filled if P >= its pull).
	_cell("tornado", c, {"bands": [[0.0, "pass"], [1.0, "fire_disrupt_zone"]]}, {"move": "pop", "tier": 3, "expect": "fire_disrupt_zone"})
	_cell("vacuum", c, {"bands": [[0.0, "fire_suppressed"], [1.0, "fire_fill_void"]]}, {"move": "pop", "tier": 1, "expect": "fire_suppressed", "tp": 18.0})
	_cell("gust", c, {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_cell("sound", c, {"outcome": "weaken", "partial": "weaken", "fail": "pass", "eff": 0.6}, {"move": "pop", "tier": 3, "expect": "weaken"})
	_cell("lightning", c, {"bands": [[0.0, "pass"]], "full_at": 0.0})
	# --- ember (a Spark Mine / Scatter Charge as an obstacle): it pops on contact.
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "metal", "glass", "ice", "sand", "flame", "blue_fire", "blast", "gust", "magma"]:
		_cell(t, "ember", {"bands": [[0.0, "fire_body_burst"]], "full_at": 0.0})
	for t in ["water", "water_wave", "mist", "steam", "vacuum", "sand_cloud"]:
		_cell(t, "ember", {"bands": [[0.0, "fire_snuffed"]], "full_at": 0.0})
