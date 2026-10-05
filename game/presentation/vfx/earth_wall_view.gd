class_name EarthWallView
extends VfxEffect
## A raised low stone guard wall made of five chunky staggered blocks. Same rock shader family as
## StoneView (shared material, instance uniforms), so cracks / dust / tone match the stones.
##
##   setup(seed, width, height, thickness)   defaults 2.4 x 1.0 x 0.55 m; the wall stands on y = 0
##   set_rise(t01)       0 = buried (fully hidden under the ground), 1 = standing; centre blocks
##                       rise first, with a cubic ease-out; dust puffs at the base while rising
##   set_damage(t01)     0 = intact, 1 = heavily cracked (unlit cracks widen, darker, rubble tone)
##   set_heat(t01)       Thermal.heat01 of the wall: 0 = cold, 1 = at its melting point (cracks glow, as stones)
##
## The wall is hidden by the ground plane while buried (no clipping shader needed as long as the
## ground is opaque). Set `dust = false` to disable the base dust.

@export var dust: bool = true

var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _width: float = 2.4
var _height: float = 1.0
var _thick: float = 0.55
var _rise: float = 0.0
var _damage: float = 0.0
var _heat: float = 0.0
var _dust_a: DustPuffFX
var _dust_b: DustPuffFX
var _dust_stage: int = 0
var _seed: int = 0


func _init() -> void:
	_mi = MeshInstance3D.new()
	_mi.name = "Wall"
	_mat = VfxMaterials.make_stone()
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	add_child(_mi)
	setup(1, _width, _height, _thick)
	set_rise(0.0)


func setup(seed_value: int, width: float = 2.4, height: float = 1.0, thickness: float = 0.55) -> void:
	_seed = seed_value
	_width = maxf(width, 0.3)
	_height = maxf(height, 0.2)
	_thick = maxf(thickness, 0.15)
	_mi.mesh = VfxMesh.wall_mesh(seed_value)
	_mi.scale = Vector3(_width * 0.5, _height, _thick * 2.0)
	_apply()


## Same rock model, other materials: "" stone, "obsidian" (black glassy), "sand" (packed sandstone),
## "mud" (dark, half-glossy). Cheap: two uniforms.
func set_material_style(style: String) -> void:
	var tint := Vector3.ONE
	var glass := 0.0
	match style:
		"obsidian":
			tint = Vector3(0.32, 0.30, 0.36)
			glass = 1.0
		"sand":
			tint = Vector3(2.7, 2.15, 1.45)
		"mud":
			tint = Vector3(0.95, 0.72, 0.5)
			glass = 0.45
	_mat.set_shader_parameter("u_tint", tint)
	_mat.set_shader_parameter("u_glass", glass)


func set_rise(t01: float) -> void:
	var prev: float = _rise
	_rise = clampf(t01, 0.0, 1.0)
	_apply()
	if dust and _rise > prev:
		if _dust_stage == 0 and _rise > 0.04:
			_dust_stage = 1
			_play_dust(1.2)
		elif _dust_stage == 1 and _rise > 0.5:
			_dust_stage = 2
			_play_dust(0.8)
	if _rise <= 0.001:
		_dust_stage = 0
	visible = _rise > 0.001 or true


func set_damage(t01: float) -> void:
	_damage = clampf(t01, 0.0, 1.0)
	_apply()


## A wall heated by a lance / scorch / white core glows through its cracks like a heated stone.
func set_heat(t01: float) -> void:
	var h := clampf(t01, 0.0, 1.0)
	if absf(h - _heat) < 0.002:
		return
	_heat = h
	_mat.set_shader_parameter("u_heat", _heat)


func get_rise() -> float:
	return _rise


func get_damage() -> float:
	return _damage


func on_acquire() -> void:
	visible = true


func reset() -> void:
	_rise = 0.0
	_damage = 0.0
	_heat = 0.0
	_mat.set_shader_parameter("u_heat", 0.0)
	_dust_stage = 0
	set_material_style("")
	_apply()
	visible = false


func _apply() -> void:
	_mat.set_shader_parameter("u_seed", float(absi(_seed) % 977) + 0.5)
	_mat.set_shader_parameter("u_rise_height", 1.25)
	_mat.set_shader_parameter("u_detail", 2.2)
	_mat.set_shader_parameter("u_rise", _rise)
	_mat.set_shader_parameter("u_damage", _damage)


func _play_dust(strength: float) -> void:
	if _dust_a == null:
		_dust_a = DustPuffFX.new()
		_dust_b = DustPuffFX.new()
		add_child(_dust_a)
		add_child(_dust_b)
		_dust_a.top_level = true
		_dust_b.top_level = true
		_dust_a.manual_time = manual_time
		_dust_b.manual_time = manual_time
	var fx: DustPuffFX = _dust_a if _dust_stage <= 1 else _dust_b
	var half: float = _width * 0.5
	var xf: Transform3D = global_transform if is_inside_tree() else transform
	var right: Vector3 = xf.basis.x.normalized()
	for k in 2:
		var f: DustPuffFX = fx if k == 0 else (_dust_b if fx == _dust_a else _dust_a)
		var side: float = -0.55 if k == 0 else 0.55
		f.visible = true
		f.play(xf.origin + right * (half * side), Vector3.UP, strength)


func advance(dt: float) -> void:
	if manual_time:
		if _dust_a != null:
			_dust_a.advance(dt)
		if _dust_b != null:
			_dust_b.advance(dt)
