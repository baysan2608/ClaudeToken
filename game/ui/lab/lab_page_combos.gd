class_name LabPageCombos
extends LabPage
## Combo trainer page (docs/MOVESET.md section 9.3): pick one of the 28 showcase combos, see its input steps for
## the active device with a timing bar per step, optionally in slow motion, and watch live success detection.

class StepBar extends Control:
	## One step's timing bar: the step's window, how much of it has passed, and a tick when it was hit.
	var state := 0            # 0 waiting, 1 current, 2 done, 3 locked (future)
	var used := 0.0           # window fraction used (current step)
	var hit_at := 0.0         # seconds after the previous step when this one started (done)
	var within := 4.0
	var accent := Color.WHITE

	func _draw() -> void:
		var r := Rect2(Vector2.ZERO, size)
		draw_rect(r, Color(1, 1, 1, 0.08))
		match state:
			1:
				draw_rect(Rect2(r.position, Vector2(r.size.x * used, r.size.y)), Color(accent.r, accent.g, accent.b, 0.55 if used < 0.8 else 0.9).lerp(Color(1, 0.4, 0.3, 0.9), clampf((used - 0.7) / 0.3, 0.0, 1.0)))
			2:
				draw_rect(Rect2(r.position, Vector2(r.size.x * clampf(hit_at / maxf(within, 0.01), 0.0, 1.0), r.size.y)), Color(0.4, 0.85, 0.5, 0.55))
				draw_rect(Rect2(Vector2(r.size.x * clampf(hit_at / maxf(within, 0.01), 0.0, 1.0) - 2.0, 0.0), Vector2(3.0, r.size.y)), Color(0.55, 1.0, 0.6, 1.0))
		draw_rect(r, Color(1, 1, 1, 0.22), false, 1.0)


var selected := "melt_return"
var slow_motion := true

var _combo_buttons := {}
var _detail: VBoxContainer = null
var _status: Label = null
var _bars: Array[StepBar] = []
var _step_labels: Array[Label] = []
var _start_btn: Button = null


func _build() -> void:
	var flow := HFlowContainer.new()
	flow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	flow.add_theme_constant_override("h_separation", int(round(0.8 * ppm)))
	flow.add_theme_constant_override("v_separation", int(round(0.8 * ppm)))
	box.add_child(flow)
	var group := ButtonGroup.new()
	for c in LabCombos.all():
		var b := Button.new()
		b.text = "%d  %s" % [int(c.n), String(c.name)]
		b.toggle_mode = true
		b.button_group = group
		b.button_pressed = c.id == selected
		b.custom_minimum_size = Vector2(0, row_h())
		b.add_theme_font_size_override("font_size", int(round(2.4 * ppm)))
		var cid: String = c.id
		b.pressed.connect(func() -> void: select(cid))
		flow.add_child(b)
		_combo_buttons[cid] = b
	_detail = VBoxContainer.new()
	_detail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail.add_theme_constant_override("separation", int(round(0.9 * ppm)))
	box.add_child(_detail)
	select(selected)


func _device() -> String:
	return String(panel.get("device")) if panel != null else "keyboard"


func select(id: String) -> void:
	selected = id
	if _combo_buttons.has(id):
		(_combo_buttons[id] as Button).set_pressed_no_signal(true)
	_rebuild_detail()


func _rebuild_detail() -> void:
	for c in _detail.get_children():
		_detail.remove_child(c)
		c.queue_free()
	_bars.clear()
	_step_labels.clear()
	var combo := LabCombos.find(selected)
	if combo.is_empty():
		return
	var title := Label.new()
	title.text = "%d. %s" % [int(combo.n), String(combo.name)]
	title.add_theme_font_size_override("font_size", int(round(3.3 * ppm)))
	_detail.add_child(title)
	var d := Label.new()
	d.text = String(combo.desc)
	d.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	d.add_theme_font_size_override("font_size", int(round(2.4 * ppm)))
	d.add_theme_color_override("font_color", UiStyle.INK_DIM)
	_detail.add_child(d)
	var dev := _device()
	var i := 0
	for s in combo.steps:
		var row := VBoxContainer.new()
		row.add_theme_constant_override("separation", int(round(0.3 * ppm)))
		var l := Label.new()
		var hint := String(s.get("hint", ""))
		l.text = "%d.  %s    [%s]%s" % [i + 1, LabCombos.step_text(s), LabCombos.step_input(s, dev), ("  -  " + hint) if hint != "" else ""]
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		l.add_theme_font_size_override("font_size", int(round(2.4 * ppm)))
		row.add_child(l)
		var bar := StepBar.new()
		bar.custom_minimum_size = Vector2(0, 2.2 * ppm)
		bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		bar.within = float(s.get("within", 4.0))
		bar.accent = UiStyle.element_color(int(s.el))
		bar.state = 3
		row.add_child(bar)
		_detail.add_child(row)
		_bars.append(bar)
		_step_labels.append(l)
		i += 1
	var res: Dictionary = combo.get("result", {})
	if not res.is_empty():
		var rl := Label.new()
		rl.text = "Result to see: %s" % String(res.get("text", ""))
		rl.add_theme_font_size_override("font_size", int(round(2.4 * ppm)))
		rl.add_theme_color_override("font_color", UiStyle.ACCENT)
		rl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		_detail.add_child(rl)
	_status = Label.new()
	_status.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail.add_child(_status)
	var slow := CheckButton.new()
	slow.text = "Slow motion while training (x0.5)"
	slow.button_pressed = slow_motion
	slow.custom_minimum_size.y = row_h()
	slow.add_theme_font_size_override("font_size", int(round(2.5 * ppm)))
	slow.toggled.connect(func(on: bool) -> void: slow_motion = on)
	_detail.add_child(slow)
	var btns := HBoxContainer.new()
	btns.add_theme_constant_override("separation", int(round(0.8 * ppm)))
	_detail.add_child(btns)
	_start_btn = button("Start trainer", func() -> void: start_trainer(), btns, 34.0)
	button("Stop", func() -> void: stop_trainer(), btns, 18.0)
	button("Demo the steps", func() -> void: emit_action("combo_demo", {"id": selected}), btns, 32.0)


func start_trainer() -> void:
	if slow_motion:
		session.set_time_scale(0.5)
	emit_action("combo_start", {"id": selected, "setup": true})
	if panel != null and panel.has_method("close_panel"):
		panel.call("close_panel")


func stop_trainer() -> void:
	if session.time_scale < 1.0 and slow_motion:
		session.set_time_scale(1.0)
	emit_action("combo_stop")


func refresh() -> void:
	_rebuild_detail()


func live(_dt: float) -> void:
	var tr: ComboTracker = panel.get("tracker") if panel != null else null
	if tr == null or tr.combo.is_empty() or String(tr.combo.get("id", "")) != selected:
		if _status != null:
			_status.text = "Press Start trainer. Spawns the situation, then play the steps in order."
		for b in _bars:
			b.state = 3
			b.queue_redraw()
		return
	for i in _bars.size():
		var b := _bars[i]
		if i < tr.step:
			b.state = 2
			b.hit_at = (tr.step_times[i] - (tr.step_times[i - 1] if i > 0 else 0.0)) if i < tr.step_times.size() else 0.0
		elif i == tr.step and tr.state == ComboTracker.State.RUNNING:
			b.state = 1
			b.used = tr.window_used() if i > 0 else 0.0
		else:
			b.state = 3
		b.queue_redraw()
	if _status != null:
		_status.text = "%s    (attempts %d, successes %d)" % [tr.status_text(), tr.attempts, tr.successes]
		_status.add_theme_color_override("font_color", Color(0.55, 1.0, 0.6) if tr.state == ComboTracker.State.SUCCESS else Color.WHITE)
