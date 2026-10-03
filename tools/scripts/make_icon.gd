extends SceneTree
## Generates the original app icon (four elemental arcs around a still centre).
## tools/scripts/godot.sh --headless -s res://../tools/scripts/make_icon.gd  (or copy into res://)
func _init() -> void:
	var n := 1024
	# Opaque RGB: App Store Connect rejects a 1024 px app icon that has an alpha channel.
	var img := Image.create(n, n, false, Image.FORMAT_RGB8)
	var bg0 := Color(0.09, 0.1, 0.12)
	var bg1 := Color(0.16, 0.17, 0.2)
	var cols := [Color(0.72, 0.58, 0.36), Color(0.38, 0.66, 0.8), Color(0.93, 0.46, 0.24), Color(0.84, 0.87, 0.82)]
	var c := Vector2(n, n) * 0.5
	for y in n:
		for x in n:
			var p := Vector2(x + 0.5, y + 0.5) - c
			var r := p.length() / (n * 0.5)
			var col := bg1.lerp(bg0, clampf(r, 0.0, 1.0))
			var ang := fposmod(atan2(p.y, p.x) + PI * 0.25, TAU)
			var q := int(ang / (PI * 0.5))
			var local := fmod(ang, PI * 0.5) / (PI * 0.5)
			# Arc band with tapered ends, slight swirl.
			var band := absf(r - 0.6 - 0.05 * (local - 0.5))
			var w := 0.07 * sin(local * PI)
			if band < w and local > 0.06 and local < 0.94:
				var a := clampf((w - band) / 0.01, 0.0, 1.0)
				col = col.lerp(cols[q], a)
			if r < 0.12:
				col = col.lerp(Color(0.95, 0.92, 0.85), clampf((0.12 - r) / 0.01, 0.0, 1.0))
			img.set_pixel(x, y, col)
	img.save_png("res://assets/icon/icon_1024.png")
	print("icon written")
	quit(0)
