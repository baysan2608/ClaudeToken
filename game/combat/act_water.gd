class_name ActWater
extends RefCounted
## Water: lash (tap) / ice lance (hold: freeze a limited shape), draw-shape-release
## technique. Water must come from somewhere: the pool, a puddle or the 6 kg
## waterskin (refilled by standing in the pool). Quantities are tracked exactly.


static func _water_filter(b: MatBody) -> bool:
	# Legality through the engine (legacy cell puddle x grip_water: reclaim).
	return b.is_water() and b.phase == Sim.Phase.LIQUID and b.mass > 0.5 and Interactions.allows(b, &"grip_water")


static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	inst.data["face"] = w.aim_dir(a, it)
	if inst.id == "water_attack":
		var have := a.water_carried + (w.held(a).mass if w.held(a) != null and w.held(a).is_water() else 0.0)
		if have < 1.0 and not a.in_water:
			inst.data["fizzle"] = true
			w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
			return
		if not w.spend_focus(a, float(inst.def.cost)):
			inst.data["fizzle"] = true
			w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.id == "water_attack":
		if inst.data.get("fizzle", false):
			return ActionInst.P.RECOVERY
		return w.attack_after_startup(a, inst, it)
	return ActionInst.P.CHANNEL


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if inst.id == "water_attack" and p == ActionInst.P.ACTIVE:
		if inst.heavy:
			_ice_lance(w, a, inst)
		else:
			_lash(w, a, inst)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"water_attack":
			if not it.attack_held:
				inst.data["released"] = true
			inst.data["face"] = w.aim_dir(a, it)
			if inst.phase == ActionInst.P.CHARGE and inst.data.get("released", false) and inst.total >= float(inst.def.heavy_min):
				if w.spend_focus(a, float(inst.def.heavy_cost) - float(inst.def.cost)):
					w.set_phase(a, inst, ActionInst.P.ACTIVE)
				else:
					w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "ice_lance"})
					inst.heavy = false
					w.set_phase(a, inst, ActionInst.P.ACTIVE)
		"water_tech":
			if it.tech_cancel and (inst.phase == ActionInst.P.STARTUP or inst.phase == ActionInst.P.CHANNEL):
				# Honoured from the first frame: a cancel during startup never draws or fires.
				var hb := w.held(a)
				if hb != null:
					w.release_body(a, Vector3(0, -1, 0), false)   # falls and becomes a puddle
				w.emit("cancel", {"actor": a.id, "move": inst.id})
				w.set_phase(a, inst, ActionInst.P.RECOVERY)
				return
			if inst.phase != ActionInst.P.CHANNEL:
				return
			inst.data["aim"] = w.aim_dir(a, it)
			inst.data["aim_active"] = it.aim_active
			inst.data["face"] = inst.data.aim
			_draw(w, a, inst, it)
			var b := w.held(a)
			if b != null:
				var sway := sin(inst.total * 5.0) * 0.25
				var side := a.forward().cross(Vector3.UP)
				b.hold_point = a.pos + Vector3(0, 1.3 + 0.15 * sin(inst.total * 3.0), 0) + a.forward() * 0.8 + side * sway
				if not it.tech_held:
					w.set_phase(a, inst, ActionInst.P.ACTIVE)
					_stream(w, a, inst)
			elif not it.tech_held:
				w.emit("whiff", {"actor": a.id, "move": inst.id})
				w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	var b := w.held(a)
	if b == null or not b.is_water():
		return
	if inst.id == "water_tech" and reason == "cancel:guard":
		return   # the guard keeps the held water as a shield
	w.release_body(a, Vector3(0, -1, 0), false)


# ---------------------------------------------------------------------------

static func _draw(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var b := w.held(a)
	var maxm := float(inst.def.max_draw)
	if b != null and b.mass >= maxm - 0.01:
		return
	var rate := float(inst.def.draw_rate) * Sim.DT
	var reach := float(inst.def.reach)
	# Source priority: pool (if near), puddle in the aim cone, then the waterskin.
	var src: MatBody = null
	var src_point := Vector3.ZERO
	var pn := Vector3(clampf(a.pos.x, w.arena.pool_min.x, w.arena.pool_max.x), w.arena.pool_level,
		clampf(a.pos.z, w.arena.pool_min.y, w.arena.pool_max.y))
	if Vector2(pn.x - a.pos.x, pn.z - a.pos.z).length() < reach and w.pool.mass > rate:
		src = w.pool
		src_point = pn
	else:
		var pd := w.find_body(a, w.aim_dir(a, it), reach, 70.0, _water_filter)
		if pd != null:
			src = pd
			src_point = pd.pos
	if src != null:
		var room := maxm - (b.mass if b != null else 0.0)
		var take := minf(minf(rate, src.mass), room)
		# Drawn water carries the source's exact state (temperature and ice fraction).
		var e_take := take * (Sim.WATER_C * (src.temp - Sim.AMBIENT_C) - Sim.WATER_LATENT_FUSION * (1.0 - src.liquid))
		if b == null:
			b = w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, take, src_point + Vector3(0, 0.2, 0), "draw:%d" % src.id)
			b.temp = src.temp
			b.liquid = src.liquid
			b.lineage.append(src.id)
			w.take_control(a, b, 0.9, "draw")
		else:
			var e := b.thermal_energy() + e_take
			b.liquid = (b.liquid * b.mass + src.liquid * take) / (b.mass + take)
			b.mass += take
			w._set_energy(b, e)
			b.update_radius()
		src.mass -= take
		if src.form == Sim.Form.PUDDLE:
			src.update_radius_puddle()
			if src.mass <= 0.05:
				w.decay_body(src, "drained")
		if w.tick % 8 == 0:
			w.emit("draw_water", {"actor": a.id, "body": b.id, "from": src.id, "at": src_point})
		return
	if b == null and a.water_carried >= 1.0:
		b = w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, a.water_carried, a.chest() + a.forward() * 0.6, "waterskin:%d" % a.id)
		a.water_carried = 0.0
		w.take_control(a, b, 0.9, "draw")
	elif b == null and inst.t > 0.3:
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func _stream(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.held(a)
	if b == null:
		return
	var target := ActEarth._throw_target(w, a, inst)
	var v := ActEarth.launch_vel(b.pos, target, float(inst.def.speed))
	var s := clampf(b.mass / 8.0, 0.5, 1.5)
	b.form = Sim.Form.STREAM
	w.release_body(a, v, true, float(inst.def.damage) * s, float(inst.def.balance) * s)
	w.emit("launch", {"actor": a.id, "body": b.id, "speed": v.length(), "kind": "stream"})


static func _lash(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var dir: Vector3 = inst.data.face
	var d := inst.def
	w.emit("lash", {"actor": a.id, "dir": dir, "range": d.range})
	# The lash is a water volume: a threat to fighters, a counter (class "water_jet") to shots it meets.
	var lash := Agent.of_volume(w, a, inst, &"water", a.chest(), dir, {"P": float(d.get("power", 8.0))})
	lash.ccls = &"water_jet"
	FxEvents.fx_for(w, a, inst, "cone", "water", {"length": float(d.range), "angle": float(d.arc) * 0.5, "power": lash.power})
	for t in w.actors_in_cone(a, dir, float(d.range), float(d.arc) * 0.5):
		var res := w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id, "damage": d.damage,
			"balance": d.balance, "knock": dir * float(d.knock), "kind": "water", "from": a.chest(), "agent": lash})
		if res != "dup" and res != "evaded":
			t.wetness = 1.0
	# The lash also knocks light incoming stones aside (legacy cell (*, water_jet): <= 25 kg).
	for b in w.bodies:
		if b.alive and b.is_projectile() and b.attack_owner != a.id:
			var to := b.pos - a.chest()
			if to.length() < float(d.range) and Vector3(to.x, 0, to.z).normalized().dot(dir) > cos(deg_to_rad(float(d.arc) * 0.5)):
				Interactions.resolve(w, Agent.of_body(w, b, a), lash, {"site": "lash"})


static func _ice_lance(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var d := inst.def
	var want := float(d.shard_mass)
	var b := w.held(a)
	var shard: MatBody = null
	if b != null and b.is_water():
		shard = b if b.mass <= want + 0.01 else w.split_body(b, want, b.pos)
		if shard == b:
			w.release_body(a, Vector3.ZERO, false)
	else:
		var m := minf(want, a.water_carried)
		if m < 0.5:
			w.emit("insufficient", {"actor": a.id, "what": "water", "move": "ice_lance"})
			return
		a.water_carried -= m
		shard = w.spawn_body(Sim.Mat.WATER, Sim.Form.SHARD, m, a.hand_point(), "waterskin:%d" % a.id)
	# Freezing dumps the water's heat into the environment (game rule: water technique).
	var e0 := shard.thermal_energy()
	shard.liquid = 0.0
	shard.temp = -5.0
	shard.phase = Sim.Phase.FROZEN
	shard.form = Sim.Form.SHARD
	shard.update_radius()
	w.ledger.freeze_dump += shard.thermal_energy() - e0
	w.emit("transform", {"body": shard.id, "at": shard.pos, "from": "water", "to": "ice", "why": "frozen"})
	shard.controller = -1
	shard.pos = a.hand_point()
	var target := ActEarth._throw_target(w, a, inst)
	shard.vel = ActEarth.launch_vel(shard.pos, target, float(d.shard_speed))
	shard.attack_id = w.new_attack_id()
	shard.attack_owner = a.id
	shard.hit_set.clear()
	shard.hit_set[a.id] = true
	shard.damage = float(d.heavy_damage)
	shard.balance_damage = float(d.heavy_balance)
	shard.max_life = Sim.REMNANT_LIFETIME
	w.emit("launch", {"actor": a.id, "body": shard.id, "speed": d.shard_speed, "kind": "ice"})
