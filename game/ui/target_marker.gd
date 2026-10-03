class_name TargetMarker
extends Control
## Lock-on reticle drawn at a screen position supplied by the game each tick
## (PlayerInputHub.set_context -> "target_screen_pos"). Thin corner brackets
## plus an optional label; eases toward the position so it never jitters.

var _has_target := false
var _pos := Vector2.ZERO
var _shown_pos := Vector2.ZERO
var _alpha := 0.0
var _label := ""
var _element_tint := Color(1, 1, 1)
var _font: Font = null


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_NONE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)


func _ready() -> void:
	_font = ThemeDB.fallback_font


## pos: Vector2 to show the marker there, anything else (null) hides it.
## label: String, or null to keep the current label.
func set_target(pos: Variant, label: Variant = null) -> void:
	if pos is Vector2:
		var p: Vector2 = pos
		if not _has_target:
			_shown_pos = p
		_pos = p
		_has_target = true
	else:
		_has_target = false
	if label != null:
		_label = str(label)
	queue_redraw()


func set_label(label: String) -> void:
	_label = label
	queue_redraw()


func has_target() -> bool:
	return _has_target


func set_tint(c: Color) -> void:
	_element_tint = c


func _process(delta: float) -> void:
	var want := 1.0 if _has_target else 0.0
	var reduced := GameSettings.current().reduced_motion
	var changed := false
	if not is_equal_approx(_alpha, want):
		_alpha = want if reduced else move_toward(_alpha, want, delta * 8.0)
		changed = true
	if _alpha > 0.0:
		var k := 1.0 if reduced else minf(1.0, delta * 22.0)
		var np := _shown_pos.lerp(_pos, k)
		if not np.is_equal_approx(_shown_pos):
			_shown_pos = np
			changed = true
		# Slow breathing keeps the marker alive without being distracting.
		if not reduced:
			changed = true
	if changed:
		queue_redraw()


func _draw() -> void:
	if _alpha <= 0.01:
		return
	if _font == null:
		_font = ThemeDB.fallback_font
	var vp := get_viewport_rect().size
	var ppm := UiScale.px_per_mm(get_viewport())
	var r := 4.6 * ppm
	var c := _shown_pos
	c.x = clampf(c.x, r, vp.x - r)
	c.y = clampf(c.y, r, vp.y - r)
	var pulse := 1.0
	if not GameSettings.current().reduced_motion:
		pulse = 1.0 + 0.04 * sin(Time.get_ticks_msec() * 0.005)
	r *= pulse
	var w := maxf(1.6, 0.2 * ppm)
	var col := Color(1.0, 0.93, 0.78, 0.85 * _alpha)
	var shadow := Color(0, 0, 0, 0.35 * _alpha)
	var arm := r * 0.55
	for sx in [-1.0, 1.0]:
		for sy in [-1.0, 1.0]:
			var corner := c + Vector2(sx * r, sy * r)
			var a := corner - Vector2(sx * arm, 0)
			var b := corner - Vector2(0, sy * arm)
			draw_polyline(PackedVector2Array([a + Vector2(1, 1.5), corner + Vector2(1, 1.5), b + Vector2(1, 1.5)]), shadow, w * 1.6, true)
			draw_polyline(PackedVector2Array([a, corner, b]), col, w, true)
	draw_circle(c, w * 1.1, col)
	if _label != "":
		var fs := int(round(1.9 * ppm))
		var pos := c + Vector2(-r * 3.0, -r - fs * 0.5)
		draw_string_outline(_font, pos, _label, HORIZONTAL_ALIGNMENT_CENTER, r * 6.0, fs, 3, Color(0, 0, 0, 0.55 * _alpha))
		draw_string(_font, pos, _label, HORIZONTAL_ALIGNMENT_CENTER, r * 6.0, fs, Color(1, 1, 1, 0.8 * _alpha))
