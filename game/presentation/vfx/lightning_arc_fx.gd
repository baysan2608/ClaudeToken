class_name LightningArcFX
extends VfxEffect
## Lightning along a gameplay conduction path: a jagged main bolt that passes exactly through the
## given points, plus 1-3 short branches, as ONE camera-facing additive ribbon mesh (billboarded in
## the vertex shader, so no per-frame rebuild). Lasts ~0.2 s with flicker and re-strike; a brief
## OmniLight3D flash with capped energy sits at the path midpoint.
##
##   strike(points, seed)    points: world-space path (>= 2 points); same seed -> same bolt.
##
## The node places itself at the world origin (top_level) because the geometry is in world space.

const DURATION: float = 0.2
const LIGHT_CAP: float = 2.2
const SEG_LEN: float = 0.32
const MAX_BRANCHES: int = 3

@export var half_width: float = 0.10
@export var intensity: float = 1.0

var _mesh: ArrayMesh
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _light: OmniLight3D
var _age: float = 0.0
var _playing: bool = false

var _verts: PackedVector3Array = PackedVector3Array()
var _tans: PackedVector3Array = PackedVector3Array()
var _uv: PackedVector2Array = PackedVector2Array()
var _col: PackedColorArray = PackedColorArray()
var _idx: PackedInt32Array = PackedInt32Array()
var _poly: PackedVector3Array = PackedVector3Array()
var _rng: RandomNumberGenerator = RandomNumberGenerator.new()


func _init() -> void:
	top_level = true
	_mesh = ArrayMesh.new()
	_mat = VfxMaterials.make("lightning")
	_mat.set_shader_parameter("duration", DURATION)
	_mi = MeshInstance3D.new()
	_mi.name = "Bolt"
	_mi.mesh = _mesh
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_mi.extra_cull_margin = 0.5
	add_child(_mi)
	_light = OmniLight3D.new()
	_light.name = "Flash"
	_light.shadow_enabled = false
	_light.light_color = Color(0.62, 0.72, 1.0)
	_light.omni_attenuation = 2.0
	_light.omni_range = 7.0
	_light.visible = false
	add_child(_light)
	reset()


func strike(points: PackedVector3Array, seed_value: int) -> void:
	if points.size() < 2:
		return
	top_level = true
	_place(Transform3D.IDENTITY)
	_rng.seed = hash(seed_value) ^ 0x2545F491
	_verts.clear()
	_tans.clear()
	_uv.clear()
	_col.clear()
	_idx.clear()
	# main bolt
	_build_main(points)
	_emit_polyline(_poly, half_width, 1.0, 1.0)
	var main_len: float = _poly_length(_poly)
	# branches
	var count: int = 0
	if main_len > 1.2:
		count = _rng.randi_range(1, MAX_BRANCHES)
	var main_poly: PackedVector3Array = _poly.duplicate()
	for b in count:
		var m: int = _rng.randi_range(maxi(1, int(main_poly.size() * 0.15)), maxi(2, int(main_poly.size() * 0.85)))
		m = clampi(m, 1, main_poly.size() - 2)
		var tdir: Vector3 = (main_poly[m + 1] - main_poly[m - 1]).normalized()
		_build_branch(main_poly[m], tdir, b)
		_emit_polyline(_poly, half_width * _rng.randf_range(0.42, 0.6), 0.25 + 0.5 * _rng.randf(), 0.8)
	_commit()
	# light flash at the midpoint of the main path
	var mid: Vector3 = main_poly[main_poly.size() / 2]
	_light.position = mid  # parent is top_level at the world origin
	_light.omni_range = clampf(4.0 + main_len * 0.35, 4.0, 9.0)
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	_apply(0.0)


func is_playing() -> bool:
	return _playing


func reset() -> void:
	_playing = false
	_age = 0.0
	_mesh.clear_surfaces()
	_light.visible = false
	_light.light_energy = 0.0
	visible = false
	set_process(false)


func advance(dt: float) -> void:
	if not _playing:
		return
	_age += dt
	if _age >= DURATION:
		_playing = false
		_light.visible = false
		visible = false
		_finish()
		return
	_apply(_age)


func _apply(t: float) -> void:
	var n: float = clampf(t / DURATION, 0.0, 1.0)
	_mat.set_shader_parameter("age", n)
	_mat.set_shader_parameter("intensity", intensity)
	# flash: instantaneous attack, decays inside ~80 ms, small flicker re-strike
	var flick: float = 0.65 + 0.35 * sin(t * 140.0)
	var restrike: float = 0.25 * exp(-(t - 0.09) * 30.0) if t >= 0.09 else 0.0
	var e: float = exp(-t * 22.0) * flick + restrike
	_light.light_energy = minf(LIGHT_CAP * intensity * e, LIGHT_CAP)
	_light.visible = _light.light_energy > 0.03


# ---- geometry ---------------------------------------------------------------------------
func _build_main(points: PackedVector3Array) -> void:
	_poly.clear()
	_poly.append(points[0])
	for i in range(1, points.size()):
		var a: Vector3 = points[i - 1]
		var b: Vector3 = points[i]
		var seg: Vector3 = b - a
		var l: float = seg.length()
		if l < 1e-4:
			continue
		var dir: Vector3 = seg / l
		var u: Vector3 = dir.cross(Vector3.UP)
		if u.length_squared() < 1e-4:
			u = dir.cross(Vector3.RIGHT)
		u = u.normalized()
		var v: Vector3 = dir.cross(u).normalized()
		var sub: int = maxi(2, int(ceil(l / SEG_LEN)))
		var amp: float = clampf(l * 0.11, 0.05, 0.24)
		var ou: float = 0.0
		var ov: float = 0.0
		for j in range(1, sub):
			var t: float = float(j) / float(sub)
			# random walk offset (smoother than white noise), pinned to zero at the path nodes
			ou = clampf(ou * 0.35 + _rng.randf_range(-1.0, 1.0) * amp, -amp * 1.6, amp * 1.6)
			ov = clampf(ov * 0.35 + _rng.randf_range(-1.0, 1.0) * amp, -amp * 1.6, amp * 1.6)
			var pin: float = pow(sin(PI * t), 0.7)
			_poly.append(a + seg * t + (u * ou + v * ov) * pin)
		_poly.append(b)


func _build_branch(start: Vector3, tdir: Vector3, index: int) -> void:
	_poly.clear()
	_poly.append(start)
	var u: Vector3 = tdir.cross(Vector3.UP)
	if u.length_squared() < 1e-4:
		u = tdir.cross(Vector3.RIGHT)
	u = u.normalized()
	var v: Vector3 = tdir.cross(u).normalized()
	var ang: float = deg_to_rad(_rng.randf_range(25.0, 55.0))
	var roll: float = _rng.randf_range(0.0, TAU)
	var side_dir: Vector3 = (u * cos(roll) + v * sin(roll))
	var dir: Vector3 = (tdir * cos(ang) + side_dir * sin(ang)).normalized()
	var steps: int = _rng.randi_range(4, 6)
	var seg_len: float = _rng.randf_range(0.22, 0.36)
	var p: Vector3 = start
	for s in steps:
		var jit := Vector3(_rng.randf_range(-1, 1), _rng.randf_range(-1, 1), _rng.randf_range(-1, 1)) * 0.38
		dir = (dir + jit).normalized()
		p += dir * seg_len
		_poly.append(p)


func _poly_length(poly: PackedVector3Array) -> float:
	var l: float = 0.0
	for i in range(1, poly.size()):
		l += poly[i].distance_to(poly[i - 1])
	return l


## Append one billboard ribbon (2 vertices per point) for `poly`.
func _emit_polyline(poly: PackedVector3Array, hw: float, life: float, var_amount: float) -> void:
	var n: int = poly.size()
	if n < 2:
		return
	var base: int = _verts.size()
	var dist: float = 0.0
	for i in n:
		var a: Vector3 = poly[maxi(i - 1, 0)]
		var b: Vector3 = poly[mini(i + 1, n - 1)]
		var t: Vector3 = (b - a)
		t = t.normalized() if t.length_squared() > 1e-10 else Vector3.UP
		if i > 0:
			dist += poly[i].distance_to(poly[i - 1])
		# branches taper toward their tips, the main bolt keeps its width
		var k: float = float(i) / float(n - 1)
		var w: float = hw if life >= 0.999 else hw * (1.0 - 0.75 * k)
		var c := Color(clampf(w / 0.25, 0.0, 1.0), life, 0.0, var_amount)
		for s in 2:
			_verts.append(poly[i])
			_tans.append(t)
			_uv.append(Vector2(float(s), dist))
			_col.append(c)
	for i in n - 1:
		var q: int = base + i * 2
		_idx.append_array(PackedInt32Array([q, q + 1, q + 2, q + 1, q + 3, q + 2]))


func _commit() -> void:
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = _verts
	arrays[Mesh.ARRAY_NORMAL] = _tans
	arrays[Mesh.ARRAY_TEX_UV] = _uv
	arrays[Mesh.ARRAY_COLOR] = _col
	arrays[Mesh.ARRAY_INDEX] = _idx
	_mesh.clear_surfaces()
	_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
