class_name BeamFX
extends VfxEffect
## Straight energy / matter beams: blue-fire needle and beam, sandblast, water jet core, sound lance,
## flame jet. One camera-facing ribbon (beam.gdshader, billboarded around its own axis in the vertex
## stage: no rebuild), premultiplied, 1 layer. Optional cool/warm flash light (blue only).
##
##   play(from, to, dur, style)   one-shot; update(from, to) re-aims it while it plays (sustained beams)

const STYLES := {
	"blue": {"core": Color(0.92, 0.98, 1.0), "glow": Color(0.30, 0.55, 1.0), "width": 0.11, "cover": 0.12, "taper": 0.3, "light": true},
	"needle": {"core": Color(0.95, 0.99, 1.0), "glow": Color(0.40, 0.65, 1.0), "width": 0.05, "cover": 0.1, "taper": 0.85},
	"flame": {"core": Color(1.0, 0.85, 0.5), "glow": Color(1.0, 0.35, 0.05), "width": 0.16, "cover": 0.5, "taper": 0.0},
	"sand": {"core": Color(0.90, 0.78, 0.55), "glow": Color(0.62, 0.50, 0.32), "width": 0.16, "cover": 0.85, "taper": 0.0, "grain": 1.0},
	"water": {"core": Color(0.85, 0.95, 1.0), "glow": Color(0.30, 0.65, 0.85), "width": 0.09, "cover": 0.7, "taper": 0.2},
	"sound": {"core": Color(1.0, 0.95, 0.8), "glow": Color(0.85, 0.75, 0.5), "width": 0.2, "cover": 0.2, "taper": 0.0, "grain": 0.6},
	"vacuum": {"core": Color(0.75, 0.65, 0.95), "glow": Color(0.30, 0.18, 0.5), "width": 0.14, "cover": 0.6, "taper": 0.0},
}

var style := "blue"
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _light: OmniLight3D
var _age := 0.0
var _dur := 0.3
var _playing := false
var _scroll := 0.0
var _use_light := false


func _init() -> void:
	_mat = VfxMaterials.make_fx("beam")
	_mi = MeshInstance3D.new()
	_mi.name = "Beam"
	_mi.mesh = FxMesh.beam_mesh()
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_mi.extra_cull_margin = 2.0
	add_child(_mi)
	_light = OmniLight3D.new()
	_light.name = "Flash"
	_light.shadow_enabled = false
	_light.light_color = Color(0.5, 0.7, 1.0)
	_light.omni_range = 5.0
	_light.visible = false
	add_child(_light)
	reset()


func play(from: Vector3, to: Vector3, dur: float, style_name: String = "blue") -> void:
	style = style_name if STYLES.has(style_name) else "blue"
	var st: Dictionary = STYLES[style]
	_mat.set_shader_parameter("core_color", VfxPalette.v3(st.core))
	_mat.set_shader_parameter("glow_color", VfxPalette.v3(st.glow))
	_mat.set_shader_parameter("width", float(st.width))
	_mat.set_shader_parameter("cover", float(st.cover))
	_mat.set_shader_parameter("taper", float(st.taper))
	_mat.set_shader_parameter("grain", float(st.get("grain", 0.0)))
	_use_light = bool(st.get("light", false))
	_dur = maxf(dur, 0.08)
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	update(from, to)
	_apply()


func update(from: Vector3, to: Vector3) -> void:
	var d := to - from
	var ln := maxf(d.length(), 0.05)
	var z := d / ln
	var x := z.cross(Vector3.UP)
	x = x.normalized() if x.length_squared() > 1e-6 else Vector3.RIGHT
	var y := z.cross(x).normalized()
	_place(Transform3D(Basis(x, y, z * ln), from))
	_light.position = Vector3(0, 0, 0.5)


func is_playing() -> bool:
	return _playing


func reset() -> void:
	_playing = false
	visible = false
	set_process(false)
	_light.visible = false


func advance(dt: float) -> void:
	if not _playing:
		return
	_age += dt
	_scroll += dt
	if _age >= _dur:
		_playing = false
		_light.visible = false
		_finish()
		return
	_apply()


func _apply() -> void:
	var t := _age / _dur
	_mat.set_shader_parameter("age", t)
	_mat.set_shader_parameter("scroll", _scroll)
	_light.visible = _use_light and t < 0.6
	_light.light_energy = 1.6 * (1.0 - t)
