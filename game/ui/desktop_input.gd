class_name DesktopInput
extends Node
## Keyboard / mouse / gamepad producer for desktop testing and controllers.
## Registers its own InputMap actions at runtime ("ff_*"), so project.godot
## needs no input section. Edges are latched from _input (and re-scanned at
## poll time) so a tap shorter than one tick is never lost.
##
## Keyboard: WASD move, arrow keys camera, J attack, K guard, Space evade,
##   L technique (hold; arrow keys aim while held), 1-4 elements, Tab target,
##   Esc / Backspace cancel the technique, otherwise pause.
## Mouse: LMB attack, RMB technique (hold, mouse movement aims), middle-drag
##   camera, F1 toggles captured mouse-look.
## Gamepad: left stick move, right stick camera (aim while technique held),
##   X attack, RB guard, B evade, RT technique, d-pad elements, Y target,
##   Start pause, LB cancel.
##
## Mouse-from-touch emulation events (device == DEVICE_ID_EMULATION) are
## ignored so a finger tap can never act as a mouse click here.

signal pause_requested

enum A { ATTACK, GUARD, EVADE, TECH, CANCEL, PAUSE, ELEM_0, ELEM_1, ELEM_2, ELEM_3, TARGET }
const COUNT := 11
const ACTIONS: Array[StringName] = [
	&"ff_attack", &"ff_guard", &"ff_evade", &"ff_tech", &"ff_cancel", &"ff_pause",
	&"ff_elem_1", &"ff_elem_2", &"ff_elem_3", &"ff_elem_4", &"ff_target",
]
const MOVE_L := &"ff_move_left"
const MOVE_R := &"ff_move_right"
const MOVE_U := &"ff_move_up"
const MOVE_D := &"ff_move_down"
const CAM_L := &"ff_cam_left"
const CAM_R := &"ff_cam_right"
const CAM_U := &"ff_cam_up"
const CAM_D := &"ff_cam_down"

## Gamepad / key camera speed at full deflection, radians per second.
const CAM_RATE := 2.6
## Mouse camera: radians per pixel at sensitivity 1.
const MOUSE_RAD_PER_PX := 0.0032
const AIM_ACTIVE_PX := 12.0
const AIM_ACTIVE_STICK := 0.3

var settings: GameSettings = null
## Disable mouse buttons / motion (the touch HUD is emulating the mouse).
var mouse_enabled: bool = true
## Test hook: when > 0, poll uses this dt for rate-based camera input.
var fixed_dt: float = -1.0

var _unlocked := PackedByteArray([1, 1, 1, 1])
var _prev := PackedByteArray()
var _mouse_state := PackedByteArray()
var _mmb_drag := false
var _p_latch := PackedByteArray()
var _r_latch := PackedByteArray()
var _tech_down := false
var _tech_cancelled := false
var _tech_aim_active := false
## True when the technique action fell while cancelled (so no commit is sent).
var _tech_cancelled_at_release := false
var _mouse_aim := Vector2.ZERO
var _l_tech_cancel := false
var _l_element := -1
var _l_pause := false
var _cam_accum := Vector2.ZERO
var _last_usec := 0


func _init() -> void:
	register_actions()
	_prev.resize(COUNT)
	_mouse_state.resize(COUNT)
	_p_latch.resize(COUNT)
	_r_latch.resize(COUNT)
	_last_usec = Time.get_ticks_usec()


func _ready() -> void:
	if settings == null:
		settings = GameSettings.current()


## Idempotent: adds the "ff_*" actions to the InputMap.
static func register_actions() -> void:
	if InputMap.has_action(ACTIONS[0]) and InputMap.has_action(CAM_D):
		return
	_axis_action(MOVE_L, KEY_A, JOY_AXIS_LEFT_X, -1.0)
	_axis_action(MOVE_R, KEY_D, JOY_AXIS_LEFT_X, 1.0)
	_axis_action(MOVE_U, KEY_W, JOY_AXIS_LEFT_Y, -1.0)
	_axis_action(MOVE_D, KEY_S, JOY_AXIS_LEFT_Y, 1.0)
	_axis_action(CAM_L, KEY_LEFT, JOY_AXIS_RIGHT_X, -1.0)
	_axis_action(CAM_R, KEY_RIGHT, JOY_AXIS_RIGHT_X, 1.0)
	_axis_action(CAM_U, KEY_UP, JOY_AXIS_RIGHT_Y, -1.0)
	_axis_action(CAM_D, KEY_DOWN, JOY_AXIS_RIGHT_Y, 1.0)
	_button_action(&"ff_attack", [KEY_J], [JOY_BUTTON_X])
	_button_action(&"ff_guard", [KEY_K], [JOY_BUTTON_RIGHT_SHOULDER])
	_button_action(&"ff_evade", [KEY_SPACE], [JOY_BUTTON_B])
	_button_action(&"ff_tech", [KEY_L], [])
	_add_event(&"ff_tech", _joy_axis(JOY_AXIS_TRIGGER_RIGHT, 1.0))
	_button_action(&"ff_cancel", [], [JOY_BUTTON_LEFT_SHOULDER])
	_button_action(&"ff_pause", [KEY_ESCAPE, KEY_BACKSPACE], [JOY_BUTTON_START])
	_button_action(&"ff_elem_1", [KEY_1], [JOY_BUTTON_DPAD_LEFT])
	_button_action(&"ff_elem_2", [KEY_2], [JOY_BUTTON_DPAD_DOWN])
	_button_action(&"ff_elem_3", [KEY_3], [JOY_BUTTON_DPAD_RIGHT])
	_button_action(&"ff_elem_4", [KEY_4], [JOY_BUTTON_DPAD_UP])
	_button_action(&"ff_target", [KEY_TAB], [JOY_BUTTON_Y])


static func _ensure(action: StringName, deadzone: float) -> void:
	if InputMap.has_action(action):
		InputMap.erase_action(action)
	InputMap.add_action(action, deadzone)


static func _add_event(action: StringName, ev: InputEvent) -> void:
	InputMap.action_add_event(action, ev)


static func _key(code: int) -> InputEventKey:
	var k := InputEventKey.new()
	k.physical_keycode = code as Key
	return k


static func _joy_axis(axis: int, value: float) -> InputEventJoypadMotion:
	var m := InputEventJoypadMotion.new()
	m.axis = axis as JoyAxis
	m.axis_value = value
	return m


static func _axis_action(action: StringName, key: int, axis: int, dir: float) -> void:
	_ensure(action, 0.22)
	_add_event(action, _key(key))
	_add_event(action, _joy_axis(axis, dir))


static func _button_action(action: StringName, keys: Array, buttons: Array) -> void:
	_ensure(action, 0.4)
	for k in keys:
		_add_event(action, _key(int(k)))
	for b in buttons:
		var jb := InputEventJoypadButton.new()
		jb.button_index = int(b) as JoyButton
		_add_event(action, jb)


# --- public API ---------------------------------------------------------------------------------

func set_unlocked(elements: Array) -> void:
	_unlocked.fill(0)
	for e in elements:
		var ei := int(e)
		if ei >= 0 and ei < 4:
			_unlocked[ei] = 1


func is_technique_active() -> bool:
	return _tech_down and not _tech_cancelled


## Write this tick's desktop input into `f` (overwrites every field).
func fill_frame(f: InputFrame) -> void:
	_scan_edges()
	var s := settings if settings != null else GameSettings.current()
	var now := Time.get_ticks_usec()
	var dt := fixed_dt if fixed_dt > 0.0 else clampf((now - _last_usec) / 1000000.0, 0.0, 0.1)
	_last_usec = now

	f.move = Input.get_vector(MOVE_L, MOVE_R, MOVE_D, MOVE_U)
	var stick := Input.get_vector(CAM_L, CAM_R, CAM_D, CAM_U)
	var tech_active := _tech_down and not _tech_cancelled

	f.attack_pressed = _p_latch[A.ATTACK] == 1
	f.attack_released = _r_latch[A.ATTACK] == 1
	f.attack_held = _prev[A.ATTACK] == 1
	f.guard_pressed = _p_latch[A.GUARD] == 1
	f.guard_released = _r_latch[A.GUARD] == 1
	f.guard_held = _prev[A.GUARD] == 1
	f.evade_pressed = _p_latch[A.EVADE] == 1

	f.tech_pressed = _p_latch[A.TECH] == 1
	f.tech_released = _r_latch[A.TECH] == 1 and not _tech_cancelled_at_release
	f.tech_held = tech_active
	f.tech_cancel = _l_tech_cancel

	var radius := _aim_radius_px()
	if tech_active or f.tech_released:
		var aim := _mouse_aim / radius + stick
		if aim.length_squared() > 1.0:
			aim = aim.normalized()
		f.tech_aim = aim
		if not _tech_aim_active and (_mouse_aim.length() > AIM_ACTIVE_PX or stick.length() > AIM_ACTIVE_STICK):
			_tech_aim_active = true
		f.tech_aim_active = _tech_aim_active
		f.cam_delta = _cam_accum
	else:
		f.tech_aim = Vector2.ZERO
		f.tech_aim_active = false
		var inv := -1.0 if s.invert_y else 1.0
		var k := CAM_RATE * s.camera_sensitivity * dt
		f.cam_delta = _cam_accum + Vector2(stick.x * k, stick.y * k * inv)
	_cam_accum = Vector2.ZERO
	if not tech_active:
		_tech_aim_active = false
		_mouse_aim = Vector2.ZERO

	f.element_select = _l_element
	f.target_cycle = _p_latch[A.TARGET] == 1
	f.pause_pressed = _l_pause

	_p_latch.fill(0)
	_r_latch.fill(0)
	_l_tech_cancel = false
	_l_element = -1
	_l_pause = false
	_tech_cancelled_at_release = false


## Cancel everything held (focus loss): a held technique is cancelled, never committed.
func cancel_all() -> void:
	_scan_edges()
	if is_technique_active():
		_cancel_technique()
	_mouse_state.fill(0)
	_mmb_drag = false
	for a in ACTIONS:
		if Input.is_action_pressed(a):
			Input.action_release(a)
	_scan_edges()


# --- internals ----------------------------------------------------------------------------------------

func _notification(what: int) -> void:
	match what:
		NOTIFICATION_APPLICATION_FOCUS_OUT, NOTIFICATION_APPLICATION_PAUSED, NOTIFICATION_WM_WINDOW_FOCUS_OUT, NOTIFICATION_PAUSED:
			cancel_all()
		NOTIFICATION_UNPAUSED:
			_resync_edge_actions()


## While the tree was paused we saw no events. Edge-only actions that are
## physically down now (e.g. the Esc that closed the pause menu) must not
## fire again as fresh presses.
func _resync_edge_actions() -> void:
	for i in [A.EVADE, A.CANCEL, A.PAUSE, A.ELEM_0, A.ELEM_1, A.ELEM_2, A.ELEM_3, A.TARGET]:
		_prev[i] = 1 if _pressed_now(i) else 0
		_p_latch[i] = 0


func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		if event.device == InputEvent.DEVICE_ID_EMULATION or not mouse_enabled:
			return
		var rel := (event as InputEventMouseMotion).relative
		if _tech_down:
			if not _tech_cancelled:
				_mouse_aim.x += rel.x
				_mouse_aim.y -= rel.y
		elif _mmb_drag or Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
			var s := settings if settings != null else GameSettings.current()
			var k := MOUSE_RAD_PER_PX * s.camera_sensitivity
			var inv := -1.0 if s.invert_y else 1.0
			_cam_accum += Vector2(rel.x * k, -rel.y * k * inv)
	elif event is InputEventMouseButton:
		if event.device == InputEvent.DEVICE_ID_EMULATION or not mouse_enabled:
			return
		var mb := event as InputEventMouseButton
		match mb.button_index:
			MOUSE_BUTTON_LEFT:
				_mouse_state[A.ATTACK] = 1 if mb.pressed else 0
			MOUSE_BUTTON_RIGHT:
				_mouse_state[A.TECH] = 1 if mb.pressed else 0
			MOUSE_BUTTON_MIDDLE:
				_mmb_drag = mb.pressed
			_:
				return
		_scan_edges()
	elif event is InputEventKey:
		var k := event as InputEventKey
		if k.pressed and not k.echo and k.physical_keycode == KEY_F1:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED else Input.MOUSE_MODE_CAPTURED
		_scan_edges()
	elif event is InputEventJoypadButton or event is InputEventJoypadMotion:
		_scan_edges()


func _pressed_now(i: int) -> bool:
	return _mouse_state[i] == 1 or Input.is_action_pressed(ACTIONS[i])


## Compare every action with its previous state and latch the edges.
func _scan_edges() -> void:
	for i in COUNT:
		var now := _pressed_now(i)
		var was := _prev[i] == 1
		if now == was:
			continue
		_prev[i] = 1 if now else 0
		if now:
			_on_pressed(i)
		else:
			_on_released(i)


func _on_pressed(i: int) -> void:
	match i:
		A.ATTACK, A.GUARD, A.EVADE, A.TARGET:
			_p_latch[i] = 1
		A.TECH:
			_p_latch[i] = 1
			_tech_down = true
			_tech_cancelled = false
			_tech_aim_active = false
			_mouse_aim = Vector2.ZERO
		A.CANCEL:
			if is_technique_active():
				_cancel_technique()
		A.PAUSE:
			if is_technique_active():
				_cancel_technique()
			else:
				_l_pause = true
				pause_requested.emit()
		_:
			var e := i - A.ELEM_0
			if e >= 0 and e < 4 and _unlocked[e] == 1:
				_l_element = e


func _on_released(i: int) -> void:
	match i:
		A.ATTACK, A.GUARD:
			_r_latch[i] = 1
		A.TECH:
			_r_latch[i] = 1
			_tech_cancelled_at_release = _tech_cancelled
			_tech_down = false
			_tech_cancelled = false


func _cancel_technique() -> void:
	_tech_cancelled = true
	_l_tech_cancel = true
	_tech_aim_active = false
	_mouse_aim = Vector2.ZERO


func _aim_radius_px() -> float:
	var vp := get_viewport()
	var h := vp.get_visible_rect().size.y if vp != null else 720.0
	return maxf(minf(0.15 * h, 140.0), 40.0)
