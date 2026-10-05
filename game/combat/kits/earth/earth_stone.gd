class_name EarthStone
extends RefCounted
## Earth / Stone (sub 0, the legacy kit) - docs/MOVESET.md §7.1.
## Legacy moves stay exactly as they are for T0/T1 (earth_attack, earth_tech, the Bulwark guard, evade):
##   * earth_attack gains T2 Boulder (65 kg) and T3 Crag Breaker (80 kg, splits into 3 rubble on impact):
##     tier data is added to the live def (ActEarth grows the held stone on the tier-up). T1 keeps no drain.
##   * earth_tech gains T+A Split (ActEarth): the seized stone becomes 3 spikes fanned 30° back at the target.
##   * the Bulwark (legacy wall) thickens with guard hold: 120 -> 160 kg at 1.0 s, 200 kg at 1.8 s
##     (CP 30 / 40 / 50), applied when the wall acts as a counter (channel hook on untagged walls).
## New slots: thrust Spear Stone, ground Rising Fangs / Earthrise, sweep Rubble Fan, push Ram Wall,
## sink Swallow, evade_hold Stone Skin (neutral) / Burrow Step (with a direction).

const BULWARK_MASS := [120.0, 160.0, 200.0]
const BULWARK_TIMES := [1.0, 1.8]
const RAM_SPEED := 9.0
const SPIKE_CP := 20.0
const SPIKE_LIFE := 1.5

static var _prev_channel_hook := Callable()


static func register() -> void:
	_extend_legacy()
	# ---------------------------------------------------------------- thrust: Spear Stone
	KitEarth.reg("spear_stone", 0, "thrust", {"name": "Spear Stone", "part": "stone",
		"desc": "A flat 12 kg stone spear ripped from the ground @26 m/s. Hold: twin, triple, Pike Volley.",
		"verb": "projectile", "startup": KitEarth.f(12), "active": KitEarth.f(4), "recovery": KitEarth.f(20), "cancel": 0.6, "chain": 0.25,
		"cost": 8.0, "source": "ground", "mat": "stone", "tag": "spear", "mass": 12.0, "speed": 26.0, "gravity": 0.4,
		"damage": 10.0, "balance": 18.0, "count": 1,
		"tiers": {"t1": {"count": 2, "spread": 6.0, "cost_add": 3.0}, "t2": {"count": 3, "spread": 8.0}, "t3": {"count": 5, "spread": 14.0, "speed": 30.0}},
		"threat": {"cls": "stone", "power": [15.6, 15.6, 15.6, 18.0]},
		"anim": "earth_lift", "anim_active": "earth_throw",
		"fx": {"mat": "stone", "shape": "spear"},
		"ai": {"role": "poke", "range": [3.0, 14.0], "tags": ["projectile", "pierce_cloud", "ranged"]}})
	# ---------------------------------------------------------------- ground: Rising Fangs / Earthrise
	KitEarth.reg("rising_fangs", 0, "ground", {"name": "Rising Fangs", "part": "stone",
		"desc": "A spike line races 9 m along the ground and launches; the spikes stand 1.5 s as a low wall that stops ground lines. T3 Earthrise: a ring of spikes around you.",
		"verb": "ground_line", "startup": KitEarth.f(16), "active": KitEarth.f(6), "recovery": KitEarth.f(22), "cancel": 0.6, "chain": 0.25,
		"cost": 9.0, "source": "ground", "mat": "stone", "tag": "spike_line", "mass": 24.0, "speed": 14.0, "budget": 9.0,
		"width": 1.4, "damage": 12.0, "balance": 34.0, "knock": 2.0, "lift": 6.0, "kind": "stone", "spike_h": 0.45,
		"tiers": {"t1": {"budget": 12.0, "mass": 30.0, "cost_add": 3.0}, "t2": {"budget": 12.0, "width": 2.0, "spike_h": 0.7, "mass": 36.0},
			"t3": {"earthrise": true, "radius": 3.5, "mass": 60.0, "damage": 16.0}},
		"counter": {"cls": "spikes", "power": [18.0, 24.0, 30.0, 40.0]}, "threat": {"cls": "spikes"},
		"hook_execute": Callable(EarthStone, "fangs_execute"),
		"anim": "earth_wall", "fx": {"mat": "stone", "release": "erupt"},
		"ai": {"role": "zone", "range": [2.0, 10.0], "tags": ["ground_line", "launcher", "stops_waves"]}})
	# ---------------------------------------------------------------- sweep: Rubble Fan
	KitEarth.reg("rubble_fan", 0, "sweep", {"name": "Rubble Fan", "part": "stone",
		"desc": "3 x 6 kg rubble fanned 40°. Hold: 5, 7, then 9 skipping stones.",
		"verb": "projectile", "startup": KitEarth.f(12), "active": KitEarth.f(6), "recovery": KitEarth.f(20), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "source": "ground", "mat": "stone", "tag": "rubble", "mass": 6.0, "speed": 18.0, "gravity": 0.8, "count": 3,
		"spread": 40.0, "damage": 5.0, "balance": 10.0, "reach": 8.0,
		"tiers": {"t1": {"count": 5, "spread": 50.0, "cost_add": 2.0}, "t2": {"count": 7, "spread": 60.0}, "t3": {"count": 9, "spread": 70.0, "ricochet": 1}},
		"threat": {"cls": "stone", "power": [5.4, 5.4, 5.4, 5.4]},
		"anim": "earth_throw", "fx": {"mat": "stone", "shape": "fan"},
		"ai": {"role": "poke", "range": [1.5, 8.0], "tags": ["projectile", "multi_hit", "area"]}})
	# ---------------------------------------------------------------- push: Ram Wall
	KitEarth.reg("ram_wall", 0, "push", {"name": "Ram Wall", "part": "stone", "module": "kit_earth",
		"desc": "The standing wall slides 6 m @9 m/s, shoving bodies and waves back; pushing an enemy wall is a contest (your K vs their CP). Then it crumbles to rubble.",
		"verb": "ram", "startup": KitEarth.f(10), "active": KitEarth.f(40), "recovery": KitEarth.f(18), "cancel": 0.6,
		"cost": 6.0, "speed": RAM_SPEED, "damage": 12.0, "balance": 40.0, "knock": 7.0,
		"counter": {"cls": "ram", "power": [54.0, 72.0, 90.0, 90.0]}, "threat": {"cls": "wall_stone"},
		"anim": "earth_heavy", "fx": {"mat": "stone", "cast": "cast", "release": "trail", "impact": "burst"},
		"ai": {"role": "counter", "range": [0.0, 7.0], "tags": ["needs_wall", "pushes_waves", "wall_contest"]}})
	# ---------------------------------------------------------------- sink: Swallow
	KitEarth.reg("swallow", 0, "sink", {"name": "Swallow", "part": "stone", "module": "kit_earth",
		"desc": "The ground opens in a 6 m 60° cone: solids sink (booked as ground_returned), lava waves drain into the trench. Strength from guard hold time.",
		"verb": "sink", "startup": KitEarth.f(6), "active": KitEarth.f(18), "recovery": KitEarth.f(16), "cancel": 0.6,
		"cost": 8.0, "range": 6.0, "angle": 30.0,
		"counter": {"cls": "swallow", "power": [22.0, 30.0, 40.0, 55.0]},
		"tiers": {"t1": {}, "t2": {}, "t3": {}},
		"anim": "earth_wall", "fx": {"mat": "stone", "shape": "open", "release": "erupt"},
		"ai": {"role": "counter", "range": [0.0, 6.0], "tags": ["sink", "vs_solids", "vs_lava_wave", "from_guard"]}})
	# ---------------------------------------------------------------- evade_hold: Stone Skin / Burrow Step
	KitEarth.reg("stone_skin", 0, "evade_hold", {"name": "Stone Skin", "part": "stone", "module": "kit_earth",
		"desc": "Hold EVADE in place: 40 % armor vs kinetic hits, anchored (no knockback / pull / lift), speed x0.4. Hold EVADE with a direction: Burrow Step - sink and resurface 3.5 m away (passes under ground lines).",
		"verb": "stance", "startup": 0.0, "active": KitEarth.f(14), "recovery": KitEarth.f(8),
		"cost": 0.0, "stance": "stone_skin", "armor": 0.4, "anchored": true, "anchor_cp": 40.0, "speed_mult": 0.4, "upkeep": 8.0,
		"counter": {"cls": "anchor", "power": [40.0]}, "aura_mat": "stone",
		"anim": "guard", "fx": {"mat": "stone", "aura": ""},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["anchor", "armor", "burrow", "vs_tornado", "vs_suction"]}})
	CombatWorld.register_body_tick(&"spike_line", Callable(EarthStone, "spike_tick"))
	CombatWorld.register_body_tick(&"crag", Callable(EarthStone, "crag_tick"))
	Interactions.register_tag_class(&"spike_line", &"spikes", &"spikes")
	_prev_channel_hook = Interactions.channel_hook(&"")
	if _prev_channel_hook.is_valid() and _prev_channel_hook.get_method() == &"bulwark_channels":
		_prev_channel_hook = Callable()
	Interactions.register_channels(&"", Callable(EarthStone, "bulwark_channels"))


## Legacy earth_attack: tier data for T2 Boulder / T3 Crag Breaker plus the def metadata. Only keys the
## base def does not have are added (BASE_DEFS values stay untouched; T0/T1 keep today's numbers).
static func _extend_legacy() -> void:
	var d: Dictionary = Moves.DEFS.earth_attack
	var add := {"name": "Stone Shot", "slot": "strike", "sub": 0, "part": "stone",
		"desc": "Rip a 20 kg stone and throw it @17. Hold: Heave 45 kg (legacy) -> Boulder 65 kg @12 -> Crag Breaker 80 kg @11 that bursts into 3 rubble.",
		"tier_times": [0.55, 1.1, 1.9],
		"tiers": {"t1": {"charge_drain": 0.0}, "t2": {"mass": 65.0, "speed": 12.0, "damage": 26.0, "balance": 65.0, "cost_add": 6.0, "charge_drain": 8.0},
			"t3": {"mass": 80.0, "speed": 11.0, "damage": 30.0, "balance": 75.0, "cost_add": 6.0, "tag": "crag", "pieces": 3}},
		"threat": {"cls": "stone", "power": [17.0, 31.5, 39.0, 44.0]},
		"fx": {"mat": "stone", "impact": "burst"},
		"ai": {"role": "poke", "range": [2.0, 14.0], "tags": ["projectile", "charge", "reuse_loose_stone"]}}
	for k in add:
		if not Moves.BASE_DEFS.earth_attack.has(k):
			d[k] = add[k]
	var t: Dictionary = Moves.DEFS.earth_tech
	var tadd := {"name": "Seize / Split", "slot": "tech", "sub": 0, "part": "stone",
		"desc": "Seize a stone <= 80 kg at 7.5 m (incoming preferred), drag-aim, release to throw. T+A Split: three spikes fanned 30° @20 m/s - split it and spike it back.",
		"shape": "split", "pieces": 3, "spread": 30.0, "split_speed": 20.0, "shape_cost": 3.0,
		"counter": {"cls": "grip_stone"}, "fx": {"mat": "stone", "shape": "spear"},
		"ai": {"role": "counter", "range": [0.0, 7.5], "tags": ["reclaim", "split", "vs_stone"]}}
	for k in tadd:
		if not Moves.BASE_DEFS.earth_tech.has(k):
			t[k] = tadd[k]


# ================================================================ Bulwark thickening

## Channel hook for untagged bodies: a Bulwark (legacy wall) that its builder keeps guarding grows with
## the guard hold time (120 -> 160 -> 200 kg, CP 30 / 40 / 50) at the moment it acts as a counter.
static func bulwark_channels(w: CombatWorld, b: MatBody, g: Agent) -> void:
	if _prev_channel_hook.is_valid():
		_prev_channel_hook.call(w, b, g)
	if b.form != Sim.Form.WALL or b.mat != Sim.Mat.STONE or b.tag != &"":
		return
	var owner := w.get_actor(b.last_actor)
	if owner == null or owner.wall_body != b.id or not owner.guarding or owner.action == null or owner.action.id != "guard":
		return
	if thicken(w, owner, b, owner.action.total):
		g.mass = b.mass


## Grows a Bulwark to the mass of the guard hold `held_s` (booked from the ground). True if it grew.
static func thicken(w: CombatWorld, a: ActorState, b: MatBody, held_s: float) -> bool:
	var tier := 0
	if held_s >= BULWARK_TIMES[1] - 1e-6:
		tier = 2
	elif held_s >= BULWARK_TIMES[0] - 1e-6:
		tier = 1
	var want: float = BULWARK_MASS[tier]
	if b.mass >= want - 1e-6 or b.props.has("ram"):
		return false
	var extra := want - b.mass
	var e := b.thermal_energy()
	b.mass += extra
	w._set_energy(b, e)
	w.mass_ledger.ground_taken += extra
	b.tier = tier + 1
	w.emit("barrier_grow", {"actor": a.id, "body": b.id, "mass": b.mass, "tier": tier + 1})
	w.emit("charge", {"actor": a.id, "move": "guard", "element": Sim.Element.EARTH, "sub": 0, "tier": tier + 1,
		"ready": tier >= 2, "slot": "guard"})
	return true


# ================================================================ module lifecycle (ram_wall, swallow, stone_skin)

static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	inst.data["face"] = w.aim_dir(a, it)
	inst.data["aim"] = inst.data.face
	match inst.id:
		"ram_wall":
			_ram_start(w, a, inst)
		"swallow":
			_swallow_start(w, a, inst)
		"stone_skin":
			_skin_start(w, a, inst, it)
		_:
			Verbs.on_start(w, a, inst, it)


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	match inst.id:
		"ram_wall", "swallow":
			return ActionInst.P.ACTIVE
		"stone_skin":
			return ActionInst.P.ACTIVE if inst.data.get("mode", "") == "burrow" else ActionInst.P.CHANNEL
	return Verbs.after_startup(w, a, inst, it)


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	match inst.id:
		"ram_wall":
			if p == ActionInst.P.ACTIVE:
				Verbs.fx(w, a, inst, "release", {"dir": inst.data.get("dir", a.forward()), "length": 6.0, "body": int(inst.data.get("ram", -1))})
			elif p == ActionInst.P.RECOVERY:
				_ram_end(w, a, inst)
		"swallow":
			if p == ActionInst.P.ACTIVE:
				var dir: Vector3 = inst.data.face
				Verbs.fx(w, a, inst, "release", {"pos": KitEarth.ground_point(w, a, dir, 3.0), "dir": dir, "length": 6.0,
					"angle": 60.0, "power": Charge.counter_power(inst.def, inst.tier())})
		"stone_skin":
			if p == ActionInst.P.RECOVERY:
				if inst.data.get("mode", "") == "burrow":
					inst.data["controls_motion"] = false
					FxEvents.fx_for(w, a, inst, "erupt", "stone", {"pos": a.pos, "shape": "small"})
				else:
					VerbMotion.stance_end(w, a, inst)
		_:
			Verbs.on_phase(w, a, inst, p)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"ram_wall":
			if inst.phase == ActionInst.P.ACTIVE:
				_ram_step(w, a, inst)
		"swallow":
			if inst.phase == ActionInst.P.ACTIVE:
				_swallow_tick(w, a, inst)
		"stone_skin":
			if inst.data.get("mode", "") == "burrow":
				if inst.phase == ActionInst.P.ACTIVE:
					VerbMotion.dash_tick(w, a, inst)
			elif inst.phase == ActionInst.P.CHANNEL:
				VerbMotion.stance_tick(w, a, inst, it)
		_:
			Verbs.on_tick(w, a, inst, it)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	match inst.id:
		"ram_wall":
			var wall := w.get_body(int(inst.data.get("ram", -1)))
			if wall != null and wall.alive:
				wall.vel = Vector3.ZERO
			inst.data["keep_wall"] = -1
		"stone_skin":
			if inst.data.get("mode", "") == "burrow":
				inst.data["controls_motion"] = false
			else:
				VerbMotion.stance_end(w, a, inst)
		"swallow":
			pass
		_:
			Verbs.on_interrupt(w, a, inst, reason)


# ---------------------------------------------------------------- Ram Wall

static func _ram_start(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var wall := w.get_body(int(inst.data.get("wall", a.wall_body)))
	if wall == null or not wall.alive or wall.form != Sim.Form.WALL or wall.last_actor != a.id:
		KitEarth.fizzle(w, a, inst, "material")
		return
	if not Verbs.pay(w, a, inst, "start"):
		inst.data["fizzle"] = true
		return
	if wall.tag == &"" and wall.mat == Sim.Mat.STONE:
		thicken(w, a, wall, float(inst.data.get("guard_t", 0.0)))
	inst.data["tier"] = clampi(wall.tier - 1, 0, 2) if wall.tier > 0 else 0
	var dir := a.forward()
	inst.data["dir"] = dir
	inst.data["ram"] = wall.id
	inst.data["keep_wall"] = wall.id
	inst.data["speed"] = RAM_SPEED
	inst.data["hit"] = {}
	wall.props["ram"] = a.id
	Verbs.fx(w, a, inst, "cast", {"body": wall.id, "pos": wall.pos})


static func _ram_agent(w: CombatWorld, a: ActorState, wall: MatBody, dir: Vector3, spd: float) -> Agent:
	var g := Agent.of_body(w, wall)
	g.kind = "body"
	g.ccls = &"ram"
	g.actor = a
	g.dir = dir
	g.power = wall.mass * spd / 20.0
	return g


static func _ram_step(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var wall := w.get_body(int(inst.data.get("ram", -1)))
	if wall == null or not wall.alive or wall.form != Sim.Form.WALL:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var dir: Vector3 = inst.data.dir
	var spd := float(inst.data.get("speed", RAM_SPEED))
	var step := dir * spd * Sim.DT
	var np := wall.pos + step
	var lead := wall.pos + dir * (wall.wall_half.z + 0.1) + Vector3(0, 0.5, 0)
	if w.arena.segment_hit(lead, lead + step + dir * 0.15, 0.05) >= 0.0:
		_ram_crumble(w, a, inst, wall, "arena")
		return
	var g := w.arena.ground_height(np.x, np.z, wall.pos.y + 0.35)
	if absf(g - wall.pos.y) > 0.35:
		_ram_crumble(w, a, inst, wall, "ledge")
		return
	np.y = g
	wall.pos = np
	wall.vel = dir * spd
	var ram := _ram_agent(w, a, wall, dir, spd)
	var hit: Dictionary = inst.data.hit
	for b in w.bodies.duplicate():
		if b == wall or not b.alive or b.controller >= 0 or b.captured_by >= 0 or b == w.pool:
			continue
		if b.form == Sim.Form.ZONE or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE:
			continue
		if b.form == Sim.Form.WALL:
			if b.last_actor != a.id and _walls_touch(w, wall, b, dir):
				var res := Interactions.resolve(w, Agent.of_body(w, wall, w.get_actor(b.last_actor)), Agent.of_body(w, b), {"site": "ram"})
				if not wall.alive:
					w.set_phase(a, inst, ActionInst.P.RECOVERY)
					return
				if bool(res.counter_broken):
					spd *= maxf(0.4, float(res.pass_scale))
				elif bool(res.stopped) or String(res.outcome) == "earth_ram_both":
					_ram_crumble(w, a, inst, wall, "contest")
					return
				else:
					spd *= maxf(0.3, float(res.pass_scale))
				inst.data["speed"] = spd
			continue
		if b.static_body:
			continue
		var touch := false
		if b.form == Sim.Form.WAVE:
			touch = w.point_in_wall(b.pos + Vector3(0, 0.2, 0), wall, b.wave_width * 0.4 + 0.2)
		else:
			touch = w.point_in_wall(b.pos, wall, b.radius + 0.25)
		if not touch or hit.has(b.id):
			continue
		hit[b.id] = true
		ram.power = wall.mass * spd / 20.0
		var r2 := Interactions.resolve(w, Agent.of_body(w, b, a), ram, {"site": "ram"})
		if not wall.alive:
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
			return
		if String(r2.outcome) == "weaken":
			spd *= 0.85
			inst.data["speed"] = spd
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0 or hit.has(-t.id):
			continue
		if w.point_in_wall(t.pos + Vector3(0, 0.9, 0), wall, Sim.ACTOR_RADIUS + 0.15):
			hit[-t.id] = true
			w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(inst.def.damage),
				"balance": float(inst.def.balance), "knock": dir * float(inst.def.knock) + Vector3(0, 2.0, 0), "kind": "stone",
				"from": wall.pos - dir, "power": wall.mass * spd / 20.0, "mat": "stone"})


static func _walls_touch(w: CombatWorld, wall: MatBody, other: MatBody, dir: Vector3) -> bool:
	var front := wall.pos + dir * wall.wall_half.z + Vector3(0, 0.4, 0)
	for k in [-0.7, 0.0, 0.7]:
		var side := Vector3(cos(wall.wall_yaw), 0, -sin(wall.wall_yaw)) * wall.wall_half.x * float(k)
		if w.point_in_wall(front + side, other, 0.15):
			return true
	return w.point_in_wall(other.pos + Vector3(0, 0.4, 0), wall, other.wall_half.z + 0.1)


static func _ram_crumble(w: CombatWorld, a: ActorState, inst: ActionInst, wall: MatBody, why: String) -> void:
	inst.data["keep_wall"] = -1
	if wall.alive:
		wall.vel = Vector3.ZERO
		FxEvents.fx_for(w, a, inst, "burst", "stone", {"pos": wall.pos + Vector3(0, 0.6, 0), "radius": 1.2, "body": wall.id})
		w.emit("ram_end", {"actor": a.id, "body": wall.id, "why": why})
		w._crumble_wall(wall)
	if inst.phase == ActionInst.P.ACTIVE and a.action == inst:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func _ram_end(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var wall := w.get_body(int(inst.data.get("ram", -1)))
	if wall != null and wall.alive and wall.form == Sim.Form.WALL:
		_ram_crumble(w, a, inst, wall, "spent")
	inst.data["keep_wall"] = -1


# ---------------------------------------------------------------- Swallow

static func _swallow_start(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	if not Verbs.pay(w, a, inst, "start"):
		inst.data["fizzle"] = true
		return
	inst.data["tier"] = KitEarth.guard_tier(inst)
	inst.data["done"] = {}
	if inst.tier() > 0:
		FxEvents.charge(w, a, inst, inst.tier(), inst.tier() >= 3)
	Verbs.fx(w, a, inst, "cast", {"shape": "open"})


static func _swallow_tick(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var dir: Vector3 = inst.data.face
	var rng := float(inst.def.get("range", 6.0))
	var cos_lim := cos(deg_to_rad(float(inst.def.get("angle", 30.0))))
	var done: Dictionary = inst.data.done
	var counter := Agent.of_move(w, a, "swallow", inst.tier(), false)
	counter.pos = KitEarth.ground_point(w, a, dir, 2.0)
	counter.dir = dir
	for b in w.bodies.duplicate():
		if not b.alive or b.static_body or b.controller >= 0 or b.captured_by >= 0 or done.has(b.id) or b == w.pool:
			continue
		if b.form == Sim.Form.WALL or b.form == Sim.Form.ZONE or b.form == Sim.Form.POOL or b.form == Sim.Form.PUDDLE or b.form == Sim.Form.CLOUD:
			continue
		if b.attack_id != 0 and b.attack_owner == a.id:
			continue
		var to := KitEarth.flat(b.pos - a.pos)
		var d := to.length()
		if d > rng + b.radius or (d > 0.6 and to.normalized().dot(dir) < cos_lim):
			continue
		if b.pos.y > a.pos.y + 2.6:
			continue
		done[b.id] = true
		Interactions.resolve(w, Agent.of_body(w, b, a), counter, {"site": "swallow"})


# ---------------------------------------------------------------- Stone Skin / Burrow Step

const BURROW := {"distance": 3.5, "iframes": 0.24, "burrow": true, "dir": "stick", "active": 14.0 / 60.0, "trail": ""}


static func _skin_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var mv := it.move
	mv.y = 0.0
	var ground_ok := a.grounded and a.surface == "stone" and not a.in_water
	if mv.length() > 0.3 and ground_ok:
		inst.data["mode"] = "burrow"
		var spec := inst.def.duplicate()
		spec.merge(BURROW, true)
		inst.data["spec_def"] = spec
		inst.data["active"] = float(BURROW.active)
		if not Verbs.pay(w, a, inst, {"focus": 6.0}):
			inst.data["fizzle"] = true
			return
		FxEvents.fx_for(w, a, inst, "erupt", "stone", {"pos": a.pos, "shape": "small"})
		VerbMotion.dash_start(w, a, inst, it)
		return
	inst.data["mode"] = "skin"
	VerbMotion.stance_start(w, a, inst)


# ================================================================ Rising Fangs / Earthrise

## T3 Earthrise: a ring of spikes around the caster and a launch burst; lower tiers run the ground line.
static func fangs_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	if not bool(Charge.param(inst, "earthrise", false)):
		var line := VerbGroundLine.launch(w, a, inst)
		if line != null:
			line.props["spike_h"] = float(Charge.param(inst, "spike_h", 0.45))
		return true
	var r := float(Charge.param(inst, "radius", 3.5))
	var mass := float(Charge.param(inst, "mass", 60.0))
	var f := a.forward()
	var pieces := 4
	for k in pieces:
		var ang := TAU * float(k) / float(pieces) + PI / float(pieces)
		var d := f.rotated(Vector3.UP, ang)
		var p := a.pos + d * r
		p.y = w.arena.ground_height(p.x, p.z, a.pos.y + 0.4)
		var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, mass / float(pieces), p, "ground@%.1f,%.1f" % [p.x, p.z])
		w.mass_ledger.ground_taken += mass / float(pieces)
		_make_spikes(w, a.id, b, atan2(d.x, d.z), 1.3, 0.7)
	VerbVolume.burst_at(w, a, inst, a.pos + Vector3(0, 0.6, 0), {"radius": r + 0.6, "power": 40.0, "damage": float(Charge.param(inst, "damage", 16.0)),
		"balance": 50.0, "knock": 3.0, "lift": 6.0, "cls": "blast", "mat": "stone"})
	FxEvents.fx_for(w, a, inst, "ring", "stone", {"pos": a.pos, "radius": r, "power": 40.0})
	return true


## A spike line (Rising Fangs) or iron filings (Lodestone Line) per tick. Stone: enemy ground lines that
## meet the front are stopped by the spikes; when the line ends or is blocked, it erupts into standing
## spikes (a low WALL tag spikes, CP 20, 1.5 s) where it stands.
static func spike_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.mat == Sim.Mat.METAL:
		return EarthMetal.filings_tick(w, b, _dt)
	if b.form == Sim.Form.WALL:
		return false
	if b.form != Sim.Form.WAVE:
		_erupt(w, b)
		return true
	for o in w.bodies:
		if o == b or not o.alive or o.form != Sim.Form.WAVE or o.attack_owner == b.attack_owner or o.tag == &"spike_line":
			continue
		if KitEarth.flat_dist(o.pos, b.pos) > (o.wave_width + b.wave_width) * 0.5 + 0.6:
			continue
		var counter := Agent.of_body(w, b)
		counter.ccls = &"spikes"
		counter.power = SPIKE_CP
		counter.actor = w.get_actor(b.attack_owner)
		var res := Interactions.resolve(w, Agent.of_body(w, o), counter, {"site": "spikes"})
		if bool(res.stopped) and b.alive:
			_erupt(w, b)
			return true
	if b.wave_budget <= 0.05:
		_erupt(w, b)
		return true
	return false


## The line body becomes the standing spikes (same mass, booked from the ground when it was ripped).
static func _erupt(w: CombatWorld, b: MatBody) -> void:
	var owner := b.attack_owner if b.attack_owner >= 0 else b.last_actor
	var dir := b.wave_dir if b.wave_dir.length() > 0.1 else Vector3.FORWARD
	b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4)
	var h := float(b.props.get("spike_h", 0.45))
	_make_spikes(w, owner, b, atan2(dir.x, dir.z), maxf(0.8, b.wave_width * 0.6), h)
	FxEvents.fx(w, "erupt", "stone", {"actor": owner, "body": b.id, "pos": b.pos, "dir": dir, "radius": b.wave_width, "element": 0})


static func _make_spikes(w: CombatWorld, owner: int, b: MatBody, yaw: float, half_x: float, half_y: float) -> void:
	release_hold(w, b)
	b.form = Sim.Form.WALL
	b.tag = &"spikes"
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.wave_path = PackedVector3Array()
	b.wall_yaw = yaw
	b.wall_half = Vector3(half_x, half_y, 0.3)
	b.wall_rise = 0.0
	b.wall_damage = 0.0
	b.static_body = true
	b.on_ground = true
	b.max_life = -1.0
	b.age = 0.0
	b.hardness = SPIKE_CP / maxf(b.mass, 1.0)
	b.props["standing"] = SPIKE_LIFE
	b.props["rise_time"] = 0.08
	b.props["source"] = "ground"
	b.last_actor = owner
	w.release_captured(b)
	w.emit("wall", {"actor": owner, "body": b.id, "tag": "spikes"})


static func release_hold(w: CombatWorld, b: MatBody) -> void:
	if b.controller >= 0:
		var h := w.get_actor(b.controller)
		if h != null and h.held_body == b.id:
			h.held_body = -1
		b.controller = -1


## Crag Breaker in flight: a slow tumble for the view.
static func crag_tick(_w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if b.attack_id != 0:
		b.spin = 6.0
	return false
