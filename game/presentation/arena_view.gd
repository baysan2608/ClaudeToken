class_name ArenaView
extends Node3D
## Builds the visible combat laboratory from ArenaMap (single source of truth),
## plus light and environment. Uses the VFX library's arena shaders when present.

const SHADER_DIR := "res://presentation/shaders/"

var arena: ArenaMap
var sun: DirectionalLight3D
var env: WorldEnvironment
var _quality := 1


func build(a: ArenaMap, quality: int = 1) -> void:
	arena = a
	_quality = quality
	for c in get_children():
		c.queue_free()
	_build_lighting()
	_build_floor()
	_build_solids()
	_build_pool()
	_build_metal()
	_build_dressing()


func set_quality(q: int) -> void:
	_quality = q
	if sun:
		sun.shadow_enabled = q >= 1
		sun.directional_shadow_max_distance = 28.0 if q >= 2 else 20.0


func _mat(shader_name: String, fallback: Color, rough: float = 0.85, metal: float = 0.0) -> Material:
	var path := SHADER_DIR + shader_name + ".gdshader"
	if ResourceLoader.exists(path):
		var sm := ShaderMaterial.new()
		sm.shader = load(path)
		return sm
	var m := StandardMaterial3D.new()
	m.albedo_color = fallback
	m.roughness = rough
	m.metallic = metal
	return m


func _build_lighting() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.rotation_degrees = Vector3(-48, -35, 0)
	sun.light_color = Color(1.0, 0.94, 0.86)
	sun.light_energy = 1.25
	sun.shadow_enabled = _quality >= 1
	sun.shadow_bias = 0.04
	sun.shadow_normal_bias = 1.2
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	sun.directional_shadow_max_distance = 24.0
	add_child(sun)
	env = WorldEnvironment.new()
	var e := Environment.new()
	var sky := Sky.new()
	var ps := ProceduralSkyMaterial.new()
	ps.sky_top_color = Color(0.36, 0.47, 0.6)
	ps.sky_horizon_color = Color(0.72, 0.71, 0.68)
	ps.ground_bottom_color = Color(0.2, 0.19, 0.18)
	ps.ground_horizon_color = Color(0.62, 0.6, 0.56)
	ps.sun_angle_max = 20.0
	sky.sky_material = ps
	e.background_mode = Environment.BG_SKY
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.ambient_light_energy = 0.75
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	e.tonemap_white = 6.0
	e.glow_enabled = _quality >= 1
	e.glow_intensity = 0.35
	e.glow_bloom = 0.04
	e.glow_hdr_threshold = 1.2
	e.fog_enabled = true
	e.fog_light_color = Color(0.68, 0.67, 0.64)
	e.fog_density = 0.006
	e.fog_sky_affect = 0.3
	env.environment = e
	add_child(env)


func _box(mn: Vector3, mx: Vector3, mat: Material, nm: String) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = mx - mn
	mi.mesh = bm
	mi.position = (mn + mx) * 0.5
	mi.material_override = mat
	mi.name = nm
	add_child(mi)
	return mi


func _build_floor() -> void:
	var h := arena.half_size
	var mat := _mat("arena_ground", Color(0.56, 0.54, 0.5))
	# Floor tiles around the pool (the pool is a sunken basin).
	var p0 := arena.pool_min
	var p1 := arena.pool_max
	var rects := [
		Rect2(-h, -h, 2 * h, p0.y + h),                 # north of pool
		Rect2(-h, p1.y, 2 * h, h - p1.y),               # south of pool
		Rect2(-h, p0.y, p0.x + h, p1.y - p0.y),         # west strip
		Rect2(p1.x, p0.y, h - p1.x, p1.y - p0.y),       # east strip
	]
	for r in rects:
		if r.size.x <= 0.01 or r.size.y <= 0.01:
			continue
		_box(Vector3(r.position.x, -0.5, r.position.y), Vector3(r.end.x, 0.0, r.end.y), mat, "floor")


func _build_solids() -> void:
	var ledge := _mat("ledge_stone", Color(0.6, 0.57, 0.52))
	var wall := _mat("ledge_stone", Color(0.5, 0.48, 0.45))
	for s in arena.solids:
		var m: Material = wall if s.kind == "wall" or s.kind == "pillar" else ledge
		_box(s.min, s.max, m, s.name)


func _build_pool() -> void:
	var p0 := arena.pool_min
	var p1 := arena.pool_max
	var basin := _mat("ledge_stone", Color(0.35, 0.36, 0.36))
	_box(Vector3(p0.x, arena.pool_floor - 0.3, p0.y), Vector3(p1.x, arena.pool_floor, p1.y), basin, "pool_floor")
	var water := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(p1.x - p0.x, p1.y - p0.y)
	water.mesh = pm
	water.position = Vector3((p0.x + p1.x) * 0.5, arena.pool_level, (p0.y + p1.y) * 0.5)
	var wm := _mat("pool_water", Color(0.16, 0.3, 0.34), 0.08)
	if wm is StandardMaterial3D:
		(wm as StandardMaterial3D).transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		(wm as StandardMaterial3D).albedo_color = Color(0.14, 0.32, 0.36, 0.78)
		(wm as StandardMaterial3D).metallic_specular = 0.8
	water.material_override = wm
	water.name = "pool_water"
	water.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(water)
	# Low coping stones around the pool edge (visual only, flush with the floor).
	var coping := _mat("ledge_stone", Color(0.66, 0.63, 0.58))
	var t := 0.25
	_box(Vector3(p0.x - t, -0.02, p0.y - t), Vector3(p1.x + t, 0.03, p0.y), coping, "coping_n")
	_box(Vector3(p0.x - t, -0.02, p1.y), Vector3(p1.x + t, 0.03, p1.y + t), coping, "coping_s")
	_box(Vector3(p0.x - t, -0.02, p0.y), Vector3(p0.x, 0.03, p1.y), coping, "coping_w")
	_box(Vector3(p1.x, -0.02, p0.y), Vector3(p1.x + t, 0.03, p1.y), coping, "coping_e")


func _build_metal() -> void:
	var m0 := arena.metal_min
	var m1 := arena.metal_max
	var mat := _mat("metal_plate", Color(0.32, 0.33, 0.35), 0.4, 0.85)
	_box(Vector3(m0.x, -0.02, m0.y), Vector3(m1.x, arena.metal_top, m1.y), mat, "metal_plate")


func _build_dressing() -> void:
	# A few restrained markers: start circles for both fighters.
	var ring_mat := StandardMaterial3D.new()
	ring_mat.albedo_color = Color(0.82, 0.78, 0.7)
	ring_mat.roughness = 0.9
	for p in [arena.player_spawn, arena.opponent_spawn]:
		var mi := MeshInstance3D.new()
		var tm := TorusMesh.new()
		tm.inner_radius = 0.95
		tm.outer_radius = 1.02
		tm.rings = 48
		mi.mesh = tm
		mi.scale = Vector3(1, 0.02, 1)
		mi.position = p + Vector3(0, 0.005, 0)
		mi.material_override = ring_mat
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(mi)
