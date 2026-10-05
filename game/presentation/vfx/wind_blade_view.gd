class_name WindBladeView
extends VfxEffect
## Barely visible air shapes (Air / Gust): crescent blade (flying arc), wind wall (standing curved
## sheet), downdraft / dust-line crest. crescent.gdshader: fast speed streaks + a soft leading edge,
## dust tinted, premultiplied, one layer.
##
##   setup(kind, size)     kind "crescent" (size.x = radius) | "wall" (size = half width, height)
##   set_motion(vel)       crescent: faces the travel direction, rolls with the cut
##   set_power(p01)

var kind := "crescent"
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _scroll := 0.0
var _power := 1.0
var _size := Vector3.ONE


func _init() -> void:
	_mat = VfxMaterials.make_fx("crescent")
	_mi = MeshInstance3D.new()
	_mi.name = "Blade"
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_mi.extra_cull_margin = 1.0
	add_child(_mi)
	reset()


func setup(kind_name: String, size: Vector3) -> void:
	kind = kind_name
	_size = size
	if kind == "wall":
		_mi.mesh = FxMesh.sheet_mesh()
		_mi.scale = Vector3(maxf(size.x, 0.3), maxf(size.y, 0.3), 1.0)
		_mat.set_shader_parameter("dusty", 0.55)
		_mat.set_shader_parameter("opacity", 0.55)
	else:
		_mi.mesh = FxMesh.crescent_mesh()
		_mi.scale = Vector3.ONE * maxf(size.x, 0.2)
		_mat.set_shader_parameter("dusty", 0.25)
		_mat.set_shader_parameter("opacity", 1.0)
		_mat.set_shader_parameter("cover", 0.5)
	_mat.set_shader_parameter("age", 0.5)


func set_motion(vel: Vector3) -> void:
	if kind != "crescent" or vel.length_squared() < 0.04:
		return
	var f := vel.normalized()
	var x := Vector3.UP.cross(f)
	x = x.normalized() if x.length_squared() > 1e-6 else Vector3.RIGHT
	var up := f.cross(x).normalized()
	# local +Z = travel, the arc lies in the local XZ plane (a horizontal cut)
	_mi.basis = Basis(x, up, f).scaled(Vector3.ONE * maxf(_size.x, 0.2))


func set_power(p01: float) -> void:
	_power = clampf(p01, 0.2, 1.5)
	_mat.set_shader_parameter("opacity", (0.55 if kind == "wall" else 1.0) * lerpf(0.7, 1.2, _power))


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)


func advance(dt: float) -> void:
	_scroll += dt * (2.2 if kind == "crescent" else 1.1)
	_mat.set_shader_parameter("scroll", _scroll)
