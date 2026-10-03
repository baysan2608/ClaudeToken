extends "res://tests/ui/ui_test_case.gd"
## Layout geometry, safe area, DesktopInput, Haptics, PlayerInputHub merge,
## SettingsPanel signals.

const Id := TouchLayout.Id

const IPAD := Vector2(1280, 960)      # 2732x2048 window under canvas_items/expand
const IPAD_PPM := 6.0                 # 264 dpi / 2.134 stretch / 25.4 * ... (viewport units per mm)
const SE := Vector2(1280, 750)        # small phone


func _geometry_ok(l: TouchLayout, what: String) -> void:
	var ids := [Id.ATTACK, Id.GUARD, Id.EVADE, Id.TECH, Id.TARGET, Id.PAUSE, Id.ELEM_0, Id.ELEM_1, Id.ELEM_2, Id.ELEM_3]
	for i in ids.size():
		var a: int = ids[i]
		var ca := l.centers[a]
		var ra := l.radii[a]
		check(ca.x - ra >= l.usable.position.x - 0.5 and ca.x + ra <= l.usable.end.x + 0.5, "%s: control %d inside usable width" % [what, a])
		check(ca.y - ra >= l.usable.position.y - 0.5 and ca.y + ra <= l.usable.end.y + 0.5, "%s: control %d inside usable height" % [what, a])
		if a != Id.PAUSE:
			var stick_side := l.is_stick_side(Vector2(ca.x - (ra if not l.left_handed else -ra), ca.y))
			check(not stick_side, "%s: control %d stays out of the stick half" % [what, a])
		for j in range(i + 1, ids.size()):
			var b: int = ids[j]
			var d := ca.distance_to(l.centers[b])
			check(d >= ra + l.radii[b] - 0.5, "%s: controls %d and %d do not overlap (d=%.1f, need %.1f)" % [what, a, b, d, ra + l.radii[b]])
	var z := l.centers[Id.CANCEL]
	check(z.x - l.radii[Id.CANCEL] >= l.usable.position.x - 0.5 and z.x + l.radii[Id.CANCEL] <= l.usable.end.x + 0.5, "%s: cancel zone inside usable width" % what)
	check(z.y - l.radii[Id.CANCEL] >= l.usable.position.y - 0.5, "%s: cancel zone below the top edge" % what)
	check(l.centers[Id.TECH].distance_to(z) - l.radii[Id.CANCEL] > l.aim_radius * 0.9, "%s: cancel zone outside the aim ring" % what)
	# Stick ghost sits in its own half.
	check(l.is_stick_side(l.stick_ghost), "%s: stick ghost on the stick side" % what)


func test_layout_geometry_all_presets_scales_and_screens() -> void:
	var screens := {"phone": [PHONE, PHONE_PPM, Vector4(132, 0, 132, 63) / 1.625], "ipad": [IPAD, IPAD_PPM, Vector4(0, 24, 0, 20) / 2.134], "small": [SE, 12.0, Vector4.ZERO]}
	for sname: String in screens:
		var sc: Array = screens[sname]
		for preset in GameSettings.PRESETS:
			for scale in [0.8, 1.0, 1.4]:
				for lefty in [false, true]:
					var l := TouchLayout.new()
					l.configure(sc[0], sc[2], sc[1], scale, preset, lefty)
					_geometry_ok(l, "%s/%s/x%.1f/%s" % [sname, preset, scale, "left" if lefty else "right"])


func test_button_sizes_are_thumb_sized() -> void:
	# Default preset, scale 1: ~12-14 mm for the main buttons, attack biggest.
	var l := TouchLayout.new()
	l.configure(PHONE, Vector4.ZERO, PHONE_PPM, 1.0, "default", false)
	for id in [Id.GUARD, Id.EVADE, Id.TECH]:
		var mm := l.radii[id] * 2.0 / PHONE_PPM
		check(mm >= 12.0 and mm <= 14.5, "control %d is %.1f mm (want 12-14.5)" % [id, mm])
	check(l.radii[Id.ATTACK] > l.radii[Id.TECH], "attack is the biggest")
	# The cluster hugs the bottom-right corner, not the middle of the screen.
	check(l.centers[Id.ATTACK].x > PHONE.x * 0.8 and l.centers[Id.ATTACK].y > PHONE.y * 0.6, "attack near the thumb corner")
	# iPad: same physical size, so it is a smaller fraction of the screen.
	var p := TouchLayout.new()
	p.configure(IPAD, Vector4.ZERO, IPAD_PPM, 1.0, "default", false)
	check_near(p.radii[Id.TECH] * 2.0 / IPAD_PPM, 14.0, 0.01, "iPad technique button still 14 mm")
	check(p.centers[Id.ATTACK].x > IPAD.x * 0.8 and p.centers[Id.ATTACK].y > IPAD.y * 0.7, "iPad cluster still in the thumb corner")


func test_safe_area_insets_respected() -> void:
	var insets := Vector4(80, 0, 90, 40)
	var l := TouchLayout.new()
	l.configure(PHONE, insets, PHONE_PPM, 1.0, "default", false)
	for id in TouchLayout.COUNT:
		check(l.centers[id].x + l.radii[id] <= PHONE.x - 90.0 + 0.5, "control %d clear of the right inset" % id)
		check(l.centers[id].y + l.radii[id] <= PHONE.y - 40.0 + 0.5, "control %d clear of the bottom inset" % id)
	check(l.stick_ghost.x - l.stick_radius >= 80.0 - 0.5, "stick ghost clear of the left inset")
	# Mirrored: the insets swap sides.
	l.configure(PHONE, insets, PHONE_PPM, 1.0, "default", true)
	for id in TouchLayout.COUNT:
		check(l.centers[id].x - l.radii[id] >= 80.0 - 0.5, "mirrored control %d clear of the left inset" % id)
	check(l.stick_ghost.x + l.stick_radius <= PHONE.x - 90.0 + 0.5, "mirrored stick ghost clear of the right inset")


func test_ui_scale_helpers_do_not_blow_up() -> void:
	var ppm := UiScale.px_per_mm(host.root)
	var vis := host.root.get_visible_rect().size
	check(ppm > 0.0 and ppm <= vis.y, "px_per_mm finite and plausible (%.2f)" % ppm)
	var ins := UiScale.safe_insets(host.root)
	check(ins.x >= 0.0 and ins.y >= 0.0 and ins.z >= 0.0 and ins.w >= 0.0, "insets non-negative")
	UiScale.dpi_override = 460.0
	var ppm2 := UiScale.px_per_mm(host.root)
	UiScale.dpi_override = 0.0
	check(ppm2 > 0.0, "dpi override works")


# --- Haptics -----------------------------------------------------------------------------------------------------------

func test_haptics_rate_limit_and_setting() -> void:
	Haptics.reset_for_tests()
	check(Haptics.play("block"), "first pulse plays")
	check(not Haptics.play("heavy"), "second pulse within 60 ms is dropped")
	check_eq(Haptics.pulse_count, 1, "only one pulse")
	check(not Haptics.play("nonsense"), "unknown kind ignored")
	OS.delay_msec(Haptics.MIN_GAP_MS + 10)
	check(Haptics.play("perfect"), "plays again after the gap")
	check_eq(Haptics.last_kind, "perfect", "last kind recorded")
	OS.delay_msec(Haptics.MIN_GAP_MS + 10)
	settings.haptics = false
	check(not Haptics.play("light"), "disabled in settings")
	settings.haptics = true
	for k: String in Haptics.KINDS:
		check(int(Haptics.KINDS[k][0]) <= 100, "%s is a short pulse, never continuous" % k)
	for k in ["light", "block", "deflect", "perfect", "heavy", "lost_control", "transform"]:
		check(Haptics.KINDS.has(k), "kind %s defined" % k)


# --- DesktopInput ------------------------------------------------------------------------------------------------------

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


func test_desktop_actions_registered() -> void:
	DesktopInput.register_actions()
	for a in DesktopInput.ACTIONS:
		check(InputMap.has_action(a), "%s registered" % a)
	for a in [DesktopInput.MOVE_L, DesktopInput.MOVE_R, DesktopInput.MOVE_U, DesktopInput.MOVE_D, DesktopInput.CAM_L, DesktopInput.CAM_R, DesktopInput.CAM_U, DesktopInput.CAM_D]:
		check(InputMap.has_action(a), "%s registered" % a)
	DesktopInput.register_actions()  # idempotent
	check_eq(InputMap.action_get_events(&"ff_pause").size(), 3, "pause: Esc, Backspace, Start")


func test_desktop_edges_and_taps() -> void:
	var d := _desktop()
	Input.action_press(&"ff_attack")
	var f := _dpoll(d)
	check(f.attack_pressed and f.attack_held and not f.attack_released, "press tick")
	f = _dpoll(d)
	check(f.attack_held and not f.attack_pressed, "held, no repeat edge")
	Input.action_release(&"ff_attack")
	f = _dpoll(d)
	check(f.attack_released and not f.attack_held, "release edge")
	# Real key events (shorter than a tick) latch through _input.
	var down := InputEventKey.new()
	down.physical_keycode = KEY_K
	down.pressed = true
	var up := InputEventKey.new()
	up.physical_keycode = KEY_K
	up.pressed = false
	# The engine updates action state and dispatches each event in turn.
	var accumulate := Input.use_accumulated_input
	Input.use_accumulated_input = false
	Input.parse_input_event(down)
	d._input(down)
	Input.parse_input_event(up)
	d._input(up)
	Input.use_accumulated_input = accumulate
	f = _dpoll(d)
	check(f.guard_pressed and f.guard_released and not f.guard_held, "guard tap shorter than a tick still latches")


func test_desktop_move_camera_and_elements() -> void:
	var d := _desktop()
	d.set_unlocked([0, 1, 3])
	Input.action_press(&"ff_move_right")
	Input.action_press(&"ff_move_up")
	var f := _dpoll(d)
	check(f.move.x > 0.6 and f.move.y > 0.6, "WD moves up-right")
	check(f.move.length() <= 1.0001, "diagonal normalised")
	Input.action_release(&"ff_move_right")
	Input.action_release(&"ff_move_up")
	Input.action_press(&"ff_cam_right")
	f = _dpoll(d)
	check(f.cam_delta.x > 0.0 and f.cam_delta.y == 0.0, "arrow right yaws right")
	Input.action_release(&"ff_cam_right")
	Input.action_press(&"ff_cam_up")
	check(_dpoll(d).cam_delta.y > 0.0, "arrow up pitches up")
	settings.invert_y = true
	check(_dpoll(d).cam_delta.y < 0.0, "invert_y flips")
	Input.action_release(&"ff_cam_up")
	# Elements: 3 is locked (fire), 4 (air) unlocked.
	Input.action_press(&"ff_elem_3")
	check_eq(_dpoll(d).element_select, -1, "locked element ignored")
	Input.action_release(&"ff_elem_3")
	_dpoll(d)
	Input.action_press(&"ff_elem_4")
	check_eq(_dpoll(d).element_select, 3, "air selected")
	Input.action_release(&"ff_elem_4")
	Input.action_press(&"ff_target")
	check(_dpoll(d).target_cycle, "tab cycles target")
	Input.action_release(&"ff_target")


func test_desktop_technique_aim_cancel_and_pause() -> void:
	var d := _desktop()
	var paused := [0]
	d.pause_requested.connect(func() -> void: paused[0] += 1)
	Input.action_press(&"ff_tech")
	var f := _dpoll(d)
	check(f.tech_pressed and f.tech_held, "technique pressed")
	# Mouse movement while held aims (not the camera).
	var m := InputEventMouseMotion.new()
	m.relative = Vector2(60, -30)  # right + up on screen
	d._input(m)
	f = _dpoll(d)
	check(f.tech_aim_active and f.tech_aim.x > 0.0 and f.tech_aim.y > 0.0, "mouse aims right/up")
	check(f.cam_delta == Vector2.ZERO, "no camera while aiming")
	# Esc cancels instead of pausing.
	Input.action_press(&"ff_pause")
	f = _dpoll(d)
	check(f.tech_cancel and not f.tech_held, "Esc cancels the technique")
	check_eq(paused[0], 0, "and does not pause")
	Input.action_release(&"ff_pause")
	Input.action_release(&"ff_tech")
	f = _dpoll(d)
	check(not f.tech_released, "no commit after cancel")
	# Esc with nothing to cancel pauses.
	Input.action_press(&"ff_pause")
	f = _dpoll(d)
	check(f.pause_pressed and paused[0] == 1, "Esc pauses when idle")
	Input.action_release(&"ff_pause")
	_dpoll(d)
	# Plain release commits.
	Input.action_press(&"ff_tech")
	_dpoll(d)
	Input.action_release(&"ff_tech")
	f = _dpoll(d)
	check(f.tech_released and not f.tech_cancel, "release commits")


func test_desktop_pause_key_does_not_retrigger_after_resume() -> void:
	var d := _desktop()
	var paused := [0]
	d.pause_requested.connect(func() -> void: paused[0] += 1)
	Input.action_press(&"ff_pause")
	var f := _dpoll(d)
	check(f.pause_pressed and paused[0] == 1, "Esc pauses")
	d.notification(Node.NOTIFICATION_PAUSED)
	# Esc is pressed again inside the menu to close it and is still down on resume.
	Input.action_press(&"ff_pause")
	d.notification(Node.NOTIFICATION_UNPAUSED)
	f = _dpoll(d)
	check(not f.pause_pressed and paused[0] == 1, "the Esc that closed the menu does not pause again")
	Input.action_release(&"ff_pause")
	_dpoll(d)
	Input.action_press(&"ff_pause")
	check(_dpoll(d).pause_pressed, "a fresh Esc press pauses again")


func test_desktop_ignores_touch_emulated_mouse() -> void:
	var d := _desktop()
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = true
	mb.device = InputEvent.DEVICE_ID_EMULATION
	d._input(mb)
	check(not _dpoll(d).attack_pressed, "emulated mouse (from touch) is not an attack")
	var real := InputEventMouseButton.new()
	real.button_index = MOUSE_BUTTON_LEFT
	real.pressed = true
	d._input(real)
	check(_dpoll(d).attack_pressed, "real LMB attacks")
	real = InputEventMouseButton.new()
	real.button_index = MOUSE_BUTTON_LEFT
	real.pressed = false
	d._input(real)
	check(_dpoll(d).attack_released, "LMB release")
	# RMB = technique hold, with focus loss => cancel, never commit.
	var rmb := InputEventMouseButton.new()
	rmb.button_index = MOUSE_BUTTON_RIGHT
	rmb.pressed = true
	d._input(rmb)
	check(_dpoll(d).tech_held, "RMB holds the technique")
	d.notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	var f := _dpoll(d)
	check(f.tech_cancel and not f.tech_released and not f.tech_held, "focus loss cancels the technique")


# --- PlayerInputHub ------------------------------------------------------------------------------------------------------

func _hub() -> PlayerInputHub:
	var h := PlayerInputHub.new()
	h.force_touch_ui = true
	host.root.add_child(h)
	track(h)
	h.touch.configure_layout(PHONE, Vector4.ZERO, PHONE_PPM)
	h.desktop.fixed_dt = 1.0 / 60.0
	return h


func test_hub_merges_touch_and_desktop() -> void:
	var h := _hub()
	var l := h.touch.get_layout()
	check(h.touch.visible, "touch HUD visible when forced")
	h.controls_visible = false
	check(not h.touch.visible, "controls_visible=false hides the HUD")
	h.controls_visible = true
	check(h.touch.visible, "and shows it again")
	# Left thumb on the touch stick, keyboard attack at the same time.
	var sp := Vector2(220, 500)
	touch_down(0, sp)
	drag_step(0, sp + Vector2(l.stick_radius * 0.6, 0), Vector2.ZERO)
	Input.action_press(&"ff_attack")
	touch_down(1, h.touch.get_layout().centers[Id.GUARD])
	var f := h.poll_frame()
	check(f.move.x > 0.4, "touch movement present")
	check(f.attack_pressed and f.attack_held, "keyboard attack present")
	check(f.guard_pressed and f.guard_held, "touch guard present")
	check(f.move.length() <= 1.0001, "merged move clamped")
	# Same object reused; edges cleared on the next poll.
	var f2 := h.poll_frame()
	check(f2 == f, "poll_frame reuses one InputFrame object")
	check(not f2.attack_pressed and not f2.guard_pressed and f2.attack_held and f2.guard_held, "edges cleared, holds kept")
	touch_up(0, sp)
	touch_up(1, l.centers[Id.GUARD])
	Input.action_release(&"ff_attack")
	f = h.poll_frame()
	check(f.guard_released and f.attack_released and f.move == Vector2.ZERO, "releases merged")


func test_hub_pause_signal_and_panel() -> void:
	var h := _hub()
	var got := [0]
	h.paused_requested.connect(func() -> void: got[0] += 1)
	var pc := h.touch.get_layout().centers[Id.PAUSE]
	touch_down(0, pc)
	touch_up(0, pc)
	check_eq(got[0], 1, "paused_requested emitted")
	check(h.settings_panel.is_open(), "panel opened")
	check(host.paused, "tree paused while the panel is open")
	check(h.settings_panel.process_mode == Node.PROCESS_MODE_ALWAYS, "panel keeps processing while paused")
	h.settings_panel.close_panel()
	check(not host.paused, "closing resumes")


func test_hub_context_and_settings_changed() -> void:
	var h := _hub()
	h.set_context({"element": 2, "unlocked_elements": [0, 2], "tech_label": "HEAT", "tech_available": true, "holding": false, "target_screen_pos": Vector2(500, 300), "target_label": "Dummy"})
	check(h.touch.is_element_unlocked(2) and not h.touch.is_element_unlocked(1), "context unlock mask applied")
	check(h.target_marker.has_target(), "target marker shown")
	h.set_context({"target_screen_pos": null})
	check(not h.target_marker.has_target(), "null hides the marker")
	var n := [0]
	h.settings_changed.connect(func() -> void: n[0] += 1)
	settings.control_scale = 1.2
	settings.changed.emit()
	check_eq(n[0], 1, "settings_changed forwarded")
	check_near(h.touch.get_layout().control_scale, 1.2, 0.0001, "touch layout picked up the new scale")


# --- SettingsPanel ---------------------------------------------------------------------------------------------------------

func test_settings_panel_flow() -> void:
	settings.storage_path = "user://ui_test_panel.cfg"
	var p := SettingsPanel.new()
	p.settings = settings
	host.root.add_child(p)
	track(p)
	var log: Array = []
	p.resume_requested.connect(func() -> void: log.append("resume"))
	p.reset_requested.connect(func() -> void: log.append("reset"))
	p.practice_requested.connect(func() -> void: log.append("practice_requested"))
	p.practice_selected.connect(func(id: Variant) -> void: log.append("selected:" + str(id)))
	p.reset_progress_requested.connect(func() -> void: log.append("progress"))
	p.quit_to_lab_requested.connect(func() -> void: log.append("quit"))
	check(not p.visible and not host.paused, "closed by default")
	p.open_panel()
	check(p.visible and host.paused and p.is_open(), "open pauses the tree")
	p._on_practice()
	check_eq(log, ["practice_requested"], "opening practice asks the game for items")
	p.set_practice_items([
		{"id": "p1", "title": "Stone shot", "subtitle": "Lift and throw", "locked": false},
		{"id": "p2", "title": "Phase lab", "subtitle": "Heat a rock", "locked": true},
	])
	var buttons := _buttons_with_children(p._practice_box)
	check_eq(buttons.size(), 2, "two practice rows")
	check(not buttons[0].disabled and buttons[1].disabled, "locked row disabled")
	buttons[0].pressed.emit()
	check_eq(log[-1], "selected:p1", "practice_selected(id) emitted")
	check(not p.is_open() and not host.paused, "choosing a practice item closes the panel")
	# Settings sliders and toggles write through to GameSettings and save.
	p.open_panel()
	var sl: HSlider = p._bindings["control_scale"]["ctl"]
	sl.value = 1.3
	check_near(settings.control_scale, 1.3, 0.001, "slider writes the setting")
	var tg: CheckButton = p._bindings["left_handed"]["ctl"]
	tg.button_pressed = true
	check(settings.left_handed, "toggle writes the setting")
	p._preset_buttons[2].button_pressed = true
	p._preset_buttons[2].pressed.emit()
	check_eq(settings.layout_preset, "wide", "preset segment writes the setting")
	# Reset progress needs a confirm.
	p._on_reset_progress_pressed()
	check(p._confirm_dim.visible and not "progress" in log, "confirm shown, nothing emitted yet")
	p._confirm_cancel.pressed.emit()
	check(not p._confirm_dim.visible and not "progress" in log, "cancel dismisses without emitting")
	p._on_reset_progress_pressed()
	p._on_confirm_reset_progress()
	check_eq(log[-1], "progress", "reset_progress_requested after confirming")
	# Reset settings to defaults.
	p._on_reset_defaults()
	check(settings.equals(GameSettings.new()), "defaults restored")
	check_near(sl.value, 1.0, 0.001, "widget follows")
	# Esc in the very frame that opened the panel is ignored; later it resumes.
	p.close_panel()
	p.open_panel()
	var esc := InputEventAction.new()
	esc.action = &"ui_cancel"
	esc.pressed = true
	p._input(esc)
	check(p.is_open(), "the Esc press that opened the panel does not close it")
	p._open_frame = -5
	p._input(esc)
	check(not p.is_open() and log.has("resume"), "Esc later resumes")
	p.open_panel()
	p._on_reset_scenario()
	check(log.has("reset") and not p.is_open(), "reset scenario closes + emits")
	p.open_panel()
	p._on_quit()
	check(log.has("quit") and not host.paused, "quit to lab closes + emits")
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://ui_test_panel.cfg"))


func _buttons_with_children(box: Node) -> Array[Button]:
	var out: Array[Button] = []
	for ch in box.get_children():
		if ch is Button and not ch.is_queued_for_deletion():
			out.append(ch)
	return out
