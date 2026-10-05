class_name FireCombustion
extends RefCounted
## Fire / Combustion (sub 3, docs/MOVESET.md §7.12): superheat a pocket of air until it detonates (heat paid like Fire,
## spent by the blast). Blasts shatter brittle things (ice / glass x2), snuff flames (oxygen), disperse clouds; vacuum
## suppresses them, mist / steam halve them, moving air fans them (+30 % radius); right after a vacuum collapses the
## inrush makes them x1.5; a fuse inside a tornado makes a fire tornado (FireUtil.detonate).
##   strike  Pop (palm) -> Burst (6 m, fuse) -> Blast (9 m) -> Detonation (12 m, r 4.5, 0.5 s fuse)
##   thrust  Spark Mine (sticks; proximity / contact / your next A-up detonates it)
##   ground  Chain Blasts · sweep Scatter Charges · guard Reactive Blast (8 Focus per trigger) · push Shockwave
##   sink    Smother Blast (+ blast jump) · tech Fuse · evade Blast Jump · evade_hold Afterglow

const E := 2
const SUB := 3
const REACTIVE_COST := 8.0
const MINE_RANGE := 1.5
const MINE_LIFE := 12.0


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
	for pair in [["strike", "pop"], ["thrust", "spark_mine"], ["ground", "chain_blasts"], ["sweep", "scatter_charges"],
			["guard", "reactive_blast"], ["push", "shockwave"], ["sink", "smother_blast"], ["tech", "fuse"], ["evade", "blast_jump"],
			["evade_hold", "afterglow"]]:
		Moves.bind(E, SUB, pair[0], pair[1])
	KitFire.handle("spark_mine", {"start": Callable(FireCombustion, "mine_start")})
	KitFire.handle("fuse", {"after": Callable(FireCombustion, "fuse_after"), "tick": Callable(FireCombustion, "fuse_tick_action"),
		"phase": Callable(FireCombustion, "fuse_phase"), "interrupt": Callable(FireCombustion, "fuse_interrupt")})
	KitFire.handle("blast_jump", {"start": Callable(FireCombustion, "jump_start")})
	KitFire.handle("afterglow", {"tick": Callable(FireCombustion, "afterglow_tick")})
	CombatWorld.register_body_tick(&"ember", Callable(FireCombustion, "ember_tick"))
	CombatWorld.register_zone_effect(&"fuse", Callable(FireCombustion, "fuse_zone_tick"))
	Interactions.register_tag_class(&"fuse", &"ember", &"fuse")


# ------------------------------------------------------------------ fused pockets (shared by strike, chain, fuse)

## A superheated air pocket (zone tag "fuse", kit-driven: the core fuse timer is parked) holding the paid heat; it
## detonates after `delay` s (delay < 0: on demand) through FireUtil.detonate.
static func pocket(w: CombatWorld, a: ActorState, p: Vector3, delay: float, prm: Dictionary, heat: float, tier: int, mode: String = "strike") -> MatBody:
	var z := w.spawn_zone(&"fuse", p, 0.35, a.id if a != null else -1, float(prm.get("power", 8.0)), Sim.Mat.AIR, 0.0, -1.0, "fuse:%d" % (a.id if a != null else -1))
	z.props["fuse"] = 1.0e9          # the core fuse timer never fires it
	z.props["fire_fuse"] = delay
	z.props["prm"] = prm
	z.props["mode"] = mode
	z.props["spare_owner"] = true
	z.heat_payload = heat            # paid heat waits in the pocket (counted by thermal_energy)
	z.tier = tier
	z.sub = SUB
	FxEvents.fx(w, "cast", "blast", {"actor": a.id if a != null else -1, "pos": p, "dur": maxf(delay, 0.1), "radius": float(prm.get("radius", 2.0)),
		"power": float(prm.get("power", 8.0)), "tier": tier, "element": E, "sub": SUB, "move": String(prm.get("move", "")), "shape": "small"})
	return z


static func fire_pocket(w: CombatWorld, z: MatBody) -> Dictionary:
	if z == null or not z.alive:
		return {}
	var prm: Dictionary = (z.props.get("prm", {}) as Dictionary).duplicate()
	prm["heat_hu"] = z.heat_payload
	prm["tier"] = z.tier
	z.heat_payload = 0.0
	var owner := w.get_actor(z.owner)
	var p := z.pos
	var mode := String(z.props.get("mode", "strike"))
	var inrush := int(z.props.get("inrush_tick", -1000000))
	w.close_zone(z, "detonated")
	return FireUtil.detonate(w, owner, null, p, prm, mode, inrush)


## Fuse pockets: count down; track vacuum zones nearby (their collapse = air inrush, x1.5 within 0.5 s).
static func fuse_zone_tick(w: CombatWorld, z: MatBody, _dt: float) -> void:
	if not z.props.has("fire_fuse"):
		return
	var seen: Dictionary = z.props.get("vac", {})
	for id in seen.keys():
		var v := w.get_body(int(id))
		if v == null or not v.alive:
			z.props["inrush_tick"] = w.tick
			seen.erase(id)
	for b in w.bodies:
		if b.alive and b.form == Sim.Form.ZONE and b != z and String(Interactions.classify(b)) == "vacuum" and b.pos.distance_to(z.pos) < b.zone_radius + 4.0:
			seen[b.id] = true
	z.props["vac"] = seen
	var delay := float(z.props.fire_fuse)
	if delay >= 0.0 and z.age >= delay - 1e-6:
		fire_pocket(w, z)


# ------------------------------------------------------------------ strike: Pop -> Burst -> Blast -> Detonation

static func _strike() -> void:
	Moves.register("pop", {
		"element": E, "sub": SUB, "slot": "strike", "name": "Pop / Burst / Blast / Detonation",
		"desc": "Tap: a palm detonation (2 m, knock 6). Hold: a Burst 6 m ahead, a Blast at 9 m (r 3), then the Detonation at 12 m (r 4.5, 0.5 s fuse). Blasts shatter ice and glass, snuff flames, disperse clouds; vacuum suppresses them, mist halves them, wind fans them.",
		"module": "verbs", "verb": "burst",
		"startup": _s(8), "active": _s(4), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"heat": 40.0, "at": "ahead", "distance": 1.4, "radius": 2.0, "power": 8.0, "damage": 7.0, "balance": 18.0, "knock": 6.0, "lift": 1.5,
		"fuse": 0.0,
		"tiers": {
			"t1": {"heat_add": 60.0, "at": "aim", "distance": 6.0, "range": 6.0, "radius": 2.0, "power": 16.0, "fuse": 0.25, "damage": 12.0, "balance": 28.0},
			"t2": {"heat_add": 160.0, "at": "aim", "distance": 9.0, "range": 9.0, "radius": 3.0, "power": 22.0, "fuse": 0.25, "damage": 16.0, "balance": 36.0, "knock": 7.0},
			"t3": {"heat_add": 300.0, "at": "aim", "distance": 12.0, "range": 12.0, "radius": 4.5, "power": 34.0, "fuse": 0.5, "damage": 22.0, "balance": 50.0,
				"knock": 9.0, "lift": 3.0},
		},
		"hook_execute": Callable(FireCombustion, "pop_execute"),
		"counter": {"cls": "blast", "power": [8.0, 16.0, 22.0, 34.0]}, "threat": {"cls": "blast"},
		"anim": "fire_jab", "anim_charge": "fire_charge", "anim_active": "fire_release", "anim_t3": "mv_overhead_slam",
		"fx": {"mat": "blast", "shape": ""},
		"ai": {"role": "poke", "range": [0.0, 12.0], "tags": ["area", "shatter_ice", "snuff_fire", "disperse", "fuse"]},
	})


static func _prm(inst: ActionInst) -> Dictionary:
	return {"radius": float(Charge.param(inst, "radius", 2.0)), "power": float(Charge.param(inst, "power", 8.0)),
		"damage": float(Charge.param(inst, "damage", 8.0)), "balance": float(Charge.param(inst, "balance", 20.0)),
		"knock": float(Charge.param(inst, "knock", 6.0)), "lift": float(Charge.param(inst, "lift", 2.0)), "move": inst.id, "tier": inst.tier()}


static func pop_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var heat := Verbs.take_heat(inst)
	var prm := _prm(inst)
	var at := String(Charge.param(inst, "at", "ahead"))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var p := a.pos + dir * float(Charge.param(inst, "distance", 1.4)) + Vector3(0, 1.0, 0)
	if at == "aim":
		p = FireUtil.aim_ground(w, a, inst, float(Charge.param(inst, "range", 6.0))) + Vector3(0, 1.0, 0)
	var fuse := float(Charge.param(inst, "fuse", 0.0))
	if fuse > 0.0:
		pocket(w, a, p, fuse, prm, heat, inst.tier())
	else:
		prm["heat_hu"] = heat
		FireUtil.detonate(w, a, inst, p, prm)
	return true


# ------------------------------------------------------------------ thrust: Spark Mine

static func _thrust() -> void:
	Moves.register("spark_mine", {
		"element": E, "sub": SUB, "slot": "thrust", "name": "Spark Mine",
		"desc": "Throw an ember @14 m/s that sticks where it lands; it detonates when a rival comes within 1.5 m, on contact, or when you flick up again.",
		"module": "kit_fire", "verb": "projectile",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"heat": 60.0, "source": "heat", "mat": "fire", "tag": "ember", "mass": 0.3, "speed": 14.0, "gravity": 1.0, "arc": true, "reach": 10.0,
		"damage": 4.0, "balance": 8.0, "life": 2.5, "on_impact": "stick",
		"mine_power": 12.0, "mine_radius": 1.8, "mine_damage": 10.0,
		"tiers": {
			"t1": {"heat_add": 20.0, "mine_power": 16.0, "mine_radius": 2.0, "mine_damage": 12.0},
			"t2": {"heat_add": 50.0, "mine_power": 22.0, "mine_radius": 2.4, "mine_damage": 15.0},
			"t3": {"heat_add": 90.0, "mine_power": 30.0, "mine_radius": 3.0, "mine_damage": 20.0},
		},
		"hook_execute": Callable(FireCombustion, "mine_execute"),
		"hook_impact": Callable(FireCombustion, "mine_impact"),
		"counter": {"cls": "blast", "power": [12.0, 16.0, 22.0, 30.0]}, "threat": {"cls": "ember"},
		"anim": "fire_jab", "anim_active": "earth_throw", "fx": {"mat": "blast", "shape": "ember"},
		"ai": {"role": "setup", "range": [3.0, 10.0], "tags": ["trap", "remote_detonate", "area_denial"]},
	})


static func _mines_of(w: CombatWorld, a: ActorState) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in w.bodies:
		if b.alive and b.tag == &"ember" and b.props.get("mine", false) and int(b.props.get("mine_owner", -1)) == a.id:
			out.append(b)
	return out


## A second flick up while your mines are out detonates them (nothing is thrown, nothing paid).
static func mine_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if not _mines_of(w, a).is_empty():
		inst.data["detonate"] = true
		inst.data["face"] = w.aim_dir(a, it)
		Verbs.fx(w, a, inst, "cast", {"shape": "small"})
		return
	Verbs.on_start(w, a, inst, it)


static func mine_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	if inst.data.get("detonate", false):
		for m in _mines_of(w, a):
			pop_ember(w, m, "remote")
		w.emit("mine_detonate", {"actor": a.id})
		return true
	var bodies := VerbProjectile.fire(w, a, inst)
	for b in bodies:
		b.props["mine_power"] = float(Charge.param(inst, "mine_power", 12.0))
		b.props["mine_radius"] = float(Charge.param(inst, "mine_radius", 1.8))
		b.props["mine_damage"] = float(Charge.param(inst, "mine_damage", 10.0))
		b.props["mine_owner"] = a.id
	return true


## The ember lands: on a fighter it pops at once, elsewhere it sticks and arms as a mine.
static func mine_impact(w: CombatWorld, b: MatBody, what: String) -> bool:
	if what == "actor":
		pop_ember(w, b, "contact")
		return true
	b.vel = Vector3.ZERO
	b.on_ground = true
	b.attack_id = 0
	b.gravity_scale = 0.0
	b.static_body = true
	b.props["mine"] = true
	b.max_life = -1.0
	b.props["mine_until"] = b.age + MINE_LIFE
	b.pos.y = maxf(b.pos.y, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3) + 0.08)
	w.emit("stick", {"body": b.id, "on": what, "mine": true})
	return true


## Embers: mines watch for rivals within 1.5 m; scatter charges pop after their delay; a mine left too long fizzles.
static func ember_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if not b.alive or b.mat != Sim.Mat.FIRE:
		return false
	if b.props.has("pop_at") and b.age >= float(b.props.pop_at):
		pop_ember(w, b, "timer")
		return true
	if b.props.get("mine", false):
		var owner := int(b.props.get("mine_owner", -1))
		var ow := w.get_actor(owner)
		for a in w.actors:
			if a.health <= 0.0 or a.id == owner or (ow != null and a.team == ow.team):
				continue
			if Vector2(a.pos.x - b.pos.x, a.pos.z - b.pos.z).length() <= MINE_RANGE and absf(a.pos.y - b.pos.y) < 2.0:
				pop_ember(w, b, "proximity")
				return true
		if b.age >= float(b.props.get("mine_until", MINE_LIFE)):
			w.emit("extinguish", {"body": b.id, "by": "time"})
			w.decay_body(b, "fizzled")
		return true
	return false


## An ember detonates where it is (its payload is the blast's heat).
static func pop_ember(w: CombatWorld, b: MatBody, why: String) -> void:
	if b == null or not b.alive:
		return
	var owner := w.get_actor(int(b.props.get("mine_owner", b.attack_owner if b.attack_owner >= 0 else b.residual_owner)))
	var p := b.pos + Vector3(0, 0.4, 0)
	var heat := b.heat_payload
	b.heat_payload = 0.0
	var prm := {"radius": float(b.props.get("mine_radius", 1.8)), "power": float(b.props.get("mine_power", 10.0)),
		"damage": float(b.props.get("mine_damage", 8.0)), "balance": 22.0, "knock": 6.0, "lift": 2.0, "heat_hu": heat,
		"move": String(b.props.get("move", "spark_mine")), "tier": b.tier}
	b.props.erase("mine")
	b.attack_id = 0
	w.decay_body(b, "detonated")
	FireUtil.detonate(w, owner, null, p, prm)
	w.emit("ember_pop", {"body": b.id, "why": why, "pos": p})


# ------------------------------------------------------------------ ground: Chain Blasts

static func _ground() -> void:
	Moves.register("chain_blasts", {
		"element": E, "sub": SUB, "slot": "ground", "name": "Chain Blasts",
		"desc": "Three detonations step along the ground every 0.15 s (P 10 each). Held: four, five, six.",
		"module": "verbs", "verb": "burst",
		"startup": _s(14), "active": _s(30), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"heat": 150.0, "steps": 3, "step": 1.7, "step_t": 0.15, "radius": 1.6, "power": 10.0, "damage": 7.0, "balance": 18.0, "knock": 5.0, "lift": 2.0,
		"tiers": {
			"t1": {"steps": 4, "heat_add": 50.0},
			"t2": {"steps": 5, "heat_add": 100.0, "radius": 1.8},
			"t3": {"steps": 6, "heat_add": 160.0, "radius": 2.0, "power": 12.0},
		},
		"hook_execute": Callable(FireCombustion, "chain_execute"),
		"counter": {"cls": "blast", "power": 10.0}, "threat": {"cls": "blast"},
		"anim": "mv_stomp", "anim_active": "fire_release", "fx": {"mat": "blast", "shape": "ground"},
		"ai": {"role": "zone", "range": [1.5, 11.0], "tags": ["ground", "line", "multi"]},
	})


static func chain_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var heat := Verbs.take_heat(inst)
	var n := int(Charge.param(inst, "steps", 3))
	var step := float(Charge.param(inst, "step", 1.7))
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	dir.y = 0.0
	dir = dir.normalized() if dir.length() > 0.01 else a.forward()
	var prm := _prm(inst)
	for k in n:
		var p := a.pos + dir * step * float(k + 1)
		p.y = w.arena.ground_height(p.x, p.z, a.pos.y + 0.5) + 0.6
		if w.arena.segment_hit(a.chest(), p) >= 0.0:
			w.ledger.spent += heat * float(n - k) / float(n)
			break
		pocket(w, a, p, float(Charge.param(inst, "step_t", 0.15)) * float(k), prm, heat / float(n), inst.tier())
	return true


# ------------------------------------------------------------------ sweep: Scatter Charges

static func _sweep() -> void:
	Moves.register("scatter_charges", {
		"element": E, "sub": SUB, "slot": "sweep", "name": "Scatter Charges",
		"desc": "Four embers fanned out to 6 m; each pops after 0.4 s (P 8). Area denial.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(10), "active": _s(4), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"heat": 120.0, "source": "heat", "mat": "fire", "tag": "ember", "mass": 0.2, "count": 4, "spread": 80.0, "speed": 9.0,
		"gravity": 1.0, "arc": true, "reach": 6.0, "damage": 3.0, "balance": 6.0, "life": 2.0, "pop": 0.4,
		"mine_power": 8.0, "mine_radius": 1.6, "mine_damage": 6.0,
		"tiers": {
			"t1": {"count": 5, "heat_add": 40.0, "spread": 100.0},
			"t2": {"count": 6, "heat_add": 90.0, "spread": 120.0, "mine_power": 10.0},
			"t3": {"count": 8, "heat_add": 160.0, "spread": 160.0, "mine_power": 12.0, "mine_radius": 1.8},
		},
		"hook_execute": Callable(FireCombustion, "scatter_execute"),
		"counter": {"cls": "blast", "power": [8.0, 8.0, 10.0, 12.0]}, "threat": {"cls": "ember"},
		"anim": "mv_wide_draw", "anim_active": "fire_release", "fx": {"mat": "blast", "shape": "ember"},
		"ai": {"role": "zone", "range": [2.0, 7.0], "tags": ["area_denial", "multi"]},
	})


static func scatter_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var bodies := VerbProjectile.fire(w, a, inst)
	for k in bodies.size():
		var b: MatBody = bodies[k]
		b.props["pop_at"] = float(Charge.param(inst, "pop", 0.4)) + 0.03 * float(k)
		b.props["mine_power"] = float(Charge.param(inst, "mine_power", 8.0))
		b.props["mine_radius"] = float(Charge.param(inst, "mine_radius", 1.6))
		b.props["mine_damage"] = float(Charge.param(inst, "mine_damage", 6.0))
		b.props["mine_owner"] = a.id
		b.props.erase("on_impact")
	return true


# ------------------------------------------------------------------ guard: Reactive Blast

static func _guard() -> void:
	Moves.register("reactive_blast", {
		"element": E, "sub": SUB, "slot": "guard", "name": "Reactive Blast",
		"desc": "When hit, detonate outward (CP 18, 8 Focus per trigger): light solids are blown aside, flames snuffed (x1.5), clouds dispersed. Perfect: light solids are blasted back at the thrower.",
		"module": "kit_fire", "verb": "barrier", "barrier": "aura", "startup": 0.0, "recovery": _s(8),
		"counter": {"cls": "guard_blast", "power": [18.0, 18.0, 18.0, 18.0]}, "move_channel": 0.35,
		"anim": "guard", "anim_active": "deflect", "fx": {"mat": "blast", "shape": ""},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["deflect_light", "snuff_fire", "reflect_perfect"]},
	})


# ------------------------------------------------------------------ push / sink: Shockwave, Smother Blast

static func _push_sink() -> void:
	Moves.register("shockwave", {
		"element": E, "sub": SUB, "slot": "push", "name": "Shockwave",
		"desc": "From the guard: a forward cone of blast (5 m, P 14): snuffs flames, shatters ice, pushes.",
		"module": "verbs", "verb": "cone", "cls": "blast", "channel": "P",
		"startup": _s(8), "active": _s(4), "recovery": _s(16), "cancel": 0.6,
		"heat": 80.0, "range": 5.0, "angle": 32.0, "power": 14.0, "damage": 8.0, "balance": 28.0, "knock": 8.0, "lift": 1.5,
		"counter": {"cls": "blast", "power": 14.0}, "threat": {"cls": "blast"},
		"anim": "mv_push_two_hand", "anim_active": "fire_release", "fx": {"mat": "blast", "shape": "open"},
		"ai": {"role": "counter", "range": [0.0, 5.0], "tags": ["snuff_fire", "knockback", "after_guard"]},
	})
	Moves.register("smother_blast", {
		"element": E, "sub": SUB, "slot": "sink", "name": "Smother Blast",
		"desc": "From the guard: a downward blast (P 10) that snuffs every fire field within 4 m and launches you 1.5 m up.",
		"module": "verbs", "verb": "burst", "at": "self",
		"startup": _s(6), "active": _s(4), "recovery": _s(14), "cancel": 0.6,
		"heat": 80.0, "radius": 4.0, "power": 10.0, "damage": 5.0, "balance": 16.0, "knock": 4.0, "lift": 2.0, "jump": 1.5,
		"hook_execute": Callable(FireCombustion, "smother_execute"),
		"counter": {"cls": "blast", "power": 10.0}, "threat": {"cls": "blast"},
		"anim": "jump", "anim_active": "mv_stomp", "fx": {"mat": "blast", "shape": "ground"},
		"ai": {"role": "counter", "range": [0.0, 4.0], "tags": ["snuff_fire", "escape_up"]},
	})


static func smother_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var prm := _prm(inst)
	prm["heat_hu"] = Verbs.take_heat(inst)
	FireUtil.detonate(w, a, inst, a.pos + Vector3(0, 0.3, 0), prm)
	a.vel.y = sqrt(2.0 * Sim.GRAVITY * float(Charge.param(inst, "jump", 1.5)))
	a.grounded = false
	w.emit("blast_jump", {"actor": a.id, "height": float(Charge.param(inst, "jump", 1.5))})
	return true


# ------------------------------------------------------------------ tech: Fuse

static func _tech() -> void:
	Moves.register("fuse", {
		"element": E, "sub": SUB, "slot": "tech", "name": "Fuse",
		"desc": "Hold: superheat a pocket of air at the aim point (10 m; steer it while holding, it grows with the hold). Release: it detonates there. In vapour x0.5, in a vacuum it fails, right after a vacuum well collapses x1.5; inside a tornado it makes a fire tornado.",
		"module": "kit_fire", "verb": "summon",
		"startup": _s(12), "active": _s(4), "recovery": _s(16), "cancel": 0.5,
		"cost": 6.0, "range": 10.0, "steer_speed": 8.0, "fuse_hu": 60.0, "radius": 2.2, "power": 14.0, "damage": 10.0, "balance": 26.0,
		"knock": 6.0, "lift": 2.0,
		"tiers": {
			"t1": {"fuse_hu": 120.0, "radius": 2.8, "power": 20.0, "damage": 14.0, "balance": 32.0, "charge_drain": 4.0},
			"t2": {"fuse_hu": 200.0, "radius": 3.4, "power": 28.0, "damage": 18.0, "balance": 40.0, "charge_drain": 6.0},
			"t3": {"fuse_hu": 320.0, "radius": 4.2, "power": 38.0, "damage": 24.0, "balance": 52.0, "knock": 9.0, "charge_drain": 8.0},
		},
		"counter": {"cls": "blast", "power": [14.0, 20.0, 28.0, 38.0]}, "threat": {"cls": "blast"},
		"anim": "fire_charge", "anim_hold": "fire_charge", "anim_active": "fire_release", "fx": {"mat": "blast", "shape": ""},
		"ai": {"role": "finisher", "range": [3.0, 10.0], "tags": ["remote", "around_cover", "fire_tornado", "inrush_x1_5"]},
	})
	CombatWorld.register_tech_preview(E, SUB, Callable(FireCombustion, "fuse_preview"))


static func fuse_preview(_w: CombatWorld, _a: ActorState, _dir: Vector3) -> Dictionary:
	return {"mode": "FUSE", "body": -1, "ok": true, "reason": "", "label": "FUSE"}


static func fuse_after(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	var p := FireUtil.aim_ground(w, a, inst, float(Charge.param(inst, "range", 10.0))) + Vector3(0, 1.0, 0)
	var z := pocket(w, a, p, -1.0, _prm(inst), 0.0, 0, "fuse")
	inst.data["pocket"] = z.id
	return ActionInst.P.CHANNEL


static func fuse_tick_action(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.CHANNEL:
		Verbs.on_tick(w, a, inst, it)
		return
	var z := w.get_body(int(inst.data.get("pocket", -1)))
	if it.tech_cancel or z == null or not z.alive:
		if z != null and z.alive:
			w.close_zone(z, "cancelled")
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["aim"] = w.aim_dir(a, it)
	inst.data["aim_active"] = it.aim_active
	inst.data["aim_point"] = w.aim_point(a, it)
	var want := FireUtil.aim_ground(w, a, inst, float(Charge.param(inst, "range", 10.0))) + Vector3(0, 1.0, 0)
	var to := want - z.pos
	var sp := float(Charge.param(inst, "steer_speed", 8.0))
	if to.length() > 0.05:
		z.pos += to.normalized() * minf(sp * Sim.DT, to.length())
	z.tier = inst.tier()
	z.props["prm"] = _prm(inst)
	if w.tick % 15 == 0:
		FxEvents.fx(w, "aura", "blast", {"actor": a.id, "pos": z.pos, "radius": float(Charge.param(inst, "radius", 2.2)) * 0.3,
			"power": float(Charge.param(inst, "power", 14.0)), "tier": inst.tier(), "on": true, "shape": "small", "body": z.id})
	if not Charge.held(inst, it):
		inst.data["released"] = true
		w.set_phase(a, inst, ActionInst.P.ACTIVE)


static func fuse_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.ACTIVE:
		var z := w.get_body(int(inst.data.get("pocket", -1)))
		if z != null and z.alive:
			z.heat_payload = w.pay_heat(a, float(Charge.param(inst, "fuse_hu", 60.0)))
			z.props["prm"] = _prm(inst)
			z.tier = inst.tier()
			var out := fire_pocket(w, z)
			w.emit("fuse", {"actor": a.id, "tier": inst.tier(), "mods": out.get("mods", []), "suppressed": out.get("suppressed", false)})
		return
	Verbs.on_phase(w, a, inst, p)


static func fuse_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	var z := w.get_body(int(inst.data.get("pocket", -1)))
	if z != null and z.alive and int(z.props.get("fire_fuse", 0)) < 0:
		w.close_zone(z, "interrupted")
	Verbs.on_interrupt(w, a, inst, reason)


# ------------------------------------------------------------------ evade: Blast Jump, Afterglow

static func _mobility() -> void:
	Moves.register("blast_jump", {
		"element": E, "sub": SUB, "slot": "evade", "name": "Blast Jump",
		"desc": "A detonation at your feet launches you 4 m along the stick (straight up if neutral); it shoves whoever stands next to you.",
		"module": "kit_fire", "verb": "dash", "startup": 0.0, "active": _s(16), "recovery": _s(8),
		"cost": 5.0, "heat": 30.0, "distance": 4.0, "iframes": _s(7), "dir": "stick", "up": 6.0,
		"anim": "jump", "anim_active": "air_dash", "fx": {"mat": "blast", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["jump", "escape", "ledge"]},
	})
	Moves.register("afterglow", {
		"element": E, "sub": SUB, "slot": "evade_hold", "name": "Afterglow",
		"desc": "Hold evade: hover on small pops for 0.8 s.",
		"module": "kit_fire", "verb": "mode", "kind": "hover", "height": 1.4, "upkeep": 6.0, "speed_mult": 0.7, "hover_t": 0.8,
		"startup": 0.0, "active": 0.0, "recovery": _s(8),
		"anim": "glide", "fx": {"mat": "blast", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 3.0], "tags": ["hover", "dodge_ground"]},
	})


static func jump_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var neutral := Vector3(it.move.x, 0.0, it.move.z).length() < 0.2
	if neutral:
		FireUtil.with_params(inst, {"distance": 0.3, "up": 11.0})
	Verbs.on_start(w, a, inst, it)
	if inst.data.get("fizzle", false):
		return
	var prm := {"radius": 1.3, "power": 5.0, "damage": 3.0, "balance": 14.0, "knock": 5.0, "lift": 1.0, "heat_hu": Verbs.take_heat(inst),
		"move": inst.id, "tier": 0}
	FireUtil.detonate(w, a, inst, a.pos + Vector3(0, 0.2, 0), prm)


static func afterglow_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action != inst or inst.phase != ActionInst.P.CHANNEL:
		return
	if inst.t >= float(Charge.param(inst, "hover_t", 0.8)):
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	if w.tick % 12 == 0:
		FxEvents.fx_for(w, a, inst, "burst", "blast", {"pos": a.pos + Vector3(0, -0.1, 0), "radius": 0.5, "power": 2.0, "shape": "small"})
