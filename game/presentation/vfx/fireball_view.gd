class_name FireballView
extends VfxEffect
## Flying fire (FIRE bodies): fireball (hot core + trailing tongues), comet (blue, long and narrow),
## ember (small spark ball). The tongues stream behind the travel direction; one brief-ish omni light
## (shadowless) for fireballs and comets only.
##
##   setup(kind, radius, blue)    kind "fireball" | "comet" | "ember"
##   set_motion(vel)              orients the tail (call every frame; cheap)
##   set_power(p01)               heat payload / tier -> size of the tongues and light

var kind := "fireball"
var _tongues: MeshInstance3D
var _core: MeshInstance3D
var _mt: ShaderMaterial
var _mc: ShaderMaterial
var _light: OmniLight3D
var _scroll := 0.0
var _radius := 0.3
var _blue := false
var _power := 1.0


func _init() -> void:
	_mt = VfxMaterials.make("flame")
	_mt.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_mt.set_shader_parameter("loop", 1.0)
	_mt.set_shader_parameter("shape", 1.0)
	_mt.set_shader_parameter("cover", 0.85)
	_tongues = MeshInstance3D.new()
	_tongues.name = "Tongues"
	_tongues.mesh = VfxMesh.flame_mesh()
	_tongues.material_override = _mt
	_tongues.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_tongues)
	_mc = VfxMaterials.make_fx("shell")
	_mc.set_shader_parameter("core", 1.0)
	_mc.set_shader_parameter("rim_power", 1.2)
	_mc.set_shader_parameter("opacity", 0.8)
	_mc.set_shader_parameter("glow", 1.5)
	_core = MeshInstance3D.new()
	_core.name = "Core"
	_core.mesh = FxMesh.sphere_mesh(1)
	_core.material_override = _mc
	_core.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_core)
	_light = OmniLight3D.new()
	_light.name = "Glow"
	_light.shadow_enabled = false
	_light.omni_attenuation = 1.8
	_light.visible = false
	add_child(_light)
	reset()


func setup(kind_name: String, radius: float, blue: bool = false) -> void:
	kind = kind_name
	_radius = clampf(radius, 0.04, 1.2)
	_blue = blue or kind == "comet"
	var cols := [Color(0.62, 0.07, 0.01), Color(1.0, 0.36, 0.05), Color(1.0, 0.78, 0.30), Color(1.0, 0.95, 0.75)]
	if _blue:
		cols = [Color(0.10, 0.12, 0.55), Color(0.25, 0.45, 1.0), Color(0.55, 0.85, 1.0), Color(0.95, 0.98, 1.0)]
	for i in 4:
		_mt.set_shader_parameter(["col_deep", "col_mid", "col_hot", "col_white"][i], VfxPalette.v3(cols[i]))
	_mc.set_shader_parameter("color", VfxPalette.v3(cols[1]))
	_mc.set_shader_parameter("color_core", VfxPalette.v3(cols[2]))
	_mt.set_shader_parameter("seed", fposmod(radius * 13.7, 1.0))
	_light.light_color = Color(0.5, 0.7, 1.0) if _blue else Color(1.0, 0.55, 0.2)
	_light.omni_range = clampf(_radius * 10.0, 2.0, 6.0)
	_apply()


func set_power(p01: float) -> void:
	_power = clampf(p01, 0.2, 1.5)
	_apply()


var _tail_dir := Vector3.UP


func _apply() -> void:
	_core.scale = Vector3.ONE * _radius * (0.42 if kind == "comet" else 0.5)
	_mt.set_shader_parameter("intensity", 1.5 if kind != "ember" else 1.6)
	_mt.set_shader_parameter("core", 0.35 if _blue else 0.0)
	_light.visible = kind != "ember"
	_light.light_energy = (1.2 if kind == "comet" else 1.5) * _power
	_orient()


## Tongues point away from the travel direction (the flame axis +Y is turned to -velocity, a
## little toward the sky so a slow fireball still burns upward).
func set_motion(vel: Vector3) -> void:
	var d := -vel
	if d.length_squared() < 0.25:
		d = Vector3.UP
	_tail_dir = d.normalized().lerp(Vector3.UP, 0.25).normalized()
	_orient()


func _orient() -> void:
	var d := _tail_dir
	var r := _radius
	var length := r * (5.5 if kind == "comet" else (2.5 if kind == "ember" else 3.2))
	var width := r * (0.75 if kind == "comet" else (0.9 if kind == "ember" else 1.05))
	var k := lerpf(0.8, 1.15, (_power - 0.2) / 1.3)
	var x := d.cross(Vector3.FORWARD if absf(d.z) < 0.9 else Vector3.RIGHT).normalized()
	_tongues.basis = Basis(x * width * k, d * length * k, x.cross(d).normalized() * width * k)
	_tongues.position = -d * r * 0.35


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_light.visible = false


func advance(dt: float) -> void:
	_scroll += dt * (1.6 if kind == "comet" else 1.2)
	_mt.set_shader_parameter("scroll", _scroll)
	_mc.set_shader_parameter("phase", _scroll)
	if _light.visible:
		_light.light_energy = (1.2 if kind == "comet" else 1.5) * _power * (0.85 + 0.15 * sin(_scroll * 17.0))
