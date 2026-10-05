class_name ArenaProps
extends Node3D
## Original boundary dressing for the arena: wall-top coping + stringcourse, shallow piers, lantern posts,
## element banners (original glyphs, wind sway), planters with shrubs, corner finials, pillar braziers and
## wall-base grime strips.  Everything sits ON TOP of the boundary walls, on their inner faces (<= 14 cm
## relief, above head height for anything larger) or on pillar tops, so no prop ever occupies play space;
## ArenaMap (read-only) remains the only source of collision.
##
## Draw calls: all repeated parts are MultiMeshes (stone boxes, iron, roofs, glass, soil, foliage, one per
## banner column), so the whole set costs ~16 draws.  Lights (lantern pinwheel + brazier) are real OmniLights
## on quality 2 only and flicker gently in _process.

const SHADER_DIR := "res://presentation/shaders/"
const BANNER_TEX := "res://assets/textures/banners_albedo.png"
const COPING_H := 0.18
const BANNER_W := 0.9
const BANNER_H := 2.25
const FACE_N := 0
const FACE_S := 1
const FACE_W := 2
const FACE_E := 3

var _arena: ArenaMap
var _h := 16.0
var _top := 3.5
var _stone_mat: Material
var _quality := 1
var _lights: Array[OmniLight3D] = []
var _light_base: Array[float] = []
var _t := 0.0
# instance transforms per MultiMesh group
var _stone: Array[Transform3D] = []
var _iron: Array[Transform3D] = []
var _bronze: Array[Transform3D] = []
var _roof: Array[Transform3D] = []
var _glass: Array[Transform3D] = []
var _soil: Array[Transform3D] = []
var _leaf: Array[Transform3D] = []
var _banners: Array = [[], [], [], []]


func build(arena: ArenaMap, stone_mat: Material, quality: int = 1) -> void:
	_arena = arena
	_stone_mat = stone_mat
	_quality = quality
	_h = arena.half_size
	for s in arena.solids:
		if s.name == "north_wall":
			_top = s.max.y
	_lights.clear()
	_light_base.clear()
	for g in [_stone, _iron, _bronze, _roof, _glass, _soil, _leaf]:
		g.clear()
	for i in 4:
		_banners[i].clear()
	_build_walls()
	_build_pillar_braziers()
	_flush()
	_build_grunge()
	set_quality(quality)


func set_quality(q: int) -> void:
	_quality = q
	for l in _lights:
		l.visible = q >= 2
	set_process(q >= 2 and not _lights.is_empty())


func _process(dt: float) -> void:
	if _quality < 2 or _lights.is_empty():
		return
	_t += dt
	for i in _lights.size():
		var ph := float(i) * 1.7
		var f := 1.0 + 0.07 * sin(_t * 7.3 + ph) * sin(_t * 3.1 + ph * 0.6) + 0.03 * sin(_t * 17.0 + ph)
		_lights[i].light_energy = _light_base[i] * f


# --------------------------------------------------------------------------- wall frames
## World position from wall-frame coordinates: a = lateral world coordinate along the wall (x for N/S, z for
## W/E), d = depth INTO the arena from the wall's inner face (negative = inside the wall mass), y = height.
func _wpos(face: int, a: float, d: float, y: float) -> Vector3:
	match face:
		FACE_N:
			return Vector3(a, y, -_h + d)
		FACE_S:
			return Vector3(a, y, _h - d)
		FACE_W:
			return Vector3(-_h + d, y, a)
	return Vector3(_h - d, y, a)


## Axis-aligned box in wall-frame ranges -> instance transform (unit box scaled to size).
func _wbox(face: int, a0: float, a1: float, d0: float, d1: float, y0: float, y1: float) -> Transform3D:
	var p0 := _wpos(face, a0, d0, y0)
	var p1 := _wpos(face, a1, d1, y1)
	var mn := Vector3(minf(p0.x, p1.x), y0, minf(p0.z, p1.z))
	var mx := Vector3(maxf(p0.x, p1.x), y1, maxf(p0.z, p1.z))
	return Transform3D(Basis.from_scale(mx - mn), (mn + mx) * 0.5)


func _face_basis(face: int) -> Basis:
	# columns: along (texture u), up, inward (out of the wall, toward the arena)
	match face:
		FACE_N:
			return Basis(Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1))
		FACE_S:
			return Basis(Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, -1))
		FACE_W:
			return Basis(Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0))
	return Basis(Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(-1, 0, 0))


## True when a non-boundary solid taller than 0.6 m abuts the wall within [a - w, a + w].
func _blocked(face: int, a: float, w: float) -> bool:
	for s in _arena.solids:
		if str(s.name).ends_with("_wall") and str(s.name) != "cover_wall":
			continue
		var mn: Vector3 = s.min
		var mx: Vector3 = s.max
		if mx.y < 0.6:
			continue
		var lat0 := mn.x if face <= FACE_S else mn.z
		var lat1 := mx.x if face <= FACE_S else mx.z
		if a + w < lat0 or a - w > lat1:
			continue
		# distance of the solid from the wall's inner face
		var dist := 0.0
		match face:
			FACE_N:
				dist = mn.z + _h
			FACE_S:
				dist = _h - mx.z
			FACE_W:
				dist = mn.x + _h
			FACE_E:
				dist = _h - mx.x
		if dist < 0.5:
			return true
	return false


# --------------------------------------------------------------------------- walls
func _build_walls() -> void:
	var span := _h + 1.15
	var pier_t := [-12.0, -4.0, 4.0, 12.0]
	var mid_t := [-8.0, 0.0, 8.0]
	for face in 4:
		var wall_end := span if face <= FACE_S else _h - 0.2
		# coping slab (overhangs 0.22 m inward at 3.5 m, i.e. above head height) and stringcourse band
		_stone.append(_wbox(face, -wall_end, wall_end, -1.15, 0.22, _top, _top + COPING_H))
		var sc := _h - 0.07 if face <= FACE_S else _h - 0.14
		_stone.append(_wbox(face, -sc, sc, 0.0, 0.07, _top - 0.2, _top - 0.06))
		for t in pier_t:
			_add_pier(face, t)
			_add_lantern(face, t, is_equal_approx(t, -4.0 if face <= FACE_S else 4.0))
		for i in mid_t.size():
			var t: float = mid_t[i]
			_add_planter(face, t)
			if not _blocked(face, t, BANNER_W * 0.5 + 0.1):
				_add_banner(face, t, (i + face) % 4)
		# corner finials on the wall top
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			var cx: float = sx * (_h + 0.5)
			var cz: float = sz * (_h + 0.5)
			_stone.append(Transform3D(Basis.from_scale(Vector3(1.0, 0.5, 1.0)), Vector3(cx, _top + COPING_H + 0.25, cz)))
			_stone.append(Transform3D(Basis.from_scale(Vector3(0.7, 0.55, 0.7)), Vector3(cx, _top + COPING_H + 0.5 + 0.275, cz)))
			_roof.append(Transform3D(Basis(Vector3.UP, PI * 0.25).scaled(Vector3(0.62, 0.34, 0.62)), Vector3(cx, _top + COPING_H + 1.05 + 0.17, cz)))


func _add_pier(face: int, t: float) -> void:
	# shallow pilaster, 10 cm relief; base plinth and capital 14 cm
	_stone.append(_wbox(face, t - 0.27, t + 0.27, 0.0, 0.1, 0.35, _top - 0.2))
	_stone.append(_wbox(face, t - 0.34, t + 0.34, 0.0, 0.14, 0.0, 0.35))
	_stone.append(_wbox(face, t - 0.34, t + 0.34, 0.0, 0.14, _top - 0.2, _top))


func _add_lantern(face: int, t: float, lit: bool) -> void:
	var d := -0.4
	var y0 := _top + COPING_H
	# stone plinth, iron base plate, four corner posts, glass core, roof cone, finial
	_stone.append(_wbox(face, t - 0.22, t + 0.22, d - 0.22, d + 0.22, y0, y0 + 0.42))
	var yb := y0 + 0.42
	_iron.append(_wbox(face, t - 0.19, t + 0.19, d - 0.19, d + 0.19, yb, yb + 0.04))
	var gy0 := yb + 0.04
	var gy1 := gy0 + 0.36
	for sa in [-1.0, 1.0]:
		for sd in [-1.0, 1.0]:
			_iron.append(_wbox(face, t + sa * 0.15 - 0.015, t + sa * 0.15 + 0.015, d + sd * 0.15 - 0.015, d + sd * 0.15 + 0.015, gy0, gy1))
	_glass.append(_wbox(face, t - 0.12, t + 0.12, d - 0.12, d + 0.12, gy0 + 0.02, gy1 - 0.02))
	_iron.append(_wbox(face, t - 0.19, t + 0.19, d - 0.19, d + 0.19, gy1, gy1 + 0.035))
	var c := _wpos(face, t, d, gy1 + 0.035 + 0.11)
	_roof.append(Transform3D(Basis(Vector3.UP, PI * 0.25).scaled(Vector3(0.5, 0.22, 0.5)), c))
	_bronze.append(_wbox(face, t - 0.025, t + 0.025, d - 0.025, d + 0.025, gy1 + 0.25, gy1 + 0.31))
	if lit:
		var l := OmniLight3D.new()
		l.name = "LanternLight"
		l.position = _wpos(face, t, 0.35, gy0 + 0.2)
		l.light_color = Color(1.0, 0.64, 0.32)
		l.light_energy = 2.0
		l.omni_range = 11.0
		l.omni_attenuation = 1.6
		l.shadow_enabled = false
		l.light_specular = 0.6
		add_child(l)
		_lights.append(l)
		_light_base.append(l.light_energy)


func _add_planter(face: int, t: float) -> void:
	var d := -0.5
	var y0 := _top + COPING_H
	# stone trough (open top: four walls + soil slab), shrubs as leaf blobs
	_stone.append(_wbox(face, t - 0.62, t + 0.62, d - 0.24, d + 0.24, y0, y0 + 0.3))
	_soil.append(_wbox(face, t - 0.55, t + 0.55, d - 0.17, d + 0.17, y0 + 0.28, y0 + 0.34))
	var rng := RandomNumberGenerator.new()
	rng.seed = hash([face, t])
	for i in 7:
		var a: float = t + rng.randf_range(-0.5, 0.5)
		var dd: float = d + rng.randf_range(-0.09, 0.09)
		var r: float = rng.randf_range(0.17, 0.27)
		var y: float = y0 + 0.34 + r * 0.55 + rng.randf_range(0.0, 0.16)
		var p := _wpos(face, a, dd, y)
		_leaf.append(Transform3D(Basis.from_scale(Vector3(r * 1.2, r, r * 1.2)), p))


func _add_banner(face: int, t: float, col: int) -> void:
	var rod_y := _top - 0.28
	# rod + two wall brackets (all within 7 cm of the face)
	_iron.append(_wbox(face, t - 0.52, t + 0.52, 0.035, 0.065, rod_y - 0.015, rod_y + 0.015))
	for s in [-0.46, 0.46]:
		_iron.append(_wbox(face, t + s - 0.012, t + s + 0.012, 0.0, 0.05, rod_y - 0.012, rod_y + 0.012))
	var origin := _wpos(face, t, 0.04, rod_y - 0.02)
	_banners[col].append(Transform3D(_face_basis(face), origin))


func _build_pillar_braziers() -> void:
	for s in _arena.solids:
		if s.kind != "pillar":
			continue
		var mn: Vector3 = s.min
		var mx: Vector3 = s.max
		var c := (mn + mx) * 0.5
		var top: float = mx.y
		# capital slab + bronze bowl + ember glow
		_stone.append(Transform3D(Basis.from_scale(Vector3(mx.x - mn.x + 0.3, 0.16, mx.z - mn.z + 0.3)), Vector3(c.x, top + 0.08, c.z)))
		_bronze.append(Transform3D(Basis.from_scale(Vector3(0.62, 0.22, 0.62)), Vector3(c.x, top + 0.16 + 0.11, c.z)))
		_glass.append(Transform3D(Basis.from_scale(Vector3(0.44, 0.12, 0.44)), Vector3(c.x, top + 0.16 + 0.24, c.z)))
		var l := OmniLight3D.new()
		l.name = "BrazierLight"
		l.position = Vector3(c.x, top + 0.75, c.z)
		l.light_color = Color(1.0, 0.55, 0.22)
		l.light_energy = 2.6
		l.omni_range = 9.0
		l.omni_attenuation = 1.5
		l.shadow_enabled = false
		add_child(l)
		_lights.append(l)
		_light_base.append(l.light_energy)


# --------------------------------------------------------------------------- MultiMesh assembly
func _mm(nm: String, mesh: Mesh, mat: Material, xf: Array[Transform3D], shadows := true) -> void:
	if xf.is_empty():
		return
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = mesh
	mm.instance_count = xf.size()
	for i in xf.size():
		mm.set_instance_transform(i, xf[i])
	var mi := MultiMeshInstance3D.new()
	mi.name = nm
	mi.multimesh = mm
	mi.material_override = mat
	if not shadows:
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)


func _std(c: Color, rough: float, metal: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.metallic = metal
	return m


func _flush() -> void:
	var box := BoxMesh.new()
	box.size = Vector3.ONE
	_mm("Stone", box, _stone_mat, _stone)
	_mm("Iron", box, _std(Color(0.07, 0.07, 0.08), 0.5, 0.75), _iron)
	_mm("Bronze", box, _std(Color(0.5, 0.34, 0.16), 0.38, 0.85), _bronze)
	_mm("Soil", box, _std(Color(0.1, 0.07, 0.05), 0.95, 0.0), _soil, false)
	var glow := _shader_mat("lantern_glow")
	_mm("Glass", box, glow if glow else _std(Color(1.0, 0.7, 0.3), 0.3, 0.0), _glass, false)
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.5
	cone.height = 1.0
	cone.radial_segments = 4
	cone.rings = 1
	cone.cap_bottom = false
	_mm("Roofs", cone, _std(Color(0.2, 0.17, 0.14), 0.55, 0.6), _roof)
	var blob := SphereMesh.new()
	blob.radius = 0.5
	blob.height = 0.8
	blob.radial_segments = 8
	blob.rings = 4
	var fol := _shader_mat("foliage")
	if fol:
		fol.set_shader_parameter("gradient_height", 0.8)
		fol.set_shader_parameter("sway", 0.03)
	_mm("Shrubs", blob, fol if fol else _std(Color(0.16, 0.26, 0.09), 0.9, 0.0), _leaf, false)
	var cloth := _banner_mesh()
	for col in 4:
		var m := _shader_mat("banner_cloth")
		if m:
			m.set_shader_parameter("column", col)
			if ResourceLoader.exists(BANNER_TEX):
				m.set_shader_parameter("banner_tex", load(BANNER_TEX))
		var xf: Array[Transform3D] = []
		xf.assign(_banners[col])
		_mm("Banners%d" % col, cloth, m if m else _std(Color(0.5, 0.2, 0.15), 0.9, 0.0), xf, false)


func _shader_mat(nm: String) -> ShaderMaterial:
	var p := SHADER_DIR + nm + ".gdshader"
	if not ResourceLoader.exists(p):
		return null
	var m := ShaderMaterial.new()
	m.shader = load(p)
	return m


## Banner cloth: 6 x 12 grid, hanging down from the rod (origin) in local -Y, front facing +Z.
func _banner_mesh() -> ArrayMesh:
	var nx := 6
	var ny := 12
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var vt := func(i: int, j: int) -> void:
		var u := float(i) / nx
		var v := float(j) / ny
		st.set_normal(Vector3(0, 0, 1))
		st.set_uv(Vector2(u, v))
		st.add_vertex(Vector3((u - 0.5) * BANNER_W, -v * BANNER_H, 0.0))
	for j in ny:
		for i in nx:
			# Godot front faces wind clockwise: seen from +Z with x right, y up
			vt.call(i, j)
			vt.call(i + 1, j)
			vt.call(i + 1, j + 1)
			vt.call(i, j)
			vt.call(i + 1, j + 1)
			vt.call(i, j + 1)
	return st.commit()


func _build_grunge() -> void:
	var m := _shader_mat("grunge_decal")
	if m == null:
		return
	# one ragged band along each wall base (UV.y = 0 at the wall)
	for face in 4:
		var mi := MeshInstance3D.new()
		var pm := PlaneMesh.new()
		pm.size = Vector2(2.0 * _h - 0.2, 1.7)
		mi.mesh = pm
		mi.material_override = m
		mi.name = "BaseGrime%d" % face
		var ang: float = [0.0, PI, PI * 0.5, -PI * 0.5][face]
		mi.rotation = Vector3(0, ang, 0)
		mi.position = _wpos(face, 0.0, 0.85, 0.004)
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(mi)
