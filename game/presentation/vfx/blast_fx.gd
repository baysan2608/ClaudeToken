class_name BlastFX
extends VfxEffect
## Explosion (Fire / Combustion, mines, detonations): white flash -> orange fireball eaten by noise ->
## grey smoke, a shock ring racing over the ground, and a 0.1 s flash light (shadowless). 0.65 s.
## 1 sphere draw (blast.gdshader) + 1 ring draw, 2 transparent layers, 1 light.
##
##   play(pos, radius, intensity = 1, blue = false)

const DURATION := 0.65

var _ball: MeshInstance3D
var _mat: ShaderMaterial
var _ring: MeshInstance3D
var _ring_mat: ShaderMaterial
var _light: OmniLight3D
var _age := 0.0
var _radius := 1.5
var _intensity := 1.0
var _playing := false


func _init() -> void:
	_mat = VfxMaterials.make_fx("blast")
	_ball = MeshInstance3D.new()
	_ball.name = "Ball"
	_ball.mesh = FxMesh.sphere_mesh(2)
	_ball.material_override = _mat
	_ball.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_ball)
	_ring_mat = VfxMaterials.make_fx("ring")
	_ring_mat.set_shader_parameter("color", Vector3(0.95, 0.85, 0.7))
	_ring_mat.set_shader_parameter("cover", 0.7)
	_ring_mat.set_shader_parameter("glow", 1.0)
	_ring = MeshInstance3D.new()
	_ring.name = "Shock"
	_ring.mesh = FxMesh.ground_quad()
	_ring.material_override = _ring_mat
	_ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_ring)
	_light = OmniLight3D.new()
	_light.name = "Flash"
	_light.shadow_enabled = false
	_light.light_color = Color(1.0, 0.75, 0.45)
	_light.omni_attenuation = 1.5
	_light.visible = false
	add_child(_light)
	reset()


func play(pos: Vector3, radius: float, intensity: float = 1.0, blue: bool = false) -> void:
	_radius = clampf(radius, 0.3, 6.0)
	_intensity = clampf(intensity, 0.2, 1.5)
	_place_at(pos)
	_mat.set_shader_parameter("seed", fposmod(pos.x * 0.37 + pos.z * 0.71, 1.0) * 5.0)
	_mat.set_shader_parameter("hot_tint", Vector3(0.55, 0.75, 1.4) if blue else Vector3.ONE)
	_light.light_color = Color(0.6, 0.75, 1.0) if blue else Color(1.0, 0.75, 0.45)
	_light.omni_range = _radius * 4.0 + 2.0
	_ring.position = Vector3(0, 0.03, 0)
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	_apply()


## The shock ring sits on the ground below the blast centre (y passed by the caller via ground_y).
func set_ground(y: float) -> void:
	_ring.position = Vector3(0, y - position.y + 0.03, 0)


func is_playing() -> bool:
	return _playing


func reset() -> void:
	_playing = false
	visible = false
	set_process(false)
	_light.visible = false
	_age = 0.0


func advance(dt: float) -> void:
	if not _playing:
		return
	_age += dt
	if _age >= DURATION:
		_playing = false
		_light.visible = false
		_finish()
		return
	_apply()


func _apply() -> void:
	var t := _age / DURATION
	var grow := 1.0 - pow(1.0 - minf(t * 2.2, 1.0), 3.0)
	_ball.scale = Vector3.ONE * _radius * (0.25 + 0.75 * grow)
	_mat.set_shader_parameter("age", t)
	_mat.set_shader_parameter("intensity", _intensity)
	var rt := minf(t * 1.6, 1.0)
	var rr := _radius * (0.4 + 1.8 * (1.0 - pow(1.0 - rt, 2.0)))
	_ring.scale = Vector3(rr / 0.8, 1.0, rr / 0.8)
	_ring_mat.set_shader_parameter("alpha", (1.0 - rt) * 0.9 * _intensity)
	_ring_mat.set_shader_parameter("width", 0.05 + 0.08 * rt)
	_ring_mat.set_shader_parameter("phase", _age)
	var le := 3.0 * _intensity * (1.0 - smoothstep(0.0, 0.16, t))
	_light.visible = le > 0.05
	_light.light_energy = le
