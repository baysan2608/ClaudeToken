class_name Conduction
extends RefCounted
## Lightning as a bounded gameplay conduction graph, built once per discharge.
## Nodes: the pool, the metal plate, liquid puddles, held/flying liquid water, actors.
## Edges: explicit geometric contact only. Stone, lava and ice never conduct.
## Limits: one discharge per bolt (no per-frame damage), max hops, fixed damage
## budget split across reached actors, one redirect per bolt.

const MIN_SHARE := 5.0


## Returns {path: PackedVector3Array, arcs: Array[PackedVector3Array], hits: Array[int], blocked: bool}
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
	# Barriers: arena solids and earth walls stop the bolt.
	var hit_t := w.arena.segment_hit(start, end)
	var wall_t := w.wall_hit(start, end)
	if wall_t >= 0.0 and (hit_t < 0.0 or wall_t < hit_t):
		hit_t = wall_t
	if hit_t >= 0.0:
		var stop := start.lerp(end, hit_t)
		out.path = PackedVector3Array([start, stop])
		out.blocked = true
		w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": [], "blocked": true, "hits": []})
		return out
	var budget := float(def.conduct_budget) * dmg_scale
	var base_dmg := float(def.damage) * dmg_scale
	var seeds: Array = []
	if target != null:
		# Redirect: equipped technique + Fire element + perfect-timed guard.
		if allow_redirect and target.has("redirect_current") and target.element == Sim.Element.FIRE and w.perfect_guard(target):
			w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": [], "blocked": false, "hits": [], "redirected": true})
			w.emit("lightning_redirect", {"actor": target.id, "from": caster.id})
			target.hits_taken[attack_id] = w.tick
			var back := w.new_attack_id()
			var saved := target.lock_target
			target.lock_target = caster.id
			discharge(w, target, caster.chest(), def, back, false, 0.8)
			target.lock_target = saved
			return out
		var grounded := target.guarding and target.element == Sim.Element.EARTH and target.surface == "stone" and target.grounded
		var dmg := base_dmg * (0.4 if grounded else 1.0)
		if grounded:
			w.emit("grounded", {"actor": target.id})
		w.hit_actor(target, {"attacker": caster.id, "attack_id": attack_id, "damage": dmg,
			"balance": float(def.balance) * (0.4 if grounded else 1.0), "knock": (end - start).normalized() * 2.0,
			"kind": "lightning", "from": start})
		out.hits.append(target.id)
		var tn := actor_surface_node(w, target)
		if tn != "":
			seeds.append(tn)
	else:
		var sn := surface_node_at(w, end)
		if sn != "":
			seeds.append(sn)
	if not seeds.is_empty():
		var graph := build_graph(w)
		var reached := bfs(graph, seeds, int(def.max_hops))
		var victims: Array[ActorState] = []
		for a in w.actors:
			if out.hits.has(a.id) or a.health <= 0.0:
				continue
			var n := actor_surface_node(w, a)
			if n != "" and reached.has(n):
				victims.append(a)
		if not victims.is_empty():
			var share := maxf(MIN_SHARE, budget / victims.size())
			var left := budget
			for v in victims:
				if left < MIN_SHARE * 0.5:
					break
				var dmg := minf(share, left)
				left -= dmg
				w.hit_actor(v, {"attacker": caster.id, "attack_id": attack_id, "damage": dmg, "balance": 22.0,
					"kind": "lightning", "from": end, "unblockable": true})
				out.hits.append(v.id)
				out.arcs.append(PackedVector3Array([node_point(w, actor_surface_node(w, v), end), v.chest()]))
		for n in reached.keys():
			if not seeds.has(n):
				out.arcs.append(PackedVector3Array([node_point(w, seeds[0], end), node_point(w, n, end)]))
		w.emit("conduct", {"actor": caster.id, "nodes": reached.keys(), "victims": out.hits})
	w.emit("lightning", {"actor": caster.id, "path": out.path, "arcs": out.arcs, "blocked": false, "hits": out.hits})
	return out


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


static func node_point(w: CombatWorld, n: String, fallback: Vector3) -> Vector3:
	if n == "pool":
		return Vector3((w.arena.pool_min.x + w.arena.pool_max.x) * 0.5, w.arena.pool_level, (w.arena.pool_min.y + w.arena.pool_max.y) * 0.5)
	if n == "metal":
		return Vector3((w.arena.metal_min.x + w.arena.metal_max.x) * 0.5, w.arena.metal_top, (w.arena.metal_min.y + w.arena.metal_max.y) * 0.5)
	if n.begins_with("puddle:"):
		var b := w.get_body(int(n.substr(7)))
		if b != null:
			return b.pos
	return fallback


static func _circle_rect(c: Vector2, r: float, mn: Vector2, mx: Vector2) -> bool:
	var q := Vector2(clampf(c.x, mn.x, mx.x), clampf(c.y, mn.y, mx.y))
	return q.distance_to(c) <= r


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
	return g


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
