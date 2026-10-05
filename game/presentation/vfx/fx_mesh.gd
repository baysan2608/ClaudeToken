class_name FxMesh
extends RefCounted
## Procedural meshes for the moveset views (crystals, metal shapes, funnels, beams, rings, tubes).
## Built once and cached (crystal clusters per seed + mode); every mesh is unit sized and scaled by
## its node. Mesh contracts are documented next to each shader in presentation/vfx/shaders.

const CUSTOM0_FLAGS: int = Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT

static var _cache: Dictionary = {}


static func _arrays() -> Array:
	var a: Array = []
	a.resize(Mesh.ARRAY_MAX)
	return a


static func _commit(key: Variant, arrays: Array, aabb: AABB = AABB(), flags: int = 0) -> ArrayMesh:
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, flags)
	if aabb.size != Vector3.ZERO:
		mesh.custom_aabb = aabb
	if key != null:
		if _cache.size() > 64:
			_cache.clear()
		_cache[key] = mesh
	return mesh


## Faceted crystals (glass / ice). mode:
##   "shard"   one double-pointed crystal along +Y, centred, length 1, radius ~0.16
##   "cluster" 5-8 crystals fanning up from the origin (spikes, fangs), ~1 m tall
##   "wall"    a row of crystals along X in [-1, 1], ~1 m tall, ~0.5 m deep (ice / glass wall)
##   "ridge"   a low jagged row along X in [-1, 1], ~0.6 m tall (ice ridge, rime crest)
## UV = (across the facet, along the crystal), UV2 = (crystal random, crystal base height).
static func crystal_mesh(seed_value: int, mode: String) -> ArrayMesh:
	var key := "crystal:%s:%d" % [mode, seed_value]
	if _cache.has(key):
		return _cache[key]
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(key)
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	var uv2s := PackedVector2Array()
	match mode:
		"shard":
			_crystal(verts, norms, uvs, uv2s, Vector3(0, -0.5, 0), Vector3.UP, 1.0, 0.16, 0.5, rng.randf(), true, rng)
		"cluster":
			var n: int = rng.randi_range(5, 8)
			for i in n:
				var ang: float = TAU * float(i) / float(n) + rng.randf_range(-0.3, 0.3)
				var tilt: float = rng.randf_range(0.15, 0.55) if i > 0 else 0.05
				var axis := Vector3(cos(ang) * sin(tilt), cos(tilt), sin(ang) * sin(tilt)).normalized()
				var base := Vector3(cos(ang), 0, sin(ang)) * rng.randf_range(0.0, 0.25) * float(i > 0) - Vector3(0, 0.15, 0)
				var ln: float = rng.randf_range(0.55, 1.0) if i > 0 else 1.15
				_crystal(verts, norms, uvs, uv2s, base, axis, ln, ln * rng.randf_range(0.12, 0.18), 0.0, rng.randf(), false, rng)
		"wall", "ridge":
			var n: int = 13 if mode == "wall" else 11
			var hmax: float = 1.0 if mode == "wall" else 0.6
			for i in n:
				var x: float = lerpf(-1.0, 1.0, (float(i) + rng.randf_range(-0.3, 0.3)) / float(n - 1))
				for row in 2:
					var z: float = rng.randf_range(-0.2, 0.2) + (row - 0.5) * 0.22
					var tilt := Vector3(rng.randf_range(-0.25, 0.25), 1.0, rng.randf_range(-0.3, 0.3) + (row - 0.5) * 0.4).normalized()
					var edge: float = 1.0 - 0.45 * pow(absf(x), 3.0)
					var ln: float = hmax * edge * rng.randf_range(0.65, 1.05) * (1.0 if row == 0 else 0.8)
					_crystal(verts, norms, uvs, uv2s, Vector3(x, -0.12, z), tilt, ln + 0.12, rng.randf_range(0.13, 0.2) * (1.2 if mode == "wall" else 1.0), 0.0, rng.randf(), false, rng)
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_TEX_UV2] = uv2s
	return _commit(key, arrays)


## One hexagonal crystal: prism from `base` along `axis`, pointed tip (and a pointed foot when
## `double`). Flat shaded, appended to the arrays.
static func _crystal(verts: PackedVector3Array, norms: PackedVector3Array, uvs: PackedVector2Array, uv2s: PackedVector2Array,
		base: Vector3, axis: Vector3, length: float, radius: float, _unused: float, rnd: float, double: bool,
		rng: RandomNumberGenerator) -> void:
	var sides: int = 6
	var side := axis.cross(Vector3.FORWARD if absf(axis.y) > 0.9 else Vector3.UP).normalized()
	var fwd := axis.cross(side).normalized()
	var body: float = length * rng.randf_range(0.62, 0.74)
	var foot: float = length * 0.14 if double else 0.0
	var r_top: float = radius * rng.randf_range(0.8, 0.95)
	var ring_a: Array[Vector3] = []
	var ring_b: Array[Vector3] = []
	var rot: float = rng.randf() * TAU
	for k in sides:
		var a: float = rot + TAU * float(k) / float(sides)
		var wob: float = rng.randf_range(0.85, 1.12)
		var d := (side * cos(a) + fwd * sin(a)) * wob
		ring_a.append(base + axis * foot + d * radius)
		ring_b.append(base + axis * (foot + body) + d * r_top)
	var tip := base + axis * length + side * rng.randf_range(-0.03, 0.03) * length
	var bot := base
	var hy: float = base.y
	var mid := base + axis * (foot + body * 0.5)
	for k in sides:
		var k2: int = (k + 1) % sides
		# prism face
		_quad(verts, norms, uvs, uv2s, ring_a[k], ring_a[k2], ring_b[k2], ring_b[k], rnd, hy, mid)
		# tip facet
		_tri(verts, norms, uvs, uv2s, ring_b[k], ring_b[k2], tip, rnd, hy, mid)
		if double:
			_tri(verts, norms, uvs, uv2s, ring_a[k2], ring_a[k], bot, rnd, hy, mid)


static func _quad(verts: PackedVector3Array, norms: PackedVector3Array, uvs: PackedVector2Array, uv2s: PackedVector2Array,
		a: Vector3, b: Vector3, c: Vector3, d: Vector3, rnd: float, hy: float, center: Vector3) -> void:
	_tri_uv(verts, norms, uvs, uv2s, a, b, c, Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), rnd, hy, center)
	_tri_uv(verts, norms, uvs, uv2s, a, c, d, Vector2(0, 0), Vector2(1, 1), Vector2(0, 1), rnd, hy, center)


static func _tri(verts: PackedVector3Array, norms: PackedVector3Array, uvs: PackedVector2Array, uv2s: PackedVector2Array,
		a: Vector3, b: Vector3, c: Vector3, rnd: float, hy: float, center: Vector3) -> void:
	_tri_uv(verts, norms, uvs, uv2s, a, b, c, Vector2(0, 0.7), Vector2(1, 0.7), Vector2(0.5, 1.0), rnd, hy, center)


## One flat-shaded triangle facing away from `center` (Godot front faces are clockwise).
static func _tri_uv(verts: PackedVector3Array, norms: PackedVector3Array, uvs: PackedVector2Array, uv2s: PackedVector2Array,
		a: Vector3, b: Vector3, c: Vector3, ua: Vector2, ub: Vector2, uc: Vector2, rnd: float, hy: float, center: Vector3) -> void:
	var n := (b - a).cross(c - a)
	if n.length_squared() < 1e-14:
		return
	n = n.normalized()
	if n.dot((a + b + c) / 3.0 - center) < 0.0:
		# geometric normal points inward: as given, the triangle is already clockwise from outside
		verts.append_array(PackedVector3Array([a, b, c]))
		uvs.append_array(PackedVector2Array([ua, ub, uc]))
		n = -n
	else:
		verts.append_array(PackedVector3Array([a, c, b]))
		uvs.append_array(PackedVector2Array([ua, uc, ub]))
	for _i in 3:
		norms.append(n)
		uv2s.append(Vector2(rnd, hy))


## Lathe (surface of revolution) around +Y from a profile of (radius, y) points. UV = (angle, t).
static func _lathe(key: String, profile: PackedVector2Array, sides: int, smooth: bool = true) -> ArrayMesh:
	if _cache.has(key):
		return _cache[key]
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	var idx := PackedInt32Array()
	var rows: int = profile.size()
	for r in rows:
		var pr: Vector2 = profile[r]
		var prev: Vector2 = profile[maxi(r - 1, 0)]
		var nxt: Vector2 = profile[mini(r + 1, rows - 1)]
		var tng := (nxt - prev)
		var n2 := Vector2(tng.y, -tng.x).normalized() if tng.length_squared() > 1e-10 else Vector2(1, 0)
		for k in sides + 1:
			var a: float = TAU * float(k) / float(sides)
			verts.append(Vector3(cos(a) * pr.x, pr.y, sin(a) * pr.x))
			norms.append(Vector3(cos(a) * n2.x, n2.y, sin(a) * n2.x).normalized() if smooth else Vector3(cos(a), 0, sin(a)))
			uvs.append(Vector2(float(k) / float(sides), float(r) / float(rows - 1)))
	for r in rows - 1:
		for k in sides:
			var a: int = r * (sides + 1) + k
			var b: int = a + sides + 1
			idx.append_array(PackedInt32Array([a, a + 1, b, a + 1, b + 1, b]))
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = idx
	return _commit(key, arrays)


## Thrown metal disc: lens with a bevelled rim, axis +Y, radius 1, thickness 0.14 (UV.x = angle,
## so the brushed streaks run around the disc).
static func disc_mesh() -> ArrayMesh:
	return _lathe("disc", PackedVector2Array([Vector2(0.0, -0.05), Vector2(0.55, -0.06), Vector2(0.92, -0.035),
		Vector2(1.0, 0.0), Vector2(0.92, 0.035), Vector2(0.55, 0.06), Vector2(0.0, 0.05)]), 24)


## Lance: long rod along +Y (-0.5..0.5), radius 1 (scale x/z), sharp tip at +Y.
static func lance_mesh() -> ArrayMesh:
	return _lathe("lance", PackedVector2Array([Vector2(0.0, -0.5), Vector2(0.8, -0.48), Vector2(1.0, -0.4),
		Vector2(1.0, 0.25), Vector2(0.8, 0.32), Vector2(0.0, 0.5)]), 8)


## Rod: blunt cylinder along +Y (-0.5..0.5), radius 1.
static func rod_mesh() -> ArrayMesh:
	return _lathe("rod", PackedVector2Array([Vector2(0.0, -0.5), Vector2(0.9, -0.5), Vector2(1.0, -0.45),
		Vector2(1.0, 0.45), Vector2(0.9, 0.5), Vector2(0.0, 0.5)]), 8)


## Spike / fang / thorn: cone along +Y (0..1), radius 1 at the base.
static func spike_mesh() -> ArrayMesh:
	return _lathe("spike", PackedVector2Array([Vector2(0.0, -0.05), Vector2(1.0, 0.0), Vector2(0.55, 0.45),
		Vector2(0.18, 0.85), Vector2(0.0, 1.0)]), 7, false)


## Unit icosphere (smooth normals) with spherical UVs, for shells, cores and blasts.
static func sphere_mesh(subdiv: int = 2) -> ArrayMesh:
	var key := "sphere:%d" % subdiv
	if _cache.has(key):
		return _cache[key]
	var ico: Array = VfxMesh.icosphere(subdiv)
	var v: PackedVector3Array = ico[0]
	var f: PackedInt32Array = ico[1]
	var uvs := PackedVector2Array()
	for p in v:
		uvs.append(Vector2(atan2(p.z, p.x) / TAU + 0.5, acos(clampf(p.y, -1.0, 1.0)) / PI))
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = v
	arrays[Mesh.ARRAY_NORMAL] = v.duplicate()
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	var idx := PackedInt32Array()
	for i in range(0, f.size(), 3):
		idx.append_array(PackedInt32Array([f[i], f[i + 2], f[i + 1]]))
	arrays[Mesh.ARRAY_INDEX] = idx
	return _commit(key, arrays)


## Caltrop: four spikes from the centre along the tetrahedron directions (one always points up).
static func caltrop_mesh() -> ArrayMesh:
	if _cache.has("caltrop"):
		return _cache["caltrop"]
	var dirs: Array[Vector3] = [Vector3(0, 1, 0), Vector3(0.943, -0.333, 0), Vector3(-0.471, -0.333, 0.816), Vector3(-0.471, -0.333, -0.816)]
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	for d in dirs:
		var s := d.cross(Vector3.FORWARD if absf(d.y) < 0.9 else Vector3.RIGHT).normalized()
		var t := d.cross(s).normalized()
		var tip := d
		var ring: Array[Vector3] = []
		for k in 4:
			var a: float = TAU * float(k) / 4.0
			ring.append((s * cos(a) + t * sin(a)) * 0.13)
		for k in 4:
			var a2: Vector3 = ring[k]
			var b2: Vector3 = ring[(k + 1) % 4]
			var n := (b2 - a2).cross(tip - a2).normalized()
			if n.dot(a2 + b2 + tip) < 0.0:
				n = -n
				verts.append_array(PackedVector3Array([a2, b2, tip]))
			else:
				verts.append_array(PackedVector3Array([a2, tip, b2]))
			for _i in 3:
				norms.append(n)
			uvs.append_array(PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(0.5, 1)]))
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	return _commit("caltrop", arrays)


## Vortex funnel: open surface, UV = (angle fraction, height fraction); the vortex shader sets the
## radius profile and height, so vertices here are placeholders on a unit cylinder.
static func funnel_mesh() -> ArrayMesh:
	var m := _lathe("funnel", _cyl_profile(16), 22)
	m.custom_aabb = AABB(Vector3(-4, -0.5, -4), Vector3(8, 9, 8))
	return m


static func _cyl_profile(rows: int) -> PackedVector2Array:
	var p := PackedVector2Array()
	for r in rows:
		p.append(Vector2(1.0, float(r) / float(rows - 1)))
	return p


## Beam strip along +Z (0..1): 2 x 17 vertices, UV = (side 0/1, along 0..1). Billboarded in beam.gdshader.
static func beam_mesh() -> ArrayMesh:
	if _cache.has("beam"):
		return _cache["beam"]
	var verts := PackedVector3Array()
	var uvs := PackedVector2Array()
	var idx := PackedInt32Array()
	var n: int = 17
	for i in n:
		var t: float = float(i) / float(n - 1)
		for s in 2:
			verts.append(Vector3(0, 0, t))
			uvs.append(Vector2(float(s), t))
	for i in n - 1:
		var a: int = i * 2
		idx.append_array(PackedInt32Array([a, a + 2, a + 1, a + 1, a + 2, a + 3]))
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = idx
	return _commit("beam", arrays, AABB(Vector3(-1, -1, -0.2), Vector3(2, 2, 1.4)))


## Crescent blade: an arc ribbon in the XZ plane bulging toward +Z (travel), outer (leading) radius 1,
## UV.x along the arc, UV.y from the leading edge (0) to the trailing edge (1).
static func crescent_mesh() -> ArrayMesh:
	if _cache.has("crescent"):
		return _cache["crescent"]
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	var idx := PackedInt32Array()
	var n: int = 21
	var rows: int = 4
	for i in n:
		var t: float = float(i) / float(n - 1)
		var a: float = lerpf(-1.25, 1.25, t)
		var thick: float = sin(t * PI)
		for r in rows:
			var y: float = float(r) / float(rows - 1)
			var rad: float = 1.0 - y * 0.42 * (0.25 + 0.75 * thick)
			verts.append(Vector3(sin(a) * rad, 0.0, cos(a) * rad - 0.6))
			norms.append(Vector3.UP)
			uvs.append(Vector2(t, y))
	for i in n - 1:
		for r in rows - 1:
			var a: int = i * rows + r
			var b: int = a + rows
			idx.append_array(PackedInt32Array([a, b, a + 1, a + 1, b, b + 1]))
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = idx
	return _commit("crescent", arrays)


## Curved vertical sheet (wind wall): X in [-1, 1], Y in [0, 1], bowed toward +Z by 0.15.
## UV.x along the width, UV.y = 1 - height (leading edge at the top of the crescent shader).
static func sheet_mesh() -> ArrayMesh:
	if _cache.has("sheet"):
		return _cache["sheet"]
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	var idx := PackedInt32Array()
	var nx: int = 13
	var ny: int = 6
	for i in nx:
		var u: float = float(i) / float(nx - 1)
		var x: float = u * 2.0 - 1.0
		for j in ny:
			var v: float = float(j) / float(ny - 1)
			verts.append(Vector3(x, v, 0.15 * (1.0 - x * x)))
			norms.append(Vector3(0, 0, 1))
			uvs.append(Vector2(u, 1.0 - v))
	for i in nx - 1:
		for j in ny - 1:
			var a: int = i * ny + j
			var b: int = a + ny
			idx.append_array(PackedInt32Array([a, b, a + 1, a + 1, b, b + 1]))
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = idx
	return _commit("sheet", arrays)


## Horizontal unit quad in XZ (size 2: -1..1), UV 0..1, normal +Y. For rings and ground decals.
static func ground_quad() -> ArrayMesh:
	if _cache.has("gquad"):
		return _cache["gquad"]
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-1, 0, -1), Vector3(1, 0, -1), Vector3(1, 0, 1), Vector3(-1, 0, 1)])
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.UP, Vector3.UP, Vector3.UP, Vector3.UP])
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 0, 2, 3])
	return _commit("gquad", arrays)


## Vertical unit quad in XY (size 2), UV 0..1 (billboarded rings use it).
static func face_quad() -> ArrayMesh:
	if _cache.has("fquad"):
		return _cache["fquad"]
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-1, -1, 0), Vector3(1, -1, 0), Vector3(1, 1, 0), Vector3(-1, 1, 0)])
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.BACK, Vector3.BACK, Vector3.BACK, Vector3.BACK])
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0)])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 2, 1, 0, 3, 2])
	return _commit("fquad", arrays)


## Billboard puff quad for the cloud shader (corners at +-0.5).
static func puff_quad() -> ArrayMesh:
	if _cache.has("puff"):
		return _cache["puff"]
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-0.5, -0.5, 0), Vector3(0.5, -0.5, 0), Vector3(0.5, 0.5, 0), Vector3(-0.5, 0.5, 0)])
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0)])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 2, 1, 0, 3, 2])
	return _commit("puff", arrays)


## Tubes along polylines (vines, roots, lattice) written into `mesh` (rebuilt in place).
## Each path gets `sides`-sided rings; radius tapers from radii[i] at the base to 30 % at the tip.
## Contract (vine.gdshader): UV = (angle fraction, metres along), CUSTOM0 = (centre xyz, arc fraction),
## UV2 = (tube random, radius).
static func build_tubes(mesh: ArrayMesh, paths: Array, radii: PackedFloat32Array, sides: int = 6) -> int:
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	var uv2s := PackedVector2Array()
	var cust := PackedFloat32Array()
	var idx := PackedInt32Array()
	for pi in paths.size():
		var pts: PackedVector3Array = paths[pi]
		var n: int = pts.size()
		if n < 2:
			continue
		var r0: float = radii[pi] if pi < radii.size() else 0.05
		var total: float = 0.0
		for i in range(1, n):
			total += pts[i].distance_to(pts[i - 1])
		total = maxf(total, 1e-3)
		var base_v: int = verts.size()
		var s: float = 0.0
		var prev_side := Vector3.ZERO
		var rnd: float = fposmod(float(pi) * 0.618034, 1.0)
		for i in n:
			if i > 0:
				s += pts[i].distance_to(pts[i - 1])
			var t := (pts[mini(i + 1, n - 1)] - pts[maxi(i - 1, 0)])
			t = t.normalized() if t.length_squared() > 1e-10 else Vector3.UP
			var side := prev_side
			if side == Vector3.ZERO or absf(side.dot(t)) > 0.95:
				side = t.cross(Vector3.UP if absf(t.y) < 0.95 else Vector3.RIGHT).normalized()
			side = (side - t * side.dot(t)).normalized()
			prev_side = side
			var bin := t.cross(side).normalized()
			var f: float = s / total
			var r: float = r0 * lerpf(1.0, 0.3, f)
			if i == n - 1:
				r = r0 * 0.05
			for k in sides + 1:
				var a: float = TAU * float(k) / float(sides)
				var d := side * cos(a) + bin * sin(a)
				verts.append(pts[i] + d * r)
				norms.append(d)
				uvs.append(Vector2(float(k) / float(sides), s))
				uv2s.append(Vector2(rnd, r0))
				cust.append_array(PackedFloat32Array([pts[i].x, pts[i].y, pts[i].z, f]))
		for i in n - 1:
			for k in sides:
				var a: int = base_v + i * (sides + 1) + k
				var b: int = a + sides + 1
				idx.append_array(PackedInt32Array([a, b, a + 1, a + 1, b, b + 1]))
	mesh.clear_surfaces()
	if verts.is_empty():
		return 0
	var arrays := _arrays()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_TEX_UV2] = uv2s
	arrays[Mesh.ARRAY_CUSTOM0] = cust
	arrays[Mesh.ARRAY_INDEX] = idx
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, CUSTOM0_FLAGS)
	return idx.size() / 3
