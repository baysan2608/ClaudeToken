class_name WaterBlobView
extends VfxEffect
## A held water orb (wobbling sphere) that can freeze into a faceted ice ball.
##   setup(radius), set_state(frozen01), set_wobble(amount)
## Same shader / single transparent layer as WaterRibbonView.

static var _sphere: SphereMesh = null

var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _radius: float = 0.3
var _frozen: float = 0.0
var _flow: float = 0.0


func _init() -> void:
	if _sphere == null:
		_sphere = SphereMesh.new()
		_sphere.radius = 1.0
		_sphere.height = 2.0
		_sphere.radial_segments = 24
		_sphere.rings = 12
	_mat = VfxMaterials.make_water()
	_mat.set_shader_parameter("blob", 1.0)
	_mi = MeshInstance3D.new()
	_mi.name = "Orb"
	_mi.mesh = _sphere
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_mi.custom_aabb = AABB(Vector3(-1.4, -1.4, -1.4), Vector3(2.8, 2.8, 2.8))
	add_child(_mi)
	setup(_radius)


func setup(radius: float) -> void:
	_radius = maxf(radius, 0.01)
	_mi.scale = Vector3.ONE * _radius


func set_state(frozen01: float) -> void:
	_frozen = clampf(frozen01, 0.0, 1.0)
	_mat.set_shader_parameter("frozen", _frozen)


func set_wobble(amount: float) -> void:
	_mat.set_shader_parameter("wobble", amount)


func get_radius() -> float:
	return _radius


func on_acquire() -> void:
	visible = true


func reset() -> void:
	_frozen = 0.0
	_flow = 0.0
	_mat.set_shader_parameter("frozen", 0.0)
	_mat.set_shader_parameter("flow", 0.0)
	visible = false


func advance(dt: float) -> void:
	_flow += dt * (1.0 - _frozen)
	_mat.set_shader_parameter("flow", _flow)
