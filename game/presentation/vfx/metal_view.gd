class_name MetalView
extends VfxEffect
## Blue-steel bodies (Earth / Metal): disc (spins, blur ring), lance, rod, plate (projectile or
## standing Aegis plate), orb, and a caltrop field (one MultiMesh). Red-hot with heat.
##
##   setup(shape, seed, size, field_radius = 0)   shape "disc" | "lance" | "rod" | "plate" | "orb" | "caltrops"
##   set_state(heat01, spin_rad_s)                spin turns the disc about its local Y and fades in the blur ring
##
## Opaque metal (metal.gdshader, 1 draw) + for discs one premultiplied blur ring (ring.gdshader).

const FIELD_MAX := 14

var shape := "disc"
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _blur: MeshInstance3D
var _blur_mat: ShaderMaterial
var _field: MultiMeshInstance3D
var _heat := -1.0
var _spin := 0.0
var _angle := 0.0


func _init() -> void:
	_mat = VfxMaterials.make_fx("metal")
	_mi = MeshInstance3D.new()
	_mi.name = "Metal"
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	add_child(_mi)
	_blur_mat = VfxMaterials.make_fx("ring")
	_blur_mat.set_shader_parameter("style", 3.0)
	_blur_mat.set_shader_parameter("radius", 0.82)
	_blur_mat.set_shader_parameter("width", 0.16)
	_blur_mat.set_shader_parameter("cover", 0.55)
	_blur_mat.set_shader_parameter("color", Vector3(0.75, 0.82, 0.92))
	_blur = MeshInstance3D.new()
	_blur.name = "SpinBlur"
	_blur.mesh = FxMesh.ground_quad()
	_blur.material_override = _blur_mat
	_blur.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_blur.visible = false
	add_child(_blur)
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = FxMesh.caltrop_mesh()
	mm.instance_count = FIELD_MAX
	mm.visible_instance_count = 0
	_field = MultiMeshInstance3D.new()
	_field.name = "Caltrops"
	_field.multimesh = mm
	_field.material_override = _mat
	_field.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_field)
	reset()


func setup(shape_name: String, seed_value: int, size: Vector3, field_radius: float = 0.0) -> void:
	shape = shape_name
	_mat.set_shader_parameter("seed", float(absi(seed_value) % 101) * 0.1)
	_mi.visible = shape != "caltrops"
	_blur.visible = false
	_field.multimesh.visible_instance_count = 0
	_mi.rotation = Vector3.ZERO
	match shape:
		"disc":
			_mi.mesh = FxMesh.disc_mesh()
			_mi.scale = Vector3(size.x, size.x, size.x)
			_blur.scale = Vector3(size.x * 1.25, 1.0, size.x * 1.25)
		"lance", "rod":
			_mi.mesh = FxMesh.lance_mesh() if shape == "lance" else FxMesh.rod_mesh()
			_mi.scale = Vector3(size.x, size.y, size.x)
		"plate":
			var bm := BoxMesh.new()
			bm.size = Vector3.ONE
			_mi.mesh = bm
			_mi.scale = size
		"orb":
			_mi.mesh = FxMesh.sphere_mesh(1)
			_mi.scale = Vector3.ONE * size.x
		"caltrops":
			var rng := RandomNumberGenerator.new()
			rng.seed = seed_value * 31 + 7
			var n: int = clampi(int(field_radius * field_radius * 3.0) + 4, 4, FIELD_MAX)
			var mm := _field.multimesh
			for i in n:
				var a := rng.randf() * TAU
				var r := sqrt(rng.randf()) * field_radius
				var b := Basis(Vector3.UP, rng.randf() * TAU).scaled(Vector3.ONE * rng.randf_range(0.15, 0.21))
				mm.set_instance_transform(i, Transform3D(b, Vector3(cos(a) * r, 0.04, sin(a) * r)))
			mm.visible_instance_count = n
			var e := field_radius + 0.5
			_field.custom_aabb = AABB(Vector3(-e, -0.2, -e), Vector3(2.0 * e, 0.6, 2.0 * e))
	_heat = -1.0
	set_state(0.0, 0.0)


func set_state(heat01: float, spin: float) -> void:
	heat01 = clampf(heat01, 0.0, 1.0)
	if heat01 != _heat:
		_heat = heat01
		_mat.set_shader_parameter("heat", heat01)
	_spin = spin
	if shape == "disc":
		var b := clampf((absf(spin) - 6.0) / 30.0, 0.0, 1.0)
		_blur.visible = b > 0.02
		_blur_mat.set_shader_parameter("alpha", b * 0.8)


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_spin = 0.0
	rotation = Vector3.ZERO
	scale = Vector3.ONE


func advance(dt: float) -> void:
	if shape == "disc" and _spin != 0.0:
		_angle = wrapf(_angle + _spin * dt, -PI, PI)
		_mi.rotation.y = _angle
		_blur_mat.set_shader_parameter("phase", _angle)
