class_name StoneView
extends Node3D
## A rock chunk whose look is driven entirely by plain parameters, so the same instance can go
## stone -> heated -> molten blob -> (ground wave handled by LavaWaveView) -> crusted -> cooled rock
## without ever being despawned.
##
## Usage: setup(seed, radius) once, then every frame set_thermal(heat, melt) and set_crust(c).
## All setters are cheap and change-guarded; the material is shared (instance uniforms).

## Optional warm light on the surroundings (single OmniLight3D, no shadows). Disable for crowds.
@export var use_light: bool = true

var _mesh_inst: MeshInstance3D
var _light: OmniLight3D
var _radius: float = 0.4
var _seed: int = 0
var _heat: float = 0.0
var _melt: float = 0.0
var _crust: float = 0.0


func _init() -> void:
	_mesh_inst = MeshInstance3D.new()
	_mesh_inst.name = "Mesh"
	_mesh_inst.material_override = VfxMaterials.stone()
	_mesh_inst.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	add_child(_mesh_inst)
	_light = OmniLight3D.new()
	_light.name = "Glow"
	_light.shadow_enabled = false
	_light.light_color = Color(1.0, 0.45, 0.16)
	_light.omni_attenuation = 1.7
	_light.visible = false
	add_child(_light)
	setup(0, _radius)


## Builds the (deterministic, cached) rock mesh for `seed_value` scaled to `radius` metres.
func setup(seed_value: int, radius: float) -> void:
	_seed = seed_value
	_radius = maxf(radius, 0.01)
	_mesh_inst.mesh = VfxMesh.rock_mesh(seed_value)
	_mesh_inst.scale = Vector3.ONE * _radius
	_apply()


func _enter_tree() -> void:
	# Instance uniforms written before the MeshInstance3D is registered in the scene are not
	# reliable (and children enter the tree after this callback): re-push everything deferred.
	_push_params.call_deferred()


## heat01: glowing crack network (0 = cold stone). melt01: softens into a molten blob.
func set_thermal(heat01: float, melt01: float) -> void:
	heat01 = clampf(heat01, 0.0, 1.0)
	melt01 = clampf(melt01, 0.0, 1.0)
	if is_equal_approx(heat01, _heat) and is_equal_approx(melt01, _melt):
		return
	_heat = heat01
	_melt = melt01
	_apply()


## Cooling crust: 0 = bare molten surface, 1 = fully crusted dark rock with dull residual glow.
func set_crust(c: float) -> void:
	c = clampf(c, 0.0, 1.0)
	if is_equal_approx(c, _crust):
		return
	_crust = c
	_apply()


## Back to cold stone (for pooling).
func reset() -> void:
	_heat = 0.0
	_melt = 0.0
	_crust = 0.0
	_apply()


func get_radius() -> float:
	return _radius


func get_heat() -> float:
	return _heat


func get_melt() -> float:
	return _melt


func get_crust() -> float:
	return _crust


func _apply() -> void:
	_push_params()
	var glow: float = clampf(0.75 * _heat + 0.45 * _melt, 0.0, 1.0) * (1.0 - 0.8 * _crust)
	if use_light and glow > 0.12:
		_light.visible = true
		_light.light_energy = glow * glow * 1.5 * clampf(_radius / 0.4, 0.5, 2.0)
		_light.omni_range = _radius * 7.0
		_light.light_color = Color(1.0, 0.36 + 0.3 * glow, 0.1 + 0.12 * glow)
	else:
		_light.visible = false


## Every instance uniform is written explicitly: unset instance uniforms are NOT reliably
## initialised to the shader default (stale instance-buffer data can show through).
func _push_params() -> void:
	var mi: MeshInstance3D = _mesh_inst
	mi.set_instance_shader_parameter("u_seed", float(absi(_seed) % 977) + 0.5)
	mi.set_instance_shader_parameter("u_heat", _heat)
	mi.set_instance_shader_parameter("u_melt", _melt)
	mi.set_instance_shader_parameter("u_crust", _crust)
	mi.set_instance_shader_parameter("u_damage", 0.0)
	mi.set_instance_shader_parameter("u_rise", 1.0)
	mi.set_instance_shader_parameter("u_rise_height", 1.0)
	mi.set_instance_shader_parameter("u_detail", 1.0)
