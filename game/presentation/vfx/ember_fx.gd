class_name EmberFX
extends VfxParticleEffect
## A few hot sparks (lava / hot rock impacts). play(pos, normal, strength). 12 particles max,
## additive, ~0.9 s, no glow cloud.

const AMOUNT: int = 12

var _ps: GPUParticles3D
var _pm: ParticleProcessMaterial
var _mat: ShaderMaterial


func _build() -> void:
	_ps = GPUParticles3D.new()
	_ps.amount = AMOUNT
	_ps.lifetime = 0.9
	_ps.explosiveness = 0.95
	_ps.randomness = 0.5
	_ps.visibility_aabb = AABB(Vector3(-4, -1, -4), Vector3(8, 6, 8))
	_ps.draw_pass_1 = make_quad()
	_mat = VfxMaterials.make("spark")
	_ps.material_override = _mat
	_pm = ParticleProcessMaterial.new()
	_pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	_pm.emission_sphere_radius = 0.06
	_pm.direction = Vector3.UP
	_pm.spread = 62.0
	_pm.initial_velocity_min = 1.6
	_pm.initial_velocity_max = 4.2
	_pm.gravity = Vector3(0.0, -6.5, 0.0)
	_pm.damping_min = 0.4
	_pm.damping_max = 1.0
	_pm.scale_min = 0.03
	_pm.scale_max = 0.06
	_pm.scale_curve = curve_tex([Vector2(0, 1.0), Vector2(1, 0.25)])
	_pm.color_ramp = ramp_tex(PackedFloat32Array([0.0, 0.25, 0.7, 1.0]),
		PackedColorArray([Color(1.0, 0.85, 0.45, 1.0), Color(1.0, 0.5, 0.1, 1.0), Color(0.8, 0.15, 0.03, 0.7), Color(0.4, 0.04, 0.0, 0.0)]))
	_ps.process_material = _pm
	_add_system(_ps)


func play(pos: Vector3, normal: Vector3 = Vector3.UP, strength: float = 1.0) -> void:
	strength = clampf(strength, 0.1, 2.0)
	var n: Vector3 = normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	_place_at(pos + n * 0.02)
	_pm.direction = n
	_pm.initial_velocity_max = 2.8 + 1.8 * strength
	_ps.amount_ratio = clampf(0.3 + 0.7 * strength, 0.25, 1.0)
	_begin(1.0)
