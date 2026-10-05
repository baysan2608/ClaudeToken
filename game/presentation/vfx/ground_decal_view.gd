class_name GroundDecalView
extends VfxEffect
## Zone ground marks on a lifted quad (ground_fx.gdshader, like ScorchDecal: no Decal node):
## quicksand swirl, ice floor, mud, melt pit, static field, caltrop shade, frost patch, sand drift.
##
##   place(pos, radius, style, seed)   style name (STYLE keys)
##   set_fade(f01) / set_heat(h01)

const STYLE := {"quicksand": 0.0, "ice_floor": 1.0, "mud": 2.0, "melt_pit": 3.0, "static_field": 4.0,
	"shade": 5.0, "frost": 6.0, "sand": 7.0}
const LIFT := 0.012

var style := "mud"
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _phase := 0.0
var _fade := 1.0
var _fade_target := 1.0


func _init() -> void:
	_mat = VfxMaterials.make_fx("ground_fx")
	_mi = MeshInstance3D.new()
	_mi.name = "Mark"
	_mi.mesh = FxMesh.ground_quad()
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mi)
	reset()


func place(pos: Vector3, radius: float, style_name: String, seed_value: int = 0) -> void:
	style = style_name if STYLE.has(style_name) else "mud"
	_mat.set_shader_parameter("style", STYLE[style])
	_mat.set_shader_parameter("seed", float(absi(seed_value) % 41) * 0.123)
	_place(Transform3D(Basis.from_scale(Vector3(maxf(radius, 0.1), 1.0, maxf(radius, 0.1))), pos + Vector3(0, LIFT, 0)))
	_fade = 0.0
	_fade_target = 1.0
	visible = true
	set_process(true)


func set_fade(f01: float) -> void:
	_fade_target = clampf(f01, 0.0, 1.0)


func set_heat(h01: float) -> void:
	_mat.set_shader_parameter("heat", clampf(h01, 0.0, 1.0))


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_fade = 0.0
	_mat.set_shader_parameter("fade", 0.0)


func advance(dt: float) -> void:
	_phase += dt
	_fade = move_toward(_fade, _fade_target, dt * 6.0)
	_mat.set_shader_parameter("fade", _fade)
	_mat.set_shader_parameter("phase", _phase)
