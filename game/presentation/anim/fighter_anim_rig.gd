class_name FighterAnimRig
extends SkeletonModifier3D
## Procedural animation layers for one fighter, run by the Skeleton3D after the AnimationPlayer
## has posed it (a SkeletonModifier3D: the changes are re-applied every frame, never saved).
## Order inside one pass:
##   1. synced locomotion blend space (full body, or legs only under guard/charge walking)
##   2. snapshot of the animated ankles/feet (the IK goal: the clip's own footprints)
##   3. additive layers: pelvis shift + landing compression, acceleration lean and turn banking,
##      spring-damper hit/block reactions, chest aim and head look-at (clamped, smoothed)
##   4. foot IK (LegIK): planted feet onto the analytic ArenaMap ground, soles kept flat
## FighterView feeds the inputs each frame from the sim state and calls tick(dt); nothing here
## moves the fighter (the simulation stays the only movement authority).

const LEG_BONES := ["thigh.L", "shin.L", "foot.L", "toe.L", "thigh.R", "shin.R", "foot.R", "toe.R"]
const ANKLE_REST_H := 0.085          # ankle height of a flat planted foot (docs/ANIMATION.md)
const PLANT_FROM := 0.115            # animated ankle height where a foot stops counting as planted
const PLANT_TO := 0.19
const MAX_DROP := 0.38               # never reach further down than a step (deeper = ledge edge)
const MAX_LIFT := 0.45
const PELVIS_SHARE := 0.5            # share of a one-foot step-down taken by the pelvis (rest: leg reach)
const REACH_FRAC := 0.96             # leg length used for reach (a planted leg never locks fully straight)
const LOOK_YAW_MAX := 1.05           # rad (about 60 deg) beyond the animated head direction
const LOOK_PITCH_MAX := 0.45
const PROBES := ["head", "hand.L", "hand.R", "shin.L", "shin.R", "foot.L", "foot.R", "toe.L", "toe.R", "hips"]
const LOCK_MAX_DIST := 0.2           # m a locked foot may lag the animated one before it lets go
const LOCK_MAX_YAW := 0.7            # rad of body turn a locked foot tolerates
const LOCK_MAX_FOOT_SPEED := 0.7    # m/s: an animated foot moving faster than this is not set down
const LOCK_RELEASE := 0.22           # s to ease a released foot back onto the animation

# ---- inputs (set by FighterView every frame) ----
var loco_target := 0.0               # 1 = locomotion owns the whole body
var legs_target := 0.0               # >0 = gait legs under an upper-body action (guard walk)
var local_vel := Vector2.ZERO        # x = toward the character's right, y = forward (m/s)
var ground_speed := 0.0              # actual horizontal speed of the body (m/s, no turn-step input)
var stance := "idle"
var lean_target := Vector2.ZERO      # x = pitch (+ forward), y = roll (+ toward the character's right)
var look_world := Vector3.ZERO
var look_target_w := 0.0
var aim_yaw := 0.0                   # rad, + turns the chest toward the character's left
var aim_target_w := 0.0
var ik_target_w := 0.0
var arena: ArenaMap = null
var lod := 0                         # 0 full, 1 reduced (no look-at, 2-clip blend)

# ---- state ----
var loco := LocomotionBlender.new()
var hits := HitReactor.new()
var sampler := AnimClipSampler.new()
var anims := {}                      # clip -> Animation
var loco_w := 0.0
var legs_w := 0.0
var ik_w := 0.0
var look_w := 0.0
var aim_w := 0.0
var lean := Vector2.ZERO
var land_y := 0.0
var land_v := 0.0
var pelvis_shift := 0.0
var foot_off := [0.0, 0.0]
var ground_w := [NAN, NAN]           # smoothed world height of the ground under each foot
var hip_tilt := 0.0
var lock_target_w := 0.0             # input: 1 = planted feet may lock to the ground
var locked := [false, false]
var lock_w := [0.0, 0.0]
var lock_pos := [Vector3.ZERO, Vector3.ZERO]
var lock_yaw := [0.0, 0.0]
var relock_wait := [false, false]
var catch_lift := [0.0, 0.0]
var _last_origin := Vector3.ZERO
var _prev_anim_w := [Vector3.ZERO, Vector3.ZERO]
var model_lift := 0.0                # input: visual height smoothing offset of the model (m)
var foot_world := [Vector3.ZERO, Vector3.ZERO]   # [left, right] ankles after all layers
var _leg_len := [0.82, 0.82]
var probe_world := PackedVector3Array()   # world positions of PROBES (AnimRigSettings.record_probes)
var look_yaw := 0.0
var look_pitch := 0.0
var hand_world := [Vector3.ZERO, Vector3.ZERO]   # [right, left] after all layers
var hand_frame := -1
var dbg_points := PackedVector3Array()
var last_reach_error := 0.0
var _dt := 1.0 / 60.0
var _legs_mask := PackedByteArray()
var _b := {}
var _head_fwd_local := Vector3(0, 0, 1)
var _ready_ok := false


func setup_rig(sk: Skeleton3D, ap: AnimationPlayer) -> void:
	for n in ["root", "hips", "spine", "chest", "neck", "head", "shoulder.L", "shoulder.R", "upper_arm.L",
			"upper_arm.R", "hand.L", "hand.R"] + LEG_BONES:
		_b[n] = sk.find_bone(n)
	_ready_ok = _b.hips >= 0 and _b["thigh.L"] >= 0 and _b["shin.L"] >= 0 and _b["foot.L"] >= 0 \
		and _b["thigh.R"] >= 0 and _b["shin.R"] >= 0 and _b["foot.R"] >= 0
	if _ready_ok:
		for i in 2:
			var s := "L" if i == 0 else "R"
			var a := sk.get_bone_global_rest(_b["thigh." + s]).origin
			var b := sk.get_bone_global_rest(_b["shin." + s]).origin
			var c := sk.get_bone_global_rest(_b["foot." + s]).origin
			_leg_len[i] = (b - a).length() + (c - b).length()
	sampler.resize(sk.get_bone_count())
	_legs_mask.resize(sk.get_bone_count())
	_legs_mask.fill(0)
	for n in LEG_BONES:
		if _b[n] >= 0:
			_legs_mask[_b[n]] = 1
	if _b.head >= 0:
		var rb := sk.get_bone_global_rest(_b.head).basis
		_head_fwd_local = (rb.inverse() * Vector3(0, 0, 1)).normalized()
	if ap:
		for c in LocomotionBlender.GAITS.keys() + ["idle", "stance_earth", "stance_water", "stance_fire", "stance_air"]:
			if ap.has_animation(c):
				anims[c] = ap.get_animation(c)


func is_rig_ready() -> bool:
	return _ready_ok


## Landing / heavy compression: v is the downward pelvis speed kick (m/s).
func kick_pelvis(v: float) -> void:
	land_v -= clampf(v, 0.0, 3.2)


## Advances every time-dependent part (blend weights, springs, smoothing). Called once per frame.
func tick(dt: float) -> void:
	_dt = clampf(dt, 0.0, 0.1)
	var k8 := 1.0 - exp(-8.0 * _dt)
	var k12 := 1.0 - exp(-12.0 * _dt)
	loco.update(_dt, local_vel, stance)
	var lt := loco_target if AnimRigSettings.loco_blend else 0.0
	loco_w = move_toward(loco_w, lt, _dt / (0.16 if lt > loco_w else 0.1))
	legs_w += ((legs_target if AnimRigSettings.loco_blend else 0.0) - legs_w) * k8
	ik_w = move_toward(ik_w, ik_target_w if AnimRigSettings.foot_ik else 0.0, _dt / (0.08 if ik_target_w > ik_w else 0.12))
	var lw := look_target_w if (AnimRigSettings.look_at and lod == 0) else 0.0
	look_w += (lw - look_w) * (1.0 - exp(-5.0 * _dt))
	aim_w += ((aim_target_w if AnimRigSettings.look_at else 0.0) - aim_w) * k12
	var lt2 := lean_target if AnimRigSettings.lean else Vector2.ZERO
	lean = lean.lerp(lt2, 1.0 - exp(-6.0 * _dt))
	# Landing compression: under-damped pelvis spring (2.4 Hz, zeta 0.55).
	var w := TAU * 2.4
	var h := _dt
	while h > 1e-6:
		var s := minf(h, 1.0 / 120.0)
		h -= s
		land_v += (-land_y * w * w - land_v * 2.0 * 0.55 * w) * s
		land_y = clampf(land_y + land_v * s, -0.22, 0.05)
	if not AnimRigSettings.lean:
		land_y = 0.0
		land_v = 0.0
	if AnimRigSettings.hit_springs:
		hits.step(_dt)
	else:
		hits.reset()


func _process_modification_with_delta(_delta: float) -> void:
	var sk := get_skeleton()
	if sk == null or not _ready_ok:
		return
	# 1. locomotion blend space
	if loco_w > 0.002 or legs_w > 0.01:
		var ents := loco.entries(anims, 6 if lod == 0 else 2)
		if not ents.is_empty():
			if loco_w > 0.002:
				sampler.blend(ents, sk)
				sampler.apply(sk, loco_w)
			if legs_w > 0.01 and loco_w < 0.99:
				sampler.blend(ents, sk, _legs_mask)
				sampler.apply(sk, legs_w * (1.0 - loco_w))
	# 2. animated foot goals (skeleton space), before any additive layer moves the hips
	var xf := sk.global_transform
	var ank: Array[Vector3] = [sk.get_bone_global_pose(_b["foot.L"]).origin, sk.get_bone_global_pose(_b["foot.R"]).origin]
	var fbas: Array[Basis] = [sk.get_bone_global_pose(_b["foot.L"]).basis, sk.get_bone_global_pose(_b["foot.R"]).basis]
	var use_ik := ik_w > 0.002 and arena != null
	var goal: Array[Vector3] = [ank[0], ank[1]]
	var gbas: Array[Basis] = [fbas[0], fbas[1]]
	if use_ik:
		var ball: Array[Vector3] = [ank[0], ank[1]]
		for i in 2:
			var toe: int = _b["toe.L" if i == 0 else "toe.R"]
			if toe >= 0:
				ball[i] = sk.get_bone_global_pose(toe).origin
		# Lock first (horizontal), then find the ground where the foot will actually stand: a locked
		# foot can be up to LOCK_MAX_DIST from the animated one, e.g. still on a rim the body has left.
		_lock_feet(xf, ank, ball, goal, fbas, gbas)
		_ground_goals(sk, xf, ank, goal)
		# Pelvis: both feet lower (the body still settling after a drop) -> all the way down; one foot
		# on lower ground -> part of the way (the lower leg straightens, the upper one bends, rather
		# than the whole body squatting), then further only if that goal is out of reach.
		var lo := minf(float(foot_off[0]), float(foot_off[1]))
		var hi := minf(maxf(float(foot_off[0]), float(foot_off[1])), 0.0)
		var shift_t := lo if lo >= 0.0 else hi + (lo - hi) * PELVIS_SHARE
		shift_t = clampf(shift_t, -MAX_DROP, 0.4)
		shift_t -= _reach_drop(sk, goal, shift_t)     # extra drop beyond shift_t
		shift_t = maxf(shift_t, -MAX_DROP)
		pelvis_shift = lerpf(pelvis_shift, shift_t, 1.0 - exp(-12.0 * _dt))
		# Hip tilt toward the lower foot (the pelvis follows uneven ground), torso counter-tilts.
		var tilt_t := clampf((float(foot_off[0]) - float(foot_off[1])) * 1.1, -0.2, 0.2)
		hip_tilt = lerpf(hip_tilt, tilt_t, 1.0 - exp(-10.0 * _dt))
	else:
		pelvis_shift = lerpf(pelvis_shift, 0.0, 1.0 - exp(-10.0 * _dt))
		hip_tilt = lerpf(hip_tilt, 0.0, 1.0 - exp(-10.0 * _dt))
		foot_off[0] = 0.0
		foot_off[1] = 0.0
		ground_w[0] = NAN
		ground_w[1] = NAN
		_unlock(0)
		_unlock(1)
		lock_w[0] = 0.0
		lock_w[1] = 0.0
	# 3. additive layers
	var hips: int = _b.hips
	var dy := pelvis_shift * ik_w + land_y
	if absf(dy) > 1e-5:
		var p := sk.get_bone_parent(hips)
		var pb := sk.get_bone_global_pose(p).basis if p >= 0 else Basis.IDENTITY
		sk.set_bone_pose_position(hips, sk.get_bone_pose_position(hips) + pb.inverse() * Vector3(0, dy, 0))
	var pitch := lean.x - land_y * 0.9        # landing folds the chest forward a little
	var roll := lean.y
	var t := hits.torso if AnimRigSettings.hit_springs else Vector3.ZERO
	var ax_pitch := Vector3.RIGHT              # +X tips the top toward +Z (forward)
	var ax_roll := Vector3(0, 0, 1)            # +Z tips the top toward -X (the character's right)
	# hips take part of the lean only when the feet are pinned by IK
	var hip_share := 0.35 * ik_w
	var tilt := hip_tilt * ik_w
	if hip_share > 0.001 or absf(tilt) > 1e-4:
		LegIK.rotate_global(sk, hips, Quaternion(ax_pitch, pitch * hip_share) * Quaternion(ax_roll, roll * hip_share + tilt) * HitReactor.quat(t, 0.25 * ik_w))
	var rest := 1.0 - hip_share
	var torso_rest := 1.0 - 0.25 * ik_w
	var spine_q := Quaternion(Vector3.UP, aim_yaw * aim_w * 0.4) * Quaternion(ax_pitch, pitch * rest * 0.5) \
		* Quaternion(ax_roll, roll * rest * 0.5 - tilt) * HitReactor.quat(t, torso_rest * 0.45)
	LegIK.rotate_global(sk, _b.spine, spine_q)
	var chest_q := Quaternion(Vector3.UP, aim_yaw * aim_w * 0.6) * Quaternion(ax_pitch, pitch * rest * 0.5) \
		* Quaternion(ax_roll, roll * rest * 0.5) * HitReactor.quat(t, torso_rest * 0.55)
	LegIK.rotate_global(sk, _b.chest, chest_q)
	if AnimRigSettings.hit_springs:
		LegIK.rotate_global(sk, _b["upper_arm.L"], HitReactor.quat(hits.arm_l))
		LegIK.rotate_global(sk, _b["upper_arm.R"], HitReactor.quat(hits.arm_r))
	# head: keep it a little more level than the banked torso, then spring + look-at
	LegIK.rotate_global(sk, _b.neck, Quaternion(ax_roll, -roll * 0.25) * HitReactor.quat(hits.head, 0.4))
	LegIK.rotate_global(sk, _b.head, Quaternion(ax_roll, -roll * 0.25) * HitReactor.quat(hits.head, 0.6))
	_apply_look(sk, xf)
	# 4. foot IK
	dbg_points.clear()
	last_reach_error = 0.0
	if use_ik:
		for i in 2:
			var s := "L" if i == 0 else "R"
			last_reach_error = maxf(last_reach_error, LegIK.solve(sk, _b["thigh." + s], _b["shin." + s], _b["foot." + s], goal[i], gbas[i], ik_w))
			if AnimRigSettings.debug_draw:
				dbg_points.append(xf * goal[i])
	# outputs: hands for VFX anchoring, feet for tests and the lab
	if _b["hand.R"] >= 0:
		hand_world[0] = xf * sk.get_bone_global_pose(_b["hand.R"]).origin
	if _b["hand.L"] >= 0:
		hand_world[1] = xf * sk.get_bone_global_pose(_b["hand.L"]).origin
	foot_world[0] = xf * sk.get_bone_global_pose(_b["foot.L"]).origin
	foot_world[1] = xf * sk.get_bone_global_pose(_b["foot.R"]).origin
	hand_frame = Engine.get_process_frames()
	if AnimRigSettings.record_probes:
		probe_world.resize(PROBES.size())
		for i in PROBES.size():
			var b: int = _b.get(PROBES[i], -1)
			probe_world[i] = xf * sk.get_bone_global_pose(b).origin if b >= 0 else xf.origin


## Ground offsets of the two feet from the analytic ArenaMap (heel or ball, whichever is higher,
## sampled where the locked or animated foot stands), smoothed in world space into foot_off and
## added to goal[i]. Surfaces higher than a step above the fighter's floor are walls (never stood on); deeper than MAX_DROP is a ledge edge (the foot
## keeps the fighter's floor height rather than reaching into the void).
func _ground_goals(sk: Skeleton3D, xf: Transform3D, ank: Array[Vector3], goal: Array[Vector3]) -> void:
	var floor_y := xf.origin.y - model_lift
	var k := 1.0 - exp(-22.0 * _dt)
	for i in 2:
		var toe: int = _b["toe.L" if i == 0 else "toe.R"]
		var hshift := xf.basis * (goal[i] - ank[i])
		hshift.y = 0.0
		var aw := xf * ank[i] + hshift
		var bw := xf * (sk.get_bone_global_pose(toe).origin if toe >= 0 else ank[i]) + hshift
		var hw := aw + (aw - bw).normalized() * 0.09     # the heel, behind the ankle
		var top := floor_y + Sim.STEP_HEIGHT + 0.02
		var g := maxf(arena.ground_height(hw.x, hw.z, top, 0.0), arena.ground_height(bw.x, bw.z, top, 0.0))
		var off := g - xf.origin.y
		if g - floor_y < -MAX_DROP:
			off = floor_y - xf.origin.y    # past a ledge edge: stay level with the edge
		# Smoothed in world space: the skeleton origin itself moves (visual height smoothing after
		# the sim snaps onto a step or into the pool), and a planted foot must not ride along with it.
		var gy := xf.origin.y + off
		if is_nan(float(ground_w[i])) or absf(gy - float(ground_w[i])) > 1.2:
			ground_w[i] = gy
		else:
			ground_w[i] = lerpf(float(ground_w[i]), gy, k)
		off = clampf(float(ground_w[i]) - xf.origin.y, -MAX_DROP - 0.1, MAX_LIFT)
		var plant := 1.0 - smoothstep(PLANT_FROM, PLANT_TO, ank[i].y)
		foot_off[i] = off if off > 0.0 else off * plant
		goal[i].y += float(foot_off[i])


## Foot locking: a planted foot keeps its world position and heading until it lifts (or the body has
## moved too far from it), then eases back onto the animated foot. Removes the residual slide of
## speed mismatches, blends, accelerations and turns.
func _lock_feet(xf: Transform3D, ank: Array[Vector3], ball: Array[Vector3], goal: Array[Vector3], fbas: Array[Basis], gbas: Array[Basis]) -> void:
	var inv := xf.affine_inverse()
	# A teleport (respawn, reset) or a long hitch: nothing stays locked.
	var jumped := (xf.origin - _last_origin).length() > 0.6
	_last_origin = xf.origin
	for i in 2:
		var plant := 1.0 - smoothstep(PLANT_FROM, PLANT_TO, ank[i].y)
		# The ball of the foot is what stays put (the heel peels off around it).
		var anim_w := xf * ball[i]
		# World speed of the animated foot: ~0 for a stance foot of a speed-matched gait, about twice
		# the body speed for a swinging one (robust where blending flattens the foot lift).
		var pv: Vector3 = _prev_anim_w[i]
		var vel := Vector2(anim_w.x - pv.x, anim_w.z - pv.z).length() / maxf(_dt, 1e-4)
		_prev_anim_w[i] = anim_w
		# With the body not translating (turning on the spot), the world motion of a foot the clip
		# has planted is the body's yaw (and the turn-stepping gait): it locks and pivots, then steps
		# round when the clip lifts it or the turn exceeds LOCK_MAX_YAW, instead of sweeping along.
		var pivot := ground_speed < 0.35
		var can := lock_target_w > 0.5 and AnimRigSettings.foot_lock and ik_w > 0.95 and not jumped
		if locked[i]:
			var lp: Vector3 = lock_pos[i]
			var d := Vector2(lp.x - anim_w.x, lp.z - anim_w.z).length()
			var yaw_d := absf(wrapf(_yaw_of(xf.basis * fbas[i]) - float(lock_yaw[i]), -PI, PI))
			if not can or plant < 0.4 or d > LOCK_MAX_DIST or yaw_d > LOCK_MAX_YAW:
				_unlock(i)
				# Torn loose while still planted: no new lock until this foot has stepped, and the
				# catch-up is a quick step (the foot lifts on the way) rather than a slide.
				relock_wait[i] = plant >= 0.4
				catch_lift[i] = clampf(d * 0.45, 0.0, 0.09) if plant >= 0.4 else 0.0
		elif can and plant > 0.9 and float(lock_w[i]) < 0.35 and (pivot or vel < LOCK_MAX_FOOT_SPEED + 0.8 * local_vel.length()) and not relock_wait[i]:
			# Lock where the foot is drawn now (mid-release that is not the animated spot).
			var e0 := float(lock_w[i])
			e0 = e0 * e0 * (3.0 - 2.0 * e0)
			var lp0: Vector3 = lock_pos[i]
			var cur := anim_w.lerp(Vector3(lp0.x, anim_w.y, lp0.z), e0)
			var dyaw0 := wrapf(float(lock_yaw[i]) - _yaw_of(xf.basis * fbas[i]), -PI, PI) * e0
			locked[i] = true
			lock_pos[i] = cur
			lock_yaw[i] = _yaw_of(xf.basis * fbas[i]) + dyaw0
			lock_w[i] = 1.0
		if plant < 0.5 or vel > 1.4:
			relock_wait[i] = false
		if not locked[i]:
			lock_w[i] = move_toward(float(lock_w[i]), 0.0, _dt / LOCK_RELEASE)
		var w := float(lock_w[i])
		if w <= 1e-3:
			continue
		var lp2: Vector3 = lock_pos[i]
		# Hold the ball horizontally (the ankle goal moves by the same amount); heights stay animated.
		var e := w * w * (3.0 - 2.0 * w)       # smoothstep release
		var shift := Vector3(lp2.x - anim_w.x, 0.0, lp2.z - anim_w.z) * e
		goal[i] = goal[i] + inv.basis * shift
		if float(catch_lift[i]) > 0.0 and not locked[i]:
			goal[i].y += 4.0 * float(catch_lift[i]) * w * (1.0 - w)
		# Keep the foot's world heading: undo the body's yaw change since touchdown.
		var dyaw := wrapf(float(lock_yaw[i]) - _yaw_of(xf.basis * fbas[i]), -PI, PI) * e
		gbas[i] = Basis(Vector3.UP, dyaw) * fbas[i]


func _unlock(i: int) -> void:
	locked[i] = false


## Drops every foot lock and spring at once (FighterView.snap: respawn / scenario reset).
func reset_state() -> void:
	for i in 2:
		locked[i] = false
		lock_w[i] = 0.0
		relock_wait[i] = false
		foot_off[i] = 0.0
		ground_w[i] = NAN
	hits.reset()
	# A (re)spawned fighter stands: IK and foot locks start engaged instead of fading in, so the
	# first turn toward a target pivots on planted feet rather than sweeping them.
	ik_w = 1.0 if AnimRigSettings.foot_ik else 0.0
	land_y = 0.0
	land_v = 0.0
	pelvis_shift = 0.0
	hip_tilt = 0.0


static func _yaw_of(b: Basis) -> float:
	# Heading of the foot's forward (+Z of the skeleton-space foot is along the sole after the rig's roll;
	# use the projected bone direction: local Y points from ankle to toe).
	var f := b.y
	return atan2(f.x, f.z)


## How much further than `shift` the pelvis must come down (m, >= 0) for both foot goals to be in reach.
func _reach_drop(sk: Skeleton3D, goal: Array[Vector3], shift: float) -> float:
	var need := 0.0
	for i in 2:
		var s := "L" if i == 0 else "R"
		var th: int = _b["thigh." + s]
		var hip := sk.get_bone_global_pose(th).origin + Vector3(0, shift, 0)
		var l := float(_leg_len[i]) * REACH_FRAC
		var d: Vector3 = goal[i] - hip
		var h2 := d.x * d.x + d.z * d.z
		if h2 >= l * l:
			continue
		var v := -d.y                     # hip above the goal
		var extra := v - sqrt(l * l - h2)
		need = maxf(need, extra)
	return clampf(need, 0.0, MAX_DROP)


## Head/neck/chest turn toward look_world, relative to where the clip already points the head,
## clamped and smoothed (never a snap: the correction itself is filtered).
func _apply_look(sk: Skeleton3D, xf: Transform3D) -> void:
	var head: int = _b.head
	if head < 0:
		return
	var want_yaw := 0.0
	var want_pitch := 0.0
	if look_w > 0.002:
		var hg := sk.get_bone_global_pose(head)
		var to := xf.affine_inverse() * look_world - hg.origin
		var fwd := hg.basis * _head_fwd_local
		var hl := Vector2(to.x, to.z).length()
		if hl > 0.3:
			want_yaw = clampf(wrapf(atan2(to.x, to.z) - atan2(fwd.x, fwd.z), -PI, PI), -LOOK_YAW_MAX, LOOK_YAW_MAX) * look_w
			want_pitch = clampf(atan2(to.y, hl) - asin(clampf(fwd.y, -1.0, 1.0)), -LOOK_PITCH_MAX, LOOK_PITCH_MAX) * look_w
	var k := 1.0 - exp(-7.0 * _dt)
	look_yaw = lerpf(look_yaw, want_yaw, k)
	look_pitch = lerpf(look_pitch, want_pitch, k)
	if absf(look_yaw) < 1e-4 and absf(look_pitch) < 1e-4:
		return
	# Yaw is shared chest 20% / neck 35% / head 45%; pitch neck 40% / head 60% (about the
	# character's own right axis, so a turned head still nods correctly).
	# Rotating about the character's right (-X) by +a raises the face.
	var right := Vector3(-1, 0, 0)
	LegIK.rotate_global(sk, _b.chest, Quaternion(Vector3.UP, look_yaw * 0.2))
	var ax_n := Quaternion(Vector3.UP, look_yaw * 0.55) * right
	LegIK.rotate_global(sk, _b.neck, Quaternion(ax_n, look_pitch * 0.4) * Quaternion(Vector3.UP, look_yaw * 0.35))
	var ax_h := Quaternion(Vector3.UP, look_yaw) * right
	LegIK.rotate_global(sk, head, Quaternion(ax_h, look_pitch * 0.6) * Quaternion(Vector3.UP, look_yaw * 0.45))
