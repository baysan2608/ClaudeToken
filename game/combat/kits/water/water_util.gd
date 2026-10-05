class_name WaterUtil
extends RefCounted
## Shared helpers of the Water kit (Water / Ice / Mist / Plant). Everything that moves water mass or
## heat goes through the CombatWorld ledgers: take() books the source, freeze_body() books freeze_dump,
## make_steam() books vapor, moisture sources book mass_ledger.moisture_taken, plants water_to_plant.

const POOL_REACH := 2.5


static func sec(frames: float) -> float:
	return frames / 60.0


# ------------------------------------------------------------------ water sources

## Distance (xz) from p to the pool rectangle (0 inside).
static func pool_dist(w: CombatWorld, p: Vector3) -> float:
	var ar := w.arena
	var q := Vector2(clampf(p.x, ar.pool_min.x, ar.pool_max.x), clampf(p.z, ar.pool_min.y, ar.pool_max.y))
	return Vector2(p.x, p.z).distance_to(q)


## A liquid puddle (not ice) within reach of p, nearest first (ties by id).
static func liquid_puddle_near(w: CombatWorld, p: Vector3, reach: float) -> MatBody:
	var best: MatBody = null
	var bd := INF
	for b in w.bodies:
		if not b.alive or b.form != Sim.Form.PUDDLE or b.phase != Sim.Phase.LIQUID or b.mass < 0.1:
			continue
		var d := Vector2(b.pos.x - p.x, b.pos.z - p.z).length() - b.radius
		if d <= reach and d < bd:
			bd = d
			best = b
	return best


static func near_water(w: CombatWorld, p: Vector3, reach: float = POOL_REACH) -> bool:
	return pool_dist(w, p) <= reach or liquid_puddle_near(w, p, reach) != null


## Water a fighter can use right now: waterskin + the pool (if within reach) + the nearest puddle.
static func available(w: CombatWorld, a: ActorState, reach: float = POOL_REACH) -> float:
	var m := a.water_carried
	if a.in_water or pool_dist(w, a.pos) <= reach:
		m += w.pool.mass
	var pd := liquid_puddle_near(w, a.pos, reach)
	if pd != null:
		m += pd.mass
	return m


static func _src_energy(b: MatBody, kg: float) -> float:
	return kg * (Sim.WATER_C * (b.temp - Sim.AMBIENT_C) - Sim.WATER_LATENT_FUSION * (1.0 - b.liquid))


## Takes up to `kg` of water: the waterskin first, then the pool (within reach of its edge or while
## standing in it), then the nearest liquid puddle. The water arrives at ambient; the source's own heat
## leaves through ledger.removed. Returns the kg taken (the caller turns it into a body or a zone).
static func take(w: CombatWorld, a: ActorState, kg: float, reach: float = POOL_REACH) -> float:
	var got := 0.0
	var m := minf(kg, a.water_carried)
	if m > 0.0:
		a.water_carried -= m
		got += m
	if got < kg - 1e-9 and (a.in_water or pool_dist(w, a.pos) <= reach):
		var t := minf(kg - got, w.pool.mass)
		if t > 0.0:
			w.ledger.removed += _src_energy(w.pool, t)
			w.pool.mass -= t
			got += t
	if got < kg - 1e-9:
		var pd := liquid_puddle_near(w, a.pos, reach)
		if pd != null:
			var t2 := minf(kg - got, pd.mass)
			w.ledger.removed += _src_energy(pd, t2)
			pd.mass -= t2
			got += t2
			if pd.mass <= 0.05:
				w.decay_body(pd, "drained")
			else:
				pd.update_radius_puddle()
	return got


## Gives water back (a move that could not finish): to the waterskin, the rest as a puddle at p.
static func give_back(w: CombatWorld, a: ActorState, kg: float, p: Vector3) -> void:
	if kg <= 0.0:
		return
	var room := maxf(0.0, 6.0 - a.water_carried)
	var back := minf(kg, room)
	a.water_carried += back
	if kg - back > 0.0:
		make_puddle(w, kg - back, p)


## A puddle of `kg` at p (merges with neighbours / the pool). Returns it (null when merged away).
static func make_puddle(w: CombatWorld, kg: float, p: Vector3) -> MatBody:
	if kg < 0.02:
		w.mass_ledger.evaporated += kg
		return null
	var b := w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, kg, Vector3(p.x, p.y + 0.3, p.z), "spray")
	w._water_to_puddle(b)
	return b if b.alive else null


## Water that simply disperses (spray droplets, rain): booked as evaporated (water_mass() stays exact).
static func disperse(w: CombatWorld, kg: float) -> void:
	w.mass_ledger.evaporated += maxf(kg, 0.0)


# ------------------------------------------------------------------ phase changes

## Freezes a water body in place (liquid 0, below zero), booking the dumped heat in freeze_dump.
static func freeze_body(w: CombatWorld, b: MatBody) -> bool:
	if b == null or not b.alive or b.mat != Sim.Mat.WATER or b.form == Sim.Form.POOL:
		return false
	var e0 := b.thermal_energy()
	b.liquid = 0.0
	b.temp = minf(b.temp, -5.0)
	b.phase = Sim.Phase.FROZEN
	w.ledger.freeze_dump += b.thermal_energy() - e0
	return true


static func is_ice(b: MatBody) -> bool:
	return b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.FROZEN


static func is_liquid_water(b: MatBody) -> bool:
	return b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.LIQUID


## Moves up to `hu` of heat from one body into another (exact: whatever the receiver cannot take goes
## back). Returns the HU moved. Water receivers may boil (ledger vapor via heat_body).
static func transfer_heat(w: CombatWorld, src: MatBody, dst: MatBody, hu: float) -> float:
	if src == null or dst == null or not src.alive or not dst.alive or hu <= 0.0:
		return 0.0
	var give := minf(hu, maxf(0.0, src.thermal_energy() - src.heat_payload))
	if give <= 0.0:
		return 0.0
	var got := -Thermal.heat(src, -give)
	if got <= 0.0:
		return 0.0
	var used := w.heat_body(dst, got)
	if used < got - 1e-9 and src.alive:
		Thermal.heat(src, got - used)
	return used


## Spawns `kg` of steam at p from water the fighter carries (booked: the waterskin loses it, the heat paid
## by the action vaporises it - ledger vapor). Returns the HU used. Uses the heat the action paid (heat_paid).
static func make_steam(w: CombatWorld, a: ActorState, inst: ActionInst, kg: float, p: Vector3) -> float:
	var got := take(w, a, kg)
	if got < 0.05:
		return 0.0
	var b := w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, got, p, "steam:%d" % a.id)
	var need := Thermal.vapor_energy(got) + 0.01
	var have := float(inst.data.get("heat_paid", 0.0)) if inst != null else need
	var hu := minf(need, have)
	var used := w.heat_body(b, hu)
	if inst != null:
		inst.data["heat_paid"] = float(inst.data.get("heat_paid", 0.0)) - used
	if b.alive:
		# Not enough heat to vaporise it all: what is left falls as warm water (a puddle).
		b.vel = Vector3.ZERO
		w._water_to_puddle(b)
	return used


## Applies the "wet" state to a fighter.
static func wet(a: ActorState, v: float = 1.0) -> void:
	a.wetness = maxf(a.wetness, v)


# ------------------------------------------------------------------ evades (legacy-shaped dashes)

## Starts a legacy-shaped evade (direction from the stick, neutral = backstep, side clip, i-frames).
## spec: dist, active, iframes, cost, hidden (concealed while moving).
static func evade_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent, spec: Dictionary) -> void:
	var dir := it.move
	dir.y = 0.0
	if dir.length() < 0.2:
		dir = -a.forward()
	dir = dir.normalized()
	inst.data["dir"] = dir
	inst.data["controls_motion"] = true
	inst.data["face"] = a.forward()
	inst.data["evade_dist"] = float(spec.get("dist", 2.8))
	inst.data["evade_dur"] = float(spec.get("active", inst.def.active))
	w.spend_focus(a, minf(a.focus, float(spec.get("cost", inst.def.get("cost", 0.0)))))
	a.iframes = maxf(a.iframes, float(spec.get("iframes", 0.14)))
	if bool(spec.get("hidden", false)):
		Status.apply(w, a, "concealed", float(spec.get("hidden_t", inst.data.evade_dur)), 1.0, a.id)
	var f := a.forward()
	var r := f.cross(Vector3.UP)
	var fd := dir.dot(f)
	var rd := dir.dot(r)
	var side := "back"
	if absf(fd) >= absf(rd):
		side = "fwd" if fd > 0.0 else "back"
	else:
		side = "r" if rd < 0.0 else "l"
	inst.data["side"] = side
	w.emit("evade", {"actor": a.id, "dir": dir, "side": side, "dash": false, "move": inst.id})


static func evade_tick(_w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	if inst.phase != ActionInst.P.ACTIVE or not inst.data.has("dir"):
		return
	var dur := float(inst.data.get("evade_dur", inst.def.active))
	var dist := float(inst.data.get("evade_dist", 2.8))
	var x := clampf(inst.t / maxf(dur, 1e-3), 0.0, 1.0)
	var spd := 2.0 * dist / maxf(dur, 1e-3) * (1.0 - x)
	var dir: Vector3 = inst.data.dir
	a.vel.x = dir.x * spd
	a.vel.z = dir.z * spd


static func evade_end(inst: ActionInst) -> void:
	inst.data["controls_motion"] = false


# ------------------------------------------------------------------ zones

## A ZONE with the kit's conventions: props merged in, zone event emitted by the core. `mass` (kg) is booked by the caller.
static func zone(w: CombatWorld, tag: String, p: Vector3, radius: float, owner_id: int, life: float, props: Dictionary = {},
		mat: int = Sim.Mat.AIR, mass: float = 0.0, power: float = 0.0) -> MatBody:
	var z := w.spawn_zone(StringName(tag), p, radius, owner_id, power, mat, mass, life)
	for k in props:
		z.props[k] = props[k]
	return z


## Ground point under (x, z) reachable from y.
static func ground_at(w: CombatWorld, p: Vector3) -> Vector3:
	return Vector3(p.x, w.arena.ground_height(p.x, p.z, p.y + 0.5), p.z)


## Fighters hit by the last volume that were wet when it landed (frost cones freeze them).
static func hit_result_ok(t: ActorState) -> bool:
	return t.last_result == "hit" or t.last_result == "knockdown"
