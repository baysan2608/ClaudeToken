class_name VfxMesh
extends RefCounted
## Procedural mesh builders shared by the VFX scenes. All meshes are built in unit space (radius ~1)
## and cached by seed so pooled stones / walls do not rebuild geometry.

static var _rock_cache: Dictionary = {}
static var _wall_cache: Dictionary = {}
static var _ico_verts: PackedVector3Array = PackedVector3Array()
static var _ico_faces: PackedInt32Array = PackedInt32Array()

const CUSTOM0_FLAGS: int = Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT


## Unit icosphere with `subdiv` midpoint subdivisions. Faces are CCW seen from outside.
static func icosphere(subdiv: int) -> Array:
	var t: float = (1.0 + sqrt(5.0)) * 0.5
	var v := PackedVector3Array([
		Vector3(-1, t, 0), Vector3(1, t, 0), Vector3(-1, -t, 0), Vector3(1, -t, 0),
		Vector3(0, -1, t), Vector3(0, 1, t), Vector3(0, -1, -t), Vector3(0, 1, -t),
		Vector3(t, 0, -1), Vector3(t, 0, 1), Vector3(-t, 0, -1), Vector3(-t, 0, 1)])
	for i in v.size():
		v[i] = v[i].normalized()
	var f := PackedInt32Array([
		0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
		1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
		3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
		4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1])
	for _s in subdiv:
		var cache: Dictionary = {}
		var nf := PackedInt32Array()
		for i in range(0, f.size(), 3):
			var a: int = f[i]
			var b: int = f[i + 1]
			var c: int = f[i + 2]
			var ab: int = _midpoint(v, cache, a, b)
			var bc: int = _midpoint(v, cache, b, c)
			var ca: int = _midpoint(v, cache, c, a)
			nf.append_array(PackedInt32Array([a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca]))
		f = nf
	return [v, f]


static func _midpoint(v: PackedVector3Array, cache: Dictionary, a: int, b: int) -> int:
	var key: int = mini(a, b) * 100000 + maxi(a, b)
	if cache.has(key):
		return cache[key]
	v.append(((v[a] + v[b]) * 0.5).normalized())
	cache[key] = v.size() - 1
	return v.size() - 1


## Faceted irregular rock, mean vertex radius 1.0, flat shaded (unindexed), ~320 triangles.
## CUSTOM0 = (relaxed blob position, per-vertex random). Deterministic from seed_value.
static func rock_mesh(seed_value: int) -> ArrayMesh:
	var key: int = seed_value
	if _rock_cache.has(key):
		return _rock_cache[key]
	if _rock_cache.size() > 48:
		_rock_cache.clear()
	if _ico_verts.is_empty():
		var ico: Array = icosphere(2)
		_ico_verts = ico[0]
		_ico_faces = ico[1]
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(seed_value) ^ 0x5bd1e995
	var nz := FastNoiseLite.new()
	nz.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	nz.fractal_octaves = 3
	nz.frequency = 1.0
	nz.seed = seed_value
	var squash := Vector3(rng.randf_range(0.92, 1.12), rng.randf_range(0.74, 0.92), rng.randf_range(0.9, 1.1))
	var plane_n: Array[Vector3] = []
	var plane_d: Array[float] = []
	var cuts: int = rng.randi_range(9, 12)
	for i in cuts:
		var n := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		plane_n.append(n)
		plane_d.append(rng.randf_range(0.66, 0.84))
	var vc: int = _ico_verts.size()
	var pos := PackedVector3Array()
	pos.resize(vc)
	var dirs := PackedVector3Array()
	dirs.resize(vc)
	var rnd := PackedFloat32Array()
	rnd.resize(vc)
	var mean_r: float = 0.0
	for i in vc:
		var d: Vector3 = _ico_verts[i]
		dirs[i] = d
		var r: float = 1.0 + 0.2 * nz.get_noise_3dv(d * 1.5 + Vector3(3.1, 1.7, 5.3))
		var p: Vector3 = d * r * squash
		for _pass in 2:
			for k in cuts:
				var ex: float = p.dot(plane_n[k]) - plane_d[k]
				if ex > 0.0:
					p -= plane_n[k] * ex
		pos[i] = p
		rnd[i] = rng.randf()
		mean_r += p.length()
	mean_r /= float(vc)
	var inv: float = 1.0 / mean_r
	var blob_k: float = 0.0
	for i in vc:
		pos[i] *= inv
	# relaxed blob: ellipsoid with the same mean radius, keeps the rock's proportions
	var blob := PackedVector3Array()
	blob.resize(vc)
	for i in vc:
		blob[i] = dirs[i] * squash
		blob_k += blob[i].length()
	blob_k = (float(vc) / blob_k)
	for i in vc:
		blob[i] *= blob_k
	var tc: int = _ico_faces.size()
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var custom := PackedFloat32Array()
	verts.resize(tc)
	norms.resize(tc)
	custom.resize(tc * 4)
	var o: int = 0
	for i in range(0, tc, 3):
		var ia: int = _ico_faces[i]
		var ib: int = _ico_faces[i + 1]
		var ic: int = _ico_faces[i + 2]
		var a: Vector3 = pos[ia]
		var b: Vector3 = pos[ib]
		var c: Vector3 = pos[ic]
		var n: Vector3 = (b - a).cross(c - a)
		if n.length_squared() < 1e-12:
			n = (a + b + c).normalized()
		n = n.normalized()
		# emit clockwise (Godot front face) -> a, c, b
		var order: Array[int] = [ia, ic, ib]
		for j in 3:
			var idx: int = order[j]
			verts[i + j] = pos[idx]
			norms[i + j] = n
			custom[o] = blob[idx].x
			custom[o + 1] = blob[idx].y
			custom[o + 2] = blob[idx].z
			custom[o + 3] = rnd[idx]
			o += 4
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_CUSTOM0] = custom
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, CUSTOM0_FLAGS)
	mesh.custom_aabb = AABB(Vector3(-1.5, -1.5, -1.5), Vector3(3, 3, 3))
	_rock_cache[key] = mesh
	return mesh


## Low stone guard wall made of five chunky, chamfered blocks (octagonal plan, bevelled tops).
## Unit footprint: x in [-1, 1], height ~1 (y 0..1), depth ~0.5 (z -0.25..0.25).
## CUSTOM0.w = per-block rise delay (0..1, centre rises first).
static func wall_mesh(seed_value: int) -> ArrayMesh:
	if _wall_cache.has(seed_value):
		return _wall_cache[seed_value]
	if _wall_cache.size() > 16:
		_wall_cache.clear()
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(seed_value) ^ 0x1b873593
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var custom := PackedFloat32Array()
	var blocks: int = 5
	var bw: float = 2.0 / blocks
	for bi in blocks:
		var cx: float = -1.0 + bw * (bi + 0.5)
		var edge: float = absf(bi - (blocks - 1) * 0.5) / ((blocks - 1) * 0.5)  # 0 centre .. 1 outer
		var h: float = (1.0 - 0.2 * edge) * rng.randf_range(0.93, 1.0)
		var hw: float = bw * 0.54 + rng.randf_range(-0.01, 0.01)
		var hd: float = rng.randf_range(0.21, 0.26)
		var ch: float = minf(hw, hd) * rng.randf_range(0.38, 0.5)
		var yaw: float = rng.randf_range(-0.07, 0.07)
		var delay: float = edge
		# plan polygon (octagon), CCW seen from above
		var plan: Array[Vector2] = [
			Vector2(hw - ch, -hd), Vector2(hw, -hd + ch), Vector2(hw, hd - ch), Vector2(hw - ch, hd),
			Vector2(-hw + ch, hd), Vector2(-hw, hd - ch), Vector2(-hw, -hd + ch), Vector2(-hw + ch, -hd)]
		var n: int = plan.size()
		# rings: base (slightly wider), shoulder (y = 0.86 h), top bevel (inset)
		var ring_base: Array[Vector3] = []
		var ring_sh: Array[Vector3] = []
		var ring_top: Array[Vector3] = []
		for i in n:
			var p: Vector2 = plan[i]
			var q: Vector2 = Vector2(p.x * cos(yaw) - p.y * sin(yaw), p.x * sin(yaw) + p.y * cos(yaw))
			var jit := Vector3(rng.randfn(0.0, 0.012), rng.randfn(0.0, 0.015), rng.randfn(0.0, 0.012))
			ring_base.append(Vector3(cx + q.x * 1.05, 0.0, q.y * 1.05))
			ring_sh.append(Vector3(cx + q.x + jit.x, h * 0.84 + jit.y * 1.5, q.y + jit.z))
			ring_top.append(Vector3(cx + q.x * 0.78 + jit.x, h * (1.0 + rng.randf_range(-0.03, 0.01)), q.y * 0.74 + jit.z))
		var centre := Vector3(cx + rng.randf_range(-0.02, 0.02), h * 1.02, rng.randf_range(-0.02, 0.02))
		var body_centre := Vector3(cx, h * 0.5, 0.0)
		var tris: Array = []
		for i in n:
			var j: int = (i + 1) % n
			# side quad base -> shoulder
			tris.append([ring_base[i], ring_base[j], ring_sh[j]])
			tris.append([ring_base[i], ring_sh[j], ring_sh[i]])
			# bevel quad shoulder -> top ring
			tris.append([ring_sh[i], ring_sh[j], ring_top[j]])
			tris.append([ring_sh[i], ring_top[j], ring_top[i]])
			# top cap fan
			tris.append([ring_top[i], ring_top[j], centre])
		for t in tris:
			var a: Vector3 = t[0]
			var b: Vector3 = t[1]
			var c: Vector3 = t[2]
			var nn: Vector3 = (b - a).cross(c - a)
			if nn.length_squared() < 1e-12:
				continue
			nn = nn.normalized()
			var tri_pts: Array[Vector3] = [a, b, c]
			if nn.dot((a + b + c) / 3.0 - body_centre) > 0.0:
				tri_pts = [a, c, b]  # Godot front face = clockwise seen from outside
			else:
				nn = -nn
			for p in tri_pts:
				verts.append(p)
				norms.append(nn)
				custom.append_array(PackedFloat32Array([p.x, p.y, p.z, delay]))
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_CUSTOM0] = custom
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, CUSTOM0_FLAGS)
	mesh.custom_aabb = AABB(Vector3(-1.4, -1.4, -0.6), Vector3(2.8, 2.8, 1.2))
	_wall_cache[seed_value] = mesh
	return mesh


static var _flame: ArrayMesh = null


## Open flame surface: axis +Y (0..1), unit radius ring, 14 sides x 14 rings (~364 tris).
## UV = (angle fraction, y). The flame shader reshapes the radius / length every frame.
static func flame_mesh() -> ArrayMesh:
	if _flame != null:
		return _flame
	var sides: int = 14
	var rings: int = 14
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	var idx := PackedInt32Array()
	for r in rings:
		var y: float = float(r) / float(rings - 1)
		for k in sides + 1:
			var a: float = float(k) / float(sides) * TAU
			verts.append(Vector3(cos(a), y, sin(a)))
			norms.append(Vector3(cos(a), 0.0, sin(a)))
			uvs.append(Vector2(float(k) / float(sides), y))
	for r in rings - 1:
		for k in sides:
			var a: int = r * (sides + 1) + k
			var b: int = a + sides + 1
			idx.append_array(PackedInt32Array([a, b, a + 1, a + 1, b, b + 1]))
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.custom_aabb = AABB(Vector3(-1.6, -0.2, -1.6), Vector3(3.2, 1.6, 3.2))
	_flame = mesh
	return _flame


static var _cone: ArrayMesh = null
static var _streaks: ArrayMesh = null


## Open cone surface for the air push: apex at y=0, unit end radius at y=1, 20 sides x 10 rings.
static func cone_mesh() -> ArrayMesh:
	if _cone != null:
		return _cone
	var sides: int = 20
	var rings: int = 10
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var uvs := PackedVector2Array()
	var idx := PackedInt32Array()
	for r in rings:
		var y: float = float(r) / float(rings - 1)
		var rad: float = 0.06 + 0.94 * y
		for k in sides + 1:
			var a: float = float(k) / float(sides) * TAU
			verts.append(Vector3(cos(a) * rad, y, sin(a) * rad))
			norms.append(Vector3(cos(a), -0.3, sin(a)).normalized())
			uvs.append(Vector2(float(k) / float(sides), y))
	for r in rings - 1:
		for k in sides:
			var a: int = r * (sides + 1) + k
			var b: int = a + sides + 1
			idx.append_array(PackedInt32Array([a, b, a + 1, a + 1, b, b + 1]))
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.custom_aabb = AABB(Vector3(-1.4, -0.1, -1.4), Vector3(2.8, 1.4, 2.8))
	_cone = mesh
	return _cone


## N dust streak quads (4 vertices each, positions are placeholders; the shader places them).
static func streak_mesh(count: int = 14) -> ArrayMesh:
	if _streaks != null:
		return _streaks
	var rng := RandomNumberGenerator.new()
	rng.seed = 90210
	var verts := PackedVector3Array()
	var uvs := PackedVector2Array()
	var cols := PackedColorArray()
	var idx := PackedInt32Array()
	for i in count:
		var c := Color(rng.randf(), rng.randf(), rng.randf(), rng.randf())
		var base: int = verts.size()
		for corner in [Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1)]:
			verts.append(Vector3.ZERO)
			uvs.append(corner)
			cols.append(c)
		idx.append_array(PackedInt32Array([base, base + 1, base + 2, base + 1, base + 3, base + 2]))
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_COLOR] = cols
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.custom_aabb = AABB(Vector3(-1.4, -0.3, -1.4), Vector3(2.8, 1.8, 2.8))
	_streaks = mesh
	return _streaks
