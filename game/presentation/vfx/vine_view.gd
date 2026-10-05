class_name VineView
extends VfxEffect
## Plant bodies (Water / Plant): vine lash / seed trail, lattice wall, roots front, briar ring and the
## rooted status (vines climbing the legs). All tubes of one view are ONE opaque mesh (vine.gdshader);
## growth is a uniform (vertices past it collapse), so growing never rebuilds geometry.
##
##   setup(mode, seed, size)        mode "lattice" (size = wall half extents) | "briar" (size.x = radius)
##                                  | "rooted" (size.x = radius) | "roots" / "lash" (paths via set_path)
##   set_path(points, radius)       roots / lash: rebuild along a world polyline (<= once per frame)
##   set_state(grow, burn, frozen)

var mode := "lattice"
var _mi: MeshInstance3D
var _mesh: ArrayMesh
var _mat: ShaderMaterial
var _phase := 0.0
var _tris := 0
var _rng := RandomNumberGenerator.new()
var _sig := Vector4.INF


func _init() -> void:
	_mesh = ArrayMesh.new()
	_mat = VfxMaterials.make_fx("vine")
	_mi = MeshInstance3D.new()
	_mi.name = "Vines"
	_mi.mesh = _mesh
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	_mi.extra_cull_margin = 1.0
	add_child(_mi)
	reset()


func setup(mode_name: String, seed_value: int, size: Vector3) -> void:
	mode = mode_name
	_rng.seed = seed_value * 131 + 5
	var paths: Array = []
	var radii := PackedFloat32Array()
	match mode:
		"lattice":
			var hw := maxf(size.x, 0.3)
			var hh := maxf(size.y, 0.3) * 2.0
			for k in 7:
				var x0 := lerpf(-hw * 1.1, hw * 1.1, float(k) / 6.0)
				var dir := 1.0 if k % 2 == 0 else -1.0
				var p := PackedVector3Array()
				for j in 9:
					var t := float(j) / 8.0
					p.append(Vector3(x0 + dir * t * hw * 0.9 + sin(t * 7.0 + k) * 0.06, t * hh, sin(t * 5.0 + k * 1.3) * 0.08))
				paths.append(p)
				radii.append(_rng.randf_range(0.045, 0.07))
			for k in 2:
				var p2 := PackedVector3Array()
				var y := hh * (0.35 + 0.35 * k)
				for j in 9:
					var t2 := float(j) / 8.0
					p2.append(Vector3(lerpf(-hw, hw, t2), y + sin(t2 * 9.0 + k) * 0.08, 0.06 * sin(t2 * 4.0)))
				paths.append(p2)
				radii.append(0.04)
		"briar", "rooted":
			var r := maxf(size.x, 0.2)
			var n := 6 if mode == "briar" else 4
			for k in n:
				var a0 := TAU * float(k) / float(n) + _rng.randf_range(-0.3, 0.3)
				var p3 := PackedVector3Array()
				var hgt := (0.7 if mode == "briar" else 0.95) * _rng.randf_range(0.7, 1.1)
				for j in 8:
					var t3 := float(j) / 7.0
					var a := a0 + t3 * (2.2 if mode == "rooted" else 1.2)
					var rr := r * (1.0 - (0.45 if mode == "rooted" else 0.1) * t3)
					p3.append(Vector3(cos(a) * rr, -0.05 + t3 * hgt, sin(a) * rr))
				paths.append(p3)
				radii.append(_rng.randf_range(0.035, 0.055))
	if not paths.is_empty():
		_tris = FxMesh.build_tubes(_mesh, paths, radii, 6)
	_sig = Vector4.INF
	set_state(1.0, 0.0, 0.0)


## Roots / lash along a world polyline (tail -> tip). Roots arch out of the ground in 3 strands.
func set_path(points: PackedVector3Array, radius: float) -> void:
	var n := points.size()
	if n < 2:
		_mesh.clear_surfaces()
		return
	var sig := Vector4(points[n - 1].x, points[n - 1].z, float(n), radius)
	if sig == _sig:
		return
	_sig = sig
	_place(Transform3D.IDENTITY)   # world-space polyline
	var paths: Array = []
	var radii := PackedFloat32Array()
	if mode == "roots":
		for s in 3:
			var p := PackedVector3Array()
			var off := float(s - 1) * 0.35
			for i in n:
				var q: Vector3 = points[i]
				var t := (points[mini(i + 1, n - 1)] - points[maxi(i - 1, 0)])
				var side := Vector3(-t.z, 0, t.x).normalized() if t.length_squared() > 1e-8 else Vector3.RIGHT
				var arch := absf(sin(float(i) * 1.3 + s)) * 0.35 * radius / 0.1
				p.append(q + side * (off + 0.08 * sin(float(i) * 2.1 + s)) + Vector3(0, arch - 0.05, 0))
			paths.append(p)
			radii.append(radius * (1.0 - 0.25 * absf(off)))
	else:
		paths.append(points)
		radii.append(radius)
	_tris = FxMesh.build_tubes(_mesh, paths, radii, 6)


func set_state(grow: float, burn: float, frozen: float) -> void:
	_mat.set_shader_parameter("grow", clampf(grow, 0.0, 1.0))
	_mat.set_shader_parameter("burn", clampf(burn, 0.0, 1.0))
	_mat.set_shader_parameter("frozen", clampf(frozen, 0.0, 1.0))


func triangles() -> int:
	return _tris


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_mesh.clear_surfaces()
	_sig = Vector4.INF
	rotation = Vector3.ZERO


func advance(dt: float) -> void:
	_phase += dt
	_mat.set_shader_parameter("phase", _phase)
