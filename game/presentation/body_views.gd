class_name BodyViews
extends Node3D
## Keeps one pooled view per live MatBody and drives it from the body's state each
## frame. A view is chosen by representation (form) but the logical body never
## changes: a stone that heats, melts, pours and cools keeps its id while its
## view crossfades stone -> molten blob -> lava wave -> crusted ridge.

var pool: VfxPool
var world: CombatWorld
## body id -> {kind: String, node: Node3D, prev: Vector3, curr: Vector3, trail: PackedVector3Array}, plus
## the ridge path/state last built (sig, st: "wave") and the radius the wet mark was placed with (r: "puddle").
var _views := {}
var _tmp_w := PackedFloat32Array()


func _init() -> void:
	pool = VfxPool.new()
	pool.name = "VfxPool"


func _ready() -> void:
	add_child(pool)


func bind(w: CombatWorld) -> void:
	clear()
	world = w


func clear() -> void:
	for id in _views.keys():
		_release(id)
	_views.clear()


func push_state() -> void:
	## Once per sim tick: record positions for interpolation, create/remove views.
	var alive := {}
	for b in world.bodies:
		if not b.alive or b.form == Sim.Form.POOL:
			continue
		alive[b.id] = true
		var kind := _kind_for(b)
		var v: Dictionary = _views.get(b.id, {})
		if v.is_empty() or v.kind != kind:
			var keep_prev: Vector3 = v.get("curr", b.pos)
			if not v.is_empty():
				_release(b.id)
			v = _make(b, kind)
			v.prev = keep_prev
			v.curr = b.pos
			_views[b.id] = v
		v.prev = v.curr
		v.curr = b.pos
		if kind == "ribbon":
			var tr: PackedVector3Array = v.trail
			tr.append(b.pos)
			if tr.size() > 6:
				tr.remove_at(0)
			v.trail = tr
	for id in _views.keys():
		if not alive.has(id):
			_release(id)
			_views.erase(id)


func render(alpha: float) -> void:
	for id in _views:
		var b := world.get_body(id)
		if b == null:
			continue
		var v: Dictionary = _views[id]
		var p: Vector3 = (v.prev as Vector3).lerp(v.curr, alpha)
		var n: Node3D = v.node
		if n == null:
			continue
		match String(v.kind):
			"stone":
				n.global_position = p
				var heat := Thermal.heat01(b)
				n.call("set_thermal", heat, b.liquid)
				n.call("set_crust", _crust(b))
				if b.controller >= 0:
					n.rotate_y(0.02)
			"wave":
				_update_wave(b, n, p, v)
			"wall":
				n.global_position = b.pos
				n.rotation.y = b.wall_yaw
				n.call("set_rise", b.wall_rise)
				n.call("set_damage", b.wall_damage)
			"blob":
				n.global_position = p
				n.call("set_state", 1.0 - b.liquid)
			"ribbon":
				var tr: PackedVector3Array = v.trail
				var pts := PackedVector3Array()
				for q in tr:
					pts.append(q)
				pts.append(p)
				var rad := PackedFloat32Array()
				rad.resize(pts.size())
				for k in pts.size():
					rad[k] = b.radius * lerpf(0.35, 1.0, float(k) / maxf(1.0, pts.size() - 1))
				n.call("set_points", pts, rad)
				n.call("set_state", 1.0 - b.liquid)
			"puddle":
				# Puddles grow as water merges in and shrink as they are drawn or boiled: keep the wet
				# patch on the live radius by scaling the placed quad (re-placing restarts its fade-in).
				var k := maxf(b.radius, 0.05) / maxf(float(v.get("r", b.radius)), 0.05)
				if absf(n.scale.x - k) > 0.01:
					n.scale = Vector3(k, 1.0, k)


func _crust(b: MatBody) -> float:
	if not b.is_stone():
		return 0.0
	if b.liquid > 0.0:
		return clampf(1.0 - b.liquid, 0.0, 1.0) * 0.8
	return 1.0 if b.temp > 200.0 else 0.0


func _kind_for(b: MatBody) -> String:
	if b.is_stone():
		match b.form:
			Sim.Form.WALL:
				return "wall"
			Sim.Form.WAVE:
				return "wave"
			_:
				# A settled/cooled wave keeps its ridge shape until someone lifts it.
				if b.wave_path.size() >= 2 and b.controller < 0:
					return "wave"
				return "stone"
	if b.is_water():
		match b.form:
			Sim.Form.PUDDLE:
				return "puddle"
			Sim.Form.STREAM, Sim.Form.SHARD:
				return "ribbon" if b.controller < 0 else "blob"
			_:
				return "blob"
	return "none"


func _make(b: MatBody, kind: String) -> Dictionary:
	var v := {"kind": kind, "node": null, "prev": b.pos, "curr": b.pos, "trail": PackedVector3Array()}
	var n: Node3D = null
	match kind:
		"stone":
			n = acquire("stone")
			if n:
				n.call("setup", b.id * 7919 + 13, b.radius)
		"wave":
			n = acquire("lava_wave")
		"wall":
			n = acquire("earth_wall")
			if n and n.has_method("setup"):
				n.call("setup", b.id, b.wall_half.x * 2.0, b.wall_half.y * 2.0, b.wall_half.z * 2.0)
		"blob":
			n = acquire("water_blob")
			if n:
				n.call("setup", b.radius)
		"ribbon":
			n = acquire("water_ribbon")
		"puddle":
			n = acquire("scorch_decal")
			if n and n.has_method("place"):
				n.call("place", b.pos, b.radius, "wet", 9999.0)
				v.r = b.radius
	if n != null:
		n.visible = true
	v.node = n
	return v


## Acquire a pooled node the caller keeps across frames. At its cap VfxPool recycles the oldest
## active node, which would leave two owners driving (and later releasing) one node, so a kept
## kind grows its cap instead: the sim already bounds how many bodies can be alive.
func acquire(key: String) -> Node3D:
	var st: Array = pool.get_stats().get(key, [])
	if st.size() == 3 and int(st[1]) == 0 and int(st[0]) >= int(st[2]):
		pool.set_cap(key, int(st[0]) + 1)
	return pool.get_fx(key) as Node3D


func _release(id: int) -> void:
	var v: Dictionary = _views.get(id, {})
	if v.is_empty() or v.node == null:
		return
	pool.release(v.node)
	v.node = null


func _update_wave(b: MatBody, n: Node3D, p: Vector3, v: Dictionary) -> void:
	# A moving wave's front is interpolated every frame; a settled ridge's path only changes when
	# the sim changes it, so its mesh is rebuilt only then (set_path re-uploads the whole strip).
	var path := b.wave_path
	var sig: Array = []
	if b.form != Sim.Form.WAVE and path.size() >= 2:
		sig = [path.size(), path[0], path[path.size() - 1], b.wave_width]
	if sig.is_empty() or sig != v.get("sig", []):
		v.sig = sig
		var pts := PackedVector3Array()
		for q in path:
			pts.append(q)
		if b.form == Sim.Form.WAVE:
			if pts.is_empty() or pts[pts.size() - 1].distance_to(p) > 0.05:
				pts.append(p)
		if pts.size() < 2:
			pts.insert(0, p - b.wave_dir * 0.4)
		_tmp_w.resize(pts.size())
		for k in pts.size():
			var t := float(k) / maxf(1.0, pts.size() - 1)
			_tmp_w[k] = b.wave_width * lerpf(0.55, 1.0, t)
		n.global_position = Vector3.ZERO
		n.call("set_path", pts, _tmp_w)
	var crust := clampf(1.0 - b.liquid / 0.85, 0.0, 1.0)
	if b.liquid <= 0.0:
		crust = 1.0
	var st := Vector3(b.liquid, crust, b.vel.length())
	if st != v.get("st", -Vector3.ONE):
		v.st = st
		n.call("set_state", b.liquid, crust, st.z)


func view_of(id: int) -> Node3D:
	var v: Dictionary = _views.get(id, {})
	return v.get("node", null)
