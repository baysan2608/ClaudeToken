extends SceneTree
## Renders the HUD over a plain dark-grey background and saves PNGs.
##   tools/scripts/godot.sh --render --resolution 2532x1170 \
##       -s res://tests/ui/render_hud_screenshot.gd -- --device=phone --out=/tmp/shots
##   tools/scripts/godot.sh --render --resolution 2732x2048 \
##       -s res://tests/ui/render_hud_screenshot.gd -- --device=ipad --out=/tmp/shots
## Optional: --shots=idle,active,strong,lefty,compact,light,settings,practice,petals,guard,ring,tiers,lefty_petals
## (the full Game + Lab dev panel are rendered by res://tests/ui/render_lab_screenshot.gd)
## Not part of the automated pass/fail suite (needs a renderer).

const Id := TouchLayout.Id


func _initialize() -> void:
	_run()


func _arg(name: String, fallback: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--" + name + "="):
			return a.substr(name.length() + 3)
	return fallback


func _run() -> void:
	await process_frame
	var device := _arg("device", "phone")
	var out_dir := _arg("out", "/tmp")
	var shots := _arg("shots", "idle,active,strong,lefty,light,settings,practice,petals,guard,ring,tiers,lefty_petals").split(",")
	DirAccess.make_dir_recursive_absolute(out_dir)

	# Physical assumptions: iPhone ~460 dpi, iPad Pro 12.9 ~264 dpi.
	UiScale.dpi_override = 460.0 if device == "phone" else 264.0
	var insets_px := Vector4(141, 0, 141, 63) if device == "phone" else Vector4(0, 0, 0, 40)

	for shot in shots:
		await _render_shot(shot, device, out_dir, insets_px)
	quit()


func _render_shot(shot: String, device: String, out_dir: String, insets_px: Vector4) -> void:
	var settings := GameSettings.new()
	settings.storage_path = "user://shot_settings.cfg"
	GameSettings.set_current(settings)
	if shot == "strong":
		settings.strong_labels = true
	if shot == "lefty" or shot == "lefty_petals":
		settings.left_handed = true
	if shot == "compact":
		settings.layout_preset = "compact"

	var bg := ColorRect.new()
	bg.color = Color(0.82, 0.86, 0.9) if shot == "light" else Color(0.16, 0.17, 0.19)
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.add_child(bg)
	if shot == "light":
		# A bright sky-ish gradient strip so ring/glyph contrast is judged fairly.
		var strip := ColorRect.new()
		strip.color = Color(0.95, 0.97, 1.0)
		strip.position = Vector2.ZERO
		strip.size = Vector2(root.get_visible_rect().size.x, 360)
		root.add_child(strip)

	var hub := PlayerInputHub.new()
	hub.force_touch_ui = true
	hub.auto_open_settings_panel = false
	root.add_child(hub)

	var win := Vector2(root.size)
	var vis := root.get_visible_rect().size
	var scale := win.y / vis.y
	var ppm := UiScale.px_per_mm(root)
	hub.touch.configure_layout(vis, insets_px / scale, ppm)
	print("shot=", shot, " window=", win, " viewport=", vis, " scale=", snappedf(scale, 0.001), " ppm=", snappedf(ppm, 0.01))

	var l := hub.touch.get_layout()
	hub.set_context({
		"element": 2, "unlocked_elements": [0, 1, 2], "tech_label": "HEAT", "tech_available": true,
		"holding": false, "target_screen_pos": Vector2(vis.x * 0.5, vis.y * 0.42), "target_label": "Dummy"})

	hub.set_context({"element": 2, "sub": 1, "unlocked_subs": [0, 1, 2, 3],
		"petals": {"up": "Comet Flame", "down": "Blue Furrow", "side": "Corona"}, "guard_petals": {"up": "Flash Over", "down": "Kiln"}})
	if shot == "petals" or shot == "lefty_petals":
		var a := l.centers[Id.ATTACK]
		hub.touch._input(_ev(1, a, true))
		hub.touch._input(_drag(1, a + Vector2(0, -0.6 * l.ppm * 6.0)))
	elif shot == "guard":
		hub.touch._input(_ev(1, l.centers[Id.GUARD], true))
		hub.touch._input(_drag(1, l.centers[Id.GUARD] + Vector2(0, -0.6 * l.ppm * 6.0)))
	elif shot == "ring":
		hub.touch._input(_ev(1, l.centers[Id.ELEM_2], true))
		hub.touch._input(_ev(1, l.centers[Id.ELEM_2], false))
	elif shot == "tiers":
		hub.touch._input(_ev(1, l.centers[Id.ATTACK], true))
		hub.set_context({"charge_ring": {"slot": "attack", "tier": 2, "frac": 0.45, "max": 3}})
	if shot == "active" or shot == "strong" or shot == "lefty" or shot == "light" or shot == "compact":
		_inject_active(hub.touch, l, vis)
	elif shot == "settings" or shot == "practice":
		hub.controls_visible = false
		var panel := hub.settings_panel
		panel.set_practice_items([
			{"id": "stone", "title": "Stone shot", "subtitle": "Lift a rock and throw it", "locked": false},
			{"id": "phase", "title": "Phase lab", "subtitle": "Heat stone until it flows", "locked": false},
			{"id": "steam", "title": "Steam burst", "subtitle": "Water meets hot rock", "locked": true},
		])
		panel.open_panel()
		if shot == "practice":
			panel._on_practice()
		paused = false  # keep rendering/animation; the panel is what we shoot

	for i in 6:
		hub.touch._process(1.0 / 60.0)
		await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	var path := "%s/hud_%s_%s.png" % [out_dir, device, shot]
	img.save_png(path)
	print("saved ", path, " ", img.get_size())

	hub.touch.release_all(true)
	root.remove_child(hub)
	hub.free()
	root.remove_child(bg)
	bg.free()
	for ch in root.get_children():
		if ch is ColorRect:
			root.remove_child(ch)
			ch.free()
	await process_frame


func _ev(idx: int, pos: Vector2, pressed: bool) -> InputEventScreenTouch:
	var e := InputEventScreenTouch.new()
	e.index = idx
	e.position = pos
	e.pressed = pressed
	return e


func _drag(idx: int, pos: Vector2) -> InputEventScreenDrag:
	var e := InputEventScreenDrag.new()
	e.index = idx
	e.position = pos
	return e


func _inject_active(c: TouchControls, l: TouchLayout, vis: Vector2) -> void:
	var sign_x := -1.0 if l.left_handed else 1.0
	# Thumb on the stick, pushed up-right (up-left when mirrored).
	var sp := Vector2(vis.x * (0.2 if not l.left_handed else 0.8), vis.y * 0.72)
	c._input(_ev(0, sp, true))
	c._input(_drag(0, sp + Vector2(0.7 * sign_x, -0.55) * l.stick_radius))
	# Guard held.
	c._input(_ev(1, l.centers[Id.GUARD], true))
	# Technique held, aiming up and slightly away.
	var t := l.centers[Id.TECH]
	c._input(_ev(2, t, true))
	c._input(_drag(2, t + Vector2(0.45 * -sign_x, -0.8) * l.aim_radius * 0.9))
	# Attack held long enough to show the charge ring.
	c._input(_ev(3, l.centers[Id.ATTACK], true))
	# A camera finger.
	var cam := Vector2(l.split_x + 60.0 * sign_x, vis.y * 0.62)
	c._input(_ev(4, cam, true))
	c._input(_drag(4, cam + Vector2(40, 10)))
	for i in 20:
		c._process(1.0 / 60.0)
