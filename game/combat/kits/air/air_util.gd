class_name AirUtil
extends RefCounted
## Shared helpers of the Air kit: aim, ground points, zones, body queries, arena reflection (Sound Lance bank
## shots), actor shoves that respect immunities, tornado infusions, the vacuum inrush flag. Every mass and heat
## change goes through the CombatWorld ledger helpers (heat_body, decay_body, split_body, convert_mat); wind
## itself is not matter (AIR bodies and zones carry mass 0).

const E := 3
const LIGHT := 30.0                 # kg: the vortex captures / the wind grips bodies up to this mass
const INRUSH_LIFE := 0.6            # s: how long the air-inrush zone left by a collapsing well lives
const NO_PAIR := 99.0               # props.rate that keeps the core zone pass away from a zone's bodies


static func f(n: float) -> float:
	return n / 60.0


# ------------------------------------------------------------------ aim / ground

static func aim_flat(a: ActorState, inst: ActionInst) -> Vector3:
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	dir.y = 0.0
	return dir.normalized() if dir.length() > 0.01 else a.forward()


static func ground_at(w: CombatWorld, p: Vector3) -> Vector3:
	return Vector3(p.x, w.arena.ground_height(p.x, p.z, p.y + 0.5), p.z)


## The aim point of an action clamped to `rmax` (horizontal): the locked rival unless the player drags an aim.
static func aim_ground(w: CombatWorld, a: ActorState, inst: ActionInst, rmax: float) -> Vector3:
	var dir: Vector3 = aim_flat(a, inst)
	var ap: Vector3 = inst.data.get("aim_point", a.chest() + dir * rmax)
	var t := w.get_actor(a.lock_target)
	if t != null and not inst.data.get("aim_active", false):
		ap = t.pos
	var flat := Vector3(ap.x - a.pos.x, 0.0, ap.z - a.pos.z)
	if flat.length() > rmax:
		flat = flat.normalized() * rmax
	return ground_at(w, a.pos + flat)


static func in_cone(origin: Vector3, dir: Vector3, p: Vector3, rng_m: float, half_deg: float, pad: float = 0.0) -> bool:
	var to := p - origin
	var flat := Vector3(to.x, 0.0, to.z)
	if flat.length() > rng_m + pad:
		return false
	if flat.length() < 0.5:
		return true
	return flat.normalized().dot(dir) >= cos(deg_to_rad(half_deg))


# ------------------------------------------------------------------ zones and bodies

## A ZONE of the kit (mat AIR unless given). props are merged into zone.props.
static func zone(w: CombatWorld, tag: String, pos: Vector3, radius: float, owner_id: int, power: float, life: float,
		props: Dictionary = {}, tier: int = 0, sub: int = 1) -> MatBody:
	var z := w.spawn_zone(StringName(tag), ground_at(w, pos), radius, owner_id, power, Sim.Mat.AIR, 0.0, life, "air:%d" % owner_id)
	z.tier = tier
	z.sub = sub
	for k in props:
		z.props[k] = props[k]
	return z


## Zones of an owner with a tag (alive).
static func zones_of(w: CombatWorld, owner_id: int, tag: String) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in w.bodies:
		if b.alive and b.form == Sim.Form.ZONE and b.owner == owner_id and String(b.tag) == tag:
			out.append(b)
	return out


static func zone_counter(w: CombatWorld, z: MatBody, perfect: bool = false) -> Agent:
	var c := Agent.of_body(w, z)
	c.actor = w.get_actor(z.owner)
	c.perfect = perfect
	return c


## Marks a (zone, body) pair as handled this tick so the core zone pass does not resolve it again.
static func mark_pair(w: CombatWorld, z: MatBody, b: MatBody) -> void:
	w._zone_pairs["%d|%d" % [z.id, b.id]] = w.tick


## True when the core zone pass already resolved the pair within `ticks` ticks.
static func pair_recent(w: CombatWorld, z: MatBody, b: MatBody, ticks: int) -> bool:
	return w.tick - int(w._zone_pairs.get("%d|%d" % [z.id, b.id], -100000)) < ticks


## Who made the body: the thrower of an attack, the zone owner, the last fighter that touched it (-1 = nobody).
static func source_owner(b: MatBody) -> int:
	if b.attack_id != 0 and b.attack_owner >= 0:
		return b.attack_owner
	if b.owner >= 0:
		return b.owner
	if b.residual_owner >= 0:
		return b.residual_owner
	return b.last_actor


## A loose body the wind can carry: not a wall / pool / zone, not held, not static, <= LIGHT kg.
static func carriable(b: MatBody, max_mass: float = LIGHT) -> bool:
	if not b.alive or b.static_body or b.controller >= 0 or b.captured_by >= 0:
		return false
	if b.form == Sim.Form.WALL or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE or b.form == Sim.Form.ZONE:
		return false
	return b.mass <= max_mass


## Bodies within r of p matching an optional filter (Callable(b) -> bool), in id order.
static func bodies_near(w: CombatWorld, p: Vector3, r: float, filter: Callable = Callable()) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in w.bodies:
		if not b.alive or b.pos.distance_to(p) > r + b.radius:
			continue
		if filter.is_valid() and not bool(filter.call(b)):
			continue
		out.append(b)
	return out


## Hostile (other owner, live attack) projectile.
static func hostile_shot(b: MatBody, to: ActorState) -> bool:
	return b.alive and b.is_projectile() and b.controller < 0 and (to == null or b.attack_owner != to.id)


## Fighters of the other team within r of p (alive), nearest first.
static func foes_near(w: CombatWorld, a: ActorState, p: Vector3, r: float) -> Array[ActorState]:
	var out: Array[ActorState] = []
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0:
			continue
		if t.chest().distance_to(p) <= r + Sim.ACTOR_RADIUS:
			out.append(t)
	out.sort_custom(func(x: ActorState, y: ActorState) -> bool:
		var dx := x.chest().distance_to(p)
		var dy := y.chest().distance_to(p)
		return dx < dy or (is_equal_approx(dx, dy) and x.id < y.id))
	return out


## Adds a velocity change to a fighter unless a status / stance makes them immune (what: knockback, pull, lift).
static func shove(t: ActorState, v: Vector3, what: String = "knockback") -> bool:
	if Status.immune(t, what):
		return false
	t.vel += v
	if v.y > 0.0:
		t.grounded = false
	return true


## True for a fighter held in the air by a mode / stance (flight, hover, whirl lift).
static func airborne(t: ActorState) -> bool:
	return t.flying or Status.has(t, "flight") or (not t.grounded and t.pos.y > t.ground_y + 0.6)


## Per-tick effect of a guard ZONE attached to its owner (Vortex Wall, Null Bubble): every loose body inside the radius
## is met through the rules with the guard's perfect timing (Vortex Catch, Vacuum Catch); the pair is marked so the
## core zone pass does not resolve it again. `skip(b)` (Callable, optional) filters bodies.
static func guard_zone_effect(w: CombatWorld, z: MatBody, skip: Callable = Callable()) -> void:
	var owner := w.get_actor(z.owner)
	if owner == null:
		return
	var perfect := w.perfect_guard(owner)
	var c: Agent = null
	for b in w.bodies:
		if b == z or not b.alive or b.static_body or b.controller >= 0 or b.captured_by >= 0:
			continue
		if b.form == Sim.Form.WALL or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE or (b.form == Sim.Form.ZONE and b.owner == z.owner):
			continue
		if skip.is_valid() and bool(skip.call(b)):
			continue
		if not w._in_zone(z, b.pos, b.radius) or pair_recent(w, z, b, int(maxf(1.0, 0.1 * Sim.HZ))):
			continue
		mark_pair(w, z, b)
		if c == null:
			c = zone_counter(w, z, perfect)
		else:
			c.perfect = perfect
		Interactions.resolve(w, Agent.of_body(w, b, owner), c, {"continuous": true, "site": "zone"}, Interactions.PASS_RULE)
		if not z.alive:
			return


# ------------------------------------------------------------------ arena reflection (Sound Lance)

## First arena solid along p0 -> p1 with the face normal: {t, n, solid} or {} (starts-inside segments are ignored).
static func arena_hit(w: CombatWorld, p0: Vector3, p1: Vector3) -> Dictionary:
	var best := {}
	var d := p1 - p0
	for s in w.arena.solids:
		var r := _slab_n(p0, d, s.min, s.max)
		if float(r.t) >= 0.0 and (best.is_empty() or float(r.t) < float(best.t)):
			best = r
			best["solid"] = s
	return best


static func _slab_n(o: Vector3, d: Vector3, mn: Vector3, mx: Vector3) -> Dictionary:
	var tmin := 0.0
	var tmax := 1.0
	var axis := -1
	var sgn := 0.0
	for i in 3:
		var oi: float = o[i]
		var di: float = d[i]
		if absf(di) < 1e-9:
			if oi < mn[i] or oi > mx[i]:
				return {"t": -1.0}
		else:
			var ta: float = (mn[i] - oi) / di
			var tb: float = (mx[i] - oi) / di
			var enter: float = ta if di > 0.0 else tb
			var leave: float = tb if di > 0.0 else ta
			if enter > tmin:
				tmin = enter
				axis = i
				sgn = -1.0 if di > 0.0 else 1.0
			tmax = minf(tmax, leave)
			if tmin > tmax:
				return {"t": -1.0}
	if axis < 0:
		return {"t": -1.0}
	var n := Vector3.ZERO
	n[axis] = sgn
	return {"t": tmin, "n": n}


## Face normal of a WALL body (its through-direction) pointing toward p.
static func wall_normal(wall: MatBody, p: Vector3) -> Vector3:
	var n := Vector3(sin(wall.wall_yaw), 0.0, cos(wall.wall_yaw))
	return n if n.dot(p - wall.pos) >= 0.0 else -n


static func reflect_dir(d: Vector3, n: Vector3) -> Vector3:
	return (d - 2.0 * d.dot(n) * n).normalized()


# ------------------------------------------------------------------ conversions (booked)

## Mist / steam collapses into water drops (same mass, the heat it carried leaves through the ledger).
static func condense(w: CombatWorld, b: MatBody) -> bool:
	if not (b.mat == Sim.Mat.STEAM or (b.is_water() and (b.form == Sim.Form.CLOUD or b.form == Sim.Form.ZONE))):
		return false
	var e1 := b.thermal_energy()
	b.mat = Sim.Mat.WATER
	if b.form == Sim.Form.ZONE or b.form == Sim.Form.CLOUD:
		b.form = Sim.Form.BLOB
	b.tag = &""
	b.phase = Sim.Phase.LIQUID
	b.liquid = 1.0
	b.temp = Sim.AMBIENT_C
	b.heat_payload = 0.0
	w.ledger.removed += e1 - b.thermal_energy()
	b.max_life = -1.0
	b.vel = Vector3(b.vel.x * 0.3, -1.0, b.vel.z * 0.3)
	b.attack_id = 0
	b.update_radius()
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "steam", "to": "water", "why": "collapsed"})
	return true


# ------------------------------------------------------------------ vacuum inrush (Fire's combustion reads it)

## A short-lived zone (tag inrush) where a vacuum just collapsed: air rushes back in. Fire's detonations inside it
## get x1.5 (docs/kits/air.md "Inrush flag"; FireUtil.zones_at(w, p, ["inrush"])).
static func spawn_inrush(w: CombatWorld, pos: Vector3, radius: float, power: float, owner_id: int) -> MatBody:
	var z := zone(w, "inrush", pos, radius, owner_id, power, INRUSH_LIFE, {"rate": NO_PAIR, "height": 3.0}, 0, 2)
	w.emit("inrush", {"pos": pos, "radius": radius, "power": power, "owner": owner_id, "tick": w.tick})
	return z


## The tick an inrush covering p began, or -1000000 when there is none.
static func inrush_tick_at(w: CombatWorld, p: Vector3) -> int:
	for b in w.bodies:
		if b.alive and b.form == Sim.Form.ZONE and b.tag == &"inrush" and w._in_zone(b, p, 0.2):
			return b.born_tick
	return -1000000
