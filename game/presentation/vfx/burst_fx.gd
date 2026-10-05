class_name BurstFX
extends VfxParticleEffect
## Configurable particle one-shot for the moveset cues: one soft puff system (puff.gdshader) and one
## spark system (spark.gdshader), <= 16 particles each (<= 32 per one-shot), 1-2 alpha layers.
## The style picks colours, motion and which systems fire, so every material's cast / impact / outcome
## cue is one pooled node type (prewarmed once).
##
##   play(pos, normal, strength, style)   style: see STYLES (sand, grit, frost, smoke, mist, steam,
##                                        leaves, sparks, blue_sparks, static, metal, glass, ember, ash,
##                                        dust, water, inflow)

const PUFFS := 16
const SPARKS := 16

## style -> puff tint (null = no puffs), spark colour (null = no sparks), puff speed, gravity (puff,
## spark), spread, duration, inward (particles converge on the point)
const STYLES := {
	"dust": {"puff": Color(0.55, 0.49, 0.40), "speed": 1.8, "g": 0.25, "spread": 70.0},
	"sand": {"puff": Color(0.82, 0.67, 0.43), "speed": 2.4, "g": -0.6, "spread": 60.0},
	"grit": {"puff": Color(0.74, 0.60, 0.40), "spark": Color(0.95, 0.80, 0.55), "speed": 1.2, "g": -1.0, "sg": -6.0, "spread": 50.0},
	"frost": {"puff": Color(0.86, 0.94, 1.0), "spark": Color(0.85, 0.95, 1.0), "speed": 1.6, "g": -0.2, "sg": -2.0, "spread": 65.0},
	"smoke": {"puff": Color(0.32, 0.31, 0.30), "speed": 1.0, "g": 0.9, "spread": 35.0, "dur": 1.3},
	"ash": {"puff": Color(0.26, 0.25, 0.24), "spark": Color(1.0, 0.45, 0.1), "speed": 0.9, "g": 0.8, "sg": 1.5, "spread": 40.0, "dur": 1.2},
	"mist": {"puff": Color(0.84, 0.88, 0.92), "speed": 0.9, "g": 0.1, "spread": 80.0, "dur": 1.4, "alpha": 0.3},
	"steam": {"puff": Color(0.95, 0.96, 0.98), "speed": 1.3, "g": 1.2, "spread": 30.0, "dur": 1.2},
	"water": {"puff": Color(0.80, 0.90, 0.96), "spark": Color(0.75, 0.9, 1.0), "speed": 2.2, "g": -2.0, "sg": -9.0, "spread": 55.0, "alpha": 0.4},
	"leaves": {"puff": Color(0.30, 0.45, 0.18), "spark": Color(0.55, 0.8, 0.3), "speed": 1.8, "g": -1.2, "sg": -3.0, "spread": 70.0},
	"sparks": {"spark": Color(1.0, 0.75, 0.35), "sg": -7.0, "spread": 55.0, "dur": 0.7},
	"metal": {"spark": Color(1.0, 0.92, 0.7), "sg": -9.0, "spread": 45.0, "dur": 0.55, "sspeed": 7.0},
	"blue_sparks": {"spark": Color(0.55, 0.75, 1.0), "sg": -4.0, "spread": 60.0, "dur": 0.7},
	"static": {"spark": Color(0.75, 0.82, 1.0), "sg": 0.0, "spread": 180.0, "dur": 0.45, "sspeed": 3.0},
	"glass": {"spark": Color(0.85, 1.0, 0.92), "puff": Color(0.85, 0.85, 0.80), "speed": 1.0, "g": 0.0, "sg": -8.0, "spread": 60.0},
	"ember": {"spark": Color(1.0, 0.6, 0.2), "puff": Color(0.30, 0.28, 0.26), "speed": 0.8, "g": 0.6, "sg": -3.0, "spread": 50.0},
	"inflow": {"puff": Color(0.70, 0.62, 0.86), "speed": -2.6, "g": 0.0, "spread": 180.0, "dur": 0.6, "alpha": 0.35},
}

var style := "dust"
static var _ramps := {}
var _puff: GPUParticles3D
var _spark: GPUParticles3D
var _pm: ParticleProcessMaterial
var _sm: ParticleProcessMaterial
var _puff_mat: ShaderMaterial


func _build() -> void:
	_puff = GPUParticles3D.new()
	_puff.name = "Puffs"
	_puff.amount = PUFFS
	_puff.lifetime = 1.0
	_puff.explosiveness = 0.9
	_puff.randomness = 0.4
	_puff.visibility_aabb = AABB(Vector3(-4, -2, -4), Vector3(8, 6, 8))
	_puff.draw_pass_1 = make_quad()
	_puff_mat = VfxMaterials.make("puff")
	_puff_mat.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_puff.material_override = _puff_mat
	_pm = ParticleProcessMaterial.new()
	_pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	_pm.emission_sphere_radius = 0.2
	_pm.direction = Vector3.UP
	_pm.damping_min = 2.0
	_pm.damping_max = 3.0
	_pm.angle_min = -180.0
	_pm.angle_max = 180.0
	_pm.angular_velocity_min = -20.0
	_pm.angular_velocity_max = 20.0
	_pm.scale_curve = curve_tex([Vector2(0, 0.45), Vector2(0.3, 1.0), Vector2(1, 1.5)])
	_pm.color_ramp = ramp_tex(PackedFloat32Array([0.0, 0.12, 0.55, 1.0]),
		PackedColorArray([Color(1, 1, 1, 0.0), Color(1, 1, 1, 0.55), Color(1, 1, 1, 0.3), Color(1, 1, 1, 0.0)]))
	_puff.process_material = _pm
	_add_system(_puff)
	_spark = GPUParticles3D.new()
	_spark.name = "Sparks"
	_spark.amount = SPARKS
	_spark.lifetime = 0.7
	_spark.explosiveness = 0.95
	_spark.randomness = 0.5
	_spark.visibility_aabb = AABB(Vector3(-4, -2, -4), Vector3(8, 6, 8))
	_spark.draw_pass_1 = make_quad()
	_spark.material_override = VfxMaterials.make("spark")
	_sm = ParticleProcessMaterial.new()
	_sm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	_sm.emission_sphere_radius = 0.08
	_sm.direction = Vector3.UP
	_sm.initial_velocity_min = 2.0
	_sm.initial_velocity_max = 5.0
	_sm.damping_min = 0.4
	_sm.damping_max = 1.0
	_sm.scale_min = 0.025
	_sm.scale_max = 0.05
	_sm.scale_curve = curve_tex([Vector2(0, 1.0), Vector2(1, 0.25)])
	_spark.process_material = _sm
	_add_system(_spark)


func play(pos: Vector3, normal: Vector3 = Vector3.UP, strength: float = 1.0, style_name: String = "dust") -> void:
	style = style_name if STYLES.has(style_name) else "dust"
	var st: Dictionary = STYLES[style]
	strength = clampf(strength, 0.1, 2.0)
	var n := normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	_place_at(pos + n * 0.06)
	var lite := VfxMaterials.lite()
	var has_puff := st.has("puff")
	var has_spark := st.has("spark")
	if has_puff:
		var pc: Color = st.puff
		_puff_mat.set_shader_parameter("tint", VfxPalette.v3(pc))
		_puff_mat.set_shader_parameter("floor_y", pos.y - 0.6 if n.y < 0.5 else pos.y - 0.02)
		var spd := float(st.get("speed", 1.5))
		_pm.direction = n
		_pm.spread = float(st.get("spread", 60.0))
		_pm.gravity = Vector3(0, float(st.get("g", 0.2)), 0)
		_pm.emission_sphere_radius = 0.1 + 0.15 * strength if spd >= 0.0 else 0.9 + 0.5 * strength
		_pm.initial_velocity_min = maxf(spd, 0.0) * (0.4 + 0.3 * strength)
		_pm.initial_velocity_max = maxf(spd, 0.0) * (0.9 + 0.6 * strength)
		_pm.radial_velocity_min = minf(spd, 0.0) * 1.2
		_pm.radial_velocity_max = minf(spd, 0.0) * 0.8
		_pm.scale_min = 0.18 + 0.1 * strength
		_pm.scale_max = 0.34 + 0.18 * strength
		_pm.color_ramp = _ramp("p:" + style, st)
		_puff.lifetime = float(st.get("dur", 1.0))
		_puff.amount_ratio = clampf((0.35 + 0.65 * strength) * (0.6 if lite else 1.0), 0.2, 1.0)
	if has_spark:
		_sm.color_ramp = _ramp("s:" + style, st)
		_sm.direction = n
		_sm.spread = float(st.get("spread", 55.0))
		_sm.gravity = Vector3(0, float(st.get("sg", -6.0)), 0)
		var ss := float(st.get("sspeed", 4.5))
		_sm.initial_velocity_min = ss * 0.4
		_sm.initial_velocity_max = ss * (0.8 + 0.4 * strength)
		_spark.lifetime = minf(float(st.get("dur", 0.8)), 0.9)
		_spark.amount_ratio = clampf((0.3 + 0.7 * strength) * (0.6 if lite else 1.0), 0.2, 1.0)
	_begin(float(st.get("dur", 1.0)) + 0.1)
	_puff.emitting = has_puff
	_spark.emitting = has_spark
	_puff.visible = has_puff
	_spark.visible = has_spark


## Colour ramps are built once per style and shared by every pooled instance.
static func _ramp(key: String, st: Dictionary) -> GradientTexture1D:
	if _ramps.has(key):
		return _ramps[key]
	var t: GradientTexture1D
	if key.begins_with("p:"):
		var a := float(st.get("alpha", 0.55))
		t = ramp_tex(PackedFloat32Array([0.0, 0.12, 0.55, 1.0]),
			PackedColorArray([Color(1, 1, 1, 0.0), Color(1, 1, 1, a), Color(1, 1, 1, a * 0.55), Color(1, 1, 1, 0.0)]))
	else:
		var sc: Color = st.spark
		t = ramp_tex(PackedFloat32Array([0.0, 0.3, 1.0]),
			PackedColorArray([Color(1, 1, 1, 1).lerp(sc, 0.3), sc, Color(sc.r * 0.6, sc.g * 0.3, sc.b * 0.2, 0.0)]))
	_ramps[key] = t
	return t
