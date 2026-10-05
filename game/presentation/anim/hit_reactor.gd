class_name HitReactor
extends RefCounted
## Physical hit reactions as additive spring-damper rotations (presentation only; the sim
## already moved the fighter and chose the stun). Each spring is an axis-angle Vector3 in the
## fighter's skeleton space (+Z forward, +Y up, +X the character's left). A hit kicks the springs'
## angular velocity so the torso tips along the hit direction, the head whips a beat later, the
## arms flail; they ring out in about half a second. Blocks give a short recoil.

const TORSO := {"f": 2.6, "z": 0.45}
const HEAD := {"f": 2.1, "z": 0.34}
const ARM := {"f": 2.3, "z": 0.4}
const MAX_ANGLE := 0.65              # rad, hard clamp per spring
const HEAD_DELAY := 0.045            # s, the head lags the torso (whiplash)

var torso := Vector3.ZERO
var torso_v := Vector3.ZERO
var head := Vector3.ZERO
var head_v := Vector3.ZERO
var arm_l := Vector3.ZERO
var arm_l_v := Vector3.ZERO
var arm_r := Vector3.ZERO
var arm_r_v := Vector3.ZERO
var _pending_head := Vector3.ZERO
var _pending_t := -1.0
var last_kind := ""


## Axis that tips the +Y "up" of a bone toward `dir` (horizontal, skeleton space).
static func tip_axis(dir: Vector3) -> Vector3:
	var d := Vector3(dir.x, 0.0, dir.z)
	if d.length_squared() < 1e-6:
		d = Vector3(0, 0, -1)
	return Vector3.UP.cross(d.normalized())


## dir: direction the blow travels (attacker -> victim), skeleton space. strength ~0.3..1.2.
func hit(dir: Vector3, strength: float) -> void:
	var s := clampf(strength, 0.0, 1.4)
	var ax := tip_axis(dir)
	var lateral := clampf(dir.x, -1.0, 1.0)   # +: blow travels toward the character's left
	torso_v += ax * (6.5 * s) + Vector3.UP * (lateral * 1.6 * s)
	_pending_head += ax * (7.5 * s) + Vector3.UP * (lateral * 3.0 * s)
	_pending_t = HEAD_DELAY
	# Arms are thrown opposite to the torso's motion (they lag), plus a little outward.
	arm_l_v += -ax * (5.0 * s) + Vector3(0, 0, 1) * (2.0 * s)
	arm_r_v += -ax * (5.0 * s) + Vector3(0, 0, -1) * (2.0 * s)
	last_kind = "hit"


## A guarded blow: a short push back of the torso and both forearms, no head whip.
func block(dir: Vector3, strength: float) -> void:
	var s := clampf(strength, 0.0, 1.0)
	var ax := tip_axis(dir)
	torso_v += ax * (3.2 * s)
	head_v += ax * (1.6 * s)
	arm_l_v += ax * (4.0 * s)
	arm_r_v += ax * (4.0 * s)
	last_kind = "block"


func reset() -> void:
	torso = Vector3.ZERO
	torso_v = Vector3.ZERO
	head = Vector3.ZERO
	head_v = Vector3.ZERO
	arm_l = Vector3.ZERO
	arm_l_v = Vector3.ZERO
	arm_r = Vector3.ZERO
	arm_r_v = Vector3.ZERO
	_pending_head = Vector3.ZERO
	_pending_t = -1.0


func active() -> bool:
	return (torso.length_squared() + head.length_squared() + arm_l.length_squared() + arm_r.length_squared()
		+ (torso_v.length_squared() + head_v.length_squared() + arm_l_v.length_squared() + arm_r_v.length_squared()) * 0.01) > 1e-6 or _pending_t >= 0.0


func step(dt: float) -> void:
	var left := clampf(dt, 0.0, 0.1)
	if _pending_t >= 0.0:
		_pending_t -= left
		if _pending_t < 0.0:
			head_v += _pending_head
			_pending_head = Vector3.ZERO
	while left > 1e-6:
		var h := minf(left, 1.0 / 120.0)
		left -= h
		var r := _spring(torso, torso_v, TORSO, h)
		torso = r[0]
		torso_v = r[1]
		r = _spring(head, head_v, HEAD, h)
		head = r[0]
		head_v = r[1]
		r = _spring(arm_l, arm_l_v, ARM, h)
		arm_l = r[0]
		arm_l_v = r[1]
		r = _spring(arm_r, arm_r_v, ARM, h)
		arm_r = r[0]
		arm_r_v = r[1]


static func _spring(x: Vector3, v: Vector3, p: Dictionary, h: float) -> Array:
	var w := TAU * float(p.f)
	var acc := -x * (w * w) - v * (2.0 * float(p.z) * w)
	v += acc * h
	x += v * h
	if x.length() > MAX_ANGLE:
		x = x.normalized() * MAX_ANGLE
		v *= 0.5
	return [x, v]


static func quat(v: Vector3, frac: float = 1.0) -> Quaternion:
	var a := v.length() * frac
	if a < 1e-6:
		return Quaternion.IDENTITY
	return Quaternion(v.normalized(), a)
