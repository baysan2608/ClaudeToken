class_name CrystalView
extends VfxEffect
## Faceted transparent solids: glass and ice in every shape the kits make (shards / needles, spike
## clusters / fangs, walls, ridges, rime crests).
##
##   setup(mode, seed, style, size)   mode "shard" | "cluster" | "wall" | "ridge"; style "ice" | "glass";
##                                    size = node scale (shard: (r, length, r); wall: (half width, height, depth))
##   set_state(heat, frost, rise)     heat: molten-glass glow; frost: clear .. rime white; rise 0..1 grows
##                                    the crystals out of the ground (walls, spikes)
##   set_shatter(t)                   crack lines brighten before a break
##
## One alpha layer (crystal.gdshader), depth-written, no screen read, flat facets with bright edges.

const STYLE := {
	"ice": {"tint": Color(0.50, 0.82, 1.0), "frost": 0.35, "opacity": 0.55, "edge": 0.7},
	"glass": {"tint": Color(0.62, 0.95, 0.82), "frost": 0.08, "opacity": 0.46, "edge": 1.4},
}

var mode := "shard"
var style := "ice"
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _heat := -1.0
var _frost := -1.0
var _rise := -1.0


func _init() -> void:
	_mat = VfxMaterials.make_fx("crystal")
	_mi = MeshInstance3D.new()
	_mi.name = "Crystal"
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mi)
	setup("shard", 0, "ice", Vector3.ONE * 0.3)
	reset()


func setup(mode_name: String, seed_value: int, style_name: String, size: Vector3) -> void:
	mode = mode_name
	style = style_name if STYLE.has(style_name) else "ice"
	_mi.mesh = FxMesh.crystal_mesh(absi(seed_value) % 16, mode)
	_mi.scale = Vector3(maxf(size.x, 0.01), maxf(size.y, 0.01), maxf(size.z, 0.01))
	var st: Dictionary = STYLE[style]
	_mat.set_shader_parameter("tint", VfxPalette.v3(st.tint))
	_mat.set_shader_parameter("opacity", float(st.opacity))
	_mat.set_shader_parameter("edge_glow", float(st.edge))
	_mat.set_shader_parameter("rise_height", 1.15)
	_heat = -1.0
	_frost = -1.0
	_rise = -1.0
	set_state(0.0, float(st.frost), 1.0)


func set_state(heat: float, frost: float, rise: float) -> void:
	heat = clampf(heat, 0.0, 1.0)
	frost = clampf(frost, 0.0, 1.0)
	rise = clampf(rise, 0.0, 1.0)
	if heat != _heat:
		_heat = heat
		_mat.set_shader_parameter("heat", heat)
	if frost != _frost:
		_frost = frost
		_mat.set_shader_parameter("frost", frost)
	if rise != _rise:
		_rise = rise
		_mat.set_shader_parameter("rise", rise)


func set_shatter(t: float) -> void:
	_mat.set_shader_parameter("shatter", clampf(t, 0.0, 1.0))


func on_acquire() -> void:
	visible = true


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	rotation = Vector3.ZERO
	set_shatter(0.0)
