class_name ShellView
extends VfxEffect
## Spherical fields and auras: null bubble, vacuum well (+ spiral inflow on the ground), corona,
## static field (+ crawling arcs), wind guard, sound barrier, stance / T3 aura, mine / fuse ember,
## vacuum inrush (air rushing back where a well collapsed), frost shell. One fresnel shell (shell.gdshader; the vacuum styles read the shared opaque screen
## copy through shell_refract.gdshader unless VfxMaterials.lite()) + optional ground spiral / arcs.
##
##   configure(style, color = default)   see STYLES
##   set_shape(radius, height_scale)     metres; height_scale flattens the shell into a dome
##   set_power(p01)                      brightness / pulse from tier or zone power

const STYLES := {
	"null_bubble": {"mat": "vacuum", "rim": 2.2, "streak": 0.55, "op": 0.75, "refract": 0.045},
	"vacuum_well": {"mat": "vacuum", "rim": 1.8, "streak": 0.9, "op": 0.55, "refract": 0.03, "spiral": true},
	"corona": {"mat": "blue", "col": Color(0.22, 0.46, 1.0), "rim": 2.4, "crackle": 0.4, "op": 0.6, "glow": 1.5, "pulse": 0.2, "absorb": 0.55},
	"static_field": {"mat": "lightning", "col": Color(0.5, 0.56, 1.0), "rim": 3.0, "crackle": 0.55, "op": 0.3, "arcs": true, "glow": 1.3, "absorb": 0.35},
	"wind_guard": {"mat": "wind", "rim": 2.4, "streak": -0.35, "op": 0.32},
	"sound_barrier": {"mat": "sound", "rim": 1.6, "streak": -1.2, "op": 0.42},
	"aura": {"mat": "", "rim": 3.0, "op": 0.22, "pulse": 0.15, "glow": 1.1},
	"mine": {"mat": "blast", "rim": 1.4, "core": 0.85, "op": 0.6, "pulse": 0.55, "glow": 1.6, "core_col": Color(1.0, 0.36, 0.08)},
	"inrush": {"mat": "vacuum", "rim": 1.3, "streak": 1.5, "op": 0.34, "spiral": true},
	"frost": {"mat": "ice", "rim": 1.5, "core": 0.12, "op": 0.6, "core_col": Color(0.7, 0.85, 0.95)},
}

var style := "aura"
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _mat_plain: ShaderMaterial
var _mat_refr: ShaderMaterial
var _spiral: MeshInstance3D
var _spiral_mat: ShaderMaterial
var _arcs: LightningArcFX
var _phase := 0.0
var _radius := 1.0
var _hs := 1.0
var _arc_t := 0.0
var _rng := RandomNumberGenerator.new()


func _init() -> void:
	_mat_plain = VfxMaterials.make_fx("shell")
	_mi = MeshInstance3D.new()
	_mi.name = "Shell"
	_mi.mesh = FxMesh.sphere_mesh(2)
	_mi.material_override = _mat_plain
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mi)
	_spiral_mat = VfxMaterials.make_fx("ring")
	_spiral_mat.set_shader_parameter("style", 1.0)
	_spiral_mat.set_shader_parameter("cover", 0.6)
	_spiral = MeshInstance3D.new()
	_spiral.name = "Spiral"
	_spiral.mesh = FxMesh.ground_quad()
	_spiral.material_override = _spiral_mat
	_spiral.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_spiral.visible = false
	add_child(_spiral)
	_mat = _mat_plain
	reset()


func configure(style_name: String, col: Color = Color(0, 0, 0, 0), seed_value: int = 0) -> void:
	style = style_name if STYLES.has(style_name) else "aura"
	var st: Dictionary = STYLES[style]
	var c: Color = col if col.a > 0.0 else st.get("col", VfxPalette.color(String(st.get("mat", "wind"))))
	var want_refr := st.has("refract") and not VfxMaterials.lite()
	if want_refr:
		if _mat_refr == null:
			_mat_refr = VfxMaterials.make_fx("shell_refract")
		_mat = _mat_refr
		_mat.set_shader_parameter("refract", float(st.refract))
	else:
		_mat = _mat_plain
	_mi.material_override = _mat
	_mat.set_shader_parameter("color", VfxPalette.v3(c))
	_mat.set_shader_parameter("color_core", VfxPalette.v3(st.get("core_col", c)))
	_mat.set_shader_parameter("rim_power", float(st.get("rim", 2.0)))
	_mat.set_shader_parameter("streak", float(st.get("streak", 0.0)))
	_mat.set_shader_parameter("crackle", float(st.get("crackle", 0.0)))
	_mat.set_shader_parameter("core", float(st.get("core", 0.0)))
	_mat.set_shader_parameter("pulse", float(st.get("pulse", 0.0)))
	_mat.set_shader_parameter("opacity", float(st.get("op", 0.5)))
	_mat.set_shader_parameter("glow", float(st.get("glow", 1.0)))
	_mat.set_shader_parameter("absorb", float(st.get("absorb", 0.0)))
	_mat.set_shader_parameter("seed", float(absi(seed_value) % 37) * 0.31)
	_spiral.visible = bool(st.get("spiral", false))
	if _spiral.visible:
		_spiral_mat.set_shader_parameter("color", VfxPalette.v3(c.lerp(Color(0.1, 0.05, 0.2), 0.35)))
	if bool(st.get("arcs", false)):
		if _arcs == null:
			_arcs = LightningArcFX.new()
			_arcs.manual_time = true
			_arcs.intensity = 0.6
			_arcs.half_width = 0.05
			add_child(_arcs)
			_arcs.set_light_enabled(false)
	_rng.seed = seed_value * 97 + 3
	_apply_shape()


func set_shape(radius: float, height_scale: float = 1.0) -> void:
	_radius = maxf(radius, 0.05)
	_hs = clampf(height_scale, 0.1, 2.0)
	_apply_shape()


func _apply_shape() -> void:
	_mi.scale = Vector3(_radius, _radius * _hs, _radius)
	_spiral.scale = Vector3(_radius * 1.15, 1.0, _radius * 1.15)
	_spiral.position = Vector3(0, 0.03, 0)


func set_power(p01: float) -> void:
	var st: Dictionary = STYLES[style]
	_mat.set_shader_parameter("opacity", float(st.get("op", 0.5)) * lerpf(0.55, 1.25, clampf(p01, 0.0, 1.0)))


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_arc_t = 0.0
	if _arcs:
		_arcs.reset()


func advance(dt: float) -> void:
	_phase += dt
	_mat.set_shader_parameter("phase", _phase)
	if _spiral.visible:
		_spiral_mat.set_shader_parameter("phase", _phase)
	if _arcs != null and bool(STYLES[style].get("arcs", false)):
		_arc_t -= dt
		_arcs.advance(dt)
		if _arc_t <= 0.0 and is_inside_tree():
			_arc_t = _rng.randf_range(0.12, 0.3)
			var c := global_position
			var a := _rng.randf() * TAU
			var r := _radius * _rng.randf_range(0.3, 0.9)
			var p0 := c + Vector3(cos(a) * r, 0.05, sin(a) * r)
			var a2 := a + _rng.randf_range(0.6, 1.6)
			var p1 := c + Vector3(cos(a2) * r * 0.8, _radius * _hs * _rng.randf_range(0.2, 0.6), sin(a2) * r * 0.8)
			_arcs.strike(PackedVector3Array([p0, p0.lerp(p1, 0.5) + Vector3(0, 0.2, 0), p1]), _rng.randi())
