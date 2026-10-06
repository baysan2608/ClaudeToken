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
var _frame_target: Variant = null   # the locked rival (kept in frame by _update_transform)
var _frame_w := 0.0
var _orbit := 0.0              # yaw offset (rad) the rig swings to when a wall is behind the player
var _see_through: Array[String] = []   # solids the camera currently sits behind (drawn shadows-only)
## Optional: the ArenaView whose solid meshes (named after ArenaMap solids) are made see-through when the camera
## has to sit behind one (cast-shadow SHADOWS_ONLY: the wall still shades the floor but never blocks the view).
var arena_view: Node3D:
	set(v):
		arena_view = v
		_see_through.clear()   # a rebuilt view starts with every solid drawn normally
const BASE_FOV := 62.0
const REDUCED_SHAKE := 0.3
## Never closer than this to the fighter (a wall behind them swings / lifts the camera, never collapses it).
const MIN_DIST := 3.0
## With a rival, the camera never looks down steeper than this (both fighters stay readable); past it the wall
## behind is drawn see-through instead of lifting further.
const MAX_PITCH_TARGET := 0.62
const ORBIT_MAX := deg_to_rad(65.0)
## Absolute floor in a boxed-in corner (both yard walls within reach): the camera rises and looks down instead.
const MIN_PULL := 1.6


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


## The direction the camera looks along (flat): the player's yaw plus the wall-avoidance swing. Movement is
## camera-relative to this, so the stick matches what is on screen.
func forward_flat() -> Vector3:
	var y := yaw + _orbit
	return Vector3(sin(y), 0.0, cos(y))


func view_yaw() -> float:
	return yaw + _orbit


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
	_frame_target = target_pos
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
	# Collision: never place the camera inside arena geometry, and never collapse onto the fighter's back.
	# 1. A wall behind the player swings the camera sideways (_orbit, smoothly; first keeping the rival in frame,
	#    up to 100 deg when nothing else fits); 2. what is still blocked lifts it (pitch capped with a rival);
	#    3. it stays >= MIN_DIST: an interior solid (pillar, cover wall) in the way is drawn shadows-only
	#    (see-through), while the arena's boundary walls are never crossed (the camera stays inside the yard).
	var max_p := MAX_PITCH_TARGET if target_pos != null else 1.2
	var want_orbit := 0.0
	if arena != null:
		var clear0 := _clear_dist(yaw + _orbit, pitch, want_dist)
		if clear0 < want_dist * 0.8 or absf(_orbit) > 0.01:
			want_orbit = _best_orbit(want_dist, player_pos, target_pos)
	_orbit = lerpf(_orbit, want_orbit, 1.0 - exp(-3.0 * dt))
	var vy := yaw + _orbit
	var clear := _clear_dist(vy, pitch, want_dist) if arena != null else want_dist
	var bclear := _clear_dist(vy, pitch, want_dist, true) if arena != null else want_dist
	if bclear < MIN_DIST:
		max_p = 1.1   # boxed in by the yard's walls (a corner): look down over the fighter rather than leave the yard
	var want_lift := clampf(1.0 - clear / want_dist, 0.0, 1.0) * 0.75
	if bclear < MIN_DIST and arena != null:
		# The lowest pitch (up to 1.1) at which the camera fits inside the yard at MIN_DIST.
		var need := 1.1
		var pp := pitch
		while pp <= 1.1:
			if _clear_dist(vy, pp, want_dist, true) >= MIN_DIST:
				need = pp
				break
			pp += 0.05
		want_lift = maxf(want_lift, need - pitch)
	want_lift = minf(want_lift, maxf(0.0, max_p - pitch))
	_lift = lerpf(_lift, want_lift, 1.0 - exp(-(10.0 if want_lift > _lift else 2.5) * dt))
	var limit := want_dist
	if arena != null:
		var pl := minf(pitch + _lift, 1.2)
		var all_c := _clear_dist(vy, pl, want_dist)
		var bnd_c := _clear_dist(vy, pl, want_dist, true)
		limit = maxf(all_c, minf(minf(MIN_DIST, want_dist), bnd_c))
		limit = maxf(limit, MIN_PULL)
	if limit < _cur_dist:
		_cur_dist = lerpf(_cur_dist, limit, 1.0 - exp(-18.0 * dt))  # pull in fast (but not a one-frame snap)
	else:
		_cur_dist = lerpf(_cur_dist, limit, 1.0 - exp(-3.0 * dt))
	_update_transform(_feel(rdt))
	_update_see_through()


## Free distance from the pivot along the camera direction (yaw y, pitch p), up to `want` - against every solid, or
## only the yard's boundary walls.
func _clear_dist(y: float, p: float, want: float, boundary_only: bool = false) -> float:
	if arena == null:
		return want
	var d := _dir_of(y, p) * want
	var best := -1.0
	var r := Vector3.ONE * 0.3
	for sd in arena.solids:
		if boundary_only and not _is_boundary(sd):
			continue
		var t := ArenaMap._slab(_pivot, d, sd.min - r, sd.max + r)
		if t >= 0.0 and (best < 0.0 or t < best):
			best = t
	return want if best < 0.0 else maxf(0.0, want * best - 0.15)


func _is_boundary(sd: Dictionary) -> bool:
	var h := arena.half_size - 0.01
	var mn: Vector3 = sd.min
	var mx: Vector3 = sd.max
	return mx.x <= -h or mn.x >= h or mx.z <= -h or mn.z >= h


func _dir_of(y: float, p: float) -> Vector3:
	var f := Vector3(sin(y), 0.0, cos(y))
	return (-f * cos(p) + Vector3.UP * sin(p)).normalized()


## The swing (relative to yaw) that gives the camera the most room, preferring small swings, among those that keep
## the rival inside the horizontal field of view (with the framing turn: fighter and rival within 2 x half FOV);
## flush against a yard wall the swing may reach 100 deg so the view runs along the wall.
func _best_orbit(want: float, player_pos: Vector3, target_pos: Variant) -> float:
	var best_d := _clear_dist(yaw, pitch, want)
	if best_d >= want * 0.95:
		return 0.0
	var half_h := _half_hfov() - deg_to_rad(8.0)
	var best := _search_orbit(want, player_pos, target_pos, half_h, ORBIT_MAX, best_d)
	if _clear_dist(yaw + best, pitch, want, true) < MIN_DIST:
		# Flush against a yard wall: run the view along the wall (up to 100 deg; framing keeps the rival in).
		var wide := _search_orbit(want, player_pos, target_pos, half_h, deg_to_rad(100.0), best_d)
		if _clear_dist(yaw + wide, pitch, want, true) > _clear_dist(yaw + best, pitch, want, true) + 0.3:
			best = wide
	# Hysteresis: keep the current swing if it is nearly as good (no flip-flopping between sides).
	if absf(_orbit) > 0.05 and signf(_orbit) != signf(best) and best != 0.0:
		var cur_d := _clear_dist(yaw + _orbit, pitch, want)
		if cur_d >= _clear_dist(yaw + best, pitch, want) - 0.5:
			return _orbit
	return best


func _search_orbit(want: float, player_pos: Vector3, target_pos: Variant, half_h: float, max_off: float, base_d: float) -> float:
	var best := 0.0
	var best_d := base_d
	for k in range(1, 14):
		var off := max_off * float(k) / 13.0
		for sg in [1.0, -1.0]:
			var o: float = off * sg
			if target_pos != null and not _target_in_view(yaw + o, want, player_pos, target_pos as Vector3, half_h):
				continue
			var d := _clear_dist(yaw + o, pitch, want)
			if d > best_d + 0.25 + 0.4 * absf(o):   # a swing has to buy real room
				best_d = d
				best = o
		if best_d >= want * 0.95:
			break
	return best


func _half_hfov() -> float:
	var aspect := 4.0 / 3.0   # conservative (iPad) when no viewport yet
	if cam != null and cam.is_inside_tree():
		var vs := cam.get_viewport().get_visible_rect().size
		if vs.y > 1.0:
			aspect = vs.x / vs.y
	return atan(tan(deg_to_rad(BASE_FOV) * 0.5) * aspect)


func _target_in_view(y: float, dist: float, player_pos: Vector3, target_pos: Vector3, half_h: float) -> bool:
	var c := _pivot + _dir_of(y, pitch) * dist
	var look := (_pivot - c)
	look.y = 0.0
	var to := target_pos - c
	to.y = 0.0
	if look.length() < 1e-3 or to.length() < 1e-3:
		return true
	# The framing turn (_update_transform) can centre the view between the two: each may sit half_h off-centre.
	return absf(look.signed_angle_to(to, Vector3.UP)) <= half_h * 2.0


## Solids between the pivot and the camera are drawn shadows-only (the wall keeps its shadow, never blocks).
func _update_see_through() -> void:
	if arena == null or arena_view == null:
		return
	var now: Array[String] = []
	var cp := global_position
	var d := cp - _pivot
	for sd in arena.solids:
		var t := ArenaMap._slab(_pivot, d, sd.min - Vector3.ONE * 0.2, sd.max + Vector3.ONE * 0.2)
		if t >= 0.0:
			now.append(String(sd.name))
	if now == _see_through:
		return
	for nm in _see_through:
		if not now.has(nm):
			_set_see_through(nm, false)
	for nm in now:
		if not _see_through.has(nm):
			_set_see_through(nm, true)
	_see_through = now


func _set_see_through(nm: String, on: bool) -> void:
	for c in arena_view.get_children():
		if c is GeometryInstance3D and String(c.name) == nm:
			(c as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY if on \
				else GeometryInstance3D.SHADOW_CASTING_SETTING_ON


## Current camera-to-player distance and swing, for tests: [distance, orbit, lift].
func rig_state() -> Array:
	return [_cur_dist, _orbit, _lift]


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
	var look_pt := _pivot + Vector3(0, 0.15, 0)
	var u_p := look_pt - p
	if _frame_target != null and u_p.length() > 0.1:
		# Keep the rival in frame: when the camera is close / high (a wall behind the fighter) and the rival would
		# fall outside the view, turn the view toward them just enough (at most to the bisector of the two).
		var u_t: Vector3 = (_frame_target as Vector3) + Vector3(0, 1.2, 0) - p
		var a := u_p.angle_to(u_t)
		var lim := deg_to_rad(BASE_FOV * 0.5 - 6.0)
		var want_w := 0.0
		if a > lim and a > 1e-3:
			want_w = minf(a - lim, a * 0.5) / a
		_frame_w = lerpf(_frame_w, want_w, 0.25)
		if _frame_w > 1e-3:
			var dir := u_p.normalized().slerp(u_t.normalized(), _frame_w)
			look_pt = p + dir * u_p.length()
	else:
		_frame_w = 0.0
	look_at(look_pt, Vector3.UP)
	cam.position = shake_off


func world_to_screen(p: Vector3) -> Variant:
	if cam == null or cam.is_position_behind(p):
		return null
	return cam.unproject_position(p)
