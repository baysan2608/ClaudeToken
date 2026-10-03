class_name LavaWaveView
extends VfxEffect
## Low ground-hugging lava wave / sheet built as a strip mesh over a ground polyline.
## Opaque, no alpha. Shares the rock/molten/crust shader family with StoneView so a molten
## blob that spreads into a wave keeps the same material language.
##
##   set_path(points, widths)   points tail -> front on the ground (may step down ledges),
##                              widths = FULL width in metres per point
##   set_state(melt, crust, flow_speed)
##
## set_path rebuilds the mesh (reused arrays, ~200-400 vertices): call it when the path changes,
## not more than once per frame.

const CROSS: int = 9  ## vertices across the strip (rounded cross-section)
const FRONT_CAP: int = 4
const TAIL_CAP: int = 1

## Crest height at the leading front / at the tail, metres.
@export var height_front: float = 0.40
@export var height_tail: float = 0.20
## Extra bulge multiplier at the leading front.
@export var front_bulge: float = 0.28
## Metres per crust plate (Voronoi cell size).
@export var plate_size: float = 0.34
## Pattern seed (give each wave a different one so neighbouring waves do not look identical).
@export var pattern_seed: float = 0.0:
	set(v):
		pattern_seed = v
		if _mat != null:
			_mat.set_shader_parameter("seed", v)

var _mi: MeshInstance3D
var _mesh: ArrayMesh
var _mat: ShaderMaterial
var _melt: float = 1.0
var _crust: float = 0.0
var _flow: float = 0.0
var _phase: float = 0.0
var _boil: float = 0.0
var _has_path: bool = false

var _verts: PackedVector3Array = PackedVector3Array()
var _norms: PackedVector3Array = PackedVector3Array()
var _uv: PackedVector2Array = PackedVector2Array()
var _uv2: PackedVector2Array = PackedVector2Array()
var _tan: PackedFloat32Array = PackedFloat32Array()
var _idx: PackedInt32Array = PackedInt32Array()
var _ring_c: PackedVector3Array = PackedVector3Array()
var _ring_side: PackedVector3Array = PackedVector3Array()
var _ring_up: PackedVector3Array = PackedVector3Array()
var _ring_hw: PackedFloat32Array = PackedFloat32Array()
var _ring_h: PackedFloat32Array = PackedFloat32Array()
var _ring_s: PackedFloat32Array = PackedFloat32Array()


func _init() -> void:
	_mesh = ArrayMesh.new()
	_mat = VfxMaterials.make("lava_wave")
	_mat.set_shader_parameter("noise_vol", VfxTextures.noise_volume())
	_mat.set_shader_parameter("plate_size", plate_size)
	_mi = MeshInstance3D.new()
	_mi.name = "Strip"
	_mi.mesh = _mesh
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mi)
	_push_state()


## Replace the ground polyline. points = tail..front, widths = full width per point.
func set_path(points: PackedVector3Array, widths: PackedFloat32Array) -> void:
	var n: int = points.size()
	if n < 2 or widths.size() < n:
		_mesh.clear_surfaces()
		_has_path = false
		return
	var rings: int = n + FRONT_CAP + TAIL_CAP
	var vcount: int = rings * CROSS
	_ring_c.resize(rings)
	_ring_side.resize(rings)
	_ring_up.resize(rings)
	_ring_hw.resize(rings)
	_ring_h.resize(rings)
	_ring_s.resize(rings)
	# --- ring frames along the polyline (index 0 = tail cap tip when TAIL_CAP = 1)
	var total: float = 0.0
	var arc := PackedFloat32Array()
	arc.resize(n)
	for i in range(1, n):
		total += points[i].distance_to(points[i - 1])
		arc[i] = total
	var up := Vector3.UP
	var tail_dir := Vector3.FORWARD
	var front_dir := Vector3.FORWARD
	for i in n:
		var a: Vector3 = points[maxi(i - 1, 0)]
		var b: Vector3 = points[mini(i + 1, n - 1)]
		var t: Vector3 = Vector3(b.x - a.x, 0.0, b.z - a.z)
		if t.length_squared() < 1e-8:
			t = Vector3(points[n - 1].x - points[0].x, 0.0, points[n - 1].z - points[0].z)
			if t.length_squared() < 1e-8:
				t = Vector3.FORWARD
		t = t.normalized()
		var k: float = arc[i] / maxf(total, 0.001)
		var hw: float = maxf(widths[i], 0.05) * 0.5
		var h: float = lerpf(height_tail, height_front, smoothstep(0.0, 1.0, k))
		h *= 1.0 + front_bulge * smoothstep(0.55, 1.0, k)
		hw *= 1.0 + 0.10 * smoothstep(0.7, 1.0, k)
		var r: int = i + TAIL_CAP
		_ring_c[r] = points[i]
		var side: Vector3 = t.cross(up).normalized()
		var f3: Vector3 = b - a
		f3 = f3.normalized() if f3.length_squared() > 1e-10 else t
		var upn: Vector3 = side.cross(f3).normalized()  # slope-aware: lava drapes over ledges
		if upn.y < 0.05:
			upn = (upn + Vector3.UP * 0.3).normalized()
		_ring_side[r] = side
		_ring_up[r] = upn
		_ring_hw[r] = hw
		_ring_h[r] = h
		_ring_s[r] = arc[i]
		if i == 0:
			tail_dir = t
		if i == n - 1:
			front_dir = t
	# tail cap: a narrow, low ring just behind the first point
	var r0: int = TAIL_CAP - 1
	_ring_c[r0] = points[0] - tail_dir * (_ring_hw[TAIL_CAP] * 0.5)
	_ring_side[r0] = _ring_side[TAIL_CAP]
	_ring_up[r0] = _ring_up[TAIL_CAP]
	_ring_hw[r0] = _ring_hw[TAIL_CAP] * 0.25
	_ring_h[r0] = _ring_h[TAIL_CAP] * 0.2
	_ring_s[r0] = -_ring_hw[TAIL_CAP] * 0.5
	# front cap: quarter-ellipse rounding the leading edge
	var last: int = n - 1 + TAIL_CAP
	var cap_len: float = _ring_hw[last] * 0.6
	for j in range(1, FRONT_CAP + 1):
		var phi: float = float(j) / float(FRONT_CAP) * PI * 0.5
		var r: int = last + j
		_ring_c[r] = points[n - 1] + front_dir * (cap_len * sin(phi))
		_ring_side[r] = _ring_side[last]
		_ring_up[r] = _ring_up[last]
		_ring_hw[r] = maxf(_ring_hw[last] * cos(phi), 0.002)
		_ring_h[r] = _ring_h[last] * pow(maxf(cos(phi), 0.0), 0.65)
		_ring_s[r] = total + cap_len * sin(phi)
	var front_s: float = total + cap_len
	# --- vertices
	_verts.resize(vcount)
	_uv.resize(vcount)
	_uv2.resize(vcount)
	_tan.resize(vcount * 4)
	_norms.resize(vcount)
	for r in rings:
		var c: Vector3 = _ring_c[r]
		var sd: Vector3 = _ring_side[r]
		var upv: Vector3 = _ring_up[r]
		var hw2: float = _ring_hw[r]
		var h2: float = _ring_h[r]
		for k in CROSS:
			var th: float = -PI * 0.5 + PI * float(k) / float(CROSS - 1)
			var u: float = sin(th)
			var v: float = cos(th)
			var vi: int = r * CROSS + k
			_verts[vi] = c + sd * (hw2 * u) + upv * (h2 * v)
			_uv[vi] = Vector2(_ring_s[r], hw2 * u)
			_uv2[vi] = Vector2(front_s - _ring_s[r], v)
	# --- indices (clockwise from above = Godot front face) and smooth normals
	var quads: int = (rings - 1) * (CROSS - 1)
	if _idx.size() != quads * 6:
		_idx.resize(quads * 6)
		var o: int = 0
		for r in rings - 1:
			for k in CROSS - 1:
				var a: int = r * CROSS + k
				var b: int = a + CROSS
				_idx[o] = a
				_idx[o + 1] = b
				_idx[o + 2] = b + 1
				_idx[o + 3] = a
				_idx[o + 4] = b + 1
				_idx[o + 5] = a + 1
				o += 6
	for i in vcount:
		_norms[i] = Vector3.ZERO
	for i in range(0, _idx.size(), 3):
		var ia: int = _idx[i]
		var ib: int = _idx[i + 1]
		var ic: int = _idx[i + 2]
		var fn: Vector3 = (_verts[ic] - _verts[ia]).cross(_verts[ib] - _verts[ia])
		_norms[ia] += fn
		_norms[ib] += fn
		_norms[ic] += fn
	for i in vcount:
		var nn: Vector3 = _norms[i]
		_norms[i] = nn.normalized() if nn.length_squared() > 1e-12 else Vector3.UP
	# tangents: along the flow (s increasing) projected on the surface, w = -1 so BINORMAL points right
	for r in rings:
		var r_a: int = maxi(r - 1, 0)
		var r_b: int = mini(r + 1, rings - 1)
		for k in CROSS:
			var tv: Vector3 = _verts[r_b * CROSS + k] - _verts[r_a * CROSS + k]
			var nv: Vector3 = _norms[r * CROSS + k]
			tv = (tv - nv * tv.dot(nv))
			tv = tv.normalized() if tv.length_squared() > 1e-10 else _ring_side[r].cross(nv)
			var ti: int = (r * CROSS + k) * 4
			_tan[ti] = tv.x
			_tan[ti + 1] = tv.y
			_tan[ti + 2] = tv.z
			_tan[ti + 3] = -1.0
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_TANGENT] = _tan
	arrays[Mesh.ARRAY_VERTEX] = _verts
	arrays[Mesh.ARRAY_NORMAL] = _norms
	arrays[Mesh.ARRAY_TEX_UV] = _uv
	arrays[Mesh.ARRAY_TEX_UV2] = _uv2
	arrays[Mesh.ARRAY_INDEX] = _idx
	_mesh.clear_surfaces()
	_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	_has_path = true
	visible = true


## melt01: 1 = liquid lava, 0 = solid. crust01: plates close the surface and stop the flow.
## flow_speed: metres/second the surface pattern travels toward the front (slowed by crust).
func set_state(melt01: float, crust01: float, flow_speed: float) -> void:
	_melt = clampf(melt01, 0.0, 1.0)
	_crust = clampf(crust01, 0.0, 1.0)
	_flow = flow_speed
	_push_state()


func reset() -> void:
	_mesh.clear_surfaces()
	_has_path = false
	_phase = 0.0
	_boil = 0.0
	_melt = 1.0
	_crust = 0.0
	_flow = 0.0
	_push_state()
	visible = false


func advance(dt: float) -> void:
	var live: float = 1.0 - _crust
	_phase += _flow * (1.0 - 0.92 * _crust) * dt
	_boil += dt * (0.25 + 0.75 * live)
	_mat.set_shader_parameter("phase", _phase)
	_mat.set_shader_parameter("boil", _boil)


func _push_state() -> void:
	_mat.set_shader_parameter("melt", _melt)
	_mat.set_shader_parameter("crust", _crust)
	_mat.set_shader_parameter("phase", _phase)
	_mat.set_shader_parameter("boil", _boil)
