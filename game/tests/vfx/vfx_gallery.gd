extends Node3D
## VFX gallery / screenshot harness.
##
##   tools/scripts/godot.sh --render --resolution 1600x900 res://tests/vfx/vfx_gallery.tscn
##   tools/scripts/godot.sh --render --resolution 1600x900 res://tests/vfx/vfx_gallery.tscn -- stones lava
##
## Builds a lit test arena, lays every effect out as a "station" and saves one PNG per station to
## the output directory (default: the scratch dir, override with `--out=/path`). Effects are driven
## with manual time so screenshots are deterministic regardless of the renderer's frame rate.

const DEFAULT_OUT: String = "/tmp/claude-0/-home-user-ClaudeToken/1c5b9df4-f793-5f68-9f97-4ed835b254e3/scratchpad/vfx"
const STATION_SPACING: float = 30.0

var _out_dir: String = DEFAULT_OUT
var _only: Array[String] = []
var _cam: Camera3D
var _stations: Array[Dictionary] = []
var _manual: Array[Node] = []  # effects advanced by the gallery
var _sun: DirectionalLight3D
var _wet_ground: bool = false
var _no_shadow: bool = false
var _ortho: bool = false
var _noenv: bool = false
var _nomax: bool = false
var _nolabel: bool = false


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out_dir = a.substr(6)
		elif a == "--noshadow":
			_no_shadow = true
		elif a == "--ortho":
			_ortho = true
		elif a == "--nomax":
			_nomax = true
		elif a == "--noenv":
			_noenv = true
		elif a == "--nolabel":
			_nolabel = true
		else:
			_only.append(a)
	DirAccess.make_dir_recursive_absolute(_out_dir)
	_build_environment()
	_build_ground()
	_cam = Camera3D.new()
	_cam.fov = 38.0
	_cam.near = 0.05
	_cam.far = 200.0
	add_child(_cam)
	_cam.current = true
	_register_stations()
	_run.call_deferred()


func _build_environment() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.20, 0.32, 0.52)
	sm.sky_horizon_color = Color(0.60, 0.58, 0.56)
	sm.ground_horizon_color = Color(0.55, 0.50, 0.45)
	sm.ground_bottom_color = Color(0.16, 0.14, 0.13)
	sm.sun_angle_max = 25.0
	sm.sun_curve = 0.12
	sky.sky_material = sm
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 1.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_exposure = 1.0
	env.tonemap_white = 6.0
	env.glow_enabled = true
	env.glow_intensity = 0.4
	env.glow_strength = 0.9
	env.glow_bloom = 0.0
	env.glow_hdr_threshold = 1.6
	env.glow_hdr_scale = 1.4
	var we := WorldEnvironment.new()
	we.environment = env
	if not _noenv:
		add_child(we)
	_sun = DirectionalLight3D.new()
	_sun.light_color = Color(1.0, 0.93, 0.82)
	_sun.light_energy = 2.8
	_sun.rotation_degrees = Vector3(-48.0, -32.0, 0.0)
	_sun.shadow_enabled = not _no_shadow
	if _ortho:
		_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	if not _nomax:
		_sun.directional_shadow_max_distance = 40.0
	add_child(_sun)


func _build_ground() -> void:
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(STATION_SPACING * 14.0, 60.0)
	mi.mesh = pm
	mi.position = Vector3(STATION_SPACING * 6.0, 0.0, 0.0)
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.50, 0.47, 0.43)
	m.roughness = 0.9
	mi.material_override = m
	mi.name = "Ground"
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)


func _track(fx: Node) -> void:
	fx.manual_time = true
	_manual.append(fx)


func _box(pos: Vector3, size: Vector3, color: Color, rough: float = 0.9) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.roughness = rough
	mi.material_override = m
	add_child(mi)
	return mi


func _label(text: String, pos: Vector3, size: float = 0.0028) -> void:
	if _nolabel:
		return
	var l := Label3D.new()
	l.text = text
	l.pixel_size = size
	l.font_size = 40
	l.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	l.no_depth_test = true
	l.modulate = Color(1, 1, 1, 0.9)
	l.outline_size = 6
	l.position = pos
	add_child(l)


func _register_stations() -> void:
	_stations.append({"name": "stones", "fn": _station_stones})
	_stations.append({"name": "lava", "fn": _station_lava})
	_stations.append({"name": "empty", "fn": _station_empty})
	_stations.append({"name": "emptybox", "fn": _station_emptybox})


func _run() -> void:
	await get_tree().process_frame
	for i in _stations.size():
		var st: Dictionary = _stations[i]
		if not _only.is_empty() and not _only.has(st.name):
			continue
		var origin := Vector3(STATION_SPACING * i, 0.0, 0.0)
		var cam_info: Dictionary = st.fn.call(origin)
		await _capture(String(st.name), cam_info)
	get_tree().quit()


func _capture(shot: String, cam_info: Dictionary) -> void:
	var shots: Array = cam_info.get("shots", [cam_info])
	for k in shots.size():
		var ci: Dictionary = shots[k]
		_cam.global_position = ci.pos
		_cam.look_at(ci.look, Vector3.UP)
		if ci.has("fov"):
			_cam.fov = ci.fov
		# let effects settle: advance manual time in small steps while rendering a few frames
		var frames: int = ci.get("frames", 3)
		var dt: float = ci.get("dt", 0.0)
		for _f in frames:
			for fx in _manual:
				if is_instance_valid(fx) and fx.has_method("advance"):
					fx.advance(dt)
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		var img: Image = get_viewport().get_texture().get_image()
		var suffix: String = "" if shots.size() == 1 else "_%d" % (k + 1)
		var path: String = "%s/shot_%s%s.png" % [_out_dir, shot, suffix]
		img.save_png(path)
		print("saved ", path)


# --------------------------------------------------------------------------------------------
func _station_stones(o: Vector3) -> Dictionary:
	var states: Array = [
		# heat, melt, crust, label
		[0.0, 0.0, 0.0, "stone h0 m0"],
		[0.5, 0.0, 0.0, "heat .5"],
		[1.0, 0.0, 0.0, "heat 1"],
		[1.0, 0.35, 0.0, "heat 1 melt .35"],
		[1.0, 0.65, 0.0, "melt .65"],
		[1.0, 1.0, 0.0, "melt 1"],
		[0.8, 1.0, 0.45, "melt 1 crust .45"],
		[0.5, 1.0, 0.8, "crust .8"],
		[0.0, 0.0, 1.0, "cooled rock (crust 1)"],
		[0.0, 1.0, 1.0, "cooled blob"],
		[0.0, 0.0, 0.0, "stone seed 9"],
		[1.0, 0.0, 0.0, "heat 1 seed 4"],
	]
	for i in states.size():
		var s: Array = states[i]
		var sv := StoneView.new()
		var col: int = i % 4
		var row: int = i / 4
		sv.position = o + Vector3(-2.25 + col * 1.5, 0.38, -1.5 + row * 1.5)
		sv.setup(3 + (i * 7 if i < 10 else (9 if i == 10 else 4)), 0.4)
		add_child(sv)
		sv.set_thermal(s[0], s[1])
		sv.set_crust(s[2])
		sv.rotation.y = i * 0.9
		_label(s[3], o + Vector3(-2.25 + col * 1.5, 1.05, -1.5 + row * 1.5))
	return {"shots": [
		{"pos": o + Vector3(0, 5.0, 5.2), "look": o + Vector3(0, 0.2, -0.2), "frames": 3},
		{"pos": o + Vector3(-1.1, 1.7, 1.2), "look": o + Vector3(-0.3, 0.45, -0.3), "frames": 2, "fov": 30.0},
	]}


func _lava_path(x0: float, x1: float, z: float, y: float, step: float = 0.45) -> Array:
	var pts := PackedVector3Array()
	var ws := PackedFloat32Array()
	var n: int = int(ceil((x1 - x0) / step)) + 1
	for i in n:
		var t: float = float(i) / float(n - 1)
		var x: float = lerpf(x0, x1, t)
		pts.append(Vector3(x, y, z + sin(t * 5.0 + z) * 0.12))
		ws.append(lerpf(0.75, 1.25, smoothstep(0.0, 0.7, t)))
	return [pts, ws]


func _station_lava(o: Vector3) -> Dictionary:
	var lanes: Array = [
		# melt, crust, flow, label
		[1.0, 0.0, 1.2, "crust 0 (flowing)"],
		[0.9, 0.5, 0.5, "crust .5"],
		[0.0, 1.0, 0.0, "crust 1 melt 0 (solid ridge)"],
	]
	for i in lanes.size():
		var l: Array = lanes[i]
		var z: float = -2.4 + i * 1.7
		var pp: Array = _lava_path(-3.0, 1.6, z, 0.0)
		var w := LavaWaveView.new()
		add_child(w)
		w.position = o
		w.set_path(pp[0], pp[1])
		w.set_state(l[0], l[1], l[2])
		_track(w)
		_label(l[3], o + Vector3(-3.3, 0.9, z))
	# ledge lane: lava runs across a 0.5 m ledge and steps down
	var zl: float = -2.4 + 3 * 1.7
	_box(o + Vector3(-1.8, 0.25, zl), Vector3(2.8, 0.5, 1.7), Color(0.30, 0.28, 0.26))
	var pts := PackedVector3Array()
	var ws := PackedFloat32Array()
	for i in 6:
		pts.append(Vector3(-3.0 + i * 0.45, 0.5, zl))
		ws.append(0.8 + i * 0.05)
	pts.append(Vector3(-0.35, 0.5, zl))
	ws.append(1.05)
	pts.append(Vector3(-0.25, 0.0, zl))
	ws.append(1.15)
	for i in 5:
		pts.append(Vector3(0.2 + i * 0.4, 0.0, zl))
		ws.append(1.2 + i * 0.03)
	var wl := LavaWaveView.new()
	add_child(wl)
	wl.position = o
	wl.set_path(pts, ws)
	wl.set_state(1.0, 0.15, 1.0)
	_track(wl)
	_label("ledge step 0.5 m", o + Vector3(-3.3, 1.2, zl))
	return {"shots": [
		{"pos": o + Vector3(-1.5, 3.4, 5.2), "look": o + Vector3(-0.8, 0.0, -0.4), "frames": 4, "dt": 0.25, "fov": 42.0},
		{"pos": o + Vector3(-1.5, 1.0, 1.2), "look": o + Vector3(-1.2, 0.2, 0.3), "frames": 1, "dt": 0.0, "fov": 36.0},
		{"pos": o + Vector3(1.8, 1.4, 4.4), "look": o + Vector3(-0.3, 0.2, 2.7), "frames": 1, "dt": 0.0, "fov": 34.0},
	]}


func _station_empty(o: Vector3) -> Dictionary:
	return {"pos": o + Vector3(0, 3.4, 5.2), "look": o + Vector3(0, 0.0, 0.0), "frames": 10}


func _station_emptybox(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.5, 0), Vector3(1, 1, 1), Color(0.5, 0.5, 0.5))
	return {"pos": o + Vector3(0, 3.4, 5.2), "look": o + Vector3(0, 0.0, 0.0), "frames": 10}
