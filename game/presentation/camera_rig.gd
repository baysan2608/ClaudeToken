class_name CameraRig
extends Node3D
## Third-person orbit camera. Player drags rotate it; when idle it gently turns
## to keep the locked target in frame (camera assistance, never a hard snap).
## Collision uses the analytic ArenaMap so walls/pillars pull the camera in.

var arena: ArenaMap
var yaw := PI                 # camera looks along (sin(yaw), 0, cos(yaw))
var pitch := 0.32             # radians above horizontal
var distance := 5.6
var height := 1.45
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


func add_input(delta: Vector2) -> void:
	if delta.length_squared() > 1e-8:
		_idle = 0.0
	yaw -= delta.x * sensitivity
	pitch = clampf(pitch + delta.y * sensitivity * (-1.0 if invert_y else 1.0), -0.12, 0.95)


func shake(amount: float) -> void:
	if reduced_motion:
		return
	_shake = maxf(_shake, amount * shake_scale)


func update_rig(dt: float, player_pos: Vector3, target_pos: Variant, threat_pos: Variant) -> void:
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
	# Collision: never place the camera inside arena geometry.
	var dir := _offset_dir()
	var desired := _pivot + dir * want_dist
	var t := arena.segment_hit(_pivot, desired, 0.3) if arena != null else -1.0
	var limit := want_dist if t < 0.0 else maxf(0.8, want_dist * t - 0.15)
	if limit < _cur_dist:
		_cur_dist = limit  # pull in immediately
	else:
		_cur_dist = lerpf(_cur_dist, limit, 1.0 - exp(-3.0 * dt))
	var shake_off := Vector3.ZERO
	if _shake > 0.001:
		_shake_t += dt * 40.0
		shake_off = Vector3(sin(_shake_t * 1.3), sin(_shake_t * 1.7 + 1.0), 0.0) * _shake * 0.06
		_shake = maxf(0.0, _shake - dt * 3.5)
	_update_transform(shake_off)


func _offset_dir() -> Vector3:
	var f := forward_flat()
	return (-f * cos(pitch) + Vector3.UP * sin(pitch)).normalized()


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
