class_name ArenaMap
extends RefCounted
## Analytic description of the combat laboratory. The same data builds the
## render/collision geometry (presentation) and answers simulation queries
## (ground height, wall pushes, line of sight), so tests need no physics engine.

var half_size := 16.0
var solids: Array[Dictionary] = []   # {min: Vector3, max: Vector3, kind: String, surface: String, name: String}
var pool_min := Vector2(7.0, -5.0)   # x, z
var pool_max := Vector2(13.0, 3.0)
var pool_floor := -0.3
var pool_level := -0.05
var metal_min := Vector2(-12.0, -4.0)
var metal_max := Vector2(-6.0, 2.0)
var metal_top := 0.02
var player_spawn := Vector3(0, 0, 7)
var opponent_spawn := Vector3(0, 0, -7)


static func make_lab() -> ArenaMap:
	var a := ArenaMap.new()
	var h := a.half_size
	# Boundary walls
	a.add_box(Vector3(-h - 1, 0, -h - 1), Vector3(h + 1, 3.5, -h), "wall", "stone", "north_wall")
	a.add_box(Vector3(-h - 1, 0, h), Vector3(h + 1, 3.5, h + 1), "wall", "stone", "south_wall")
	a.add_box(Vector3(-h - 1, 0, -h), Vector3(-h, 3.5, h), "wall", "stone", "west_wall")
	a.add_box(Vector3(h, 0, -h), Vector3(h + 1, 3.5, h), "wall", "stone", "east_wall")
	# Low cover wall west of the duel line (lava wave stop test, lightning barrier)
	a.add_box(Vector3(-5.0, 0, -1.25), Vector3(-2.5, 1.7, -0.75), "wall", "stone", "cover_wall")
	# Terrace behind the player: waves flowing off its edge drop down; waves can't climb it
	a.add_box(Vector3(-4.0, 0, 10.0), Vector3(4.0, 0.6, 14.0), "ledge", "stone", "terrace")
	# Walkable step block
	a.add_box(Vector3(9.0, 0, 9.0), Vector3(12.0, 0.35, 12.0), "ledge", "stone", "step_block")
	# High ledge: only reachable with an air updraft (traversal challenge)
	a.add_box(Vector3(-15.0, 0, -15.0), Vector3(-10.5, 1.8, -10.5), "ledge", "stone", "high_ledge")
	# Pillars (camera collision / cover)
	a.add_box(Vector3(12.5, 0, -13.5), Vector3(13.5, 3.2, -12.5), "pillar", "stone", "pillar_ne")
	a.add_box(Vector3(-13.5, 0, 12.5), Vector3(-12.5, 3.2, 13.5), "pillar", "stone", "pillar_sw")
	return a


func add_box(mn: Vector3, mx: Vector3, kind: String, surface: String, nm: String) -> void:
	solids.append({"min": mn, "max": mx, "kind": kind, "surface": surface, "name": nm})


func in_pool(x: float, z: float) -> bool:
	return x > pool_min.x and x < pool_max.x and z > pool_min.y and z < pool_max.y


func on_metal(x: float, z: float) -> bool:
	return x > metal_min.x and x < metal_max.x and z > metal_min.y and z < metal_max.y


func base_floor(x: float, z: float) -> float:
	if in_pool(x, z):
		return pool_floor
	if on_metal(x, z):
		return metal_top
	return 0.0


## Highest walkable surface under (x, z) that can be reached from height from_y
## (surfaces above from_y + step are walls, not ground).
func ground_height(x: float, z: float, from_y: float = INF, step: float = Sim.STEP_HEIGHT) -> float:
	var g := base_floor(x, z)
	for s in solids:
		var mn: Vector3 = s.min
		var mx: Vector3 = s.max
		if x >= mn.x and x <= mx.x and z >= mn.z and z <= mx.z:
			if mx.y <= from_y + step and mx.y > g:
				g = mx.y
	return g


func surface_at(x: float, z: float, y: float) -> String:
	if in_pool(x, z) and y < pool_level + 0.15:
		return "water"
	if on_metal(x, z) and absf(y - metal_top) < 0.15:
		return "metal"
	return "stone"


## Pushes a vertical capsule (centre x/z, feet y, radius r, height h) out of solids
## that are too tall to step onto. Returns corrected position.
func push_out(p: Vector3, r: float, h: float = Sim.ACTOR_HEIGHT, step: float = Sim.STEP_HEIGHT) -> Vector3:
	var out := p
	for s in solids:
		var mn: Vector3 = s.min
		var mx: Vector3 = s.max
		if mx.y <= out.y + step or mn.y >= out.y + h:
			continue
		var cx := clampf(out.x, mn.x, mx.x)
		var cz := clampf(out.z, mn.z, mx.z)
		var dx := out.x - cx
		var dz := out.z - cz
		var d2 := dx * dx + dz * dz
		if d2 >= r * r:
			continue
		if d2 > 1e-8:
			var d := sqrt(d2)
			out.x = cx + dx / d * r
			out.z = cz + dz / d * r
		else:
			# Centre inside the box: leave through the nearest face.
			var pen := [out.x - mn.x, mx.x - out.x, out.z - mn.z, mx.z - out.z]
			var i := 0
			for k in range(1, 4):
				if pen[k] < pen[i]:
					i = k
			match i:
				0: out.x = mn.x - r
				1: out.x = mx.x + r
				2: out.z = mn.z - r
				3: out.z = mx.z + r
	return out


## First hit of segment a->b against solids (inflated by radius). Returns t in 0..1, or -1.
func segment_hit(a: Vector3, b: Vector3, radius: float = 0.0) -> float:
	var best := -1.0
	var d := b - a
	for s in solids:
		var mn: Vector3 = s.min - Vector3(radius, radius, radius)
		var mx: Vector3 = s.max + Vector3(radius, radius, radius)
		var t := _slab(a, d, mn, mx)
		if t >= 0.0 and (best < 0.0 or t < best):
			best = t
	return best


static func _slab(o: Vector3, d: Vector3, mn: Vector3, mx: Vector3) -> float:
	var tmin := 0.0
	var tmax := 1.0
	for i in 3:
		var oi := o[i]
		var di := d[i]
		if absf(di) < 1e-9:
			if oi < mn[i] or oi > mx[i]:
				return -1.0
		else:
			var t1 := (mn[i] - oi) / di
			var t2 := (mx[i] - oi) / di
			if t1 > t2:
				var tt := t1
				t1 = t2
				t2 = tt
			tmin = maxf(tmin, t1)
			tmax = minf(tmax, t2)
			if tmin > tmax:
				return -1.0
	return tmin


func has_los(a: Vector3, b: Vector3) -> bool:
	return segment_hit(a, b) < 0.0


func solid_named(nm: String) -> Dictionary:
	for s in solids:
		if s.name == nm:
			return s
	return {}
