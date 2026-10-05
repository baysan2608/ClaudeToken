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
## Lab: draw bodies / zones / power numbers over the arena.
var lab_overlay := false
## Lab: "FROZEN" / "x0.25" tag under the rival's vitals ("" = hidden).
var lab_status := ""

var _toast := ""
var _status_t0 := {}   # "actor/status" -> longest remaining time seen (status icon fill)
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
	# Sub-element label, then the resource bars (heat reserve, waterskin, metal satchel, static charge).
	var ecol := UiStyle.element_color(p.element)
	var sub_base := y + 3 * row + fs_stat + grow_stat * 0.2
	var sub_text := "%s / %s" % [Sim.ELEMENT_NAMES[p.element], Sim.SUB_NAMES[p.element][p.sub()]]
	draw_string_outline(_font, Vector2(x, sub_base), sub_text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, maxi(2, fs_stat / 6), Color(0, 0, 0, 0.45 * _alpha))
	draw_string(_font, Vector2(x, sub_base), sub_text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, Color(ecol.r, ecol.g, ecol.b, 0.95 * _alpha))
	var ry := sub_base + 4 * s + grow_bar * 0.5
	var res_step := maxf(9 * s + grow_bar + grow_stat * 0.2, fs_stat + 2.0 * s)
	var heat_base := ry + 6 * s + grow_bar * 0.5 + grow_stat * 0.35
	if p.heat_reserve > 1.0:
		_bar(Vector2(x, ry), bw * 0.6, p.heat_reserve / Sim.RESERVE_MAX, Color(1.0, 0.45, 0.2), 1.0)
		draw_string(_font, Vector2(x + bw * 0.62, heat_base), "HEAT %d" % int(p.heat_reserve), HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, Color(1, 0.6, 0.4, 0.9))
		ry += res_step
		heat_base += res_step
	if p.element == Sim.Element.WATER or p.water_carried < 5.95:
		_bar(Vector2(x, ry), bw * 0.6, p.water_carried / 6.0, Color(0.45, 0.78, 1.0), 0.9 * _alpha)
		draw_string(_font, Vector2(x + bw * 0.62, heat_base), "water %.1f kg" % p.water_carried, HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, Color(0.6, 0.85, 1.0, 0.85 * _alpha))
		ry += res_step
		heat_base += res_step
	if (p.element == Sim.Element.EARTH and p.sub() == 1) or p.metal_carried < 11.95:
		_bar(Vector2(x, ry), bw * 0.6, p.metal_carried / 12.0, Color(0.72, 0.78, 0.86), 0.9 * _alpha)
		draw_string(_font, Vector2(x + bw * 0.62, heat_base), "metal %.1f kg" % p.metal_carried, HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, Color(0.8, 0.85, 0.92, 0.85 * _alpha))
		ry += res_step
		heat_base += res_step
	if p.static_charge > 0.5 or (p.element == Sim.Element.FIRE and p.sub() == 2):
		_bar(Vector2(x, ry), bw * 0.6, p.static_charge / 60.0, Color(0.7, 0.55, 1.0), 0.9 * _alpha)
		draw_string(_font, Vector2(x + bw * 0.62, heat_base), "static %d" % int(p.static_charge), HORIZONTAL_ALIGNMENT_LEFT, -1, fs_stat, Color(0.8, 0.7, 1.0, 0.85 * _alpha))
		ry += res_step
		heat_base += res_step
	var water_base := heat_base
	water_base = maxf(water_base, ry)
	var status_h := _draw_statuses(p, Vector2(x, ry + 1.5 * s), s, false)
	water_base += status_h
	# Rival vitals (locked target), top centre, compact.
	var t := world.get_actor(p.lock_target)
	if t != null:
		var cx := sr.get_center().x
		_bar(Vector2(cx - 80 * s, y), 160 * s, t.health / Sim.HEALTH_MAX, Color(0.95, 0.55, 0.45), 0.85)
		_bar(Vector2(cx - 64 * s, y + row), 128 * s, t.balance / Sim.BALANCE_MAX, Color(0.75, 0.82, 0.92), 0.7)
		var name_base := y + 28 * s + grow_bar + (fs_name - int(12 * s)) * 0.75
		draw_string(_font, Vector2(cx - 80 * s, name_base), t.name, HORIZONTAL_ALIGNMENT_CENTER, 160 * s, fs_name, Color(1, 1, 1, 0.7))
		if not t.status.is_empty():
			_draw_statuses(t, Vector2(cx - 80 * s, name_base + 5 * s), s, true)
	if lab_status != "":
		var lx := sr.get_center().x
		draw_string(_font, Vector2(lx - 90 * s, y + 52 * s + grow_bar + grow_stat), lab_status, HORIZONTAL_ALIGNMENT_CENTER, 180 * s, fs_stat, Color(1.0, 0.85, 0.45, 0.95))
	_draw_charge_bar(p, sr, s)
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
	if lab_overlay:
		_lab_overlay(s)
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


# ================================================================ status icons, charge bar, Lab overlay

## Short code, colour and display name of every status (docs/MOVESET.md section 15.8).
const STATUS_STYLE := {
	"wet": ["WT", Color(0.45, 0.75, 1.0)], "burning": ["BN", Color(1.0, 0.5, 0.25)], "chilled": ["CH", Color(0.7, 0.9, 1.0)],
	"frozen": ["FZ", Color(0.6, 0.85, 1.0)], "rooted": ["RT", Color(0.5, 0.8, 0.4)], "slowed": ["SL", Color(0.8, 0.8, 0.6)],
	"muddy": ["MD", Color(0.65, 0.5, 0.35)], "slick": ["SK", Color(0.6, 0.9, 0.95)], "blinded": ["BL", Color(0.85, 0.85, 0.6)],
	"concealed": ["CN", Color(0.7, 0.75, 0.85)], "deafened": ["DF", Color(0.8, 0.7, 0.9)], "shocked": ["SH", Color(0.95, 0.9, 0.4)],
	"anchored": ["AN", Color(0.8, 0.7, 0.5)], "armored": ["AR", Color(0.75, 0.78, 0.85)], "levitating": ["LV", Color(0.7, 0.95, 0.9)],
	"charged": ["CG", Color(1.0, 0.9, 0.5)],
}


## A row of status icons (rounded square, 2-letter code, remaining time as a fill). Returns the height used.
func _draw_statuses(a: ActorState, origin: Vector2, s: float, centred: bool) -> float:
	if a.status.is_empty():
		return 0.0
	var sz := maxf(16.0 * s, 3.8 * _ppm)
	var gap := 3.0 * s
	var names: Array = a.status.keys()
	names.sort()
	var total := names.size() * (sz + gap) - gap
	var x := origin.x
	if centred:
		x = origin.x + (160.0 * s - total) * 0.5
	var fs := maxi(8, int(sz * 0.42))
	for nm in names:
		var st: Dictionary = a.status[nm]
		var sty: Array = STATUS_STYLE.get(nm, [String(nm).substr(0, 2).to_upper(), Color(0.8, 0.8, 0.8)])
		var col: Color = sty[1]
		var r := Rect2(Vector2(x, origin.y), Vector2(sz, sz))
		draw_rect(r, Color(0.04, 0.05, 0.07, 0.55 * _alpha))
		var t := float(st.get("t", -1.0))
		if t >= 0.0:
			var key := "%d/%s" % [a.id, nm]
			_status_t0[key] = maxf(float(_status_t0.get(key, 0.0)), t)
			var f := clampf(t / maxf(float(_status_t0[key]), 0.1), 0.0, 1.0)
			draw_rect(Rect2(Vector2(x, origin.y + sz * (1.0 - f)), Vector2(sz, sz * f)), Color(col.r, col.g, col.b, 0.25 * _alpha))
		draw_rect(r, Color(col.r, col.g, col.b, 0.9 * _alpha), false, maxf(1.5, sz * 0.07))
		draw_string(_font, Vector2(x, origin.y + sz * 0.5 + fs * 0.36), sty[0], HORIZONTAL_ALIGNMENT_CENTER, sz, fs, Color(1, 1, 1, 0.95 * _alpha))
		x += sz + gap
	return sz + 3.0 * s


## The sim's charge tiers (Charge.progress) as three segments with the move name: desktop / pad players
## have no ring on a button, touch players get both.
func _draw_charge_bar(p: ActorState, sr: Rect2, s: float) -> void:
	var inst := p.action
	if inst == null or not (inst.phase == ActionInst.P.CHARGE or inst.phase == ActionInst.P.CHANNEL):
		return
	var def := Charge.pdef(inst)
	var mx := Charge.max_tier(def)
	if mx <= 0:
		return
	var pr := Charge.progress(inst)
	var tier := int(pr.x)
	var w := maxf(150.0 * s, 28.0 * _ppm)
	var h := maxf(6.0 * s, 1.2 * _ppm)
	var x := sr.get_center().x - w * 0.5
	var y := sr.end.y - 62.0 * s
	var col := UiStyle.element_color(inst.element)
	var gap := 4.0 * s
	var seg := (w - gap * (mx - 1)) / mx
	for k in mx:
		var r := Rect2(Vector2(x + k * (seg + gap), y), Vector2(seg, h))
		draw_rect(r, Color(0, 0, 0, 0.45))
		var lit := 1.0 if k < tier else (pr.y if k == tier else 0.0)
		if lit > 0.0:
			draw_rect(Rect2(r.position, Vector2(seg * lit, h)), Color(col.r, col.g, col.b, 0.95))
		draw_rect(r, Color(1, 1, 1, 0.35), false, 1.0)
	var names := String(def.get("name", inst.id)).split(" / ")
	var nm: String = names[clampi(tier, 0, names.size() - 1)]
	var fs := _fs(12, TEXT_MM)
	draw_string_outline(_font, Vector2(x, y - 4 * s), "%s  T%d" % [nm, tier], HORIZONTAL_ALIGNMENT_CENTER, w, fs, maxi(2, fs / 6), Color(0, 0, 0, 0.6))
	draw_string(_font, Vector2(x, y - 4 * s), "%s  T%d" % [nm, tier], HORIZONTAL_ALIGNMENT_CENTER, w, fs, Color(1, 1, 1, 0.95))


## Lab overlay: every live body (id, tag, mass, temperature, power), zone radii, actor action / status.
func _lab_overlay(s: float) -> void:
	if cam == null:
		return
	var fs := maxi(10, int(10 * s))
	for b in world.bodies:
		if not b.alive or b.form == Sim.Form.POOL:
			continue
		var sp: Variant = cam.world_to_screen(b.pos + Vector3(0, 0.3, 0))
		if sp == null:
			continue
		var col := Color(0.6, 1.0, 0.7, 0.85)
		if b.form == Sim.Form.ZONE or b.zone_radius > 0.0:
			col = Color(0.7, 0.8, 1.0, 0.85)
			var edge: Variant = cam.world_to_screen(b.pos + cam.forward_flat().cross(Vector3.UP) * maxf(b.zone_radius, 0.2))
			if edge != null:
				var rad := (edge as Vector2).distance_to(sp as Vector2)
				draw_arc(sp as Vector2, rad, 0.0, TAU, 40, Color(col, 0.5), maxf(1.5, s), true)
		elif b.is_hot():
			col = Color(1.0, 0.65, 0.4, 0.9)
		var txt := "#%d %s%s %.0fkg %.0fC" % [b.id, Sim.MAT_NAMES[b.mat], ("[%s]" % b.tag) if b.tag != &"" else "", b.mass, b.temp]
		if b.power > 0.0:
			txt += " P%.0f" % b.power
		if b.tier > 0:
			txt += " T%d" % b.tier
		draw_circle(sp as Vector2, 3.0 * s, col)
		draw_string_outline(_font, (sp as Vector2) + Vector2(6 * s, -4 * s), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, maxi(2, fs / 5), Color(0, 0, 0, 0.7))
		draw_string(_font, (sp as Vector2) + Vector2(6 * s, -4 * s), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, col)
	for a in world.actors:
		var ap: Variant = cam.world_to_screen(a.pos + Vector3(0, 2.4, 0))
		if ap == null:
			continue
		var act := "-" if a.action == null else "%s/%s" % [a.action.id, a.action.phase_name()]
		var st := " ".join(PackedStringArray(a.status.keys()))
		var line := "%s %s%s" % [a.name, act, (" [" + st + "]") if st != "" else ""]
		draw_string_outline(_font, (ap as Vector2) + Vector2(-60 * s, 0), line, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, maxi(2, fs / 5), Color(0, 0, 0, 0.7))
		draw_string(_font, (ap as Vector2) + Vector2(-60 * s, 0), line, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color(1, 1, 0.8, 0.9))
