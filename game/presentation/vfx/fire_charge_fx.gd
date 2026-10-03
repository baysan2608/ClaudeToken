class_name FireChargeFX
extends VfxEffect
## Held flame at the hands while charging. set_charge(t01): 0 = off, 1 = full flame + light.
## Same two-layer mesh flame as FireBurstFX in steady "loop" mode (teardrop shape), plus a small
## flickering OmniLight3D. 2 draw calls, ~730 tris. Parent it to the hand node.
##
## `direction` is the flame axis in this node's local space (default up).

@export var base_size: float = 0.34
@export var direction: Vector3 = Vector3.UP

var _body: Node3D
var _mat_outer: ShaderMaterial
var _mat_inner: ShaderMaterial
var _light: OmniLight3D
var _charge: float = 0.0
var _scroll: float = 0.0


func _init() -> void:
	_body = Node3D.new()
	_body.name = "Body"
	add_child(_body)
	var mesh: ArrayMesh = VfxMesh.flame_mesh()
	_mat_outer = _make_mat(0.0)
	_mat_inner = _make_mat(1.0)
	for m in [_mat_outer, _mat_inner]:
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.material_override = m
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		_body.add_child(mi)
	_light = OmniLight3D.new()
	_light.name = "Glow"
	_light.shadow_enabled = false
	_light.light_color = Color(1.0, 0.5, 0.16)
	_light.omni_attenuation = 2.0
	_light.omni_range = 3.0
	_light.visible = false
	add_child(_light)
	set_charge(0.0)


func _make_mat(core_value: float) -> ShaderMaterial:
	var m: ShaderMaterial = VfxMaterials.make("flame")
	m.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	m.set_shader_parameter("core", core_value)
	m.set_shader_parameter("cover", 0.25 if core_value > 0.5 else 0.8)
	m.set_shader_parameter("loop", 1.0)
	m.set_shader_parameter("shape", 1.0)
	m.set_shader_parameter("seed", 0.2 + core_value * 0.35)
	m.render_priority = 1 if core_value > 0.5 else 0
	return m


func set_charge(t01: float) -> void:
	_charge = clampf(t01, 0.0, 1.0)
	var on: bool = _charge > 0.01
	visible = on
	set_process(on and not manual_time)
	if not on:
		_light.visible = false
		return
	var d: Vector3 = direction.normalized() if direction.length_squared() > 1e-6 else Vector3.UP
	var q: Quaternion = FireBurstFX._quat_up_to(d)
	var s: float = base_size * (0.35 + 0.65 * _charge)
	_body.transform = Transform3D(Basis(q) * Basis.from_scale(Vector3(s * 0.52, s * 2.0, s * 0.52)), Vector3.ZERO)
	var inten: float = 0.35 + 0.65 * _charge
	_mat_outer.set_shader_parameter("intensity", inten)
	_mat_inner.set_shader_parameter("intensity", inten)
	_light.position = d * s * 0.7
	_light.visible = true
	_apply_flicker()


func get_charge() -> float:
	return _charge


func reset() -> void:
	_scroll = 0.0
	set_charge(0.0)


func advance(dt: float) -> void:
	if _charge <= 0.01:
		return
	_scroll += dt
	_mat_outer.set_shader_parameter("scroll", _scroll)
	_mat_inner.set_shader_parameter("scroll", _scroll)
	_apply_flicker()


func _apply_flicker() -> void:
	var f: float = 0.86 + 0.09 * sin(_scroll * 31.0) + 0.05 * sin(_scroll * 53.0 + 1.3)
	_light.light_energy = (0.15 + 1.65 * _charge * _charge) * f
	_light.omni_range = 1.8 + 2.4 * _charge
