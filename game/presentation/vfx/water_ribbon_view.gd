class_name WaterRibbonView
extends VfxEffect
## Controlled water as a tube (whip / stream) that can freeze into ice.
##
##   set_points(points, radius)   world/local points tail -> tip, radius per point (metres)
##   set_state(frozen01)          0 = clean refractive-looking water, 1 = pale frosted faceted ice
##
## One transparent layer (single surface, back faces culled), 10-sided tube with smooth normals and
## rounded caps. set_points rebuilds a ~300 vertex ArrayMesh with reused arrays; call it once per
## frame while the whip moves. `flow_speed` scrolls the surface ripples along the tube.

const SIDES: int = 10
const TIP_CAP: int = 3
const BASE_CAP: int = 2

@export var flow_speed: float = 1.0

var _mi: MeshInstance3D
var _mesh: ArrayMesh
var _mat: ShaderMaterial
var _frozen: float = 0.0
var _flow: float = 0.0

var _verts: PackedVector3Array = PackedVector3Array()
var _norms: PackedVector3Array = PackedVector3Array()
var _uv: PackedVector2Array = PackedVector2Array()
var _cust: PackedFloat32Array = PackedFloat32Array()
var _idx: PackedInt32Array = PackedInt32Array()
var _ring_c: PackedVector3Array = PackedVector3Array()
var _ring_r: PackedFloat32Array = PackedFloat32Array()
var _ring_axial: PackedFloat32Array = PackedFloat32Array()
var _ring_t: PackedVector3Array = PackedVector3Array()
var _ring_s: PackedFloat32Array = PackedFloat32Array()
var _built_rings: int = -1


func _init() -> void:
	_mesh = ArrayMesh.new()
	_mat = VfxMaterials.make_water()
	_mat.set_shader_parameter("blob", 0.0)
	_mi = MeshInstance3D.new()
	_mi.name = "Tube"
	_mi.mesh = _mesh
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mi)


## points: tail..tip. radius: per point (metres). Fewer than 2 points hides the ribbon.
func set_points(points: PackedVector3Array, radius: PackedFloat32Array) -> void:
	var n: int = points.size()
	if n < 2 or radius.size() < n:
		_mesh.clear_surfaces()
		return
	var rings: int = BASE_CAP + n + TIP_CAP
	_ring_c.resize(rings)
	_ring_r.resize(rings)
	_ring_axial.resize(rings)
	_ring_t.resize(rings)
	_ring_s.resize(rings)
	# tangents
	var t_first := Vector3.FORWARD
	var t_last := Vector3.FORWARD
	var s: float = 0.0
	for i in n:
		var a: Vector3 = points[maxi(i - 1, 0)]
		var b: Vector3 = points[mini(i + 1, n - 1)]
		var t: Vector3 = b - a
		t = t.normalized() if t.length_squared() > 1e-10 else Vector3.FORWARD
		if i > 0:
			s += points[i].distance_to(points[i - 1])
		var r: int = BASE_CAP + i
		_ring_c[r] = points[i]
		_ring_r[r] = maxf(radius[i], 0.002)
		_ring_axial[r] = 0.0
		_ring_t[r] = t
		_ring_s[r] = s
		if i == 0:
			t_first = t
		if i == n - 1:
			t_last = t
	# base cap (quarter spheres, rounded)
	var r0: float = _ring_r[BASE_CAP]
	for j in BASE_CAP:
		var phi: float = float(BASE_CAP - j) / float(BASE_CAP) * PI * 0.5
		_ring_c[j] = points[0] - t_first * (r0 * sin(phi) * 0.8)
		_ring_r[j] = maxf(r0 * cos(phi), 0.002)
		_ring_axial[j] = -sin(phi)
		_ring_t[j] = t_first
		_ring_s[j] = -r0 * sin(phi) * 0.8
	# tip cap
	var rl: float = _ring_r[BASE_CAP + n - 1]
	for j in range(1, TIP_CAP + 1):
		var phi2: float = float(j) / float(TIP_CAP) * PI * 0.5
		var r: int = BASE_CAP + n - 1 + j
		_ring_c[r] = points[n - 1] + t_last * (rl * sin(phi2) * 1.1)
		_ring_r[r] = maxf(rl * cos(phi2), 0.002)
		_ring_axial[r] = sin(phi2)
		_ring_t[r] = t_last
		_ring_s[r] = s + rl * sin(phi2) * 1.1
	var total: float = maxf(_ring_s[rings - 1] - _ring_s[0], 0.001)

	var cols: int = SIDES + 1
	var vcount: int = rings * cols
	_verts.resize(vcount)
	_norms.resize(vcount)
	_uv.resize(vcount)
	_cust.resize(vcount * 4)
	# rotation-minimising frame: carry the normal along the curve
	var nrm: Vector3 = _ring_t[0].cross(Vector3.UP)
	if nrm.length_squared() < 1e-4:
		nrm = _ring_t[0].cross(Vector3.RIGHT)
	nrm = nrm.normalized()
	for r in rings:
		var t2: Vector3 = _ring_t[r]
		nrm = (nrm - t2 * nrm.dot(t2))
		nrm = nrm.normalized() if nrm.length_squared() > 1e-8 else t2.cross(Vector3.RIGHT).normalized()
		var bn: Vector3 = t2.cross(nrm)
		var c: Vector3 = _ring_c[r]
		var rad: float = _ring_r[r]
		var ax: float = _ring_axial[r]
		var radial_w: float = sqrt(maxf(1.0 - ax * ax, 0.0))
		for k in cols:
			var ang: float = float(k) / float(SIDES) * TAU
			var dir: Vector3 = nrm * cos(ang) + bn * sin(ang)
			var vi: int = r * cols + k
			_verts[vi] = c + dir * rad
			_norms[vi] = (dir * radial_w + t2 * ax).normalized()
			_uv[vi] = Vector2(_ring_s[r], float(k) / float(SIDES))
			var ci: int = vi * 4
			_cust[ci] = float(r)
			_cust[ci + 1] = float(k % SIDES)
			_cust[ci + 2] = rad
			_cust[ci + 3] = (_ring_s[r] - _ring_s[0]) / total
	if _built_rings != rings:
		_idx.resize((rings - 1) * SIDES * 6)
		var o: int = 0
		for r in rings - 1:
			for k in SIDES:
				var a: int = r * cols + k
				var b: int = a + cols
				# clockwise seen from outside
				_idx[o] = a
				_idx[o + 1] = b
				_idx[o + 2] = a + 1
				_idx[o + 3] = a + 1
				_idx[o + 4] = b
				_idx[o + 5] = b + 1
				o += 6
		_built_rings = rings
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = _verts
	arrays[Mesh.ARRAY_NORMAL] = _norms
	arrays[Mesh.ARRAY_TEX_UV] = _uv
	arrays[Mesh.ARRAY_CUSTOM0] = _cust
	arrays[Mesh.ARRAY_INDEX] = _idx
	_mesh.clear_surfaces()
	_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, VfxMesh.CUSTOM0_FLAGS)
	visible = true


## 0 = water, 1 = ice.
func set_state(frozen01: float) -> void:
	_frozen = clampf(frozen01, 0.0, 1.0)
	_mat.set_shader_parameter("frozen", _frozen)


func get_frozen() -> float:
	return _frozen


func reset() -> void:
	_mesh.clear_surfaces()
	_built_rings = -1
	_frozen = 0.0
	_flow = 0.0
	_mat.set_shader_parameter("frozen", 0.0)
	_mat.set_shader_parameter("flow", 0.0)
	visible = false


func advance(dt: float) -> void:
	_flow += dt * flow_speed * (1.0 - _frozen)
	_mat.set_shader_parameter("flow", _flow)
