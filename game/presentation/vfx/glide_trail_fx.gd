class_name GlideTrailFX
extends VfxEffect
## Wind trail for gliding / fast air movement: a soft camera-facing ribbon that follows a point.
## The ribbon keeps the last `POINTS` positions (sampled every `sample_interval`), fades with age and
## is rebuilt once per frame from reused arrays (<= 2 x 24 vertices, one draw call).
##
##   begin(follow)     start trailing a Node3D (its global_position is sampled each frame)
##   push(pos)         manual feeding (use instead of follow when the caller owns the position)
##   end()             stop emitting; the trail fades out and the effect finishes
##   set_width(w)      ribbon width in metres (default 0.5)
##
## The node is top_level (world space geometry).

const POINTS: int = 24

@export var sample_interval: float = 0.025
@export var max_age: float = 0.7
@export var width: float = 0.5

var _mesh: ArrayMesh
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _follow: Node3D = null
var _emitting: bool = false
var _pos: PackedVector3Array = PackedVector3Array()
var _stamp: PackedFloat32Array = PackedFloat32Array()
var _count: int = 0
var _clock: float = 0.0
var _since_sample: float = 0.0
var _flow: float = 0.0

var _verts: PackedVector3Array = PackedVector3Array()
var _tans: PackedVector3Array = PackedVector3Array()
var _uv: PackedVector2Array = PackedVector2Array()
var _col: PackedColorArray = PackedColorArray()
var _idx: PackedInt32Array = PackedInt32Array()


func _init() -> void:
	top_level = true
	_mesh = ArrayMesh.new()
	_mat = VfxMaterials.make("glide_trail")
	_mat.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_mi = MeshInstance3D.new()
	_mi.name = "Ribbon"
	_mi.mesh = _mesh
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_mi.extra_cull_margin = 4.0
	add_child(_mi)
	_pos.resize(POINTS)
	_stamp.resize(POINTS)
	reset()


func begin(follow: Node3D = null) -> void:
	top_level = true
	_place(Transform3D.IDENTITY)
	_follow = follow
	_emitting = true
	_count = 0
	_since_sample = sample_interval
	visible = true
	set_process(true)
	if follow != null and follow.is_inside_tree():
		push(follow.global_position)


func end() -> void:
	_emitting = false
	_follow = null


func set_width(w: float) -> void:
	width = maxf(w, 0.02)


func is_playing() -> bool:
	return _emitting or _count > 0


## Add a point now (newest last in the ring).
func push(world_pos: Vector3) -> void:
	if _count == POINTS:
		for i in POINTS - 1:
			_pos[i] = _pos[i + 1]
			_stamp[i] = _stamp[i + 1]
		_count -= 1
	_pos[_count] = world_pos
	_stamp[_count] = _clock
	_count += 1


func reset() -> void:
	_emitting = false
	_follow = null
	_count = 0
	_clock = 0.0
	_mesh.clear_surfaces()
	visible = false
	set_process(false)


func advance(dt: float) -> void:
	_clock += dt
	_flow += dt * 1.5
	if _emitting:
		_since_sample += dt
		if _follow != null and is_instance_valid(_follow) and _follow.is_inside_tree() and _since_sample >= sample_interval:
			_since_sample = 0.0
			push(_follow.global_position)
	# drop expired points from the old end
	while _count > 0 and _clock - _stamp[0] > max_age:
		for i in _count - 1:
			_pos[i] = _pos[i + 1]
			_stamp[i] = _stamp[i + 1]
		_count -= 1
	if not _emitting and _count == 0:
		_mesh.clear_surfaces()
		visible = false
		_finish()
		return
	_rebuild()


func _rebuild() -> void:
	if _count < 2:
		_mesh.clear_surfaces()
		return
	var n: int = _count
	_verts.resize(n * 2)
	_tans.resize(n * 2)
	_uv.resize(n * 2)
	_col.resize(n * 2)
	var dist: float = 0.0
	for i in n:
		var a: Vector3 = _pos[maxi(i - 1, 0)]
		var b: Vector3 = _pos[mini(i + 1, n - 1)]
		var t: Vector3 = b - a
		t = t.normalized() if t.length_squared() > 1e-10 else Vector3.FORWARD
		if i > 0:
			dist += _pos[i].distance_to(_pos[i - 1])
		var age01: float = clampf((_clock - _stamp[i]) / max_age, 0.0, 1.0)
		var taper: float = lerpf(1.0, 0.25, age01)
		var c := Color(clampf(width * taper / 1.0, 0.0, 1.0), age01, 0.0, 1.0)
		for s in 2:
			var vi: int = i * 2 + s
			_verts[vi] = _pos[i]
			_tans[vi] = t
			_uv[vi] = Vector2(float(s), dist)
			_col[vi] = c
	var quads: int = n - 1
	if _idx.size() != quads * 6:
		_idx.resize(quads * 6)
		var o: int = 0
		for i in quads:
			var q: int = i * 2
			_idx[o] = q
			_idx[o + 1] = q + 1
			_idx[o + 2] = q + 2
			_idx[o + 3] = q + 1
			_idx[o + 4] = q + 3
			_idx[o + 5] = q + 2
			o += 6
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = _verts
	arrays[Mesh.ARRAY_NORMAL] = _tans
	arrays[Mesh.ARRAY_TEX_UV] = _uv
	arrays[Mesh.ARRAY_COLOR] = _col
	arrays[Mesh.ARRAY_INDEX] = _idx
	_mesh.clear_surfaces()
	_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	_mat.set_shader_parameter("flow", _flow)
