class_name UiScale
extends RefCounted
## Physical-size heuristics for touch UI. All results are in *viewport units*
## (the coordinates InputEventScreenTouch / Control use), so they stay correct
## under the project's canvas_items stretch.

## Reference phone-ish pixel density used when the OS reports nothing useful.
const FALLBACK_DPI := 160.0

## Optional overrides (screenshots, tests, a future "force iPad" debug flag).
static var dpi_override: float = 0.0


## Viewport units per millimetre of physical screen.
static func px_per_mm(vp: Viewport) -> float:
	var vp_size := vp.get_visible_rect().size
	var win_scale := window_scale(vp)
	var dpi := dpi_override
	var forced := dpi > 0.0
	if not forced:
		var d := DisplayServer.screen_get_dpi(DisplayServer.window_get_current_screen())
		dpi = float(d) if d > 0 else FALLBACK_DPI
	var ppm := dpi / 25.4 / maxf(win_scale, 0.001)
	if not forced and not OS.has_feature("mobile"):
		# Desktop dev builds: monitors report ~96 dpi, which would make a
		# "13 mm" button tiny. Keep the touch UI legible for testing.
		ppm = maxf(ppm, 0.0108 * vp_size.y)
	# Guard against absurd DPI reports.
	return clampf(ppm, 0.0040 * vp_size.y, 0.0225 * vp_size.y)


## Window pixels per viewport unit (stretch factor).
static func window_scale(vp: Viewport) -> float:
	var vis := vp.get_visible_rect().size
	var win := vp.get_window()
	if win == null or vis.y <= 0.0:
		return 1.0
	var s := float(win.size.y) / vis.y
	return s if s > 0.0 else 1.0


## Safe-area insets (left, top, right, bottom) in viewport units.
static func safe_insets(vp: Viewport) -> Vector4:
	var win := vp.get_window()
	if win == null:
		return Vector4.ZERO
	var win_size := Vector2(win.size)
	var s := window_scale(vp)
	var safe := DisplayServer.get_display_safe_area()
	if safe.size.x <= 0 or safe.size.y <= 0:
		return Vector4.ZERO
	var win_pos := Vector2(DisplayServer.window_get_position(win.get_window_id()))
	var sp := Vector2(safe.position) - win_pos
	var se := Vector2(safe.end) - win_pos
	var l := maxf(0.0, sp.x)
	var t := maxf(0.0, sp.y)
	var r := maxf(0.0, win_size.x - se.x)
	var b := maxf(0.0, win_size.y - se.y)
	# A window that is larger than / not inside the screen yields nonsense
	# insets (desktop). Real notches / home indicators are a few % of an axis.
	if l > win_size.x * 0.12 or r > win_size.x * 0.12 or t > win_size.y * 0.15 or b > win_size.y * 0.15:
		return Vector4.ZERO
	return Vector4(l, t, r, b) / s
