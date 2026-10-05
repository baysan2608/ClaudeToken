class_name FireBurstFX
extends VfxEffect
## Short flame burst along a direction: two additive mesh-flame layers (outer + hot core, 2 draw
## calls, ~730 tris) plus a brief OmniLight3D whose energy follows a fast attack / fast decay curve.
## No particles, no persistent glow. ~0.55 s.
##
##   play(origin, dir, length, intensity)   intensity 0..1+ scales brightness, width and light.

const DURATION: float = 0.55
const LIGHT_PEAK: float = 3.0

var _body: Node3D
var _outer: MeshInstance3D
var _inner: MeshInstance3D
var _mat_outer: ShaderMaterial
var _mat_inner: ShaderMaterial
var _light: OmniLight3D
var _age: float = 0.0
var _playing: bool = false
var _intensity: float = 1.0
var _length: float = 3.0
var _blue: bool = false


func _init() -> void:
	_body = Node3D.new()
	_body.name = "Body"
	add_child(_body)
	var mesh: ArrayMesh = VfxMesh.flame_mesh()
	_mat_outer = _make_mat(0.0)
	_mat_inner = _make_mat(1.0)
	_outer = _make_mi(mesh, _mat_outer)
	_inner = _make_mi(mesh, _mat_inner)
	_body.add_child(_outer)
	_body.add_child(_inner)
	_light = OmniLight3D.new()
	_light.name = "Flash"
	_light.shadow_enabled = false
	_light.light_color = Color(1.0, 0.52, 0.18)
	_light.omni_attenuation = 2.0
	_light.light_energy = 0.0
	_light.visible = false
	add_child(_light)
	reset()


func _make_mat(core_value: float) -> ShaderMaterial:
	var m: ShaderMaterial = VfxMaterials.make("flame")
	m.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	m.set_shader_parameter("core", core_value)
	m.set_shader_parameter("cover", 0.25 if core_value > 0.5 else 0.8)
	m.set_shader_parameter("loop", 0.0)
	m.render_priority = 1 if core_value > 0.5 else 0
	return m


func _make_mi(mesh: ArrayMesh, mat: ShaderMaterial) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return mi


## Blue fire palette for the next play (Fire / Blue cones, comets, columns). Reset on release.
func set_blue(on: bool) -> void:
	if on == _blue:
		return
	_blue = on
	var cols: Array = [Color(0.10, 0.12, 0.55), Color(0.25, 0.45, 1.0), Color(0.55, 0.85, 1.0), Color(0.95, 0.98, 1.0)] if on \
		else [Color(0.62, 0.07, 0.01), Color(1.0, 0.36, 0.05), Color(1.0, 0.78, 0.30), Color(1.0, 0.95, 0.75)]
	for m in [_mat_outer, _mat_inner]:
		for i in 4:
			(m as ShaderMaterial).set_shader_parameter(["col_deep", "col_mid", "col_hot", "col_white"][i], VfxPalette.v3(cols[i]))
	_light.light_color = Color(0.5, 0.7, 1.0) if on else Color(1.0, 0.52, 0.18)


func play(origin: Vector3, dir: Vector3, length: float, intensity: float = 1.0) -> void:
	var d: Vector3 = dir.normalized() if dir.length_squared() > 1e-8 else Vector3.FORWARD
	_length = maxf(length, 0.2)
	_intensity = clampf(intensity, 0.1, 2.0)
	var q: Quaternion = _quat_up_to(d)
	_place(Transform3D(Basis(q), origin))
	var w: float = _length * 0.17 * (0.75 + 0.35 * _intensity)
	_body.scale = Vector3(w, _length, w)
	var seed_value: float = fmod(origin.x * 0.37 + origin.z * 0.61 + d.x * 3.1, 1.0)
	seed_value = absf(seed_value)
	_mat_outer.set_shader_parameter("seed", seed_value)
	_mat_inner.set_shader_parameter("seed", fmod(seed_value + 0.37, 1.0))
	_mat_outer.set_shader_parameter("intensity", _intensity)
	_mat_inner.set_shader_parameter("intensity", _intensity)
	_light.position = Vector3(0.0, _length * 0.42, 0.0)
	_light.omni_range = 2.5 + _length * 1.1
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
	if _blue:
		set_blue(false)
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
	_mat_outer.set_shader_parameter("age", n)
	_mat_inner.set_shader_parameter("age", n)
	# fast attack (~15 ms), fast exponential decay: bright for the first ~120 ms only
	var e: float = (1.0 - exp(-t * 70.0)) * exp(-t * 7.5)
	_light.light_energy = LIGHT_PEAK * _intensity * e
	_light.visible = _light.light_energy > 0.02


static func _quat_up_to(d: Vector3) -> Quaternion:
	var dot: float = Vector3.UP.dot(d)
	if dot > 0.9999:
		return Quaternion.IDENTITY
	if dot < -0.9999:
		return Quaternion(Vector3.RIGHT, PI)
	return Quaternion(Vector3.UP.cross(d).normalized(), acos(dot))
