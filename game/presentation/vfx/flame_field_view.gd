class_name FlameFieldView
extends VfxEffect
## Standing flames as ONE MultiMesh draw (flame_field.gdshader on VfxMesh.flame_mesh): fire field
## zones, fire line fronts, burning status on a fighter, flame columns. Every tongue flickers on its
## own clock; flame or blue-fire colours. Optional warm/cool omni light (shadowless, capped).
##
##   setup(mode, seed, radius, height, blue = false)   mode "field" | "line" | "burning" | "column"
##   set_path(points)            line mode: flames along a world polyline (<= once per frame)
##   set_intensity(i01)          fades the field in / out (also shrinks the tongues)

const MAX_N := 14
const LITE_N := 8
const FLAME_COLS := [Color(0.62, 0.07, 0.01), Color(1.0, 0.36, 0.05), Color(1.0, 0.78, 0.30), Color(1.0, 0.95, 0.75)]
const BLUE_COLS := [Color(0.10, 0.12, 0.55), Color(0.25, 0.45, 1.0), Color(0.55, 0.85, 1.0), Color(0.95, 0.98, 1.0)]

var mode := "field"
var _mmi: MultiMeshInstance3D
var _mat: ShaderMaterial
var _light: OmniLight3D
var _scroll := 0.0
var _n := 0
var _rng := RandomNumberGenerator.new()
var _radius := 1.5
var _height := 0.8
var _intensity := 1.0
var _sig := Vector3.INF
var use_light := true


func _init() -> void:
	_mat = VfxMaterials.make_fx("flame_field")
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_custom_data = true
	mm.mesh = VfxMesh.flame_mesh()
	mm.instance_count = MAX_N
	mm.visible_instance_count = 0
	_mmi = MultiMeshInstance3D.new()
	_mmi.name = "Flames"
	_mmi.multimesh = mm
	_mmi.material_override = _mat
	_mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mmi)
	_light = OmniLight3D.new()
	_light.name = "Glow"
	_light.shadow_enabled = false
	_light.omni_attenuation = 1.6
	_light.visible = false
	add_child(_light)
	reset()


func setup(mode_name: String, seed_value: int, radius: float, height: float, blue: bool = false) -> void:
	mode = mode_name
	_radius = maxf(radius, 0.1)
	_height = maxf(height, 0.1)
	_rng.seed = seed_value * 613 + 11
	var cols: Array = BLUE_COLS if blue else FLAME_COLS
	for i in 4:
		_mat.set_shader_parameter(["col_deep", "col_mid", "col_hot", "col_white"][i], VfxPalette.v3(cols[i]))
	_light.light_color = Color(0.45, 0.65, 1.0) if blue else Color(1.0, 0.55, 0.22)
	_mat.set_shader_parameter("flame_h", _height)
	_mat.set_shader_parameter("flame_w", clampf(_height * 0.3, 0.06, 0.4))
	_mat.set_shader_parameter("cover", 0.6 if blue else 0.8)
	_sig = Vector3.INF
	var cap := LITE_N if VfxMaterials.lite() else MAX_N
	var mm := _mmi.multimesh
	match mode:
		"field", "burning", "column":
			_n = clampi(int(_radius * _radius * 2.2) + 4, 4, cap) if mode == "field" else (5 if mode == "burning" else 6)
			for i in _n:
				var a := _rng.randf() * TAU
				var r := sqrt((float(i) + 0.5) / float(_n)) * _radius * (0.85 if mode == "field" else 0.6)
				var p := Vector3(cos(a + float(i) * 2.4) * r, 0.0, sin(a + float(i) * 2.4) * r)
				if mode == "burning":
					p.y = _rng.randf_range(0.2, 1.1)
				mm.set_instance_transform(i, Transform3D(Basis(Vector3.UP, _rng.randf() * TAU), p))
				var hs := _rng.randf_range(0.6, 1.1) * (1.0 - 0.35 * r / maxf(_radius, 0.01) if mode == "field" else 1.0)
				mm.set_instance_custom_data(i, Color(_rng.randf(), hs, _rng.randf_range(0.8, 1.2), 1.0))
			mm.visible_instance_count = _n
			var e := _radius + 1.0
			_mmi.custom_aabb = AABB(Vector3(-e, -0.3, -e), Vector3(2.0 * e, _height * 1.6 + 1.5, 2.0 * e))
		"line":
			_n = 0
			mm.visible_instance_count = 0
	_light.omni_range = clampf(_radius * 2.5 + 2.0, 3.0, 8.0)
	_light.position = Vector3(0, _height * 0.6, 0)
	set_intensity(1.0)


func set_path(points: PackedVector3Array) -> void:
	var n := points.size()
	if mode != "line" or n < 2:
		return
	var sig := Vector3(points[n - 1].x, points[n - 1].z, float(n))
	if sig == _sig:
		return
	_sig = sig
	_place(Transform3D.IDENTITY)
	var total := 0.0
	for i in range(1, n):
		total += points[i].distance_to(points[i - 1])
	var cap := LITE_N if VfxMaterials.lite() else MAX_N
	_n = clampi(int(total / 0.45) + 1, 2, cap)
	var mm := _mmi.multimesh
	var lo := points[0]
	var hi := points[0]
	for k in _n:
		var target := total * (float(k) + 0.5) / float(_n)
		var acc := 0.0
		var p: Vector3 = points[n - 1]
		for i in range(1, n):
			var seg := points[i].distance_to(points[i - 1])
			if acc + seg >= target:
				p = points[i - 1].lerp(points[i], (target - acc) / maxf(seg, 1e-4))
				break
			acc += seg
		var hs := lerpf(0.55, 1.15, float(k) / float(maxi(_n - 1, 1)))   # taller toward the front
		mm.set_instance_transform(k, Transform3D(Basis(Vector3.UP, float(k) * 2.1), p))
		mm.set_instance_custom_data(k, Color(fposmod(float(k) * 0.618, 1.0), hs, 1.0, 1.0))
		lo = Vector3(minf(lo.x, p.x), minf(lo.y, p.y), minf(lo.z, p.z))
		hi = Vector3(maxf(hi.x, p.x), maxf(hi.y, p.y), maxf(hi.z, p.z))
	mm.visible_instance_count = _n
	_mmi.custom_aabb = AABB(lo - Vector3(1, 0.5, 1), hi - lo + Vector3(2, _height * 1.6 + 1.5, 2))
	_light.position = points[n - 1] + Vector3(0, _height * 0.6, 0)


func set_intensity(i01: float) -> void:
	_intensity = clampf(i01, 0.0, 1.0)
	_mat.set_shader_parameter("intensity", _intensity)
	_light.visible = use_light and _intensity > 0.1 and mode != "burning"
	_light.light_energy = 0.9 * _intensity


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_light.visible = false
	_mmi.multimesh.visible_instance_count = 0
	_sig = Vector3.INF


func advance(dt: float) -> void:
	_scroll += dt
	_mat.set_shader_parameter("scroll", _scroll)
	if _light.visible:
		_light.light_energy = 0.9 * _intensity * (0.85 + 0.15 * sin(_scroll * 13.0) * sin(_scroll * 7.3))
