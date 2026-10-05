class_name CloudView
extends VfxEffect
## Soft particulate volumes driven from body state: sand cloud, sandstorm, fog, mist, steam, steam
## screen, geyser column, dust line, sand slug, concealment veil, smoke.
##
##   configure(style, seed)       look preset (see STYLES); cheap, call on acquire
##   set_shape(radius, height)    metres; the node sits on the ground (or the slug centre)
##   set_amount(a01)              fades the volume in/out by showing a fraction of the puffs
##   set_floor(y)                 world height under the volume (puffs fade instead of clipping)
##
## Two camera-facing puff layers (outer: wide, soft; core: denser), each ONE MultiMesh draw whose
## puffs are positioned and animated entirely in cloud.gdshader. No particles, no CPU per frame
## beyond one uniform. Lite (quality 0): fewer puffs.

const OUTER_N := 12
const CORE_N := 9
const LITE_OUTER_N := 7
const LITE_CORE_N := 5

## style -> {size, swirl, rise, column, flatten, grow, wander, op, op_core, top, low, stretch, h_core}
const STYLES := {
	"sand_cloud": {"size": 1.5, "swirl": 0.35, "op": 0.42, "op_core": 0.5, "flatten": 0.15, "top": Color(0.66, 0.50, 0.30), "low": Color(0.38, 0.27, 0.15)},
	"sandstorm": {"size": 1.9, "swirl": 1.1, "wander": 0.35, "op": 0.48, "op_core": 0.52, "top": Color(0.64, 0.48, 0.28), "low": Color(0.36, 0.25, 0.14)},
	"fog": {"size": 1.8, "swirl": 0.06, "flatten": 0.85, "op": 0.15, "op_core": 0.17, "top": Color(0.80, 0.84, 0.88), "low": Color(0.58, 0.63, 0.68)},
	"mist": {"size": 1.5, "swirl": 0.1, "flatten": 0.7, "op": 0.13, "op_core": 0.16, "top": Color(0.82, 0.87, 0.91), "low": Color(0.62, 0.68, 0.74)},
	"steam": {"size": 1.1, "swirl": 0.2, "rise": 0.45, "column": 0.3, "grow": 1.1, "op": 0.3, "op_core": 0.36, "top": Color(0.97, 0.97, 0.98), "low": Color(0.80, 0.82, 0.85)},
	"steam_screen": {"size": 1.4, "swirl": 0.05, "rise": 0.18, "grow": 0.6, "op": 0.3, "op_core": 0.34, "top": Color(0.96, 0.97, 0.98), "low": Color(0.78, 0.80, 0.83)},
	"geyser": {"size": 0.9, "swirl": 0.5, "rise": 1.1, "column": 1.0, "grow": 1.5, "op": 0.42, "op_core": 0.55, "top": Color(0.97, 0.98, 1.0), "low": Color(0.62, 0.78, 0.86)},
	"dust_line": {"size": 0.9, "swirl": 0.2, "flatten": 0.35, "op": 0.36, "op_core": 0.4, "top": Color(0.74, 0.67, 0.56), "low": Color(0.50, 0.44, 0.36)},
	"slug": {"size": 0.42, "swirl": 2.5, "wander": 0.04, "stretch": 0.25, "op": 0.75, "op_core": 0.9, "top": Color(0.70, 0.53, 0.31), "low": Color(0.42, 0.30, 0.16)},
	"veil": {"size": 1.0, "swirl": 0.4, "rise": 0.12, "op": 0.18, "op_core": 0.2, "top": Color(0.88, 0.92, 0.95), "low": Color(0.72, 0.76, 0.80)},
	"smoke": {"size": 1.0, "swirl": 0.2, "rise": 0.3, "column": 0.4, "grow": 1.4, "op": 0.32, "op_core": 0.36, "top": Color(0.42, 0.41, 0.40), "low": Color(0.20, 0.19, 0.19)},
}

var style := "sand_cloud"
var _outer: MultiMeshInstance3D
var _core: MultiMeshInstance3D
var _mat_o: ShaderMaterial
var _mat_c: ShaderMaterial
var _phase := 0.0
var _radius := 1.5
var _height := 1.0
var _amount := 1.0
var _seed := 0


func _init() -> void:
	var lite := VfxMaterials.lite()
	_mat_o = VfxMaterials.make_fx("cloud")
	_mat_c = VfxMaterials.make_fx("cloud")
	_outer = _layer("Outer", LITE_OUTER_N if lite else OUTER_N, _mat_o, 11)
	_core = _layer("Core", LITE_CORE_N if lite else CORE_N, _mat_c, 29)
	reset()


func _layer(nm: String, n: int, mat: ShaderMaterial, salt: int) -> MultiMeshInstance3D:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_custom_data = true
	mm.mesh = FxMesh.puff_quad()
	mm.instance_count = n
	var rng := RandomNumberGenerator.new()
	rng.seed = 7919 * salt
	for i in n:
		mm.set_instance_transform(i, Transform3D.IDENTITY)
		# Stratified so a fraction `amount` of the puffs (rand < amount) is spread evenly.
		var rnd := (float(i) + rng.randf()) / float(n)
		mm.set_instance_custom_data(i, Color(rng.randf(), sqrt(rng.randf()), rng.randf(), rnd))
	var mi := MultiMeshInstance3D.new()
	mi.name = nm
	mi.multimesh = mm
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)
	return mi


func configure(style_name: String, seed_value: int = 0) -> void:
	style = style_name if STYLES.has(style_name) else "sand_cloud"
	_seed = seed_value
	var st: Dictionary = STYLES[style]
	for k in 2:
		var m: ShaderMaterial = _mat_o if k == 0 else _mat_c
		var core := k == 1
		m.set_shader_parameter("puff_size", float(st.get("size", 1.0)) * (0.7 if core else 1.0))
		m.set_shader_parameter("swirl", float(st.get("swirl", 0.3)) * (1.5 if core else 1.0))
		m.set_shader_parameter("rise", float(st.get("rise", 0.0)))
		m.set_shader_parameter("column", float(st.get("column", 0.0)))
		m.set_shader_parameter("flatten", float(st.get("flatten", 0.0)))
		m.set_shader_parameter("grow", float(st.get("grow", 0.6)))
		m.set_shader_parameter("wander", float(st.get("wander", 0.15)))
		m.set_shader_parameter("stretch", float(st.get("stretch", 0.0)))
		m.set_shader_parameter("opacity", float(st.get("op_core" if core else "op", 0.3)))
		var top: Color = st.get("top", Color.WHITE)
		var low: Color = st.get("low", Color.GRAY)
		m.set_shader_parameter("tint_top", VfxPalette.v3(top))
		m.set_shader_parameter("tint_low", VfxPalette.v3(low.lerp(top, 0.25) if core else low))
		m.set_shader_parameter("erosion", 0.45 if core else 0.6)
	_phase = float(absi(seed_value) % 97) * 0.37
	_apply_shape()


func set_shape(radius: float, height: float, squash_x: float = 1.0) -> void:
	_radius = maxf(radius, 0.05)
	_height = maxf(height, 0.05)
	for m in [_mat_o, _mat_c]:
		(m as ShaderMaterial).set_shader_parameter("squash_x", squash_x)
	_apply_shape()


func _apply_shape() -> void:
	var centred := style == "slug" or style == "veil"
	_mat_o.set_shader_parameter("radius", _radius)
	_mat_o.set_shader_parameter("height", _height)
	_mat_c.set_shader_parameter("radius", _radius * 0.6)
	_mat_c.set_shader_parameter("height", _height * 0.75)
	_mat_o.set_shader_parameter("y0", -_height * 0.5 if style == "slug" else 0.0)
	_mat_c.set_shader_parameter("y0", -_height * 0.35 if style == "slug" else 0.0)
	var r := _radius * 1.6 + 2.0
	var aabb := AABB(Vector3(-r * 1.8, -1.0 - (_height if centred else 0.0), -r), Vector3(r * 3.6, _height * 2.5 + 3.0, r * 2.0))
	_outer.custom_aabb = aabb
	_core.custom_aabb = aabb


func set_amount(a01: float) -> void:
	a01 = clampf(a01, 0.0, 1.0)
	if is_equal_approx(a01, _amount):
		return
	_amount = a01
	_mat_o.set_shader_parameter("amount", a01)
	_mat_c.set_shader_parameter("amount", a01)


func set_floor(y: float) -> void:
	_mat_o.set_shader_parameter("floor_y", y - 0.03)
	_mat_c.set_shader_parameter("floor_y", y - 0.03)


func set_opacity_scale(k: float) -> void:
	var st: Dictionary = STYLES[style]
	_mat_o.set_shader_parameter("opacity", float(st.get("op", 0.3)) * k)
	_mat_c.set_shader_parameter("opacity", float(st.get("op_core", 0.3)) * k)


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_amount = -1.0
	set_amount(1.0)
	set_floor(-1000.0)


func advance(dt: float) -> void:
	_phase += dt
	_mat_o.set_shader_parameter("phase", _phase)
	_mat_c.set_shader_parameter("phase", _phase * 1.07)


## Draw calls / transparent layers of this view (budget table).
func cost() -> Vector2i:
	return Vector2i(2, 2)
