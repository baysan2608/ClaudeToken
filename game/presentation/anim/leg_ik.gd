class_name LegIK
extends RefCounted
## Analytic two-bone IK for thigh > shin > foot in skeleton space.
## Keeps the animated knee plane (the knee only folds/unfolds in the plane the clip authored,
## then the whole leg swings minimally about the hip), so a target equal to the animated ankle
## reproduces the clip exactly and fading the weight never pops. The foot's global orientation
## is set explicitly (the caller passes the pre-additive animated one), so planted soles stay flat.


## Rotates `bone` by q (skeleton space, about the bone's own head) by rewriting its local rotation.
static func rotate_global(sk: Skeleton3D, bone: int, q: Quaternion) -> void:
	if bone < 0:
		return
	var g := sk.get_bone_global_pose(bone).basis
	var p := sk.get_bone_parent(bone)
	var nb := Basis(q) * g
	if p >= 0:
		nb = sk.get_bone_global_pose(p).basis.inverse() * nb
	sk.set_bone_pose_rotation(bone, nb.get_rotation_quaternion())


## Sets the global (skeleton-space) basis of `bone`.
static func set_global_basis(sk: Skeleton3D, bone: int, b: Basis) -> void:
	var p := sk.get_bone_parent(bone)
	var nb := b
	if p >= 0:
		nb = sk.get_bone_global_pose(p).basis.inverse() * b
	sk.set_bone_pose_rotation(bone, nb.get_rotation_quaternion())


const SOFT := 0.05


static func _soft(d: float, l: float, d_anim: float) -> float:
	var knee := minf(l - SOFT, d_anim)
	if d <= knee:
		return d
	var span := maxf(l - knee, 1e-3)
	return knee + span * (1.0 - exp(-(d - knee) / span))


## Moves the ankle (head of `foot`) to `target` (skeleton space) with weight w and gives the foot
## the global basis `foot_basis` (blended by w). Returns the remaining distance to the target
## (> 0 when the leg could not reach).
static func solve(sk: Skeleton3D, thigh: int, shin: int, foot: int, target: Vector3, foot_basis: Basis, w: float) -> float:
	if w <= 1e-4:
		return 0.0
	var gt := sk.get_bone_global_pose(thigh)
	var gs := sk.get_bone_global_pose(shin)
	var gf := sk.get_bone_global_pose(foot)
	var a := gt.origin
	var b := gs.origin
	var c := gf.origin
	var t := c.lerp(target, clampf(w, 0.0, 1.0))
	var l1 := (b - a).length()
	var l2 := (c - b).length()
	if l1 < 1e-4 or l2 < 1e-4:
		return 0.0
	var new_t := gt.basis
	var new_s := gs.basis
	if (t - c).length_squared() > 1e-10:
		# Soft IK: near full extension the knee angle is singular (a mm of reach = degrees of knee),
		# so the reach is eased into the last SOFT metres. The eased zone starts at the animated
		# ankle's own distance when that is already in it, so a target equal to it still
		# reproduces the clip exactly.
		var dt_ := (t - a).length()
		var soft_d := _soft(dt_, l1 + l2, (c - a).length())
		if dt_ > 1e-5 and absf(soft_d - dt_) > 1e-6:
			t = a + (t - a) * (soft_d / dt_)
		var lat := clampf((t - a).length(), absf(l1 - l2) + 1e-3, l1 + l2 - 1e-3)
		var ba := (a - b) / l1
		var bc := (c - b) / l2
		var theta0 := acos(clampf(ba.dot(bc), -1.0, 1.0))
		var theta1 := acos(clampf((l1 * l1 + l2 * l2 - lat * lat) / (2.0 * l1 * l2), -1.0, 1.0))
		var n := bc.cross(ba)
		if n.length_squared() < 1e-10:
			n = gt.basis.x
		n = n.normalized()
		# Positive rotation about n = bc x ba turns bc toward ba (closes the knee).
		var r1 := Quaternion(n, theta0 - theta1)
		var c1 := b + r1 * (c - b)
		var from := c1 - a
		var to := t - a
		var r2 := Quaternion.IDENTITY
		if from.length_squared() > 1e-10 and to.length_squared() > 1e-10:
			var fn := from.normalized()
			var tn := to.normalized()
			if fn.dot(tn) < 0.99999999:
				r2 = Quaternion(fn, tn)
		new_t = Basis(r2) * gt.basis
		new_s = Basis(r2 * r1) * gs.basis
		set_global_basis(sk, thigh, new_t)
		sk.set_bone_pose_rotation(shin, (new_t.inverse() * new_s).get_rotation_quaternion())
	var fb := gf.basis.slerp(foot_basis, clampf(w, 0.0, 1.0)) if w < 0.999 else foot_basis
	sk.set_bone_pose_rotation(foot, (new_s.inverse() * fb).get_rotation_quaternion())
	return maxf(0.0, (t - a).length() - (l1 + l2))
