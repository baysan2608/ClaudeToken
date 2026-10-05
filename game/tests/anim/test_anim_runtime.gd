extends TestCase
## Runtime animation layers: blend space, foot locking/IK on the ArenaMap ground, hit springs,
## secondary-motion detection, clip API (play_one_shot, def-driven moves). Scene tests drive the
## animation lab (tests/anim/anim_lab.gd) deterministically, one sim tick + one frame at a time.

var tree: SceneTree

const LAB := "res://tests/anim/anim_lab.tscn"


func _lab(shot: String, extra: Array = []) -> Node:
	var lab: Node = (load(LAB) as PackedScene).instantiate()
	lab.set("manual", true)
	var a := PackedStringArray(["--shot=" + shot, "--metrics"])
	for e in extra:
		a.append(String(e))
	lab.set("args", a)
	tree.root.add_child(lab)
	return lab


func _run_until(lab: Node, until_t: float) -> void:
	while float(lab.get("t")) < until_t:
		lab.call("step_manual")
		await tree.process_frame


func _view(lab: Node) -> FighterView:
	var subj: ActorState = lab.get("subject")
	return (lab.get("views") as Dictionary)[subj.id]


# ------------------------------------------------------------------ pure logic

func test_blend_weights_cover_every_direction_and_speed() -> void:
	for spd in [0.0, 0.3, 1.0, 1.8, 3.0, 4.5, 5.5]:
		for k in 16:
			var ang := TAU * k / 16.0
			var v := Vector2(sin(ang), cos(ang)) * float(spd)
			var tw := LocomotionBlender.target_weights(v, "stance_fire")
			var sum := 0.0
			for c in tw:
				check(float(tw[c]) >= -1e-6, "no negative weight (%s at %.1f m/s)" % [c, spd])
				sum += float(tw[c])
			near(sum, 1.0, 1e-4, "weights sum to 1 at %.1f m/s, dir %d" % [spd, k])
	var back := LocomotionBlender.target_weights(Vector2(0, -1.0), "idle")
	check(float(back.get("walk_back", 0.0)) > 0.5, "slow backward motion uses the backpedal")
	var fast_back := LocomotionBlender.target_weights(Vector2(0, -5.0), "idle")
	check(float(fast_back.get("walk_back", 0.0)) < 0.01, "a fast run never uses directional gaits")


func test_cadence_matches_ground_speed_through_the_whole_speed_range() -> void:
	# Footprint speed = cycle rate x stride x gait share must equal the ground speed (no skating),
	# including the slow end where the stride shrinks instead of the cadence going slow-motion.
	for spd: float in [0.4, 0.8, 1.4, 1.8, 4.0, 5.5]:
		var lb := LocomotionBlender.new()
		for i in 120:
			lb.update(1.0 / 60.0, Vector2(0, spd), "idle")
		var gw := 0.0
		var stride := 0.0
		var tot := 0.0
		for c in lb.weights:
			tot += float(lb.weights[c])
			if LocomotionBlender.GAITS.has(c):
				gw += float(lb.weights[c])
				stride += float(lb.weights[c]) * float(LocomotionBlender.GAITS[c].stride)
		var foot_speed := lb.cycle_rate * stride / gw * (gw / tot)
		near(foot_speed, spd, spd * 0.12 + 0.03, "footprints travel at %.1f m/s" % spd)
		check(lb.cycle_rate >= 0.5 and lb.cycle_rate <= 2.2, "cadence %.2f Hz stays human at %.1f m/s" % [lb.cycle_rate, spd])


func test_gait_changes_never_restart_the_cycle() -> void:
	var lb := LocomotionBlender.new()
	var last := 0.0
	var spd := 0.0
	for i in 400:
		spd = minf(spd + 0.05, 5.5) if i < 200 else maxf(spd - 0.08, 0.0)
		var dir := Vector2(sin(i * 0.05), cos(i * 0.05)) * spd
		lb.update(1.0 / 60.0, dir, "idle")
		var step := fposmod(lb.phase - last, 1.0)
		check(step < 0.06, "phase advances smoothly (step %.3f at frame %d)" % [step, i])
		last = lb.phase


func test_leg_ik_reproduces_the_clip_when_the_goal_is_the_animated_ankle() -> void:
	var fv := FighterView.new()
	tree.root.add_child(fv)
	fv.setup(1, {})
	await tree.process_frame
	if not check(fv.skel != null, "setup: the fighter has a skeleton"):
		fv.free()
		return
	var sk := fv.skel
	var th := sk.find_bone("thigh.L")
	var sh := sk.find_bone("shin.L")
	var ft := sk.find_bone("foot.L")
	var before := sk.get_bone_global_pose(ft)
	var knee := sk.get_bone_global_pose(sh).origin
	LegIK.solve(sk, th, sh, ft, before.origin, before.basis, 1.0)
	check(sk.get_bone_global_pose(ft).origin.distance_to(before.origin) < 1e-4, "ankle unchanged")
	check(sk.get_bone_global_pose(sh).origin.distance_to(knee) < 1e-4, "knee unchanged")
	# A goal 12 cm higher is reached (the knee bends further).
	var goal := before.origin + Vector3(0, 0.12, 0.05)
	LegIK.solve(sk, th, sh, ft, goal, before.basis, 1.0)
	check(sk.get_bone_global_pose(ft).origin.distance_to(goal) < 0.01, "a reachable goal is reached")
	fv.free()


func test_hit_springs_scale_with_damage_and_ring_out() -> void:
	var peaks := []
	for s: float in [0.4, 1.0]:
		var h := HitReactor.new()
		h.hit(Vector3(0, 0, -1), s)
		var peak := 0.0
		for i in 60:
			h.step(1.0 / 60.0)
			peak = maxf(peak, h.torso.length())
		peaks.append(peak)
		for i in 60:
			h.step(1.0 / 60.0)
		check(h.torso.length() < 0.02 and h.head.length() < 0.02, "springs settle within 2 s (strength %.1f)" % s)
	check(float(peaks[1]) > float(peaks[0]) * 1.8, "a heavier blow tips the torso further (%.2f vs %.2f rad)" % [peaks[1], peaks[0]])
	# A blow from the front tips the chest backward (-Z in the skeleton frame).
	var h2 := HitReactor.new()
	h2.hit(Vector3(0, 0, -1), 1.0)
	for i in 8:
		h2.step(1.0 / 60.0)
	var up := HitReactor.quat(h2.torso) * Vector3.UP
	check(up.z < -0.05, "the chest snaps back from a frontal hit (up.z %.2f)" % up.z)


func test_secondary_chains_are_found_by_name_and_absent_rigs_are_a_no_op() -> void:
	var fv := FighterView.new()
	fv.setup(1, {})
	if not check(fv.skel != null, "setup: the fighter has a skeleton"):
		fv.free()
		return
	var chains := FighterSecondaryMotion.find_chains(fv.skel)
	var kinds := {}
	for c in chains:
		kinds[c[2]] = int(kinds.get(c[2], 0)) + 1
	check(int(kinds.get("sash", 0)) == 2, "both sash tails become spring chains (%s)" % [chains])
	check(int(kinds.get("hair", 0)) >= 1, "the hair knot becomes a spring chain")
	check(fv.secondary != null, "the fighter gets a SpringBoneSimulator3D")
	var bare := Skeleton3D.new()
	bare.add_bone("hips")
	bare.add_bone("spine")
	bare.set_bone_parent(1, 0)
	check(FighterSecondaryMotion.attach(bare) == null, "a rig without sash/hair bones gets nothing")
	bare.free()
	fv.free()


func test_one_shots_accept_any_library_clip_and_fall_back_gracefully() -> void:
	var fv := FighterView.new()
	fv.setup(1, {})
	var a := ActorState.new()
	a.grounded = true
	fv._animate(a, Sim.DT)
	var before := fv._cur
	for c in fv.clips:
		if String(c).begins_with("mv_"):
			check(fv.play_one_shot(c), "%s plays" % c)
			fv._animate(a, Sim.DT)
			check(fv._cur == c, "%s is the playing clip (got %s)" % [c, fv._cur])
	# Natural length when dur <= 0, contact alignment when asked.
	check(fv.play_one_shot("fire_jab", 0.0), "fire_jab plays")
	near(fv._one_shot_t, fv._clip_len("fire_jab"), 1e-4, "dur 0 uses the clip's length")
	check(fv.play_one_shot("fire_jab", -1.0, fv._contact("fire_jab") * 2.0), "aligned one-shot plays")
	near(fv._one_shot_speed, 0.5, 1e-3, "contact lands at the requested time (half speed)")
	# Stand-ins and unknown clips.
	check(fv.resolve_clip("mv_not_a_clip_yet") == "", "an unknown clip resolves to nothing")
	fv._one_shot_t = 0.0
	fv._animate(a, Sim.DT)
	before = fv._cur
	check(not fv.play_one_shot("no_such_clip"), "an unknown clip is refused")
	fv._animate(a, Sim.DT)
	check(fv._cur == before, "and the current animation keeps playing (%s)" % fv._cur)
	if fv.ap:
		var lib_name := fv.ap.get_animation_library_list()[0]
		var lib := fv.ap.get_animation_library(lib_name)
		if lib.has_animation("mv_stomp"):
			lib.rename_animation("mv_stomp", "mv_stomp_hidden")
			check(fv.resolve_clip("mv_stomp") == "earth_wall", "an older asset without mv_stomp plays its stand-in")
			check(fv.play_one_shot("mv_stomp"), "the stand-in one-shot plays")
			lib.rename_animation("mv_stomp_hidden", "mv_stomp")
	fv.free()


func test_moves_without_a_view_branch_animate_from_their_def() -> void:
	var fv := FighterView.new()
	fv.setup(1, {})
	var a := ActorState.new()
	a.grounded = true
	var inst := ActionInst.new()
	inst.id = "stone_stomp_test"
	inst.def = {"startup": 0.3, "anim": "mv_stomp", "anim_active": "earth_throw"}
	inst.attack_id = 7
	a.action = inst
	var clip := ""
	while inst.total < 0.3 - 1e-6:
		fv._animate(a, Sim.DT)
		clip = fv._cur
		if fv.ap:
			fv.ap.advance(Sim.DT)
		inst.total += Sim.DT
	check(clip == "mv_stomp", "startup plays the def's anim (got %s)" % clip)
	if fv.ap:
		var late := (fv.ap.current_animation_position - fv._contact("mv_stomp")) / maxf(fv.ap.speed_scale, 0.01)
		check(absf(late) <= Sim.DT + 0.005, "its contact lands at the end of startup (%.3f s)" % late)
	inst.phase = ActionInst.P.ACTIVE
	fv._animate(a, Sim.DT)
	check(fv._cur == "earth_throw", "the active phase plays anim_active (got %s)" % fv._cur)
	inst.def = {"startup": 0.2}
	inst.id = "no_clip_move"
	inst.phase = ActionInst.P.STARTUP
	var keep := fv._cur
	fv._animate(a, Sim.DT)
	check(fv._cur == keep, "a def without clips leaves the current clip playing")
	fv.free()


# ------------------------------------------------------------------ in the scene tree

func test_planted_feet_do_not_skate_in_walks_runs_and_strafes() -> void:
	var lab := _lab("loco")
	await _run_until(lab, 5.2)
	var m: Dictionary = lab.call("metric_table")
	lab.queue_free()
	for seg in ["walk_slow", "walk", "run"]:
		if check(m.has(seg), "measured %s" % seg):
			check(float(m[seg].skate_cms) < 4.0, "%s: planted feet slide %.1f cm/s (< 4)" % [seg, m[seg].skate_cms])
	var lab2 := _lab("strafe")
	await _run_until(lab2, 9.5)
	var m2: Dictionary = lab2.call("metric_table")
	lab2.queue_free()
	for seg in ["strafe_a", "strafe_b", "back", "in"]:
		if check(m2.has(seg), "measured %s" % seg):
			check(float(m2[seg].skate_cms) < 15.0, "%s: planted feet slide %.1f cm/s (< 15)" % [seg, m2[seg].skate_cms])
	await tree.process_frame


func test_foot_locking_is_what_removes_the_skating() -> void:
	var lab := _lab("loco", ["--off=lock"])
	await _run_until(lab, 5.2)
	var m: Dictionary = lab.call("metric_table")
	lab.queue_free()
	check(float(m.run.skate_cms) > 15.0, "without locking the run skates (%.1f cm/s): the metric is live" % m.run.skate_cms)
	await tree.process_frame


func test_feet_follow_steps_and_ledges_with_the_pelvis_lowered() -> void:
	var lab := _lab("ledge")
	# On the step block (0.35 m) facing +X at its z = 9 edge: the left foot is over the floor.
	await _run_until(lab, 6.3)
	var fv := _view(lab)
	var rig := fv.rig
	var subj: ActorState = lab.get("subject")
	near(subj.pos.y, 0.35, 0.01, "setup: standing on the step block")
	var l: Vector3 = rig.foot_world[0]
	var r: Vector3 = rig.foot_world[1]
	check(l.z < 9.0 and r.z > 9.0, "setup: the left foot is past the edge (L z %.2f, R z %.2f)" % [l.z, r.z])
	near(l.y, FighterAnimRig.ANKLE_REST_H, 0.05, "the left foot is planted on the floor below")
	near(r.y, 0.35 + FighterAnimRig.ANKLE_REST_H, 0.04, "the right foot stays on the step")
	check(rig.pelvis_shift < -0.2, "the pelvis comes down to let the leg reach (%.2f m)" % rig.pelvis_shift)
	check(rig.hip_tilt < -0.05, "the hips tilt toward the lower foot (%.2f rad)" % rig.hip_tilt)
	# Terrace (0.6 m): too deep to reach - the hanging foot keeps the edge's height.
	await _run_until(lab, 10.8)
	l = rig.foot_world[0]
	near(subj.pos.y, 0.6, 0.01, "setup: on the terrace")
	check(l.y > 0.6 + FighterAnimRig.ANKLE_REST_H - 0.06, "no reaching into a drop deeper than a step (L y %.2f)" % l.y)
	# Pool edge (floor -0.3 m): the outer foot goes down to the pool floor.
	await _run_until(lab, 17.5)
	l = rig.foot_world[0]
	check(l.z < 3.0, "setup: the left foot is over the pool (z %.2f)" % l.z)
	near(l.y, -0.3 + FighterAnimRig.ANKLE_REST_H, 0.06, "the foot stands on the pool floor")
	lab.queue_free()
	await tree.process_frame


func test_blend_space_and_player_stay_in_step() -> void:
	# While the blend owns the body, the AnimationPlayer's own gait clip runs on the blend's phase,
	# so the cross-fade into an action starts from the same step.
	var lab := _lab("loco")
	await _run_until(lab, 4.6)
	var fv := _view(lab)
	var c := fv.ap.current_animation
	check(c == "run", "setup: running (got %s)" % c)
	var anim := fv.ap.get_animation(c)
	var want := fv.rig.loco.clip_time(c, anim.length)
	var drift := absf(wrapf(fv.ap.current_animation_position - want, -anim.length * 0.5, anim.length * 0.5))
	check(drift < 0.05, "player clip within 0.05 s of the blend phase (%.3f)" % drift)
	lab.queue_free()
	await tree.process_frame


func test_hits_kick_the_springs_in_the_game_path() -> void:
	var lab := _lab("hits")
	await _run_until(lab, 0.9)
	var rig := _view(lab).rig
	check(rig.hits.torso.length() < 1e-3, "calm before the first blow")
	var light := await _peak_torso(lab, rig, 1.0, 1.4)
	var medium := await _peak_torso(lab, rig, 2.5, 2.9)
	check(light > 0.05, "a light hit tips the torso (%.2f rad)" % light)
	check(medium > light, "a heavier hit tips it further (%.2f vs %.2f)" % [medium, light])
	lab.queue_free()
	await tree.process_frame


func _peak_torso(lab: Node, rig: FighterAnimRig, from: float, to: float) -> float:
	await _run_until(lab, from)
	var peak := 0.0
	while float(lab.get("t")) < to:
		lab.call("step_manual")
		await tree.process_frame
		peak = maxf(peak, rig.hits.torso.length())
	return peak


func test_six_fighters_stay_cheap() -> void:
	# Budget guard (desktop CPU, so generous): the full rig of 6 fighters per frame.
	var lab := _lab("crowd")
	await _run_until(lab, 0.5)
	var t0 := Time.get_ticks_usec()
	var n := 0
	while float(lab.get("t")) < 2.5:
		lab.call("step_manual")
		await tree.process_frame
		n += 1
	var per_frame := float(Time.get_ticks_usec() - t0) / n / 1000.0
	note("6 fighters, sim + views + rigs: %.2f ms per frame (headless)" % per_frame)
	check(per_frame < 25.0, "6 fighters cost %.2f ms per frame (< 25, gross-regression guard)" % per_frame)
	lab.queue_free()
	await tree.process_frame
