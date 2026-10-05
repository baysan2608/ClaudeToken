class_name ArenaScenery
extends Node3D
## The world beyond the arena walls, kept entirely outside the play space and inside the gameplay camera's
## 120 m far plane: packed-earth / dry-grass ground, two hill silhouette layers, a ring of hip-roofed halls
## with a few lit windows, cypress and round-canopy trees.  Everything is deterministic (fixed seeds) and
## drawn with a handful of MultiMeshes (about 10 draw calls), no shadows cast.

const SHADER_DIR := "res://presentation/shaders/"

var _quality := 1
var _trees: Array[MultiMeshInstance3D] = []


func build(arena: ArenaMap, quality: int = 1) -> void:
	_quality = quality
	var h := arena.half_size + 1.0     # outer face of the boundary walls
	_build_ground(h)
	_build_hills()
	_build_halls(h)
	_build_trees(h)
	set_quality(quality)


func set_quality(q: int) -> void:
	_quality = q
	for t in _trees:
		t.visible = q >= 1


func _shader_mat(nm: String) -> ShaderMaterial:
	var p := SHADER_DIR + nm + ".gdshader"
	if not ResourceLoader.exists(p):
		return null
	var m := ShaderMaterial.new()
	m.shader = load(p)
	return m


func _std(c: Color, rough := 0.9) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	return m


func _quad(mn: Vector2, mx: Vector2, y: float, mat: Material, nm: String) -> void:
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = mx - mn
	mi.mesh = pm
	mi.position = Vector3((mn.x + mx.x) * 0.5, y, (mn.y + mx.y) * 0.5)
	mi.material_override = mat
	mi.name = nm
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)


func _build_ground(h: float) -> void:
	var m: Material = _shader_mat("outer_ground")
	if m == null:
		m = _std(Color(0.32, 0.28, 0.17))
	var far := 140.0
	# four rectangles around the wall footprint, slightly below the floor line so wall bases overlap them
	_quad(Vector2(-far, -far), Vector2(far, -h), -0.03, m, "ground_n")
	_quad(Vector2(-far, h), Vector2(far, far), -0.03, m, "ground_s")
	_quad(Vector2(-far, -h), Vector2(-h, h), -0.03, m, "ground_w")
	_quad(Vector2(h, -h), Vector2(far, h), -0.03, m, "ground_e")


func _build_hills() -> void:
	var layers := [
		{"r": 86.0, "h0": 3.0, "h1": 9.0, "col": Color(0.3, 0.32, 0.24), "seed": 11, "f": 3.0},
		{"r": 102.0, "h0": 7.0, "h1": 18.0, "col": Color(0.46, 0.45, 0.42), "seed": 23, "f": 2.0},
	]
	var mat := _shader_mat("hills")
	if mat == null:
		mat = null
	for L in layers:
		var noise := FastNoiseLite.new()
		noise.seed = L.seed
		noise.frequency = 1.0
		noise.fractal_octaves = 3
		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		var n := 96
		var r: float = L.r
		var col: Color = L.col
		for i in n:
			var a0 := TAU * float(i) / n
			var a1 := TAU * float(i + 1) / n
			var h0 := _hill_h(noise, a0, L)
			var h1 := _hill_h(noise, a1, L)
			var p00 := Vector3(cos(a0) * r, h0, sin(a0) * r)
			var p10 := Vector3(cos(a1) * r, h1, sin(a1) * r)
			var b00 := Vector3(p00.x, -8.0, p00.z)
			var b10 := Vector3(p10.x, -8.0, p10.z)
			var top0 := col.lerp(Color(0.86, 0.72, 0.58), 0.16 * clampf(h0 / float(L.h1), 0.0, 1.0))
			var top1 := col.lerp(Color(0.86, 0.72, 0.58), 0.16 * clampf(h1 / float(L.h1), 0.0, 1.0))
			var low := col * 0.8
			for v in [[p00, top0], [p10, top1], [b10, low], [p00, top0], [b10, low], [b00, low]]:
				st.set_color(v[1])
				st.add_vertex(v[0])
		var mi := MeshInstance3D.new()
		mi.mesh = st.commit()
		mi.material_override = mat if mat else _std(col)
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		mi.name = "hills_%d" % int(r)
		add_child(mi)


func _hill_h(noise: FastNoiseLite, a: float, L: Dictionary) -> float:
	var f: float = L.f
	var x := cos(a) * f
	var z := sin(a) * f
	var n := noise.get_noise_2d(x, z) * 0.5 + 0.5
	return lerpf(L.h0, L.h1, n)


func _mm(nm: String, mesh: Mesh, mat: Material, xf: Array[Transform3D]) -> MultiMeshInstance3D:
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
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)
	return mi


func _build_halls(h: float) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 4242
	var walls: Array[Transform3D] = []
	var roofs: Array[Transform3D] = []
	var windows: Array[Transform3D] = []
	var n := 13
	for i in n:
		var ang := TAU * (float(i) + rng.randf_range(-0.25, 0.25)) / n
		var dist := rng.randf_range(34.0, 62.0)
		var w := rng.randf_range(8.0, 15.0)
		var d := rng.randf_range(7.0, 11.0)
		var hh := rng.randf_range(5.0, 8.5)
		var pos := Vector3(cos(ang) * dist, 0.0, sin(ang) * dist)
		var yaw := atan2(-pos.x, -pos.z) + rng.randf_range(-0.15, 0.15)   # front faces the arena
		var basis := Basis(Vector3.UP, yaw)
		walls.append(Transform3D(basis.scaled_local(Vector3(w, hh, d)), pos + Vector3(0, hh * 0.5 - 0.1, 0)))
		var rh := rng.randf_range(3.0, 4.6)
		# pyramid roof (4-sided cone, rotated to square up) overhanging the walls a little
		var rb := Basis(Vector3.UP, yaw + PI * 0.25).scaled_local(Vector3(w * 1.42 + 1.4, rh, d * 1.42 + 1.4))
		roofs.append(Transform3D(rb, pos + Vector3(0, hh - 0.1 + rh * 0.5, 0)))
		# two rows of lit windows on the arena-facing side
		for k in 4:
			var lx := (float(k) - 1.5) * (w / 4.2)
			var fwd := basis * Vector3(lx, hh * 0.45, d * 0.5 + 0.03)
			var wb := basis.scaled_local(Vector3(0.9, 1.5, 0.12))
			windows.append(Transform3D(wb, pos + fwd))
	var box := BoxMesh.new()
	box.size = Vector3.ONE
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.5
	cone.height = 1.0
	cone.radial_segments = 4
	cone.rings = 1
	_mm("halls", box, _std(Color(0.7, 0.6, 0.47), 0.95), walls)
	_mm("hall_roofs", cone, _std(Color(0.3, 0.18, 0.13), 0.8), roofs)
	var glow := _shader_mat("lantern_glow")
	if glow:
		glow.set_shader_parameter("glow_energy", 1.5)
	_mm("hall_windows", box, glow if glow else _std(Color(1.0, 0.7, 0.35)), windows)


func _build_trees(h: float) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 777
	var cones: Array[Transform3D] = []
	var blobs: Array[Transform3D] = []
	var trunks: Array[Transform3D] = []
	var tries := 0
	while cones.size() + blobs.size() < 56 and tries < 400:
		tries += 1
		var ang := rng.randf() * TAU
		var dist := rng.randf_range(23.0, 66.0)
		var p := Vector3(cos(ang) * dist, 0.0, sin(ang) * dist)
		if rng.randf() < 0.45:
			var ht := rng.randf_range(5.0, 9.0)
			var wd := rng.randf_range(1.2, 2.0)
			cones.append(Transform3D(Basis.from_scale(Vector3(wd, ht, wd)), p + Vector3(0, ht * 0.5 - 0.2, 0)))
		else:
			var r := rng.randf_range(2.0, 3.4)
			var th := rng.randf_range(2.2, 3.6)
			trunks.append(Transform3D(Basis.from_scale(Vector3(0.4, th, 0.4)), p + Vector3(0, th * 0.5, 0)))
			blobs.append(Transform3D(Basis.from_scale(Vector3(r * 2.0, r * 1.7, r * 2.0)), p + Vector3(0, th + r * 0.55, 0)))
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.5
	cone.height = 1.0
	cone.radial_segments = 6
	cone.rings = 1
	cone.cap_bottom = false
	var sph := SphereMesh.new()
	sph.radius = 0.5
	sph.height = 1.0
	sph.radial_segments = 8
	sph.rings = 5
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.5
	cyl.bottom_radius = 0.6
	cyl.height = 1.0
	cyl.radial_segments = 5
	cyl.rings = 1
	var f1 := _shader_mat("foliage")
	var f2 := _shader_mat("foliage")
	if f1:
		f1.set_shader_parameter("gradient_height", 1.0)
		f1.set_shader_parameter("color_low", Color(0.04, 0.1, 0.04))
		f1.set_shader_parameter("color_high", Color(0.2, 0.3, 0.11))
		f1.set_shader_parameter("sway", 0.0)
		f2.set_shader_parameter("gradient_height", 1.0)
		f2.set_shader_parameter("color_low", Color(0.1, 0.18, 0.05))
		f2.set_shader_parameter("color_high", Color(0.45, 0.5, 0.17))
		f2.set_shader_parameter("sway", 0.0)
	_trees.append(_mm("cypresses", cone, f1 if f1 else _std(Color(0.1, 0.18, 0.08)), cones))
	_trees.append(_mm("canopies", sph, f2 if f2 else _std(Color(0.2, 0.28, 0.1)), blobs))
	_trees.append(_mm("trunks", cyl, _std(Color(0.2, 0.14, 0.09)), trunks))
