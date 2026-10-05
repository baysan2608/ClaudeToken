class_name GroundStripView
extends LavaWaveView
## Travelling ground fronts that share the lava wave's strip geometry (same set_path / set_state
## contract as LavaWaveView): water wave (foam lip, translucent), sand surge (granular, opaque),
## rime / ice ridge (frosted, glossy), mud. The style swaps the material and the cross-section.
##
##   configure(style, seed)   "water" | "sand" | "rime" | "mud"
##   set_state(melt, crust, flow_speed)   crust > 0 freezes a water wave / settles sand

const PROFILE := {
	"water": [0.85, 0.22, 0.45],   # height front, tail, front bulge
	"sand": [0.55, 0.18, 0.35],
	"rime": [0.22, 0.10, 0.1],
	"mud": [0.25, 0.12, 0.2],
}

var style := "water"


func configure(style_name: String, seed_value: int = 0) -> void:
	style = style_name if PROFILE.has(style_name) else "water"
	if style == "water":
		_mat = VfxMaterials.make_fx("strip_water")
	else:
		_mat = VfxMaterials.make_fx("strip_ground")
		_mat.set_shader_parameter("style", {"sand": 0.0, "rime": 1.0, "mud": 2.0}[style])
	_mat.set_shader_parameter("seed", float(absi(seed_value) % 53) * 0.173)
	_mi.material_override = _mat
	var p: Array = PROFILE[style]
	height_front = p[0]
	height_tail = p[1]
	front_bulge = p[2]
	_push_state()


## Draw calls / transparent layers (budget table).
func cost() -> Vector2i:
	return Vector2i(1, 1 if style == "water" else 0)
