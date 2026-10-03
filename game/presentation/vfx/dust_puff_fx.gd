class_name DustPuffFX
extends VfxParticleEffect
## Impact / landing / stone-rip dust. play(pos, normal, strength): a short ring-shaped burst of soft
## grey-brown puffs that expand and settle. 14 particles max, one alpha layer, ~0.9 s.

const AMOUNT: int = 14

var _ps: GPUParticles3D
var _pm: ParticleProcessMaterial
var _mat: ShaderMaterial

## Dust colour (match the floor under the effect).
@export var tint: Color = Color(0.50, 0.45, 0.38)


func _build() -> void:
	_ps = GPUParticles3D.new()
	_ps.amount = AMOUNT
	_ps.lifetime = 0.95
	_ps.explosiveness = 0.9
	_ps.randomness = 0.4
	_ps.visibility_aabb = AABB(Vector3(-3, -1, -3), Vector3(6, 4, 6))
	_ps.draw_pass_1 = make_quad()
	_mat = VfxMaterials.make("puff")
	_mat.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_ps.material_override = _mat
	_pm = ParticleProcessMaterial.new()
	_pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	_pm.emission_sphere_radius = 0.18
	_pm.direction = Vector3.UP
	_pm.spread = 75.0
	_pm.initial_velocity_min = 0.8
	_pm.initial_velocity_max = 2.2
	_pm.gravity = Vector3(0.0, 0.25, 0.0)
	_pm.damping_min = 2.2
	_pm.damping_max = 3.2
	_pm.angle_min = -180.0
	_pm.angle_max = 180.0
	_pm.angular_velocity_min = -20.0
	_pm.angular_velocity_max = 20.0
	_pm.scale_min = 0.35
	_pm.scale_max = 0.7
	_pm.scale_curve = curve_tex([Vector2(0, 0.45), Vector2(0.3, 1.0), Vector2(1, 1.45)])
	_pm.color_ramp = ramp_tex(PackedFloat32Array([0.0, 0.12, 0.55, 1.0]),
		PackedColorArray([Color(1, 1, 1, 0.0), Color(1, 1, 1, 0.55), Color(1, 1, 1, 0.3), Color(1, 1, 1, 0.0)]))
	_ps.process_material = _pm
	_add_system(_ps)


## pos: contact point, normal: surface normal (dust sprays away along it), strength 0..1+.
func play(pos: Vector3, normal: Vector3 = Vector3.UP, strength: float = 1.0) -> void:
	strength = clampf(strength, 0.1, 2.0)
	var n: Vector3 = normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	_place_at(pos + n * (0.10 + 0.08 * strength))
	_mat.set_shader_parameter("floor_y", pos.y - 0.02)
	_mat.set_shader_parameter("tint", Vector3(tint.r, tint.g, tint.b))
	_pm.direction = n
	_pm.emission_sphere_radius = 0.12 + 0.12 * strength
	_pm.initial_velocity_min = 0.5 + 0.5 * strength
	_pm.initial_velocity_max = 1.4 + 1.2 * strength
	_pm.scale_min = 0.2 + 0.08 * strength
	_pm.scale_max = 0.36 + 0.16 * strength
	_ps.amount_ratio = clampf(0.4 + 0.6 * strength, 0.3, 1.0)
	_begin(1.0)
