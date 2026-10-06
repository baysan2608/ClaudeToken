class_name AirSound
extends RefCounted
## Air / Sound (sub 3) - resonance and flight. docs/MOVESET.md §7.16, §8.4 (Sound column), §9.3 combos 10, 18, 26.
##
## strike  Clap -> Shout -> Roar -> Resonance        thrust Sound Lance (bank shots)      ground Tremor Hum
## sweep   Echo Ring                                 guard  Sound Barrier (+ Echo Return, the perfect)
## push    Thunder Step                              sink   Ground Ping
## tech    Flight (a persistent hover field: every attack stays usable)
## evade   Boom Step                                 hold   Hover
##
## Sound is pressure with a medium: P >= the charge's cohesion (6 + 4 x tier) disrupts a charge or channel,
## brittle bodies (ice, glass, thin walls) shatter, sand / fog / vines absorb it, stone and metal walls reflect it
## (so the Lance banks off walls and the arena), and a vacuum nullifies it. Nothing here creates matter or heat.

const E := 3
const SUB := 3
const HIDDEN := ["concealed", "fogbound", "fogwalk", "veiled"]
const FLIGHT_HEIGHT := 2.2
const FLIGHT_UPKEEP := 10.0
const MAX_BOUNCES := 2


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
	CombatWorld.register_body_tick(&"tremor", Callable(AirSound, "tremor_tick"))
	CombatWorld.register_zone_effect(&"flight_field", Callable(AirSound, "flight_effect"))
	CombatWorld.register_tech_preview(E, SUB, Callable(AirSound, "tech_preview"))


# ================================================================ strike: Clap / Shout / Roar / Resonance

static func _strike() -> void:
	KitAir.reg("sound_clap", SUB, "strike", {"name": "Clap / Shout / Roar / Resonance",
		"desc": "A cone of sound: Clap (3 m, 60 deg, daze) -> Shout (7 m) -> Roar (10 m: shatters ice, glass and thin walls, rains out mist, -8 Focus) -> Resonance (12 m: shatters any brittle body, deafens 2 s). Disrupts charges and channels when its pressure reaches their cohesion (6 + 4 x tier). Sand, fog and vines absorb it; stone and metal walls throw it back; a vacuum nullifies it; it passes through water.",
		"module": "verbs", "verb": "cone",
		"startup": _s(6), "active": _s(6), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "cls": "sound", "power": 8.0, "range": 3.0, "angle": 30.0, "damage": 3.0, "balance": 12.0, "knock": 3.0, "lift": 0.4,
		"status": "dazed", "status_t": 0.15, "drain": 0.0,
		"tiers": {
			"t1": {"power": 14.0, "range": 7.0, "angle": 15.0, "damage": 6.0, "balance": 18.0, "cost_add": 4.0, "status_t": 0.2},
			"t2": {"power": 22.0, "range": 10.0, "angle": 18.0, "damage": 10.0, "balance": 26.0, "drain": 8.0, "cost_add": 8.0},
			"t3": {"power": 32.0, "range": 12.0, "angle": 22.0, "damage": 14.0, "balance": 36.0, "drain": 8.0, "status": "deafened", "status_t": 2.0, "cost_add": 14.0},
		},
		"hook_execute": Callable(AirSound, "clap_execute"),
		"counter": {"cls": "sound", "power": [8.0, 14.0, 22.0, 32.0]}, "threat": {"cls": "sound", "power": [8.0, 14.0, 22.0, 32.0]},
		"anim": "air_push", "anim_active": "mv_front_kick", "fx": {"mat": "sound", "shape": "", "release": "cone"},
		"ai": {"role": "poke", "range": [0.5, 12.0], "tags": ["cone", "disrupt", "shatter_brittle", "deafen", "vs_ice", "vs_glass", "vs_charge"]}})


static func clap_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var rng_m := float(Charge.param(inst, "range", 3.0))
	var half := float(Charge.param(inst, "angle", 30.0))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var drain := float(Charge.param(inst, "drain", 0.0))
	var foes := w.actors_in_cone(a, dir, rng_m, half)
	# The pulse reaches a charge (or a non-guard channel) before it lands as a hit: P >= 6 + 4 x tier disrupts.
	# A held guard is not disrupted - the pulse meets the guard's counter cell through the cone hit instead.
	var vd := Agent.of_volume(w, a, inst, &"sound", a.chest(), dir, {"P": float(Charge.param(inst, "power", 8.0))})
	for t in foes:
		if t.health > 0.0:
			disrupt(w, a, t, vd)
	VerbVolume.cone(w, a, inst)
	if drain > 0.0:
		for t in foes:
			if t.health > 0.0 and (t.last_result == "hit" or t.last_result == "knockdown"):
				t.focus = maxf(0.0, t.focus - drain)
				t.focus_idle = 0.0
				w.emit("focus_drain", {"actor": t.id, "by": a.id, "amount": drain})
	return true


## Sound against a charge / channel: the core `disrupt` outcome decides (P >= 6 + 4 x the charge's tier).
static func disrupt(w: CombatWorld, a: ActorState, t: ActorState, v: Agent) -> bool:
	if t.action == null or not Outcomes.disruptable(t):
		return false
	var th := Agent.new()
	th.kind = "volume"
	th.cls = &"charge"
	th.ccls = &"charge"
	th.actor = t
	th.hostile = true
	var before := t.action
	var res := Interactions.resolve(w, th, v, {"site": "sound"})
	return t.action != before or res.outcome == "disrupt"


# ================================================================ thrust: Sound Lance (bank shots)

static func _thrust() -> void:
	KitAir.reg("sound_lance", SUB, "thrust", {"name": "Sound Lance",
		"desc": "A focused pulse (14 m, a visible ring): passes through water and mist, reflects off stone and metal walls and the arena walls - bank it around cover. Hold: stronger and longer.",
		"module": "verbs", "verb": "beam",
		"startup": _s(10), "active": _s(4), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"cost": 6.0, "range": 14.0, "width": 0.5, "cls": "sound", "power": 10.0, "damage": 7.0, "balance": 16.0, "knock": 3.0,
		"tiers": {
			"t1": {"power": 14.0, "damage": 10.0, "range": 15.0, "cost_add": 2.0},
			"t2": {"power": 20.0, "damage": 14.0, "range": 16.0, "cost_add": 4.0},
			"t3": {"power": 28.0, "damage": 19.0, "range": 18.0, "cost_add": 8.0},
		},
		"hook_execute": Callable(AirSound, "lance_execute"),
		"counter": {"cls": "sound", "power": [10.0, 14.0, 20.0, 28.0]}, "threat": {"cls": "sound", "power": [10.0, 14.0, 20.0, 28.0]},
		"anim": "mv_palm_thrust", "fx": {"mat": "sound", "shape": "", "release": "beam"},
		"ai": {"role": "poke", "range": [2.0, 18.0], "tags": ["beam", "bank_shot", "through_water", "through_mist", "ranged", "reflects"]}})


## Hitscan along the aim with up to two bounces: the arena's solids and stone / metal / glass walls reflect it (x0.8 power
## per bounce); other barriers are met through the rules (water shields and mist let it through); the first fighter is hit.
static func lance_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var total := float(Charge.param(inst, "range", 14.0))
	var power := float(Charge.param(inst, "power", 10.0))
	var width := float(Charge.param(inst, "width", 0.5))
	var dir := AirUtil.aim_flat(a, inst)
	var pos := a.hand_point() + Vector3(0, 0.2, 0)
	var path := PackedVector3Array([pos])
	var left := total
	var v := Agent.of_volume(w, a, inst, &"sound", pos, dir, {"P": power})
	var hit_someone := false
	for seg in MAX_BOUNCES + 1:
		var end := pos + dir * left
		var stop_t := 1.0
		var reflect_n := Vector3.ZERO
		var cands: Array = []                      # [t_along, kind, ref]
		# barriers (arena solids, walls, barrier zones) nearest first
		var ah := AirUtil.arena_hit(w, pos, end)
		if not ah.is_empty():
			cands.append([float(ah.t) * left, "arena", ah])
		for b in w.bodies:
			if not b.alive or b == w.pool or b.static_body and b.form != Sim.Form.WALL and b.form != Sim.Form.ZONE:
				continue
			if b.form == Sim.Form.WALL and b.wall_rise > 0.5:
				var tw := w.wall_segment_t(pos, end, b)
				if tw >= 0.0:
					cands.append([tw * left, "wall", b])
			elif b.form == Sim.Form.ZONE and b.props.get("barrier", false):
				var tz := Conduction._cyl_t(pos, end, b.pos, b.zone_radius, float(b.props.get("height", 2.5)))
				if tz >= 0.0:
					cands.append([tz * left, "zone", b])
			elif AirUtil.carriable(b, 80.0) and b.controller != a.id and b.mat != Sim.Mat.AIR:
				var tb := (b.pos - pos).dot(dir)
				if tb >= 0.0 and tb <= left and (pos + dir * tb).distance_to(b.pos) <= width + b.radius:
					cands.append([tb, "body", b])
		for t in w.actors:
			if t == a or t.team == a.team or t.health <= 0.0:
				continue
			var tt := (t.chest() - pos).dot(dir)
			if tt >= 0.0 and tt <= left and (pos + dir * tt).distance_to(t.chest()) <= width + Sim.ACTOR_RADIUS + 0.2:
				cands.append([tt, "actor", t])
		cands.sort_custom(func(x, y): return float(x[0]) < float(y[0]))
		var ended := false
		var hit_at := left
		for c in cands:
			var kind := String(c[1])
			var at := float(c[0])
			if kind == "actor":
				var t: ActorState = c[2]
				var info := {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "damage", 7.0)) * power / maxf(float(Charge.param(inst, "power", 10.0)), 1.0),
					"balance": float(Charge.param(inst, "balance", 16.0)), "knock": dir * float(Charge.param(inst, "knock", 3.0)) + Vector3(0, 0.4, 0),
					"kind": "sound", "from": pos, "agent": v, "power": power, "tier": v.tier, "mat": "sound"}
				disrupt(w, a, t, v)
				var res := w.hit_actor(t, info)
				if res == "hit" or res == "knockdown":
					hit_someone = true
				hit_at = at
				ended = true
				break
			if kind == "body":
				Interactions.resolve(w, Agent.of_body(w, c[2], a), v, {"site": "lance"}, Interactions.PASS_RULE)
				continue
			if kind == "arena":
				var n: Vector3 = c[2].n
				reflect_n = n
				hit_at = at
				break
			if kind == "wall":
				var wall: MatBody = c[2]
				var hard := wall.mat == Sim.Mat.STONE or wall.mat == Sim.Mat.METAL or wall.mat == Sim.Mat.GLASS
				if hard:
					reflect_n = AirUtil.wall_normal(wall, pos)
					hit_at = at
					break
				var r := VerbVolume.meet_body(w, a, v, wall)
				if bool(r.stopped) or float(r.pass_scale) <= 0.0:
					hit_at = at
					ended = true
					break
				continue
			if kind == "zone":
				var rz := VerbVolume.meet_body(w, a, v, c[2])
				if bool(rz.stopped) or float(rz.pass_scale) <= 0.0:
					hit_at = at
					ended = true
					break
				v.power *= float(rz.pass_scale)
		var stop := pos + dir * minf(hit_at, left)
		path.append(stop)
		left -= pos.distance_to(stop)
		if ended or reflect_n == Vector3.ZERO or left < 0.5 or seg == MAX_BOUNCES:
			break
		# reflect: the pulse goes on from the surface with the mirrored heading, a little weaker
		dir = AirUtil.reflect_dir(dir, reflect_n)
		pos = stop + reflect_n * 0.08
		v.power *= 0.8
		power *= 0.8
		w.emit("ricochet", {"pos": stop, "by": a.id, "dir": dir, "power": power})
	Verbs.fx(w, a, inst, "beam", {"length": total - left, "path": path, "power": power})
	inst.data["bank"] = path.size() - 2
	inst.data["hit"] = hit_someone
	return true


# ================================================================ ground: Tremor Hum

static func _ground() -> void:
	KitAir.reg("sound_tremor", SUB, "ground", {"name": "Tremor Hum",
		"desc": "A ground hum races 18 m/s for 10 m: knocks fighters over (-35 balance), cracks the walls it passes, and pops loose stones into the air where they can be seized from range.",
		"module": "verbs", "verb": "ground_line",
		"startup": _s(14), "active": _s(6), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"cost": 7.0, "source": "none", "mat": "air", "tag": "tremor", "mass": 0.0, "speed": 18.0, "budget": 10.0,
		"width": 1.8, "damage": 6.0, "balance": 35.0, "knock": 2.0, "lift": 2.5, "kind": "sound", "steer": 0.0, "power": 12.0,
		"tiers": {
			"t1": {"power": 16.0, "budget": 11.0, "width": 2.1, "damage": 8.0, "cost_add": 2.0},
			"t2": {"power": 22.0, "budget": 12.0, "width": 2.5, "damage": 11.0, "cost_add": 5.0},
			"t3": {"power": 30.0, "budget": 14.0, "width": 3.0, "damage": 15.0, "balance": 45.0, "cost_add": 9.0},
		},
		"counter": {"cls": "tremor", "power": [12.0, 16.0, 22.0, 30.0]}, "threat": {"cls": "sound", "power": [12.0, 16.0, 22.0, 30.0]},
		"anim": "mv_stomp", "fx": {"mat": "sound", "release": "release"},
		"ai": {"role": "zone", "range": [2.0, 14.0], "tags": ["ground_line", "knockdown", "cracks_walls", "pops_stones", "ranged"]}})


## The tremor cracks every wall its front passes (once per wall): wall damage grows with its power; at 1.0 the wall crumbles.
static func tremor_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if not b.alive or b.form != Sim.Form.WAVE:
		return false
	var done: Dictionary = b.props.get("cracked", {})
	for o in w.bodies:
		if not o.alive or o.form != Sim.Form.WALL or o.wall_rise < 0.5 or done.has(o.id):
			continue
		var reach := b.wave_width * 0.5 + maxf(o.wall_half.x, o.wall_half.z)
		if Vector2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() > reach:
			continue
		done[o.id] = true
		var cp := maxf(1.0, o.mass * Materials.hardness(o))
		o.wall_damage_add(clampf(b.power / cp, 0.1, 1.0) * 0.6)
		w.emit("wall_crack", {"body": o.id, "by": b.attack_owner, "damage": o.wall_damage})
		if o.wall_damage >= 1.0:
			w._crumble_wall(o)
	b.props["cracked"] = done
	return false


# ================================================================ sweep: Echo Ring

static func _sweep() -> void:
	KitAir.reg("sound_echo_ring", SUB, "sweep", {"name": "Echo Ring",
		"desc": "A ring of sound (r 4 m) all around you: reveals everyone hiding (fog, Mist Step, Fog Walk, burrowers) and interrupts the channels and charges its pressure reaches.",
		"module": "verbs", "verb": "burst",
		"startup": _s(8), "active": _s(4), "recovery": _s(14), "cancel": 0.6, "chain": 0.25,
		"cost": 5.0, "at": "self", "radius": 4.0, "power": 10.0, "damage": 3.0, "balance": 10.0, "knock": 2.0, "lift": 0.3, "cls": "sound",
		"tiers": {
			"t1": {"power": 14.0, "radius": 4.5, "cost_add": 2.0},
			"t2": {"power": 18.0, "radius": 5.0, "damage": 5.0, "cost_add": 4.0},
			"t3": {"power": 24.0, "radius": 6.0, "damage": 7.0, "cost_add": 8.0},
		},
		"hook_execute": Callable(AirSound, "echo_execute"),
		"counter": {"cls": "sound", "power": [10.0, 14.0, 18.0, 24.0]}, "threat": {"cls": "sound", "power": [10.0, 14.0, 18.0, 24.0]},
		"anim": "mv_spin", "fx": {"mat": "sound", "shape": "open", "release": "ring"},
		"ai": {"role": "counter", "range": [0.0, 6.0], "tags": ["ring", "reveal", "interrupt_channel", "vs_fog", "vs_burrow", "area"]}})


static func echo_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var r := float(Charge.param(inst, "radius", 4.0))
	var power := float(Charge.param(inst, "power", 10.0))
	var prm := {"radius": r, "power": power, "damage": float(Charge.param(inst, "damage", 3.0)), "balance": float(Charge.param(inst, "balance", 10.0)),
		"knock": float(Charge.param(inst, "knock", 2.0)), "lift": float(Charge.param(inst, "lift", 0.3)), "cls": "sound", "mat": "sound", "heat_hu": 0.0}
	var v := Agent.of_volume(w, a, inst, &"sound", a.pos + Vector3(0, 1.0, 0), Vector3.ZERO, {"P": power})
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0 or t.pos.distance_to(a.pos) > r + Sim.ACTOR_RADIUS:
			continue
		if reveal(w, t, a.id):
			w.emit("revealed", {"actor": t.id, "by": a.id})
		disrupt(w, a, t, v)
	VerbVolume.burst_at(w, a, inst, a.pos + Vector3(0, 1.0, 0), prm)
	return true


## Removes every hiding status from a fighter and marks them revealed for 2 s. True when something was hiding.
static func reveal(w: CombatWorld, t: ActorState, by: int) -> bool:
	var was := false
	for st in HIDDEN:
		if t.status.has(st):
			Status.remove(w, t, st)
			was = true
	if was:
		Status.apply(w, t, "revealed", 2.0, 1.0, by)
	return was


# ================================================================ guard: Sound Barrier / Echo Return

static func _guard() -> void:
	KitAir.reg("sound_barrier", SUB, "guard", {"name": "Sound Barrier",
		"desc": "A vibrating front (CP 12): ice and glass shatter against it (x2.5), light solids are turned, sound is cancelled. Perfect: Echo Return throws incoming sound back at its source. A vacuum nullifies it, sand absorbs it, heavy solids ignore it. Flick up: Thunder Step. Flick down: Ground Ping.",
		"module": "verbs", "verb": "barrier", "barrier": "aura", "mat": "air", "upkeep": 0.0, "move_channel": 0.55,
		"startup": 0.0, "active": 0.0, "recovery": _s(6), "cost": 0.0,
		"tiers": {"t1": {}, "t2": {}, "t3": {}},
		"counter": {"cls": "barrier_sound", "power": [12.0, 14.0, 18.0, 22.0]}, "threat": {"cls": "sound"},
		"anim": "guard", "fx": {"mat": "sound", "aura": "aura"},
		"ai": {"role": "counter", "range": [0.0, 8.0], "tags": ["vs_sound", "vs_ice", "vs_glass", "reflect_sound", "light_solids"]}})


# ================================================================ push: Thunder Step / sink: Ground Ping

static func _push_sink() -> void:
	KitAir.reg("sound_thunder_step", SUB, "push", {"name": "Thunder Step",
		"desc": "From the guard: a 4 m boom-dash forward that shoves everything in front of you (P 14).",
		"module": "kit_air", "verb": "dash",
		"startup": _s(6), "active": _s(10), "recovery": _s(14), "cancel": 0.5,
		"cost": 6.0, "distance": 4.0, "iframes": 0.0, "dir": "aim", "radius": 2.2, "power": 14.0, "damage": 7.0, "balance": 20.0, "knock": 6.0,
		"counter": {"cls": "sound", "power": [14.0]}, "threat": {"cls": "sound", "power": [14.0]},
		"anim": "air_dash", "fx": {"mat": "sound", "trail": "trail"},
		"ai": {"role": "finisher", "range": [0.0, 5.0], "tags": ["from_guard", "dash", "knockback"]}})
	KitAir.handle("sound_thunder_step", {"phase": Callable(AirSound, "boom_phase")})
	KitAir.reg("sound_ping", SUB, "sink", {"name": "Ground Ping",
		"desc": "From the guard: a pulse through the ground (r 6 m, strength by guard hold): reveals burrowers and disrupts the ground lines whose power it matches - spike lines, currents, roots, sand surges.",
		"module": "verbs", "verb": "burst",
		"startup": _s(6), "active": _s(4), "recovery": _s(12), "cancel": 0.6,
		"cost": 4.0, "radius": 6.0, "power": 12.0,
		"tiers": {"t1": {"power": 16.0}, "t2": {"power": 22.0}, "t3": {"power": 30.0}},
		"hook_execute": Callable(AirSound, "ping_execute"),
		"counter": {"cls": "ground_ping", "power": [12.0, 16.0, 22.0, 30.0]}, "threat": {"cls": "sound", "power": [12.0, 16.0, 22.0, 30.0]},
		"anim": "mv_stomp", "fx": {"mat": "sound", "shape": "ground", "release": "ring"},
		"ai": {"role": "counter", "range": [0.0, 6.0], "tags": ["from_guard", "ground_pulse", "vs_ground_line", "vs_burrow", "reveal"]}})


## The boom at the end of a Thunder Step / Boom Step: a burst in front of the fighter.
static func boom_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	Verbs.on_phase(w, a, inst, p)
	if p != ActionInst.P.RECOVERY or inst.data.get("boomed", false):
		return
	inst.data["boomed"] = true
	var ahead := inst.id == "sound_thunder_step"
	var centre := a.pos + a.forward() * (1.2 if ahead else 0.0) + Vector3(0, 1.0, 0)
	var prm := {"radius": float(Charge.param(inst, "radius", 2.0)), "power": float(Charge.param(inst, "power", 8.0)),
		"damage": float(Charge.param(inst, "damage", 4.0)), "balance": float(Charge.param(inst, "balance", 10.0)),
		"knock": float(Charge.param(inst, "knock", 3.0)), "lift": 0.8, "cls": "sound", "mat": "sound", "heat_hu": 0.0}
	VerbVolume.burst_at(w, a, inst, centre, prm)


static func ping_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var r := float(Charge.param(inst, "radius", 6.0))
	var power := float(Charge.param(inst, "power", 12.0))
	Verbs.fx(w, a, inst, "ring", {"pos": a.pos, "radius": r, "power": power, "shape": "ground"})
	var c := Agent.of_move(w, a, "sound_ping", inst.tier(), false)
	c.pos = a.pos
	for b in w.bodies:
		if not b.alive or b.form != Sim.Form.WAVE or b.attack_owner == a.id or Vector2(b.pos.x - a.pos.x, b.pos.z - a.pos.z).length() > r:
			continue
		Interactions.resolve(w, Agent.of_body(w, b, a), c, {"site": "ping"}, Interactions.PASS_RULE)
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0 or t.pos.distance_to(a.pos) > r:
			continue
		if reveal(w, t, a.id):
			w.emit("revealed", {"actor": t.id, "by": a.id, "ground": true})
	return true


# ================================================================ tech: Flight

static func _tech() -> void:
	KitAir.reg("sound_flight", SUB, "tech", {"name": "Flight",
		"desc": "Rise to 2.2 m and fly at 6 m/s with every attack still usable (6 Focus + 10/s). Press again to glide down. Airborne: ground lines, quicksand and ice floors do nothing to you; gusts and tornadoes blow you around (x1.5 balance) and a lightning bolt has no ground to run to.",
		"module": "kit_air", "verb": "flight",
		"startup": _s(10), "active": 0.0, "recovery": _s(12), "cancel": 0.5,
		"cost": 6.0, "upkeep": FLIGHT_UPKEEP, "height": FLIGHT_HEIGHT,
		"tiers": {"t1": {}, "t2": {}, "t3": {}},
		"counter": {"cls": "flight"}, "threat": {"cls": "gust"},
		"anim": "jump", "anim_hold": "glide", "fx": {"mat": "sound", "aura": "aura"},
		"ai": {"role": "mobility", "range": [0.0, 20.0], "tags": ["flight", "vs_ground_line", "vs_quicksand", "vs_ice_floor", "weak_to_gust", "weak_to_lightning"]}})
	KitAir.handle("sound_flight", {"start": Callable(AirSound, "flight_start"), "after": Callable(AirSound, "flight_after")})


static func flight_zone(w: CombatWorld, a: ActorState) -> MatBody:
	var zs := AirUtil.zones_of(w, a.id, "flight_field")
	return zs[0] if not zs.is_empty() else null


static func flight_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	inst.data["face"] = w.aim_dir(a, it)
	inst.data["move_scale"] = 1.0
	if flight_zone(w, a) != null:
		inst.data["land"] = true             # pressing again: no cost, glide down
		return
	if not Verbs.pay(w, a, inst, "start"):
		inst.data["fizzle"] = true
		return
	Verbs.fx(w, a, inst, "cast")


static func flight_after(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> int:
	if inst.data.get("land", false):
		end_flight(w, a, "landed")
		return ActionInst.P.RECOVERY
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	var z := AirUtil.zone(w, "flight_field", a.pos, 0.5, a.id, 0.0, -1.0, {"attach": a.id, "attach_off": Vector3.ZERO, "height": 3.0, "rate": AirUtil.NO_PAIR}, 0, SUB)
	z.props["height_target"] = float(Charge.param(inst, "height", FLIGHT_HEIGHT))
	a.flying = true
	a.stance = "flight"
	a.gliding = false
	Status.apply(w, a, "flight", -1.0, 1.0, a.id)
	w.emit("mode", {"actor": a.id, "kind": "flight", "on": true, "move": inst.id})
	Verbs.fx(w, a, inst, "aura", {"on": true, "shape": "flight"})
	return ActionInst.P.RECOVERY


## Per tick of the flight field: hold 2.2 m above the ground, pay the upkeep, end on a knockdown, an empty Focus pool or death.
static func flight_effect(w: CombatWorld, z: MatBody, dt: float) -> void:
	var a := w.get_actor(z.owner)
	if a == null or a.health <= 0.0 or a.stun_kind == "knockdown":
		end_flight_zone(w, z, "down")
		return
	if not w.spend_focus(a, FLIGHT_UPKEEP * dt):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "sound_flight"})
		end_flight(w, a, "focus")
		return
	if not Status.has(a, "flight"):
		Status.apply(w, a, "flight", -1.0, 1.0, a.id)
	a.flying = true
	var hg := w.arena.ground_height(a.pos.x, a.pos.z, a.pos.y + 3.0)
	var target_y := hg + float(z.props.get("height_target", FLIGHT_HEIGHT))
	a.grounded = false
	a.vel.y = clampf((target_y - a.pos.y) * 6.0, -4.0, 6.0)


static func end_flight_zone(w: CombatWorld, z: MatBody, why: String) -> void:
	var a := w.get_actor(z.owner)
	if z.alive:
		w.close_zone(z, why)
	if a != null:
		_flight_off(w, a, why)


## Ends the fighter's flight (a landing press, a downdraft, a tornado, no Focus): the hover field closes, a glide down follows.
static func end_flight(w: CombatWorld, a: ActorState, why: String) -> void:
	var z := flight_zone(w, a)
	if z != null:
		w.close_zone(z, why)
	_flight_off(w, a, why)


static func _flight_off(w: CombatWorld, a: ActorState, why: String) -> void:
	var was := a.flying or Status.has(a, "flight")
	if Status.has(a, "flight"):
		Status.remove(w, a, "flight")
	if a.stance == "flight":
		a.stance = ""
	if a.stance != "hover" and (a.action == null or not a.action.data.has("hover_height")):
		a.flying = false
	if was:
		if not a.grounded and why != "slammed" and why != "down":
			a.gliding = true           # release = glide down
		w.emit("mode", {"actor": a.id, "kind": "flight", "on": false, "why": why})


static func tech_preview(w: CombatWorld, a: ActorState, _dir: Vector3) -> Dictionary:
	if flight_zone(w, a) != null:
		return {"mode": "LAND", "body": -1, "ok": true, "reason": ""}
	var ok := a.focus >= 6.0
	return {"mode": "FLY", "body": -1, "ok": ok, "reason": "" if ok else "focus"}


# ================================================================ evade: Boom Step / hold: Hover

static func _evade() -> void:
	KitAir.reg("sound_boom_step", SUB, "evade", {"name": "Boom Step",
		"desc": "A 6 m supersonic dash (9 i-frames) that ends in a boom: knock 3 on everyone within 2 m.",
		"module": "kit_air", "verb": "dash",
		"startup": 0.0, "active": _s(12), "recovery": _s(6), "cancel": 0.5,
		"cost": 6.0, "distance": 6.0, "iframes": _s(9), "dir": "stick", "radius": 2.0, "power": 8.0, "damage": 4.0, "balance": 10.0, "knock": 3.0,
		"counter": {"cls": "sound", "power": [8.0]}, "threat": {"cls": "sound", "power": [8.0]},
		"anim": "air_dash", "fx": {"mat": "sound", "trail": "trail"},
		"ai": {"role": "mobility", "range": [0.0, 7.0], "tags": ["dash", "iframes", "boom", "knockback"]}})
	KitAir.handle("sound_boom_step", {"phase": Callable(AirSound, "boom_phase")})
	KitAir.reg("sound_hover", SUB, "evade_hold", {"name": "Hover",
		"desc": "Hold EVADE: hold your height in the air (6 Focus/s). Ground lines pass under you.",
		"module": "kit_air", "verb": "mode",
		"startup": 0.0, "active": 0.0, "recovery": _s(8), "cancel": 0.5,
		"cost": 0.0, "kind": "hover", "height": 1.5, "upkeep": 6.0, "speed_mult": 0.8,
		"anim": "glide", "fx": {"mat": "sound", "aura": "aura"},
		"ai": {"role": "mobility", "range": [0.0, 8.0], "tags": ["hover", "immune_ground_lines"]}})
	KitAir.handle("sound_hover", {"after": Callable(AirSound, "hover_after")})


## Hover holds the height the fighter has when the hold starts (at least 0.9 m, at most 3.5 m).
static func hover_after(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	var phase := Verbs.after_startup(w, a, inst, it)
	if phase == ActionInst.P.CHANNEL:
		var g := w.arena.ground_height(a.pos.x, a.pos.z, a.pos.y + 0.5)
		inst.data["hover_height"] = clampf(a.pos.y - g, 0.9, 3.5)
	return phase


# ================================================================ rules: the Sound column (sound, barrier_sound, ground_ping, tremor)

static func register_rules() -> void:
	Interactions.register_outcome("air_pop", Callable(AirSound, "o_pop"))
	Interactions.register_outcome("air_still", Callable(AirSound, "o_still"))
	_sound_cells()
	_barrier_cells()
	_ping_cells()
	_tremor_cells()


static func _sound_cells() -> void:
	var c := "sound"
	# a disruption: P >= 6 + 4 x the charge's tier (the core handler decides; the cell routes it)
	AirRules._cell("charge", c, {"bands": [[0.0, "disrupt"]], "full_at": 0.0, "fallback": "pass", "id": "air_sound_disrupt"}, {})
	# brittle bodies shatter (x2.5 on ice and glass, x1.3 on hot rock), a stone shot shatters from resonance
	for t in ["ice", "glass"]:
		AirRules._cell(t, c, {"outcome": "air_shatter", "partial": "bend", "fail": "pass", "eff": 2.5, "pieces": 3, "fallback": "pass"}, {"move": "sound_clap", "tier": 0, "expect": "air_shatter"})
	AirRules._cell("hot_rock", c, {"bands": [[0.0, "pass"], [0.5, "bend"], [1.0, "air_shatter"]], "eff": 1.3, "pieces": 3, "fallback": "pass"},
		{"move": "sound_clap", "tier": 2, "expect": "air_shatter"})
	AirRules._cell("stone", c, {"bands": [[0.0, "pass"], [0.5, "bend"], [1.0, "air_shatter"]], "pieces": 3, "fallback": "pass"}, {"move": "sound_clap", "tier": 2, "expect": "air_shatter"})
	AirRules._cell("stone_heavy", c, {"bands": [[0.0, "pass"], [0.5, "bend"], [1.0, "air_shatter"]], "pieces": 3, "fallback": "pass"},
		{"move": "sound_clap", "tier": 3, "expect": "air_shatter"})
	AirRules._cell("boulder", c, {"bands": [[0.0, "pass"], [1.0, "air_shatter"]], "pieces": 4, "fallback": "pass"}, {"move": "sound_clap", "tier": 3, "expect": "pass"})
	AirRules._cell("magma", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 3, "expect": "pass"})
	AirRules._cell("lava_wave", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 3, "expect": "pass"})
	AirRules._cell("metal", c, {"bands": [[0.0, "pass"], [0.5, "bend"]]}, {"move": "sound_clap", "tier": 1, "expect": "bend"})
	# sand and fog absorb sound; Roar T3 only weakens a cloud; water lets it pass (a Roar atomises a stream)
	AirRules._cell("sand", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 2, "expect": "pass"})
	AirRules._cell("sand_cloud", c, {"bands": [[0.0, "pass"], [4.0, "weaken"]]}, {"move": "sound_clap", "tier": 3, "expect": "weaken"})
	AirRules._cell("sand_surge", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 3, "expect": "pass"})
	AirRules._cell("water", c, {"bands": [[0.0, "pass"], [1.5, "weaken"]]}, {"move": "sound_clap", "tier": 2, "expect": "weaken"})
	AirRules._cell("water_wave", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 3, "expect": "pass"})
	AirRules._cell("mist", c, {"bands": [[0.0, "pass"], [5.0, "air_compress"]]}, {"move": "sound_clap", "tier": 2, "expect": "air_compress"})
	AirRules._cell("steam", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 2, "expect": "pass"})
	AirRules._cell("vine", c, {"bands": [[0.0, "pass"], [1.4, "weaken"]]}, {"move": "sound_clap", "tier": 2, "expect": "weaken"})
	# fire: a roar blows small flames out; blue fire and lightning do not care
	AirRules._cell("flame", c, {"bands": [[0.0, "pass"], [1.0, "weaken"], [2.0, "extinguish"]]}, {"move": "sound_clap", "tier": 2, "expect": "extinguish"})
	AirRules._cell("ember", c, {"bands": [[0.0, "pass"], [1.0, "weaken"], [2.0, "extinguish"]]}, {})
	AirRules._cell("blue_fire", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 2, "expect": "pass"})
	AirRules._cell("lightning", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 2, "expect": "pass"})
	# pressure vs pressure: sound weakens blasts (x0.6), gusts (x0.8), tornadoes (x0.6); a vacuum has no medium
	AirRules._cell("blast", c, {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 0.6}, {"move": "sound_clap", "tier": 2, "expect": "weaken"})
	AirRules._cell("gust", c, {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 0.8}, {"move": "sound_clap", "tier": 2, "expect": "weaken"})
	AirRules._cell("tornado", c, {"bands": [[0.0, "pass"], [0.5, "air_shrink"]], "eff": 0.6}, {"move": "sound_clap", "tier": 3, "expect": "air_shrink", "tp": 30.0})
	AirRules._cell("vacuum", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_clap", "tier": 3, "expect": "pass"})
	AirRules._cell("sound", c, {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 1.0}, {})


static func _barrier_cells() -> void:
	var c := "barrier_sound"
	# ice and glass shatter against the vibrating front (x2.5); light solids are turned (a stone shot only bends it)
	for t in ["ice", "glass"]:
		AirRules._cell(t, c, {"outcome": "air_shatter", "partial": "bend", "fail": "block", "eff": 2.5, "pieces": 3, "chip": 0.0, "bal": 0.0, "knock": 0.0, "fallback": "block"},
			{"move": "sound_barrier", "tier": 0, "expect": "air_shatter"})
	for t in ["stone", "hot_rock", "metal", "sand"]:
		var ref := {}
		if t == "stone":
			ref = {"move": "sound_barrier", "tier": 0, "expect": "bend"}
		elif t == "metal":
			ref = {"move": "sound_barrier", "tier": 0, "expect": "deflect", "tp": 6.0}
		AirRules._cell(t, c, {"outcome": "deflect", "partial": "bend", "fail": "bend", "eff": 1.0, "side": 0.45, "up": 2.0, "verb": "sound"}, ref)
	for t in ["stone_heavy", "boulder", "magma", "lava_wave"]:
		AirRules._cell(t, c, {"bands": [[0.0, "overwhelm"], [1.0, "block"]], "chip": 0.12, "bal": 0.55, "knock": 0.35},
			{"move": "sound_barrier", "tier": 0, "expect": "overwhelm"} if t == "boulder" else {})
	# sound: cancelled (anti-phase), thrown back by a perfect guard (Echo Return); a vacuum nullifies the barrier
	AirRules._cell("sound", c, {"outcome": "absorb", "partial": "weaken", "fail": "weaken", "perfect": "reflect", "aura": true},
		{"move": "sound_barrier", "tier": 0, "expect": "absorb", "tp": 10.0})
	AirRules._cell("vacuum", c, {"bands": [[0.0, "overwhelm"]], "full_at": 0.0}, {"move": "sound_barrier", "tier": 0, "expect": "overwhelm"})
	AirRules._cell("sand_surge", c, {"bands": [[0.0, "weaken"], [1.0, "block"]]}, {})
	AirRules._cell("water", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_barrier", "tier": 0, "expect": "pass"})
	AirRules._cell("water_wave", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {})


static func _ping_cells() -> void:
	var c := "ground_ping"
	# ground lines whose power the pulse reaches are stilled; weaker pulses only slow them
	var still := {"bands": [[0.0, "pass"], [0.5, "weaken"], [1.0, "air_still"]]}
	var sand := still.duplicate(true)
	sand["eff"] = 1.5
	AirRules._cell("sand_surge", c, sand, {"move": "sound_ping", "tier": 1, "expect": "air_still", "tp": 20.0})
	AirRules._cell("water_wave", c, still, {"move": "sound_ping", "tier": 3, "expect": "air_still", "tp": 24.0})
	AirRules._cell("spikes", c, still, {"move": "sound_ping", "tier": 2, "expect": "air_still", "tp": 20.0})
	AirRules._cell("vine", c, still, {"move": "sound_ping", "tier": 1, "expect": "air_still", "tp": 14.0})
	AirRules._cell("lightning", c, still, {"move": "sound_ping", "tier": 3, "expect": "air_still", "tp": 26.0})
	AirRules._cell("lava_wave", c, {"bands": [[0.0, "pass"]], "full_at": 0.0}, {"move": "sound_ping", "tier": 3, "expect": "pass"})
	AirRules._cell("*", c, {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "air_ping_default"})


static func _tremor_cells() -> void:
	var c := "tremor"
	# loose stones on the tremor's path are popped into the air (seizable from range); brittle ones shatter
	for t in ["stone", "hot_rock", "metal", "sand", "stone_heavy"]:
		AirRules._cell(t, c, {"bands": [[0.0, "air_pop"]], "full_at": 0.0, "inert": "air_pop", "inert_else": "pass"},
			{"move": "sound_tremor", "tier": 0, "expect": "air_pop"} if t == "stone" else {})
	for t in ["ice", "glass"]:
		AirRules._cell(t, c, {"outcome": "air_shatter", "partial": "air_pop", "fail": "air_pop", "eff": 2.5, "pieces": 3, "inert": "air_shatter", "inert_else": "pass", "fallback": "pass"},
			{"move": "sound_tremor", "tier": 0, "expect": "air_shatter"} if t == "ice" else {})
	AirRules._cell("*", c, {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "air_tremor_default"})


## A loose stone on a tremor's path is popped up (vy ~ 3.5 + P / 6, less for heavy ones) and can be seized in the air.
static func o_pop(_w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive:
		return false
	var vy := clampf((3.5 + float(res.cp_eff) / 6.0) * pow(20.0 / maxf(b.mass, 4.0), 0.35), 1.5, 8.0)
	b.vel.y = maxf(b.vel.y, vy)
	b.vel += c.dir * 1.5
	b.on_ground = false
	b.rest_time = 0.0
	b.touch(Outcomes._id(c), "pop", _w.tick)
	_w.emit("pop", {"body": b.id, "vy": vy})
	res.pass_scale = 1.0
	AirOutcomes.report(res, "push")
	return true


## A ground line that the Ground Ping out-powers is stilled on the spot (settles / fades through its own end rules).
static func o_still(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive:
		return false
	res.stopped = true
	res.pass_scale = 0.0
	if b.form == Sim.Form.WAVE:
		w.emit("wave_disrupted", {"body": b.id, "by": "ping"})
		w._settle_wave(b, "disrupted")
	elif b.form == Sim.Form.ZONE:
		w.close_zone(b, "disrupted")
	else:
		b.attack_id = 0
		b.vel *= 0.2
	AirOutcomes.report(res, "disrupt")
	return true
