class_name UiStyle
extends RefCounted
## Shared look for the touch HUD and the settings panel: palette, element
## glyphs (simple vector line art, no fonts/textures) and a code-built Theme.

enum Glyph { EARTH, WATER, FIRE, AIR, ATTACK, GUARD, EVADE, TARGET, PAUSE, CANCEL }

const ELEMENT_COUNT := 4
const ELEMENT_NAMES: Array[String] = ["Earth", "Water", "Fire", "Air"]
## Element order matches Sim.Element: EARTH, WATER, FIRE, AIR.
const ELEMENT_COLORS: Array[Color] = [
	Color(0.84, 0.66, 0.38),
	Color(0.40, 0.70, 0.97),
	Color(0.98, 0.47, 0.30),
	Color(0.72, 0.90, 0.86),
]

const INK := Color(0.93, 0.95, 0.97)
const INK_DIM := Color(0.93, 0.95, 0.97, 0.62)
const SURFACE := Color(0.075, 0.085, 0.105, 0.985)
const SURFACE_RAISED := Color(0.13, 0.145, 0.17, 0.96)
const SURFACE_HOVER := Color(0.18, 0.2, 0.235, 0.98)
const LINE := Color(1, 1, 1, 0.14)
const ACCENT := Color(0.62, 0.82, 1.0)
const DANGER := Color(0.96, 0.45, 0.42)

static var _glyphs: Array = []


static func element_color(e: int) -> Color:
	return ELEMENT_COLORS[clampi(e, 0, ELEMENT_COUNT - 1)]


## Draw a glyph centred on `c`, fitting a circle of radius `r`.
static func draw_glyph(ci: CanvasItem, glyph: int, c: Vector2, r: float, color: Color, width: float) -> void:
	if _glyphs.is_empty():
		_build_glyphs()
	var lines: Array = _glyphs[glyph]
	for pts: PackedVector2Array in lines:
		var n := pts.size()
		var out := PackedVector2Array()
		out.resize(n)
		for i in n:
			out[i] = c + pts[i] * r
		ci.draw_polyline(out, color, width, true)


# --- glyph construction (unit space, y down, fits radius 1) -----------------

static func _build_glyphs() -> void:
	_glyphs.clear()
	_glyphs.resize(10)
	# Earth: faceted mountain.
	_glyphs[Glyph.EARTH] = [
		_closed(PackedVector2Array([
			Vector2(-1.0, 0.62), Vector2(-0.42, -0.34), Vector2(-0.08, 0.12),
			Vector2(0.34, -0.7), Vector2(1.0, 0.62)])),
		PackedVector2Array([Vector2(0.34, -0.7), Vector2(0.18, -0.34), Vector2(0.5, -0.34)]),
	]
	# Water: droplet.
	var drop := PackedVector2Array()
	drop.append(Vector2(0.0, -1.0))
	var cen := Vector2(0.0, 0.26)
	var rr := 0.66
	var a0 := deg_to_rad(-31.3)
	var a1 := deg_to_rad(148.7)
	var steps := 18
	for i in steps + 1:
		var a := lerpf(a0, a1, float(i) / steps)
		drop.append(cen + Vector2(cos(a), sin(a)) * rr)
	drop.append(Vector2(0.0, -1.0))
	_glyphs[Glyph.WATER] = [drop, _arc_pts(Vector2(0.0, 0.3), 0.3, deg_to_rad(20.0), deg_to_rad(100.0), 8)]
	# Fire: flame with an inner lick.
	var flame := PackedVector2Array([
		Vector2(0.12, -1.0), Vector2(0.4, -0.55), Vector2(0.66, 0.05), Vector2(0.58, 0.52),
		Vector2(0.28, 0.88), Vector2(-0.1, 0.96), Vector2(-0.46, 0.76), Vector2(-0.64, 0.38),
		Vector2(-0.55, -0.02), Vector2(-0.34, -0.3), Vector2(-0.14, -0.04), Vector2(-0.1, -0.5)])
	var inner := PackedVector2Array([
		Vector2(0.02, 0.28), Vector2(0.24, 0.58), Vector2(0.06, 0.82),
		Vector2(-0.2, 0.62)])
	_glyphs[Glyph.FIRE] = [_chaikin(flame, 2, true), _chaikin(inner, 2, true)]
	# Air: three wind lines with curls.
	var l1 := PackedVector2Array([Vector2(-0.95, -0.42), Vector2(0.28, -0.42)])
	l1.append_array(_arc_pts(Vector2(0.28, -0.7), 0.28, deg_to_rad(90.0), deg_to_rad(-150.0), 14))
	var l2 := PackedVector2Array([Vector2(-0.95, 0.04), Vector2(0.62, 0.04)])
	l2.append_array(_arc_pts(Vector2(0.62, 0.32), 0.28, deg_to_rad(-90.0), deg_to_rad(150.0), 14))
	var l3 := PackedVector2Array([Vector2(-0.6, 0.5), Vector2(0.1, 0.5)])
	l3.append_array(_arc_pts(Vector2(0.1, 0.72), 0.22, deg_to_rad(-90.0), deg_to_rad(120.0), 10))
	_glyphs[Glyph.AIR] = [l1, l2, l3]
	# Attack: three slashes.
	_glyphs[Glyph.ATTACK] = [
		PackedVector2Array([Vector2(-0.72, 0.78), Vector2(0.78, -0.72)]),
		PackedVector2Array([Vector2(-0.98, 0.3), Vector2(0.3, -0.98)]),
		PackedVector2Array([Vector2(-0.3, 0.98), Vector2(0.98, -0.3)]),
	]
	# Guard: shield.
	var shield := PackedVector2Array([
		Vector2(-0.72, -0.62), Vector2(0.0, -0.88), Vector2(0.72, -0.62), Vector2(0.72, 0.08),
		Vector2(0.4, 0.58), Vector2(0.0, 0.9), Vector2(-0.4, 0.58), Vector2(-0.72, 0.08)])
	_glyphs[Glyph.GUARD] = [
		_closed(shield),
		PackedVector2Array([Vector2(0.0, -0.5), Vector2(0.0, 0.5)]),
	]
	# Evade: double chevron.
	_glyphs[Glyph.EVADE] = [
		PackedVector2Array([Vector2(-0.82, -0.6), Vector2(-0.22, 0.0), Vector2(-0.82, 0.6)]),
		PackedVector2Array([Vector2(0.0, -0.6), Vector2(0.6, 0.0), Vector2(0.0, 0.6)]),
	]
	# Target: reticle.
	var ret := _arc_pts(Vector2.ZERO, 0.52, 0.0, TAU, 28)
	_glyphs[Glyph.TARGET] = [
		ret,
		PackedVector2Array([Vector2(0.0, -1.0), Vector2(0.0, -0.74)]),
		PackedVector2Array([Vector2(0.0, 1.0), Vector2(0.0, 0.74)]),
		PackedVector2Array([Vector2(-1.0, 0.0), Vector2(-0.74, 0.0)]),
		PackedVector2Array([Vector2(1.0, 0.0), Vector2(0.74, 0.0)]),
	]
	# Pause: two bars.
	_glyphs[Glyph.PAUSE] = [
		PackedVector2Array([Vector2(-0.36, -0.72), Vector2(-0.36, 0.72)]),
		PackedVector2Array([Vector2(0.36, -0.72), Vector2(0.36, 0.72)]),
	]
	# Cancel: cross.
	_glyphs[Glyph.CANCEL] = [
		PackedVector2Array([Vector2(-0.7, -0.7), Vector2(0.7, 0.7)]),
		PackedVector2Array([Vector2(0.7, -0.7), Vector2(-0.7, 0.7)]),
	]


static func _closed(p: PackedVector2Array) -> PackedVector2Array:
	var out := p.duplicate()
	out.append(p[0])
	return out


static func _arc_pts(c: Vector2, r: float, a0: float, a1: float, steps: int) -> PackedVector2Array:
	var out := PackedVector2Array()
	for i in steps + 1:
		var a := lerpf(a0, a1, float(i) / steps)
		out.append(c + Vector2(cos(a), sin(a)) * r)
	return out


## Chaikin corner cutting. `closed` wraps (and the result is closed again).
static func _chaikin(p: PackedVector2Array, iters: int, closed: bool) -> PackedVector2Array:
	var cur := p
	for _it in iters:
		var nxt := PackedVector2Array()
		var n := cur.size()
		var last := n if closed else n - 1
		if not closed:
			nxt.append(cur[0])
		for i in last:
			var a := cur[i]
			var b := cur[(i + 1) % n]
			nxt.append(a.lerp(b, 0.25))
			nxt.append(a.lerp(b, 0.75))
		if not closed:
			nxt.append(cur[n - 1])
		cur = nxt
	if closed:
		cur.append(cur[0])
	return cur


# --- Theme --------------------------------------------------------------------

## Build the settings-panel Theme. `ppm` = viewport units per millimetre.
static func build_theme(ppm: float) -> Theme:
	var t := Theme.new()
	var fs := int(round(clampf(3.1 * ppm, 15.0, 44.0)))
	var pad := int(round(2.2 * ppm))
	var radius := int(round(1.6 * ppm))
	t.default_font_size = fs
	t.set_color("font_color", "Label", INK)
	t.set_color("font_color", "Button", INK)
	t.set_color("font_hover_color", "Button", Color.WHITE)
	t.set_color("font_pressed_color", "Button", Color.WHITE)
	t.set_color("font_focus_color", "Button", Color.WHITE)
	t.set_color("font_disabled_color", "Button", Color(1, 1, 1, 0.32))
	t.set_font_size("font_size", "Button", fs)
	t.set_font_size("font_size", "Label", fs)
	t.set_font_size("font_size", "CheckButton", fs)
	for ctl in ["CheckButton", "OptionButton"]:
		t.set_color("font_color", ctl, INK)
		t.set_color("font_hover_color", ctl, Color.WHITE)
		t.set_color("font_pressed_color", ctl, Color.WHITE)
		t.set_color("font_focus_color", ctl, Color.WHITE)

	var normal := _box(SURFACE_RAISED, LINE, radius, pad)
	var hover := _box(SURFACE_HOVER, Color(1, 1, 1, 0.24), radius, pad)
	var pressed := _box(Color(0.22, 0.26, 0.32, 1.0), Color(ACCENT, 0.7), radius, pad)
	var disabled := _box(Color(0.1, 0.11, 0.13, 0.7), Color(1, 1, 1, 0.06), radius, pad)
	var focus := _box(Color(0, 0, 0, 0), Color(ACCENT, 0.9), radius, pad)
	focus.draw_center = false
	focus.set_border_width_all(maxi(2, int(round(0.28 * ppm))))
	for ctl in ["Button", "OptionButton", "CheckButton"]:
		t.set_stylebox("normal", ctl, normal)
		t.set_stylebox("hover", ctl, hover)
		t.set_stylebox("pressed", ctl, pressed)
		t.set_stylebox("disabled", ctl, disabled)
		t.set_stylebox("focus", ctl, focus)
	# Toggle rows are flat: only a faint highlight on hover/focus.
	var row_flat := _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), radius, pad)
	var row_hover := _box(Color(1, 1, 1, 0.05), Color(0, 0, 0, 0), radius, pad)
	t.set_stylebox("normal", "CheckButton", row_flat)
	t.set_stylebox("pressed", "CheckButton", row_flat)
	t.set_stylebox("hover", "CheckButton", row_hover)
	t.set_stylebox("hover_pressed", "CheckButton", row_hover)
	t.set_stylebox("disabled", "CheckButton", row_flat)

	var panel := _box(SURFACE, Color(1, 1, 1, 0.1), int(round(2.4 * ppm)), int(round(3.0 * ppm)))
	t.set_stylebox("panel", "PanelContainer", panel)
	t.set_stylebox("panel", "PopupPanel", panel)

	# Sliders: thin track, large grab.
	var track := StyleBoxFlat.new()
	track.bg_color = Color(1, 1, 1, 0.16)
	track.set_corner_radius_all(int(ppm))
	track.content_margin_top = 0.5 * ppm
	track.content_margin_bottom = 0.5 * ppm
	var fill := StyleBoxFlat.new()
	fill.bg_color = Color(ACCENT, 0.85)
	fill.set_corner_radius_all(int(ppm))
	fill.content_margin_top = 0.5 * ppm
	fill.content_margin_bottom = 0.5 * ppm
	t.set_stylebox("slider", "HSlider", track)
	t.set_stylebox("grabber_area", "HSlider", fill)
	t.set_stylebox("grabber_area_highlight", "HSlider", fill)
	var grab := _make_circle_icon(int(round(5.2 * ppm)), Color.WHITE)
	var grab_hi := _make_circle_icon(int(round(5.2 * ppm)), ACCENT)
	t.set_icon("grabber", "HSlider", grab)
	t.set_icon("grabber_highlight", "HSlider", grab_hi)
	t.set_icon("grabber_disabled", "HSlider", grab)
	t.set_constant("center_grabber", "HSlider", 0)

	# Check button toggle icons: simple pill switch.
	var sw := int(round(7.0 * ppm))
	var sh := int(round(3.8 * ppm))
	t.set_icon("unchecked", "CheckButton", _make_switch_icon(sw, sh, false))
	t.set_icon("checked", "CheckButton", _make_switch_icon(sw, sh, true))
	t.set_icon("unchecked_disabled", "CheckButton", _make_switch_icon(sw, sh, false))
	t.set_icon("checked_disabled", "CheckButton", _make_switch_icon(sw, sh, true))
	t.set_icon("unchecked_mirrored", "CheckButton", _make_switch_icon(sw, sh, false))
	t.set_icon("checked_mirrored", "CheckButton", _make_switch_icon(sw, sh, true))
	t.set_icon("unchecked_disabled_mirrored", "CheckButton", _make_switch_icon(sw, sh, false))
	t.set_icon("checked_disabled_mirrored", "CheckButton", _make_switch_icon(sw, sh, true))
	t.set_constant("h_separation", "CheckButton", int(round(2.0 * ppm)))

	# Scrollbars: wide enough to grab.
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(1, 1, 1, 0.05)
	sb.set_corner_radius_all(int(ppm))
	var sg := StyleBoxFlat.new()
	sg.bg_color = Color(1, 1, 1, 0.28)
	sg.set_corner_radius_all(int(ppm))
	t.set_stylebox("scroll", "VScrollBar", sb)
	t.set_stylebox("grabber", "VScrollBar", sg)
	t.set_stylebox("grabber_highlight", "VScrollBar", sg)
	t.set_stylebox("grabber_pressed", "VScrollBar", sg)
	t.set_constant("separation", "VBoxContainer", int(round(1.4 * ppm)))
	t.set_constant("separation", "HBoxContainer", int(round(1.6 * ppm)))
	return t


static func _box(bg: Color, border: Color, radius: int, pad: int) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.border_color = border
	s.set_border_width_all(1)
	s.set_corner_radius_all(radius)
	s.content_margin_left = pad
	s.content_margin_right = pad
	s.content_margin_top = pad * 0.6
	s.content_margin_bottom = pad * 0.6
	s.anti_aliasing = true
	return s


static func _make_circle_icon(d: int, col: Color) -> Texture2D:
	d = maxi(d, 8)
	var img := Image.create(d, d, false, Image.FORMAT_RGBA8)
	var c := Vector2(d, d) * 0.5
	var r := d * 0.5 - 1.0
	for y in d:
		for x in d:
			var dist := Vector2(x + 0.5, y + 0.5).distance_to(c)
			var a := clampf(r - dist + 0.5, 0.0, 1.0)
			img.set_pixel(x, y, Color(col.r, col.g, col.b, a))
	return ImageTexture.create_from_image(img)


static func _make_switch_icon(w: int, h: int, on: bool) -> Texture2D:
	w = maxi(w, 16)
	h = maxi(h, 8)
	var img := Image.create(w, h, false, Image.FORMAT_RGBA8)
	var track := Color(ACCENT.r, ACCENT.g, ACCENT.b, 0.85) if on else Color(1, 1, 1, 0.2)
	var knob := Color.WHITE if on else Color(1, 1, 1, 0.8)
	var rr := h * 0.5
	var kx := (w - rr) if on else rr
	for y in h:
		for x in w:
			var p := Vector2(x + 0.5, y + 0.5)
			# Stadium distance for the track.
			var cx := clampf(p.x, rr, w - rr)
			var d := p.distance_to(Vector2(cx, rr))
			var a := clampf(rr - d + 0.5, 0.0, 1.0)
			var col := Color(track.r, track.g, track.b, track.a * a)
			var kd := p.distance_to(Vector2(kx, rr))
			var ka := clampf(rr * 0.74 - kd + 0.5, 0.0, 1.0)
			if ka > 0.0:
				col = col.lerp(Color(knob.r, knob.g, knob.b, 1.0), ka)
				col.a = maxf(col.a, ka)
			img.set_pixel(x, y, col)
	return ImageTexture.create_from_image(img)
