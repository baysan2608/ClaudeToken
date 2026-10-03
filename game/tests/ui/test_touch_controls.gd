extends "res://tests/ui/ui_test_case.gd"
## TouchControls behaviour: finger ownership, edges, cancellation, mirroring,
## element chips, settings persistence. Events go through Viewport.push_input.

const Id := TouchLayout.Id


# --- movement stick ---------------------------------------------------------------------------------

func test_stick_relative_deadzone_and_release() -> void:
	var c := make_controls()
	var l := c.get_layout()
	var p := stick_point(c)
	touch_down(0, p)
	var f := poll(c)
	check_vec_near(f.move, Vector2.ZERO, 0.0001, "no movement at touch-down (relative stick)")
	# Inside the 8% dead zone.
	drag_step(0, p + Vector2(l.stick_radius * 0.05, 0), Vector2(l.stick_radius * 0.05, 0))
	check_vec_near(poll(c).move, Vector2.ZERO, 0.0001, "dead zone swallows 5% deflection")
	# Half deflection to the right, rescaled past the dead zone.
	drag_step(0, p + Vector2(l.stick_radius * 0.5, 0), Vector2(l.stick_radius * 0.45, 0))
	var expected := (0.5 - TouchControls.DEADZONE) / (1.0 - TouchControls.DEADZONE)
	f = poll(c)
	check_near(f.move.x, expected, 0.002, "half deflection right")
	check_near(f.move.y, 0.0, 0.001, "no vertical component")
	# Up on screen is +y (forward).
	drag_step(0, p + Vector2(0, -l.stick_radius * 2.0), Vector2.ZERO)
	f = poll(c)
	check(f.move.y > 0.95, "dragging up gives forward movement")
	check(f.move.length() <= 1.0001, "stick length never exceeds 1")
	check(not f.attack_pressed and not f.tech_pressed and f.cam_delta == Vector2.ZERO, "stick finger produces only movement")
	touch_up(0, p)
	check_vec_near(poll(c).move, Vector2.ZERO, 0.0001, "release returns the stick to zero")
	check_eq(c.active_finger_count(), 0, "no fingers left")


func test_stick_base_follows_thumb() -> void:
	var c := make_controls()
	var l := c.get_layout()
	var p := stick_point(c)
	touch_down(0, p)
	# Drag far right, then back toward the start by one radius: base followed,
	# so the stick should already be near zero rather than still saturated.
	drag_step(0, p + Vector2(l.stick_radius * 3.0, 0), Vector2.ZERO)
	check_near(poll(c).move.x, 1.0, 0.001, "saturated at the edge")
	drag_step(0, p + Vector2(l.stick_radius * 2.0, 0), Vector2.ZERO)
	check(poll(c).move.x < 0.05, "base followed the thumb, so one radius back is ~zero")


# --- camera -----------------------------------------------------------------------------------------------

func test_camera_drag_only_produces_cam_delta() -> void:
	var c := make_controls()
	var p := cam_point(c)
	touch_down(1, p)
	drag(1, p, p + Vector2(60, -30), 3)
	var f := poll(c)
	var k := TouchControls.CAM_RAD_PER_MM / PHONE_PPM
	check_near(f.cam_delta.x, 60.0 * k, 0.0005, "yaw from horizontal drag")
	check_near(f.cam_delta.y, 30.0 * k, 0.0005, "dragging up pitches up")
	check(f.move == Vector2.ZERO, "camera drag does not move")
	check(not (f.attack_pressed or f.attack_held or f.tech_pressed or f.tech_held or f.guard_pressed or f.evade_pressed), "camera drag never presses buttons")
	# cam_delta is consumed per tick.
	check_vec_near(poll(c).cam_delta, Vector2.ZERO, 0.00001, "cam_delta cleared after poll")
	touch_up(1, p)


func test_camera_sensitivity_and_invert() -> void:
	settings.camera_sensitivity = 2.0
	settings.invert_y = true
	var c := make_controls()
	var p := cam_point(c)
	touch_down(1, p)
	drag_step(1, p + Vector2(40, -20), Vector2(40, -20))
	var k := TouchControls.CAM_RAD_PER_MM / PHONE_PPM * 2.0
	var f := poll(c)
	check_near(f.cam_delta.x, 40.0 * k, 0.0005, "sensitivity doubles yaw")
	check_near(f.cam_delta.y, -20.0 * k, 0.0005, "invert_y flips pitch")


func test_camera_finger_can_never_press_buttons() -> void:
	var c := make_controls()
	var p := cam_point(c)
	touch_down(1, p)
	# Wander across attack, technique, guard, evade and release on the attack button.
	var path := [Id.TECH, Id.GUARD, Id.EVADE, Id.ATTACK]
	var cur := p
	for id in path:
		cur = drag(1, cur, btn(c, id), 4)
	touch_up(1, cur)
	var f := poll(c)
	check(not f.attack_pressed and not f.attack_released and not f.attack_held, "camera finger never triggers attack")
	check(not f.tech_pressed and not f.tech_released and not f.tech_held, "camera finger never triggers technique")
	check(not f.guard_pressed and not f.evade_pressed, "camera finger never triggers guard/evade")
	check(f.cam_delta != Vector2.ZERO, "camera still rotated")


# --- technique ----------------------------------------------------------------------------------------------

func test_technique_drag_aims_and_never_rotates_camera() -> void:
	var c := make_controls()
	var l := c.get_layout()
	var t := btn(c, Id.TECH)
	touch_down(2, t)
	var f := poll(c)
	check(f.tech_pressed and f.tech_held, "press edge + held")
	check(not f.tech_aim_active, "no aim before dragging")
	# Tiny finger roll: still not an intentional aim.
	drag_step(2, t + Vector2(0, -5), Vector2(0, -5))
	check(not poll(c).tech_aim_active, "5 px roll is not an aim")
	# Real drag: half the aim radius, straight up.
	var up := t + Vector2(0, -l.aim_radius * 0.5)
	drag(2, t + Vector2(0, -5), up, 4)
	f = poll(c)
	check(f.tech_held and not f.tech_pressed, "held without a new press edge")
	check(f.tech_aim_active, "aim active once the drag passes the threshold")
	check_vec_near(f.tech_aim, Vector2(0, 0.5), 0.01, "aim normalised, up on screen = +y")
	check_vec_near(f.cam_delta, Vector2.ZERO, 0.00001, "technique drag never rotates the camera")
	# Drag far to the right: aim saturates at length 1.
	drag(2, up, t + Vector2(l.aim_radius * 2.0, 0), 4)
	f = poll(c)
	check_near(f.tech_aim.length(), 1.0, 0.001, "aim clamped to unit length")
	check(f.tech_aim.x > 0.99, "aim points right")
	check_vec_near(f.cam_delta, Vector2.ZERO, 0.00001, "still no camera rotation")
	check(f.move == Vector2.ZERO, "no movement from technique finger")
	# Release commits (and still reports the final aim in that frame).
	touch_up(2, t + Vector2(l.aim_radius * 2.0, 0))
	f = poll(c)
	check(f.tech_released and not f.tech_cancel and not f.tech_held, "release commits")
	check(f.tech_aim.x > 0.99 and f.tech_aim_active, "final aim delivered with the release")
	f = poll(c)
	check(not f.tech_released and f.tech_aim == Vector2.ZERO and not f.tech_aim_active, "everything cleared next tick")


func test_technique_cancel_by_dragging_into_zone() -> void:
	var c := make_controls()
	var t := btn(c, Id.TECH)
	var z := btn(c, Id.CANCEL)
	touch_down(2, t)
	poll(c)
	# Cancel zone is only reachable by an intentional drag away from the aim ring.
	check(c.get_layout().aim_radius < t.distance_to(z) - c.get_layout().radii[Id.CANCEL], "cancel zone lies outside the aim ring")
	drag(2, t, z, 6)
	var f := poll(c)
	check(f.tech_cancel, "tech_cancel set when dragging into the zone")
	check(not f.tech_held and not f.tech_released, "no hold, no commit")
	check(c.is_technique_cancelled(), "controller remembers the cancel")
	# Keep dragging around, then release: still no commit.
	drag(2, z, z + Vector2(30, 0), 3)
	touch_up(2, z)
	f = poll(c)
	check(not f.tech_released and not f.tech_cancel and not f.tech_held, "release after cancel commits nothing")
	check_vec_near(f.cam_delta, Vector2.ZERO, 0.00001, "cancel drag never rotated the camera")


func test_technique_cancel_by_second_tap_on_chip() -> void:
	var c := make_controls()
	var t := btn(c, Id.TECH)
	var z := btn(c, Id.CANCEL)
	touch_down(2, t)
	poll(c)
	touch_down(5, z)
	var f := poll(c)
	check(f.tech_cancel and not f.tech_held, "second tap on the cancel chip cancels")
	touch_up(5, z)
	touch_up(2, t)
	f = poll(c)
	check(not f.tech_released, "no commit after chip cancel")
	# With no technique held, that spot is just free space (camera area).
	touch_down(6, z)
	check_eq(c.finger_role(6), TouchControls.ROLE_CAMERA, "cancel zone is inert without a held technique")
	touch_up(6, z)


# --- buttons -------------------------------------------------------------------------------------------------

func test_tap_shorter_than_one_tick_still_registers() -> void:
	var c := make_controls()
	var a := btn(c, Id.ATTACK)
	touch_down(3, a)
	touch_up(3, a)
	var f := poll(c)
	check(f.attack_pressed, "attack_pressed latched")
	check(f.attack_released, "attack_released latched")
	check(not f.attack_held, "not held after the finger is gone")
	f = poll(c)
	check(not f.attack_pressed and not f.attack_released and not f.attack_held, "edges consumed by one poll")
	# Same for guard and technique taps.
	var g := btn(c, Id.GUARD)
	touch_down(3, g)
	touch_up(3, g)
	f = poll(c)
	check(f.guard_pressed and f.guard_released, "guard tap latched both edges")
	var t := btn(c, Id.TECH)
	touch_down(3, t)
	touch_up(3, t)
	f = poll(c)
	check(f.tech_pressed and f.tech_released and not f.tech_cancel, "quick technique tap = press + commit")


func test_attack_tap_versus_hold() -> void:
	var c := make_controls()
	var a := btn(c, Id.ATTACK)
	touch_down(3, a)
	var f := poll(c)
	check(f.attack_pressed and f.attack_held and not f.attack_released, "press tick")
	step(c, 0.10)
	f = poll(c)
	check(f.attack_held and not f.attack_pressed, "still held at 0.10 s, no repeat edge")
	check(c._attack_t < c.charge_time(), "below the charge threshold")
	step(c, 0.20)
	check(c._attack_t >= c.charge_time(), "past the element's hold threshold the charge cue is full")
	f = poll(c)
	check(f.attack_held, "held through the charge")
	touch_up(3, a)
	f = poll(c)
	check(f.attack_released and not f.attack_held, "release edge after a hold")
	step(c, 0.05)
	check_near(c._attack_t, 0.0, 0.0001, "charge timer resets")
	# A quick tap never reaches the threshold.
	touch_down(3, a)
	step(c, 0.08)
	touch_up(3, a)
	check(c._attack_t < c.charge_time(), "tap stays under the threshold")


func test_guard_hold_and_evade_edge() -> void:
	var c := make_controls()
	var g := btn(c, Id.GUARD)
	var e := btn(c, Id.EVADE)
	touch_down(1, g)
	var f := poll(c)
	check(f.guard_pressed and f.guard_held, "guard press edge (deflection window) + held")
	f = poll(c)
	check(f.guard_held and not f.guard_pressed, "guard stays held with no new edge")
	touch_down(2, e)
	f = poll(c)
	check(f.evade_pressed and f.guard_held, "evade pressed while guard held")
	check(poll(c).evade_pressed == false, "evade is a pure edge")
	touch_up(2, e)
	check(not poll(c).evade_pressed, "no evade edge on release")
	touch_up(1, g)
	f = poll(c)
	check(f.guard_released and not f.guard_held, "guard released")


func test_second_finger_on_a_taken_control_is_inert() -> void:
	var c := make_controls()
	var a := btn(c, Id.ATTACK)
	touch_down(1, a)
	touch_down(2, a)
	check_eq(c.finger_role(2), TouchControls.ROLE_DEAD, "second finger on attack owns nothing")
	var f := poll(c)
	check(f.attack_pressed, "single press edge")
	touch_up(2, a)
	check(poll(c).attack_held, "inert finger lifting does not release the real one")
	touch_up(1, a)
	check(poll(c).attack_released, "real finger releases")


func test_target_cycle_and_pause_buttons() -> void:
	var c := make_controls()
	var paused := [0]
	c.pause_requested.connect(func() -> void: paused[0] += 1)
	var tcen := btn(c, Id.TARGET)
	touch_down(1, tcen)
	touch_up(1, tcen)
	check(poll(c).target_cycle, "target cycle edge")
	var pc := btn(c, Id.PAUSE)
	touch_down(1, pc)
	check_eq(paused[0], 0, "pause waits for the release (no accidental pause on touch-down)")
	touch_up(1, pc)
	check_eq(paused[0], 1, "pause fires on release inside the button")
	check(poll(c).pause_pressed, "frame flag set too")
	# Sliding off the button before release cancels the pause.
	touch_down(1, pc)
	drag_step(1, pc + Vector2(-400, 300), Vector2.ZERO)
	touch_up(1, pc + Vector2(-400, 300))
	check_eq(paused[0], 1, "released far away: no pause")
	# A cancelled touch never pauses.
	touch_down(1, pc)
	touch_up(1, pc, true)
	check_eq(paused[0], 1, "canceled touch: no pause")


# --- simultaneity ------------------------------------------------------------------------------------------------

func test_move_guard_and_camera_simultaneously() -> void:
	var c := make_controls()
	var l := c.get_layout()
	var sp := stick_point(c)
	var cp := cam_point(c)
	touch_down(0, sp)
	touch_down(1, btn(c, Id.GUARD))
	touch_down(2, cp)
	drag_step(0, sp + Vector2(l.stick_radius * 0.8, 0), Vector2(l.stick_radius * 0.8, 0))
	drag_step(2, cp + Vector2(50, 0), Vector2(50, 0))
	var f := poll(c)
	check(f.move.x > 0.6, "moving")
	check(f.guard_held and f.guard_pressed, "guarding")
	check(f.cam_delta.x > 0.0, "camera turning")
	check_eq(c.active_finger_count(), 3, "three independent fingers")
	# Lifting one finger leaves the other two untouched.
	touch_up(1, btn(c, Id.GUARD))
	drag_step(2, cp + Vector2(80, 0), Vector2(30, 0))
	f = poll(c)
	check(f.guard_released and not f.guard_held, "guard released")
	check(f.move.x > 0.6, "still moving")
	check(f.cam_delta.x > 0.0, "still turning")


func test_move_and_technique_aim_with_two_thumbs() -> void:
	var c := make_controls()
	var l := c.get_layout()
	var sp := stick_point(c)
	var t := btn(c, Id.TECH)
	touch_down(0, sp)
	touch_down(1, t)
	drag_step(0, sp + Vector2(0, -l.stick_radius), Vector2(0, -l.stick_radius))
	drag_step(1, t + Vector2(l.aim_radius * 0.7, 0), Vector2(l.aim_radius * 0.7, 0))
	var f := poll(c)
	check(f.move.y > 0.9, "left thumb walks forward")
	check(f.tech_held and f.tech_aim_active, "right thumb aims")
	check_near(f.tech_aim.x, 0.7, 0.01, "aim to the right")
	check_vec_near(f.cam_delta, Vector2.ZERO, 0.00001, "no camera movement")


# --- cancellation ----------------------------------------------------------------------------------------------------

func _hold_everything(c: TouchControls) -> void:
	var l := c.get_layout()
	var sp := stick_point(c)
	touch_down(0, sp)
	drag_step(0, sp + Vector2(l.stick_radius, 0), Vector2.ZERO)
	touch_down(1, btn(c, Id.ATTACK))
	touch_down(2, btn(c, Id.TECH))
	drag_step(2, btn(c, Id.TECH) + Vector2(40, 0), Vector2.ZERO)
	touch_down(3, btn(c, Id.GUARD))
	touch_down(4, cam_point(c))
	poll(c)  # consume the press edges; fingers stay down


func _check_everything_released(c: TouchControls, f: InputFrame, what: String) -> void:
	check_vec_near(f.move, Vector2.ZERO, 0.0001, what + ": stick back to zero")
	check(f.attack_released and not f.attack_held, what + ": attack released")
	check(f.guard_released and not f.guard_held, what + ": guard released")
	check(f.tech_cancel, what + ": technique cancelled")
	check(not f.tech_released, what + ": technique NOT committed")
	check(not f.tech_held, what + ": technique no longer held")
	check_eq(c.active_finger_count(), 0, what + ": no finger left")
	var f2 := poll(c)
	check(not f2.attack_released and not f2.tech_cancel and not f2.guard_released, what + ": nothing repeats")


func test_focus_out_mid_hold_releases_everything() -> void:
	for what in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED, Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT]:
		var c := make_controls()
		_hold_everything(c)
		c.notification(what)
		_check_everything_released(c, poll(c), "notification %d" % what)
		# Late 'finger up' events after the cancel are harmless.
		touch_up(1, btn(c, Id.ATTACK))
		touch_up(2, btn(c, Id.TECH))
		var f := poll(c)
		check(not f.attack_released and not f.tech_released, "late up events are ignored")


func test_canceled_touch_events_are_safe() -> void:
	var c := make_controls()
	_hold_everything(c)
	touch_up(0, stick_point(c), true)
	touch_up(1, btn(c, Id.ATTACK), true)
	touch_up(2, btn(c, Id.TECH), true)
	touch_up(3, btn(c, Id.GUARD), true)
	touch_up(4, cam_point(c), true)
	_check_everything_released(c, poll(c), "canceled=true")


func test_pausing_hiding_and_exit_release_everything() -> void:
	var c := make_controls()
	_hold_everything(c)
	c.visible = false
	_check_everything_released(c, poll(c), "hidden")
	c.visible = true
	_hold_everything(c)
	c.notification(Node.NOTIFICATION_PAUSED)
	_check_everything_released(c, poll(c), "tree paused")
	# Events while hidden are ignored.
	c.visible = false
	touch_down(1, btn(c, Id.ATTACK))
	check(not poll(c).attack_pressed, "hidden controls ignore touches")
	c.visible = true


# --- handedness / ownership ------------------------------------------------------------------------------------------

func test_left_handed_mirrors_regions() -> void:
	var c := make_controls()
	var l := c.get_layout()
	var vp := l.viewport_size
	var attack := btn(c, Id.ATTACK)
	var tech := btn(c, Id.TECH)
	var ghost := l.stick_ghost
	var pause := btn(c, Id.PAUSE)
	check(attack.x > vp.x * 0.5 and ghost.x < vp.x * 0.5, "right-handed: buttons right, stick left")
	check(l.is_stick_side(Vector2(100, 400)) and not l.is_stick_side(Vector2(vp.x - 100, 400)), "right-handed regions")
	settings.left_handed = true
	settings.changed.emit()
	l = c.get_layout()
	check_near(btn(c, Id.ATTACK).x, vp.x - attack.x, 0.01, "attack mirrored")
	check_near(btn(c, Id.TECH).x, vp.x - tech.x, 0.01, "technique mirrored")
	check_near(btn(c, Id.TECH).y, tech.y, 0.01, "y unchanged")
	check_near(btn(c, Id.PAUSE).x, vp.x - pause.x, 0.01, "pause moves to the top-left corner")
	check_near(l.stick_ghost.x, vp.x - ghost.x, 0.01, "stick ghost mirrored")
	check(not l.is_stick_side(Vector2(100, 400)) and l.is_stick_side(Vector2(vp.x - 100, 400)), "regions flipped")
	# Behaviour follows the geometry.
	touch_down(0, Vector2(vp.x - 220, 500))
	check_eq(c.finger_role(0), TouchControls.ROLE_STICK, "right side owns the stick when left-handed")
	touch_down(1, Vector2(100, 300))
	check_eq(c.finger_role(1), TouchControls.ROLE_CAMERA, "left side is the camera area")
	touch_down(2, btn(c, Id.ATTACK))
	check(poll(c).attack_pressed, "mirrored attack button works")
	# Back again.
	c.release_all()
	settings.left_handed = false
	settings.changed.emit()
	check_near(btn(c, Id.ATTACK).x, attack.x, 0.01, "restored")


func test_ownership_persists_across_region_boundaries() -> void:
	var c := make_controls()
	var l := c.get_layout()
	var vp := l.viewport_size
	var sp := stick_point(c)
	var cp := cam_point(c)
	# Stick finger wanders over the whole right side, across attack and technique.
	touch_down(0, sp)
	var cur := drag(0, sp, btn(c, Id.TECH), 5)
	cur = drag(0, cur, btn(c, Id.ATTACK), 3)
	cur = drag(0, cur, Vector2(vp.x - 20, 60), 3)
	var f := poll(c)
	check(not f.attack_pressed and not f.tech_pressed and not f.guard_pressed, "stick finger pressed nothing")
	check(f.cam_delta == Vector2.ZERO, "stick finger rotated nothing")
	check(f.move.length() > 0.5, "stick finger still steering")
	check_eq(c.finger_role(0), TouchControls.ROLE_STICK, "still the stick")
	touch_up(0, cur)
	# Camera finger dragged into the stick half: stays camera, no stick.
	touch_down(1, cp)
	cur = drag(1, cp, Vector2(150, 500), 6)
	f = poll(c)
	check_vec_near(f.move, Vector2.ZERO, 0.00001, "camera finger never steers")
	check(f.cam_delta.length() > 0.1, "still turning the camera")
	touch_up(1, cur)
	# Technique finger dragged across the screen: still aiming, never camera/stick.
	touch_down(2, btn(c, Id.TECH))
	cur = drag(2, btn(c, Id.TECH), Vector2(150, 500), 6)
	f = poll(c)
	check(f.tech_held and f.tech_aim_active, "technique finger still aiming")
	check(f.cam_delta == Vector2.ZERO and f.move == Vector2.ZERO, "and nothing else")
	touch_up(2, cur)
	# Attack finger dragged away stays attack until it lifts.
	touch_down(3, btn(c, Id.ATTACK))
	poll(c)
	cur = drag(3, btn(c, Id.ATTACK), Vector2(100, 100), 6)
	f = poll(c)
	check(f.attack_held and not f.attack_released, "attack still held when the finger left the button")
	touch_up(3, cur)
	check(poll(c).attack_released, "released at lift")


func test_touch_down_wins_by_position_not_by_travel() -> void:
	# A finger starting on empty cluster-gap space in the right half is camera,
	# even if later dragged onto a button; one starting on the button owns it.
	var c := make_controls()
	var p := cam_point(c)
	touch_down(0, p)
	touch_down(1, btn(c, Id.ATTACK))
	check_eq(c.finger_role(0), TouchControls.ROLE_CAMERA, "free space = camera")
	check_eq(c.finger_role(1), TouchControls.ROLE_BUTTON, "button = button")


# --- element chips ------------------------------------------------------------------------------------------------------

func test_element_chips_only_unlocked() -> void:
	var c := make_controls()
	c.set_context({"element": 0, "unlocked_elements": [0, 2]})
	var chips := [Id.ELEM_0, Id.ELEM_1, Id.ELEM_2, Id.ELEM_3]
	# Locked water: swallowed, no selection, no camera drag.
	touch_down(1, btn(c, Id.ELEM_1))
	check_eq(poll(c).element_select, -1, "locked element not selectable")
	check_eq(c.finger_role(1), TouchControls.ROLE_DEAD, "locked chip swallows the touch")
	touch_up(1, btn(c, Id.ELEM_1))
	touch_down(1, btn(c, Id.ELEM_3))
	check_eq(poll(c).element_select, -1, "locked air not selectable")
	touch_up(1, btn(c, Id.ELEM_3))
	# Unlocked fire.
	touch_down(1, btn(c, Id.ELEM_2))
	var f := poll(c)
	check_eq(f.element_select, 2, "fire selected")
	check(not f.attack_pressed and f.cam_delta == Vector2.ZERO, "chip tap does nothing else")
	touch_up(1, btn(c, Id.ELEM_2))
	check_eq(poll(c).element_select, -1, "selection is an edge")
	touch_down(1, btn(c, Id.ELEM_0))
	check_eq(poll(c).element_select, 0, "earth selected")
	touch_up(1, btn(c, Id.ELEM_0))
	# Unlock water via context and it becomes tappable.
	c.set_context({"unlocked_elements": [0, 1, 2]})
	touch_down(1, btn(c, Id.ELEM_1))
	check_eq(poll(c).element_select, 1, "water selectable after unlock")
	touch_up(1, btn(c, Id.ELEM_1))
	check_eq(chips.size(), 4, "four chips")


func test_chips_disabled_while_technique_held() -> void:
	var c := make_controls()
	touch_down(1, btn(c, Id.TECH))
	touch_down(2, btn(c, Id.ELEM_2))
	check_eq(poll(c).element_select, -1, "no element switching mid-technique")
	check_eq(c.finger_role(2), TouchControls.ROLE_DEAD, "chip finger is inert")


func test_chips_are_distinct_hit_targets() -> void:
	var c := make_controls()
	var l := c.get_layout()
	for e in 4:
		check_eq(l.hit_test(btn(c, Id.ELEM_0 + e)), Id.ELEM_0 + e, "chip %d hit at its centre" % e)
	for e in 3:
		var mid := (btn(c, Id.ELEM_0 + e) + btn(c, Id.ELEM_0 + e + 1)) * 0.5
		var h := l.hit_test(mid)
		check(h == Id.ELEM_0 + e or h == Id.ELEM_0 + e + 1 or h == TouchLayout.NONE, "midpoint resolves to a neighbour, never a third control")


# --- settings -------------------------------------------------------------------------------------------------------------

func test_settings_save_load_round_trip() -> void:
	var path := "user://ui_test_roundtrip.cfg"
	var s := GameSettings.new()
	s.control_scale = 1.25
	s.control_opacity = 0.55
	s.layout_preset = "wide"
	s.left_handed = true
	s.strong_labels = true
	s.camera_sensitivity = 1.7
	s.invert_y = true
	s.screen_shake = 0.4
	s.flashes = 0.0
	s.haptics = false
	s.reduced_motion = true
	s.slowmo_assist = true
	s.show_debug = true
	check_eq(s.save(path), OK, "save ok")
	var t := GameSettings.load_from(path)
	check(t.equals(s), "round trip preserves every field")
	check_near(t.control_scale, 1.25, 0.0001, "scale")
	check_eq(t.layout_preset, "wide", "preset")
	check(t.left_handed and t.strong_labels and t.invert_y and t.reduced_motion and t.slowmo_assist and t.show_debug, "bools")
	check(not t.haptics, "haptics off persisted")
	t.reset_to_defaults()
	check(t.equals(GameSettings.new()), "reset_to_defaults restores defaults")
	check(not t.equals(s), "and differs from the saved one")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))


func test_settings_clamp_and_missing_file() -> void:
	var s := GameSettings.new()
	s.control_scale = 9.0
	check_near(s.control_scale, 1.4, 0.0001, "scale clamped high")
	s.control_scale = 0.1
	check_near(s.control_scale, 0.8, 0.0001, "scale clamped low")
	s.control_opacity = 0.0
	check_near(s.control_opacity, 0.3, 0.0001, "opacity clamped")
	s.layout_preset = "bogus"
	check_eq(s.layout_preset, "default", "unknown preset falls back")
	s.screen_shake = -3.0
	check_near(s.screen_shake, 0.0, 0.0001, "shake clamped")
	var missing := GameSettings.load_from("user://does_not_exist_ui_test.cfg")
	check(missing.equals(GameSettings.new()), "missing file -> defaults")
	# Hand-edited garbage is repaired, not trusted.
	var path := "user://ui_test_garbage.cfg"
	var cfg := ConfigFile.new()
	cfg.set_value("controls", "control_scale", 99.0)
	cfg.set_value("controls", "layout_preset", 12)
	cfg.set_value("controls", "left_handed", "yes")
	cfg.set_value("comfort", "flashes", "lots")
	cfg.save(path)
	var g := GameSettings.load_from(path)
	check_near(g.control_scale, 1.4, 0.0001, "garbage scale clamped")
	check_eq(g.layout_preset, "default", "garbage preset repaired")
	check(not g.left_handed, "garbage bool ignored")
	check_near(g.flashes, 1.0, 0.0001, "garbage float ignored")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
