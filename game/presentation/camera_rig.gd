class_name CameraRig
extends Node3D
## Third-person orbit camera. Player drags rotate it; when idle it gently turns
## to keep the locked target in frame (camera assistance, never a hard snap).
## Collision uses the analytic ArenaMap so walls/pillars pull the camera in.
##
## Game feel (docs/MOVESET.md §10.2) runs in REAL time, so it keeps moving through a hit-stop
## (Engine.time_scale dip): shake with distance falloff 1 / (1 + d / 8) and its own decay, a
## directional kick along the hit, a FOV punch (T3) and the transformation zoom (3 % toward the
## event for 0.3 s). Reduced motion: shake x0.3, no FOV punch, no zoom, no kick.

var arena: ArenaMap
var yaw := PI                 # camera looks along (sin(yaw), 0, cos(yaw))
var pitch := 0.32             # radians above horizontal
var distance := 5.6
var height := 1.45
# Not applied by add_input: the input producers already fold them into cam_delta.
var sensitivity := 1.0
var invert_y := false
var shake_scale := 1.0
var reduced_motion := false

var cam: Camera3D
var _pivot := Vector3.ZERO
var _cur_dist := 5.6
var _idle := 0.0
var _shake := 0.0
var _shake_t := 0.0
var _focus := Vector3.ZERO
var _lift := 0.0               # extra pitch when walls pull the camera in
var _shake_decay := 3.5        # amount per second (set per shake)
var _kick := Vector3.ZERO      # world offset, decays to zero
var _kick_t := 0.0
var _fov_punch := 0.0          # degrees (negative = narrower)
var _fov_t := 0.0
var _fov_dur := 0.25
var _zoom_t := 0.0
var _zoom_dur := 0.3
var _zoom_amt := 0.0
var _zoom_at := Vector3.ZERO
var _last_us := -1
const BASE_FOV := 62.0
const REDUCED_SHAKE := 0.3


func _ready() -> void:
	cam = Camera3D.new()
	cam.fov = 62.0
	cam.near = 0.05
	cam.far = 120.0
	add_child(cam)
	cam.current = true


func snap_to(player_pos: Vector3, look_at_pos: Vector3) -> void:
	var d := look_at_pos - player_pos
	d.y = 0.0
	if d.length() > 0.1:
		yaw = atan2(d.x, d.z)
	_pivot = player_pos + Vector3(0, height, 0)
	_cur_dist = distance
	_update_transform(Vector3.ZERO)


func forward_flat() -> Vector3:
	return Vector3(sin(yaw), 0.0, cos(yaw))


## `delta` is InputFrame.cam_delta: radians, x = look right +, y = look up +, with
## camera_sensitivity and invert_y already applied by the producer (applied once).
func add_input(delta: Vector2) -> void:
	if delta.length_squared() > 1e-8:
		_idle = 0.0
	yaw -= delta.x
	# pitch is the camera's elevation: looking up lowers the camera.
	pitch = clampf(pitch - delta.y, -0.12, 0.95)


func shake(amount: float) -> void:
	_shake = maxf(_shake, amount * shake_scale * (REDUCED_SHAKE if reduced_motion else 1.0))
	_shake_decay = 3.5


## Shake from an event at `pos` (falls off with the distance to the player: 1 / (1 + d / 8)) that
## decays over `decay_s` seconds of real time.
func shake_at(amount: float, pos: Vector3, decay_s: float = 0.2) -> void:
	var d := pos.distance_to(_pivot - Vector3(0, height, 0)) if pos != Vector3.INF else 0.0
	var a := amount * shake_scale * (REDUCED_SHAKE if reduced_motion else 1.0) / (1.0 + d / 8.0)
	if a > _shake:
		_shake = a
		_shake_decay = a / maxf(decay_s, 0.05)


## Directional kick: the camera is pushed `amount` metres along `dir` and springs back (0.2 s).
func kick(dir: Vector3, amount: float = 0.1) -> void:
	if reduced_motion or dir.length_squared() < 1e-6:
		return
	_kick = dir.normalized() * amount * shake_scale
	_kick_t = 0.2


## FOV punch (T3 hits): narrows the view by `deg` degrees, eased back over `dur` seconds.
func fov_punch(deg: float = -3.0, dur: float = 0.25) -> void:
	if reduced_motion:
		return
	_fov_punch = deg
	_fov_t = dur
	_fov_dur = dur


## Transformation zoom: 3 % toward `at` for 0.3 s (stone -> lava, wall slump, glass fuse, ice ridge).
func zoom_to(at: Vector3, amount: float = 0.03, dur: float = 0.3) -> void:
	if reduced_motion:
		return
	_zoom_at = at
	_zoom_amt = amount
	_zoom_t = dur
	_zoom_dur = dur


## Real seconds since the previous rig update (independent of Engine.time_scale).
func _real_dt(dt: float) -> float:
	var now := Time.get_ticks_usec()
	var r := dt
	if _last_us >= 0:
		r = clampf(float(now - _last_us) / 1e6, 0.0, 0.1)
	_last_us = now
	return r


## Current feel offsets, for tests: [shake, kick, fov_punch, zoom].
func feel_state() -> Array:
	return [_shake, _kick, _fov_punch * (_fov_t / maxf(_fov_dur, 1e-3)), _zoom_t]


func update_rig(dt: float, player_pos: Vector3, target_pos: Variant, threat_pos: Variant) -> void:
	var rdt := _real_dt(dt)
	_idle += dt
	# Follow: fast but not rigid (smooths sim steps and knockbacks).
	var want_pivot := player_pos + Vector3(0, height, 0)
	_pivot = _pivot.lerp(want_pivot, 1.0 - exp(-14.0 * dt))
	# Assist: keep the target (or an incoming threat) inside the frame when the player isn't steering.
	var focus_pos: Variant = threat_pos if threat_pos != null else target_pos
	if focus_pos != null and _idle > 0.45:
		var to: Vector3 = (focus_pos as Vector3) - player_pos
		to.y = 0.0
		if to.length() > 1.0:
			var want := atan2(to.x, to.z)
			var diff := wrapf(want - yaw, -PI, PI)
			var dead := deg_to_rad(18.0)
			if absf(diff) > dead:
				var rate := (1.4 if threat_pos == null else 2.6) * dt
				yaw += clampf(diff - signf(diff) * dead, -rate, rate)
		# Pull back a little when the target is far, so both fighters stay readable.
	var want_dist := distance
	if target_pos != null:
		var sep := (target_pos as Vector3).distance_to(player_pos)
		want_dist = clampf(distance + (sep - 8.0) * 0.12, distance - 0.6, distance + 1.6)
	# Collision: never place the camera inside arena geometry. When a wall pulls it in,
	# rise and look down over the fighter instead of filling the screen with their back.
	# Measure the obstruction along the un-lifted direction (stable, no oscillation).
	var f0 := forward_flat()
	var base_dir := (-f0 * cos(pitch) + Vector3.UP * sin(pitch)).normalized()
	var t := arena.segment_hit(_pivot, _pivot + base_dir * want_dist, 0.3) if arena != null else -1.0
	var limit := want_dist if t < 0.0 else maxf(0.8, want_dist * t - 0.15)
	var dir := base_dir
	var want_lift := clampf(1.0 - limit / want_dist, 0.0, 1.0) * 0.75
	_lift = lerpf(_lift, want_lift, 1.0 - exp(-(10.0 if want_lift > _lift else 2.5) * dt))
	if _lift > 0.01:
		dir = _offset_dir()
		var t2 := arena.segment_hit(_pivot, _pivot + dir * want_dist, 0.3)
		limit = want_dist if t2 < 0.0 else maxf(0.8, want_dist * t2 - 0.15)
	if limit < _cur_dist:
		_cur_dist = limit  # pull in immediately
	else:
		_cur_dist = lerpf(_cur_dist, limit, 1.0 - exp(-3.0 * dt))
	_update_transform(_feel(rdt))


## Shake + kick + FOV punch + zoom for this frame (real time).
func _feel(rdt: float) -> Vector3:
	var off := Vector3.ZERO
	if _shake > 0.001:
		_shake_t += rdt * 40.0
		off = Vector3(sin(_shake_t * 1.3), sin(_shake_t * 1.7 + 1.0), 0.0) * _shake * 0.06
		_shake = maxf(0.0, _shake - rdt * _shake_decay)
	if _kick_t > 0.0:
		_kick_t = maxf(0.0, _kick_t - rdt)
		var k := _kick_t / 0.2
		# local camera space: the kick is applied as a world offset projected on the camera plane
		off += (cam.global_basis.inverse() * _kick) * k * k if cam and cam.is_inside_tree() else Vector3.ZERO
	var fov := BASE_FOV
	if _fov_t > 0.0:
		_fov_t = maxf(0.0, _fov_t - rdt)
		var f := _fov_t / maxf(_fov_dur, 1e-3)
		fov += _fov_punch * f * f
	if _zoom_t > 0.0:
		_zoom_t = maxf(0.0, _zoom_t - rdt)
		var z := sin(PI * (1.0 - _zoom_t / maxf(_zoom_dur, 1e-3)))
		fov *= 1.0 - _zoom_amt * z
		if cam and cam.is_inside_tree():
			# and drift toward the event, so the zoom centres on it rather than on the screen
			var to := _zoom_at - cam.global_position
			off += (cam.global_basis.inverse() * to) * _zoom_amt * z
	if cam:
		cam.fov = fov
	return off


func _offset_dir() -> Vector3:
	var f := forward_flat()
	var p := minf(pitch + _lift, 1.2)
	return (-f * cos(p) + Vector3.UP * sin(p)).normalized()


func _update_transform(shake_off: Vector3) -> void:
	var p := _pivot + _offset_dir() * _cur_dist
	if arena != null:
		var g := arena.ground_height(p.x, p.z, p.y) + 0.35
		p.y = maxf(p.y, g)
	global_position = p
	look_at(_pivot + Vector3(0, 0.15, 0), Vector3.UP)
	cam.position = shake_off


func world_to_screen(p: Vector3) -> Variant:
	if cam == null or cam.is_position_behind(p):
		return null
	return cam.unproject_position(p)
