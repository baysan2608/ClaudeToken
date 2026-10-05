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
const SETTLE_SECONDS: float = 3.0

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
var _only_origin: bool = false
var _skip: PackedStringArray = PackedStringArray()
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
		elif a.begins_with("--skip="):
			_skip = a.substr(7).split(",")
		elif a == "--lite":
			VfxMaterials.screen_refraction = false
		elif a == "--at0":
			_only_origin = true
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
	env.ambient_light_energy = 1.35
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
	_sun.shadow_enabled = not (_no_shadow or _only.has("cost"))
	if _ortho:
		_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	if not _nomax:
		_sun.directional_shadow_max_distance = 40.0
	add_child(_sun)


func _build_ground() -> void:
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(STATION_SPACING * 20.0, 60.0)
	mi.mesh = pm
	mi.position = Vector3(STATION_SPACING * 9.0, 0.0, 0.0)
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
	_stations.append({"name": "cost", "fn": _station_cost})
	# Moveset families, built from synthetic sim bodies through BodyViews (the real mapping).
	_stations.append({"name": "mv_earth", "fn": _station_mv_earth})
	_stations.append({"name": "mv_water", "fn": _station_mv_water})
	_stations.append({"name": "mv_fire", "fn": _station_mv_fire})
	_stations.append({"name": "mv_air", "fn": _station_mv_air})
	_stations.append({"name": "mv_cues", "fn": _station_mv_cues})
	_stations.append({"name": "mv_cost", "fn": _station_mv_cost})


var _bvs: Array[BodyViews] = []


func _process(_dt: float) -> void:
	for bv in _bvs:
		if is_instance_valid(bv):
			bv.render(1.0)


func _run() -> void:
	# warm-up: let the renderer build pipelines / shadow maps before the first capture
	for _i in 10:
		await get_tree().process_frame
	for i in _stations.size():
		var st: Dictionary = _stations[i]
		if (_only.is_empty() and (st.name == "cost" or st.name == "mv_cost")) or (not _only.is_empty() and not _only.has(st.name)):
			continue
		var origin := Vector3(STATION_SPACING * i, 0.0, 0.0)
		if _only_origin or st.name == "cost" or st.name == "mv_cost":
			origin = Vector3.ZERO
		var cam_info: Dictionary = await st.fn.call(origin)
		if cam_info.get("skip_capture", false):
			continue
		await _capture(String(st.name), cam_info)
	get_tree().quit()


## Keep rendering for `seconds` of wall-clock time (and at least 8 frames).
func _settle(seconds: float) -> void:
	var t0: int = Time.get_ticks_msec()
	var n: int = 0
	while n < 8 or float(Time.get_ticks_msec() - t0) < seconds * 1000.0:
		await get_tree().process_frame
		n += 1


func _capture(shot: String, cam_info: Dictionary) -> void:
	var shots: Array = cam_info.get("shots", [cam_info])
	for k in shots.size():
		var ci: Dictionary = shots[k]
		_cam.global_position = ci.pos
		_cam.look_at(ci.look, Vector3.UP)
		if ci.has("fov"):
			_cam.fov = ci.fov
		# Let the renderer catch up with the new camera position (shadow cascades, pipelines): the
		# software Vulkan driver (lavapipe) JIT-compiles shaders slowly and the engine draws with a
		# fallback ubershader / stale shadow data in the meantime. Effects are NOT advanced here.
		await _settle(SETTLE_SECONDS if k == 0 else 0.7)
		# then advance the manually driven effects by exactly `frames` steps of `dt`
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
	_box(o + Vector3(0, 0.02, 0.0), Vector3(10, 0.04, 6), Color(0.30, 0.29, 0.28))
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
		{"pos": o + Vector3(-1.8, 1.3, 3.8), "look": o + Vector3(-1.8, 0.6, 0.0), "frames": 1, "dt": 0.28, "fov": 40.0},
		{"pos": o + Vector3(2.4, 1.3, 3.8), "look": o + Vector3(2.4, 0.6, 0.0), "frames": 1, "dt": 0.0, "fov": 40.0},
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
	wm.set_shader_parameter("wetness", 0.8)
	g_wet.material_override = wm
	add_child(g_wet)
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


func _station_hero(o: Vector3) -> Dictionary:
	# A composite "courtyard" frame: ledge + lava cascade, heated / molten stones, rising wall with
	# dust, frozen water whip, fire burst, lightning, air push, steam, scorch + wet marks.
	if not _skip.has("ledge"):
		var ledge := MeshInstance3D.new()
		var lb := BoxMesh.new()
		lb.size = Vector3(3.2, 0.5, 2.2)
		ledge.mesh = lb
		ledge.position = o + Vector3(-5.6, 0.25, -1.6)
		ledge.material_override = VfxMaterials.make("ledge_stone")
		add_child(ledge)
	if not _skip.has("lava"):
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
	s1.use_light = not _skip.has("stonelight")
	s1.setup(21, 0.42)
	add_child(s1)
	s1.position = o + Vector3(-3.6, 0.38, 1.8)
	s1.set_thermal(1.0, 0.85)
	var s2 := StoneView.new()
	s2.use_light = not _skip.has("stonelight")
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
	if not _skip.has("steam"):
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
	if not _skip.has("water"):
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
	if not _skip.has("fire"):
		# fire burst from the first defender's hand toward the second
		var fb := FireBurstFX.new()
		add_child(fb)
		fb.play(o + Vector3(3.5, 1.2, -1.0), Vector3(1, 0.05, 0.0), 2.4, 1.0)
		fb.advance(0.16)
		fb.manual_time = true
		_manual.append(fb)
	if not _skip.has("lightning"):
		# lightning through three targets, high
		var la := LightningArcFX.new()
		add_child(la)
		la.strike(PackedVector3Array([o + Vector3(1.0, 2.8, -2.2), o + Vector3(2.6, 2.0, -1.8), o + Vector3(4.2, 2.5, -2.4), o + Vector3(5.8, 1.6, -2.0)]), 4)
		la.advance(0.04)
		_track(la)
	if not _skip.has("air"):
		# air push from the far right
		var ap := AirPushFX.new()
		add_child(ap)
		ap.play(o + Vector3(6.4, 1.0, 1.4), Vector3(-1, 0.0, -0.1), 1.3, 5.0)
		ap.advance(0.2)
		_track(ap)
	if not _skip.has("marks"):
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


# --------------------------------------------------------------------------------------------
# Cost table: instantiate each effect alone and log the visible-pass draw calls / primitives
# (shadow casters are counted again in the shadow pass; printed separately).
func _info() -> Vector3i:
	var vp: Viewport = get_viewport()
	return Vector3i(
		vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_OBJECTS_IN_FRAME),
		vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME),
		vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_PRIMITIVES_IN_FRAME))


func _station_cost(o_unused: Vector3) -> Dictionary:
	var o := Vector3.ZERO  # effects are built in world space; always measured at the origin
	_cam.global_position = o + Vector3(0, 2.0, 5.0)
	_cam.look_at(o + Vector3(0, 0.8, 0), Vector3.UP)
	await _settle(2.0)
	var base: Vector3i = _info()
	var rows: Array[String] = []
	var makers: Array = [
		["StoneView (melt 1)", func() -> Node:
			var n := StoneView.new()
			n.setup(3, 0.4)
			n.use_light = false
			n.set_thermal(1.0, 1.0)
			return n],
		["StoneView + OmniLight", func() -> Node:
			var n := StoneView.new()
			n.setup(3, 0.4)
			n.set_thermal(1.0, 1.0)
			return n],
		["EarthWallView", func() -> Node:
			var n := EarthWallView.new()
			n.setup(3)
			n.dust = false
			n.set_rise(1.0)
			return n],
		["LavaWaveView (10 pts)", func() -> Node:
			var n := LavaWaveView.new()
			var p := PackedVector3Array()
			var w := PackedFloat32Array()
			for i in 10:
				p.append(Vector3(-2.0 + i * 0.45, 0, 0))
				w.append(1.0)
			n.set_path(p, w)
			return n],
		["WaterRibbonView (18 pts)", func() -> Node:
			var n := WaterRibbonView.new()
			var p := PackedVector3Array()
			var r := PackedFloat32Array()
			for i in 18:
				p.append(Vector3(-1.5 + i * 0.17, 0.8, 0))
				r.append(0.08)
			n.set_points(p, r)
			return n],
		["WaterBlobView", func() -> Node:
			var n := WaterBlobView.new()
			n.setup(0.4)
			n.position = Vector3(0, 0.8, 0)
			return n],
		["FireBurstFX (t=0.2)", func() -> Node:
			var n := FireBurstFX.new()
			n.manual_time = true
			n.play(Vector3(-1, 1, 0), Vector3.RIGHT, 2.0, 1.0)
			n.advance(0.2)
			return n],
		["FireChargeFX (1.0)", func() -> Node:
			var n := FireChargeFX.new()
			n.position = Vector3(0, 0.8, 0)
			n.set_charge(1.0)
			return n],
		["LightningArcFX", func() -> Node:
			var n := LightningArcFX.new()
			n.manual_time = true
			n.strike(PackedVector3Array([Vector3(-2, 2, 0), Vector3(0, 1, 0), Vector3(2, 2, 0)]), 1)
			n.advance(0.05)
			return n],
		["ChargeAimFX", func() -> Node:
			var n := ChargeAimFX.new()
			n.set_aim(Vector3(-2, 1, 0), Vector3(2, 1, 0), 0.5)
			return n],
		["AirPushFX", func() -> Node:
			var n := AirPushFX.new()
			n.manual_time = true
			n.play(Vector3(-2, 1, 0), Vector3.RIGHT, 1.5, 4.0)
			n.advance(0.25)
			return n],
		["GlideTrailFX", func() -> Node:
			var n := GlideTrailFX.new()
			n.manual_time = true
			n.begin(null)
			for i in 20:
				n.push(Vector3(-2 + i * 0.2, 1.0 + 0.2 * sin(i * 0.5), 0))
			n.advance(0.0)
			return n],
		["DustPuffFX", func() -> Node:
			var n := DustPuffFX.new()
			n.manual_time = true
			n.play(Vector3.ZERO, Vector3.UP, 1.0)
			n.advance(0.3)
			return n],
		["SteamFX", func() -> Node:
			var n := SteamFX.new()
			n.manual_time = true
			n.play(Vector3.ZERO, 1.0)
			n.advance(0.3)
			return n],
		["SplashFX", func() -> Node:
			var n := SplashFX.new()
			n.manual_time = true
			n.play(Vector3.ZERO, Vector3.UP, 1.0)
			n.advance(0.2)
			return n],
		["EmberFX", func() -> Node:
			var n := EmberFX.new()
			n.manual_time = true
			n.play(Vector3.ZERO, Vector3.UP, 1.0)
			n.advance(0.2)
			return n],
		["ScorchDecal", func() -> Node:
			var n := ScorchDecal.new()
			n.manual_time = true
			n.place(Vector3.ZERO, 1.0, "scorch", 5.0)
			n.advance(0.2)
			return n],
	]
	print("COST baseline objects=%d draws=%d prims=%d" % [base.x, base.y, base.z])
	for m in makers:
		var node: Node = m[1].call()
		add_child(node)
		await _settle(0.6)
		var cur: Vector3i = _info()
		print("COST %-28s objects=%d draws=%d prims=%d" % [m[0], cur.x - base.x, cur.y - base.y, cur.z - base.z])
		node.queue_free()
		await get_tree().process_frame
		await get_tree().process_frame
	return {"pos": o + Vector3(0, 2.0, 5.0), "look": o + Vector3(0, 0.8, 0), "frames": 1, "skip_capture": true}


# -------------------------------------------------------------------------------- moveset stations
## A fresh sim world per station (no arena solids), bodies set by hand, views through BodyViews.
func _mv_world() -> Array:
	var w := CombatWorld.new(1)
	w.arena.solids.clear()
	var bv := BodyViews.new()
	add_child(bv)
	bv.bind(w)
	_bvs.append(bv)
	return [w, bv]


func _mv_body(w: CombatWorld, mat: int, form: int, mass: float, p: Vector3, tag: String = "", f: Dictionary = {}) -> MatBody:
	var b := w.spawn_body(mat, form, mass, p, "gallery")
	b.tag = StringName(tag)
	b.age = 2.0
	for k in f:
		b.set(k, f[k])
	return b


func _mv_zone(w: CombatWorld, tag: String, p: Vector3, r: float, mat: int = Sim.Mat.AIR, f: Dictionary = {}) -> MatBody:
	var z := w.spawn_zone(StringName(tag), p, r, 0, 10.0, mat, 0.0, -1.0)
	z.age = 2.0
	for k in f:
		z.set(k, f[k])
	return z


func _mv_wall(w: CombatWorld, mat: int, p: Vector3, tag: String, half: Vector3, f: Dictionary = {}) -> MatBody:
	var b := _mv_body(w, mat, Sim.Form.WALL, 80.0, p, tag, f)
	b.wall_half = half
	b.wall_rise = 1.0
	return b


func _mv_wave(w: CombatWorld, mat: int, p: Vector3, tag: String, length: float, f: Dictionary = {}) -> MatBody:
	var b := _mv_body(w, mat, Sim.Form.WAVE, 30.0, p, tag, f)
	var path := PackedVector3Array()
	for i in 8:
		var t := float(i) / 7.0
		path.append(p + Vector3(-length * (1.0 - t), 0, sin(t * 3.0) * 0.25))
	b.wave_path = path
	b.wave_width = 1.3
	b.wave_dir = Vector3.RIGHT
	b.vel = Vector3(4, 0, 0)
	if mat == Sim.Mat.WATER:
		b.phase = Sim.Phase.LIQUID
		b.liquid = 1.0
	return b


func _mv_settle(bv: BodyViews) -> void:
	bv.push_state()
	bv.push_state()
	bv.render(1.0)


func _station_mv_earth(o: Vector3) -> Dictionary:
	var wb := _mv_world()
	var w: CombatWorld = wb[0]
	var bv: BodyViews = wb[1]
	# metal: disc (spinning), lance, Aegis plate, caltrop field, red-hot rod
	_mv_body(w, Sim.Mat.METAL, Sim.Form.CHUNK, 2.0, o + Vector3(-6.0, 1.2, 1.5), "disc", {"spin": 40.0, "vel": Vector3(10, 0, 2), "radius": 0.28})
	_mv_body(w, Sim.Mat.METAL, Sim.Form.CHUNK, 3.0, o + Vector3(-4.5, 1.3, 1.5), "lance", {"vel": Vector3(12, 1, 0), "radius": 0.2})
	_mv_body(w, Sim.Mat.METAL, Sim.Form.CHUNK, 3.0, o + Vector3(-4.5, 0.7, 2.6), "rod", {"vel": Vector3(12, 0, -3), "radius": 0.2, "temp": 950.0})
	_mv_wall(w, Sim.Mat.METAL, o + Vector3(-6.2, 0, -1.0), "plate", Vector3(0.7, 0.8, 0.1))
	_mv_zone(w, "caltrops", o + Vector3(-4.0, 0, -1.2), 1.0, Sim.Mat.METAL)
	_label("metal: disc / lance / hot rod / plate / caltrops", o + Vector3(-5.0, 2.4, 0.0))
	# sand: slug, cloud, surge, quicksand, sand wall
	_mv_body(w, Sim.Mat.SAND, Sim.Form.CHUNK, 3.0, o + Vector3(-1.6, 1.1, 2.2), "slug", {"vel": Vector3(9, 0, 1), "radius": 0.22})
	_mv_zone(w, "sand_cloud", o + Vector3(-1.5, 0, -1.6), 1.6, Sim.Mat.SAND)
	_mv_wave(w, Sim.Mat.SAND, o + Vector3(1.6, 0, 2.4), "sand_surge", 2.6)
	_mv_zone(w, "quicksand", o + Vector3(1.2, 0, -0.6), 1.2, Sim.Mat.SAND)
	_mv_wall(w, Sim.Mat.SAND, o + Vector3(0.6, 0, -3.2), "sand", Vector3(1.0, 0.5, 0.25))
	_label("sand: slug / cloud / surge / quicksand / wall", o + Vector3(0.0, 2.4, 0.0))
	# glass: wall, shards; magma: glob, bomb, obsidian wall, lava pool; stone spikes
	_mv_wall(w, Sim.Mat.GLASS, o + Vector3(3.6, 0, -2.6), "glass", Vector3(1.0, 0.6, 0.25))
	_mv_body(w, Sim.Mat.GLASS, Sim.Form.SHARD, 0.6, o + Vector3(3.2, 1.1, 1.6), "needle", {"vel": Vector3(9, 2, 0), "radius": 0.12})
	_mv_body(w, Sim.Mat.STONE, Sim.Form.CHUNK, 4.0, o + Vector3(4.6, 1.0, 1.8), "glob", {"temp": 1150.0, "liquid": 0.8, "vel": Vector3(8, 1, 0), "radius": 0.24})
	_mv_wall(w, Sim.Mat.STONE, o + Vector3(6.4, 0, -1.6), "obsidian", Vector3(1.0, 0.55, 0.28))
	_mv_zone(w, "lava_pool", o + Vector3(5.8, 0, 1.0), 1.0, Sim.Mat.STONE, {"liquid": 1.0})
	_mv_wall(w, Sim.Mat.STONE, o + Vector3(3.2, 0, -0.4), "spikes", Vector3(0.9, 0.5, 0.2))
	_label("glass wall / shard, magma glob / pool, obsidian, spikes", o + Vector3(4.8, 2.4, 0.0))
	_mv_settle(bv)
	return {"shots": [
		{"pos": o + Vector3(0, 5.6, 9.5), "look": o + Vector3(0, 0.4, -0.2), "frames": 6, "dt": 0.05, "fov": 50.0},
		{"pos": o + Vector3(-5.0, 1.9, 4.6), "look": o + Vector3(-5.0, 0.8, 0.4), "frames": 2, "fov": 40.0},
		{"pos": o + Vector3(4.8, 2.0, 5.0), "look": o + Vector3(4.6, 0.6, -0.6), "frames": 2, "fov": 42.0},
	]}


func _station_mv_water(o: Vector3) -> Dictionary:
	var wb := _mv_world()
	var w: CombatWorld = wb[0]
	var bv: BodyViews = wb[1]
	_mv_wave(w, Sim.Mat.WATER, o + Vector3(-3.2, 0, 2.2), "water_wave", 3.0)
	_mv_wall(w, Sim.Mat.WATER, o + Vector3(-5.6, 0, -1.8), "ice", Vector3(1.1, 0.75, 0.3), {"phase": Sim.Phase.FROZEN, "liquid": 0.0})
	_mv_wave(w, Sim.Mat.WATER, o + Vector3(-1.4, 0, -0.4), "rime", 2.4, {"phase": Sim.Phase.FROZEN})
	_mv_zone(w, "ice_floor", o + Vector3(-1.6, 0, -2.8), 1.3, Sim.Mat.WATER)
	_mv_body(w, Sim.Mat.WATER, Sim.Form.SHARD, 0.5, o + Vector3(-5.6, 1.2, 1.0), "needle", {"vel": Vector3(10, 1, 0), "radius": 0.1, "phase": Sim.Phase.FROZEN, "liquid": 0.0})
	_mv_wall(w, Sim.Mat.WATER, o + Vector3(-3.6, 0, -3.4), "spikes", Vector3(0.8, 0.5, 0.2), {"phase": Sim.Phase.FROZEN})
	_label("water wave / ice wall / rime / ice floor / spikes", o + Vector3(-3.4, 2.4, 0.0))
	_mv_zone(w, "fog", o + Vector3(1.6, 0, -2.4), 1.8, Sim.Mat.WATER)
	_mv_body(w, Sim.Mat.STEAM, Sim.Form.CLOUD, 2.0, o + Vector3(1.4, 0.2, 1.4), "", {"radius": 0.9})
	_mv_zone(w, "geyser", o + Vector3(3.2, 0, 0.0), 1.4, Sim.Mat.WATER, {"tier": 2})
	_mv_zone(w, "steam_screen", o + Vector3(3.0, 0, -3.4), 2.0, Sim.Mat.STEAM)
	_label("fog / steam / geyser / steam screen", o + Vector3(2.2, 2.6, 0.0))
	_mv_wall(w, Sim.Mat.PLANT, o + Vector3(6.0, 0, -2.4), "vine", Vector3(1.0, 0.8, 0.2))
	_mv_wave(w, Sim.Mat.PLANT, o + Vector3(6.6, 0, 1.6), "roots", 2.2)
	_mv_zone(w, "briar", o + Vector3(5.2, 0, 0.0), 0.9, Sim.Mat.PLANT)
	_label("vine lattice / roots / briar", o + Vector3(6.0, 2.4, 0.0))
	_mv_settle(bv)
	return {"shots": [
		{"pos": o + Vector3(0, 5.6, 9.5), "look": o + Vector3(0, 0.5, -0.4), "frames": 6, "dt": 0.05, "fov": 52.0},
		{"pos": o + Vector3(-3.8, 1.8, 4.8), "look": o + Vector3(-3.4, 0.6, 0.0), "frames": 2, "fov": 44.0},
		{"pos": o + Vector3(5.2, 2.0, 5.0), "look": o + Vector3(5.6, 0.7, -0.6), "frames": 2, "fov": 44.0},
	]}


func _station_mv_fire(o: Vector3) -> Dictionary:
	var wb := _mv_world()
	var w: CombatWorld = wb[0]
	var bv: BodyViews = wb[1]
	_mv_body(w, Sim.Mat.FIRE, Sim.Form.CHUNK, 1.0, o + Vector3(-5.5, 1.2, 1.4), "fireball", {"vel": Vector3(10, 0, 0), "heat_payload": 400.0, "tier": 1})
	_mv_body(w, Sim.Mat.FIRE, Sim.Form.CHUNK, 1.0, o + Vector3(-5.5, 1.4, -0.6), "comet", {"vel": Vector3(14, 0, 0), "heat_payload": 600.0})
	_mv_body(w, Sim.Mat.FIRE, Sim.Form.CHUNK, 0.3, o + Vector3(-3.6, 1.0, 2.0), "ember", {"vel": Vector3(6, 2, 0)})
	_mv_body(w, Sim.Mat.FIRE, Sim.Form.CHUNK, 0.3, o + Vector3(-3.4, 0.15, 0.6), "ember", {"props": {"mine": true}, "radius": 0.15})
	_label("fireball / comet / ember / mine", o + Vector3(-4.6, 2.4, 0.0))
	_mv_zone(w, "fire_field", o + Vector3(-1.2, 0, -1.8), 1.5, Sim.Mat.FIRE)
	_mv_zone(w, "fire_field", o + Vector3(1.8, 0, -2.8), 1.0, Sim.Mat.FIRE, {"props": {"blue": true}})
	_mv_wave(w, Sim.Mat.FIRE, o + Vector3(0.6, 0, 2.0), "fire_line", 3.0)
	_label("fire field / blue field / fire line", o + Vector3(0.0, 2.4, 0.0))
	_mv_zone(w, "corona", o + Vector3(3.6, 0, 1.0), 0.9, Sim.Mat.FIRE)
	_mv_zone(w, "static_field", o + Vector3(5.8, 0, -1.6), 1.6, Sim.Mat.AIR)
	_mv_wave(w, Sim.Mat.AIR, o + Vector3(7.0, 0, 2.0), "ground_current", 2.5)
	var cs := _mv_body(w, Sim.Mat.STONE, Sim.Form.CHUNK, 8.0, o + Vector3(4.0, 1.1, -1.4), "", {"charge": 25.0, "radius": 0.3})
	cs.on_ground = false
	_label("corona / static field / ground current / charged stone", o + Vector3(5.4, 2.4, 0.0))
	_mv_settle(bv)
	return {"shots": [
		{"pos": o + Vector3(0, 5.0, 9.0), "look": o + Vector3(0, 0.6, -0.2), "frames": 6, "dt": 0.05, "fov": 52.0},
		{"pos": o + Vector3(-4.6, 1.6, 4.2), "look": o + Vector3(-4.6, 0.9, 0.4), "frames": 2, "fov": 40.0},
		{"pos": o + Vector3(5.0, 1.8, 5.0), "look": o + Vector3(5.0, 0.7, -0.4), "frames": 2, "fov": 42.0},
	]}


func _station_mv_air(o: Vector3) -> Dictionary:
	var wb := _mv_world()
	var w: CombatWorld = wb[0]
	var bv: BodyViews = wb[1]
	_mv_body(w, Sim.Mat.AIR, Sim.Form.CHUNK, 0.1, o + Vector3(-6.0, 1.2, 1.6), "crescent", {"vel": Vector3(14, 0, 0), "radius": 0.6})
	var ww := _mv_wall(w, Sim.Mat.AIR, o + Vector3(-6.0, 0, -2.0), "wind_wall", Vector3(1.2, 1.0, 0.2))
	ww.form = Sim.Form.ZONE
	ww.tag = &"wind_wall"
	_mv_zone(w, "tornado", o + Vector3(-3.4, 0, -1.0), 1.4, Sim.Mat.AIR, {"spin": 6.0})
	_mv_zone(w, "tornado", o + Vector3(-0.6, 0, -1.4), 1.3, Sim.Mat.AIR, {"spin": 6.0, "props": {"infused": "sand"}})
	_mv_zone(w, "tornado", o + Vector3(2.2, 0, -1.4), 1.3, Sim.Mat.AIR, {"spin": 6.0, "props": {"infused": "fire"}})
	_mv_zone(w, "tornado", o + Vector3(5.0, 0, -1.4), 1.3, Sim.Mat.AIR, {"spin": 6.0, "props": {"infused": "water"}})
	_label("crescent / wind wall / tornado: plain, sand, fire, water", o + Vector3(-1.0, 3.6, -1.0))
	_mv_body(w, Sim.Mat.AIR, Sim.Form.CHUNK, 0.1, o + Vector3(-3.8, 0.9, 2.6), "twister", {"vel": Vector3(10, 0, 0), "radius": 0.3, "spin": 12.0})
	_mv_zone(w, "eddy", o + Vector3(-1.2, 0, 2.4), 1.0, Sim.Mat.AIR, {"spin": 8.0})
	_mv_zone(w, "null_bubble", o + Vector3(1.2, 1.0, 2.4), 1.0, Sim.Mat.AIR)
	_mv_zone(w, "vacuum_well", o + Vector3(3.6, 0, 2.6), 1.4, Sim.Mat.AIR)
	_mv_zone(w, "sound_barrier", o + Vector3(6.4, 0.0, 2.2), 1.1, Sim.Mat.AIR)
	_mv_wave(w, Sim.Mat.AIR, o + Vector3(7.6, 0, -0.2), "dust_line", 2.0)
	_label("twister / eddy / null bubble / vacuum well / sound barrier", o + Vector3(1.6, 2.6, 2.4))
	_mv_settle(bv)
	return {"shots": [
		{"pos": o + Vector3(0, 5.6, 10.5), "look": o + Vector3(0, 0.9, 0.0), "frames": 6, "dt": 0.05, "fov": 54.0},
		{"pos": o + Vector3(1.0, 1.8, 6.4), "look": o + Vector3(1.6, 0.8, 1.6), "frames": 2, "fov": 46.0},
	]}


func _station_mv_cues(o: Vector3) -> Dictionary:
	var pool := VfxPool.new()
	add_child(pool)
	var styles := ["sand", "frost", "smoke", "leaves", "metal", "blue_sparks", "water", "ember"]
	for i in styles.size():
		var b: BurstFX = pool.get_fx("burst")
		_track(b)
		b.play(o + Vector3(-6.0 + i * 1.5, 0.1, 2.0), Vector3.UP, 1.0, styles[i])
		b.advance(0.25)
		_label(styles[i], o + Vector3(-6.0 + i * 1.5, 1.6, 2.0))
	var mats := ["ice", "glass", "stone", "metal"]
	for i in mats.size():
		var sh: ShardsFX = pool.get_fx("shards")
		_track(sh)
		sh.play(o + Vector3(-5.0 + i * 2.2, 0.8, -0.4), Vector3.UP, 1.0, mats[i], i, o.y)
		sh.advance(0.12)
		_label("shards " + mats[i], o + Vector3(-5.0 + i * 2.2, 2.0, -0.4))
	var bl: BlastFX = pool.get_fx("blast")
	_track(bl)
	bl.play(o + Vector3(4.6, 0.8, -1.2), 1.4, 1.0)
	bl.advance(0.17)
	var bm: BeamFX = pool.get_fx("beam")
	_track(bm)
	bm.play(o + Vector3(-6.0, 1.2, -3.0), o + Vector3(0.0, 1.0, -3.4), 0.5, "blue")
	bm.advance(0.15)
	var bs: BeamFX = pool.get_fx("beam")
	_track(bs)
	bs.play(o + Vector3(-6.0, 0.7, -2.4), o + Vector3(0.0, 0.6, -2.6), 0.5, "sand")
	bs.advance(0.15)
	for i in 3:
		var r: RingFX = pool.get_fx("ring")
		_track(r)
		var col := VfxPalette.color(["sound", "vacuum", "wind"][i])
		r.play(o + Vector3(1.6 + i * 1.8, 1.1, -3.2), Vector3.BACK, 0.2, 0.8, 0.5, col, {"billboard": true, "count": 2, "width": 0.1})
		r.advance(0.22)
	for t in 3:
		var c: ChargeFX = pool.get_fx("charge")
		if c == null:
			c = ChargeFX.new()
			add_child(c)
		_track(c)
		c.set_anchor(o + Vector3(5.6 + t * 1.2 - 2.4, 1.2, 1.4), o + Vector3(5.6 + t * 1.2 - 2.4, 0, 1.4))
		c.set_charge(t + 1, 0.5, ["flame", "ice", "lightning"][t])
		c.advance(0.1)
	_label("blast / blue beam / sandblast / rings / charge T1-T3", o + Vector3(2.0, 2.8, -1.0))
	return {"shots": [
		{"pos": o + Vector3(0, 4.2, 9.0), "look": o + Vector3(0, 0.8, -0.6), "frames": 1, "dt": 0.0, "fov": 54.0},
	]}


## Draw calls / primitives of each new view (same method as `cost`).
func _station_mv_cost(_o: Vector3) -> Dictionary:
	_cam.global_position = Vector3(0, 2.0, 6.0)
	_cam.look_at(Vector3(0, 0.8, 0), Vector3.UP)
	await _settle(2.0)
	var base: Vector3i = _info()
	var makers: Array = [
		["CloudView (sand_cloud)", func() -> Node:
			var n := CloudView.new(); n.configure("sand_cloud", 1); n.set_shape(1.5, 1.2); n.on_acquire(); return n],
		["CrystalView (wall)", func() -> Node:
			var n := CrystalView.new(); n.setup("wall", 1, "ice", Vector3(1, 1, 0.5)); n.on_acquire(); return n],
		["MetalView (disc spinning)", func() -> Node:
			var n := MetalView.new(); n.setup("disc", 1, Vector3.ONE * 0.3); n.set_state(0.0, 40.0); n.on_acquire(); n.position = Vector3(0, 1, 0); return n],
		["MetalView (caltrops r1.2)", func() -> Node:
			var n := MetalView.new(); n.setup("caltrops", 1, Vector3.ONE, 1.2); n.on_acquire(); return n],
		["GroundStripView water (8 pts)", func() -> Node:
			var n := GroundStripView.new(); n.configure("water", 1)
			var p := PackedVector3Array(); var wd := PackedFloat32Array()
			for i in 8:
				p.append(Vector3(-2.0 + i * 0.5, 0, 0)); wd.append(1.2)
			n.set_path(p, wd); return n],
		["VortexView (tornado)", func() -> Node:
			var n := VortexView.new(); n.configure("tornado", "sand", 1); n.set_shape(1.4, 3.0); n.on_acquire(); n.advance(0.1); return n],
		["ShellView (null bubble)", func() -> Node:
			var n := ShellView.new(); n.configure("null_bubble"); n.set_shape(1.0); n.on_acquire(); n.position = Vector3(0, 1, 0); return n],
		["ShellView (vacuum well)", func() -> Node:
			var n := ShellView.new(); n.configure("vacuum_well"); n.set_shape(1.2, 0.45); n.on_acquire(); return n],
		["VineView (lattice)", func() -> Node:
			var n := VineView.new(); n.setup("lattice", 1, Vector3(1, 0.8, 0.2)); n.on_acquire(); return n],
		["FlameFieldView (field r1.5)", func() -> Node:
			var n := FlameFieldView.new(); n.setup("field", 1, 1.5, 0.6); n.on_acquire(); return n],
		["FireballView", func() -> Node:
			var n := FireballView.new(); n.setup("fireball", 0.3); n.on_acquire(); n.position = Vector3(0, 1, 0); return n],
		["WindBladeView (crescent)", func() -> Node:
			var n := WindBladeView.new(); n.setup("crescent", Vector3.ONE); n.on_acquire(); n.position = Vector3(0, 1, 0); return n],
		["GroundDecalView", func() -> Node:
			var n := GroundDecalView.new(); n.place(Vector3.ZERO, 1.2, "quicksand", 1); n.advance(0.5); return n],
		["SpikesView (row)", func() -> Node:
			var n := SpikesView.new(); n.setup("stone", "row", 1, Vector3(1, 0.8, 1)); n.on_acquire(); return n],
		["ChargeFX (T3)", func() -> Node:
			var n := ChargeFX.new(); n.set_anchor(Vector3(0, 1.2, 0), Vector3.ZERO); n.set_charge(3, 0.5, "flame"); n.advance(0.1); return n],
		["RingFX (2 rings)", func() -> Node:
			var n := RingFX.new(); n.manual_time = true; n.play(Vector3(0, 0.05, 0), Vector3.UP, 0.2, 1.5, 0.6, Color.WHITE, {"count": 2}); n.advance(0.2); return n],
		["BurstFX (grit)", func() -> Node:
			var n := BurstFX.new(); n.manual_time = true; n.play(Vector3.ZERO, Vector3.UP, 1.0, "grit"); n.advance(0.2); return n],
		["ShardsFX (ice)", func() -> Node:
			var n := ShardsFX.new(); n.manual_time = true; n.play(Vector3(0, 0.8, 0), Vector3.UP, 1.0, "ice", 1, 0.0); n.advance(0.1); return n],
		["BlastFX", func() -> Node:
			var n := BlastFX.new(); n.manual_time = true; n.play(Vector3(0, 0.8, 0), 1.4); n.advance(0.15); return n],
		["BeamFX (blue)", func() -> Node:
			var n := BeamFX.new(); n.manual_time = true; n.play(Vector3(-2, 1, 0), Vector3(2, 1, 0), 0.5, "blue"); n.advance(0.1); return n],
	]
	print("COST baseline objects=%d draws=%d prims=%d" % [base.x, base.y, base.z])
	for m in makers:
		var node: Node = m[1].call()
		add_child(node)
		if node is VfxEffect and not (node as VfxEffect).manual_time:
			(node as VfxEffect).manual_time = true
		await _settle(0.6)
		var cur: Vector3i = _info()
		print("COST %-30s objects=%d draws=%d prims=%d" % [m[0], cur.x - base.x, cur.y - base.y, cur.z - base.z])
		node.queue_free()
		await get_tree().process_frame
		await get_tree().process_frame
	return {"pos": Vector3(0, 2.0, 5.0), "look": Vector3(0, 0.8, 0), "frames": 1, "skip_capture": true}
