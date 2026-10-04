class_name TouchControls
extends Control
## Full-rect gameplay touch overlay: floating movement stick, camera drag,
## ATTACK / GUARD / EVADE / TECHNIQUE buttons, element chips, pause and
## target-cycle buttons.
##
## Finger ownership is decided at touch-down and kept until release/cancel:
##   - a finger that lands on a control owns that control
##   - otherwise on the stick half it owns the stick, else the camera
##   - a finger that owns nothing (second finger on a taken control) is inert
## so camera drags can never trigger attack/technique and a technique drag can
## never rotate the camera, however far the finger wanders.
##
## Events arrive through _input (not the GUI) with `index` bookkeeping. State
## is latched here and handed out once per simulation tick by fill_frame().

signal pause_requested
signal element_changed(element: int)

const MAX_FINGERS := 16
const MOUSE_FINGER := 15

const ROLE_NONE := 0
const ROLE_STICK := 1
const ROLE_CAMERA := 2
const ROLE_BUTTON := 3
const ROLE_DEAD := 4

## Stick dead zone as a fraction of the stick radius.
const DEADZONE := 0.08
## Attack move per element (CombatWorld picks the same ids) for the charge-ring timing.
const ATTACK_MOVES: Array[String] = ["earth_attack", "water_attack", "fire_attack", "air_attack"]
## Slack (s) so a ring time summed from 60 Hz steps reads full on the tick it is reached.
const CHARGE_EPS := 0.001
## Minimum drag (viewport px) before technique aim counts as intentional.
const AIM_ACTIVE_PX := 12.0
## ... and never less than this physically (finger roll on press is ~1 mm).
const AIM_ACTIVE_MM := 2.0
## Camera sensitivity: radians per millimetre of finger travel at 1.0.
const CAM_RAD_PER_MM := 0.055
## Pause fires on release when the finger is still within this many hit radii.
const PAUSE_RELEASE_SLACK := 1.8

## Attack ring fill time override (s); 0 = the selected element's rule (attack_charge_sec).
var charge_hold_sec: float = 0.0
## Desktop debug: treat the left mouse button as a finger (index 15).
var mouse_emulation: bool = false
var settings: GameSettings = null

var _layout := TouchLayout.new()
var _layout_override := false
var _ov_size := Vector2.ZERO
var _ov_insets := Vector4.ZERO
var _ov_ppm := 0.0

# --- finger bookkeeping (no per-event allocation) ---------------------------------
var _role := PackedInt32Array()
var _f_btn := PackedInt32Array()
var _f_pos := PackedVector2Array()
var _btn_finger := PackedInt32Array()

# --- stick ---------------------------------------------------------------------------
var _stick_finger := -1
var _stick_center := Vector2.ZERO
var _stick_knob := Vector2.ZERO
var _stick_vec := Vector2.ZERO
var _stick_alpha := 0.0

# --- camera ----------------------------------------------------------------------------
var _cam_finger := -1
var _cam_accum := Vector2.ZERO

# --- technique ----------------------------------------------------------------------------
var _tech_finger := -1
var _tech_origin := Vector2.ZERO
var _tech_pos := Vector2.ZERO
var _tech_aim := Vector2.ZERO
var _tech_aim_active := false
var _tech_cancelled := false

# --- latched edges (cleared by fill_frame) ---------------------------------------
var _l_attack_pressed := false
var _l_attack_released := false
var _l_guard_pressed := false
var _l_guard_released := false
var _l_evade := false
var _l_tech_pressed := false
var _l_tech_released := false
var _l_tech_cancel := false
var _l_element := -1
var _l_target := false
var _l_pause := false

# --- HUD context ----------------------------------------------------------------------------
var _ctx_element := 0
var _ctx_unlocked := PackedByteArray([1, 1, 1, 1])
var _ctx_label := ""
var _ctx_available := true
var _ctx_holding := false
var _ctx_has_charge := false     # the game reports the sim's attack progress (else the ring self-times)
var _ctx_attack_charge := -1.0
var _ctx_attack_element := -1
var _attack_sent := false        # this hold's press has reached the sim (fill_frame)

# --- visual state ----------------------------------------------------------------------------
var _vis_press := PackedFloat32Array()
var _chip_flash := PackedFloat32Array()
var _cancel_alpha := 0.0
var _cancel_near := 0.0
var _attack_t := 0.0
var _font: Font = null


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_NONE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_role.resize(MAX_FINGERS)
	_f_btn.resize(MAX_FINGERS)
	_f_pos.resize(MAX_FINGERS)
	_btn_finger.resize(TouchLayout.COUNT)
	_btn_finger.fill(-1)
	_vis_press.resize(TouchLayout.COUNT)
	_chip_flash.resize(UiStyle.ELEMENT_COUNT)


func _ready() -> void:
	_font = ThemeDB.fallback_font
	if settings == null:
		settings = GameSettings.current()
	if not settings.changed.is_connected(apply_settings):
		settings.changed.connect(apply_settings)
	var vp := get_viewport()
	if vp != null and not vp.size_changed.is_connected(relayout):
		vp.size_changed.connect(relayout)
	relayout()


func _exit_tree() -> void:
	release_all(true)
	if settings != null and settings.changed.is_connected(apply_settings):
		settings.changed.disconnect(apply_settings)


# --- public API ---------------------------------------------------------------------------------

## Use another settings object (also rebuilds the layout).
func use_settings(s: GameSettings) -> void:
	if settings != null and settings.changed.is_connected(apply_settings):
		settings.changed.disconnect(apply_settings)
	settings = s
	if is_inside_tree() and not settings.changed.is_connected(apply_settings):
		settings.changed.connect(apply_settings)
	apply_settings()


func apply_settings() -> void:
	relayout()
	queue_redraw()


## Pin the layout inputs (tests, screenshots) instead of probing the display.
func configure_layout(vp_size: Vector2, insets: Vector4, px_per_mm: float) -> void:
	_layout_override = true
	_ov_size = vp_size
	_ov_insets = insets
	_ov_ppm = px_per_mm
	relayout()


func get_layout() -> TouchLayout:
	return _layout


func relayout() -> void:
	var s := settings if settings != null else GameSettings.current()
	var vp_size := _ov_size
	var insets := _ov_insets
	var ppm := _ov_ppm
	if not _layout_override:
		var vp := get_viewport()
		if vp == null:
			return
		vp_size = vp.get_visible_rect().size
		insets = UiScale.safe_insets(vp)
		ppm = UiScale.px_per_mm(vp)
	_layout.configure(vp_size, insets, ppm, s.control_scale, s.layout_preset, s.left_handed)
	queue_redraw()


## HUD hints from the game (see PlayerInputHub.set_context). The game sends them
## every frame, so this redraws only when a value changed (returns true then).
func set_context(ctx: Dictionary) -> bool:
	var changed := false
	if ctx.has("element"):
		var el := clampi(int(ctx["element"]), 0, UiStyle.ELEMENT_COUNT - 1)
		changed = changed or el != _ctx_element
		_ctx_element = el
	if ctx.has("unlocked_elements"):
		var mask := 0
		for e in ctx["unlocked_elements"]:
			var ei := int(e)
			if ei >= 0 and ei < UiStyle.ELEMENT_COUNT:
				mask |= 1 << ei
		for ei in UiStyle.ELEMENT_COUNT:
			var u := (mask >> ei) & 1
			if _ctx_unlocked[ei] != u:
				_ctx_unlocked[ei] = u
				changed = true
	if ctx.has("tech_label"):
		var label := str(ctx["tech_label"])
		changed = changed or label != _ctx_label
		_ctx_label = label
	if ctx.has("tech_available"):
		var ok := bool(ctx["tech_available"])
		changed = changed or ok != _ctx_available
		_ctx_available = ok
	if ctx.has("holding"):
		var holding := bool(ctx["holding"])
		changed = changed or holding != _ctx_holding
		_ctx_holding = holding
	if ctx.has("attack_charge"):
		var ac := float(ctx["attack_charge"])
		var ae := int(ctx.get("attack_element", -1))
		# Only the ring shows it, and only while ATTACK is held.
		changed = changed or (_btn_finger[TouchLayout.Id.ATTACK] != -1 and (ac != _ctx_attack_charge or ae != _ctx_attack_element))
		_ctx_has_charge = true
		_ctx_attack_charge = ac
		_ctx_attack_element = ae
	if changed:
		queue_redraw()
	return changed


func is_element_unlocked(e: int) -> bool:
	return e >= 0 and e < UiStyle.ELEMENT_COUNT and _ctx_unlocked[e] == 1


## Hold time (s) after which the sim commits `element`'s attack to a charge:
## max(startup, Moves.HOLD_THRESHOLD) from the press (CombatWorld.attack_after_startup),
## rounded up to whole 60 Hz ticks.
static func attack_charge_sec(element: int) -> float:
	var def: Dictionary = Moves.DEFS.get(ATTACK_MOVES[clampi(element, 0, ATTACK_MOVES.size() - 1)], {})
	var t := maxf(float(def.get("startup", 0.0)), Moves.HOLD_THRESHOLD)
	return ceilf(t / Sim.DT - 0.001) * Sim.DT


## Seconds the attack ring takes to fill (the "charged" cue): the running attack's element
## when the game reports one, else the selected element.
func charge_time() -> float:
	if charge_hold_sec > 0.0:
		return charge_hold_sec
	return attack_charge_sec(_ctx_attack_element if _ctx_attack_element >= 0 else _ctx_element)


## Attack ring fill (0..1) while ATTACK is held. With the game's "attack_charge" context it
## follows the sim's attack action (a press still buffered or dropped shows empty), so a full
## ring always means the sim commits the charge; standalone it times itself from touch-down.
func attack_ring_fill() -> float:
	if _btn_finger[TouchLayout.Id.ATTACK] == -1:
		return 0.0
	var t := _attack_t
	if _ctx_has_charge:
		t = _ctx_attack_charge if _attack_sent else 0.0
	var ct := maxf(charge_time(), 0.01)
	return 1.0 if t >= ct - CHARGE_EPS else clampf(t / ct, 0.0, 1.0)


## Write this tick's input into `f` (all touch-owned fields are overwritten)
## and clear the latched edges. Call once per simulation tick.
func fill_frame(f: InputFrame) -> void:
	f.move = _stick_vec
	f.cam_delta = _cam_accum
	_cam_accum = Vector2.ZERO

	f.attack_pressed = _l_attack_pressed
	if _l_attack_pressed:
		_attack_sent = true
	f.attack_released = _l_attack_released
	f.attack_held = _btn_finger[TouchLayout.Id.ATTACK] != -1
	f.guard_pressed = _l_guard_pressed
	f.guard_released = _l_guard_released
	f.guard_held = _btn_finger[TouchLayout.Id.GUARD] != -1
	f.evade_pressed = _l_evade

	var tech_active := _tech_finger != -1 and not _tech_cancelled
	f.tech_pressed = _l_tech_pressed
	f.tech_released = _l_tech_released
	f.tech_held = tech_active
	f.tech_cancel = _l_tech_cancel
	if tech_active or _l_tech_released:
		f.tech_aim = _tech_aim
		f.tech_aim_active = _tech_aim_active
	else:
		f.tech_aim = Vector2.ZERO
		f.tech_aim_active = false
	if not tech_active:
		_tech_aim = Vector2.ZERO
		_tech_aim_active = false

	f.element_select = _l_element
	f.target_cycle = _l_target
	f.pause_pressed = _l_pause

	_l_attack_pressed = false
	_l_attack_released = false
	_l_guard_pressed = false
	_l_guard_released = false
	_l_evade = false
	_l_tech_pressed = false
	_l_tech_released = false
	_l_tech_cancel = false
	_l_element = -1
	_l_target = false
	_l_pause = false


## Release every finger safely: stick -> 0, held buttons send released,
## a held technique sends tech_cancel (never a commit).
func release_all(cancelled: bool = true) -> void:
	for i in MAX_FINGERS:
		if _role[i] != ROLE_NONE:
			_release_finger(i, cancelled, _f_pos[i])
	# Defensive: anything still marked held after the loop.
	_stick_finger = -1
	_stick_vec = Vector2.ZERO
	_cam_finger = -1
	for id in TouchLayout.COUNT:
		if _btn_finger[id] != -1:
			_btn_finger[id] = -1
	if _tech_finger != -1:
		if not _tech_cancelled:
			_l_tech_cancel = true
		_tech_finger = -1
		_tech_cancelled = false
	queue_redraw()


## Number of fingers currently owning something (tests / debug HUD).
func active_finger_count() -> int:
	var n := 0
	for i in MAX_FINGERS:
		if _role[i] != ROLE_NONE:
			n += 1
	return n


func finger_role(index: int) -> int:
	if index < 0 or index >= MAX_FINGERS:
		return ROLE_NONE
	return _role[index]


func is_technique_cancelled() -> bool:
	return _tech_cancelled


# --- engine hooks -----------------------------------------------------------------------------

func _notification(what: int) -> void:
	match what:
		NOTIFICATION_APPLICATION_FOCUS_OUT, NOTIFICATION_APPLICATION_PAUSED, NOTIFICATION_WM_WINDOW_FOCUS_OUT, NOTIFICATION_PAUSED:
			release_all(true)
		NOTIFICATION_VISIBILITY_CHANGED:
			if not is_visible_in_tree():
				release_all(true)
		NOTIFICATION_RESIZED:
			if is_inside_tree():
				relayout()


func _input(event: InputEvent) -> void:
	if not is_visible_in_tree():
		return
	if event is InputEventScreenTouch:
		var t := event as InputEventScreenTouch
		if t.pressed:
			_touch_down(t.index, t.position)
		else:
			_touch_up(t.index, t.position, t.canceled)
		_mark_handled()
	elif event is InputEventScreenDrag:
		var d := event as InputEventScreenDrag
		_touch_drag(d.index, d.position)
		_mark_handled()
	elif mouse_emulation:
		if event is InputEventMouseButton and (event as InputEventMouseButton).button_index == MOUSE_BUTTON_LEFT:
			var mb := event as InputEventMouseButton
			if mb.pressed:
				_touch_down(MOUSE_FINGER, mb.position)
			else:
				_touch_up(MOUSE_FINGER, mb.position, false)
			_mark_handled()
		elif event is InputEventMouseMotion and _role[MOUSE_FINGER] != ROLE_NONE:
			_touch_drag(MOUSE_FINGER, (event as InputEventMouseMotion).position)
			_mark_handled()


func _mark_handled() -> void:
	var vp := get_viewport()
	if vp != null:
		vp.set_input_as_handled()


func _process(delta: float) -> void:
	var reduced := settings != null and settings.reduced_motion
	var dirty := false
	# Press feedback: instant on, quick ease off.
	for i in TouchLayout.COUNT:
		var p := _vis_press[i]
		if _btn_finger[i] != -1:
			if p < 1.0:
				_vis_press[i] = 1.0
				dirty = true
		elif p > 0.0:
			_vis_press[i] = 0.0 if reduced else maxf(0.0, p - delta * 7.0)
			dirty = true
	for e in UiStyle.ELEMENT_COUNT:
		if _chip_flash[e] > 0.0:
			_chip_flash[e] = maxf(0.0, _chip_flash[e] - delta * 4.0)
			dirty = true
	if _stick_finger != -1:
		_stick_alpha = 1.0
	elif _stick_alpha > 0.0:
		_stick_alpha = 0.0 if reduced else maxf(0.0, _stick_alpha - delta * 3.5)
		dirty = true
	var tech_live := _tech_finger != -1 and not _tech_cancelled
	var cancel_target := 1.0 if tech_live else 0.0
	if not is_equal_approx(_cancel_alpha, cancel_target):
		var step := delta * (6.0 if tech_live else 5.0)
		if reduced:
			_cancel_alpha = cancel_target
		else:
			_cancel_alpha = move_toward(_cancel_alpha, cancel_target, step)
		dirty = true
	if tech_live:
		var near := _layout.radii[TouchLayout.Id.CANCEL] * 1.9
		var dist := _tech_pos.distance_to(_layout.centers[TouchLayout.Id.CANCEL])
		var want := clampf(1.0 - (dist - _layout.radii[TouchLayout.Id.CANCEL]) / maxf(near, 1.0), 0.0, 1.0)
		_cancel_near = lerpf(_cancel_near, want, minf(1.0, delta * 14.0))
		dirty = true
	else:
		_cancel_near = 0.0
	if _btn_finger[TouchLayout.Id.ATTACK] != -1:
		_attack_t += delta
		dirty = true
	else:
		_attack_t = 0.0
	if _stick_finger != -1 or _cam_finger != -1 or (settings != null and settings.show_debug):
		dirty = true
	if dirty:
		queue_redraw()


# --- touch state machine --------------------------------------------------------------------------

func _touch_down(idx: int, pos: Vector2) -> void:
	if idx < 0 or idx >= MAX_FINGERS:
		return
	if _role[idx] != ROLE_NONE:
		# Index re-used without an up event: treat the old finger as cancelled.
		_release_finger(idx, true, _f_pos[idx])
	_f_pos[idx] = pos
	# Second tap on the cancel chip while a technique is held.
	if _tech_finger != -1 and not _tech_cancelled and _layout.in_cancel_zone(pos):
		_cancel_technique()
		_role[idx] = ROLE_DEAD
		return
	var hit := _layout.hit_test(pos)
	if hit != TouchLayout.NONE:
		_press_button(idx, hit, pos)
	elif _layout.is_stick_side(pos):
		if _stick_finger == -1:
			_begin_stick(idx, pos)
		else:
			_role[idx] = ROLE_DEAD
	else:
		if _cam_finger == -1:
			_cam_finger = idx
			_role[idx] = ROLE_CAMERA
		else:
			_role[idx] = ROLE_DEAD
	queue_redraw()


func _touch_drag(idx: int, pos: Vector2) -> void:
	if idx < 0 or idx >= MAX_FINGERS:
		return
	var prev := _f_pos[idx]
	_f_pos[idx] = pos
	match _role[idx]:
		ROLE_STICK:
			_update_stick(pos)
		ROLE_CAMERA:
			_apply_camera_drag(pos - prev)
		ROLE_BUTTON:
			if _f_btn[idx] == TouchLayout.Id.TECH and idx == _tech_finger:
				_update_tech_aim(pos)


func _touch_up(idx: int, pos: Vector2, cancelled: bool) -> void:
	if idx < 0 or idx >= MAX_FINGERS:
		return
	if _role[idx] == ROLE_NONE:
		return
	_release_finger(idx, cancelled, pos)
	queue_redraw()


func _release_finger(idx: int, cancelled: bool, pos: Vector2) -> void:
	var role := _role[idx]
	_role[idx] = ROLE_NONE
	match role:
		ROLE_STICK:
			_stick_finger = -1
			_stick_vec = Vector2.ZERO
		ROLE_CAMERA:
			_cam_finger = -1
		ROLE_BUTTON:
			var id := _f_btn[idx]
			_btn_finger[id] = -1
			match id:
				TouchLayout.Id.ATTACK:
					_l_attack_released = true
				TouchLayout.Id.GUARD:
					_l_guard_released = true
				TouchLayout.Id.TECH:
					if idx == _tech_finger:
						if not _tech_cancelled:
							if cancelled:
								_l_tech_cancel = true
							else:
								_l_tech_released = true
						_tech_finger = -1
						_tech_cancelled = false
				TouchLayout.Id.PAUSE:
					var reach := _layout.hit_radii[TouchLayout.Id.PAUSE] * PAUSE_RELEASE_SLACK
					if not cancelled and pos.distance_to(_layout.centers[TouchLayout.Id.PAUSE]) <= reach:
						_l_pause = true
						pause_requested.emit()


func _press_button(idx: int, id: int, pos: Vector2) -> void:
	if _btn_finger[id] != -1:
		_role[idx] = ROLE_DEAD
		return
	var tech_live := _tech_finger != -1 and not _tech_cancelled
	match id:
		TouchLayout.Id.ATTACK:
			_own_button(idx, id)
			_l_attack_pressed = true
			_attack_t = 0.0
			_attack_sent = false
		TouchLayout.Id.GUARD:
			_own_button(idx, id)
			_l_guard_pressed = true
		TouchLayout.Id.EVADE:
			_own_button(idx, id)
			_l_evade = true
		TouchLayout.Id.TECH:
			if _tech_finger != -1:
				_role[idx] = ROLE_DEAD
				return
			_own_button(idx, id)
			_tech_finger = idx
			_tech_origin = pos
			_tech_pos = pos
			_tech_aim = Vector2.ZERO
			_tech_aim_active = false
			_tech_cancelled = false
			_l_tech_pressed = true
		TouchLayout.Id.TARGET:
			_own_button(idx, id)
			_l_target = true
		TouchLayout.Id.PAUSE:
			_own_button(idx, id)
		_:
			var e := id - TouchLayout.Id.ELEM_0
			if e < 0 or e >= UiStyle.ELEMENT_COUNT or tech_live or not is_element_unlocked(e):
				_role[idx] = ROLE_DEAD
				return
			_own_button(idx, id)
			_l_element = e
			_ctx_element = e
			_chip_flash[e] = 1.0
			Haptics.play("light")
			element_changed.emit(e)


func _own_button(idx: int, id: int) -> void:
	_role[idx] = ROLE_BUTTON
	_f_btn[idx] = id
	_btn_finger[id] = idx
	_vis_press[id] = 1.0


func _begin_stick(idx: int, pos: Vector2) -> void:
	_stick_finger = idx
	_role[idx] = ROLE_STICK
	_stick_center = pos
	_stick_knob = pos
	_stick_vec = Vector2.ZERO
	_stick_alpha = 1.0


func _update_stick(pos: Vector2) -> void:
	var r := _layout.stick_radius
	var d := pos - _stick_center
	var len := d.length()
	if len > r:
		# The base follows the thumb so direction changes stay quick.
		_stick_center += d * ((len - r) / len)
		d = pos - _stick_center
		len = r
	_stick_knob = _stick_center + d
	var mag := len / r
	if mag < DEADZONE:
		_stick_vec = Vector2.ZERO
	else:
		var scaled := minf((mag - DEADZONE) / (1.0 - DEADZONE), 1.0)
		_stick_vec = Vector2(d.x, -d.y) / len * scaled


func _apply_camera_drag(delta_px: Vector2) -> void:
	var s := settings
	var sens := s.camera_sensitivity if s != null else 1.0
	var k := CAM_RAD_PER_MM / _layout.ppm * sens
	var inv := -1.0 if (s != null and s.invert_y) else 1.0
	_cam_accum.x += delta_px.x * k
	_cam_accum.y += -delta_px.y * k * inv


func _update_tech_aim(pos: Vector2) -> void:
	if _tech_cancelled:
		return
	_tech_pos = pos
	var d := pos - _tech_origin
	var aim := Vector2(d.x, -d.y) / _layout.aim_radius
	if aim.length_squared() > 1.0:
		aim = aim.normalized()
	_tech_aim = aim
	if not _tech_aim_active and d.length() > maxf(AIM_ACTIVE_PX, AIM_ACTIVE_MM * _layout.ppm):
		_tech_aim_active = true
	if _layout.in_cancel_zone(pos):
		_cancel_technique()


func _cancel_technique() -> void:
	if _tech_finger == -1 or _tech_cancelled:
		return
	_tech_cancelled = true
	_l_tech_cancel = true
	_tech_aim = Vector2.ZERO
	_tech_aim_active = false


# --- drawing ----------------------------------------------------------------------------------------------

func _opacity() -> float:
	return settings.control_opacity if settings != null else 0.8


func _draw() -> void:
	if _font == null:
		_font = ThemeDB.fallback_font
	var op := _opacity()
	var ppm := _layout.ppm
	var rw := maxf(1.6, 0.2 * ppm)
	var strong := settings != null and settings.strong_labels
	var reduced := settings != null and settings.reduced_motion
	var tech_live := _tech_finger != -1 and not _tech_cancelled

	_draw_stick(op, rw)
	_draw_camera_touch(op, rw)

	# Element chips.
	var chip_dim := 0.1 if tech_live else 1.0
	for e in UiStyle.ELEMENT_COUNT:
		_draw_chip(e, op * chip_dim, rw, reduced)

	_draw_round_button(TouchLayout.Id.GUARD, UiStyle.Glyph.GUARD, "GUARD", Color.WHITE, op, rw, strong, reduced, 1.0)
	_draw_round_button(TouchLayout.Id.EVADE, UiStyle.Glyph.EVADE, "EVADE", Color.WHITE, op, rw, strong, reduced, 1.0)
	_draw_attack(op, rw, strong, reduced)
	_draw_technique(op, rw, strong, reduced)
	_draw_round_button(TouchLayout.Id.TARGET, UiStyle.Glyph.TARGET, "", Color.WHITE, op * 0.9, rw, false, reduced, 1.0)
	_draw_round_button(TouchLayout.Id.PAUSE, UiStyle.Glyph.PAUSE, "", Color.WHITE, op * 0.9, rw, false, reduced, 1.0)

	_draw_tech_aim(rw)
	_draw_cancel_zone(rw)
	if settings != null and settings.show_debug:
		_draw_debug()


func _draw_stick(op: float, rw: float) -> void:
	var r := _layout.stick_radius
	# Resting hint, always faintly there so the left thumb finds its zone.
	if _stick_finger == -1:
		var hint := op * 0.22 * (1.0 - _stick_alpha)
		var gc := _layout.stick_ghost
		draw_arc(gc, r * 0.9, 0.0, TAU, 56, Color(1, 1, 1, hint), rw * 0.8, true)
		draw_circle(gc, r * 0.2, Color(1, 1, 1, hint * 0.8))
	var a := _stick_alpha
	if a <= 0.0:
		return
	var c := _stick_center
	draw_circle(c, r * 1.02, Color(0, 0, 0, 0.12 * a * op))
	draw_circle(c, r, Color(1, 1, 1, 0.05 * a))
	draw_arc(c, r, 0.0, TAU, 64, Color(1, 1, 1, 0.5 * a * op), rw, true)
	draw_arc(c, r * 0.46, 0.0, TAU, 40, Color(1, 1, 1, 0.12 * a * op), rw * 0.7, true)
	var k := _stick_knob
	draw_circle(k, r * 0.44, Color(0, 0, 0, 0.18 * a * op))
	draw_circle(k, r * 0.40, Color(1, 1, 1, 0.26 * a * minf(1.0, op + 0.2)))
	draw_arc(k, r * 0.40, 0.0, TAU, 40, Color(1, 1, 1, 0.85 * a * minf(1.0, op + 0.15)), rw, true)


func _draw_camera_touch(op: float, rw: float) -> void:
	if _cam_finger == -1:
		return
	var p := _f_pos[_cam_finger]
	var r := 2.4 * _layout.ppm
	draw_arc(p, r, 0.0, TAU, 28, Color(1, 1, 1, 0.3 * op), rw * 0.8, true)
	draw_circle(p, r * 0.16, Color(1, 1, 1, 0.4 * op))


func _draw_round_button(id: int, glyph: int, label: String, tint: Color, op: float, rw: float, strong: bool, reduced: bool, enabled_alpha: float) -> void:
	var c := _layout.centers[id]
	var p := _vis_press[id]
	var r := _layout.radii[id] * (1.0 - (0.0 if reduced else 0.07) * p)
	var a := lerpf(op, 1.0, p) * enabled_alpha
	draw_circle(c, r * 1.1, Color(0, 0, 0, 0.14 * a))
	draw_circle(c, r - rw * 0.4, Color(0.04, 0.05, 0.07, 0.40 * a).lerp(Color(1, 1, 1, 0.26), p))
	draw_arc(c, r, 0.0, TAU, 56, Color(0, 0, 0, 0.26 * a), rw * 2.6, true)
	draw_arc(c, r, 0.0, TAU, 56, Color(tint.r, tint.g, tint.b, lerpf(0.5, 1.0, p) * a), rw * lerpf(1.0, 1.35, p), true)
	draw_arc(c, r * 0.84, 0.0, TAU, 48, Color(tint.r, tint.g, tint.b, 0.1 * a), rw * 0.7, true)
	var gy := -0.2 * r if strong and label != "" else 0.0
	var gr := r * (0.38 if strong and label != "" else 0.5)
	_glyph(glyph, c + Vector2(0, gy), gr, Color(tint.r, tint.g, tint.b, lerpf(0.82, 1.0, p) * a), rw * 1.05)
	if strong and label != "":
		_draw_label(label, c + Vector2(0, r * 0.7), r * 0.25, Color(1, 1, 1, a), r * 1.7)


func _draw_attack(op: float, rw: float, strong: bool, reduced: bool) -> void:
	var id := TouchLayout.Id.ATTACK
	_draw_round_button(id, UiStyle.Glyph.ATTACK, "ATTACK", Color.WHITE, op, rw, strong, reduced, 1.0)
	if _btn_finger[id] != -1:
		var c := _layout.centers[id]
		var r := _layout.radii[id]
		var f := attack_ring_fill()
		if f > 0.05:
			var col := Color(1, 1, 1, 0.9)
			if f >= 1.0:
				col = Color(1.0, 0.92, 0.7, 1.0)
			draw_arc(c, r * 1.16, -PI * 0.5, -PI * 0.5 + TAU * f, 64, col, rw * 1.5, true)


func _draw_technique(op: float, rw: float, strong: bool, reduced: bool) -> void:
	var id := TouchLayout.Id.TECH
	var c := _layout.centers[id]
	var p := _vis_press[id]
	var r := _layout.radii[id] * (1.0 - (0.0 if reduced else 0.07) * p)
	var col := UiStyle.element_color(_ctx_element)
	var avail := 1.0 if _ctx_available else 0.5
	var a := lerpf(op, 1.0, p) * avail
	draw_circle(c, r * 1.1, Color(0, 0, 0, 0.14 * a))
	draw_circle(c, r - rw * 0.4, Color(0.04, 0.05, 0.07, 0.40 * a).lerp(Color(col.r, col.g, col.b, 0.3), p))
	draw_arc(c, r, 0.0, TAU, 56, Color(0, 0, 0, 0.26 * a), rw * 2.6, true)
	draw_arc(c, r, 0.0, TAU, 56, Color(col.r, col.g, col.b, lerpf(0.62, 1.0, p) * a), rw * lerpf(1.1, 1.5, p), true)
	draw_arc(c, r * 0.84, 0.0, TAU, 48, Color(col.r, col.g, col.b, 0.12 * a), rw * 0.7, true)
	if _ctx_holding:
		draw_circle(c, r * 0.9, Color(col.r, col.g, col.b, 0.14 * a))
	var has_label := _ctx_label != ""
	var gy := -0.2 * r if has_label else 0.0
	var gr := r * (0.4 if has_label else 0.5)
	_glyph(_ctx_element, c + Vector2(0, gy), gr, Color(col.r, col.g, col.b, 0.95 * a), rw * 1.1)
	if has_label:
		_draw_label(_ctx_label, c + Vector2(0, r * 0.64), r * (0.27 if strong else 0.24), Color(1, 1, 1, (0.95 if strong else 0.72) * a), r * 1.7)


func _draw_chip(e: int, op: float, rw: float, reduced: bool) -> void:
	var id := TouchLayout.Id.ELEM_0 + e
	var c := _layout.centers[id]
	var p := _vis_press[id]
	var flash := _chip_flash[e]
	var r := _layout.radii[id] * (1.0 - (0.0 if reduced else 0.08) * p)
	var col := UiStyle.element_color(e)
	var unlocked := _ctx_unlocked[e] == 1
	var selected := e == _ctx_element
	var a := op * (1.0 if unlocked else 0.38)
	a = lerpf(a, minf(1.0, a + 0.4), maxf(p, flash))
	draw_circle(c, r * 1.12, Color(0, 0, 0, 0.12 * a))
	var fill := Color(0.04, 0.05, 0.07, 0.38 * a)
	if selected and unlocked:
		fill = Color(col.r, col.g, col.b, 0.30 * a)
	draw_circle(c, r - rw * 0.4, fill)
	draw_arc(c, r, 0.0, TAU, 40, Color(0, 0, 0, 0.24 * a), rw * 2.4, true)
	var ring_w := rw * (1.6 if (selected and unlocked) else 0.9)
	var ring_a := (0.95 if (selected and unlocked) else 0.5) * a
	draw_arc(c, r, 0.0, TAU, 40, Color(col.r, col.g, col.b, ring_a), ring_w, true)
	var gcol := Color(col.r, col.g, col.b, (0.95 if unlocked else 0.6) * a)
	_glyph(e, c, r * 0.5, gcol, rw * 0.95)
	if not unlocked:
		# Locked: a thin strike so it reads as unavailable even without colour.
		draw_line(c + Vector2(-r, r) * 0.55, c + Vector2(r, -r) * 0.55, Color(1, 1, 1, 0.45 * a), rw * 0.8, true)


## Glyph with a soft dark halo so it stays legible on bright scenes.
func _glyph(glyph: int, c: Vector2, r: float, col: Color, w: float) -> void:
	UiStyle.draw_glyph(self, glyph, c, r, Color(0, 0, 0, 0.30 * col.a), w * 2.4)
	UiStyle.draw_glyph(self, glyph, c, r, col, w)


func _draw_label(text: String, base: Vector2, size_px: float, col: Color, width: float) -> void:
	var fs := maxi(8, int(round(size_px)))
	var pos := base + Vector2(-width * 0.5, 0)
	var outline := Color(0, 0, 0, col.a * 0.55)
	draw_string_outline(_font, pos, text, HORIZONTAL_ALIGNMENT_CENTER, width, fs, maxi(2, fs / 5), outline)
	draw_string(_font, pos, text, HORIZONTAL_ALIGNMENT_CENTER, width, fs, col)


func _draw_tech_aim(rw: float) -> void:
	if _tech_finger == -1 or _tech_cancelled:
		return
	var col := UiStyle.element_color(_ctx_element)
	var o := _tech_origin
	var r := _layout.aim_radius
	var op := _opacity()
	draw_arc(o, r, 0.0, TAU, 56, Color(col.r, col.g, col.b, 0.28 * minf(1.0, op + 0.3)), rw * 0.8, true)
	var d := _tech_pos - o
	if d.length() > r:
		d = d.normalized() * r
	var k := o + d
	if d.length() > 2.0:
		draw_line(o, k, Color(col.r, col.g, col.b, 0.35), rw * 0.8, true)
	draw_circle(k, r * 0.16, Color(col.r, col.g, col.b, 0.55 if _tech_aim_active else 0.3))
	draw_arc(k, r * 0.16, 0.0, TAU, 20, Color(col.r, col.g, col.b, 0.95), rw * 0.9, true)


func _draw_cancel_zone(rw: float) -> void:
	var a := _cancel_alpha
	if a <= 0.01:
		return
	var id := TouchLayout.Id.CANCEL
	var c := _layout.centers[id]
	var r := _layout.radii[id]
	var near := _cancel_near
	var danger := UiStyle.DANGER
	draw_circle(c, r, Color(0.05, 0.05, 0.07, 0.62 * a))
	draw_circle(c, r, Color(danger.r, danger.g, danger.b, (0.10 + 0.12 * near) * a))
	draw_arc(c, r, 0.0, TAU, 48, Color(danger.r, danger.g, danger.b, (0.55 + 0.4 * near) * a), rw * (1.0 + 0.4 * near), true)
	UiStyle.draw_glyph(self, UiStyle.Glyph.CANCEL, c + Vector2(0, -r * 0.14), r * 0.34, Color(1, 1, 1, 0.9 * a), rw * 1.1)
	_draw_label("CANCEL", c + Vector2(0, r * 0.66), r * 0.26, Color(1, 1, 1, 0.85 * a), r * 2.0)


func _draw_debug() -> void:
	var yellow := Color(1, 0.9, 0.2, 0.5)
	var split := _layout.split_x
	draw_line(Vector2(split, 0), Vector2(split, size.y), yellow, 1.0)
	draw_rect(_layout.usable, Color(0.3, 1, 0.5, 0.35), false, 1.0)
	for i in TouchLayout.COUNT:
		draw_arc(_layout.centers[i], _layout.hit_radii[i], 0.0, TAU, 32, yellow, 1.0, true)
	draw_arc(_layout.stick_ghost, _layout.stick_radius, 0.0, TAU, 32, yellow, 1.0, true)
	for i in MAX_FINGERS:
		if _role[i] != ROLE_NONE:
			var p := _f_pos[i]
			draw_circle(p, 6.0, Color(1, 0.3, 0.3, 0.8))
			draw_string(_font, p + Vector2(10, -10), "%d:%d" % [i, _role[i]], HORIZONTAL_ALIGNMENT_LEFT, -1, 16, Color(1, 0.6, 0.6))
	var txt := "move %.2f,%.2f  tech aim %.2f,%.2f%s" % [_stick_vec.x, _stick_vec.y, _tech_aim.x, _tech_aim.y, " cancelled" if _tech_cancelled else ""]
	draw_string(_font, Vector2(_layout.usable.position.x + 12, _layout.usable.position.y + 28), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, 18, Color(1, 1, 0.6, 0.9))
