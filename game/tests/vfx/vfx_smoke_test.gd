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

	# --- moveset views and cues (docs/VFX.md "Moveset layer")
	await _moveset(pool)

	# --- textures built
	_check(VfxTextures.noise_volume().get_width() == VfxTextures.VOLUME_SIZE, "noise volume built")

	print("VFX smoke test: ", "PASS" if _fails == 0 else "FAIL (%d)" % _fails)
	quit(0 if _fails == 0 else 1)


## API, determinism, budgets and pool caps of the moveset layer.
func _moveset(pool: VfxPool) -> void:
	var keys := ["cloud", "crystal", "metal", "ground_strip", "vortex", "shell", "vine", "flame_field", "fireball",
		"wind_blade", "crackle", "ground_decal", "spikes", "charge", "ring", "burst", "shards", "blast", "beam"]
	var stats := pool.get_stats()
	for k in keys:
		_check(stats.has(k), "pool registers %s" % k)
	# every key has a prewarm recipe (pipelines compile behind the loading screen)
	var src: String = (load("res://presentation/vfx/vfx_pool.gd") as GDScript).source_code
	for k in keys:
		_check(src.contains('"%s":' % k), "VfxPool.prewarm covers %s" % k)
	# API smoke: each view through its whole parameter range
	var cv: CloudView = pool.get_fx("cloud")
	for st in CloudView.STYLES:
		cv.configure(st, 3)
		cv.set_shape(1.5, 1.0)
		cv.set_amount(0.5)
		cv.advance(0.1)
	_check(cv.cost() == Vector2i(2, 2), "cloud: 2 draws, 2 layers")
	var cr: CrystalView = pool.get_fx("crystal")
	for m in ["shard", "cluster", "wall", "ridge"]:
		cr.setup(m, 2, "glass", Vector3.ONE)
		cr.set_state(0.5, 0.3, 0.5)
		_check(cr._mi.mesh.get_surface_count() == 1, "crystal %s mesh built" % m)
	_check(FxMesh.crystal_mesh(5, "wall") == FxMesh.crystal_mesh(5, "wall"), "crystal meshes cached per seed")
	var a1: PackedVector3Array = FxMesh.crystal_mesh(6, "cluster").surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
	FxMesh._cache.clear()
	var a2: PackedVector3Array = FxMesh.crystal_mesh(6, "cluster").surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
	_check(a1 == a2, "crystal mesh deterministic per seed")
	_check(a1.size() / 3 <= 500, "crystal cluster <= 500 tris (%d)" % (a1.size() / 3))
	var mv: MetalView = pool.get_fx("metal")
	for sh in ["disc", "lance", "rod", "plate", "orb", "caltrops"]:
		mv.setup(sh, 1, Vector3(0.3, 1.0, 0.3), 1.5)
		mv.set_state(0.8, 40.0)
		mv.advance(0.05)
	_check(mv._field.multimesh.visible_instance_count <= MetalView.FIELD_MAX, "caltrops capped")
	var gs: GroundStripView = pool.get_fx("ground_strip")
	for st2 in ["water", "sand", "rime", "mud"]:
		gs.configure(st2, 1)
		gs.set_path(PackedVector3Array([Vector3.ZERO, Vector3(1, 0, 0), Vector3(2, 0, 0.3)]), PackedFloat32Array([1, 1.1, 1.2]))
		gs.set_state(1.0, 0.2, 2.0)
		gs.advance(0.05)
	var vx: VortexView = pool.get_fx("vortex")
	for kind in ["tornado", "twister", "eddy", "funnel", "vortex_wall"]:
		for inf in ["", "sand", "fire", "water", "steam"]:
			vx.configure(kind, inf, 1)
			vx.set_shape(1.2, 2.5)
			vx.set_spin(-8.0)
			vx.advance(0.05)
	var vtris := 0
	for c in [vx._outer, vx._inner]:
		vtris += (c as MeshInstance3D).mesh.surface_get_arrays(0)[Mesh.ARRAY_INDEX].size() / 3
	vtris += VortexView.DEBRIS * vx._debris.multimesh.mesh.surface_get_arrays(0)[Mesh.ARRAY_INDEX].size() / 3
	_check(vtris <= 2000, "tornado (2 funnels + debris) <= 2000 tris (%d)" % vtris)
	var sv: ShellView = pool.get_fx("shell")
	_check(ShellView.STYLES.has("inrush"), "shell has the vacuum inrush style")
	for st3 in ShellView.STYLES:
		sv.configure(st3)
		sv.set_shape(1.0, 0.6)
		sv.set_power(0.7)
		sv.advance(0.05)
	var vn: VineView = pool.get_fx("vine")
	vn.setup("lattice", 1, Vector3(1, 0.8, 0.2))
	_check(vn.triangles() > 0 and vn.triangles() <= 1200, "vine lattice tris (%d)" % vn.triangles())
	vn.setup("roots", 1, Vector3.ONE)
	vn.set_path(PackedVector3Array([Vector3.ZERO, Vector3(1, 0, 0), Vector3(2, 0, 0.5)]), 0.08)
	vn.set_state(0.6, 0.4, 0.0)
	var ff: FlameFieldView = pool.get_fx("flame_field")
	ff.setup("field", 1, 4.0, 0.8)
	_check(ff._n <= FlameFieldView.MAX_N, "flame field instances capped (%d)" % ff._n)
	ff.setup("line", 1, 0.5, 0.9, true)
	ff.set_path(PackedVector3Array([Vector3.ZERO, Vector3(3, 0, 0), Vector3(6, 0, 1)]))
	ff.advance(0.05)
	var fbv: FireballView = pool.get_fx("fireball")
	for kd in ["fireball", "comet", "ember"]:
		fbv.setup(kd, 0.3)
		fbv.set_motion(Vector3(10, 1, 0))
		fbv.set_power(1.0)
		fbv.advance(0.05)
	var wb: WindBladeView = pool.get_fx("wind_blade")
	wb.setup("crescent", Vector3.ONE)
	wb.set_motion(Vector3(0, 0, 12))
	wb.setup("wall", Vector3(1.2, 2.0, 1.0))
	var ck: CrackleView = pool.get_fx("crackle")
	ck.setup("ground", 0.5, 1)
	ck.set_target(Vector3.ZERO, PackedVector3Array([Vector3.ZERO, Vector3(1, 0, 0), Vector3(2, 0, 0), Vector3(3, 0, 0)]))
	for i in 10:
		ck.advance(0.05)
	var gd: GroundDecalView = pool.get_fx("ground_decal")
	for st4 in GroundDecalView.STYLE:
		gd.place(Vector3.ZERO, 1.5, st4, 1)
		gd.advance(0.1)
	var sp: SpikesView = pool.get_fx("spikes")
	for st5 in ["stone", "ice", "glass", "metal"]:
		for lay in ["row", "ring", "cluster"]:
			sp.setup(st5, lay, 1, Vector3(1.0, 0.8, 1.0))
			_check(sp._mmi.multimesh.visible_instance_count <= SpikesView.MAX_N, "spikes capped")
	var ch: ChargeFX = pool.get_fx("charge")
	for t in 4:
		ch.set_anchor(Vector3(0, 1, 0), Vector3.ZERO)
		ch.set_charge(t, 0.5, "ice")
		ch.advance(0.016)
	_check(ch.visible, "a T3 charge shows")
	# one-shots: finish and release themselves; particle budget <= 32 per one-shot
	var bu: BurstFX = pool.get_fx("burst")
	_check(BurstFX.PUFFS + BurstFX.SPARKS <= 32, "burst <= 32 particles")
	bu.manual_time = true
	for st6 in BurstFX.STYLES:
		bu.play(Vector3.ZERO, Vector3.UP, 1.0, st6)
	for i in 120:
		bu.advance(0.02)
	_check(not bu.is_playing(), "burst finishes")
	var sh: ShardsFX = pool.get_fx("shards")
	sh.manual_time = true
	sh.play(Vector3(0, 1, 0), Vector3.UP, 1.0, "glass", 7, 0.0)
	sh.advance(0.3)
	var t1: Transform3D = sh._mmi.multimesh.get_instance_transform(0)
	sh.play(Vector3(0, 1, 0), Vector3.UP, 1.0, "glass", 7, 0.0)
	sh.advance(0.3)
	_check(t1.is_equal_approx(sh._mmi.multimesh.get_instance_transform(0)), "shards deterministic per seed")
	_check(sh._n <= ShardsFX.MAX_N, "shards capped")
	for i in 60:
		sh.advance(0.02)
	_check(not sh.is_playing(), "shards finish")
	for k2 in ["ring", "blast", "beam"]:
		var n: Node = pool.get_fx(k2)
		n.manual_time = true
		match k2:
			"ring":
				n.play(Vector3.ZERO, Vector3.UP, 0.2, 2.0, 0.4, Color.WHITE, {"count": 2})
			"blast":
				n.play(Vector3(0, 1, 0), 2.0, 1.0)
			"beam":
				n.play(Vector3.ZERO, Vector3(0, 0, 5), 0.3, "needle")
		for i in 60:
			n.advance(0.02)
		_check(not n.is_playing(), "%s finishes" % k2)
	# lights: at most one per effect, all shadowless
	for k3 in keys:
		var nd: Node = pool.get_fx(k3)
		var lights := nd.find_children("*", "OmniLight3D", true, false)
		_check(lights.size() <= 1, "%s has <= 1 light" % k3)
		for l in lights:
			_check(not (l as OmniLight3D).shadow_enabled, "%s light is shadowless" % k3)
	# caps: kept views never exceed their cap through the plain pool API
	pool.release_all()
	for i in 20:
		pool.get_fx("ring")
	_check(int(pool.get_stats().ring[0]) <= 8, "ring cap 8")
	pool.release_all()
	await process_frame
