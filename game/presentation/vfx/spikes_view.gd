class_name SpikesView
extends VfxEffect
## Rows, rings and clusters of spikes as ONE MultiMesh draw: stone fangs / spike walls / spike lines
## (stone.gdshader, same rock look as StoneView), ice and glass spikes (crystal.gdshader), metal
## spikes (metal.gdshader). Spikes erupt with set_rise (shared uniform, per-spike stagger in placement).
##
##   setup(style, layout, seed, size)  style "stone" | "ice" | "glass" | "metal"; layout "row" (size.x = half
##                                     length, size.y = height) | "ring" (size.x = radius) | "cluster"
##   set_path(points, height)          a spike line along a world polyline (rebuilt when it grows)
##   set_rise(t01) / set_heat(h01)

const MAX_N := 16

var style := "stone"
var layout := "row"
var _mmi: MultiMeshInstance3D
var _mat: ShaderMaterial
var _rng := RandomNumberGenerator.new()
var _height := 1.0
var _sig := Vector3.INF
var _mats := {}


func _init() -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.instance_count = MAX_N
	mm.visible_instance_count = 0
	_mmi = MultiMeshInstance3D.new()
	_mmi.name = "Spikes"
	_mmi.multimesh = mm
	_mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	add_child(_mmi)
	reset()


func _material(st: String) -> ShaderMaterial:
	if _mats.has(st):
		return _mats[st]
	var m: ShaderMaterial
	match st:
		"ice", "glass":
			m = VfxMaterials.make_fx("crystal")
			var cs: Dictionary = CrystalView.STYLE[st]
			m.set_shader_parameter("tint", VfxPalette.v3(cs.tint))
			m.set_shader_parameter("frost", float(cs.frost))
			m.set_shader_parameter("opacity", float(cs.opacity) + 0.15)
			m.set_shader_parameter("edge_glow", float(cs.edge))
			m.set_shader_parameter("rise_height", 1.2)
		"metal":
			m = VfxMaterials.make_fx("metal")
		_:
			m = VfxMaterials.make_stone()
			m.set_shader_parameter("u_rise_height", 1.2)
			m.set_shader_parameter("u_detail", 1.6)
	_mats[st] = m
	return m


func setup(style_name: String, layout_name: String, seed_value: int, size: Vector3) -> void:
	style = style_name
	layout = layout_name
	_height = maxf(size.y, 0.2)
	_rng.seed = seed_value * 271 + 9
	_mat = _material(style)
	_mmi.material_override = _mat
	var crystal := style == "ice" or style == "glass"
	var mm := _mmi.multimesh
	mm.mesh = FxMesh.crystal_mesh(seed_value % 8, "shard") if crystal else FxMesh.spike_mesh()
	_mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF if crystal else GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	_sig = Vector3.INF
	if layout == "path":
		mm.visible_instance_count = 0
		return
	var n := 0
	match layout:
		"row":
			n = clampi(int(size.x * 4.0) + 3, 3, MAX_N)
			for i in n:
				var t := (float(i) + _rng.randf_range(-0.3, 0.3)) / float(maxi(n - 1, 1))
				_put(i, Vector3(lerpf(-size.x, size.x, t), 0.0, _rng.randf_range(-0.18, 0.18)), 1.0 - 0.4 * pow(absf(t * 2.0 - 1.0), 2.0))
		"ring":
			n = clampi(int(size.x * 6.0) + 4, 5, MAX_N)
			for i in n:
				var a := TAU * float(i) / float(n) + _rng.randf_range(-0.2, 0.2)
				_put(i, Vector3(cos(a), 0, sin(a)) * size.x, 1.0, Vector3(cos(a), 0, sin(a)) * 0.35)
		_:
			n = 7
			for i in n:
				var a2 := _rng.randf() * TAU
				var r := sqrt(_rng.randf()) * size.x * 0.6
				_put(i, Vector3(cos(a2) * r, 0, sin(a2) * r), 1.0 - 0.5 * r / maxf(size.x, 0.1), Vector3(cos(a2), 0, sin(a2)) * 0.4 * r)
	mm.visible_instance_count = n
	var e := size.x + 1.0
	_mmi.custom_aabb = AABB(Vector3(-e, -1.5, -e), Vector3(2.0 * e, _height * 1.6 + 1.6, 2.0 * e))
	set_rise(1.0)


func _put(i: int, p: Vector3, k: float, lean: Vector3 = Vector3.ZERO) -> void:
	var crystal := style == "ice" or style == "glass"
	var h := _height * k * _rng.randf_range(0.7, 1.05)
	var w := h * _rng.randf_range(0.16, 0.24)
	var up := (Vector3.UP + lean + Vector3(_rng.randf_range(-0.15, 0.15), 0, _rng.randf_range(-0.15, 0.15))).normalized()
	var x := up.cross(Vector3.FORWARD).normalized()
	var z := x.cross(up).normalized()
	var b := Basis(x * w, up * h, z * w)
	if crystal:
		b = Basis(x * w * 5.0, up * h, z * w * 5.0)   # shard mesh: radius 0.16, length 1 (centred)
		p += up * h * 0.38
	b = b * Basis(Vector3.UP, _rng.randf() * TAU)
	_mmi.multimesh.set_instance_transform(i, Transform3D(b, p))


## Spike line: spikes along a world polyline, growing toward the front (rebuilt when it moves on).
func set_path(points: PackedVector3Array, height: float) -> void:
	var n := points.size()
	if n < 2:
		return
	var sig := Vector3(points[n - 1].x, points[n - 1].z, float(n))
	if sig == _sig:
		return
	_sig = sig
	_place(Transform3D.IDENTITY)
	_height = height
	var total := 0.0
	for i in range(1, n):
		total += points[i].distance_to(points[i - 1])
	var k := clampi(int(total / 0.5) + 1, 2, MAX_N)
	var lo := points[0]
	var hi := points[0]
	for j in k:
		var target := total * (float(j) + 0.5) / float(k)
		var acc := 0.0
		var p: Vector3 = points[n - 1]
		for i in range(1, n):
			var seg := points[i].distance_to(points[i - 1])
			if acc + seg >= target:
				p = points[i - 1].lerp(points[i], (target - acc) / maxf(seg, 1e-4))
				break
			acc += seg
		_rng.seed = j * 977 + 3
		_put(j, p, lerpf(0.55, 1.0, float(j) / float(maxi(k - 1, 1))))
		lo = Vector3(minf(lo.x, p.x), minf(lo.y, p.y), minf(lo.z, p.z))
		hi = Vector3(maxf(hi.x, p.x), maxf(hi.y, p.y), maxf(hi.z, p.z))
	_mmi.multimesh.visible_instance_count = k
	_mmi.custom_aabb = AABB(lo - Vector3(1, 1.5, 1), hi - lo + Vector3(2, height * 1.6 + 1.6, 2))
	set_rise(1.0)


func set_rise(t01: float) -> void:
	_mat.set_shader_parameter("u_rise" if style == "stone" else "rise", clampf(t01, 0.0, 1.0))


func set_heat(h01: float) -> void:
	if style == "stone":
		_mat.set_shader_parameter("u_heat", clampf(h01, 0.0, 1.0))
	else:
		_mat.set_shader_parameter("heat", clampf(h01, 0.0, 1.0))


func on_acquire() -> void:
	visible = true


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_mmi.multimesh.visible_instance_count = 0
	_sig = Vector3.INF
	rotation = Vector3.ZERO
