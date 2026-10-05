extends SceneTree
## Renders the real Game scene (Lab scenario, touch HUD forced) and saves PNGs of the in-play HUD (idle,
## ATTACK petals, GUARD labels, sub-element ring, charge ring, status icons) and every Lab dev-panel page.
##   tools/scripts/godot.sh --render --resolution 2532x1170 -s res://tests/ui/render_lab_screenshot.gd -- \
##       --device=phone --out=/tmp/shots --touchui --scenario=lab
##   tools/scripts/godot.sh --render --resolution 2732x2048 -s res://tests/ui/render_lab_screenshot.gd -- \
##       --device=ipad --out=/tmp/shots --touchui --scenario=lab
## Optional: --shots=idle,petals,guard,ring,charge,status,dev0,dev1,dev2,dev3,dev4,dev5
## Not part of the automated pass/fail suite (needs a renderer).

const Id := TouchLayout.Id

var game: Game
var device := "phone"
var out_dir := "/tmp"


func _initialize() -> void:
	_run()


func _arg(name: String, fallback: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--" + name + "="):
			return a.substr(name.length() + 3)
	return fallback


func _ev(idx: int, pos: Vector2, pressed: bool) -> InputEventScreenTouch:
	var e := InputEventScreenTouch.new()
	e.index = idx
	e.position = pos
	e.pressed = pressed
	return e


func _drag(idx: int, pos: Vector2, rel: Vector2 = Vector2.ZERO) -> InputEventScreenDrag:
	var e := InputEventScreenDrag.new()
	e.index = idx
	e.position = pos
	e.relative = rel
	return e


func _frames(n: int) -> void:
	for i in n:
		await process_frame


func _shot(name: String) -> void:
	await _frames(3)
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	var path := "%s/lab_%s_%s.png" % [out_dir, device, name]
	img.save_png(path)
	print("saved ", path, " ", img.get_size())


func _run() -> void:
	await process_frame
	device = _arg("device", "phone")
	out_dir = _arg("out", "/tmp")
	var shots := _arg("shots", "idle,petals,guard,ring,charge,status,dev0,dev1,dev2,dev3,dev4,dev5").split(",")
	DirAccess.make_dir_recursive_absolute(out_dir)
	UiScale.dpi_override = 460.0 if device == "phone" else 264.0
	var insets_px := Vector4(141, 0, 141, 63) if device == "phone" else Vector4(0, 0, 0, 40)

	var s := GameSettings.new()
	s.storage_path = "user://shot_settings.cfg"
	GameSettings.set_current(s)
	game = (load("res://main.tscn") as PackedScene).instantiate()
	root.add_child(game)
	await _frames(40)
	var win := Vector2(root.size)
	var vis := root.get_visible_rect().size
	var scale := win.y / vis.y
	var ppm := UiScale.px_per_mm(root)
	game.hub.touch.configure_layout(vis, insets_px / scale, ppm)
	print("window=", win, " viewport=", vis, " ppm=", snappedf(ppm, 0.01))
	var l := game.hub.touch.get_layout()
	var c := game.hub.touch

	for shot in shots:
		match shot:
			"idle":
				await _shot("idle")
			"petals":
				# ATTACK held and dragged up a little: the UP petal lights (flick preview).
				var a := l.centers[Id.ATTACK]
				root.push_input(_ev(1, a, true), true)
				await _frames(4)
				root.push_input(_drag(1, a + Vector2(0, -0.6 * l.ppm * 6.0), Vector2(0, -3)), true)
				await _frames(3)
				await _shot("petals")
				root.push_input(_ev(1, a + Vector2(0, -0.6 * l.ppm * 6.0), false), true)
				await _frames(10)
			"guard":
				var g := l.centers[Id.GUARD]
				root.push_input(_ev(2, g, true), true)
				await _frames(6)
				await _shot("guard")
				root.push_input(_ev(2, g, false), true)
				await _frames(4)
			"ring":
				# Tap the active element chip: the four sub-element petals open beside the chip arc.
				var chip: Vector2 = l.centers[Id.ELEM_0 + game.player.element]
				root.push_input(_ev(3, chip, true), true)
				root.push_input(_ev(3, chip, false), true)
				await _frames(6)
				await _shot("ring")
				c._close_ring()
				await _frames(2)
			"charge":
				# Hold ATTACK for a charged fire move: the tier ring fills.
				game.player.element = Sim.Element.FIRE
				game.player.subs[Sim.Element.FIRE] = 1
				var a2 := l.centers[Id.ATTACK]
				root.push_input(_ev(4, a2, true), true)
				await _frames(75)
				await _shot("charge")
				root.push_input(_ev(4, a2, false), true)
				await _frames(40)
			"status":
				game.player.status["burning"] = {"t": 3.0, "mag": 1.0, "src": -1}
				game.player.status["chilled"] = {"t": 5.0, "mag": 1.0, "src": -1}
				game.player.wetness = 0.8
				game.player.static_charge = 24.0
				game.player.element = Sim.Element.WATER
				game.player.water_carried = 3.5
				await _frames(8)
				await _shot("status")
			_:
				if shot.begins_with("dev"):
					var page := int(shot.substr(3))
					game.lab_panel.open_panel(page)
					await _frames(8)
					await _shot("dev%d" % page)
					game.lab_panel.close_panel()
					await _frames(4)
	quit()
