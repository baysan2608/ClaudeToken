class_name Hud
extends Control
## Minimal in-play HUD: thin vitals (fade when safe), heat reserve when non-zero,
## rival vitals while locked on, objective line, short toasts, off-screen threat
## arrows and an optional debug/state overlay (Settings > show debug).

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


func _bar(pos: Vector2, w: float, frac: float, col: Color, a: float) -> void:
	draw_rect(Rect2(pos, Vector2(w, 4)), Color(0, 0, 0, 0.35 * a))
	draw_rect(Rect2(pos, Vector2(w * clampf(frac, 0.0, 1.0), 4)), Color(col, a))


func _draw() -> void:
	if world == null:
		return
	var sr := _safe_rect()
	var s := clampf(sr.size.y / 720.0, 0.8, 2.0)
	var p := world.get_actor(player_id)
	if p == null:
		return
	var x := sr.position.x + 8 * s
	var y := sr.position.y + 8 * s
	var bw := 190.0 * s
	_bar(Vector2(x, y), bw, p.health / Sim.HEALTH_MAX, Color(0.92, 0.9, 0.86), _alpha)
	_bar(Vector2(x, y + 9 * s), bw * 0.8, p.balance / Sim.BALANCE_MAX, Color(0.75, 0.82, 0.92), _alpha * 0.9)
	_bar(Vector2(x, y + 18 * s), bw * 0.8, p.focus / Sim.FOCUS_MAX, Color(0.96, 0.82, 0.45), _alpha * 0.9)
	if p.heat_reserve > 1.0:
		_bar(Vector2(x, y + 27 * s), bw * 0.6, p.heat_reserve / Sim.RESERVE_MAX, Color(1.0, 0.45, 0.2), 1.0)
		draw_string(_font, Vector2(x + bw * 0.62, y + 33 * s), "HEAT %d" % int(p.heat_reserve), HORIZONTAL_ALIGNMENT_LEFT, -1, int(11 * s), Color(1, 0.6, 0.4, 0.9))
	if p.element == Sim.Element.WATER:
		draw_string(_font, Vector2(x, y + 44 * s), "water %.0f kg" % p.water_carried, HORIZONTAL_ALIGNMENT_LEFT, -1, int(11 * s), Color(0.6, 0.85, 1.0, 0.8 * _alpha))
	# Rival vitals (locked target), top centre, compact.
	var t := world.get_actor(p.lock_target)
	if t != null:
		var cx := sr.get_center().x
		_bar(Vector2(cx - 80 * s, y), 160 * s, t.health / Sim.HEALTH_MAX, Color(0.95, 0.55, 0.45), 0.85)
		_bar(Vector2(cx - 64 * s, y + 9 * s), 128 * s, t.balance / Sim.BALANCE_MAX, Color(0.75, 0.82, 0.92), 0.7)
		draw_string(_font, Vector2(cx - 80 * s, y + 28 * s), t.name, HORIZONTAL_ALIGNMENT_CENTER, 160 * s, int(12 * s), Color(1, 1, 1, 0.7))
	# Objective + challenge.
	if objective != "":
		draw_string(_font, Vector2(sr.position.x, sr.end.y - 10 * s), objective, HORIZONTAL_ALIGNMENT_CENTER, sr.size.x, int(13 * s), Color(1, 1, 1, 0.55))
	if challenge_text != "":
		draw_string(_font, Vector2(sr.position.x, sr.end.y - 28 * s), challenge_text, HORIZONTAL_ALIGNMENT_CENTER, sr.size.x, int(13 * s), Color(1.0, 0.86, 0.55, 0.85))
	if _toast_t > 0.0:
		var a := clampf(_toast_t / 0.4, 0.0, 1.0)
		draw_string(_font, Vector2(sr.position.x, sr.get_center().y - 90 * s), _toast, HORIZONTAL_ALIGNMENT_CENTER, sr.size.x, int(18 * s), Color(1, 1, 1, 0.9 * a))
	if _flash_t > 0.0:
		var col := Color(1.0, 0.95, 0.8, 0.10) if _flash == "perfect" else Color(0.8, 0.85, 1.0, 0.08)
		if GameSettings.current().flashes > 0.0:
			col.a *= GameSettings.current().flashes
			draw_rect(get_viewport_rect(), col)
	_threat_arrows(sr, s)
	if show_debug:
		_timing_bars(s)
		var dy := y + 60 * s
		for line in debug_lines:
			draw_string(_font, Vector2(x, dy), line, HORIZONTAL_ALIGNMENT_LEFT, -1, int(11 * s), Color(0.85, 1.0, 0.85, 0.9))
			dy += 14 * s
		draw_string(_font, Vector2(sr.end.x - 260 * s, sr.end.y - 50 * s), perf_text, HORIZONTAL_ALIGNMENT_LEFT, -1, int(11 * s), Color(0.85, 1.0, 0.85, 0.9))


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
