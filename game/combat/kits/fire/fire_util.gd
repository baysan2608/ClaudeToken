class_name FireUtil
extends RefCounted
## Shared helpers of the Fire kit. Every heat change is booked: heat comes from the caster (CombatWorld.pay_heat ->
## ledger generated / the reserve), from a FIRE body's heat_payload, or from a volume's paid budget, and goes into a
## body through CombatWorld.heat_body / boil_water (what a body can't take is returned to its source or booked spent).

const E := 2
const FIELD_STATUS_T := 0.6
const VAPOR_TAGS := ["fog", "mist", "steam", "steam_screen", "geyser"]
const GUST_TAGS := ["wind_guard", "gust", "wind_wall"]
const DAMPEN := 0.5        # blasts inside mist / steam
const FAN := 1.3           # blasts inside moving air: +30 % radius
const INRUSH := 1.5        # blasts right after a vacuum collapses
const INRUSH_TICKS := 30   # "right after": within 0.5 s


static func s(frames: float) -> float:
	return frames / 60.0


# ------------------------------------------------------------------ def plumbing

## Runs the rest of an action with some def parameters replaced (Charge.param reads inst.data.spec_def first).
static func with_params(inst: ActionInst, over: Dictionary) -> void:
	var d: Dictionary = Charge.pdef(inst).duplicate(true)
	for k in over:
		d[k] = over[k]
	var tiers: Dictionary = d.get("tiers", {})
	for tk in tiers:
		for k in over:
			(tiers[tk] as Dictionary).erase(k)
	inst.data["spec_def"] = d


## The aim point of an action clamped to `rmax` (horizontal) on the ground: the locked rival unless the player drags
## an aim, else along the aim.
static func aim_ground(w: CombatWorld, a: ActorState, inst: ActionInst, rmax: float) -> Vector3:
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	var ap: Vector3 = inst.data.get("aim_point", a.chest() + dir * rmax)
	var t := w.get_actor(a.lock_target)
	if t != null and not inst.data.get("aim_active", false):
		ap = t.pos
	var flat := Vector3(ap.x - a.pos.x, 0.0, ap.z - a.pos.z)
	if flat.length() > rmax:
		flat = flat.normalized() * rmax
	var p := a.pos + flat
	p.y = w.arena.ground_height(p.x, p.z, a.pos.y + 0.5)
	return p


# ------------------------------------------------------------------ booked heat

static func _src_avail(src: Agent) -> float:
	if src.body != null and src.body.alive and (src.body.mat == Sim.Mat.FIRE or src.body.mat == Sim.Mat.AIR):
		return maxf(0.0, src.body.heat_payload)
	return maxf(0.0, src.heat)


## Moves up to `hu` of heat from a source agent into body `dst`. The source is a FIRE body / zone (its payload) or a
## volume (its paid budget `heat`). Liquid water is flash-boiled at the contact (boil_water), everything else is
## heated (heat_body; what it can't take stays in the source). Returns the HU that left the source.
static func transfer(w: CombatWorld, src: Agent, dst: MatBody, hu: float, boil: bool = true) -> float:
	if src == null or dst == null or not dst.alive or hu <= 0.0:
		return 0.0
	var want := minf(hu, _src_avail(src))
	if want <= 1e-6:
		return 0.0
	var used := 0.0
	if boil and dst.is_water() and dst.phase == Sim.Phase.LIQUID:
		w.boil_water(dst, want, dst.pos)   # what the water can't take is booked as ambient inside boil_water
		used = want
		if dst.mass <= 0.05 and dst.form != Sim.Form.POOL and dst.alive:
			w.decay_body(dst, "boiled")
	else:
		used = w.heat_body(dst, want)
	if src.body != null and src.body.alive and (src.body.mat == Sim.Mat.FIRE or src.body.mat == Sim.Mat.AIR):
		src.body.heat_payload = maxf(0.0, src.body.heat_payload - used)
	src.heat = maxf(0.0, src.heat - used)
	return used


## Heat that brings a fusible body to `to_liquid` melt fraction, or melts ice and warms the water to boiling.
static func melt_need(b: MatBody, to_liquid: float = 1.0) -> float:
	if b.is_water():
		return maxf(0.0, -b.thermal_energy()) + b.mass * Sim.WATER_C * maxf(0.0, Sim.WATER_BOIL_C - maxf(b.temp, 0.0))
	if not Materials.is_fusible(b.mat):
		return 0.0
	var c := Materials.c(b.mat)
	var sens := maxf(0.0, Materials.melt(b.mat) - b.temp) * b.mass * c
	var lat := maxf(0.0, to_liquid - b.liquid) * b.mass * Materials.latent(b.mat)
	return sens + lat


## The caster pays up to `hu` (reserve first, then Focus) and heats `dst` with it; heat the body can't take is
## booked spent. Returns the HU used.
static func pay_into(w: CombatWorld, a: ActorState, dst: MatBody, hu: float, allow_partial: bool = true) -> float:
	if hu <= 0.0 or dst == null or not dst.alive:
		return 0.0
	var paid := w.pay_heat(a, hu, allow_partial)
	if paid <= 0.0:
		return 0.0
	var used := 0.0
	if dst.is_water() and dst.phase == Sim.Phase.LIQUID:
		w.boil_water(dst, paid, dst.pos)
		used = paid
		if dst.mass <= 0.05 and dst.alive and dst.form != Sim.Form.POOL:
			w.decay_body(dst, "boiled")
	else:
		used = w.heat_body(dst, paid)
	w.ledger.spent += paid - used
	return used


# ------------------------------------------------------------------ fire fields

## A burning patch (ZONE, mat FIRE, tag fire_field) holding `heat_hu` of already-paid heat. Fighters inside (not the
## owner) burn; bodies inside are met by the fire_field cells (warmed, melted, boiled, burned); wind fans or snuffs it,
## water douses it, blasts and vacuum snuff it. The payload decays 35 %/s to the air (ledger ambient).
static func spawn_field(w: CombatWorld, owner_id: int, pos: Vector3, radius: float, life: float, heat_hu: float,
		blue: bool = false, tier: int = 0, tag: StringName = &"fire_field") -> MatBody:
	var p := pos
	p.y = w.arena.ground_height(p.x, p.z, p.y + 0.5)
	var z := w.spawn_zone(tag, p, radius, owner_id, 0.0, Sim.Mat.FIRE, 0.0, life, "fire:%d" % owner_id)
	z.heat_payload = maxf(0.0, heat_hu)
	z.tier = tier
	z.sub = 1 if blue else 0
	z.props["actor_status"] = "burning"
	z.props["status_t"] = FIELD_STATUS_T
	z.props["status_mag"] = 1.5 if blue else 1.0
	z.props["height"] = 2.2
	z.props["rate"] = 0.15
	if blue:
		z.props["blue"] = true
	return z


## Counter power of a fire field / fire body: its live heat in PU (the channel hook keeps Agents in sync).
static func field_power(b: MatBody) -> float:
	return maxf(0.1, b.heat_payload / Interactions.HU_PER_PU)


## Zone effect of fire fields: ride a carrier (props.follow: a tornado or a body), go out when the heat is gone.
static func field_tick(w: CombatWorld, z: MatBody, _dt: float) -> void:
	var fid := int(z.props.get("follow", -1))
	if fid >= 0:
		var f := w.get_body(fid)
		if f == null or not f.alive:
			w.close_zone(z, "carrier_gone")
			return
		z.pos = Vector3(f.pos.x, w.arena.ground_height(f.pos.x, f.pos.z, f.pos.y + 0.5), f.pos.z)
		z.vel = f.vel
		if f.form == Sim.Form.ZONE:
			z.zone_radius = maxf(z.zone_radius, f.zone_radius * 0.8)
			z.radius = z.zone_radius
	if z.heat_payload < 2.0 and z.age > 0.2:
		w.close_zone(z, "burned_out")


# ------------------------------------------------------------------ detonations (Combustion)

## Zones of a threat class around a point.
static func zones_at(w: CombatWorld, p: Vector3, classes: Array, r: float = 0.2) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in w.bodies:
		if not b.alive or b.form != Sim.Form.ZONE:
			continue
		if not classes.has(String(Interactions.classify(b))) and not classes.has(String(b.tag)):
			continue
		if w._in_zone(b, p, r):
			out.append(b)
	return out


## True when vapour (fog, mist, steam zones or steam / mist clouds) fills the point.
static func in_vapor(w: CombatWorld, p: Vector3) -> bool:
	for b in w.bodies:
		if not b.alive:
			continue
		if b.form == Sim.Form.ZONE and (VAPOR_TAGS.has(String(b.tag)) or b.mat == Sim.Mat.STEAM) and w._in_zone(b, p, 0.3):
			return true
		if b.form == Sim.Form.CLOUD and (b.mat == Sim.Mat.STEAM or b.is_water()) and b.pos.distance_to(p) < maxf(b.radius, 1.2) + 0.5:
			return true
	return false


## True when moving air (a gust zone, a wind body passing) is at the point: the blast is fanned (+30 % radius).
static func in_wind(w: CombatWorld, p: Vector3) -> bool:
	for b in w.bodies:
		if not b.alive or b.mat != Sim.Mat.AIR:
			continue
		if String(Interactions.classify(b)) != "gust":
			continue
		if b.form == Sim.Form.ZONE:
			if w._in_zone(b, p, 0.3):
				return true
		elif b.pos.distance_to(p) < b.radius + 1.0:
			return true
	return false


## A volume carrier for a detonation (tier, move id and attack instance; no def counter power so the blast's own,
## modified power counts).
static func blast_inst(w: CombatWorld, move_id: String, tier: int, attack_id: int = 0, sub: int = 3) -> ActionInst:
	var fi := ActionInst.new()
	fi.id = move_id
	fi.def = {"counter": {"cls": "blast"}, "fx": {"mat": "blast"}, "element": E, "sub": sub}
	fi.element = E
	fi.sub = sub
	fi.attack_id = attack_id if attack_id != 0 else w.new_attack_id()
	fi.data["tier"] = tier
	fi.phase = ActionInst.P.ACTIVE
	return fi


## A detonation of Combustion at `p` (MOVESET §7.12): vacuum suppresses it (a well it can out-push is filled and
## collapses instead), vapour halves it, moving air fans it (+30 % radius), the air inrush right after a vacuum
## collapses boosts it x1.5, a tornado at the point is disrupted (P >= the tornado's) - or, with mode "fuse",
## ignited into a fire tornado. prm: radius power damage balance knock lift heat_hu status status_t (+ mat).
## Heat in prm.heat_hu is spent here (ledger spent / carried into a fire tornado). Returns
## {suppressed, power, radius, mods: [..], tornado}.
static func detonate(w: CombatWorld, a: ActorState, inst: ActionInst, p: Vector3, prm: Dictionary, mode: String = "strike",
		inrush_tick: int = -1000000) -> Dictionary:
	var out := {"suppressed": false, "power": float(prm.get("power", 8.0)), "radius": float(prm.get("radius", 2.0)), "mods": [], "tornado": -1}
	var power := float(prm.get("power", 8.0))
	var heat := float(prm.get("heat_hu", 0.0))
	var tier := inst.tier() if inst != null else int(prm.get("tier", 0))
	# The blast volume carries its tier and move (rule cells by tier) but its own power (modifiers apply to it).
	inst = blast_inst(w, inst.id if inst != null else String(prm.get("move", "")), tier, inst.attack_id if inst != null else 0)
	var vol := Agent.of_volume(w, a, inst, &"blast", p, Vector3.ZERO, {"P": power, "heat_hu": heat})
	vol.power = power
	vol.tier = tier
	# 1 vacuum: no air, no blast.
	for z in zones_at(w, p, ["vacuum"]):
		var th := Agent.of_body(w, z)
		var res := Interactions.resolve(w, th, vol, {"site": "detonation"})
		out.suppressed = true
		(out.mods as Array).append("vacuum:" + String(res.outcome))
		FxEvents.fx(w, "burst", "vacuum", {"actor": a.id if a != null else -1, "pos": p, "radius": out.radius * 0.4, "power": power,
			"shape": "small", "tier": tier})
		w.ledger.spent += heat
		w.emit("blast_suppressed", {"actor": a.id if a != null else -1, "pos": p, "by": z.id, "outcome": String(res.outcome)})
		return out
	# 2 tornado: disrupted by a strong enough blast, or set alight by a fuse.
	for z in zones_at(w, p, ["tornado"]):
		if mode == "fuse":
			var f := spawn_field(w, a.id if a != null else -1, z.pos, maxf(1.5, z.zone_radius * 0.8), maxf(2.0, z.max_life - z.age if z.max_life > 0.0 else 4.0), heat, false, tier)
			f.props["follow"] = z.id
			f.props["spare_owner"] = true
			z.props["fire"] = true
			z.props["infused"] = "fire"
			out.tornado = z.id
			(out.mods as Array).append("fire_tornado")
			w.emit("infuse", {"actor": a.id if a != null else -1, "body": z.id, "with": "fire", "field": f.id})
			FxEvents.fx(w, "burst", "flame", {"actor": a.id if a != null else -1, "pos": p, "radius": z.zone_radius, "power": power, "tier": tier})
			return out
		var res2 := Interactions.resolve(w, Agent.of_body(w, z), vol, {"site": "detonation"})
		(out.mods as Array).append("tornado:" + String(res2.outcome))
	# 3 modifiers.
	if w.tick - inrush_tick <= INRUSH_TICKS:
		power *= INRUSH
		(out.mods as Array).append("inrush")
	if in_vapor(w, p):
		power *= DAMPEN
		(out.mods as Array).append("vapor")
	var r := float(prm.get("radius", 2.0))
	if in_wind(w, p):
		r *= FAN
		(out.mods as Array).append("wind")
	var q := prm.duplicate()
	q["power"] = power
	q["radius"] = r
	q["cls"] = "blast"
	q["mat"] = "blast"
	var k := power / maxf(float(prm.get("power", 8.0)), 0.01)
	q["damage"] = float(prm.get("damage", 8.0)) * k
	q["balance"] = float(prm.get("balance", 20.0)) * k
	VerbVolume.burst_at(w, a, inst, p, q)
	out.power = power
	out.radius = r
	return out
