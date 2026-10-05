class_name AirVortex
extends RefCounted
## Air / Vortex (sub 1) - docs/MOVESET.md §7.14, §8.4 (Vortex column), §9.3 combos 11, 12, 28.
##
## strike  Twister -> twin Twisters -> Tornado (zone, walks to the target) -> Cyclone Fortress   thrust Spiral Lance
## ground  Dust Funnel                                                      sweep  Eddy Ring
## guard   Vortex Wall (+ Vortex Catch, the perfect)   push Unleash   sink Funnel Down
## tech    Eye of the Storm (steer by aim; contest an enemy tornado)
## evade   Spin Step                                                        hold   Whirl Lift
##
## A tornado is a ZONE body (tag tornado): it walks to the target, captures and orbits bodies <= 30 kg (rule cells
## x|tornado, Outcomes.capture), lifts fighters, and takes the character of what it carries - sand -> sandstorm (blind,
## abrade), fire -> fire tornado, water -> water tornado (it conducts), steam -> scalding, spilled lava -> magma vortex.
## A tornado infused by the ENEMY's material turns neutral (owner -1: it hurts both). Captured bodies can be flung
## back as the caster's attacks (Unleash). Nothing here creates matter or heat: the vortex carries bodies that
## already exist; wind cools what it carries through the ambient ledger.

const E := 3
const SUB := 1
const MAX_CAPTURED := 8
const SPIN := 7.0
const INFUSION_STATUS := {"sand": ["blinded", "sandblasted"], "fire": ["burning"], "magma": ["burning"], "water": ["wet"], "steam": ["scalded"]}


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
	CombatWorld.register_body_tick(&"twister", Callable(AirVortex, "twister_tick"))
	CombatWorld.register_zone_effect(&"tornado", Callable(AirVortex, "tornado_effect"))
	CombatWorld.register_zone_effect(&"vortex_wall", Callable(AirVortex, "wall_effect"))
	CombatWorld.register_zone_effect(&"eddy", Callable(AirVortex, "eddy_effect"))
	CombatWorld.register_tech_preview(E, SUB, Callable(AirVortex, "tech_preview"))


# ================================================================ strike: Twister / Tornado / Cyclone Fortress

static func _strike() -> void:
	KitAir.reg("vortex_twister", SUB, "strike", {"name": "Twister / Tornado",
		"desc": "A small vortex @12 m/s that lifts light targets and catches small shots it passes. Hold: twin twisters, then a Tornado (a zone that walks to the target for 4 s: it captures and orbits bodies up to 30 kg and lifts fighters) and the Cyclone Fortress (r 4 m, 6 s). Sand, fire, water or steam in it infuse it - the enemy's material turns it neutral.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(12), "active": _s(4), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "source": "none", "mat": "air", "tag": "twister", "mass": 0.0, "speed": 12.0, "gravity": 0.0,
		"power": 8.0, "life": 1.2, "damage": 6.0, "balance": 16.0, "count": 1, "radius": 0.8,
		"tiers": {
			"t1": {"count": 2, "spread": 14.0, "power": 12.0, "damage": 8.0, "cost_add": 4.0},
			"t2": {"tag": "tornado", "radius": 2.5, "life": 4.0, "power": 25.0, "walk_speed": 4.0, "at": "ahead", "distance": 3.0, "height": 5.0,
				"dps": 2.0, "mat": "air", "cost_add": 10.0},
			"t3": {"radius": 4.0, "life": 6.0, "power": 35.0, "cost_add": 18.0},
		},
		"hook_execute": Callable(AirVortex, "twister_execute"),
		"counter": {"cls": "tornado", "power": [8.0, 12.0, 25.0, 35.0]}, "threat": {"cls": "tornado", "power": [8.0, 12.0, 25.0, 35.0]},
		"anim": "air_gust", "anim_active": "mv_spin", "fx": {"mat": "vortex", "release": "release", "ring": "ring"},
		"ai": {"role": "zone", "range": [3.0, 14.0], "tags": ["projectile", "lift", "capture", "infusion", "walks", "vs_volley"]}})


static func twister_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	if inst.tier() >= 2:
		var z := VerbZone.spawn(w, a, inst)
		z.sub = SUB
		z.props["lift"] = true
		z.props["rate"] = 0.1
		z.spin = SPIN
		inst.data["bodies"] = [z.id]
		return true
	var made := VerbProjectile.fire(w, a, inst)
	for b in made:
		b.radius = float(Charge.param(inst, "radius", 0.8))
		b.spin = 12.0
	return true


## A twister (projectile) passes: it catches the small bodies it meets (cells x|tornado with the twister as the
## counter) and lifts the fighters it touches. It keeps flying through them.
static func twister_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if not b.alive or b.attack_id == 0 or b.form == Sim.Form.ZONE:
		return false
	b.spin = 12.0
	var owner := w.get_actor(b.attack_owner)
	var c: Agent = null
	for o in w.bodies:
		if o == b or not AirUtil.carriable(o):
			continue
		if o.attack_id != 0 and o.attack_owner == b.attack_owner:
			continue
		if o.mat == Sim.Mat.AIR:
			continue
		if o.pos.distance_to(b.pos) > b.radius + o.radius + 0.4:
			continue
		var key := "%d|%d" % [b.id, o.id]
		if w.tick - int(w._zone_pairs.get(key, -100000)) < 8:
			continue
		w._zone_pairs[key] = w.tick
		if c == null:
			c = Agent.of_body(w, b)
			c.actor = owner
		Interactions.resolve(w, Agent.of_body(w, o, owner), c, {"site": "twister"}, Interactions.PASS_RULE)
		if not b.alive:
			return false
	if owner == null:
		return false
	for t in w.actors:
		if t == owner or t.team == owner.team or t.health <= 0.0 or b.hit_set.has(t.id):
			continue
		if b.pos.distance_to(t.chest()) > b.radius + Sim.ACTOR_RADIUS + 0.5:
			continue
		b.hit_set[t.id] = true
		var lift := 6.0 + 0.2 * b.power
		var ag := Agent.of_body(w, b)
		ag.actor = owner
		w.hit_actor(t, {"attacker": owner.id, "attack_id": b.attack_id, "damage": b.damage, "balance": b.balance_damage,
			"knock": b.vel.normalized() * 3.0 + Vector3(0, lift, 0), "kind": "air", "from": b.pos, "agent": ag, "power": b.power,
			"tier": b.tier, "mat": "vortex"})
	return false


# ================================================================ the tornado zone

## Per tick: spin, orbit and cool what it carries, take the character of its infusions, lift and swirl the fighters in it.
static func tornado_effect(w: CombatWorld, z: MatBody, dt: float) -> void:
	z.spin = SPIN
	var inf := _scan_infusions(w, z)
	_orbit(w, z, dt)
	z.charge = 0.02 if inf.has("water") else 0.0        # a water tornado conducts (Materials.conducts: charged bodies)
	var lift_k := clampf(z.power / 25.0, 0.4, 1.6)
	var neutral := bool(z.props.get("neutral", false))
	for a in w.actors:
		if a.health <= 0.0 or (a.id == z.owner and not neutral):
			continue
		if not w._in_zone(z, a.pos + Vector3(0, 0.9, 0), Sim.ACTOR_RADIUS):
			continue
		var rel := a.pos - z.pos
		rel.y = 0.0
		var d := rel.length()
		var tangent := Vector3(-rel.z, 0.0, rel.x).normalized() if d > 0.05 else Vector3.RIGHT
		if not Status.immune(a, "pull"):
			var inward := -rel.normalized() * minf(d, 3.0) * 1.2 if d > 0.05 else Vector3.ZERO
			a.vel += (tangent * 7.0 + inward) * dt
		if not Status.immune(a, "lift"):
			var height := a.pos.y - w.arena.ground_height(a.pos.x, a.pos.z, a.pos.y + 0.3)
			var vy_t := 5.0 * lift_k if height < 3.4 else 0.5
			a.vel.y = move_toward(a.vel.y, vy_t, 40.0 * dt)
			a.grounded = false
			Status.apply(w, a, "windborne", 0.3, 1.0, z.owner)
			# the whirl wears balance down (x1.5 for fighters held up by wind: flight)
			a.balance = minf(a.balance, maxf(8.0, a.balance - (10.5 if Status.has(a, "flight") else 7.0) * dt))
			a.balance_idle = 0.0
			if Status.has(a, "flight"):
				AirSound.end_flight(w, a, "tornado")
		for kind in inf:
			for st in INFUSION_STATUS.get(kind, []):
				Status.apply(w, a, String(st), 0.4, 1.0, z.owner)
		if z.props.has("dps") and float(z.props.dps) > 0.0 and inf.size() > 0:
			a.health = maxf(0.0, a.health - 1.5 * float(inf.size()) * dt)


## What the tornado carries / has soaked up, as infusion kinds -> count: sand, fire, water, steam, magma.
static func _scan_infusions(w: CombatWorld, z: MatBody) -> Dictionary:
	var out := {}
	for id in z.captured:
		var b := w.get_body(id)
		if b == null or not b.alive or b.captured_by != z.id:
			continue
		var k := infusion_kind(b)
		if k != "":
			out[k] = int(out.get(k, 0)) + 1
	if float(z.props.get("spatter_until", -1.0)) > z.age:
		out["magma"] = int(out.get("magma", 0)) + 1
	var seen: Dictionary = z.props.get("inf_seen", {})
	for k in out:
		if not seen.has(k):
			seen[k] = true
			w.emit("infuse", {"body": z.id, "with": k, "owner": z.owner, "neutral": bool(z.props.get("neutral", false))})
	z.props["inf_seen"] = seen
	z.props["infused"] = ", ".join(PackedStringArray(out.keys())) if not out.is_empty() else ""
	return out


static func infusion_kind(b: MatBody) -> String:
	match b.mat:
		Sim.Mat.SAND:
			return "sand"
		Sim.Mat.FIRE:
			return "fire" if b.heat_payload > 2.0 else ""
		Sim.Mat.WATER:
			return "water" if b.phase == Sim.Phase.LIQUID and b.form != Sim.Form.CLOUD else ""
		Sim.Mat.STEAM:
			return "steam"
		Sim.Mat.STONE:
			return "magma" if b.liquid > 0.0 else ""
	return ""


## Captured bodies orbit in a helix inside the zone; hot ones cool in the wind (convective cooling, booked ambient).
static func _orbit(w: CombatWorld, z: MatBody, dt: float) -> void:
	var n := 0
	var radius := maxf(0.6, z.zone_radius * 0.6)
	for id in z.captured.duplicate():
		var b := w.get_body(id)
		if b == null or not b.alive or b.captured_by != z.id:
			z.captured.erase(id)
			continue
		var off: Vector3 = b.props.get("capture_off", Vector3.ZERO)
		var flat := Vector3(off.x, 0.0, off.z)
		var l := flat.length()
		var want := radius * (0.7 + 0.3 * float(n % 3) / 2.0)
		flat = (flat / l if l > 0.05 else Vector3(1, 0, 0)) * move_toward(l, want, 2.0 * dt)
		var y := 0.9 + 0.3 * float(n % 4) + 0.35 * sin(float(w.tick) * dt * 3.0 + float(n) * 1.7)
		b.props["capture_off"] = Vector3(flat.x, y, flat.z)
		if (b.mat == Sim.Mat.STONE or b.mat == Sim.Mat.METAL or b.mat == Sim.Mat.SAND or b.mat == Sim.Mat.GLASS) and b.thermal_energy() > 0.0:
			var rate := 40.0 * clampf(z.power / 25.0, 0.4, 2.0) * dt
			var got := -Thermal.heat(b, -minf(rate, b.thermal_energy()))
			w.ledger.ambient -= got
		n += 1


## Tornado -> neutral: an infusion made of the enemy's material turns the whirl against both fighters.
static func make_neutral(w: CombatWorld, z: MatBody, src: int) -> void:
	if bool(z.props.get("neutral", false)):
		return
	z.props["origin_owner"] = z.owner
	z.props["neutral"] = true
	z.props["spare_owner"] = false
	z.owner = -1
	w.emit("infuse", {"body": z.id, "with": "enemy", "owner": -1, "neutral": true, "src": src})


# ================================================================ thrust: Spiral Lance

static func _thrust() -> void:
	KitAir.reg("vortex_spiral", SUB, "thrust", {"name": "Spiral Lance",
		"desc": "A drilling spiral of wind @24 m/s. Pierces mist, sand clouds and sand walls; stone and metal walls turn it. Hold: faster, stronger, pierces fighters.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(12), "active": _s(4), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "source": "none", "mat": "air", "tag": "spiral", "mass": 0.0, "speed": 24.0, "gravity": 0.0,
		"power": 10.0, "life": 1.2, "damage": 10.0, "balance": 18.0, "count": 1,
		"tiers": {
			"t1": {"power": 14.0, "damage": 12.0, "cost_add": 2.0},
			"t2": {"power": 18.0, "damage": 15.0, "pierce": 1, "cost_add": 4.0},
			"t3": {"power": 24.0, "damage": 20.0, "pierce": 2, "speed": 28.0, "cost_add": 8.0},
		},
		"counter": {"cls": "gust", "power": [10.0, 14.0, 18.0, 24.0]}, "threat": {"cls": "spiral", "power": [10.0, 14.0, 18.0, 24.0]},
		"anim": "air_push", "anim_active": "mv_palm_thrust", "fx": {"mat": "vortex", "shape": "spiral", "release": "release"},
		"ai": {"role": "poke", "range": [3.0, 20.0], "tags": ["projectile", "pierce_cloud", "pierce_sand_wall", "ranged"]}})


# ================================================================ ground: Dust Funnel

static func _ground() -> void:
	KitAir.reg("vortex_funnel", SUB, "ground", {"name": "Dust Funnel",
		"desc": "A ground vortex races 10 m/s: it gathers the loose bodies on its path and carries them to the target, where they are released as your attack - the arena's rubble becomes ammunition.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(14), "active": _s(6), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"cost": 8.0, "source": "none", "mat": "air", "tag": "funnel", "mass": 0.0, "speed": 10.0, "budget": 10.0,
		"width": 1.8, "damage": 6.0, "balance": 20.0, "knock": 2.0, "lift": 4.0, "kind": "air", "steer": 14.0, "power": 10.0,
		"tiers": {
			"t1": {"power": 14.0, "budget": 11.0, "width": 2.1, "cost_add": 2.0},
			"t2": {"power": 20.0, "budget": 12.0, "width": 2.5, "damage": 9.0, "cost_add": 5.0},
			"t3": {"power": 28.0, "budget": 14.0, "width": 3.0, "damage": 12.0, "cost_add": 9.0},
		},
		"counter": {"cls": "tornado", "power": [10.0, 14.0, 20.0, 28.0]}, "threat": {"cls": "tornado", "power": [10.0, 14.0, 20.0, 28.0]},
		"anim": "mv_ground_slap", "fx": {"mat": "vortex", "release": "release"},
		"ai": {"role": "zone", "range": [2.0, 14.0], "tags": ["ground_line", "gathers_loose", "ammunition", "ranged"]}})


# ================================================================ sweep: Eddy Ring

static func _sweep() -> void:
	KitAir.reg("vortex_eddy", SUB, "sweep", {"name": "Eddy Ring",
		"desc": "A ring vortex (r 3 m) around you for 1.5 s: projectiles curve around you - orbit-deflected, sometimes straight back at the thrower - and light fighters are pushed out.",
		"module": "verbs", "verb": "zone",
		"startup": _s(8), "active": _s(24), "recovery": _s(14), "cancel": 0.6,
		"cost": 6.0, "tag": "eddy", "mat": "air", "at": "self", "attach": true, "radius": 3.0, "life": 1.5, "power": 10.0, "height": 2.8, "rate": 0.1,
		"tiers": {
			"t1": {"power": 14.0, "radius": 3.2, "life": 1.8, "cost_add": 2.0},
			"t2": {"power": 18.0, "radius": 3.5, "life": 2.2, "cost_add": 4.0},
			"t3": {"power": 24.0, "radius": 4.0, "life": 2.8, "cost_add": 8.0},
		},
		"counter": {"cls": "eddy", "power": [10.0, 14.0, 18.0, 24.0]}, "threat": {"cls": "tornado", "power": [10.0, 14.0, 18.0, 24.0]},
		"anim": "mv_spin", "fx": {"mat": "vortex", "shape": "open", "cast": "cast", "release": "ring"},
		"ai": {"role": "counter", "range": [0.0, 4.0], "tags": ["vs_volley", "orbit_deflect", "pushes_out", "area"]}})


## The ring pushes the fighters inside it outward (light fighters: anchored ones stay).
static func eddy_effect(w: CombatWorld, z: MatBody, dt: float) -> void:
	z.spin = 10.0
	for t in w.actors:
		if t.health <= 0.0 or t.id == z.owner:
			continue
		if not w._in_zone(z, t.pos + Vector3(0, 0.9, 0), Sim.ACTOR_RADIUS):
			continue
		var rel := t.pos - z.pos
		rel.y = 0.0
		var out := rel.normalized() if rel.length() > 0.05 else -t.forward()
		var want := 5.0 + 0.2 * z.power
		var vout := t.vel.dot(out)
		if vout < want:
			AirUtil.shove(t, out * (want - vout))


# ================================================================ guard: Vortex Wall / Vortex Catch

static func _guard() -> void:
	KitAir.reg("vortex_wall", SUB, "guard", {"name": "Vortex Wall",
		"desc": "A spinning barrier around you. It captures light projectiles - they orbit inside - instead of stopping them; heavier ones pass bent, flames are fed and spun. Perfect: Vortex Catch grabs even heavier shots and flings them faster. Flick up: Unleash. Flick down: Funnel Down. Weak to water mass and a Vacuum Well.",
		"module": "verbs", "verb": "barrier", "barrier": "zone", "tag": "vortex_wall", "mat": "air", "radius": 1.8, "height": 2.6,
		"stops_bolts": false, "upkeep": 4.0, "move_channel": 0.5, "startup": 0.0, "active": 0.0, "recovery": _s(10), "cost": 4.0,
		"tiers": {
			"t1": {"radius": 2.1, "cost_add": 0.0},
			"t2": {"radius": 2.4},
			"t3": {"radius": 2.8},
		},
		"counter": {"cls": "wall_vortex", "power": [16.0, 20.0, 26.0, 32.0], "perfect": "air_catch"}, "threat": {"cls": "tornado"},
		"anim": "mv_rising_guard", "fx": {"mat": "vortex", "aura": "aura"},
		"ai": {"role": "counter", "range": [0.0, 6.0], "tags": ["captures_light", "vs_volley", "then_unleash", "weak_to_water", "weak_to_vacuum"]}})


## The Vortex Wall zone (attached to the guard): a body entering its radius is met through the rules with the guard's
## perfect timing (Vortex Catch); orbit management like the tornado's.
static func wall_effect(w: CombatWorld, z: MatBody, dt: float) -> void:
	z.spin = 9.0
	AirUtil.guard_zone_effect(w, z)
	if z.alive:
		_orbit(w, z, dt)


# ================================================================ push: Unleash / sink: Funnel Down

static func _push_sink() -> void:
	KitAir.reg("vortex_unleash", SUB, "push", {"name": "Unleash",
		"desc": "From the Vortex Wall: fling everything it captured at the target. Each body keeps its mass and heat and flies as YOUR attack (a new attack id); a perfectly caught shot flies 25 % faster.",
		"module": "verbs", "verb": "burst",
		"startup": _s(8), "active": _s(4), "recovery": _s(16), "cancel": 0.6,
		"cost": 4.0, "speed": 20.0, "damage": 8.0, "balance": 20.0,
		"hook_execute": Callable(AirVortex, "unleash_execute"),
		"counter": {"cls": "tornado", "power": [16.0]}, "threat": {"cls": "tornado"},
		"anim": "mv_push_two_hand", "fx": {"mat": "vortex", "shape": "fan", "release": "release"},
		"ai": {"role": "finisher", "range": [0.0, 16.0], "tags": ["from_guard", "return_volley", "needs_captured"]}})
	KitAir.reg("vortex_funnel_down", SUB, "sink", {"name": "Funnel Down",
		"desc": "From the Vortex Wall: drive the vortex into the ground - a dust crater - and drop the captured bodies at your feet, ready for Earth or Water to seize.",
		"module": "verbs", "verb": "burst",
		"startup": _s(6), "active": _s(8), "recovery": _s(14), "cancel": 0.6,
		"cost": 4.0, "radius": 2.2, "power": 10.0, "damage": 3.0, "balance": 12.0, "knock": 3.0, "lift": 1.0, "cls": "gust",
		"hook_execute": Callable(AirVortex, "funnel_down_execute"),
		"counter": {"cls": "tornado", "power": [10.0]}, "threat": {"cls": "tornado"},
		"anim": "mv_ground_slap", "fx": {"mat": "vortex", "shape": "ground", "release": "burst"},
		"ai": {"role": "setup", "range": [0.0, 3.0], "tags": ["from_guard", "drops_captured", "crater"]}})


## Vortex zones the fighter can unleash / drop: the kept Vortex Wall first, then their own tornado nearby.
static func _own_vortices(w: CombatWorld, a: ActorState) -> Array[MatBody]:
	var out := AirUtil.zones_of(w, a.id, "vortex_wall")
	for z in AirUtil.zones_of(w, a.id, "tornado"):
		if z.pos.distance_to(a.pos) <= 8.0:
			out.append(z)
	return out


static func unleash_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var target := Verbs.target_point(w, a, inst, 16.0)
	var speed := float(Charge.param(inst, "speed", 20.0))
	var n := 0
	for z in _own_vortices(w, a):
		for id in z.captured.duplicate():
			var b := w.get_body(id)
			if b == null or not b.alive or b.captured_by != z.id:
				continue
			z.captured.erase(id)
			b.captured_by = -1
			b.props.erase("capture_off")
			b.pos = b.pos + Vector3(0, 0.1, 0)
			var spd := speed * (1.25 if b.props.get("caught_perfect", false) else 1.0)
			b.gravity_scale = 0.3
			b.vel = Verbs.launch_vel(b.pos, target + Vector3(0, 0.0, 0), spd, 0.3)
			b.on_ground = false
			var dmg := float(Charge.param(inst, "damage", 8.0)) + 0.4 * b.mass
			Verbs.arm(w, a, inst, b, dmg, float(Charge.param(inst, "balance", 20.0)) + 0.5 * b.mass)
			b.touch(a.id, "unleash", w.tick)
			w.emit("unleash", {"actor": a.id, "body": b.id, "speed": spd, "tier": inst.tier()})
			Verbs.fx(w, a, inst, "release", {"body": b.id, "pos": b.pos, "dir": b.vel.normalized(), "power": b.mass * spd / 20.0})
			n += 1
		if z.tag == &"vortex_wall":
			w.close_zone(z, "unleashed")
	inst.data["unleashed"] = n
	if n == 0:
		w.emit("whiff", {"actor": a.id, "move": inst.id})
	return true


static func funnel_down_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var dropped := 0
	for z in _own_vortices(w, a):
		var k := 0
		for id in z.captured.duplicate():
			var b := w.get_body(id)
			if b == null or not b.alive or b.captured_by != z.id:
				continue
			z.captured.erase(id)
			b.captured_by = -1
			b.props.erase("capture_off")
			var ang := TAU * float(k) / maxf(1.0, float(z.captured.size() + 1))
			b.pos = a.pos + Vector3(cos(ang), 0.0, sin(ang)) * 0.9 + Vector3(0, 0.6, 0)
			b.vel = Vector3(0, -2.0, 0)
			b.attack_id = 0
			b.on_ground = false
			dropped += 1
			k += 1
		if z.tag == &"vortex_wall":
			w.close_zone(z, "funnel_down")
	inst.data["dropped"] = dropped
	var prm := {"radius": float(Charge.param(inst, "radius", 2.2)), "power": float(Charge.param(inst, "power", 10.0)),
		"damage": float(Charge.param(inst, "damage", 3.0)), "balance": float(Charge.param(inst, "balance", 12.0)),
		"knock": float(Charge.param(inst, "knock", 3.0)), "lift": float(Charge.param(inst, "lift", 1.0)), "cls": "gust", "mat": "vortex", "heat_hu": 0.0}
	VerbVolume.burst_at(w, a, inst, a.pos + Vector3(0, 0.2, 0), prm)
	Verbs.fx(w, a, inst, "burst", {"pos": a.pos, "radius": 2.2, "shape": "ground", "mat": "sand"})
	return true


# ================================================================ tech: Eye of the Storm

static func _tech() -> void:
	KitAir.reg("vortex_eye", SUB, "tech", {"name": "Eye of the Storm",
		"desc": "Create a tornado at the aim point (10 m) and steer it by dragging the aim while you hold; release and it lives 2 s more. Over an enemy tornado: a contest - strong enough, you take it over (and steer it); merged with your own it grows. Hold longer: bigger, stronger.",
		"module": "kit_air", "verb": "summon",
		"startup": _s(14), "active": 0.0, "recovery": _s(18), "cancel": 0.5, "move_channel": 0.6,
		"cost": 10.0, "upkeep": 10.0, "tag": "tornado", "mat": "air", "at": "aim", "range": 10.0, "radius": 2.5, "power": 25.0, "height": 5.0,
		"dps": 2.0, "steer_speed": 6.0, "linger": 2.0,
		"tiers": {
			"t1": {"power": 28.0, "radius": 2.5},
			"t2": {"power": 32.0, "radius": 3.0},
			"t3": {"power": 35.0, "radius": 4.0},
		},
		"counter": {"cls": "tornado", "power": [25.0, 28.0, 32.0, 35.0]}, "threat": {"cls": "tornado", "power": [25.0, 28.0, 32.0, 35.0]},
		"anim": "mv_wide_draw", "anim_hold": "glide", "fx": {"mat": "vortex", "release": "ring", "ring": "ring"},
		"ai": {"role": "zone", "range": [2.0, 10.0], "tags": ["summon", "steer", "contest_tornado", "lift", "capture"]}})
	KitAir.handle("vortex_eye", {"after": Callable(AirVortex, "eye_after"), "tick": Callable(AirVortex, "eye_tick")})


static func _enemy_tornado_near(w: CombatWorld, a: ActorState, p: Vector3, r: float) -> MatBody:
	var best: MatBody = null
	for z in w.bodies:
		if not z.alive or z.form != Sim.Form.ZONE or z.tag != &"tornado" or z.owner == a.id:
			continue
		if z.owner >= 0:
			var o := w.get_actor(z.owner)
			if o != null and o.team == a.team:
				continue
		if Vector2(z.pos.x - p.x, z.pos.z - p.z).length() <= r + z.zone_radius and (best == null or z.id < best.id):
			best = z
	return best


## A tornado already stands at the aim point: contest it. Strong enough (the Eye's power >= 90 % of it), the caster takes
## it over and steers it; otherwise the new tornado is summoned beside it and the two meet through the rules.
static func eye_after(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	var ap := AirUtil.aim_ground(w, a, inst, 10.0)
	var enemy := _enemy_tornado_near(w, a, ap, 1.0)
	if enemy != null and float(Charge.param(inst, "power", 25.0)) >= enemy.power * 0.9:
		var from := enemy.owner
		enemy.props["origin_owner"] = from
		enemy.owner = a.id
		enemy.props["neutral"] = false
		enemy.props["spare_owner"] = true
		enemy.props.erase("walk_speed")
		enemy.props.erase("walk_target")
		enemy.max_life = -1.0
		inst.data["summon"] = enemy.id
		inst.data["took_over"] = true
		w.emit("tornado_taken", {"body": enemy.id, "by": a.id, "from": from})
		FxEvents.fx_for(w, a, inst, "ring", "vortex", {"pos": enemy.pos, "radius": enemy.zone_radius, "body": enemy.id, "power": enemy.power})
		return ActionInst.P.CHANNEL
	var phase := Verbs.after_startup(w, a, inst, it)
	var z := w.get_body(int(inst.data.get("summon", -1)))
	if z != null:
		z.sub = SUB
		z.spin = SPIN
	return phase


static func eye_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	var z := w.get_body(int(inst.data.get("summon", -1)))
	if z != null and z.alive and inst.data.get("took_over", false):
		z.owner = a.id


static func tech_preview(w: CombatWorld, a: ActorState, dir: Vector3) -> Dictionary:
	var p := a.pos + dir * 8.0
	if _enemy_tornado_near(w, a, p, 1.0) != null:
		return {"mode": "CONTEST", "body": -1, "ok": true, "reason": ""}
	return {"mode": "STORM", "body": -1, "ok": a.focus >= 10.0, "reason": "" if a.focus >= 10.0 else "focus"}


# ================================================================ evade: Spin Step / Whirl Lift

static func _evade() -> void:
	KitAir.reg("vortex_spin_step", SUB, "evade", {"name": "Spin Step",
		"desc": "A spinning sidestep (3 m, 10 i-frames) that turns light projectiles aside while you spin.",
		"module": "verbs", "verb": "dash",
		"startup": 0.0, "active": _s(16), "recovery": _s(8), "cancel": 0.5,
		"cost": 5.0, "distance": 3.0, "iframes": _s(10), "dir": "stick",
		"hook_tick": Callable(AirVortex, "spin_tick"),
		"counter": {"cls": "eddy", "power": [14.0]},
		"anim": "mv_spin", "fx": {"mat": "vortex", "trail": "trail"},
		"ai": {"role": "mobility", "range": [0.0, 3.5], "tags": ["dash", "iframes", "deflects_light", "vs_volley"]}})
	KitAir.reg("vortex_whirl", SUB, "evade_hold", {"name": "Whirl Lift",
		"desc": "Hold EVADE: hover 1.5 m in a small vortex of your own (8 Focus/s). Ground lines pass under you; projectiles curve around the vortex.",
		"module": "kit_air", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(8), "cancel": 0.5,
		"cost": 0.0, "kind": "hover", "height": 1.5, "upkeep": 8.0, "speed_mult": 0.8,
		"counter": {"cls": "eddy", "power": [10.0]},
		"anim": "glide", "fx": {"mat": "vortex", "aura": "aura"},
		"ai": {"role": "mobility", "range": [0.0, 8.0], "tags": ["hover", "immune_ground_lines", "vs_ground_line"]}})
	KitAir.handle("vortex_whirl", {"after": Callable(AirVortex, "whirl_after"), "phase": Callable(AirVortex, "whirl_phase"),
		"interrupt": Callable(AirVortex, "whirl_interrupt")})


## While the sidestep spins, light hostile projectiles near the fighter are met by the eddy cells (curved / deflected).
static func spin_tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.ACTIVE:
		return
	var c := Agent.of_move(w, a, "vortex_spin_step", inst.tier(), false)
	c.pos = a.chest()
	for b in w.bodies:
		if not AirUtil.hostile_shot(b, a) or b.pos.distance_to(a.chest()) > 1.6 + b.radius:
			continue
		if inst.data.get("spun", {}).has(b.id):
			continue
		var done: Dictionary = inst.data.get("spun", {})
		done[b.id] = true
		inst.data["spun"] = done
		c.dir = a.forward()
		Interactions.resolve(w, Agent.of_body(w, b, a), c, {"site": "spin"}, Interactions.PASS_RULE)


static func whirl_after(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	var phase := Verbs.after_startup(w, a, inst, it)
	if phase == ActionInst.P.CHANNEL:
		var z := AirUtil.zone(w, "eddy", a.pos, 1.3, a.id, 10.0, -1.0, {"attach": a.id, "attach_off": Vector3.ZERO, "height": 2.4, "rate": 0.1}, 0, SUB)
		z.spin = 12.0
		inst.data["whirl"] = z.id
	return phase


static func _whirl_end(w: CombatWorld, inst: ActionInst) -> void:
	var z := w.get_body(int(inst.data.get("whirl", -1)))
	inst.data.erase("whirl")
	if z != null and z.alive:
		w.close_zone(z, "whirl_end")


static func whirl_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		_whirl_end(w, inst)
	Verbs.on_phase(w, a, inst, p)


static func whirl_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	_whirl_end(w, inst)
	Verbs.on_interrupt(w, a, inst, reason)


# ================================================================ rules: the Vortex column (wall_vortex, tornado, eddy)

static func register_rules() -> void:
	Interactions.register_outcome("air_infuse", Callable(AirVortex, "o_infuse"))
	Interactions.register_outcome("air_catch", Callable(AirVortex, "o_catch"))
	Interactions.register_outcome("air_slow_bend", Callable(AirVortex, "o_slow_bend"))
	Interactions.register_outcome("air_spatter", Callable(AirVortex, "o_spatter"))
	Interactions.register_outcome("air_contest", Callable(AirVortex, "o_contest"))
	# Capture light bodies: P x eff >= TP/2 catches (partial or better), below only bends. Bodies > 30 kg only slow and bend.
	for c in ["tornado", "wall_vortex"]:
		var maxc := MAX_CAPTURED if c == "tornado" else 6
		# (bodies over 30 kg are slowed and bent inside o_infuse; the perfect Vortex Catch takes up to 45 kg)
		var cap := {"bands": [[0.0, "bend"], [0.5, "air_infuse"]], "perfect": "air_catch", "inert": "air_infuse", "inert_else": "pass",
			"max_captured": maxc, "bend_impulse": 60.0, "fallback": "capture"}
		var refc := {"move": "vortex_twister" if c == "tornado" else "vortex_wall", "tier": 2 if c == "tornado" else 0}
		# light solids, sand, water, vapour: caught (the infusion follows from what they are)
		for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "sand_cloud", "water", "mist", "steam"]:
			var ref := refc.duplicate()
			ref["expect"] = "air_infuse"
			AirRules._cell(t, c, cap, ref if t in ["stone", "metal", "sand", "water", "ice"] else {})
		AirRules._cell("sand_surge", c, cap, {})
		AirRules._cell("stone_heavy", c, {"bands": [[0.0, "air_slow_bend"]], "full_at": 0.0, "eff": 0.8, "bend_impulse": 60.0,
			"perfect": "air_catch", "fallback": "air_slow_bend"},
			{"move": refc.move, "tier": refc.tier, "expect": "air_slow_bend"})
		AirRules._cell("boulder", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": refc.move, "tier": refc.tier, "expect": "pass"})
		# lava: a partial picks up the spatter (a magma vortex), a full counter sets it into rock (Cyclone Fortress)
		for t in ["molten", "magma"]:
			AirRules._cell(t, c, {"bands": [[0.0, "pass"], [0.5, "air_spatter"], [1.2, "transform"]], "full_at": 1.2, "to": "rock", "hu_per_pu": 15.0,
				"heat_mult": 0.03, "liquid_floor": 0.5}, {})
		# water mass drowns a vortex; a small stream is caught (water tornado)
		AirRules._cell("water_wave", c, {"bands": [[0.0, "overwhelm"], [0.5, "air_infuse"]], "when": {"mass_max": AirUtil.LIGHT}, "else": "overwhelm",
			"max_captured": maxc}, {"move": refc.move, "tier": refc.tier, "expect": "overwhelm", "mass": 45.0, "tp": 45.0})
		AirRules._cell("vine", c, {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 1.0}, {})
		# flames are fed and spun (fire tornado); at >= 2x the whirl puts them out
		for t in ["flame", "blue_fire", "ember"]:
			AirRules._cell(t, c, {"bands": [[0.0, "air_infuse"], [2.0, "extinguish"]], "max_captured": maxc, "fallback": "amplify"},
				{"move": refc.move, "tier": refc.tier, "expect": "air_infuse", "tp": 20.0} if t == "flame" else {})
		AirRules._cell("lightning", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": refc.move, "tier": refc.tier, "expect": "pass"})
		AirRules._cell("blast", c, {"bands": [[0.0, "amplify"], [1.0, "weaken"]], "amp": 1.2}, {})
		AirRules._cell("gust", c, {"bands": [[0.0, "absorb"]], "full_at": 0.0}, {"move": refc.move, "tier": refc.tier, "expect": "absorb"})
		AirRules._cell("sound", c, {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 0.6}, {"move": refc.move, "tier": refc.tier, "expect": "weaken"})
		AirRules._cell("vacuum", c, {"bands": [[0.0, "overwhelm"], [1.0, "air_contest"]]}, {})
		AirRules._cell("tornado", c, {"bands": [[0.0, "air_contest"]], "full_at": 0.0}, {"move": refc.move, "tier": refc.tier, "expect": "air_contest"})
	# A lava wave: the front sets into rock at CP >= TP (Cyclone Fortress 35 vs 27 / T3 vs a 62 wave only crusts + spatter).
	AirRules._cell("lava_wave", "tornado", {"bands": [[0.0, "pass"], [0.5, "air_spatter"], [1.2, "transform"]], "full_at": 1.2, "to": "rock",
		"hu_per_pu": 15.0, "heat_mult": 0.03, "liquid_floor": 0.5},
		{"move": "vortex_twister", "tier": 3, "expect": "transform", "tp": 27.3, "mass": 20.0})
	AirRules._cell("lava_wave", "wall_vortex", {"bands": [[0.0, "pass"], [0.5, "air_spatter"], [1.2, "transform"]], "full_at": 1.2, "to": "rock",
		"hu_per_pu": 15.0, "heat_mult": 0.03, "liquid_floor": 0.5}, {})
	# the Eddy Ring curves projectiles around you: bend, orbit-deflect, and sometimes straight back at the thrower
	var orbit := {"bands": [[0.0, "pass"], [0.5, "bend"], [1.0, "deflect"], [1.5, "reflect"]], "eff": 1.5, "angle": 90.0, "side": 0.5, "up": 1.5}
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "water"]:
		AirRules._cell(t, "eddy", orbit, {"move": "vortex_eddy", "tier": 2, "expect": "reflect"} if t == "stone" else {})
	var orbit_h := orbit.duplicate(true)
	orbit_h["eff"] = 0.6
	AirRules._cell("stone_heavy", "eddy", orbit_h, {})
	AirRules._cell("boulder", "eddy", {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "vortex_eddy", "tier": 3, "expect": "pass"})
	for t in ["flame", "blue_fire", "ember"]:
		AirRules._cell(t, "eddy", {"bands": [[0.0, "amplify"], [1.0, "deflect"], [2.0, "extinguish"]], "amp": 1.2, "side": 0.5, "up": 1.5}, {})
	AirRules._cell("*", "eddy", {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "air_eddy_default"})


## Capture into a vortex (zone, twister, funnel wave): the body orbits, harmless, until the vortex ends or the caster
## unleashes it. A tornado takes the character of what it catches; enemy material turns it neutral.
static func o_infuse(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	var cb := c.body
	if b == null or cb == null or not b.alive or not cb.alive or cb == b:
		return false
	if not AirUtil.carriable(b, 1.0e9):
		return false
	if b.mass > float(r.get("max_mass", AirUtil.LIGHT)):
		# too heavy to catch: a hostile shot is slowed and bent, a resting heavy body is left alone
		if t.hostile:
			return o_slow_bend(w, t, c, res, r, ctx)
		res.pass_scale = 1.0
		AirOutcomes.report(res, "pass")
		return true
	var spd := 0.0
	if cb.form == Sim.Form.WAVE:
		spd = 16.0
	elif cb.form != Sim.Form.ZONE:
		spd = 12.0
	var rule := {"max_captured": int(r.get("max_captured", MAX_CAPTURED)), "release_speed": spd}
	var src := AirUtil.source_owner(b)           # before the capture makes the body harmless
	if not Outcomes.capture(w, t, c, res, rule, ctx):
		return false
	b.props["release_damage"] = 8.0 + 0.3 * b.mass
	b.props["caught_by"] = cb.id
	if c.perfect:
		b.props["caught_perfect"] = true
	var kind := infusion_kind(b)
	if cb.form == Sim.Form.ZONE and cb.tag == &"tornado" and kind != "":
		var caster := w.get_actor(cb.owner)
		var srca := w.get_actor(src)
		if cb.owner >= 0 and src >= 0 and src != cb.owner and srca != null and caster != null and srca.team != caster.team:
			make_neutral(w, cb, src)
	AirOutcomes.report(res, "capture", kind)
	return true


## Vortex Catch (a perfect Vortex Wall): catches shots up to 45 kg (not only 30) and marks them for a faster Unleash.
static func o_catch(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var rr := r.duplicate()
	rr["max_mass"] = 45.0
	var b := t.body
	if b != null and b.alive and b.mass > 45.0:
		return false
	var ok := o_infuse(w, t, c, res, rr, ctx)
	if ok:
		w.emit("vortex_catch", {"actor": Outcomes._id(c), "body": b.id, "mass": b.mass})
	return ok


## A body too heavy to catch: it is slowed and bent by the whirl.
static func o_slow_bend(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	Outcomes.slow(w, t, c, res, {"factor": 0.6}, ctx)
	Outcomes.bend(w, t, c, res, {"bend_impulse": float(r.get("bend_impulse", 60.0))}, ctx)
	AirOutcomes.report(res, "slow")
	return true


## A partial against lava: the wind cools it (heat_mult, booked ambient) and picks up spatter: a magma vortex for 4 s.
static func o_spatter(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	# the crust keeps a floor: wind alone does not set a lava wave that a tornado only out-pushes by < 20 %
	if t.body == null or t.body.liquid > float(r.get("liquid_floor", 0.0)):
		Outcomes.weaken(w, t, c, res, r, ctx)
	if c.body != null and c.body.alive and c.body.form == Sim.Form.ZONE:
		c.body.props["spatter_until"] = c.body.age + 4.0
	AirOutcomes.report(res, "amplify")
	return true


## Two whirls meet: the stronger absorbs the weaker (power, radius); equal powers: the lower id keeps going.
static func o_contest(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var tz := t.body
	var cz := c.body
	if tz == null or cz == null or not tz.alive or not cz.alive or tz == cz:
		return false
	var tp := maxf(tz.power, 0.001)
	var cp := maxf(cz.power, 0.001)
	var win := cz if (cp > tp or (is_equal_approx(cp, tp) and cz.id < tz.id)) else tz
	var lose := tz if win == cz else cz
	win.power = minf(45.0, win.power + 0.3 * lose.power)
	win.zone_radius = minf(5.0, win.zone_radius + 0.3)
	win.radius = win.zone_radius
	for id in lose.captured.duplicate():
		var cap := w.get_body(id)
		if cap != null and cap.alive and cap.captured_by == lose.id:
			cap.captured_by = win.id
			win.captured.append(id)
			lose.captured.erase(id)
	if lose.form == Sim.Form.ZONE:
		w.close_zone(lose, "absorbed")
	else:
		w.decay_body(lose, "absorbed")
	res.stopped = true
	res.pass_scale = 0.0
	w.emit("tornado_contest", {"winner": win.id, "loser": lose.id})
	AirOutcomes.report(res, "absorb")
	return true
