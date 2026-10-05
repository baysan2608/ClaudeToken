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

const MIN_SHARE := 5.0


## Returns {path: PackedVector3Array, arcs: Array[PackedVector3Array], hits: Array[int], blocked: bool, e: float}
## def: range, damage, balance, conduct_budget, max_hops (+ optional E: electric power, default = damage).
static func discharge(w: CombatWorld, caster: ActorState, aim: Vector3, def: Dictionary, attack_id: int, allow_redirect: bool, dmg_scale: float = 1.0) -> Dictionary:
	var start := caster.hand_point() + Vector3(0, 0.25, 0)
	var rng_m := float(def.range)
	var target := w.get_actor(caster.lock_target)
	var end := aim
	if target != null and target.chest().distance_to(start) <= rng_m:
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
		var gscale := 1.0
		if target.guarding and w.guard_element(target) == Sim.Element.EARTH and target.surface == "stone" and target.grounded:
			var gres := Interactions.resolve(w, bolt, Agent.of_env(w, "ground", target.pos, target), {"site": "bolt"})
			gscale = float(gres.pass_scale)
		var dmg := base_dmg * gscale
		w.hit_actor(target, {"attacker": caster.id, "attack_id": attack_id, "damage": dmg,
			"balance": float(def.balance) * gscale, "knock": (end - start).normalized() * 2.0,
			"kind": "lightning", "from": start, "agent": bolt, "power": e_val})
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
	w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": out.arcs, "blocked": false, "hits": out.hits})
	return out


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
