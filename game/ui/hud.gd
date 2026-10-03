class_name Hud
extends Control
## Minimal in-play HUD: thin vitals (fade when safe), heat reserve when non-zero,
## rival vitals while locked on, objective line, short toasts, off-screen threat
## arrows and an optional debug/state overlay (Settings > show debug).

## Physical floors (mm) for HUD text and bars. iPhone landscape is ~11 viewport
## units per mm, where the viewport-scaled sizes alone give ~1 mm glyphs.
const TEXT_MM := 2.0     # stat text, rival name (~11-12 pt)
const LINE_MM := 2.2     # objective / challenge lines
const TOAST_MM := 3.0
const BAR_MM := 0.6

var world: CombatWorld
var player_id := 1
var cam: CameraRig
var objective := ""
var challenge_text := ""
var show_debug := false
var debug_lines: PackedStringArray = []
var perf_text := ""

var _toast := ""
var _toast_t := 0.0
var _flash := ""
var _flash_t := 0.0
var _alpha := 0.35
var _calm := 0.0
var _font: Font
var _last_health := 100.0
# Per-draw sizing: viewport scale, physical density (0 = unknown) and bar thickness.
var _s := 1.0
var _ppm := 0.0
var _bar_h := 4.0


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_font = get_theme_default_font()


func toast(text: String) -> void:
	_toast = text
	_toast_t = 1.8


func flash(kind: String) -> void:
	_flash = kind
	_flash_t = 0.18


func _process(dt: float) -> void:
	_toast_t = maxf(0.0, _toast_t - dt)
	_flash_t = maxf(0.0, _flash_t - dt)
	var p := world.get_actor(player_id) if world else null
	if p:
		var busy := p.action != null or p.stun > 0.0 or p.health < _last_health - 0.01 or p.focus < 99.0 or p.heat_reserve > 0.0
		_last_health = p.health
		_calm = 0.0 if busy else _calm + dt
	_alpha = lerpf(_alpha, 0.35 if _calm > 2.5 else 1.0, 1.0 - exp(-6.0 * dt))
	queue_redraw()


func _safe_rect() -> Rect2:
	var vr := get_viewport_rect()
	var sa := DisplayServer.get_display_safe_area()
	var ss := DisplayServer.screen_get_size()
	if ss.x <= 0 or sa.size.x <= 0:
		return vr.grow(-16)
	var sx := vr.size.x / float(ss.x)
	var sy := vr.size.y / float(ss.y)
	return Rect2(Vector2(sa.position) * Vector2(sx, sy), Vector2(sa.size) * Vector2(sx, sy)).grow(-12)


## Viewport units per mm where the density is real (device, or a pinned DPI for
## screenshots/tests), else 0: desktop keeps the viewport-scaled sizes unchanged.
static func density_ppm(vp: Viewport) -> float:
	if vp == null or not (OS.has_feature("mobile") or UiScale.dpi_override > 0.0):
		return 0.0
	return UiScale.px_per_mm(vp)


## Font size: the viewport-scaled `base`, never below `mm` millimetres when `ppm` > 0.
static func font_px(base: float, mm: float, s: float, ppm: float) -> int:
	return maxi(int(base * s), int(round(mm * ppm)))


func _fs(base: float, mm: float) -> int:
	return font_px(base, mm, _s, _ppm)


func _bar(pos: Vector2, w: float, frac: float, col: Color, a: float) -> void:
	draw_rect(Rect2(pos, Vector2(w, _bar_h)), Color(0, 0, 0, 0.35 * a))
	draw_rect(Rect2(pos, Vector2(w * clampf(frac, 0.0, 1.0), _bar_h)), Color(col, a))


func _draw() -> void:
	if world == null:
		return
	var sr := _safe_rect()
	var s := clampf(sr.size.y / 720.0, 0.8, 2.0)
	var p := world.get_actor(player_id)
	if p == null:
		return
	_s = s
	_ppm = density_ppm(get_viewport())
	_bar_h = maxf(4.0, BAR_MM * _ppm)
	var fs_stat := _fs(11, TEXT_MM)
	var fs_name := _fs(12, TEXT_MM)
	var fs_line := _fs(13, LINE_MM)
	# Growth over the viewport-scaled layout (all 0 on desktop) pushes rows apart.
	var grow_bar := _bar_h - 4.0
	var grow_stat := float(fs_stat - int(11 * s))
	var grow_line := float(fs_line - int(13 * s))
	var row := 9 * s + grow_bar
	var x := sr.position.x + 8 * s
	var y := sr.position.y + 8 * s
	var bw := 190.0 * s
	_bar(Vector2(x, y), bw, p.health / Sim.HEALTH_MAX, Color(0.92, 0.9, 0.86), _alpha)
	_bar(Vector2(x, y + row), bw * 0.8, p.balance / Sim.BALANCE_MAX, Color(0.75, 0.82, 0.92), _alpha * 0.9)
	_bar(Vector2(x, y + 2 * row), bw * 0.8, p.focus / Sim.FOCUS_MAX, Color(0.96, 0.82, 0.45), _alpha * 0.9)
	# The HEAT label sits beside its bar, under the end of the longer focus bar: keep it clear.
	var heat_y := y + 3 * row + grow_stat * 0.5
	var heat_base := heat_y + 6 * s + grow_bar * 0.5 + grow_stat * 0.35
	if p.heat_reserve > 1.0:
		_bar(Vector2(x, heat_y), bw * 0.6, p.heat_reserve / Sim.RESERVE_MAX, Color(1.0, 0.45, 0.2), 1.0)
		draw_string(_font, Vector2(x + bw * 0.62, heat_base), "HEAT %d" % int(p.heat_reserve), HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, Color(1, 0.6, 0.4, 0.9))
	var water_base := heat_base + 11 * s + grow_stat
	if p.element == Sim.Element.WATER:
		draw_string(_font, Vector2(x, water_base), "water %.0f kg" % p.water_carried, HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, Color(0.6, 0.85, 1.0, 0.8 * _alpha))
	# Rival vitals (locked target), top centre, compact.
	var t := world.get_actor(p.lock_target)
	if t != null:
		var cx := sr.get_center().x
		_bar(Vector2(cx - 80 * s, y), 160 * s, t.health / Sim.HEALTH_MAX, Color(0.95, 0.55, 0.45), 0.85)
		_bar(Vector2(cx - 64 * s, y + row), 128 * s, t.balance / Sim.BALANCE_MAX, Color(0.75, 0.82, 0.92), 0.7)
		var name_base := y + 28 * s + grow_bar + (fs_name - int(12 * s)) * 0.75
		draw_string(_font, Vector2(cx - 80 * s, name_base), t.name, HORIZONTAL_ALIGNMENT_CENTER, 160 * s, fs_name, Color(1, 1, 1, 0.7))
	# Objective + challenge.
	var obj_base := sr.end.y - 10 * s
	if objective != "":
		draw_string(_font, Vector2(sr.position.x, obj_base), objective, HORIZONTAL_ALIGNMENT_CENTER, sr.size.x, fs_line, Color(1, 1, 1, 0.55))
	if challenge_text != "":
		draw_string(_font, Vector2(sr.position.x, obj_base - 18 * s - grow_line), challenge_text, HORIZONTAL_ALIGNMENT_CENTER, sr.size.x, fs_line, Color(1.0, 0.86, 0.55, 0.85))
	if _toast_t > 0.0:
		var a := clampf(_toast_t / 0.4, 0.0, 1.0)
		draw_string(_font, Vector2(sr.position.x, sr.get_center().y - 90 * s), _toast, HORIZONTAL_ALIGNMENT_CENTER, sr.size.x, _fs(18, TOAST_MM), Color(1, 1, 1, 0.9 * a))
	if _flash_t > 0.0:
		var col := Color(1.0, 0.95, 0.8, 0.10) if _flash == "perfect" else Color(0.8, 0.85, 1.0, 0.08)
		if GameSettings.current().flashes > 0.0:
			col.a *= GameSettings.current().flashes
			draw_rect(get_viewport_rect(), col)
	_threat_arrows(sr, s)
	if show_debug:
		_timing_bars(s)
		var dy := water_base + 16 * s
		for line in debug_lines:
			draw_string(_font, Vector2(x, dy), line, HORIZONTAL_ALIGNMENT_LEFT, -1, int(11 * s), Color(0.85, 1.0, 0.85, 0.9))
			dy += 14 * s
		draw_string(_font, Vector2(sr.end.x - 260 * s, sr.end.y - 50 * s - grow_line), perf_text, HORIZONTAL_ALIGNMENT_LEFT, -1, int(11 * s), Color(0.85, 1.0, 0.85, 0.9))


func _timing_bars(s: float) -> void:
	## Practice overlay: each fighter's current move as startup / active / recovery segments.
	if cam == null:
		return
	for a in world.actors:
		var inst := a.action
		if inst == null or inst.id == "guard":
			continue
		var sp: Variant = cam.world_to_screen(a.pos + Vector3(0, 2.1, 0))
		if sp == null:
			continue
		var su := float(inst.data.get("startup", inst.def.startup))
		var ac := float(inst.data.get("active", inst.def.active))
		var rc := float(inst.def.recovery)
		var tot := maxf(su + ac + rc, 0.01)
		var w := 90.0 * s
		var o: Vector2 = (sp as Vector2) - Vector2(w * 0.5, 0)
		draw_rect(Rect2(o, Vector2(w * su / tot, 5 * s)), Color(1.0, 0.85, 0.3, 0.85))
		draw_rect(Rect2(o + Vector2(w * su / tot, 0), Vector2(w * ac / tot, 5 * s)), Color(1.0, 0.35, 0.3, 0.9))
		draw_rect(Rect2(o + Vector2(w * (su + ac) / tot, 0), Vector2(w * rc / tot, 5 * s)), Color(0.6, 0.6, 0.65, 0.8))
		var elapsed := inst.total
		if inst.phase == ActionInst.P.CHARGE or inst.phase == ActionInst.P.CHANNEL:
			elapsed = su
		draw_rect(Rect2(o + Vector2(w * clampf(elapsed / tot, 0.0, 1.0) - 1.0, -3 * s), Vector2(2, 11 * s)), Color.WHITE)
		draw_string(_font, o + Vector2(0, -5 * s), "%s %s" % [inst.id, inst.phase_name()], HORIZONTAL_ALIGNMENT_LEFT, -1, int(10 * s), Color(1, 1, 1, 0.8))


func _threat_arrows(sr: Rect2, s: float) -> void:
	## Incoming attacks that are off screen get an edge arrow pointing at them.
	if cam == null:
		return
	var p := world.get_actor(player_id)
	for b in world.bodies:
		if not b.alive or b.attack_id == 0 or b.attack_owner == player_id:
			continue
		var to := p.chest() - b.pos
		if b.form != Sim.Form.WAVE and b.vel.dot(to) <= 0.0:
			continue
		if to.length() > 16.0:
			continue
		var sp: Variant = cam.world_to_screen(b.pos)
		if sp != null and sr.has_point(sp):
			continue
		# Direction on screen from camera yaw.
		var f := cam.forward_flat()
		var r := f.cross(Vector3.UP)
		var d := -to
		var v := Vector2(d.dot(r), -d.dot(f)).normalized()
		var c := sr.get_center()
		var edge := c + v * minf(sr.size.x, sr.size.y) * 0.42
		var col := Color(1.0, 0.55, 0.3, 0.9) if b.is_hot() else Color(1, 1, 1, 0.85)
		var tip := edge + v * 14 * s
		var side := Vector2(-v.y, v.x) * 9 * s
		draw_colored_polygon(PackedVector2Array([tip, edge + side, edge - side]), col)
