class_name WaterJet
extends RefCounted
## Pressure Jet (T2) and Cutting Jet (T3) of Water Bullet: a sustained jet that is still connected to its
## caster. The connection is a liquid WATER body (tag "jet") held by the caster (take_control), so the
## conduction graph (Materials.conducts: liquid water; Conduction.actor_nodes: held bodies) carries a
## lightning strike on the jet back into its owner. The jet body is booked water from the waterskin /
## pool: when the jet ends it falls as a puddle (water mass stays exact).
##   inst.data.jet = body id, jet_t, jet_end (where it last hit)

const PULSE := 0.1


static func start(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var kg := float(Charge.param(inst, "jet_kg", 3.0))
	var got := WaterUtil.take(w, a, kg)
	if got < 0.8:
		WaterUtil.give_back(w, a, got, a.pos)
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		inst.data["active"] = 0.02
		return true
	inst.data["active"] = float(Charge.param(inst, "jet_t", 0.8))
	var dir: Vector3 = inst.data.get("face", a.forward())
	var b := w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, got, a.chest() + dir * 1.0, "jet:%d" % a.id)
	b.tag = &"jet"
	b.max_life = -1.0
	w.take_control(a, b, 0.97, "jet")
	b.hold_point = b.pos
	inst.data["jet"] = b.id
	inst.data["jet_t"] = 0.0
	inst.data["jet_end"] = a.chest() + dir * float(Charge.param(inst, "jet_range", 10.0))
	w.emit("jet", {"actor": a.id, "body": b.id, "tier": inst.tier(), "on": true})
	_pulse(w, a, inst, b)
	return true


## Called every tick of the action (kit_water tick handler): pulses while active.
static func tick(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	if not inst.data.has("jet") or inst.phase != ActionInst.P.ACTIVE:
		return
	var b := w.get_body(int(inst.data.jet))
	if b == null or not b.alive or b.controller != a.id:
		inst.data.erase("jet")
		return
	inst.data["jet_t"] = float(inst.data.get("jet_t", 0.0)) + Sim.DT
	if float(inst.data.jet_t) >= PULSE - 1e-6:
		inst.data["jet_t"] = 0.0
		_pulse(w, a, inst, b)


static func end(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.get_body(int(inst.data.get("jet", -1)))
	inst.data.erase("jet")
	if b == null or not b.alive:
		return
	if a.held_body == b.id:
		a.held_body = -1
	b.controller = -1
	b.authority = 0.0
	b.vel = Vector3.ZERO
	b.tag = &""
	var e: Vector3 = inst.data.get("jet_end", a.pos + a.forward() * 4.0)
	b.pos = WaterUtil.ground_at(w, Vector3(e.x, 0.0, e.z)) + Vector3(0, 0.3, 0)
	b.update_radius()
	w.emit("jet", {"actor": a.id, "body": b.id, "tier": inst.tier(), "on": false})
	w._water_to_puddle(b)


## One 0.1 s pulse: a water volume (threat class water, counter class water_jet) along the aim.
static func _pulse(w: CombatWorld, a: ActorState, inst: ActionInst, jet: MatBody) -> void:
	var dir: Vector3 = inst.data.get("face", a.forward())
	var rng_m := float(Charge.param(inst, "jet_range", 10.0))
	var width := float(Charge.param(inst, "jet_width", 0.5))
	var cut := bool(Charge.param(inst, "cut", false))
	var start := a.hand_point() + Vector3(0, 0.2, 0)
	var v := Agent.of_volume(w, a, inst, &"water", start, dir, {"P": float(Charge.param(inst, "jet_power", 10.0))})
	v.ccls = &"water_jet"
	v.data["knock"] = float(Charge.param(inst, "jet_knock", 3.0))
	inst.attack_id = w.new_attack_id()   # every pulse is a new hit instance
	var end := start + dir * rng_m
	var stop_t := 1.0
	for hb in Conduction.barriers_on(w, start, end):
		var t := float(hb.t)
		if hb.body == null:
			stop_t = t
			break
		var hbody: MatBody = hb.body
		if cut and hbody.form == Sim.Form.WALL and ["sand", "mud", "vine"].has(String(hbody.tag)):
			hbody.wall_damage_add(0.3)          # a thin jet cuts soft walls
			if hbody.wall_damage >= 1.0:
				w._crumble_wall(hbody)
				continue
		var r := VerbVolume.meet_body(w, a, v, hbody)
		if bool(r.stopped) or float(r.pass_scale) <= 0.0:
			stop_t = t
			break
		v.power *= float(r.pass_scale)
	end = start.lerp(end, stop_t)
	var seg := end - start
	var seg_len := maxf(seg.length(), 0.01)
	for o in w.bodies:
		if not o.alive or o == jet or o.static_body or o.controller == a.id or o.form == Sim.Form.WALL or o.form == Sim.Form.POOL:
			continue
		var tb := clampf((o.pos - start).dot(dir), 0.0, seg_len)
		if (start + dir * tb).distance_to(o.pos) <= width + o.radius + 0.1:
			VerbVolume.meet_body(w, a, v, o)
	var hits: Array = []
	for t in w.actors:
		if t == a or t.team == a.team or t.health <= 0.0:
			continue
		var tt := clampf((t.chest() - start).dot(dir), 0.0, seg_len)
		if (start + dir * tt).distance_to(t.chest()) <= width + Sim.ACTOR_RADIUS + 0.4:
			hits.append([tt, t])
	hits.sort_custom(func(x, y): return float(x[0]) < float(y[0]) or (float(x[0]) == float(y[0]) and x[1].id < y[1].id))
	for h in hits:
		var tgt: ActorState = h[1]
		var res := w.hit_actor(tgt, {"attacker": a.id, "attack_id": inst.attack_id, "damage": float(Charge.param(inst, "jet_dmg", 2.5)),
			"balance": float(Charge.param(inst, "jet_balance", 4.0)), "knock": dir * float(v.data.knock) + Vector3(0, 0.4, 0),
			"kind": "water", "from": start, "agent": v, "power": v.power, "tier": inst.tier(), "mat": "water"})
		if res == "hit" or res == "knockdown" or res == "block":
			tgt.wetness = 1.0
		end = start + dir * float(h[0])
		break
	inst.data["jet_end"] = end
	jet.hold_point = (start + end) * 0.5
	jet.pos = jet.hold_point
	jet.radius = maxf(0.4, start.distance_to(end) * 0.5)
	Verbs.fx(w, a, inst, "beam", {"length": start.distance_to(end), "path": PackedVector3Array([start, end]), "power": v.power, "dir": dir})
	if v.heat > 0.0:
		w.ledger.spent += v.heat
		v.heat = 0.0
