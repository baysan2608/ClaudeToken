class_name SteamFX
extends VfxParticleEffect
## Soft white rising steam puffs (water meeting hot rock / lava). play(pos, amount01).
## 10 particles max, one alpha layer, ~1.3 s.

const AMOUNT: int = 10

var _ps: GPUParticles3D
var _pm: ParticleProcessMaterial
var _mat: ShaderMaterial


func _build() -> void:
	_ps = GPUParticles3D.new()
	_ps.amount = AMOUNT
	_ps.lifetime = 1.2
	_ps.explosiveness = 0.55
	_ps.randomness = 0.5
	_ps.visibility_aabb = AABB(Vector3(-2, -0.5, -2), Vector3(4, 5, 4))
	_ps.draw_pass_1 = make_quad()
	_mat = VfxMaterials.make("puff")
	_mat.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_mat.set_shader_parameter("tint", Vector3(0.93, 0.95, 0.97))
	_mat.set_shader_parameter("erosion", 0.7)
	_mat.set_shader_parameter("light_top", 0.35)
	_ps.material_override = _mat
	_pm = ParticleProcessMaterial.new()
	_pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	_pm.emission_sphere_radius = 0.22
	_pm.direction = Vector3.UP
	_pm.spread = 22.0
	_pm.initial_velocity_min = 0.6
	_pm.initial_velocity_max = 1.4
	_pm.gravity = Vector3(0.0, 0.7, 0.0)
	_pm.damping_min = 0.6
	_pm.damping_max = 1.0
	_pm.angle_min = -180.0
	_pm.angle_max = 180.0
	_pm.angular_velocity_min = -12.0
	_pm.angular_velocity_max = 12.0
	_pm.scale_min = 0.35
	_pm.scale_max = 0.6
	_pm.scale_curve = curve_tex([Vector2(0, 0.5), Vector2(0.4, 1.0), Vector2(1, 1.7)])
	_pm.turbulence_enabled = true
	_pm.turbulence_noise_strength = 0.6
	_pm.turbulence_noise_scale = 2.0
	_pm.color_ramp = ramp_tex(PackedFloat32Array([0.0, 0.18, 0.6, 1.0]),
		PackedColorArray([Color(1, 1, 1, 0.0), Color(1, 1, 1, 0.42), Color(1, 1, 1, 0.22), Color(1, 1, 1, 0.0)]))
	_ps.process_material = _pm
	_add_system(_ps)


## pos: where steam is released, amount01: 0..1 (scales count and size).
func play(pos: Vector3, amount01: float = 1.0) -> void:
	amount01 = clampf(amount01, 0.05, 1.0)
	_place_at(pos)
	_ps.amount_ratio = clampf(0.25 + 0.75 * amount01, 0.2, 1.0)
	_pm.scale_min = 0.25 + 0.2 * amount01
	_pm.scale_max = 0.4 + 0.3 * amount01
	_pm.initial_velocity_max = 0.9 + 0.7 * amount01
	_begin(1.4)
