extends SceneTree
## Headless API / pooling smoke test for the VFX layer.
##   tools/scripts/godot.sh --headless -s res://tests/vfx/vfx_smoke_test.gd
## Exits with code 0 on success, 1 on failure.

var _fails: int = 0


func _check(cond: bool, msg: String) -> void:
	if not cond:
		_fails += 1
		printerr("FAIL: ", msg)


func _initialize() -> void:
	await process_frame
	await _run()


func _run() -> void:
	# (async: prewarm awaits frames)
	var root_node := Node3D.new()
	root.add_child(root_node)
	var pool := VfxPool.new()
	root_node.add_child(pool)

	# --- stone continuum (same instance, continuous parameters)
	var st: StoneView = pool.get_fx("stone")
	st.setup(7, 0.4)
	for i in 11:
		var t: float = float(i) / 10.0
		st.set_thermal(minf(t * 2.0, 1.0), clampf(t * 2.0 - 0.6, 0.0, 1.0))
		st.set_crust(clampf(t * 2.0 - 1.0, 0.0, 1.0))
	_check(is_equal_approx(st.get_crust(), 1.0), "stone crust reached 1")
	st.reset()
	_check(st.get_heat() == 0.0 and st.get_melt() == 0.0, "stone reset")
	var mesh: ArrayMesh = VfxMesh.rock_mesh(7)
	var tris: int = mesh.surface_get_array_len(0) / 3
	_check(tris >= 200 and tris <= 500, "rock triangle count in 200..500 (got %d)" % tris)
	_check(VfxMesh.rock_mesh(7) == mesh, "rock mesh cached per seed")

	# --- lava wave
	var lw: LavaWaveView = pool.get_fx("lava_wave")
	var pts := PackedVector3Array([Vector3(0, 0.5, 0), Vector3(1, 0.5, 0), Vector3(1.1, 0.0, 0), Vector3(2, 0.0, 0.2)])
	var wd := PackedFloat32Array([1.0, 1.1, 1.2, 1.3])
	lw.set_path(pts, wd)
	lw.set_state(1.0, 0.2, 1.0)
	lw.advance(0.1)
	_check(lw.get_child_count() > 0, "lava built")

	# --- water
	var wr: WaterRibbonView = pool.get_fx("water_ribbon")
	wr.set_points(PackedVector3Array([Vector3.ZERO, Vector3(0, 0.2, -1), Vector3(0.3, 0.4, -2)]), PackedFloat32Array([0.1, 0.09, 0.06]))
	wr.set_state(0.5)
	wr.advance(0.1)
	var wb: WaterBlobView = pool.get_fx("water_blob")
	wb.setup(0.3)
	wb.set_state(1.0)

	# --- one-shots
	var fb: FireBurstFX = pool.get_fx("fire_burst")
	fb.play(Vector3.ZERO, Vector3.FORWARD, 3.0, 1.0)
	_check(fb.is_playing(), "fire burst playing")
	for i in 40:
		fb.advance(0.02)
	_check(not fb.is_playing(), "fire burst finished")
	var fc: FireChargeFX = pool.get_fx("fire_charge")
	fc.set_charge(0.5)
	fc.advance(0.1)
	var la: LightningArcFX = pool.get_fx("lightning_arc")
	la.strike(PackedVector3Array([Vector3.ZERO, Vector3(2, 1, 0), Vector3(4, 0, 1)]), 99)
	_check(la.is_playing(), "lightning playing")
	for i in 20:
		la.advance(0.02)
	_check(not la.is_playing(), "lightning finished")
	var ca: ChargeAimFX = pool.get_fx("charge_aim")
	ca.set_aim(Vector3.ZERO, Vector3(3, 0, 0), 0.4)
	var ap: AirPushFX = pool.get_fx("air_push")
	ap.play(Vector3.ZERO, Vector3.FORWARD, 1.5, 6.0)
	ap.advance(0.2)
	var gt: GlideTrailFX = pool.get_fx("glide_trail")
	gt.begin(null)
	for i in 10:
		gt.push(Vector3(i * 0.3, 1, 0))
		gt.advance(0.03)
	gt.end()
	var dp: DustPuffFX = pool.get_fx("dust_puff")
	dp.play(Vector3.ZERO, Vector3.UP, 1.0)
	var sf: SteamFX = pool.get_fx("steam")
	sf.play(Vector3.ZERO, 0.8)
	var sp: SplashFX = pool.get_fx("splash")
	sp.play(Vector3.ZERO, Vector3.UP, 1.0)
	var em: EmberFX = pool.get_fx("ember")
	em.play(Vector3.ZERO, Vector3.UP, 1.0)
	var sd: ScorchDecal = pool.get_fx("scorch_decal")
	sd.place(Vector3.ZERO, 1.0, "scorch", 3.0)
	var ew: EarthWallView = pool.get_fx("earth_wall")
	ew.setup(3)
	ew.set_rise(0.5)
	ew.set_damage(0.5)

	# --- pool caps / recycling / release
	var stones: Array = []
	for i in 30:
		var s: Node = pool.get_fx("stone")
		_check(s != null, "recycling pool never returns null at cap")
		stones.append(s)
	var stats: Dictionary = pool.get_stats()
	_check(stats["stone"][0] <= 24, "stone cap 24 respected (active %d)" % stats["stone"][0])
	pool.recycle_oldest = false
	pool.release_all()
	for i in 24:
		pool.get_fx("stone")
	_check(pool.get_fx("stone") == null, "returns null at cap when recycle_oldest = false")
	pool.release_all()
	_check(pool.get_stats()["stone"][0] == 0, "release_all frees everything")
	var a: Node = pool.get_fx("dust_puff")
	pool.release(a)
	pool.release(a)  # double release is safe
	_check(pool.get_stats()["dust_puff"][0] == 0, "double release safe")

	# --- prewarm cycles every built-in type through the pool and releases them
	await pool.prewarm(Vector3(0, -50, 0), 2)
	var total_active: int = 0
	for key in pool.get_stats():
		total_active += int(pool.get_stats()[key][0])
	_check(total_active == 0, "prewarm releases everything (active %d)" % total_active)

	# --- textures built
	_check(VfxTextures.noise_volume().get_width() == VfxTextures.VOLUME_SIZE, "noise volume built")

	print("VFX smoke test: ", "PASS" if _fails == 0 else "FAIL (%d)" % _fails)
	quit(0 if _fails == 0 else 1)
