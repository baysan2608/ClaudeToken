extends "res://tests/ui/ui_test_case.gd"
## Regressions for the input -> sim / camera / HUD pipeline: drag aim on the
## technique release tick, camera sensitivity / invert / pitch direction applied
## once, attack charge ring timing vs the sim's tap/hold rule, touch overlay
## redraw only on context change, HUD text physical size on iPhone.

const Id := TouchLayout.Id


func _hub() -> PlayerInputHub:
	var h := PlayerInputHub.new()
	h.force_touch_ui = true
	host.root.add_child(h)
	track(h)
	h.touch.configure_layout(PHONE, Vector4.ZERO, PHONE_PPM)
	h.desktop.fixed_dt = 1.0 / 60.0
	return h


func _step(w: CombatWorld, intents: Dictionary, a: ActorState, pc: PlayerController, f: InputFrame, cam_yaw: float) -> Array:
	intents[a.id] = pc.build(f, cam_yaw)
	w.step(intents)
	return w.take_events()


# --- technique aim on the commit tick ----------------------------------------------------------------------

func test_release_tick_keeps_drag_aim_in_intent() -> void:
	var pc := PlayerController.new()
	var f := InputFrame.new()
	# Release tick exactly as TouchControls / DesktopInput produce it: final aim still present.
	f.tech_released = true
	f.tech_held = false
	f.tech_aim = Vector2(1, 0)
	f.tech_aim_active = true
	var it := pc.build(f, PI)    # camera looks along -Z, so screen right is +X
	check(it.aim_active, "aim stays active on the release (commit) tick")
	check_vec_near(Vector2(it.aim_dir.x, it.aim_dir.z), Vector2(1, 0), 0.001, "aim_dir is the dragged direction")
	# Nothing held or released: a stale aim flag is ignored.
	f.tech_released = false
	check(not pc.build(f, PI).aim_active, "no aim without a held or released technique")


func test_touch_drag_aims_the_earth_throw() -> void:
	# Full pipeline: touch drag -> TouchControls.fill_frame -> PlayerController -> CombatWorld.
	var c := make_controls()
	var l := c.get_layout()
	var pc := PlayerController.new()
	var w := CombatWorld.new(4)
	var p := w.add_actor("P", Vector3(-6, 0, 8), 0, {}, Sim.Element.EARTH)
	var o := w.add_actor("O", Vector3(-6, 0, -4), 1, {}, Sim.Element.EARTH)
	o.is_dummy = true
	var intents := {}
	for k in 30:
		_step(w, intents, p, pc, poll(c), PI)
	w.take_events()
	var t := btn(c, Id.TECH)
	var aim_to := t + Vector2(l.aim_radius, 0)    # drag fully to the right (opponent is straight ahead)
	var launched := Vector3.ZERO
	for k in 90:
		if k == 0:
			touch_down(2, t)
		elif k == 4:
			drag(2, t, aim_to, 4)
		elif k == 50:
			touch_up(2, aim_to)
		for e in _step(w, intents, p, pc, poll(c), PI):
			if e.type == "launch" and e.get("actor", -1) == p.id:
				launched = w.get_body(e.body).vel
	check(launched != Vector3.ZERO, "the held stone was thrown on release")
	var flat := Vector3(launched.x, 0, launched.z).normalized()
	check(flat.x > 0.95, "throw follows the drag to the right, not the auto-aim target (dir %s)" % str(flat))


# --- camera: sensitivity / invert applied once, finger up looks up -----------------------------------------------

func _cam_rig() -> CameraRig:
	var cam := CameraRig.new()
	host.root.add_child(cam)
	track(cam)
	cam.snap_to(Vector3.ZERO, Vector3(0, 0, -5))
	return cam


## One camera-finger drag through the hub into the rig, wired like Game (which also
## copies the settings onto the rig). Returns [dyaw, dpitch, view-dir y before, after].
func _cam_drag(h: PlayerInputHub, cam: CameraRig, px: Vector2) -> Array[float]:
	cam.sensitivity = settings.camera_sensitivity
	cam.invert_y = settings.invert_y
	cam.yaw = PI
	cam.pitch = 0.32
	cam.update_rig(1.0 / 60.0, Vector3.ZERO, null, null)
	var y0 := cam.yaw
	var p0 := cam.pitch
	var look0 := -cam.cam.global_transform.basis.z.y
	var p := cam_point(h.touch)
	touch_down(3, p)
	drag_step(3, p + px, px)
	cam.add_input(h.poll_frame().cam_delta)
	cam.update_rig(1.0 / 60.0, Vector3.ZERO, null, null)
	touch_up(3, p + px)
	h.poll_frame()
	return [cam.yaw - y0, cam.pitch - p0, look0, -cam.cam.global_transform.basis.z.y]


func test_camera_finger_up_looks_up_and_settings_apply_once() -> void:
	var h := _hub()
	var cam := _cam_rig()
	var k := TouchControls.CAM_RAD_PER_MM / PHONE_PPM
	var r := _cam_drag(h, cam, Vector2(0, -20))
	check(r[3] > r[2] + 0.01, "finger up looks up (view y %.3f -> %.3f)" % [r[2], r[3]])
	check_near(r[1], -20.0 * k, 0.0005, "pitch moves by exactly the producer's delta")
	settings.invert_y = true
	r = _cam_drag(h, cam, Vector2(0, -20))
	check(r[3] < r[2] - 0.01, "invert_y: finger up looks down (view y %.3f -> %.3f)" % [r[2], r[3]])
	settings.invert_y = false
	for sens in [0.5, 1.0, 2.0]:
		settings.camera_sensitivity = sens
		r = _cam_drag(h, cam, Vector2(20, 0))
		check_near(r[0], -20.0 * k * sens, 0.0005, "yaw scales linearly with sensitivity %.1f" % sens)
	# Finger right turns the view toward camera-right.
	settings.camera_sensitivity = 1.0
	cam.yaw = PI
	var right0 := cam.forward_flat().cross(Vector3.UP)
	cam.add_input(Vector2(0.2, 0))
	check(cam.forward_flat().dot(right0) > 0.0, "positive yaw delta turns right")


# --- attack charge ring vs the sim's tap/hold rule -------------------------------------------------------------

func _attack_goes_heavy(elem: int, hold_ticks: int) -> bool:
	var pc := PlayerController.new()
	var w := CombatWorld.new(5)
	var p := w.add_actor("P", Vector3(-8, 0, 6), 0, {}, elem)
	p.in_water = false
	var o := w.add_actor("O", Vector3(-8, 0, -4), 1, {}, Sim.Element.EARTH)
	o.is_dummy = true
	var intents := {}
	for k in 20:
		_step(w, intents, p, pc, InputFrame.new(), PI)
	w.take_events()
	var heavy := false
	for k in 80:
		var f := InputFrame.new()
		f.attack_pressed = k == 0
		f.attack_held = k < hold_ticks
		f.attack_released = k == hold_ticks
		for e in _step(w, intents, p, pc, f, PI):
			if e.type == "action" and e.get("actor", -1) == p.id and e.get("phase", "") == "charge":
				heavy = true
	return heavy


func test_charge_ring_fills_when_the_sim_commits_heavy() -> void:
	var c := make_controls()
	for elem in 4:
		var sec := TouchControls.attack_charge_sec(elem)
		var n := int(round(sec / Sim.DT))
		check(_attack_goes_heavy(elem, n), "%s: a hold of %d ticks (ring full) is heavy" % [Sim.ELEMENT_NAMES[elem], n])
		check(not _attack_goes_heavy(elem, n - 1), "%s: one tick less (ring not full) is still light" % Sim.ELEMENT_NAMES[elem])
		c.set_context({"element": elem})
		check_near(c.charge_time(), sec, 0.0001, "%s: ring follows the selected element" % Sim.ELEMENT_NAMES[elem])
	c.charge_hold_sec = 0.4
	check_near(c.charge_time(), 0.4, 0.0001, "an explicit override still wins")


# --- touch overlay redraw ---------------------------------------------------------------------------------------

func test_set_context_redraws_only_on_change() -> void:
	var c := make_controls()
	var ctx := {"element": 2, "unlocked_elements": [0, 1, 2], "tech_label": "HEAT", "tech_available": true, "holding": false}
	check(c.set_context(ctx), "first context changes the overlay")
	check(not c.is_element_unlocked(3) and c.is_element_unlocked(2), "unlock mask applied")
	for i in 3:
		check(not c.set_context(ctx.duplicate(true)), "an identical context (sent every frame) does not redraw")
	var tweaks := [{"element": 1}, {"unlocked_elements": [0, 1, 2, 3]}, {"tech_label": "POUR"}, {"tech_available": false}, {"holding": true}]
	for tw: Dictionary in tweaks:
		var ctx2 := ctx.duplicate(true)
		ctx2.merge(tw, true)
		check(c.set_context(ctx2), "changing %s redraws" % str(tw.keys()))
		check(c.set_context(ctx), "and changing it back redraws")
	check(not c.set_context({}), "an empty context changes nothing")


# --- HUD text size ------------------------------------------------------------------------------------------------

func test_hud_text_has_a_physical_minimum_on_iphone() -> void:
	# Desktop (no real density): the viewport-scaled sizes are unchanged.
	check_eq(Hud.density_ppm(host.root), 0.0, "desktop without a pinned DPI has no physical floor")
	for s in [0.8, 1.0, 1.5]:
		check_eq(Hud.font_px(13, Hud.LINE_MM, s, 0.0), int(13 * s), "desktop size unchanged at scale %.1f" % s)
	UiScale.dpi_override = 460.0
	var ppm := Hud.density_ppm(host.root)
	UiScale.dpi_override = 0.0
	check(ppm > 0.0, "a pinned DPI enables the physical floor")
	# iPhone landscape: safe height ~657 of 720 -> s ~0.91, ~11.1 units per mm.
	var s_phone := 657.0 / 720.0
	var min_mm := 1.8    # ~11 pt at 460 ppi
	for base in [11.0, 12.0, 13.0]:
		var mm := Hud.LINE_MM if base == 13.0 else Hud.TEXT_MM
		var fs := Hud.font_px(base, mm, s_phone, PHONE_PPM)
		check(fs / PHONE_PPM >= min_mm, "iPhone text base %d is %.2f mm (>= %.1f)" % [int(base), fs / PHONE_PPM, min_mm])
	check(Hud.font_px(18, Hud.TOAST_MM, s_phone, PHONE_PPM) / PHONE_PPM >= 2.7, "iPhone toast >= 2.7 mm")
	check(Hud.BAR_MM * PHONE_PPM >= 6.0, "iPhone vitals bars thicker than 4 units")
	# iPad (~6 units/mm, s ~1.33) already exceeds the floor: unchanged.
	check_eq(Hud.font_px(13, Hud.LINE_MM, 960.0 / 720.0, 6.0), int(13 * 960.0 / 720.0), "iPad size unchanged")
