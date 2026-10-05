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
const LOOK_YAW_MAX := 1.05           # rad (about 60 deg) beyond the animated head direction
const LOOK_PITCH_MAX := 0.45

# ---- inputs (set by FighterView every frame) ----
var loco_target := 0.0               # 1 = locomotion owns the whole body
var legs_target := 0.0               # >0 = gait legs under an upper-body action (guard walk)
var local_vel := Vector2.ZERO        # x = toward the character's right, y = forward (m/s)
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
	var off_eff := [0.0, 0.0]
	if use_ik:
		var floor_y := xf.origin.y
		var k := 1.0 - exp(-22.0 * _dt)
		for i in 2:
			var toe: int = _b["toe.L" if i == 0 else "toe.R"]
			var aw := xf * ank[i]
			var bw := xf * (sk.get_bone_global_pose(toe).origin if toe >= 0 else ank[i])
			var from_y := floor_y + Sim.STEP_HEIGHT + 0.02
			var g := maxf(arena.ground_height(aw.x, aw.z, from_y), arena.ground_height(bw.x, bw.z, from_y))
			var off := g - floor_y
			if off < -MAX_DROP:
				off = 0.0          # past a ledge edge: the foot stays on the edge, no reaching into the void
			off = clampf(off, -MAX_DROP, MAX_LIFT)
			var plant := 1.0 - smoothstep(PLANT_FROM, PLANT_TO, ank[i].y)
			var tgt := off if off > 0.0 else off * plant
			foot_off[i] = lerpf(float(foot_off[i]), tgt, k)
			off_eff[i] = foot_off[i]
		var shift_t := clampf(minf(float(off_eff[0]), float(off_eff[1])), -MAX_DROP, 0.4)
		pelvis_shift = lerpf(pelvis_shift, shift_t, 1.0 - exp(-10.0 * _dt))
	else:
		pelvis_shift = lerpf(pelvis_shift, 0.0, 1.0 - exp(-10.0 * _dt))
		foot_off[0] = 0.0
		foot_off[1] = 0.0
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
	if hip_share > 0.001:
		LegIK.rotate_global(sk, hips, Quaternion(ax_pitch, pitch * hip_share) * Quaternion(ax_roll, roll * hip_share) * HitReactor.quat(t, 0.25 * ik_w))
	var rest := 1.0 - hip_share
	var torso_rest := 1.0 - 0.25 * ik_w
	var spine_q := Quaternion(Vector3.UP, aim_yaw * aim_w * 0.4) * Quaternion(ax_pitch, pitch * rest * 0.5) \
		* Quaternion(ax_roll, roll * rest * 0.5) * HitReactor.quat(t, torso_rest * 0.45)
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
	if use_ik or ik_w > 0.002:
		for i in 2:
			var s := "L" if i == 0 else "R"
			var goal: Vector3 = ank[i] + Vector3(0, float(off_eff[i]), 0)
			last_reach_error = maxf(last_reach_error, LegIK.solve(sk, _b["thigh." + s], _b["shin." + s], _b["foot." + s], goal, fbas[i], ik_w))
			if AnimRigSettings.debug_draw:
				dbg_points.append(xf * goal)
	# hands for VFX anchoring
	if _b["hand.R"] >= 0:
		hand_world[0] = xf * sk.get_bone_global_pose(_b["hand.R"]).origin
	if _b["hand.L"] >= 0:
		hand_world[1] = xf * sk.get_bone_global_pose(_b["hand.L"]).origin
	hand_frame = Engine.get_process_frames()


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
