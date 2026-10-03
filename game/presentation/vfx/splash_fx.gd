class_name SplashFX
extends VfxParticleEffect
## Water contact spray: fast droplets (gravity) + a few soft mist puffs. play(pos, normal, strength).
## 22 droplets + 4 mist puffs max, ~0.7 s.

const DROPS: int = 22
const MIST: int = 4

var _drops: GPUParticles3D
var _drop_pm: ParticleProcessMaterial
var _drop_mat: ShaderMaterial
var _mist: GPUParticles3D
var _mist_pm: ParticleProcessMaterial
var _mist_mat: ShaderMaterial


func _build() -> void:
	_drops = GPUParticles3D.new()
	_drops.amount = DROPS
	_drops.lifetime = 0.7
	_drops.explosiveness = 1.0
	_drops.randomness = 0.3
	_drops.visibility_aabb = AABB(Vector3(-3, -1, -3), Vector3(6, 5, 6))
	_drops.draw_pass_1 = make_quad()
	_drop_mat = VfxMaterials.make("puff")
	_drop_mat.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_drop_mat.set_shader_parameter("tint", Vector3(0.78, 0.9, 1.0))
	_drop_mat.set_shader_parameter("erosion", 0.05)
	_drop_mat.set_shader_parameter("soft_edge", 0.35)
	_drop_mat.set_shader_parameter("light_top", 0.5)
	_drops.material_override = _drop_mat
	_drop_pm = ParticleProcessMaterial.new()
	_drop_pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	_drop_pm.emission_sphere_radius = 0.1
	_drop_pm.direction = Vector3.UP
	_drop_pm.spread = 48.0
	_drop_pm.initial_velocity_min = 1.8
	_drop_pm.initial_velocity_max = 4.4
	_drop_pm.gravity = Vector3(0.0, -9.8, 0.0)
	_drop_pm.scale_min = 0.03
	_drop_pm.scale_max = 0.075
	_drop_pm.scale_curve = curve_tex([Vector2(0, 1.0), Vector2(0.7, 0.9), Vector2(1, 0.3)])
	_drop_pm.color_ramp = ramp_tex(PackedFloat32Array([0.0, 0.08, 0.7, 1.0]),
		PackedColorArray([Color(1, 1, 1, 0.0), Color(1, 1, 1, 0.9), Color(1, 1, 1, 0.7), Color(1, 1, 1, 0.0)]))
	_drops.process_material = _drop_pm
	_add_system(_drops)

	_mist = GPUParticles3D.new()
	_mist.amount = MIST
	_mist.lifetime = 0.7
	_mist.explosiveness = 0.9
	_mist.visibility_aabb = AABB(Vector3(-2, -1, -2), Vector3(4, 3, 4))
	_mist.draw_pass_1 = make_quad()
	_mist_mat = VfxMaterials.make("puff")
	_mist_mat.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_mist_mat.set_shader_parameter("tint", Vector3(0.86, 0.93, 0.98))
	_mist_mat.set_shader_parameter("erosion", 0.6)
	_mist.material_override = _mist_mat
	_mist_pm = ParticleProcessMaterial.new()
	_mist_pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	_mist_pm.emission_sphere_radius = 0.12
	_mist_pm.direction = Vector3.UP
	_mist_pm.spread = 60.0
	_mist_pm.initial_velocity_min = 0.4
	_mist_pm.initial_velocity_max = 1.1
	_mist_pm.gravity = Vector3(0.0, 0.2, 0.0)
	_mist_pm.damping_min = 1.5
	_mist_pm.damping_max = 2.5
	_mist_pm.angle_min = -180.0
	_mist_pm.angle_max = 180.0
	_mist_pm.scale_min = 0.22
	_mist_pm.scale_max = 0.4
	_mist_pm.scale_curve = curve_tex([Vector2(0, 0.5), Vector2(1, 1.5)])
	_mist_pm.color_ramp = ramp_tex(PackedFloat32Array([0.0, 0.2, 1.0]),
		PackedColorArray([Color(1, 1, 1, 0.0), Color(1, 1, 1, 0.3), Color(1, 1, 1, 0.0)]))
	_mist.process_material = _mist_pm
	_add_system(_mist)


func play(pos: Vector3, normal: Vector3 = Vector3.UP, strength: float = 1.0) -> void:
	strength = clampf(strength, 0.1, 2.0)
	var n: Vector3 = normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	_place_at(pos + n * 0.02)
	_drop_pm.direction = n
	_mist_pm.direction = n
	_drop_pm.initial_velocity_min = 1.2 + 0.8 * strength
	_drop_pm.initial_velocity_max = 3.0 + 1.6 * strength
	_drops.amount_ratio = clampf(0.35 + 0.65 * strength, 0.3, 1.0)
	_mist.amount_ratio = clampf(0.4 + 0.6 * strength, 0.3, 1.0)
	_begin(0.8)
