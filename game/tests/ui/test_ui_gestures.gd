extends "res://tests/ui/ui_test_case.gd"
## Flicks (attack thrust / ground / sweep, guard push / sink), T+A shape taps, evade hold, gesture petals,
## the sub-element ring (tap and slide), keyboard / gamepad chords, left-handed mirror, hub plumbing.

const Id := TouchLayout.Id
const G := Sim.Gesture


func _mm(c: TouchControls, mm: float) -> float:
	return mm * c.get_layout().ppm


# ---------------------------------------------------------------- recogniser

func test_flick_classify_quadrants() -> void:
	check_eq(FlickRecognizer.classify(Vector2(2, -30)), G.UP, "up")
	check_eq(FlickRecognizer.classify(Vector2(-3, 30)), G.DOWN, "down")
	check_eq(FlickRecognizer.classify(Vector2(-30, 4)), G.SIDE, "left")
	check_eq(FlickRecognizer.classify(Vector2(30, -5)), G.SIDE, "right")
	check_eq(FlickRecognizer.classify(Vector2.ZERO), G.NONE, "no travel")


func test_attack_flicks_report_the_gesture_with_the_press() -> void:
	var c := make_controls()
	var a := btn(c, Id.ATTACK)
	for case in [[Vector2(0, -1), G.UP], [Vector2(0, 1), G.DOWN], [Vector2(-1, 0), G.SIDE], [Vector2(1, 0), G.SIDE]]:
		touch_down(1, a)
		drag(1, a, a + case[0] * _mm(c, 8.0), 4)
		var f := poll(c)
		check(f.attack_pressed and f.attack_held, "press edge + held on the flick tick")
		check_eq(f.attack_gesture, case[1], "flick %s" % str(case[0]))
		check_eq(poll(c).attack_gesture, 0, "gesture is an edge")
		touch_up(1, a + case[0] * _mm(c, 8.0))
		check(poll(c).attack_released, "release")
		check_eq(poll(c).attack_gesture, 0, "release adds no second gesture")


func test_short_travel_is_not_a_flick() -> void:
	var c := make_controls()
	var a := btn(c, Id.ATTACK)
	touch_down(1, a)
	drag(1, a, a + Vector2(0, -_mm(c, 5.0)), 4)
	touch_up(1, a + Vector2(0, -_mm(c, 5.0)))
	check_eq(poll(c).attack_gesture, 0, "5 mm is a wobble, not a flick")


func test_flick_after_a_hold_is_recognised_at_release() -> void:
	var c := make_controls()
	var a := btn(c, Id.ATTACK)
	touch_down(1, a)
	step(c, 0.40)
	poll(c)
	drag(1, a, a + Vector2(0, -_mm(c, 9.0)), 4)
	var f := poll(c)
	check_eq(f.attack_gesture, 0, "no gesture mid-hold after the 0.25 s window")
	check(f.attack_held, "still held")
	touch_up(1, a + Vector2(0, -_mm(c, 9.0)))
	f = poll(c)
	check_eq(f.attack_gesture, G.UP, "flick at release after a hold")
	check(f.attack_released, "carried by the release tick")


func test_cancelled_touch_never_flicks() -> void:
	var c := make_controls()
	var a := btn(c, Id.ATTACK)
	touch_down(1, a)
	step(c, 0.4)
	drag(1, a, a + Vector2(0, -_mm(c, 9.0)), 3)
	poll(c)
	touch_up(1, a, true)
	var f := poll(c)
	check(f.attack_released, "released")
	check_eq(f.attack_gesture, 0, "a cancelled touch carries no gesture")


func test_guard_flicks_push_and_sink_and_rebase() -> void:
	var c := make_controls()
	var g := btn(c, Id.GUARD)
	touch_down(1, g)
	poll(c)
	step(c, 0.8)
	drag(1, g, g + Vector2(0, -_mm(c, 8.0)), 4)
	var f := poll(c)
	check_eq(f.guard_gesture, G.UP, "flick up = push (any time while held)")
	check(f.guard_held, "guard stays held")
	# Re-based: a second flick down from where the finger is now is a sink.
	var p := g + Vector2(0, -_mm(c, 8.0))
	drag(1, p, p + Vector2(0, _mm(c, 9.0)), 4)
	check_eq(poll(c).guard_gesture, G.DOWN, "then down = sink")
	var q := p + Vector2(0, _mm(c, 9.0))
	drag(1, q, q + Vector2(_mm(c, 9.0), 0), 3)
	check_eq(poll(c).guard_gesture, 0, "a sideways guard flick is nothing")
	touch_up(1, q)
	check(poll(c).guard_released, "released")


func test_shape_tap_is_an_edge_only() -> void:
	var c := make_controls()
	touch_down(1, btn(c, Id.TECH))
	poll(c)
	touch_down(2, btn(c, Id.ATTACK))
	var f := poll(c)
	check(f.tech_held and f.attack_pressed, "ATTACK tapped while the technique is held")
	check(not f.attack_held, "a shape tap never holds an attack")
	check_eq(f.attack_gesture, 0, "no gesture")
	drag(2, btn(c, Id.ATTACK), btn(c, Id.ATTACK) + Vector2(0, -_mm(c, 9.0)), 3)
	check_eq(poll(c).attack_gesture, 0, "no flick while shaping")
	touch_up(2, btn(c, Id.ATTACK))
	f = poll(c)
	check(not f.attack_released, "its lift sends no release")
	touch_up(1, btn(c, Id.TECH))
	check(poll(c).tech_released, "technique commits normally")


func test_evade_hold_level() -> void:
	var c := make_controls()
	touch_down(1, btn(c, Id.EVADE))
	var f := poll(c)
	check(f.evade_pressed and f.evade_held, "press edge and level")
	f = poll(c)
	check(f.evade_held and not f.evade_pressed, "level persists")
	touch_up(1, btn(c, Id.EVADE))
	check(not poll(c).evade_held, "released")


# ---------------------------------------------------------------- petals

func test_petals_light_the_flick_direction() -> void:
	var c := make_controls()
	c.set_context({"petals": {"up": "Spear", "down": "Fangs", "side": "Fan"}, "guard_petals": {"up": "Ram", "down": "Swallow"}})
	var a := btn(c, Id.ATTACK)
	touch_down(1, a)
	check_eq(c.attack_gesture_hot(), 0, "nothing lit at rest")
	drag(1, a, a + Vector2(0, _mm(c, 4.0)), 3)
	check_eq(c.attack_gesture_hot(), G.DOWN, "4 mm down already points at the ground petal")
	touch_up(1, a)
	touch_down(2, btn(c, Id.GUARD))
	drag(2, btn(c, Id.GUARD), btn(c, Id.GUARD) + Vector2(0, -_mm(c, 4.5)), 3)
	check_eq(c.guard_gesture_hot(), G.UP, "guard push petal lit")
	touch_up(2, btn(c, Id.GUARD))


func test_petal_anchors_stay_on_screen_and_mirror() -> void:
	for lefty in [false, true]:
		for dev in [[Vector2(1558, 720), 11.1, Vector4(132, 0, 132, 63) / 1.625], [Vector2(1280, 960), 6.0, Vector4(0, 24, 0, 20) / 2.134]]:
			var l := TouchLayout.new()
			l.configure(dev[0], dev[2], dev[1], 1.0, "default", lefty)
			for g in [G.UP, G.DOWN, G.SIDE]:
				var an := l.attack_petal_anchor(g)
				check(l.usable.grow(2.0).has_point(an.pos), "attack petal %d anchor inside the usable rect (lefty %s)" % [g, str(lefty)])
			var side := l.attack_petal_anchor(G.SIDE)
			var dx: float = side.pos.x - l.centers[Id.ATTACK].x
			check((dx > 0.0) == lefty, "the sweep petal sits on the inner side (lefty %s)" % str(lefty))
			for g2 in [G.UP, G.DOWN]:
				check(l.usable.grow(2.0).has_point(l.guard_petal_anchor(g2).pos), "guard petal %d inside" % g2)


# ---------------------------------------------------------------- sub-element ring

func _ring_controls() -> TouchControls:
	var c := make_controls()
	c.set_context({"element": 2, "sub": 0, "unlocked_elements": [0, 1, 2, 3], "unlocked_subs": [0, 1, 2, 3]})
	return c


func test_tap_active_chip_opens_ring_then_petal_selects_sub() -> void:
	var c := _ring_controls()
	var chip := btn(c, Id.ELEM_2)
	touch_down(1, chip)
	poll(c)
	touch_up(1, chip)
	check_eq(c.ring_mode(), TouchControls.RING_TAP, "tapping the active chip opens the ring")
	var rects := c.get_layout().ring_rects()
	touch_down(2, rects[2].get_center())
	var f := poll(c)
	check_eq(f.sub_select, 2, "tapping petal 2 selects sub 2")
	check_eq(f.element_select, -1, "and does not touch the element")
	check_eq(c.ring_mode(), TouchControls.RING_CLOSED, "ring closes")
	check_eq(c.finger_role(2), TouchControls.ROLE_DEAD, "the petal finger owns nothing afterwards")
	touch_up(2, rects[2].get_center())
	check_eq(poll(c).sub_select, -1, "edge")


func test_ring_closes_on_a_miss_and_on_the_chip_again() -> void:
	var c := _ring_controls()
	var chip := btn(c, Id.ELEM_2)
	touch_down(1, chip)
	touch_up(1, chip)
	check(c.is_ring_open(), "open")
	touch_down(2, cam_point(c))
	check(not c.is_ring_open(), "a touch elsewhere closes it")
	touch_up(2, cam_point(c))
	touch_down(1, chip)
	touch_up(1, chip)
	check(c.is_ring_open(), "open again")
	touch_down(3, chip)
	check(not c.is_ring_open(), "the active chip again closes it")
	touch_up(3, chip)
	# Timeout.
	touch_down(1, chip)
	touch_up(1, chip)
	step(c, TouchControls.RING_TAP_TIMEOUT_S + 0.1)
	check(not c.is_ring_open(), "a tap ring times out")


func test_long_press_slides_to_a_petal() -> void:
	var c := _ring_controls()
	var chip := btn(c, Id.ELEM_2)
	touch_down(1, chip)
	check_eq(poll(c).element_select, 2, "the press still selects the element immediately")
	step(c, TouchControls.RING_LONG_PRESS_S + 0.05)
	check_eq(c.ring_mode(), TouchControls.RING_SLIDE, "held long enough: the slide ring opens")
	var rects := c.get_layout().ring_rects()
	drag(1, chip, rects[3].get_center(), 5)
	check_eq(c.ring_hover(), 3, "hovering petal 3")
	touch_up(1, rects[3].get_center())
	check_eq(poll(c).sub_select, 3, "released on petal 3 selects sub 3")
	check(not c.is_ring_open(), "closed")


func test_slide_off_the_ring_selects_nothing() -> void:
	var c := _ring_controls()
	var chip := btn(c, Id.ELEM_2)
	touch_down(1, chip)
	step(c, 0.4)
	drag(1, chip, chip + Vector2(0, 90), 3)
	touch_up(1, chip + Vector2(0, 90))
	check_eq(poll(c).sub_select, -1, "no petal under the finger: no selection")
	check(not c.is_ring_open(), "closed")


func test_locked_sub_elements_cannot_be_selected() -> void:
	var c := make_controls()
	c.set_context({"element": 0, "sub": 0, "unlocked_elements": [0, 1, 2, 3], "unlocked_subs": [0, 1]})
	var chip := btn(c, Id.ELEM_0)
	touch_down(1, chip)
	touch_up(1, chip)
	var rects := c.get_layout().ring_rects()
	touch_down(2, rects[3].get_center())
	check_eq(poll(c).sub_select, -1, "locked sub 3 is not selectable")
	touch_up(2, rects[3].get_center())
	check(c.is_sub_unlocked(1) and not c.is_sub_unlocked(2), "unlock mask from the context")


func test_ring_is_cancelled_by_focus_loss_and_blocked_during_a_technique() -> void:
	var c := _ring_controls()
	var chip := btn(c, Id.ELEM_2)
	touch_down(1, chip)
	step(c, 0.4)
	c.release_all(true)
	check(not c.is_ring_open(), "release_all closes the ring")
	check_eq(poll(c).sub_select, -1, "and selects nothing")
	# Technique held: chips (and so the ring) are disabled.
	touch_down(2, btn(c, Id.TECH))
	touch_down(3, chip)
	check_eq(c.finger_role(3), TouchControls.ROLE_DEAD, "chip inert while the technique is held")
	check(not c.is_ring_open(), "no ring")


func test_ring_geometry_fits_every_screen() -> void:
	var screens := [[Vector2(1558, 720), 11.1, Vector4(132, 0, 132, 63) / 1.625], [Vector2(1280, 960), 6.0, Vector4(0, 24, 0, 20) / 2.134], [Vector2(1280, 750), 12.0, Vector4.ZERO]]
	for sc in screens:
		for lefty in [false, true]:
			for scale in [0.8, 1.0, 1.4]:
				var l := TouchLayout.new()
				l.configure(sc[0], sc[2], sc[1], scale, "default", lefty)
				var rs := l.ring_rects()
				check_eq(rs.size(), 4, "four petals")
				for i in 4:
					check(l.usable.grow(1.0).encloses(rs[i]), "petal %d inside the usable rect (%s, x%.1f, lefty %s)" % [i, str(sc[0]), scale, str(lefty)])
					check(rs[i].size.y >= 7.0 * l.ppm * scale * 0.99, "petal height >= 7 mm")
					for j in range(i + 1, 4):
						check(not rs[i].intersects(rs[j]), "petals %d / %d do not overlap" % [i, j])
				for id in [Id.ATTACK, Id.GUARD, Id.EVADE, Id.TECH, Id.TARGET]:
					for r in rs:
						check(not r.intersects(Rect2(l.centers[id] - Vector2.ONE * l.radii[id], Vector2.ONE * l.radii[id] * 2.0)), "ring never covers button %d" % id)


# ---------------------------------------------------------------- keyboard / gamepad

func _desktop() -> DesktopInput:
	var d := DesktopInput.new()
	d.settings = settings
	d.fixed_dt = 1.0 / 60.0
	host.root.add_child(d)
	track(d)
	return d


func _dpoll(d: DesktopInput) -> InputFrame:
	var f := InputFrame.new()
	d.fill_frame(f)
	return f


func test_keyboard_slot_keys_carry_their_gesture() -> void:
	var d := _desktop()
	for case in [[&"ff_thrust", G.UP], [&"ff_ground", G.DOWN], [&"ff_sweep", G.SIDE]]:
		Input.action_press(case[0])
		var f := _dpoll(d)
		check(f.attack_pressed and f.attack_held, "%s press + held" % case[0])
		check_eq(f.attack_gesture, case[1], "%s gesture on the press tick" % case[0])
		Input.action_release(case[0])
		f = _dpoll(d)
		check(f.attack_released and not f.attack_held, "%s release" % case[0])
	Input.action_press(&"ff_attack")
	check_eq(_dpoll(d).attack_gesture, 0, "J is the plain strike")
	Input.action_release(&"ff_attack")
	_dpoll(d)


func test_guard_chords_push_and_sink() -> void:
	var d := _desktop()
	Input.action_press(&"ff_guard")
	_dpoll(d)
	Input.action_press(&"ff_attack")
	var f := _dpoll(d)
	check_eq(f.guard_gesture, G.UP, "K + J = push")
	check(f.guard_held and not f.attack_pressed and not f.attack_held, "the chord is a guard flick, not an attack")
	Input.action_release(&"ff_attack")
	f = _dpoll(d)
	check(not f.attack_released, "its release is silent")
	Input.action_press(&"ff_ground")
	f = _dpoll(d)
	check_eq(f.guard_gesture, G.DOWN, "K + N = sink")
	check(not f.attack_pressed, "not a ground attack")
	Input.action_release(&"ff_ground")
	Input.action_release(&"ff_guard")
	_dpoll(d)
	# Same tick (both pressed before one poll): guard is scanned first.
	Input.action_press(&"ff_guard")
	Input.action_press(&"ff_attack")
	f = _dpoll(d)
	check_eq(f.guard_gesture, G.UP, "chord pressed within one tick still reads as push")
	Input.action_release(&"ff_attack")
	Input.action_release(&"ff_guard")
	_dpoll(d)
	_dpoll(d)


func test_attack_key_while_the_technique_is_held_is_a_shape_tap() -> void:
	var d := _desktop()
	Input.action_press(&"ff_tech")
	_dpoll(d)
	Input.action_press(&"ff_attack")
	var f := _dpoll(d)
	check(f.attack_pressed and f.tech_held, "J while L is held")
	check(not f.attack_held, "never a held attack")
	Input.action_release(&"ff_attack")
	f = _dpoll(d)
	check(not f.attack_released, "no release")
	Input.action_release(&"ff_tech")
	_dpoll(d)


func test_evade_hold_and_sub_cycling_keys() -> void:
	var d := _desktop()
	d.set_sub_context(2, 0, [0, 1, 2, 3])
	Input.action_press(&"ff_evade")
	var f := _dpoll(d)
	check(f.evade_pressed and f.evade_held, "Space press")
	check(_dpoll(d).evade_held, "Space held = evade_hold input")
	Input.action_release(&"ff_evade")
	check(not _dpoll(d).evade_held, "released")
	# The active element's key again cycles; another element's key selects it.
	Input.action_press(&"ff_elem_3")
	f = _dpoll(d)
	check_eq(f.sub_select, 1, "3 again (Fire is active) = next sub-element")
	check_eq(f.element_select, -1, "no element change")
	Input.action_release(&"ff_elem_3")
	_dpoll(d)
	d.set_sub_context(2, 1, [0, 1, 3])
	Input.action_press(&"ff_sub_next")
	check_eq(_dpoll(d).sub_select, 3, "E skips the locked sub 2")
	Input.action_release(&"ff_sub_next")
	_dpoll(d)
	Input.action_press(&"ff_sub_prev")
	check_eq(_dpoll(d).sub_select, 0, "Q goes back")
	Input.action_release(&"ff_sub_prev")
	_dpoll(d)
	Input.action_press(&"ff_elem_1")
	check_eq(_dpoll(d).element_select, 0, "another element's key selects the element")
	Input.action_release(&"ff_elem_1")
	_dpoll(d)


func test_gamepad_lb_dpad_selects_a_sub_and_lb_still_cancels() -> void:
	var d := _desktop()
	d.set_sub_context(2, 0, [0, 1, 2, 3])
	Input.action_press(&"ff_cancel")
	_dpoll(d)
	Input.action_press(&"ff_elem_4")       # d-pad up = sub 3 while LB is held
	var f := _dpoll(d)
	check_eq(f.sub_select, 3, "LB + d-pad up = sub 3")
	check_eq(f.element_select, -1, "no element switch")
	Input.action_release(&"ff_elem_4")
	Input.action_release(&"ff_cancel")
	_dpoll(d)
	Input.action_press(&"ff_tech")
	_dpoll(d)
	Input.action_press(&"ff_cancel")
	f = _dpoll(d)
	check(f.tech_cancel and not f.tech_held, "LB cancels a held technique")
	Input.action_release(&"ff_cancel")
	Input.action_release(&"ff_tech")
	_dpoll(d)


func test_gamepad_bindings() -> void:
	DesktopInput.register_actions()
	var want := {&"ff_attack": JOY_BUTTON_X, &"ff_thrust": JOY_BUTTON_Y, &"ff_sweep": JOY_BUTTON_B, &"ff_evade": JOY_BUTTON_A,
		&"ff_guard": JOY_BUTTON_RIGHT_SHOULDER, &"ff_target": JOY_BUTTON_RIGHT_STICK, &"ff_cancel": JOY_BUTTON_LEFT_SHOULDER}
	for a in want:
		var found := false
		for ev in InputMap.action_get_events(a):
			if ev is InputEventJoypadButton and (ev as InputEventJoypadButton).button_index == want[a]:
				found = true
		check(found, "%s is on its pad button" % a)
	var lt := false
	for ev in InputMap.action_get_events(&"ff_ground"):
		if ev is InputEventJoypadMotion and (ev as InputEventJoypadMotion).axis == JOY_AXIS_TRIGGER_LEFT:
			lt = true
	check(lt, "LT is ground")


func test_dev_toggle_keys_emit_a_signal() -> void:
	var d := _desktop()
	var n := [0]
	d.dev_requested.connect(func() -> void: n[0] += 1)
	for k in [KEY_QUOTELEFT, KEY_F2]:
		var e := InputEventKey.new()
		e.physical_keycode = k
		e.pressed = true
		d._input(e)
	check_eq(n[0], 2, "backquote and F2 both open the dev panel")


# ---------------------------------------------------------------- hub and controller

func test_player_controller_carries_the_new_fields() -> void:
	var f := InputFrame.new()
	f.attack_gesture = G.DOWN
	f.guard_gesture = G.UP
	f.sub_select = 2
	f.evade_held = true
	var it := PlayerController.new().build(f, 0.0)
	check_eq(it.attack_gesture, G.DOWN, "attack gesture")
	check_eq(it.guard_gesture, G.UP, "guard gesture")
	check_eq(it.sub_select, 2, "sub select")
	check(it.evade_held, "evade held")


func test_hub_merges_gestures_and_blocks_input_for_modal_tools() -> void:
	var h := PlayerInputHub.new()
	h.force_touch_ui = true
	host.root.add_child(h)
	track(h)
	h.touch.configure_layout(PHONE, Vector4.ZERO, PHONE_PPM)
	h.desktop.fixed_dt = 1.0 / 60.0
	var l := h.touch.get_layout()
	touch_down(1, l.centers[Id.ATTACK])
	drag(1, l.centers[Id.ATTACK], l.centers[Id.ATTACK] + Vector2(0, -_mm(h.touch, 8.0)), 4)
	var f := h.poll_frame()
	check_eq(f.attack_gesture, G.UP, "touch gesture survives the hub merge")
	touch_up(1, l.centers[Id.ATTACK])
	h.poll_frame()
	var opened := [0]
	h.paused_requested.connect(func() -> void: opened[0] += 1)
	h.input_blocked = true
	check(not h.touch.visible, "touch HUD hidden while a modal tool is open")
	h.desktop._on_pressed(DesktopInput.A.PAUSE)
	h._on_pause()
	check_eq(opened[0], 0, "pause is ignored while blocked")
	Input.action_press(&"ff_attack")
	f = h.poll_frame()
	check(not f.attack_pressed and not f.attack_held, "idle frame while blocked")
	Input.action_release(&"ff_attack")
	h.poll_frame()
	h.input_blocked = false
	check(h.touch.visible, "visible again")
	var devs := [0]
	h.dev_requested.connect(func() -> void: devs[0] += 1)
	h.settings_panel.dev_requested.emit()
	check_eq(devs[0], 1, "the pause menu's Dev button reaches the game")
