class_name VerbVolume
extends RefCounted
## Instant volumes: cone, beam, burst (MOVESET §15.7). A volume is an Agent (Agent.of_volume): a threat
## to the fighters it reaches (hit_actor answers with their guard) and to barrier bodies, a counter to
## the loose bodies it meets (Interactions.is_barrier decides who meets whom).
## Shared params: cls (volume class: flame blue_fire lightning blast gust sound water sand steam vacuum
## frost), channel (K H C E P; default from cls), power (PU), damage, balance, knock, lift, heat (HU paid
## by the move: becomes the volume's heat budget, the rest is booked as spent), status, status_t, wet.
##   cone: range, angle (half angle, deg)
##   beam: range, width, pierce (bool), conduct (lightning through Conduction), pulse (s, sustained
##         over the active window), max_hops, conduct_budget
##   burst: at (self ahead aim), distance, radius, fuse (s: a "fuse" zone detonates later)


static func _volume(w: CombatWorld, a: ActorState, inst: ActionInst, pos: Vector3, dir: Vector3) -> Agent:
	var cls := StringName(String(Charge.param(inst, "cls", _default_cls(inst))))
	var chn := String(Charge.param(inst, "channel", Interactions.CLASS_CHANNEL.get(cls, "P")))
	var heat := Verbs.take_heat(inst)
	var power := float(Charge.param(inst, "power", heat / Interactions.HU_PER_PU if chn == "H" else 8.0))
	var ch := {chn: power}
	if heat > 0.0:
		ch["heat_hu"] = heat
		if chn != "H":
			ch["H"] = heat / Interactions.HU_PER_PU
	var v := Agent.of_volume(w, a, inst, cls, pos, dir, ch)
	v.data["knock"] = float(Charge.param(inst, "knock", 3.0))
	return v


static func _default_cls(inst: ActionInst) -> String:
	match Verbs.fx_mat(inst):
		"flame", "magma":
			return "flame"
		"blue":
			return "blue_fire"
		"lightning":
			return "lightning"
		"blast":
			return "blast"
		"water":
			return "water"
		"sand":
			return "sand"
		"steam", "mist":
			return "steam"
		"vacuum":
			return "vacuum"
		"sound":
			return "sound"
		"ice":
			return "frost"
	return "gust"


static func _hit_info(a: ActorState, inst: ActionInst, v: Agent, from: Vector3, knock_dir: Vector3) -> Dictionary:
	return {"attacker": a.id, "attack_id": inst.attack_id if inst != null else 0, "damage": float(Charge.param(inst, "damage", 6.0)) if inst != null else 6.0,
		"balance": float(Charge.param(inst, "balance", 14.0)) if inst != null else 14.0,
		"knock": knock_dir * float(v.data.get("knock", 3.0)) + Vector3(0, float(Charge.param(inst, "lift", 1.0)) if inst != null else 1.0, 0),
		"kind": String(v.cls), "from": from, "agent": v, "power": v.power, "tier": v.tier, "mat": Verbs.fx_mat(inst) if inst != null else ""}


## Fighters hit by a volume: statuses / wetness on a landed hit.
static func _after_hit(w: CombatWorld, a: ActorState, inst: ActionInst, t: ActorState, res: String) -> void:
	if res == "hit" or res == "knockdown":
		var st := String(Charge.param(inst, "status", ""))
		if st != "":
			Status.apply(w, t, st, float(Charge.param(inst, "status_t", 1.0)), float(Charge.param(inst, "status_mag", 1.0)), a.id)
		if bool(Charge.param(inst, "wet", false)):
			t.wetness = 1.0


## A volume meets a body: barriers counter the volume, loose bodies are countered by it.
static func meet_body(w: CombatWorld, a: ActorState, v: Agent, b: MatBody) -> Dictionary:
	if Interactions.is_barrier(w, b):
		var c := Agent.of_body(w, b)
		c.actor = w.get_actor(b.last_actor if b.form == Sim.Form.WALL else (b.controller if b.controller >= 0 else b.owner))
		return Interactions.resolve(w, v, c, {"site": "volume"}, Interactions.PASS_RULE)
	return Interactions.resolve(w, Agent.of_body(w, b, a), v, {"site": "volume"}, Interactions.PASS_RULE)


static func cone(w: CombatWorld, a: ActorState, inst: ActionInst) -> Agent:
	var rng_m := float(Charge.param(inst, "range", 5.0))
	var ang := float(Charge.param(inst, "angle", 30.0))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var v := _volume(w, a, inst, a.chest(), dir)
	Verbs.fx(w, a, inst, "cone", {"length": rng_m, "angle": ang, "power": v.power})
	var cos_lim := cos(deg_to_rad(ang))
	for b in w.bodies:
		if not b.alive or b.static_body and b.form != Sim.Form.WALL or b.controller == a.id or b == w.pool:
			continue
		var to := b.pos - a.chest()
		var flat := Vector3(to.x, 0, to.z)
		if flat.length() > rng_m + b.radius or (flat.length() > 0.5 and flat.normalized().dot(dir) < cos_lim):
			continue
		meet_body(w, a, v, b)
	for t in w.actors_in_cone(a, dir, rng_m, ang):
		var res := w.hit_actor(t, _hit_info(a, inst, v, a.chest(), dir))
		_after_hit(w, a, inst, t, res)
	if v.heat > 0.0:
		w.ledger.spent += v.heat
		v.heat = 0.0
	return v


## Beam along the aim: stops at the first barrier that stops it (rules), hits the first fighter
## (all with pierce). Lightning beams (cls lightning or conduct) go through Conduction.discharge.
static func beam(w: CombatWorld, a: ActorState, inst: ActionInst) -> Agent:
	inst.data["pulse_t"] = 0.0
	return _beam_once(w, a, inst)


static func beam_tick(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var pulse := float(Charge.param(inst, "pulse", 0.0))
	if pulse <= 0.0:
		return
	inst.data["pulse_t"] = float(inst.data.get("pulse_t", 0.0)) + Sim.DT
	if float(inst.data.pulse_t) >= pulse:
		inst.data["pulse_t"] = 0.0
		inst.attack_id = w.new_attack_id()   # each pulse is a new hit instance
		if float(Charge.param(inst, "heat", 0.0)) > 0.0:
			Verbs.pay(w, a, inst, {"heat": float(Charge.param(inst, "heat", 0.0)) * pulse / maxf(float(Charge.param(inst, "active", inst.def.active)), 0.05)})
		_beam_once(w, a, inst)


static func _beam_once(w: CombatWorld, a: ActorState, inst: ActionInst) -> Agent:
	var rng_m := float(Charge.param(inst, "range", 10.0))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var start := a.hand_point() + Vector3(0, 0.2, 0)
	var v := _volume(w, a, inst, start, dir)
	if v.cls == &"lightning" or bool(Charge.param(inst, "conduct", false)):
		var aimp: Vector3 = inst.data.get("aim_point", a.chest() + dir * rng_m)
		var def := {"range": rng_m, "damage": float(Charge.param(inst, "damage", 10.0)), "balance": float(Charge.param(inst, "balance", 20.0)),
			"conduct_budget": float(Charge.param(inst, "conduct_budget", float(Charge.param(inst, "damage", 10.0)))),
			"max_hops": int(Charge.param(inst, "max_hops", 3)), "E": v.power, "tier": inst.tier()}
		var out := Conduction.discharge(w, a, aimp, def, inst.attack_id, bool(Charge.param(inst, "allow_redirect", true)))
		Verbs.fx(w, a, inst, "beam", {"path": out.path, "length": rng_m, "power": v.power})
		return v
	var width := float(Charge.param(inst, "width", 0.6))
	var end := start + dir * rng_m
	var stop_t := 1.0
	for hb in Conduction.barriers_on(w, start, end):
		var t := float(hb.t)
		if hb.body == null:
			stop_t = t
			break
		var r := meet_body(w, a, v, hb.body)
		if bool(r.stopped) or float(r.pass_scale) <= 0.0:
			stop_t = t
			break
		v.power *= float(r.pass_scale)
	end = start.lerp(end, stop_t)
	var seg := end - start
	var seg_len := seg.length()
	var pierce := bool(Charge.param(inst, "pierce", false))
	# Loose bodies along the beam.
	for b in w.bodies:
		if not b.alive or b.static_body or b.controller == a.id or b.form == Sim.Form.WALL:
			continue
		var tb := clampf((b.pos - start).dot(dir), 0.0, seg_len)
		if (start + dir * tb).distance_to(b.pos) <= width + b.radius:
			meet_body(w, a, v, b)
	var hits: Array = []
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0:
			continue
		var tt := clampf((t.chest() - start).dot(dir), 0.0, seg_len)
		if (start + dir * tt).distance_to(t.chest()) <= width + Sim.ACTOR_RADIUS + 0.4:
			hits.append([tt, t])
	hits.sort_custom(func(x, y): return float(x[0]) < float(y[0]) or (float(x[0]) == float(y[0]) and x[1].id < y[1].id))
	for h in hits:
		var res := w.hit_actor(h[1], _hit_info(a, inst, v, start, dir))
		_after_hit(w, a, inst, h[1], res)
		if not pierce:
			end = start + dir * float(h[0])
			break
	Verbs.fx(w, a, inst, "beam", {"length": start.distance_to(end), "path": PackedVector3Array([start, end]), "power": v.power})
	if v.heat > 0.0:
		w.ledger.spent += v.heat
		v.heat = 0.0
	return v


## Burst of the action at its point (self / ahead / aim), or a fuse zone that detonates later.
static func burst(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var at := String(Charge.param(inst, "at", "ahead"))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var p := a.pos + Vector3(0, 1.0, 0)
	match at:
		"ahead":
			p = a.pos + dir * float(Charge.param(inst, "distance", 3.0)) + Vector3(0, 1.0, 0)
		"aim":
			var ap: Vector3 = inst.data.get("aim_point", a.chest() + dir * 8.0)
			var rmax := float(Charge.param(inst, "range", 10.0))
			if a.chest().distance_to(ap) > rmax:
				ap = a.chest() + (ap - a.chest()).normalized() * rmax
			p = ap
	p.y = maxf(p.y, w.arena.ground_height(p.x, p.z, p.y + 0.5) + 0.5)
	var prm := {"radius": float(Charge.param(inst, "radius", 2.0)), "power": float(Charge.param(inst, "power", 8.0)),
		"damage": float(Charge.param(inst, "damage", 8.0)), "balance": float(Charge.param(inst, "balance", 20.0)),
		"knock": float(Charge.param(inst, "knock", 6.0)), "lift": float(Charge.param(inst, "lift", 2.0)),
		"cls": String(Charge.param(inst, "cls", "blast")), "heat_hu": Verbs.take_heat(inst), "mat": Verbs.fx_mat(inst),
		"status": String(Charge.param(inst, "status", "")), "status_t": float(Charge.param(inst, "status_t", 1.0))}
	var fuse := float(Charge.param(inst, "fuse", 0.0))
	if fuse > 0.0:
		var z := w.spawn_zone(&"fuse", p, 0.3, a.id, prm.power, Sim.Mat.AIR, 0.0, -1.0)
		z.props["fuse"] = fuse
		z.props["burst"] = prm
		z.tier = inst.tier()
		z.heat_payload = float(prm.heat_hu)   # the paid heat waits in the pocket (counted by thermal_energy)
		prm.heat_hu = 0.0
		Verbs.fx(w, a, inst, "cast", {"pos": p, "dur": fuse})
		return
	burst_at(w, a, inst, p, prm)


## Detonation at a point. prm: radius power damage balance knock lift cls heat_hu mat status status_t.
## heat_hu is spent here (ledger spent); fighters in the radius are hit, bodies met by the rules.
static func burst_at(w: CombatWorld, a: ActorState, inst: ActionInst, p: Vector3, prm: Dictionary) -> void:
	var cls := StringName(String(prm.get("cls", "blast")))
	var chn := String(Interactions.CLASS_CHANNEL.get(cls, "P"))
	var ch := {chn: float(prm.get("power", 8.0))}
	var heat := float(prm.get("heat_hu", 0.0))
	if heat > 0.0:
		ch["heat_hu"] = heat
	var v := Agent.of_volume(w, a, inst, cls, p, Vector3.ZERO, ch)
	v.data["knock"] = float(prm.get("knock", 6.0))
	var r := float(prm.get("radius", 2.0))
	FxEvents.fx(w, "burst", String(prm.get("mat", "blast")), {"actor": a.id if a != null else -1, "pos": p, "radius": r,
		"power": v.power, "tier": v.tier, "move": inst.id if inst != null else "", "element": inst.element if inst != null else -1})
	for b in w.bodies:
		if not b.alive or (b.static_body and b.form != Sim.Form.WALL) or b == w.pool:
			continue
		if b.pos.distance_to(p) > r + b.radius:
			continue
		if a != null and b.controller == a.id:
			continue
		var rel := b.pos - p
		v.dir = rel.normalized() if rel.length() > 0.05 else Vector3.UP
		meet_body(w, a, v, b)
	var aid := inst.attack_id if inst != null else w.new_attack_id()
	for t in w.actors:
		if t.health <= 0.0 or (a != null and (t == a or t.team == a.team)):
			continue
		var rel2 := t.chest() - p
		if rel2.length() > r + Sim.ACTOR_RADIUS:
			continue
		if not w.los(p, t.chest()):
			continue
		var kd := Vector3(rel2.x, 0, rel2.z).normalized() if Vector3(rel2.x, 0, rel2.z).length() > 0.05 else t.forward() * -1.0
		var info := {"attacker": a.id if a != null else -1, "attack_id": aid, "damage": float(prm.get("damage", 8.0)),
			"balance": float(prm.get("balance", 20.0)), "knock": kd * float(prm.get("knock", 6.0)) + Vector3(0, float(prm.get("lift", 2.0)), 0),
			"kind": String(cls), "from": p, "agent": v, "power": v.power}
		var res := w.hit_actor(t, info)
		if (res == "hit" or res == "knockdown") and String(prm.get("status", "")) != "":
			Status.apply(w, t, String(prm.status), float(prm.get("status_t", 1.0)), 1.0, a.id if a != null else -1)
	if v.heat > 0.0:
		w.ledger.spent += v.heat
		v.heat = 0.0


## Body tick for "fuse" zones: detonate after props.fuse seconds (or when its owner re-triggers).
static func fuse_tick(w: CombatWorld, z: MatBody, _dt: float) -> bool:
	if z.age >= float(z.props.get("fuse", 0.5)) or z.props.get("trigger", false):
		var prm: Dictionary = z.props.get("burst", {}).duplicate()
		prm["heat_hu"] = z.heat_payload
		z.heat_payload = 0.0
		var owner := w.get_actor(z.owner)
		burst_at(w, owner, null, z.pos, prm)
		w.close_zone(z, "detonated")
	return true
