extends RefCounted
## Tiny assertion/test-harness base for the UI suites (no addons needed).
## Suites extend this, define `test_*` methods, and are driven by run_ui_tests.gd.

const PHONE := Vector2(1558, 720)    # 2532x1170 window under canvas_items/expand
const PHONE_PPM := 11.1              # ~460 dpi iPhone
const TEST_CFG := "user://ui_test_settings.cfg"

var host: SceneTree
var checks := 0
var failures: Array[String] = []
var current_test := ""
var settings: GameSettings
var _nodes: Array[Node] = []


func before_each() -> void:
	settings = GameSettings.new()
	settings.storage_path = TEST_CFG
	GameSettings.set_current(settings)
	Haptics.reset_for_tests()
	Haptics.hardware_enabled = false


func after_each() -> void:
	for n in _nodes:
		if is_instance_valid(n):
			if n.get_parent() != null:
				n.get_parent().remove_child(n)
			n.free()
	_nodes.clear()
	host.paused = false
	for a in DesktopInput.ACTIONS:
		if InputMap.has_action(a):
			Input.action_release(a)


# --- assertions -----------------------------------------------------------------------------------

func check(cond: bool, msg: String) -> void:
	checks += 1
	if not cond:
		failures.append("%s: %s" % [current_test, msg])


func check_eq(a: Variant, b: Variant, msg: String) -> void:
	checks += 1
	if a != b:
		failures.append("%s: %s (got %s, expected %s)" % [current_test, msg, str(a), str(b)])


func check_near(a: float, b: float, eps: float, msg: String) -> void:
	checks += 1
	if absf(a - b) > eps:
		failures.append("%s: %s (got %.4f, expected %.4f +- %.4f)" % [current_test, msg, a, b, eps])


func check_vec_near(a: Vector2, b: Vector2, eps: float, msg: String) -> void:
	checks += 1
	if a.distance_to(b) > eps:
		failures.append("%s: %s (got %s, expected %s +- %.4f)" % [current_test, msg, str(a), str(b), eps])


# --- scene helpers --------------------------------------------------------------------------------

func track(n: Node) -> Node:
	_nodes.append(n)
	return n


func make_controls(vp: Vector2 = PHONE, insets: Vector4 = Vector4.ZERO, ppm: float = PHONE_PPM) -> TouchControls:
	var c := TouchControls.new()
	c.settings = settings
	host.root.add_child(c)
	c.configure_layout(vp, insets, ppm)
	track(c)
	return c


func btn(c: TouchControls, id: int) -> Vector2:
	return c.get_layout().centers[id]


## A point in the camera region that is not on any control.
func cam_point(c: TouchControls) -> Vector2:
	var l := c.get_layout()
	var x := l.split_x - 40.0 if l.left_handed else l.split_x + 40.0
	var p := Vector2(x, 140.0)
	check(l.hit_test(p) == TouchLayout.NONE, "cam_point must be free of controls")
	return p


## A point in the stick region that is not on any control.
func stick_point(c: TouchControls) -> Vector2:
	var l := c.get_layout()
	var x := l.viewport_size.x - 220.0 if l.left_handed else 220.0
	var p := Vector2(x, 500.0)
	check(l.hit_test(p) == TouchLayout.NONE, "stick_point must be free of controls")
	return p


# --- event injection (through the real viewport input path) --------------------------------------------

func send(ev: InputEvent) -> void:
	host.root.push_input(ev, true)


func touch_down(idx: int, pos: Vector2) -> void:
	var e := InputEventScreenTouch.new()
	e.index = idx
	e.position = pos
	e.pressed = true
	send(e)


func touch_up(idx: int, pos: Vector2, canceled: bool = false) -> void:
	var e := InputEventScreenTouch.new()
	e.index = idx
	e.position = pos
	e.pressed = false
	e.canceled = canceled
	send(e)


func drag_step(idx: int, pos: Vector2, rel: Vector2) -> void:
	var e := InputEventScreenDrag.new()
	e.index = idx
	e.position = pos
	e.relative = rel
	send(e)


## Drag a finger from `from` to `to` in `steps` events. Returns `to`.
func drag(idx: int, from: Vector2, to: Vector2, steps: int = 5) -> Vector2:
	var prev := from
	for i in range(1, steps + 1):
		var p := from.lerp(to, float(i) / steps)
		drag_step(idx, p, p - prev)
		prev = p
	return to


func poll(c: TouchControls) -> InputFrame:
	var f := InputFrame.new()
	c.fill_frame(f)
	return f


func step(c: TouchControls, seconds: float) -> void:
	var t := 0.0
	while t < seconds - 0.0001:
		c._process(1.0 / 60.0)
		t += 1.0 / 60.0
