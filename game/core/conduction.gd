class_name Conduction
extends RefCounted
## Lightning as a bounded gameplay conduction graph, built once per discharge.
## Nodes: the pool, the metal plate, liquid puddles, and (moveset engine) conductive bodies -
## metal of any phase, rods, caltrops zones, liquid water bodies (streams, blobs, waves, held
## shields), fog zones (60 %), charged bodies - plus actors standing on / in / holding them.
## Edges: explicit geometric contact only. Stone, lava and ice never conduct.
## Barriers on the path are counters (Interactions): arena solids always stop the bolt; a wall
## grounds E <= CP (insulators 1.5 x CP), else it shatters and the bolt continues with E - 0.5 CP.
## Limits: one discharge per bolt (no per-frame damage), max hops, fixed damage budget split across
## reached actors, one redirect per bolt.
## Fire / Lightning kit (owned with this file, docs/kits/fire.md): optional def keys keep every legacy bolt exact
## when absent - start (strike from a point, e.g. Skybreak from the sky), force_target (actor id), meet_bodies
## (loose bodies and shots on the path are met by the lightning cells: stones shatter, sand fuses, metal charges),
## forks (n arcs to the nearest conductors after the hit) - plus rail() (a line that follows the conductors it
## crosses, e.g. through a connected water jet into its holder) and relay() (a bolt banked through a charged body).

const MIN_SHARE := 5.0


## Returns {path: PackedVector3Array, arcs: Array[PackedVector3Array], hits: Array[int], blocked: bool, e: float}
## def: range, damage, balance, conduct_budget, max_hops (+ optional E: electric power, default = damage).
static func discharge(w: CombatWorld, caster: ActorState, aim: Vector3, def: Dictionary, attack_id: int, allow_redirect: bool, dmg_scale: float = 1.0) -> Dictionary:
	var start: Vector3 = def.get("start", caster.hand_point() + Vector3(0, 0.25, 0))
	var rng_m := float(def.range)
	var target := w.get_actor(int(def.get("force_target", caster.lock_target)))
	var end := aim
	if def.has("force_target") and target != null:
		end = target.chest()
	elif target != null and target.chest().distance_to(start) <= rng_m:
		var to_t := (target.chest() - start).normalized()
		var to_aim := (aim - start).normalized()
		if to_t.dot(to_aim) > 0.9:
			end = target.chest()
		else:
			target = null
	else:
		target = null
	if start.distance_to(end) > rng_m:
		end = start + (end - start).normalized() * rng_m
		target = null
	if target == null:
		# Aimed at the ground / a surface: strike where the line meets the floor.
		var dir := (end - start).normalized()
		var t := 0.0
		while t < rng_m:
			var p := start + dir * t
			if p.y <= w.arena.ground_height(p.x, p.z, p.y + 0.5) + 0.05:
				end = p
				break
			t += 0.25
	var out := {"path": PackedVector3Array([start, end]), "arcs": [], "hits": [], "blocked": false}
	var e_val := float(def.get("E", def.damage)) * dmg_scale
	var bolt := Agent.of_volume(w, caster, null, &"lightning", start, (end - start).normalized(), {"E": e_val})
	bolt.tier = int(def.get("tier", 0))
	# Barriers along the path, nearest first: arena solids and walls (and zones flagged props.barrier).
	for hb in barriers_on(w, start, end):
		var stop := start.lerp(end, float(hb.t))
		if hb.body == null:
			Interactions.resolve(w, bolt, Agent.of_env(w, "arena_wall", stop), {"site": "bolt"})
			return _blocked(w, caster, out, start, stop)
		var counter := Agent.of_body(w, hb.body)
		counter.actor = w.get_actor(hb.body.last_actor if hb.body.form == Sim.Form.WALL else hb.body.owner)
		var res := Interactions.resolve(w, bolt, counter, {"site": "bolt"})
		if bool(res.stopped) or float(res.pass_scale) <= 0.0:
			return _blocked(w, caster, out, start, stop)
		# Blasted through (shatter): the bolt continues with what is left.
		e_val *= float(res.pass_scale)
		dmg_scale *= float(res.pass_scale)
		bolt.ch.E = e_val
	if bool(def.get("meet_bodies", false)):
		var mres := meet_path_bodies(w, caster, bolt, start, end)
		e_val *= float(mres.scale)
		dmg_scale *= float(mres.scale)
		bolt.ch.E = e_val
		if bool(mres.stopped):
			out["e"] = e_val
			return _blocked(w, caster, out, start, mres.at)
	out["e"] = e_val
	var budget := float(def.conduct_budget) * dmg_scale
	var base_dmg := float(def.damage) * dmg_scale
	var seeds: Array = []
	if target != null:
		# Redirect: equipped technique + Fire element + perfect-timed guard (legacy cell aura_flame x lightning).
		if allow_redirect and target.guarding and w.perfect_guard(target) and target.has("redirect_current") \
				and w.guard_element(target) == Sim.Element.FIRE:
			var g := Agent.of_guard(w, target)
			if String(Interactions.predict(w, bolt, g).outcome) == "redirect":
				var cb := func(factor: float) -> void:
					w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": [], "blocked": false, "hits": [], "redirected": true})
					w.emit("lightning_redirect", {"actor": target.id, "from": caster.id})
					target.hits_taken[attack_id] = w.tick
					var back := w.new_attack_id()
					var saved := target.lock_target
					target.lock_target = caster.id
					discharge(w, target, caster.chest(), def, back, false, factor * dmg_scale)
					target.lock_target = saved
				var rres := Interactions.resolve(w, bolt, g, {"allow_redirect": true, "redirect_cb": cb})
				if String(rres.result) == "redirected":
					return out
		# Grounded stance (legacy cell lightning x ground): Earth guard on stone takes 40 %.
		# The stance REPLACES the guard's cell (it does not stack with the plain-guard chip): the grounded 40 %
		# lands as an unblockable hit.
		var gscale := 1.0
		var grounded := false
		# Only the plain Earth guard grounds this way; a kit guard with its own cell (Aegis plate...) answers itself.
		if target.guarding and w.guard_element(target) == Sim.Element.EARTH and target.surface == "stone" and target.grounded \
				and Agent.of_guard(w, target).ccls == &"guard_earth":
			var gres := Interactions.resolve(w, bolt, Agent.of_env(w, "ground", target.pos, target), {"site": "bolt"})
			gscale = float(gres.pass_scale)
			grounded = true
		var dmg := base_dmg * gscale
		w.hit_actor(target, {"attacker": caster.id, "attack_id": attack_id, "damage": dmg,
			"balance": float(def.balance) * gscale, "knock": (end - start).normalized() * 2.0,
			"kind": "lightning", "from": start, "agent": bolt, "power": e_val, "unblockable": grounded})
		out.hits.append(target.id)
		var tn := actor_surface_node(w, target)
		if tn != "":
			seeds.append(tn)
	else:
		var sn := surface_node_at(w, end)
		if sn != "":
			seeds.append(sn)
		var bn := body_node_at(w, end)
		if bn != "" and not seeds.has(bn):
			seeds.append(bn)
	if not seeds.is_empty():
		var graph := build_graph(w)
		var reached := bfs(graph, seeds, int(def.max_hops))
		var victims: Array[ActorState] = []
		var factors := {}
		for a in w.actors:
			if out.hits.has(a.id) or a.health <= 0.0:
				continue
			if Status.immune(a, "conduct"):
				continue
			var f := 0.0
			for n in actor_nodes(w, a):
				if reached.has(n):
					f = maxf(f, node_factor(w, n))
			if f > 0.0:
				victims.append(a)
				factors[a.id] = f
		if not victims.is_empty():
			var share := maxf(MIN_SHARE, budget / victims.size())
			var left := budget
			for v in victims:
				if left < MIN_SHARE * 0.5:
					break
				var dmg := minf(share, left)
				left -= dmg
				w.hit_actor(v, {"attacker": caster.id, "attack_id": attack_id, "damage": dmg * float(factors[v.id]), "balance": 22.0,
					"kind": "lightning", "from": end, "unblockable": true})
				out.hits.append(v.id)
				out.arcs.append(PackedVector3Array([node_point(w, _reached_node(w, v, reached), end), v.chest()]))
		for n in reached.keys():
			if not seeds.has(n):
				out.arcs.append(PackedVector3Array([node_point(w, seeds[0], end), node_point(w, n, end)]))
		w.emit("conduct", {"actor": caster.id, "nodes": reached.keys(), "victims": out.hits})
	var nf := int(def.get("forks", 0))
	if nf > 0:
		_forks(w, caster, out, end, nf, e_val, attack_id, def)
	w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": out.arcs, "blocked": false, "hits": out.hits, "e": e_val})
	return out


## Loose bodies and shots near the bolt's path meet it (lightning cells): a stone it out-powers shatters, sand fuses to
## glass and takes half, metal / water are charged. Returns {scale (of E left), stopped, at}.
static func meet_path_bodies(w: CombatWorld, caster: ActorState, bolt: Agent, p0: Vector3, p1: Vector3) -> Dictionary:
	var out := {"scale": 1.0, "stopped": false, "at": p1}
	var seg := p1 - p0
	var seg_len := seg.length()
	if seg_len < 0.05:
		return out
	var dir := seg / seg_len
	var hits: Array = []
	for b in w.bodies:
		if not b.alive or b.static_body or b.form == Sim.Form.WALL or b.form == Sim.Form.ZONE or b.form == Sim.Form.POOL \
				or b.form == Sim.Form.PUDDLE or b.controller == caster.id or b.form == Sim.Form.CLOUD:
			continue
		var t := (b.pos - p0).dot(dir)
		if t < 0.3 or t > seg_len:
			continue
		if (p0 + dir * t).distance_to(b.pos) <= b.radius + 0.45:
			hits.append([t, b])
	hits.sort_custom(func(x, y): return float(x[0]) < float(y[0]) or (float(x[0]) == float(y[0]) and x[1].id < y[1].id))
	for h in hits:
		var b: MatBody = h[1]
		if not b.alive:
			continue
		var res := Interactions.resolve(w, Agent.of_body(w, b, caster), bolt, {"site": "bolt_path"}, Interactions.PASS_RULE)
		var ps := float(res.pass_scale)
		if ps < 1.0:
			out.scale = float(out.scale) * ps
			bolt.ch.E = float(bolt.ch.E) * ps
		if ps <= 0.0:
			out.stopped = true
			out.at = p0 + dir * float(h[0])
			return out
	return out


## Arcs from the strike point to the n nearest conductive bodies within 6 m (charging them; their holders are shocked
## for a share of E). Storm Bolt forks.
static func _forks(w: CombatWorld, caster: ActorState, out: Dictionary, at: Vector3, n: int, e_val: float, attack_id: int, def: Dictionary) -> void:
	var cands: Array = []
	for b in w.bodies:
		if not b.alive or b.form == Sim.Form.POOL or b.form == Sim.Form.ZONE:
			continue
		if not (Materials.conducts(b) or b.form == Sim.Form.PUDDLE and b.phase == Sim.Phase.LIQUID):
			continue
		var d := b.pos.distance_to(at)
		if d <= 6.0 and d > 0.2:
			cands.append([d, b])
	cands.sort_custom(func(x, y): return float(x[0]) < float(y[0]) or (float(x[0]) == float(y[0]) and x[1].id < y[1].id))
	for k in mini(n, cands.size()):
		var b: MatBody = cands[k][1]
		out.arcs.append(PackedVector3Array([at, b.pos]))
		b.charge = maxf(b.charge, e_val * 0.25)
		if b.controller >= 0:
			var h := w.get_actor(b.controller)
			if h != null and h.id != caster.id and not out.hits.has(h.id) and not Status.immune(h, "conduct"):
				w.hit_actor(h, {"attacker": caster.id, "attack_id": attack_id, "damage": float(def.damage) * 0.35, "balance": 20.0,
					"kind": "lightning", "from": b.pos, "unblockable": true})
				out.hits.append(h.id)
	w.emit("fork", {"actor": caster.id, "at": at, "n": mini(n, cands.size())})


## A straight line that follows the conductors it crosses (MOVESET §7.11 Rail Arc): barriers answer it like a bolt; a
## fighter on the line is hit; every conductive body the line touches (a water jet, a stream, metal, a charged body)
## seeds the conduction graph, so a jet still held by its caster carries the arc into them.
## def: range, damage, balance, E, conduct_budget, max_hops (+ tier). Returns the discharge result.
static func rail(w: CombatWorld, caster: ActorState, dir: Vector3, def: Dictionary, attack_id: int) -> Dictionary:
	var start := caster.hand_point() + Vector3(0, 0.25, 0)
	var end := start + dir.normalized() * float(def.range)
	var out := {"path": PackedVector3Array([start, end]), "arcs": [], "hits": [], "blocked": false}
	var e_val := float(def.get("E", def.damage))
	var bolt := Agent.of_volume(w, caster, null, &"lightning", start, dir, {"E": e_val})
	bolt.tier = int(def.get("tier", 0))
	var stop_t := 1.0
	for hb in barriers_on(w, start, end):
		var stop := start.lerp(end, float(hb.t))
		if hb.body == null:
			Interactions.resolve(w, bolt, Agent.of_env(w, "arena_wall", stop), {"site": "bolt"})
			stop_t = float(hb.t)
			out.blocked = true
			break
		var counter := Agent.of_body(w, hb.body)
		counter.actor = w.get_actor(hb.body.last_actor if hb.body.form == Sim.Form.WALL else hb.body.owner)
		var res := Interactions.resolve(w, bolt, counter, {"site": "bolt"})
		if bool(res.stopped) or float(res.pass_scale) <= 0.0:
			stop_t = float(hb.t)
			out.blocked = true
			break
		e_val *= float(res.pass_scale)
		bolt.ch.E = e_val
	end = start.lerp(end, stop_t)
	out.path = PackedVector3Array([start, end])
	var scale := e_val / maxf(float(def.get("E", def.damage)), 0.01)
	var seg := end - start
	var seg_len := maxf(seg.length(), 0.01)
	var d := seg / seg_len
	# The first fighter on the line.
	var first: ActorState = null
	var ft := INF
	for t in w.actors:
		if t == caster or t.team == caster.team or t.health <= 0.0:
			continue
		var tt := clampf((t.chest() - start).dot(d), 0.0, seg_len)
		if (start + d * tt).distance_to(t.chest()) <= Sim.ACTOR_RADIUS + 0.45 and tt < ft:
			ft = tt
			first = t
	# Conductors the line touches (bodies and zones), nearest first, before the first fighter.
	var seeds: Array = []
	for b in w.bodies:
		if not b.alive or not (_is_body_node(b) or b.form == Sim.Form.PUDDLE and b.phase == Sim.Phase.LIQUID):
			continue
		var tb := clampf((b.pos - start).dot(d), 0.0, seg_len)
		var r := b.zone_radius if b.form == Sim.Form.ZONE else b.radius
		if tb > ft or (start + d * tb).distance_to(b.pos) > r + 0.5:
			continue
		var key := ("puddle:%d" % b.id) if b.form == Sim.Form.PUDDLE else ("body:%d" % b.id)
		if not seeds.has(key):
			seeds.append(key)
			if b.form != Sim.Form.PUDDLE:
				Interactions.resolve(w, Agent.of_body(w, b, caster), bolt, {"site": "rail"}, Interactions.PASS_RULE)
	if first != null:
		w.hit_actor(first, {"attacker": caster.id, "attack_id": attack_id, "damage": float(def.damage) * scale,
			"balance": float(def.balance) * scale, "knock": d * 2.0, "kind": "lightning", "from": start, "agent": bolt, "power": e_val})
		out.hits.append(first.id)
		end = first.chest()
		out.path = PackedVector3Array([start, end])
		var tn := actor_surface_node(w, first)
		if tn != "" and not seeds.has(tn):
			seeds.append(tn)
	var sn := surface_node_at(w, end)
	if sn != "" and not seeds.has(sn):
		seeds.append(sn)
	_conduct_from(w, caster, out, seeds, end, float(def.conduct_budget) * scale, int(def.max_hops), attack_id)
	out["e"] = e_val
	w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": out.arcs, "blocked": out.blocked, "hits": out.hits, "e": e_val, "rail": true})
	return out


## A bolt banked through relay nodes (charged bodies, puddles, rods, embedded lances): caster -> node 1 -> ... -> the
## nearest fighter within `relay_range` of the last node that the node can see. Each leg meets barriers like a bolt.
## def: damage, balance, E, conduct_budget, max_hops, relay_range (8). Returns {path, hits, blocked, e}.
static func relay(w: CombatWorld, caster: ActorState, nodes: Array, def: Dictionary, attack_id: int) -> Dictionary:
	var pts := PackedVector3Array([caster.hand_point() + Vector3(0, 0.25, 0)])
	var out := {"path": pts, "arcs": [], "hits": [], "blocked": false}
	var e_val := float(def.get("E", def.damage))
	var bolt := Agent.of_volume(w, caster, null, &"lightning", pts[0], Vector3.ZERO, {"E": e_val})
	bolt.tier = int(def.get("tier", 0))
	var last: Vector3 = pts[0]
	var legs: Array = []
	for nb in nodes:
		var b: MatBody = nb
		if b == null or not b.alive:
			continue
		legs.append(b.pos + Vector3(0, 0.2, 0))
	# The target: the nearest enemy within relay_range of the last node, in its line of sight.
	var anchor: Vector3 = legs[legs.size() - 1] if not legs.is_empty() else last
	var tgt: ActorState = null
	var best := INF
	for t in w.actors:
		if t == caster or t.team == caster.team or t.health <= 0.0:
			continue
		var dd := t.chest().distance_to(anchor)
		if dd <= float(def.get("relay_range", 8.0)) and dd < best and w.arena.has_los(anchor, t.chest()):
			best = dd
			tgt = t
	if tgt != null:
		legs.append(tgt.chest())
	for p in legs:
		var hit_stop := false
		for hb in barriers_on(w, last, p):
			var stop := last.lerp(p, float(hb.t))
			if hb.body == null:
				Interactions.resolve(w, bolt, Agent.of_env(w, "arena_wall", stop), {"site": "bolt"})
				pts.append(stop)
				hit_stop = true
				break
			var counter := Agent.of_body(w, hb.body)
			counter.actor = w.get_actor(hb.body.last_actor if hb.body.form == Sim.Form.WALL else hb.body.owner)
			var res := Interactions.resolve(w, bolt, counter, {"site": "bolt"})
			if bool(res.stopped) or float(res.pass_scale) <= 0.0:
				pts.append(stop)
				hit_stop = true
				break
			e_val *= float(res.pass_scale)
			bolt.ch.E = e_val
		if hit_stop:
			out.blocked = true
			break
		pts.append(p)
		last = p
	out.path = pts
	out["e"] = e_val
	var scale := e_val / maxf(float(def.get("E", def.damage)), 0.01)
	if not out.blocked and tgt != null:
		w.hit_actor(tgt, {"attacker": caster.id, "attack_id": attack_id, "damage": float(def.damage) * scale,
			"balance": float(def.balance) * scale, "knock": (tgt.chest() - anchor).normalized() * 2.0, "kind": "lightning",
			"from": anchor, "agent": bolt, "power": e_val, "relay": true})
		out.hits.append(tgt.id)
		var tn := actor_surface_node(w, tgt)
		if tn != "":
			_conduct_from(w, caster, out, [tn], tgt.chest(), float(def.conduct_budget) * scale, int(def.max_hops), attack_id)
	w.emit("lightning", {"actor": caster.id, "path": pts, "arcs": out.arcs, "blocked": out.blocked, "hits": out.hits, "e": e_val, "relay": true})
	return out


## Spreads a strike through the conduction graph from seed nodes (shared by rail / relay): the bounded budget is split
## between the fighters on reached nodes (not the ones already hit, not the immune).
static func _conduct_from(w: CombatWorld, caster: ActorState, out: Dictionary, seeds: Array, at: Vector3, budget: float, max_hops: int, attack_id: int) -> void:
	if seeds.is_empty():
		return
	var graph := build_graph(w)
	var reached := bfs(graph, seeds, max_hops)
	var victims: Array[ActorState] = []
	var factors := {}
	for a in w.actors:
		if out.hits.has(a.id) or a.health <= 0.0 or Status.immune(a, "conduct"):
			continue
		var f := 0.0
		for n in actor_nodes(w, a):
			if reached.has(n):
				f = maxf(f, node_factor(w, n))
		if f > 0.0:
			victims.append(a)
			factors[a.id] = f
	if not victims.is_empty():
		var share := maxf(MIN_SHARE, budget / victims.size())
		var left := budget
		for v in victims:
			if left < MIN_SHARE * 0.5:
				break
			var dmg := minf(share, left)
			left -= dmg
			w.hit_actor(v, {"attacker": caster.id, "attack_id": attack_id, "damage": dmg * float(factors[v.id]), "balance": 22.0,
				"kind": "lightning", "from": at, "unblockable": true})
			out.hits.append(v.id)
			out.arcs.append(PackedVector3Array([node_point(w, _reached_node(w, v, reached), at), v.chest()]))
	for n in reached.keys():
		if not seeds.has(n):
			out.arcs.append(PackedVector3Array([node_point(w, seeds[0], at), node_point(w, n, at)]))
	w.emit("conduct", {"actor": caster.id, "nodes": reached.keys(), "victims": out.hits})


static func _blocked(w: CombatWorld, caster: ActorState, out: Dictionary, start: Vector3, stop: Vector3) -> Dictionary:
	out.path = PackedVector3Array([start, stop])
	out.blocked = true
	w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": [], "blocked": true, "hits": []})
	return out


## Barriers crossing the segment, nearest first: [{t, body (null = arena solid)}]. Walls count when
## risen > 0.5 (legacy); zones with props.barrier as vertical cylinders.
static func barriers_on(w: CombatWorld, p0: Vector3, p1: Vector3) -> Array:
	var out: Array = []
	var ta := w.arena.segment_hit(p0, p1)
	if ta >= 0.0:
		out.append({"t": ta, "body": null})
	for b in w.bodies:
		if not b.alive:
			continue
		var t := -1.0
		if b.form == Sim.Form.WALL and b.wall_rise > 0.5:
			t = w.wall_segment_t(p0, p1, b)
		elif b.form == Sim.Form.ZONE and b.props.get("barrier", false):
			t = _cyl_t(p0, p1, b.pos, b.zone_radius, float(b.props.get("height", 2.5)))
		if t >= 0.0:
			out.append({"t": t, "body": b})
	out.sort_custom(func(x, y):
		if not is_equal_approx(float(x.t), float(y.t)):
			return float(x.t) < float(y.t)
		return (x.body.id if x.body != null else -1) < (y.body.id if y.body != null else -1))
	return out


## First entry t (0..1) of the segment into a vertical cylinder, or -1. A segment starting inside is ignored.
static func _cyl_t(p0: Vector3, p1: Vector3, c: Vector3, r: float, h: float) -> float:
	var d := Vector2(p1.x - p0.x, p1.z - p0.z)
	var f := Vector2(p0.x - c.x, p0.z - c.z)
	if f.length() <= r:
		return -1.0
	var a := d.dot(d)
	if a < 1e-9:
		return -1.0
	var b := 2.0 * f.dot(d)
	var cc := f.dot(f) - r * r
	var disc := b * b - 4.0 * a * cc
	if disc < 0.0:
		return -1.0
	var t := (-b - sqrt(disc)) / (2.0 * a)
	if t < 0.0 or t > 1.0:
		return -1.0
	var y := lerpf(p0.y, p1.y, t)
	if y < c.y - 0.2 or y > c.y + h:
		return -1.0
	return t


## Surface node an actor stands on/in: "pool", "metal", "puddle:<id>", or "".
static func actor_surface_node(w: CombatWorld, a: ActorState) -> String:
	if not a.grounded:
		return ""
	if a.in_water:
		return "pool"
	if w.arena.on_metal(a.pos.x, a.pos.z) and absf(a.pos.y - w.arena.metal_top) < 0.15:
		return "metal"
	var pd := w.puddle_at(a.pos)
	if pd != null and pd.phase == Sim.Phase.LIQUID:
		return "puddle:%d" % pd.id
	return ""


## Every node an actor touches: its surface, a conductive body it holds, conductive zones it is inside.
static func actor_nodes(w: CombatWorld, a: ActorState) -> Array[String]:
	var out: Array[String] = []
	var sn := actor_surface_node(w, a)
	if sn != "":
		out.append(sn)
	for b in w.bodies:
		if not b.alive or not _is_body_node(b):
			continue
		if b.controller == a.id:
			out.append("body:%d" % b.id)
		elif b.form == Sim.Form.ZONE and w._in_zone(b, a.pos + Vector3(0, 0.9, 0), Sim.ACTOR_RADIUS):
			if a.grounded or b.tag == &"fog":
				out.append("body:%d" % b.id)
	return out


static func _reached_node(w: CombatWorld, a: ActorState, reached: Dictionary) -> String:
	for n in actor_nodes(w, a):
		if reached.has(n):
			return n
	return actor_surface_node(w, a)


## Conduction factor of a node (fog 0.6, else 1).
static func node_factor(w: CombatWorld, n: String) -> float:
	if n.begins_with("body:"):
		var b := w.get_body(int(n.substr(5)))
		if b != null:
			return Materials.conduction_factor(b)
	return 1.0


static func surface_node_at(w: CombatWorld, p: Vector3) -> String:
	if w.arena.in_pool(p.x, p.z) and p.y < w.arena.pool_level + 0.3:
		return "pool"
	if w.arena.on_metal(p.x, p.z) and p.y < w.arena.metal_top + 0.3:
		return "metal"
	for b in w.bodies:
		if b.alive and b.form == Sim.Form.PUDDLE and b.phase == Sim.Phase.LIQUID:
			if Vector2(p.x - b.pos.x, p.z - b.pos.z).length() < b.radius + 0.2:
				return "puddle:%d" % b.id
	return ""


## A conductive body (or zone) at a strike point.
static func body_node_at(w: CombatWorld, p: Vector3) -> String:
	for b in w.bodies:
		if not b.alive or not _is_body_node(b):
			continue
		if b.form == Sim.Form.ZONE:
			if w._in_zone(b, p, 0.2):
				return "body:%d" % b.id
		elif b.pos.distance_to(p) < b.radius + 0.4:
			return "body:%d" % b.id
	return ""


static func node_point(w: CombatWorld, n: String, fallback: Vector3) -> Vector3:
	if n == "pool":
		return Vector3((w.arena.pool_min.x + w.arena.pool_max.x) * 0.5, w.arena.pool_level, (w.arena.pool_min.y + w.arena.pool_max.y) * 0.5)
	if n == "metal":
		return Vector3((w.arena.metal_min.x + w.arena.metal_max.x) * 0.5, w.arena.metal_top, (w.arena.metal_min.y + w.arena.metal_max.y) * 0.5)
	if n.begins_with("puddle:"):
		var b := w.get_body(int(n.substr(7)))
		if b != null:
			return b.pos
	if n.begins_with("body:"):
		var b2 := w.get_body(int(n.substr(5)))
		if b2 != null:
			return b2.pos
	return fallback


static func _circle_rect(c: Vector2, r: float, mn: Vector2, mx: Vector2) -> bool:
	var q := Vector2(clampf(c.x, mn.x, mx.x), clampf(c.y, mn.y, mx.y))
	return q.distance_to(c) <= r


## Conductive bodies that are graph nodes (puddles and the pool have their own legacy nodes).
static func _is_body_node(b: MatBody) -> bool:
	if b.form == Sim.Form.PUDDLE or b.form == Sim.Form.POOL:
		return false
	return Materials.conducts(b)


## Adjacency between conductive surfaces (explicit contact only).
static func build_graph(w: CombatWorld) -> Dictionary:
	var g := {"pool": [], "metal": []}
	var puddles: Array[MatBody] = []
	for b in w.bodies:
		if b.alive and b.form == Sim.Form.PUDDLE and b.phase == Sim.Phase.LIQUID:
			puddles.append(b)
			g["puddle:%d" % b.id] = []
	for b in puddles:
		var key := "puddle:%d" % b.id
		var c := Vector2(b.pos.x, b.pos.z)
		if _circle_rect(c, b.radius, w.arena.pool_min, w.arena.pool_max):
			g[key].append("pool")
			g["pool"].append(key)
		if _circle_rect(c, b.radius, w.arena.metal_min, w.arena.metal_max):
			g[key].append("metal")
			g["metal"].append(key)
		for o in puddles:
			if o.id > b.id and c.distance_to(Vector2(o.pos.x, o.pos.z)) <= b.radius + o.radius:
				var ok := "puddle:%d" % o.id
				g[key].append(ok)
				g[ok].append(key)
	# Pool and metal plate only connect if they actually touch (they don't in the lab layout).
	var gap_x := maxf(w.arena.metal_min.x - w.arena.pool_max.x, w.arena.pool_min.x - w.arena.metal_max.x)
	var gap_z := maxf(w.arena.metal_min.y - w.arena.pool_max.y, w.arena.pool_min.y - w.arena.metal_max.y)
	if maxf(gap_x, gap_z) <= 0.05:
		g["pool"].append("metal")
		g["metal"].append("pool")
	# Moveset nodes: conductive bodies and zones, connected by contact.
	var nodes: Array[MatBody] = []
	for b in w.bodies:
		if b.alive and _is_body_node(b):
			nodes.append(b)
			g["body:%d" % b.id] = []
	for b in nodes:
		var key := "body:%d" % b.id
		var r := b.zone_radius if b.form == Sim.Form.ZONE else b.radius
		var c2 := Vector2(b.pos.x, b.pos.z)
		var low := b.pos.y - r < w.arena.pool_level + 0.3 or b.form == Sim.Form.ZONE
		if low and _circle_rect(c2, r, w.arena.pool_min, w.arena.pool_max) and b.pos.y < w.arena.pool_level + r + 0.3:
			_link(g, key, "pool")
		if low and _circle_rect(c2, r, w.arena.metal_min, w.arena.metal_max) and b.pos.y < w.arena.metal_top + r + 0.3:
			_link(g, key, "metal")
		for p in puddles:
			if c2.distance_to(Vector2(p.pos.x, p.pos.z)) <= r + p.radius and absf(b.pos.y - p.pos.y) < r + 0.35:
				_link(g, key, "puddle:%d" % p.id)
		for o in nodes:
			if o.id <= b.id:
				continue
			var ro := o.zone_radius if o.form == Sim.Form.ZONE else o.radius
			var touch := false
			if b.form == Sim.Form.ZONE and o.form != Sim.Form.ZONE:
				touch = w._in_zone(b, o.pos, o.radius)
			elif o.form == Sim.Form.ZONE and b.form != Sim.Form.ZONE:
				touch = w._in_zone(o, b.pos, b.radius)
			else:
				touch = b.pos.distance_to(o.pos) <= r + ro + 0.1
			if touch:
				_link(g, key, "body:%d" % o.id)
	return g


static func _link(g: Dictionary, a: String, b: String) -> void:
	if not g.has(a):
		g[a] = []
	if not g.has(b):
		g[b] = []
	g[a].append(b)
	g[b].append(a)


static func bfs(g: Dictionary, seeds: Array, max_hops: int) -> Dictionary:
	var seen := {}
	var frontier: Array = []
	for s in seeds:
		if g.has(s):
			seen[s] = 0
			frontier.append(s)
	while not frontier.is_empty():
		var n: String = frontier.pop_front()
		var depth: int = seen[n]
		if depth >= max_hops:
			continue
		for m in g[n]:
			if not seen.has(m):
				seen[m] = depth + 1
				frontier.append(m)
	return seen
