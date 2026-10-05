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


# ------------------------------------------------------------------------------ moveset presentation
# BodyViews mapping of MOVESET §15.6 bodies, FxDirector feel (hit-stop table / cap / reduced motion),
# moveset cue events, and the MoveAnimBridge with test-local move defs. Views run in the tree here.

## Records loops; one-shot names are read from FxDirector.sfx_log (no dependency on play()'s
## signature, which the audio stream owns). `enabled = false` keeps AudioDirector.play silent.
class SfxProbe extends AudioDirector:
	var on := {}

	func loop(key: String, _nm: String, on_now: bool, _pos: Vector3, _vol_db: float = 0.0) -> void:
		on[key] = on_now

	func step(_pos: Vector3) -> void:
		pass

	func ui(_nm: String, _vol_db: float = 0.0) -> void:
		pass


## The runner has no live scene tree (tests run in SceneTree._init): views run off-tree, where the
## legacy global_position writes log "not inside tree"; keep the log clean while they run.
func _tree_views(h: SimHarness) -> BodyViews:
	var bv := BodyViews.new()
	bv.bind(h.w)
	Engine.print_error_messages = false
	return bv


func _free_tree_views(bv: BodyViews) -> void:
	Engine.print_error_messages = true
	bv.pool.free()
	bv.free()


func _director(h: SimHarness, bv: BodyViews) -> FxDirector:
	var fx := FxDirector.new()   # stays off-tree: never touches Engine.time_scale in tests
	fx.world = h.w
	fx.views = bv
	var probe := SfxProbe.new()
	probe.enabled = false
	fx.audio = probe
	fx.sfx_log = []
	fx.bind(h.w)
	return fx


func _drop(fx: FxDirector, bv: BodyViews) -> void:
	fx.audio.free()
	fx.free()
	_free_tree_views(bv)


func test_moveset_bodies_map_to_their_views() -> void:
	var h := SimHarness.new(1)
	var bv := _tree_views(h)
	var w := h.w
	var cases: Array = []   # [body, expected class name]
	var add := func(mat: int, form: int, tag: String, want: String, f: Dictionary = {}) -> void:
		var b := w.spawn_body(mat, form, 2.0, Vector3(cases.size() * 1.5 - 12.0, 1.0, 3.0), "test")
		b.tag = StringName(tag)
		for k in f:
			b.set(k, f[k])
		cases.append([b, want])
	add.call(Sim.Mat.METAL, Sim.Form.CHUNK, "disc", "MetalView", {"spin": 40.0})
	add.call(Sim.Mat.METAL, Sim.Form.CHUNK, "lance", "MetalView")
	add.call(Sim.Mat.SAND, Sim.Form.CHUNK, "slug", "CloudView")
	add.call(Sim.Mat.SAND, Sim.Form.WAVE, "sand_surge", "GroundStripView")
	add.call(Sim.Mat.GLASS, Sim.Form.WALL, "glass", "CrystalView")
	add.call(Sim.Mat.GLASS, Sim.Form.SHARD, "needle", "CrystalView")
	add.call(Sim.Mat.STONE, Sim.Form.WALL, "obsidian", "EarthWallView")
	add.call(Sim.Mat.STONE, Sim.Form.WALL, "spikes", "SpikesView")
	add.call(Sim.Mat.STONE, Sim.Form.CHUNK, "glob", "StoneView", {"temp": 1100.0, "liquid": 0.6})
	add.call(Sim.Mat.WATER, Sim.Form.WAVE, "water_wave", "GroundStripView", {"liquid": 1.0})
	add.call(Sim.Mat.WATER, Sim.Form.WALL, "ice", "CrystalView", {"phase": Sim.Phase.FROZEN})
	add.call(Sim.Mat.STEAM, Sim.Form.CLOUD, "", "CloudView")
	add.call(Sim.Mat.PLANT, Sim.Form.WALL, "vine", "VineView")
	add.call(Sim.Mat.FIRE, Sim.Form.CHUNK, "fireball", "FireballView")
	add.call(Sim.Mat.FIRE, Sim.Form.CHUNK, "comet", "FireballView")
	add.call(Sim.Mat.FIRE, Sim.Form.WAVE, "fire_line", "FlameFieldView")
	add.call(Sim.Mat.AIR, Sim.Form.CHUNK, "crescent", "WindBladeView")
	add.call(Sim.Mat.AIR, Sim.Form.CHUNK, "twister", "VortexView", {"spin": 12.0})
	add.call(Sim.Mat.METAL, Sim.Form.WAVE, "spike_line", "SpikesView")
	add.call(Sim.Mat.AIR, Sim.Form.WAVE, "wind_wall", "WindBladeView")
	add.call(Sim.Mat.AIR, Sim.Form.WAVE, "dust_line", "CloudView")
	_check_views(bv, cases)
	cases.clear()
	# zones in a second world (MAX_BODIES = 32 counts zones too)
	var h2 := SimHarness.new(2)
	bv.bind(h2.w)
	w = h2.w
	var zones := [["sand_cloud", "CloudView"], ["fog", "CloudView"], ["quicksand", "GroundDecalView"],
		["fire_field", "FlameFieldView"], ["tornado", "VortexView"], ["null_bubble", "ShellView"],
		["vacuum_well", "ShellView"], ["caltrops", "MetalView"], ["briar", "VineView"], ["corona", "ShellView"],
		["static_field", "ShellView"], ["lava_pool", "LavaWaveView"], ["ice_floor", "GroundDecalView"], ["geyser", "CloudView"],
		["sound_barrier", "ShellView"], ["eddy", "VortexView"], ["steam_screen", "CloudView"], ["mine", "ShellView"],
		["rod", "MetalView"], ["snare", "VineView"], ["inrush", "ShellView"]]
	for z in zones:
		var zb := w.spawn_zone(StringName(z[0]), Vector3(cases.size() * 1.5 - 12.0, 0.0, -3.0), 1.5, -1, 10.0)
		cases.append([zb, z[1]])
	_check_views(bv, cases)
	# a charged body crackles (overlay), and loses it when discharged
	var cs := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 5.0, Vector3(0, 1, 6), "test")
	cs.charge = 20.0
	bv.push_state()
	bv.render(1.0)
	check(int(bv.pool.get_stats().crackle[0]) == 1, "a charged stone crackles")
	cs.charge = 0.0
	bv.render(1.0)
	check(int(bv.pool.get_stats().crackle[0]) == 0, "the crackle goes back to the pool when discharged")
	# a sand wall fusing to glass swaps its view (and the wall view goes back square)
	var sw := w.spawn_body(Sim.Mat.SAND, Sim.Form.WALL, 60.0, Vector3(4, 0, 8), "test")
	bv.push_state()
	check(bv.view_of(sw.id) is EarthWallView, "a sand wall is a (sandstone) earth wall view")
	sw.mat = Sim.Mat.GLASS
	bv.push_state()
	check(bv.view_of(sw.id) is CrystalView, "fused to glass it becomes a crystal wall")
	bv.clear()
	var total := 0
	for k in bv.pool.get_stats():
		total += int(bv.pool.get_stats()[k][0])
	check(total == 0, "clear() returns every view (active %d)" % total)
	_free_tree_views(bv)


## Body tags a kit leaves without a view on purpose (a slick zone lies over its own puddle view).
const VIEWLESS_OK := ["slick", "zone"]


## Every bound move (16 sub-elements x 10 slots, tap and T3) played through the real input path: each body it makes
## maps to a view, the director digests every event it emits, and the one-shot pools stay within their caps.
func test_every_move_maps_its_bodies_and_events() -> void:
	Moves.ensure()
	var unmapped := {}
	var bv: BodyViews = null
	var fx: FxDirector = null
	var runs := 0
	for e in 4:
		for sb in 4:
			for slot in Sim.SLOTS:
				for tier in [0, 3]:
					var h := SimHarness.new(3)
					var p := h.actor("p", Vector3(0, 0, 4.0), 0, {"heat_draw": true, "magma": true, "redirect_current": true}, e)
					h.actor("o", Vector3(0, 0, -3.0), 1, {}, Sim.Element.EARTH)
					if bv == null:
						bv = _tree_views(h)
						fx = _director(h, bv)
						fx.player_id = p.id
					else:
						bv.bind(h.w)
						fx.world = h.w
						fx.bind(h.w)
					var sc := LabScript.for_move(e, sb, String(slot), tier)
					var seen := 0
					for t in sc.length() + 120:
						var ip := h.it(p)
						ip.attack_held = false
						ip.guard_held = false
						ip.tech_held = false
						ip.evade_held = false
						LabScript.apply_dict(ip, sc.next())
						ip.aim_dir = Vector3(0, 0, -1)
						h.step(1)
						var evs: Array[Dictionary] = []
						for k in range(seen, h.log.size()):
							evs.append(h.log[k])
						seen = h.log.size()
						bv.push_state()
						fx.handle(evs)
						if t % 3 == 0:
							bv.render(1.0)
							fx.update_continuous(0.05)
						for b in h.w.bodies:
							if b.alive and b.form != Sim.Form.POOL and bv._kind_for(b) == "none" and not VIEWLESS_OK.has(String(b.tag)):
								unmapped["%s %s [%s] (%s)" % [Sim.MAT_NAMES[b.mat], Sim.FORM_NAMES[b.form], b.tag, Moves.resolve(e, sb, String(slot))]] = true
					runs += 1
	check(runs == 4 * 4 * Sim.SLOTS.size() * 2, "every slot played (%d)" % runs)
	check(unmapped.is_empty(), "every move body has a view: %s" % ", ".join(PackedStringArray(unmapped.keys())))
	var st := bv.pool.get_stats()
	for k in ["ring", "burst", "shards", "blast", "beam"]:
		check(int(st[k][0]) <= int(st[k][2]), "%s stays within its pool cap (%s)" % [k, st[k]])
	check((fx.sfx_log as Array).size() > 100, "the moves play their cues (%d sounds)" % (fx.sfx_log as Array).size())
	_drop(fx, bv)


## Viewless fronts (a tremor, a sound flight field) have no kept node but must still pulse their rings.
func test_viewless_fronts_pulse_rings() -> void:
	var h := SimHarness.new(1)
	var bv := _tree_views(h)
	var tr := h.w.spawn_body(Sim.Mat.AIR, Sim.Form.WAVE, 1.0, Vector3(0, 0, 3), "test")
	tr.tag = &"tremor"
	var ff := h.w.spawn_zone(&"flight_field", Vector3(3, 0, 3), 0.5, -1, 0.0)
	bv.push_state()
	check(bv.view_of(tr.id) == null and bv.view_of(ff.id) == null, "fronts keep no node")
	for i in 6:
		bv.render(1.0)
	check(int(bv.pool.get_stats().ring[0]) >= 2, "tremor + flight field emit rings (%d)" % int(bv.pool.get_stats().ring[0]))
	_free_tree_views(bv)


## A planted lightning rod stands upright on the ground and crackles at its tip, not over its 6 m reach.
func test_planted_rod_stands_and_crackles_at_the_tip() -> void:
	var h := SimHarness.new(1)
	var bv := _tree_views(h)
	var z := h.w.spawn_zone(&"rod", Vector3(1, 0, 2), 6.0, -1, 60.0, Sim.Mat.METAL, 3.0, -1.0)
	z.charge = 20.0
	bv.push_state()
	bv.render(1.0)
	var n := bv.view_of(z.id)
	check(n is MetalView and (n as MetalView).shape == "rod", "a planted rod is a metal rod view")
	if n:
		check(n.position.y > 0.5 and absf(n.basis.y.normalized().dot(Vector3.UP)) > 0.99, "it stands upright above the ground")
	check(int(bv.pool.get_stats().crackle[0]) == 1, "a charged rod crackles")
	_free_tree_views(bv)


func _check_views(bv: BodyViews, cases: Array) -> void:
	bv.push_state()
	bv.render(1.0)
	var seen := {}
	for c in cases:
		var b: MatBody = c[0]
		var n := bv.view_of(b.id)
		var got := "null" if n == null else String(n.get_script().get_global_name())
		check(got == c[1], "%s %s [%s] -> %s (got %s)" % [Sim.MAT_NAMES[b.mat], Sim.FORM_NAMES[b.form], b.tag, c[1], got])
		check(n == null or not seen.has(n), "body %d has its own view" % b.id)
		if n:
			seen[n] = true
			check(n.visible, "%s view is visible" % b.tag)


func test_zones_fade_in_and_out_with_their_life() -> void:
	var h := SimHarness.new(1)
	var bv := _tree_views(h)
	var z := h.w.spawn_zone(&"fog", Vector3(0, 0, 3), 2.0, -1, 5.0, Sim.Mat.WATER, 0.0, 3.0)
	z.age = 0.0
	check(is_zero_approx(bv._life01(z)), "a zone opens from nothing")
	z.age = 1.0
	near(bv._life01(z), 1.0, 1e-4, "fully shown mid-life")
	z.age = 2.9
	check(bv._life01(z) < 0.3, "fading out before max_life (%.2f)" % bv._life01(z))
	_free_tree_views(bv)


func test_thrown_stones_tumble_and_resting_stones_do_not() -> void:
	var h := SimHarness.new(1)
	var bv := _tree_views(h)
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 6.0, Vector3(0, 1.5, 3), "test")
	b.vel = Vector3(10, 0, 0)
	b.on_ground = false
	bv.push_state()
	var n := bv.view_of(b.id)
	var b0 := n.basis
	bv.render(1.0)
	check(not n.basis.is_equal_approx(b0), "a flying stone turns")
	var axis := (n.basis * b0.inverse()).get_rotation_quaternion().get_axis()
	check(absf(axis.dot(Vector3.UP.cross(b.vel).normalized())) > 0.9, "about up x velocity (axis %s)" % axis)
	b.on_ground = true
	var b1 := n.basis
	bv.render(1.0)
	check(n.basis.is_equal_approx(b1), "a resting stone stays put")
	b.tag = &"spear"
	b.on_ground = false
	bv.render(1.0)
	check(n.basis.z.normalized().dot(Vector3.RIGHT) > 0.99 and n.basis.z.length() > 2.0, "a spear flies point first, stretched")
	_free_tree_views(bv)


func test_hitstop_follows_the_table_and_is_capped() -> void:
	var h := SimHarness.new(1)
	var bv := _tree_views(h)
	var fx := _director(h, bv)
	fx.feel("t0", Vector3.ZERO)
	check(fx.hitstop_pending() == 3, "T0 hit: 3 frames (got %d)" % fx.hitstop_pending())
	fx.feel("t3", Vector3.ZERO)
	check(fx.hitstop_pending() == 9, "T3 hit: 9 frames, requests do not stack (got %d)" % fx.hitstop_pending())
	near(fx.time_scale_wanted(), FxDirector.HITSTOP_SCALE, 1e-6, "frozen while pending")
	for i in 9:
		fx.update_continuous(0.016)
	check(fx.hitstop_pending() == 0 and is_equal_approx(fx.time_scale_wanted(), 1.0), "time runs again after 9 frames")
	fx.feel("t3", Vector3.ZERO)
	check(fx.hitstop_pending() == 3, "<= 12 frozen frames per second: only 3 left (got %d)" % fx.hitstop_pending())
	fx._hs_hist.clear()
	fx._hs_frames = 0
	var gs := GameSettings.new()
	gs.reduced_motion = true
	fx.settings = gs
	fx.feel("t3", Vector3.ZERO)
	check(fx.hitstop_pending() <= 3, "reduced motion keeps hit-stop short (got %d)" % fx.hitstop_pending())
	fx._slowmo = 0.2
	fx._hs_frames = 0
	near(fx.time_scale_wanted(), 0.55, 1e-6, "the slow-mo assist still works on its own")
	check(Engine.time_scale == 1.0, "unit tests never change the engine time scale")
	_drop(fx, bv)


func test_camera_feel_runs_in_real_time_and_respects_reduced_motion() -> void:
	var cam := CameraRig.new()
	cam.cam = Camera3D.new()
	cam.add_child(cam.cam)
	cam.shake_at(1.0, Vector3.ZERO)
	var near_s: float = cam.feel_state()[0]
	cam._shake = 0.0
	cam.shake_at(1.0, Vector3(40, 0, 0))
	check(float(cam.feel_state()[0]) < near_s * 0.3, "shake falls off with distance (1 / (1 + d / 8))")
	cam.fov_punch(-3.0, 0.25)
	check(float(cam.feel_state()[2]) < -2.9, "T3 FOV punch")
	cam._feel(0.1)
	check(cam.cam.fov < CameraRig.BASE_FOV and cam.cam.fov > CameraRig.BASE_FOV - 3.0, "eases back (fov %.2f)" % cam.cam.fov)
	cam._feel(0.2)
	near(cam.cam.fov, CameraRig.BASE_FOV, 1e-3, "and is gone after 0.25 s")
	cam.reduced_motion = true
	cam._shake = 0.0
	cam.fov_punch(-3.0)
	cam.zoom_to(Vector3(0, 0, 5))
	cam.shake(1.0)
	check(is_zero_approx(float(cam.feel_state()[2])) and is_zero_approx(float(cam.feel_state()[3])), "reduced motion: no FOV punch, no zoom")
	near(float(cam.feel_state()[0]), CameraRig.REDUCED_SHAKE, 1e-4, "reduced motion: shake x0.3")
	cam.free()


func test_moveset_cue_events_play_pooled_effects_within_caps() -> void:
	var h := SimHarness.new(1)
	var a := h.actor("A", Vector3.ZERO, 0, {}, Sim.Element.FIRE)
	var o := h.actor("O", Vector3(0, 0, 6), 1, {}, Sim.Element.WATER)
	var bv := _tree_views(h)
	var fx := _director(h, bv)
	fx.player_id = a.id
	var evs: Array[Dictionary] = []
	for key in FxEvents.FX:
		for mat in FxEvents.MATS:
			evs.append({"type": "fx", "fx": key, "mat": mat, "shape": "down" if mat == "lightning" else "", "actor": a.id, "tier": 2,
				"pos": Vector3(0, 1, 1), "dir": Vector3(0, 0, 1), "radius": 1.5, "length": 4.0, "power": 20.0, "on": true})
	for oc in FxEvents.OUTCOMES:
		evs.append({"type": "interaction", "threat": "stone", "counter": "wall_ice", "outcome": oc, "band": "full",
			"perfect": oc == "deflect", "pos": Vector3(0, 1, 3), "dir": Vector3(0, 0, -1), "tp": 30.0, "cp": 40.0,
			"threat_actor": o.id, "counter_actor": a.id, "threat_body": -1, "counter_body": -1, "to": "steam"})
	evs.append({"type": "clash", "a": -1, "b": -1, "pos": Vector3(0, 1, 2), "mat": "metal", "power": 30.0})
	evs.append({"type": "zone", "body": -1, "kind": "sand_cloud", "phase": "open", "radius": 2.0, "owner": a.id, "pos": Vector3(2, 0, 2)})
	evs.append({"type": "zone", "body": -1, "kind": "sand_cloud", "phase": "close", "radius": 2.0, "owner": a.id, "pos": Vector3(2, 0, 2)})
	fx.handle(evs)
	var played: Array = fx.sfx_log
	check(played.size() > 20, "the cues play sounds (%d)" % played.size())
	for nm in ["fireball_whoosh", "thunderclap", "counter_success", "clash_solid", "explosion_small"]:
		check(played.has(nm), "plays %s" % nm)
	var st := bv.pool.get_stats()
	for k in ["ring", "burst", "shards", "blast", "beam"]:
		check(int(st[k][0]) <= int(st[k][2]), "%s stays within its pool cap (%s)" % [k, st[k]])
	check(fx.hitstop_pending() > 0, "the cues requested hit-stop")
	# stance aura on / off
	fx.handle([{"type": "fx", "fx": "aura", "mat": "metal", "actor": a.id, "on": true, "pos": Vector3.ZERO}] as Array[Dictionary])
	check(fx.cues._auras.has(a.id), "aura on")
	fx.handle([{"type": "fx", "fx": "aura", "mat": "metal", "actor": a.id, "on": false, "pos": Vector3.ZERO}] as Array[Dictionary])
	check(not fx.cues._auras.has(a.id), "aura off")
	_drop(fx, bv)


func test_charge_and_status_cues_live_while_their_state_holds() -> void:
	var h := SimHarness.new(1)
	var a := h.actor("A", Vector3.ZERO, 0, {}, Sim.Element.EARTH)
	h.actor("O", Vector3(0, 0, 6), 1, {}, Sim.Element.EARTH)
	var bv := _tree_views(h)
	var fx := _director(h, bv)
	h.step(20)
	h.press(a, "attack")
	h.step(40)
	if not check(a.action != null and a.action.phase == ActionInst.P.CHARGE, "setup: charging"):
		_drop(fx, bv)
		return
	fx.handle([{"type": "charge", "actor": a.id, "move": a.action.id, "element": 0, "sub": 0, "tier": 1, "ready": false}] as Array[Dictionary])
	fx.update_continuous(0.016)
	check(int(bv.pool.get_stats().charge[0]) == 1, "the tier telegraph shows while charging")
	h.release(a, "attack")
	h.step(2)
	fx.update_continuous(0.016)
	check(int(bv.pool.get_stats().charge[0]) == 0, "and goes back to the pool on release")
	for s in ["burning", "frozen", "shocked", "rooted", "concealed", "wet", "blinded"]:
		fx.handle([{"type": "status", "actor": a.id, "status": s, "on": true, "t": 2.0, "mag": 1.0}] as Array[Dictionary])
	fx.update_continuous(0.4)
	check(fx.cues._status.size() == 7, "seven status cues live (%d)" % fx.cues._status.size())
	for s in ["burning", "frozen", "shocked", "rooted", "concealed", "wet", "blinded"]:
		fx.handle([{"type": "status", "actor": a.id, "status": s, "on": false, "t": 0.0, "mag": 0.0}] as Array[Dictionary])
	check(fx.cues._status.is_empty(), "and all end")
	for k in ["flame_field", "shell", "crackle", "vine", "cloud"]:
		check(int(bv.pool.get_stats()[k][0]) == 0, "%s returned" % k)
	_drop(fx, bv)


func _bridge_def(anim_active: String = "mv_front_kick") -> Dictionary:
	return {"name": "Bridge test", "element": Sim.Element.EARTH, "sub": 2, "slot": "strike", "verb": "cone",
		"startup": 0.3, "active": 0.1, "recovery": 0.35, "heavy_min": 0.4, "cost": 0.0, "range": 2.0, "angle": 30.0,
		"power": 1.0, "damage": 1.0, "tiers": {"t1": {"range": 3.0}},
		"anim": "mv_palm_thrust", "anim_hold": "earth_hold", "anim_active": anim_active}


func test_bridge_aligns_contact_holds_and_stops() -> void:
	var h := SimHarness.new(1)
	h.begin_scope()
	Moves.register("vfx_bridge_test", _bridge_def())
	Moves.bind(Sim.Element.EARTH, 2, "strike", "vfx_bridge_test")
	var p := h.actor("P", Vector3.ZERO, 0, {}, Sim.Element.EARTH)
	h.actor("O", Vector3(0, 0, 8), 1, {}, Sim.Element.EARTH)
	var fv := FighterView.new()
	fv.setup(1, {})
	var br := MoveAnimBridge.new()
	var fighters := {p.id: fv}
	h.sub(p, 2)
	h.step(30)
	# --- tap: startup clip with its contact on the end of startup
	h.press(p, "attack")
	h.step(1)
	h.release(p, "attack")
	if not check(p.action != null and p.action.id == "vfx_bridge_test", "setup: the test move runs (%s)" % [p.action.id if p.action else "none"]):
		h.end_scope()
		fv.free()
		return
	var clip := fv.resolve_clip("mv_palm_thrust")
	var guard := 0
	while p.action != null and p.action.phase == ActionInst.P.STARTUP and guard < 60:
		br.update(h.w, fighters, Sim.DT)
		fv._animate(p, Sim.DT)
		fv.ap.advance(Sim.DT)
		h.step(1)
		guard += 1
	check(fv._one_shot == clip, "startup plays the def's anim (%s, got %s)" % [clip, fv._one_shot])
	var late := (fv.ap.current_animation_position - MoveAnimBridge.contact_of(clip)) / maxf(fv.ap.speed_scale, 0.01)
	check(late >= -0.02 and late <= Sim.DT + 0.01, "contact lands within a tick of startup end (%.3f s late)" % late)
	br.update(h.w, fighters, Sim.DT)
	check(fv._one_shot == fv.resolve_clip("mv_front_kick"), "active plays anim_active (got %s)" % fv._one_shot)
	while p.action != null and guard < 200:
		br.update(h.w, fighters, Sim.DT)
		fv._animate(p, Sim.DT)
		h.step(1)
		guard += 1
	br.update(h.w, fighters, Sim.DT)
	for i in 10:
		fv._animate(p, Sim.DT)
	check(fv._one_shot_t <= 0.0, "nothing is re-issued after the action ends")
	check(br.state_of(p.id).is_empty(), "bridge state cleared")
	# --- hold: anim_hold loops during CHARGE by refreshing short one-shots
	h.step(20)
	h.press(p, "attack")
	var issued0 := br.issued
	var holds := 0
	for i in 70:
		h.step(1)
		br.update(h.w, fighters, Sim.DT)
		fv._animate(p, Sim.DT)
		if p.action != null and p.action.phase == ActionInst.P.CHARGE:
			holds += 1
			check(fv._one_shot == fv.resolve_clip("earth_hold") and fv._one_shot_t > 0.0, "charging loops anim_hold")
	check(holds > 20, "setup: charged for %d frames" % holds)
	check(br.issued - issued0 >= holds / 8, "short one-shots are refreshed while the charge holds (%d)" % (br.issued - issued0))
	# --- a stun drops it at once
	p.stun = 0.5
	p.stun_kind = "light"
	br.update(h.w, fighters, Sim.DT)
	fv._animate(p, Sim.DT)
	check(fv._one_shot_t <= 0.0 and br.state_of(p.id).is_empty(), "a stun stops the bridge")
	h.end_scope()
	fv.free()


func test_bridge_leaves_legacy_actions_to_fighter_view() -> void:
	var h := SimHarness.new(1)
	var p := h.actor("P", Vector3.ZERO, 0, {}, Sim.Element.EARTH)
	h.actor("O", Vector3(0, 0, 8), 1, {}, Sim.Element.EARTH)
	var fv := FighterView.new()
	fv.setup(1, {})
	var br := MoveAnimBridge.new()
	h.step(20)
	h.press(p, "attack")
	h.step(2)
	br.update(h.w, {p.id: fv}, Sim.DT)
	check(p.action != null and p.action.id == "earth_attack", "setup: legacy attack")
	check(br.issued == 0 and fv._one_shot_t <= 0.0, "legacy earth_attack is FighterView's own")
	fv.free()
