extends TestCase
## Regressions for the presentation layer (FxDirector, BodyViews, FighterView): loops of dead
## bodies, the lightning aim line, mirrored strafe/evade clips, pooled views shared between two
## bodies, settled ridges rebuilt every frame, attack contact timing and the puddle's wet mark.
## Each failed before its fix. Views run off-tree here, so nothing reads a global transform.


class AudioProbe extends AudioDirector:
	var on := {}     # loop key -> last on/off request
	var at := {}     # loop key -> last position

	func loop(key: String, _nm: String, on_now: bool, pos: Vector3, _vol_db: float = 0.0) -> void:
		on[key] = on_now
		at[key] = pos

	func step(_pos: Vector3) -> void:
		pass


class ProbeWave extends LavaWaveView:
	var builds := 0

	func set_path(points: PackedVector3Array, widths: PackedFloat32Array) -> void:
		builds += 1
		super(points, widths)


func _free_views(bv: BodyViews) -> void:
	bv.pool.free()
	bv.free()


func _ridge(h: SimHarness, p: Vector3) -> MatBody:
	# The state a cooled lava wave settles into: a resting chunk that keeps its ridge path.
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, p, "test")
	b.wave_path = PackedVector3Array([p - Vector3(0, 0, 1.5), p])
	b.on_ground = true
	return b


func test_dead_body_loops_fade_out_where_the_body_was() -> void:
	var h := SimHarness.new(1)
	h.actor("P", Vector3.ZERO, 0, {}, Sim.Element.FIRE)
	var fx := FxDirector.new()
	var au := AudioProbe.new()
	fx.world = h.w
	fx.audio = au
	var p := Vector3(3, 0, 4)
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.BLOB, 20.0, p, "vent", 1300.0)
	b.liquid = 1.0
	var key := "b%dlava" % b.id
	fx.update_continuous(Sim.DT)
	check(au.on.get(key, false), "setup: the molten pool's bubbling loop is on")
	h.w.decay_body(b, "remnant_cap")   # trimmed, timed out or merged while still molten
	h.step(1)
	fx.update_continuous(Sim.DT)
	check(au.on.get(key, true) == false, "the dead pool's loop is switched off")
	check((au.at.get(key, Vector3.ZERO) as Vector3).distance_to(p) < 0.01, "and fades out where the pool was")
	au.free()
	fx.free()


func test_bolt_ready_charge_shows_the_aim_line() -> void:
	var h := SimHarness.new(1)
	var c := h.actor("C", Vector3(0, 0, 6), 0, {"lightning": true}, Sim.Element.FIRE)
	h.actor("T", Vector3(0, 0, -2), 1, {}, Sim.Element.EARTH)
	h.step(20)
	h.press(c, "attack")
	h.step(48)   # held past lightning_min
	if not check(c.action != null and c.action.data.get("bolt_ready", false), "setup: the bolt is ready"):
		return
	var bv := BodyViews.new()
	var fx := FxDirector.new()
	fx.world = h.w
	fx.views = bv
	# The first CHARGE frame (always before bolt_ready) acquired the flame.
	fx._charge_fx[c.id] = bv.pool.get_fx("fire_charge")
	fx._charge_visual(c, true, true)
	var n: Node = fx._charge_fx.get(c.id, null)
	check(n is ChargeAimFX, "a ready bolt is telegraphed by the aim line (got %s)" % [n])
	check(int(bv.pool.get_stats().fire_charge[0]) == 0, "the flame goes back to the pool")
	fx._charge_visual(c, false, false)
	check(not fx._charge_fx.has(c.id) and int(bv.pool.get_stats().charge_aim[0]) == 0, "the aim line is released when the charge ends")
	fx.free()
	_free_views(bv)


func test_strafe_and_evade_clips_match_the_side_travelled() -> void:
	# Facing +Z (facing 0) the rig's left is +X (docs/ANIMATION.md); clips are named for the
	# character's own side.
	var fv := FighterView.new()
	fv.setup(1, {})
	for sx in [1.0, -1.0]:
		var want := "l" if sx > 0.0 else "r"
		var a := ActorState.new()
		a.facing = 0.0
		a.grounded = true
		a.lock_target = 2
		a.vel = Vector3(1.1 * sx, 0, 0)
		fv._animate(a, Sim.DT)
		check(fv._cur == "strafe_" + want, "strafe toward x=%+.0f plays strafe_%s (got %s)" % [sx, want, fv._cur])
		var h := SimHarness.new(1)
		var p := h.actor("P", Vector3.ZERO, 0, {}, Sim.Element.EARTH)
		h.actor("O", Vector3(0, 0, 8), 1, {}, Sim.Element.EARTH)
		h.it(p).move = Vector3(sx, 0, 0)
		h.press(p, "evade")
		h.step(1)
		if not check(p.action != null and p.action.id == "evade", "setup: evading"):
			continue
		fv._animate(p, Sim.DT)
		check(fv._cur == "evade_" + want, "evade toward x=%+.0f plays evade_%s (got %s)" % [sx, want, fv._cur])
	var h2 := SimHarness.new(1)
	var q := h2.actor("P", Vector3.ZERO, 0, {}, Sim.Element.EARTH)
	h2.actor("O", Vector3(0, 0, 8), 1, {}, Sim.Element.EARTH)
	h2.it(q).move = Vector3(0, 0, 1)
	h2.press(q, "evade")
	h2.step(1)
	fv._animate(q, Sim.DT)
	check(fv._cur == "evade_fwd", "an evade toward the target still plays evade_fwd (got %s)" % fv._cur)
	fv.free()


func test_live_bodies_never_share_a_pooled_view() -> void:
	var h := SimHarness.new(1)
	var bv := BodyViews.new()
	bv.bind(h.w)
	var ids: Array[int] = []
	for k in 6:   # more settled ridges than the lava_wave pool's initial cap (4)
		ids.append(_ridge(h, Vector3(-8.0 + k * 3.0, 0.0, 4.0)).id)
	bv.push_state()
	var seen := {}
	for id in ids:
		var n := bv.view_of(id)
		check(n != null and not seen.has(n), "ridge %d has its own view node" % id)
		seen[n] = true
	h.w.decay_body(h.w.get_body(ids[0]), "lifetime")
	bv.push_state()
	var st: Array = bv.pool.get_stats().lava_wave
	check(int(st[0]) == 5 and int(st[1]) == 1, "only the dead ridge's node is free (stats %s)" % [st])
	for k in range(1, ids.size()):
		var n := bv.view_of(ids[k])
		check(n != null and n.visible, "ridge %d keeps a visible view" % ids[k])
	_free_views(bv)


func test_settled_ridge_mesh_is_built_once() -> void:
	var h := SimHarness.new(1)
	var bv := BodyViews.new()
	bv.pool.register("lava_wave", func() -> Node: return ProbeWave.new(), 4)
	bv.bind(h.w)
	var b := _ridge(h, Vector3(0, 0, 4))
	bv.push_state()
	var n := bv.view_of(b.id) as ProbeWave
	if not check(n != null, "setup: the ridge has a wave view"):
		_free_views(bv)
		return
	# Off-tree the view's global_position write logs "not inside tree": keep the log clean.
	Engine.print_error_messages = false
	for f in 10:
		bv.render(0.5)
	check(n.builds == 1, "a settled ridge is built once, not every frame (built %d times)" % n.builds)
	b.wave_path.append(Vector3(0, 0, 4.4))
	bv.render(0.5)
	check(n.builds == 2, "a changed ridge path is rebuilt")
	b.form = Sim.Form.WAVE   # a moving wave's front is interpolated every frame
	bv.render(0.3)
	bv.render(0.6)
	Engine.print_error_messages = true
	check(n.builds == 4, "a moving wave still rebuilds each frame (built %d times)" % n.builds)
	_free_views(bv)


func test_attack_contact_lands_at_the_end_of_startup() -> void:
	var fv := FighterView.new()
	fv.setup(1, {})
	for elem in [Sim.Element.EARTH, Sim.Element.WATER, Sim.Element.FIRE, Sim.Element.AIR]:
		var h := SimHarness.new(1)
		var p := h.actor("P", Vector3.ZERO, 0, {"magma": true}, elem)
		h.actor("O", Vector3(0, 0, 8), 1, {}, Sim.Element.EARTH)
		h.step(30)
		h.press(p, "attack")
		h.step(1)
		h.release(p, "attack")
		var clip := ""
		var guard := 0
		while p.action != null and p.action.phase == ActionInst.P.STARTUP and guard < 60:
			fv._animate(p, Sim.DT)
			clip = fv._cur
			fv.ap.advance(Sim.DT)
			h.step(1)
			guard += 1
		if not check(clip != "", "setup: element %d played a startup clip" % elem):
			continue
		# Seconds since the clip crossed its contact frame, at the speed it was playing.
		var late := (fv.ap.current_animation_position - fv._contact(clip)) / maxf(fv.ap.speed_scale, 0.01)
		check(late >= -0.01 and late <= Sim.DT + 0.005, "%s contact lands within a tick of startup end (%.3f s late)" % [clip, late])
	fv.free()


func test_puddle_mark_follows_the_live_radius() -> void:
	var h := SimHarness.new(1)
	var bv := BodyViews.new()
	bv.bind(h.w)
	var b := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 10.0, Vector3(2, 0, 3), "test")
	b.update_radius_puddle()
	bv.push_state()
	var n := bv.view_of(b.id)
	if not check(n != null, "setup: the puddle has a wet mark"):
		_free_views(bv)
		return
	var mark := n.get_node("Mark") as Node3D
	for mass in [40.0, 3.0]:   # water merged in, then drawn off
		b.mass = mass
		b.update_radius_puddle()
		bv.render(1.0)
		near(mark.scale.x * n.scale.x * 0.5, b.radius, 0.02, "the wet mark matches the puddle at mass %.0f" % mass)
	_free_views(bv)
