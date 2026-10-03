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
	var gm: ShaderMaterial = VfxMaterials.make("arena_ground")
	gm.set_shader_parameter("wear", 0.4)
	mi.material_override = gm
	mi.name = "Ground"
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)


func _matte(color: Color, rough: float = 0.9) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = load("res://tests/vfx/gallery_matte.gdshader") as Shader
	var lin: Color = color.srgb_to_linear()
	m.set_shader_parameter("albedo", Vector3(lin.r, lin.g, lin.b))
	m.set_shader_parameter("rough", rough)
	return m


func _track(fx: Node) -> void:
	fx.manual_time = true
	_manual.append(fx)


func _box(pos: Vector3, size: Vector3, color: Color, rough: float = 0.9) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	mi.material_override = _matte(color, rough)
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
	_stations.append({"name": "water", "fn": _station_water})
	_stations.append({"name": "particles", "fn": _station_particles})
	_stations.append({"name": "fire", "fn": _station_fire})
	_stations.append({"name": "lightning", "fn": _station_lightning})
	_stations.append({"name": "air", "fn": _station_air})
	_stations.append({"name": "wall", "fn": _station_wall})
	_stations.append({"name": "arena", "fn": _station_arena})
	_stations.append({"name": "hero", "fn": _station_hero})
	_stations.append({"name": "empty", "fn": _station_empty})
	_stations.append({"name": "emptybox", "fn": _station_emptybox})


func _run() -> void:
	# warm-up: let the renderer build pipelines / shadow maps before the first capture
	for _i in 10:
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
		w.pattern_seed = float(i) * 1.7 + 0.3
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
	wl.pattern_seed = 7.1
	wl.set_path(pts, ws)
	wl.set_state(1.0, 0.15, 1.0)
	_track(wl)
	_label("ledge step 0.5 m", o + Vector3(-3.3, 1.2, zl))
	return {"shots": [
		{"pos": o + Vector3(-2.2, 4.6, 6.4), "look": o + Vector3(-0.8, 0.0, 0.2), "frames": 4, "dt": 0.25, "fov": 42.0},
		{"pos": o + Vector3(-1.5, 1.0, 1.2), "look": o + Vector3(-1.2, 0.2, 0.3), "frames": 1, "dt": 0.0, "fov": 36.0},
		{"pos": o + Vector3(1.8, 1.4, 4.4), "look": o + Vector3(-0.3, 0.2, 2.7), "frames": 1, "dt": 0.0, "fov": 34.0},
	]}


func _station_empty(o: Vector3) -> Dictionary:
	return {"pos": o + Vector3(0, 3.4, 5.2), "look": o + Vector3(0, 0.0, 0.0), "frames": 10}


func _station_emptybox(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.5, 0), Vector3(1, 1, 1), Color(0.5, 0.5, 0.5))
	return {"pos": o + Vector3(0, 3.4, 5.2), "look": o + Vector3(0, 0.0, 0.0), "frames": 10}


func _whip_points(o: Vector3, z: float, phase: float) -> Array:
	var pts := PackedVector3Array()
	var rad := PackedFloat32Array()
	var n: int = 18
	for i in n:
		var t: float = float(i) / float(n - 1)
		var x: float = lerpf(-1.6, 1.5, t)
		var y: float = 0.55 + 0.45 * sin(t * 4.2 + phase) * (0.3 + 0.7 * t)
		var zz: float = z + 0.35 * sin(t * 6.0 + phase * 1.3)
		pts.append(o + Vector3(x, y, zz))
		rad.append(lerpf(0.11, 0.07, t) * (1.0 + 0.15 * sin(t * 9.0)))
	return [pts, rad]


func _station_water(o: Vector3) -> Dictionary:
	var states: Array = [0.0, 0.5, 1.0]
	for i in 3:
		var pr: Array = _whip_points(o, -2.0 + i * 1.7, 0.4 + i * 0.5)
		var wr := WaterRibbonView.new()
		add_child(wr)
		wr.set_points(pr[0], pr[1])
		wr.set_state(states[i])
		_track(wr)
		_label("whip frozen %.1f" % states[i], o + Vector3(-1.9, 1.7, -2.0 + i * 1.7))
	for i in 3:
		var wb := WaterBlobView.new()
		wb.setup(0.38)
		add_child(wb)
		wb.position = o + Vector3(-1.2 + i * 1.3, 0.55, 2.0)
		wb.set_state(states[i])
		_track(wb)
		_label("orb frozen %.1f" % states[i], wb.position + Vector3(0, 0.7, 0))
	# a backdrop so transparency has something to read against
	_box(o + Vector3(0, 0.02, 0.0), Vector3(8, 0.04, 6), Color(0.30, 0.29, 0.28))
	_box(o + Vector3(0.0, 0.9, -3.4), Vector3(8, 1.8, 0.2), Color(0.34, 0.24, 0.18))
	return {"shots": [
		{"pos": o + Vector3(0.5, 3.2, 5.8), "look": o + Vector3(0, 0.5, 0.2), "frames": 3, "dt": 0.3, "fov": 40.0},
		{"pos": o + Vector3(-0.6, 1.4, 3.6), "look": o + Vector3(0.1, 0.55, 1.4), "frames": 1, "fov": 32.0},
	]}


func _station_particles(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(10, 0.04, 6), Color(0.62, 0.60, 0.56))
	var xs: Array[float] = [-3.6, -1.8, 0.0, 1.8, 3.6]
	var names: Array[String] = ["dust", "steam", "splash", "ember", "dust (soft)"]
	for i in 5:
		var fx: VfxEffect
		var pos := o + Vector3(xs[i], 0.0, 0.0)
		match i:
			0, 4:
				fx = DustPuffFX.new()
			1:
				fx = SteamFX.new()
			2:
				fx = SplashFX.new()
			3:
				fx = EmberFX.new()
		add_child(fx)
		_track(fx)
		match i:
			0:
				fx.play(pos, Vector3.UP, 1.2)
			1:
				fx.play(pos, 1.0)
			2:
				fx.play(pos, Vector3.UP, 1.0)
			3:
				fx.play(pos, Vector3.UP, 1.0)
			4:
				fx.play(pos, Vector3.UP, 0.4)
		_label(names[i], pos + Vector3(0, 1.9, 0))
	return {"shots": [
		{"pos": o + Vector3(0, 1.4, 6.4), "look": o + Vector3(0, 0.8, 0), "frames": 1, "dt": 0.25, "fov": 36.0},
		{"pos": o + Vector3(0, 1.4, 6.4), "look": o + Vector3(0, 0.8, 0), "frames": 1, "dt": 0.25, "fov": 36.0},
	]}


func _station_fire(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(12, 0.04, 7), Color(0.30, 0.29, 0.28))
	_box(o + Vector3(0, 1.4, -3.2), Vector3(12, 2.8, 0.2), Color(0.36, 0.33, 0.30))
	# bursts at several ages
	var ages: Array[float] = [0.08, 0.2, 0.34, 0.46]
	for i in ages.size():
		var fb := FireBurstFX.new()
		add_child(fb)
		_track(fb)
		fb.play(o + Vector3(-5.0 + i * 2.6, 1.2, 0.0), Vector3(1, 0.0, 0.0), 2.2, 1.0)
		fb.advance(ages[i])
		fb.manual_time = true
		_label("burst t=%.2fs" % ages[i], o + Vector3(-4.0 + i * 2.6, 2.4, 0.0))
	# held charge at 0.25 / 0.6 / 1.0
	var cs: Array[float] = [0.25, 0.6, 1.0]
	for i in cs.size():
		var fc := FireChargeFX.new()
		add_child(fc)
		fc.position = o + Vector3(-3.0 + i * 1.4, 0.7, 2.2)
		_track(fc)
		fc.set_charge(cs[i])
		_label("charge %.2f" % cs[i], fc.position + Vector3(0, 1.0, 0))
	return {"shots": [
		{"pos": o + Vector3(0, 1.9, 6.8), "look": o + Vector3(-0.5, 1.0, 0.5), "frames": 2, "dt": 0.0, "fov": 44.0},
		{"pos": o + Vector3(-4.6, 1.5, 4.2), "look": o + Vector3(-3.4, 1.1, 0.0), "frames": 1, "dt": 0.0, "fov": 34.0},
	]}


func _station_lightning(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(14, 0.04, 8), Color(0.34, 0.33, 0.32))
	_box(o + Vector3(0, 1.6, -3.4), Vector3(14, 3.2, 0.2), Color(0.20, 0.20, 0.24))
	# bolts: straight-ish conduction path through 3 "targets", and a longer bent path
	var path_a := PackedVector3Array([o + Vector3(-5.5, 1.6, 0.0), o + Vector3(-3.4, 1.0, 0.4), o + Vector3(-1.8, 1.4, -0.4)])
	var path_b := PackedVector3Array([o + Vector3(0.6, 2.4, 0.0), o + Vector3(2.0, 1.2, 0.6), o + Vector3(3.4, 0.3, -0.2), o + Vector3(5.0, 1.0, 0.3)])
	var arcs: Array = []
	for i in 2:
		var la := LightningArcFX.new()
		add_child(la)
		_track(la)
		la.strike(path_a if i == 0 else path_b, 11 + i * 5)
		la.advance(0.05)
		arcs.append(la)
	# conduction targets (stand-ins)
	for p in [path_a[1], path_a[2], path_b[1], path_b[2], path_b[3]]:
		var m := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 0.18
		sm.height = 0.36
		m.mesh = sm
		m.position = p
		add_child(m)
	# charge / aim line
	var ca := ChargeAimFX.new()
	add_child(ca)
	_track(ca)
	ca.set_aim(o + Vector3(-5.0, 0.9, 2.2), o + Vector3(-0.5, 0.9, 2.2), 0.3)
	var cb := ChargeAimFX.new()
	add_child(cb)
	_track(cb)
	cb.set_aim(o + Vector3(0.5, 0.9, 2.2), o + Vector3(5.0, 0.9, 2.2), 0.95)
	_label("bolt A (3 nodes)", o + Vector3(-3.6, 2.6, 0.0))
	_label("bolt B (4 nodes)", o + Vector3(2.8, 3.1, 0.0))
	_label("aim t=0.3", o + Vector3(-2.8, 1.4, 2.2))
	_label("aim t=0.95", o + Vector3(2.8, 1.4, 2.2))
	return {"shots": [
		{"pos": o + Vector3(0, 1.8, 7.6), "look": o + Vector3(0, 1.1, 0.0), "frames": 2, "dt": 0.0, "fov": 40.0},
		{"pos": o + Vector3(-3.6, 1.4, 3.6), "look": o + Vector3(-3.4, 1.1, 0.4), "frames": 1, "dt": 0.0, "fov": 30.0},
	]}


func _station_air(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(14, 0.04, 8), Color(0.55, 0.53, 0.50))
	# backdrop with a checker-ish pattern so distortion is visible
	var back := _box(o + Vector3(0, 1.6, -3.4), Vector3(14, 3.2, 0.2), Color(0.62, 0.50, 0.38))
	for i in 14:
		_box(o + Vector3(-6.5 + i, 0.9, -3.28), Vector3(0.18, 1.8, 0.05), Color(0.25, 0.25, 0.3) if i % 2 == 0 else Color(0.85, 0.8, 0.7))
	# stand-in "enemies"
	for i in 3:
		var cm := MeshInstance3D.new()
		var cap := CapsuleMesh.new()
		cap.radius = 0.28
		cap.height = 1.7
		cm.mesh = cap
		cm.position = o + Vector3(-1.5 + i * 2.6, 0.85, -0.6)
		cm.material_override = _matte(Color(0.7, 0.2, 0.15) if i == 1 else Color(0.2, 0.35, 0.6))
		add_child(cm)
	var ages: Array[float] = [0.18, 0.38]
	for i in ages.size():
		var ap := AirPushFX.new()
		add_child(ap)
		_track(ap)
		ap.play(o + Vector3(-5.2, 1.0, 0.8 - i * 1.6), Vector3(1, 0.0, -0.1), 1.5, 8.0)
		ap.advance(ages[i])
	# glide trail: a figure-8-ish path
	var gt := GlideTrailFX.new()
	add_child(gt)
	_track(gt)
	gt.begin(null)
	for i in 20:
		var t: float = float(i) / 19.0
		gt.push(o + Vector3(-2.0 + t * 4.0, 2.4 + 0.5 * sin(t * 6.0), 1.6 + 0.4 * cos(t * 5.0)))
	gt.advance(0.0)
	_label("air push t=0.18 / 0.38", o + Vector3(-3.0, 2.4, 0.8))
	_label("glide trail", o + Vector3(0.0, 3.4, 1.6))
	return {"shots": [
		{"pos": o + Vector3(-3.0, 1.9, 6.6), "look": o + Vector3(-0.5, 1.2, -0.4), "frames": 2, "dt": 0.0, "fov": 46.0},
	]}


func _station_wall(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, -0.02, 0.0), Vector3(14, 0.04, 8), Color(0.50, 0.47, 0.43))
	var rises: Array[float] = [0.0, 0.25, 0.55, 1.0]
	var dmg: Array[float] = [0.0, 0.0, 0.0, 0.0]
	for i in 4:
		var w := EarthWallView.new()
		add_child(w)
		w.position = o + Vector3(-5.2 + i * 3.0, 0.0, 0.0)
		w.setup(5 + i)
		w.manual_time = true
		_manual.append(w)
		w.set_rise(rises[i])
		_label("rise %.2f" % rises[i], w.position + Vector3(0, 1.7, 0))
	for i in 3:
		var w2 := EarthWallView.new()
		add_child(w2)
		w2.position = o + Vector3(-3.7 + i * 3.0, 0.0, 2.8)
		w2.setup(11 + i)
		w2.dust = false
		w2.set_rise(1.0)
		w2.set_damage([0.0, 0.5, 1.0][i])
		_label("damage %.1f" % [0.0, 0.5, 1.0][i], w2.position + Vector3(0, 1.5, 0))
	# scorch / wet decals
	if not _only.has("nodecal"):
		var sd := ScorchDecal.new()
		add_child(sd)
		_track(sd)
		sd.place(o + Vector3(4.4, 0.0, 2.8), 0.9, "scorch", 9.0)
		sd.advance(0.5)
		var wd := ScorchDecal.new()
		add_child(wd)
		_track(wd)
		wd.place(o + Vector3(5.6, 0.0, 1.2), 0.8, "wet", 9.0)
		wd.advance(0.5)
	_label("scorch / wet", o + Vector3(5.0, 1.2, 2.0))
	return {"shots": [
		{"pos": o + Vector3(0, 3.6, 7.2), "look": o + Vector3(0, 0.5, 0.8), "frames": 2, "dt": 0.3, "fov": 44.0},
		{"pos": o + Vector3(-2.8, 1.4, 3.2), "look": o + Vector3(-1.8, 0.5, 0.0), "frames": 1, "fov": 34.0},
	]}


func _station_arena(o: Vector3) -> Dictionary:
	# courtyard: flagstones (dry / wet), metal plate deck, ledge blocks, pool
	var g_dry := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(8, 8)
	g_dry.mesh = pm
	g_dry.position = o + Vector3(-4, 0.01, 0)
	g_dry.material_override = VfxMaterials.make("arena_ground")
	add_child(g_dry)
	var g_wet := MeshInstance3D.new()
	g_wet.mesh = pm
	g_wet.position = o + Vector3(4, 0.01, 0)
	var wm := VfxMaterials.make("arena_ground")
	g_wet.material_override = wm
	add_child(g_wet)
	g_wet.set_instance_shader_parameter("wetness", 0.8)
	# metal deck
	var deck := MeshInstance3D.new()
	var dm := PlaneMesh.new()
	dm.size = Vector2(4, 3)
	deck.mesh = dm
	deck.position = o + Vector3(-1.2, 0.01, 3.4)
	deck.material_override = VfxMaterials.make("metal_plate")
	add_child(deck)
	# ledge: cut stone block 0.5 m high with vertical + top faces
	var ledge := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(4.0, 0.5, 1.6)
	ledge.mesh = bm
	ledge.position = o + Vector3(-4.0, 0.25, -2.4)
	ledge.material_override = VfxMaterials.make("ledge_stone")
	add_child(ledge)
	var ledge2 := MeshInstance3D.new()
	var bm2 := BoxMesh.new()
	bm2.size = Vector3(1.6, 1.1, 1.6)
	ledge2.mesh = bm2
	ledge2.position = o + Vector3(-1.2, 0.55, -2.4)
	ledge2.material_override = VfxMaterials.make("ledge_stone")
	add_child(ledge2)
	# pool: recessed look via a dark rim box + water plane at ground level
	var pool_rim := MeshInstance3D.new()
	var prm := PlaneMesh.new()
	prm.size = Vector2(6.4, 4.4)
	pool_rim.mesh = prm
	pool_rim.position = o + Vector3(4.0, 0.012, -1.0)
	var wmat := VfxMaterials.make("pool_water")
	wmat.set_shader_parameter("half_extent", Vector2(3.0, 2.0))
	wmat.set_shader_parameter("corner_radius", 0.7)
	pool_rim.material_override = wmat
	add_child(pool_rim)
	_label("flagstones dry", o + Vector3(-4, 1.0, 1.0))
	_label("flagstones wet 0.8", o + Vector3(4, 1.0, 3.0))
	_label("metal plate", o + Vector3(-1.2, 0.8, 3.4))
	_label("ledge stone", o + Vector3(-4.0, 1.2, -2.4))
	_label("pool water", o + Vector3(4.0, 0.8, -1.0))
	return {"shots": [
		{"pos": o + Vector3(0, 7.2, 8.0), "look": o + Vector3(0, 0.0, 0.0), "frames": 2, "dt": 0.2, "fov": 46.0},
		{"pos": o + Vector3(-5.5, 1.5, 4.5), "look": o + Vector3(-3.0, 0.2, 0.5), "frames": 1, "fov": 40.0},
		{"pos": o + Vector3(5.0, 1.6, 4.0), "look": o + Vector3(4.2, 0.0, -1.0), "frames": 1, "fov": 40.0},
	]}


func _station_empty(o: Vector3) -> Dictionary:
	return {"pos": o + Vector3(0, 3.4, 5.2), "look": o + Vector3(0, 0.0, 0.0), "frames": 10}


func _station_emptybox(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.5, 0), Vector3(1, 1, 1), Color(0.5, 0.5, 0.5))
	return {"pos": o + Vector3(0, 3.4, 5.2), "look": o + Vector3(0, 0.0, 0.0), "frames": 10}


func _whip_points(o: Vector3, z: float, phase: float) -> Array:
	var pts := PackedVector3Array()
	var rad := PackedFloat32Array()
	var n: int = 18
	for i in n:
		var t: float = float(i) / float(n - 1)
		var x: float = lerpf(-1.6, 1.5, t)
		var y: float = 0.55 + 0.45 * sin(t * 4.2 + phase) * (0.3 + 0.7 * t)
		var zz: float = z + 0.35 * sin(t * 6.0 + phase * 1.3)
		pts.append(o + Vector3(x, y, zz))
		rad.append(lerpf(0.11, 0.07, t) * (1.0 + 0.15 * sin(t * 9.0)))
	return [pts, rad]


func _station_water(o: Vector3) -> Dictionary:
	var states: Array = [0.0, 0.5, 1.0]
	for i in 3:
		var pr: Array = _whip_points(o, -2.0 + i * 1.7, 0.4 + i * 0.5)
		var wr := WaterRibbonView.new()
		add_child(wr)
		wr.set_points(pr[0], pr[1])
		wr.set_state(states[i])
		_track(wr)
		_label("whip frozen %.1f" % states[i], o + Vector3(-1.9, 1.7, -2.0 + i * 1.7))
	for i in 3:
		var wb := WaterBlobView.new()
		wb.setup(0.38)
		add_child(wb)
		wb.position = o + Vector3(-1.2 + i * 1.3, 0.55, 2.0)
		wb.set_state(states[i])
		_track(wb)
		_label("orb frozen %.1f" % states[i], wb.position + Vector3(0, 0.7, 0))
	# a backdrop so transparency has something to read against
	_box(o + Vector3(0, 0.02, 0.0), Vector3(8, 0.04, 6), Color(0.30, 0.29, 0.28))
	_box(o + Vector3(0.0, 0.9, -3.4), Vector3(8, 1.8, 0.2), Color(0.34, 0.24, 0.18))
	return {"shots": [
		{"pos": o + Vector3(0.5, 3.2, 5.8), "look": o + Vector3(0, 0.5, 0.2), "frames": 3, "dt": 0.3, "fov": 40.0},
		{"pos": o + Vector3(-0.6, 1.4, 3.6), "look": o + Vector3(0.1, 0.55, 1.4), "frames": 1, "fov": 32.0},
	]}


func _station_particles(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(10, 0.04, 6), Color(0.62, 0.60, 0.56))
	var xs: Array[float] = [-3.6, -1.8, 0.0, 1.8, 3.6]
	var names: Array[String] = ["dust", "steam", "splash", "ember", "dust (soft)"]
	for i in 5:
		var fx: VfxEffect
		var pos := o + Vector3(xs[i], 0.0, 0.0)
		match i:
			0, 4:
				fx = DustPuffFX.new()
			1:
				fx = SteamFX.new()
			2:
				fx = SplashFX.new()
			3:
				fx = EmberFX.new()
		add_child(fx)
		_track(fx)
		match i:
			0:
				fx.play(pos, Vector3.UP, 1.2)
			1:
				fx.play(pos, 1.0)
			2:
				fx.play(pos, Vector3.UP, 1.0)
			3:
				fx.play(pos, Vector3.UP, 1.0)
			4:
				fx.play(pos, Vector3.UP, 0.4)
		_label(names[i], pos + Vector3(0, 1.9, 0))
	return {"shots": [
		{"pos": o + Vector3(0, 1.4, 6.4), "look": o + Vector3(0, 0.8, 0), "frames": 1, "dt": 0.25, "fov": 36.0},
		{"pos": o + Vector3(0, 1.4, 6.4), "look": o + Vector3(0, 0.8, 0), "frames": 1, "dt": 0.25, "fov": 36.0},
	]}


func _station_fire(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(12, 0.04, 7), Color(0.30, 0.29, 0.28))
	_box(o + Vector3(0, 1.4, -3.2), Vector3(12, 2.8, 0.2), Color(0.36, 0.33, 0.30))
	# bursts at several ages
	var ages: Array[float] = [0.08, 0.2, 0.34, 0.46]
	for i in ages.size():
		var fb := FireBurstFX.new()
		add_child(fb)
		_track(fb)
		fb.play(o + Vector3(-5.0 + i * 2.6, 1.2, 0.0), Vector3(1, 0.0, 0.0), 2.2, 1.0)
		fb.advance(ages[i])
		fb.manual_time = true
		_label("burst t=%.2fs" % ages[i], o + Vector3(-4.0 + i * 2.6, 2.4, 0.0))
	# held charge at 0.25 / 0.6 / 1.0
	var cs: Array[float] = [0.25, 0.6, 1.0]
	for i in cs.size():
		var fc := FireChargeFX.new()
		add_child(fc)
		fc.position = o + Vector3(-3.0 + i * 1.4, 0.7, 2.2)
		_track(fc)
		fc.set_charge(cs[i])
		_label("charge %.2f" % cs[i], fc.position + Vector3(0, 1.0, 0))
	return {"shots": [
		{"pos": o + Vector3(0, 1.9, 6.8), "look": o + Vector3(-0.5, 1.0, 0.5), "frames": 2, "dt": 0.0, "fov": 44.0},
		{"pos": o + Vector3(-4.6, 1.5, 4.2), "look": o + Vector3(-3.4, 1.1, 0.0), "frames": 1, "dt": 0.0, "fov": 34.0},
	]}


func _station_lightning(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(14, 0.04, 8), Color(0.34, 0.33, 0.32))
	_box(o + Vector3(0, 1.6, -3.4), Vector3(14, 3.2, 0.2), Color(0.20, 0.20, 0.24))
	# bolts: straight-ish conduction path through 3 "targets", and a longer bent path
	var path_a := PackedVector3Array([o + Vector3(-5.5, 1.6, 0.0), o + Vector3(-3.4, 1.0, 0.4), o + Vector3(-1.8, 1.4, -0.4)])
	var path_b := PackedVector3Array([o + Vector3(0.6, 2.4, 0.0), o + Vector3(2.0, 1.2, 0.6), o + Vector3(3.4, 0.3, -0.2), o + Vector3(5.0, 1.0, 0.3)])
	var arcs: Array = []
	for i in 2:
		var la := LightningArcFX.new()
		add_child(la)
		_track(la)
		la.strike(path_a if i == 0 else path_b, 11 + i * 5)
		la.advance(0.05)
		arcs.append(la)
	# conduction targets (stand-ins)
	for p in [path_a[1], path_a[2], path_b[1], path_b[2], path_b[3]]:
		var m := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 0.18
		sm.height = 0.36
		m.mesh = sm
		m.position = p
		add_child(m)
	# charge / aim line
	var ca := ChargeAimFX.new()
	add_child(ca)
	_track(ca)
	ca.set_aim(o + Vector3(-5.0, 0.9, 2.2), o + Vector3(-0.5, 0.9, 2.2), 0.3)
	var cb := ChargeAimFX.new()
	add_child(cb)
	_track(cb)
	cb.set_aim(o + Vector3(0.5, 0.9, 2.2), o + Vector3(5.0, 0.9, 2.2), 0.95)
	_label("bolt A (3 nodes)", o + Vector3(-3.6, 2.6, 0.0))
	_label("bolt B (4 nodes)", o + Vector3(2.8, 3.1, 0.0))
	_label("aim t=0.3", o + Vector3(-2.8, 1.4, 2.2))
	_label("aim t=0.95", o + Vector3(2.8, 1.4, 2.2))
	return {"shots": [
		{"pos": o + Vector3(0, 1.8, 7.6), "look": o + Vector3(0, 1.1, 0.0), "frames": 2, "dt": 0.0, "fov": 40.0},
		{"pos": o + Vector3(-3.6, 1.4, 3.6), "look": o + Vector3(-3.4, 1.1, 0.4), "frames": 1, "dt": 0.0, "fov": 30.0},
	]}


func _station_air(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, 0.02, 0.0), Vector3(14, 0.04, 8), Color(0.55, 0.53, 0.50))
	# backdrop with a checker-ish pattern so distortion is visible
	var back := _box(o + Vector3(0, 1.6, -3.4), Vector3(14, 3.2, 0.2), Color(0.62, 0.50, 0.38))
	for i in 14:
		_box(o + Vector3(-6.5 + i, 0.9, -3.28), Vector3(0.18, 1.8, 0.05), Color(0.25, 0.25, 0.3) if i % 2 == 0 else Color(0.85, 0.8, 0.7))
	# stand-in "enemies"
	for i in 3:
		var cm := MeshInstance3D.new()
		var cap := CapsuleMesh.new()
		cap.radius = 0.28
		cap.height = 1.7
		cm.mesh = cap
		cm.position = o + Vector3(-1.5 + i * 2.6, 0.85, -0.6)
		cm.material_override = _matte(Color(0.7, 0.2, 0.15) if i == 1 else Color(0.2, 0.35, 0.6))
		add_child(cm)
	var ages: Array[float] = [0.18, 0.38]
	for i in ages.size():
		var ap := AirPushFX.new()
		add_child(ap)
		_track(ap)
		ap.play(o + Vector3(-5.2, 1.0, 0.8 - i * 1.6), Vector3(1, 0.0, -0.1), 1.5, 8.0)
		ap.advance(ages[i])
	# glide trail: a figure-8-ish path
	var gt := GlideTrailFX.new()
	add_child(gt)
	_track(gt)
	gt.begin(null)
	for i in 20:
		var t: float = float(i) / 19.0
		gt.push(o + Vector3(-2.0 + t * 4.0, 2.4 + 0.5 * sin(t * 6.0), 1.6 + 0.4 * cos(t * 5.0)))
	gt.advance(0.0)
	_label("air push t=0.18 / 0.38", o + Vector3(-3.0, 2.4, 0.8))
	_label("glide trail", o + Vector3(0.0, 3.4, 1.6))
	return {"shots": [
		{"pos": o + Vector3(-3.0, 1.9, 6.6), "look": o + Vector3(-0.5, 1.2, -0.4), "frames": 2, "dt": 0.0, "fov": 46.0},
	]}


func _station_wall(o: Vector3) -> Dictionary:
	_box(o + Vector3(0, -0.02, 0.0), Vector3(14, 0.04, 8), Color(0.50, 0.47, 0.43))
	var rises: Array[float] = [0.0, 0.25, 0.55, 1.0]
	var dmg: Array[float] = [0.0, 0.0, 0.0, 0.0]
	for i in 4:
		var w := EarthWallView.new()
		add_child(w)
		w.position = o + Vector3(-5.2 + i * 3.0, 0.0, 0.0)
		w.setup(5 + i)
		w.manual_time = true
		_manual.append(w)
		w.set_rise(rises[i])
		_label("rise %.2f" % rises[i], w.position + Vector3(0, 1.7, 0))
	for i in 3:
		var w2 := EarthWallView.new()
		add_child(w2)
		w2.position = o + Vector3(-3.7 + i * 3.0, 0.0, 2.8)
		w2.setup(11 + i)
		w2.dust = false
		w2.set_rise(1.0)
		w2.set_damage([0.0, 0.5, 1.0][i])
		_label("damage %.1f" % [0.0, 0.5, 1.0][i], w2.position + Vector3(0, 1.5, 0))
	# scorch / wet decals
	if not _only.has("nodecal"):
		var sd := ScorchDecal.new()
		add_child(sd)
		_track(sd)
		sd.place(o + Vector3(4.4, 0.0, 2.8), 0.9, "scorch", 9.0)
		sd.advance(0.5)
		var wd := ScorchDecal.new()
		add_child(wd)
		_track(wd)
		wd.place(o + Vector3(5.6, 0.0, 1.2), 0.8, "wet", 9.0)
		wd.advance(0.5)
	_label("scorch / wet", o + Vector3(5.0, 1.2, 2.0))
	return {"shots": [
		{"pos": o + Vector3(0, 3.6, 7.2), "look": o + Vector3(0, 0.5, 0.8), "frames": 2, "dt": 0.3, "fov": 44.0},
		{"pos": o + Vector3(-2.8, 1.4, 3.2), "look": o + Vector3(-1.8, 0.5, 0.0), "frames": 1, "fov": 34.0},
	]}


func _station_arena(o: Vector3) -> Dictionary:
	# courtyard: flagstones (dry / wet), metal plate deck, ledge blocks, pool
	var g_dry := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(8, 8)
	g_dry.mesh = pm
	g_dry.position = o + Vector3(-4, 0.01, 0)
	g_dry.material_override = VfxMaterials.make("arena_ground")
	add_child(g_dry)
	var g_wet := MeshInstance3D.new()
	g_wet.mesh = pm
	g_wet.position = o + Vector3(4, 0.01, 0)
	var wm := VfxMaterials.make("arena_ground")
	g_wet.material_override = wm
	add_child(g_wet)
	g_wet.set_instance_shader_parameter("wetness", 0.8)
	# metal deck
	var deck := MeshInstance3D.new()
	var dm := PlaneMesh.new()
	dm.size = Vector2(4, 3)
	deck.mesh = dm
	deck.position = o + Vector3(-1.2, 0.01, 3.4)
	deck.material_override = VfxMaterials.make("metal_plate")
	add_child(deck)
	# ledge: cut stone block 0.5 m high with vertical + top faces
	var ledge := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(4.0, 0.5, 1.6)
	ledge.mesh = bm
	ledge.position = o + Vector3(-4.0, 0.25, -2.4)
	ledge.material_override = VfxMaterials.make("ledge_stone")
	add_child(ledge)
	var ledge2 := MeshInstance3D.new()
	var bm2 := BoxMesh.new()
	bm2.size = Vector3(1.6, 1.1, 1.6)
	ledge2.mesh = bm2
	ledge2.position = o + Vector3(-1.2, 0.55, -2.4)
	ledge2.material_override = VfxMaterials.make("ledge_stone")
	add_child(ledge2)
	# pool: recessed look via a dark rim box + water plane at ground level
	var pool_rim := MeshInstance3D.new()
	var prm := PlaneMesh.new()
	prm.size = Vector2(6.4, 4.4)
	pool_rim.mesh = prm
	pool_rim.position = o + Vector3(4.0, 0.012, -1.0)
	var wmat := VfxMaterials.make("pool_water")
	wmat.set_shader_parameter("half_extent", Vector2(3.0, 2.0))
	wmat.set_shader_parameter("corner_radius", 0.7)
	pool_rim.material_override = wmat
	add_child(pool_rim)
	_label("flagstones dry", o + Vector3(-4, 1.0, 1.0))
	_label("flagstones wet 0.8", o + Vector3(4, 1.0, 3.0))
	_label("metal plate", o + Vector3(-1.2, 0.8, 3.4))
	_label("ledge stone", o + Vector3(-4.0, 1.2, -2.4))
	_label("pool water", o + Vector3(4.0, 0.8, -1.0))
	return {"shots": [
		{"pos": o + Vector3(0, 7.2, 8.0), "look": o + Vector3(0, 0.0, 0.0), "frames": 2, "dt": 0.2, "fov": 46.0},
		{"pos": o + Vector3(-5.5, 1.5, 4.5), "look": o + Vector3(-3.0, 0.2, 0.5), "frames": 1, "fov": 40.0},
		{"pos": o + Vector3(5.0, 1.6, 4.0), "look": o + Vector3(4.2, 0.0, -1.0), "frames": 1, "fov": 40.0},
	]}


func _station_dbg(o: Vector3) -> Dictionary:
	# lighting comparison: box floor / standard plane / shader plane
	_box(o + Vector3(-5.0, 0.02, 0.0), Vector3(3.5, 0.04, 6), Color(0.5, 0.47, 0.43))
	var pm := PlaneMesh.new()
	pm.size = Vector2(3.5, 6)
	var m1 := MeshInstance3D.new()
	m1.mesh = pm
	m1.position = o + Vector3(-1.4, 0.03, 0.0)
	var sm := StandardMaterial3D.new()
	sm.albedo_color = Color(0.5, 0.47, 0.43)
	m1.material_override = sm
	add_child(m1)
	var m2 := MeshInstance3D.new()
	m2.mesh = pm
	m2.position = o + Vector3(2.2, 0.03, 0.0)
	m2.material_override = VfxMaterials.make("arena_ground")
	add_child(m2)
	var m3 := MeshInstance3D.new()
	m3.mesh = pm
	m3.position = o + Vector3(5.8, 0.03, 0.0)
	var sm3 := StandardMaterial3D.new()
	sm3.albedo_color = Color(0.5, 0.47, 0.43)
	m3.material_override = sm3
	m3.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(m3)
	return {"pos": o + Vector3(0, 6.0, 7.0), "look": o + Vector3(0, 0.0, 0.0), "frames": 3, "fov": 50.0}


func _station_hero(o: Vector3) -> Dictionary:
	# A composite "courtyard" frame: ledge + lava cascade, heated / molten stones, rising wall with
	# dust, frozen water whip, fire burst, lightning, air push, steam, scorch + wet marks.
	var ledge := MeshInstance3D.new()
	var lb := BoxMesh.new()
	lb.size = Vector3(3.2, 0.5, 2.2)
	ledge.mesh = lb
	ledge.position = o + Vector3(-5.6, 0.25, -1.6)
	ledge.material_override = VfxMaterials.make("ledge_stone")
	add_child(ledge)
	# lava cascade: runs along the ledge top then steps down and across the floor
	var pts := PackedVector3Array()
	var ws := PackedFloat32Array()
	for i in 4:
		pts.append(o + Vector3(-6.8 + i * 0.5, 0.5, -1.6))
		ws.append(0.9)
	pts.append(o + Vector3(-4.85, 0.5, -1.6))
	ws.append(1.1)
	pts.append(o + Vector3(-4.7, 0.0, -1.55))
	ws.append(1.25)
	for i in 6:
		var t: float = float(i) / 5.0
		pts.append(o + Vector3(-4.2 + i * 0.55, 0.0, -1.5 + t * 1.0 + sin(t * 3.0) * 0.2))
		ws.append(1.3 + t * 0.2)
	var lava := LavaWaveView.new()
	add_child(lava)
	lava.pattern_seed = 3.3
	lava.set_path(pts, ws)
	lava.set_state(1.0, 0.1, 1.0)
	_track(lava)
	# stones
	var s1 := StoneView.new()
	s1.setup(21, 0.42)
	add_child(s1)
	s1.position = o + Vector3(-3.6, 0.38, 1.8)
	s1.set_thermal(1.0, 0.85)
	var s2 := StoneView.new()
	s2.setup(5, 0.34)
	add_child(s2)
	s2.position = o + Vector3(-5.0, 0.3, 1.6)
	s2.rotation.y = 1.1
	s2.set_thermal(0.85, 0.2)
	var s3 := StoneView.new()
	s3.setup(33, 0.3)
	add_child(s3)
	s3.position = o + Vector3(-2.2, 0.26, 2.7)
	s3.set_thermal(0.0, 0.0)
	# steam where the lava meets the wet stone
	var sf := SteamFX.new()
	add_child(sf)
	_track(sf)
	sf.play(o + Vector3(-2.3, 0.1, -1.0), 1.0)
	sf.advance(0.45)
	# earth wall rising with dust
	var wall := EarthWallView.new()
	add_child(wall)
	wall.position = o + Vector3(0.4, 0.0, 0.2)
	wall.setup(8, 2.6, 1.05, 0.6)
	wall.manual_time = true
	_manual.append(wall)
	wall.set_rise(0.62)
	# frozen water whip arcing to the right of the wall
	var wp := PackedVector3Array()
	var wr := PackedFloat32Array()
	for i in 16:
		var t: float = float(i) / 15.0
		wp.append(o + Vector3(1.8 + t * 2.4, 0.5 + 1.1 * sin(t * PI) * (1.0 - 0.3 * t), 1.6 - t * 0.8))
		wr.append(lerpf(0.11, 0.06, t))
	var wh := WaterRibbonView.new()
	add_child(wh)
	wh.set_points(wp, wr)
	wh.set_state(0.0)
	_track(wh)
	var wh2 := WaterRibbonView.new()
	add_child(wh2)
	var wp2 := PackedVector3Array()
	for p in wp:
		wp2.append(p + Vector3(0.0, 0.0, 1.1))
	wh2.set_points(wp2, wr)
	wh2.set_state(0.85)
	_track(wh2)
	var orb := WaterBlobView.new()
	orb.setup(0.3)
	add_child(orb)
	orb.position = o + Vector3(1.3, 0.8, 2.6)
	_track(orb)
	# defender stand-ins
	for x in [3.8, 5.0]:
		var cm := MeshInstance3D.new()
		var cap := CapsuleMesh.new()
		cap.radius = 0.27
		cap.height = 1.7
		cm.mesh = cap
		cm.position = o + Vector3(x, 0.85, -1.0)
		cm.material_override = _matte(Color(0.28, 0.30, 0.38))
		add_child(cm)
	# fire burst from the first defender's hand toward the second
	var fb := FireBurstFX.new()
	add_child(fb)
	fb.play(o + Vector3(3.5, 1.2, -1.0), Vector3(1, 0.05, 0.0), 2.4, 1.0)
	fb.advance(0.16)
	fb.manual_time = true
	_manual.append(fb)
	# lightning through three targets, high
	var la := LightningArcFX.new()
	add_child(la)
	la.strike(PackedVector3Array([o + Vector3(1.0, 2.8, -2.2), o + Vector3(2.6, 2.0, -1.8), o + Vector3(4.2, 2.5, -2.4), o + Vector3(5.8, 1.6, -2.0)]), 4)
	la.advance(0.04)
	_track(la)
	# air push from the far right
	var ap := AirPushFX.new()
	add_child(ap)
	ap.play(o + Vector3(6.4, 1.0, 1.4), Vector3(-1, 0.0, -0.1), 1.3, 5.0)
	ap.advance(0.2)
	_track(ap)
	# scorch + wet marks, dust on the floor, embers at the lava front
	var sd := ScorchDecal.new()
	add_child(sd)
	sd.place(o + Vector3(5.0, 0.0, 0.4), 0.8, "scorch", 8.0)
	sd.advance(0.4)
	_track(sd)
	var wd := ScorchDecal.new()
	add_child(wd)
	wd.place(o + Vector3(2.2, 0.0, 2.0), 0.9, "wet", 8.0)
	wd.advance(0.4)
	_track(wd)
	var em := EmberFX.new()
	add_child(em)
	em.play(o + Vector3(-1.3, 0.3, -0.7), Vector3.UP, 1.0)
	em.advance(0.3)
	_track(em)
	var dp := DustPuffFX.new()
	add_child(dp)
	dp.play(o + Vector3(-0.6, 0.0, 1.0), Vector3.UP, 0.8)
	dp.advance(0.3)
	_track(dp)
	return {"shots": [
		{"pos": o + Vector3(1.0, 6.6, 10.4), "look": o + Vector3(0.2, 0.5, 0.0), "frames": 3, "dt": 0.0, "fov": 46.0},
		{"pos": o + Vector3(-3.4, 2.2, 5.6), "look": o + Vector3(-3.8, 0.4, 0.2), "frames": 1, "dt": 0.0, "fov": 38.0},
		{"pos": o + Vector3(3.4, 2.4, 6.0), "look": o + Vector3(3.2, 1.0, 0.0), "frames": 1, "dt": 0.0, "fov": 40.0},
	]}
