class_name SettingsPanel
extends Control
## Pause / settings screen. Opening it pauses the tree (the panel itself runs
## with process_mode ALWAYS); closing resumes. Left column: Resume, Reset
## scenario, Practice, Quit to lab. Right column: settings bound live to
## GameSettings (saved shortly after the last change and on close), plus
## "Reset progress" behind a confirm.
##
## Signals the game connects to:
##   resume_requested, reset_requested, practice_requested,
##   practice_selected(id), reset_progress_requested, quit_to_lab_requested

signal resume_requested
signal reset_requested
## Emitted when the player opens the practice list; answer with set_practice_items().
signal practice_requested
signal practice_selected(id)
## A row of option buttons under a practice item changed (e.g. Free Spar difficulty / kit).
signal practice_option_changed(key: String, value: String)
signal reset_progress_requested
signal quit_to_lab_requested
## "Dev" in the pause menu (touch has no backquote key): open the Lab dev panel.
signal dev_requested
signal opened
signal closed

enum Page { MAIN, PRACTICE }

const SAVE_DEBOUNCE_SEC := 0.45

var settings: GameSettings = null

var _built := false
var _ppm := 8.0
## 0.8 (phone) .. 1.0 (tablet): row/button heights relax on short screens.
var _dens := 1.0
var _open := false
var _open_frame := -1
var _page: int = Page.MAIN
var _practice_items: Array[Dictionary] = []

var _dim: ColorRect
var _card: PanelContainer
var _title: Label
var _actions: VBoxContainer
var _settings_scroll: ScrollContainer
var _settings_box: VBoxContainer
var _practice_scroll: ScrollContainer
var _practice_box: VBoxContainer
var _practice_back: Button
var _resume_btn: Button
var _dev_btn: Button
var _confirm_dim: ColorRect
var _confirm_card: PanelContainer
var _confirm_cancel: Button
var _save_timer: Timer
## prop name -> {ctl, kind, label?, fmt?}
var _bindings: Dictionary = {}
var _preset_buttons: Array[Button] = []
var _updating := false


func _init() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	mouse_filter = Control.MOUSE_FILTER_STOP
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	visible = false


func _ready() -> void:
	if settings == null:
		settings = GameSettings.current()
	_build()
	get_viewport().size_changed.connect(_relayout)
	_relayout()


# --- public API ---------------------------------------------------------------------------------

func is_open() -> bool:
	return _open


func open_panel() -> void:
	if not _built:
		_build()
	if _open:
		return
	_open = true
	_open_frame = Engine.get_process_frames()
	refresh_from_settings()
	_show_page(Page.MAIN)
	_confirm_dim.visible = false
	visible = true
	_relayout()
	get_tree().paused = true
	_resume_btn.grab_focus()
	opened.emit()


func close_panel() -> void:
	if not _open:
		return
	_open = false
	visible = false
	_confirm_dim.visible = false
	_flush_save()
	if is_inside_tree():
		get_tree().paused = false
	closed.emit()


## items: Array of {id, title, subtitle, locked: bool}. Safe to call any time.
func set_practice_items(items: Array[Dictionary]) -> void:
	_practice_items = items.duplicate()
	if _built:
		_rebuild_practice_list()


## Re-read every bound widget from `settings` (after a reset or external change).
func refresh_from_settings() -> void:
	if not _built:
		return
	_updating = true
	for prop: String in _bindings:
		var b: Dictionary = _bindings[prop]
		var v: Variant = settings.get(prop)
		match b["kind"]:
			"slider":
				(b["ctl"] as HSlider).value = float(v)
				_update_value_label(prop, float(v))
			"toggle":
				(b["ctl"] as CheckButton).button_pressed = bool(v)
	for i in _preset_buttons.size():
		_preset_buttons[i].button_pressed = GameSettings.PRESETS[i] == settings.layout_preset
	_updating = false


# --- construction -----------------------------------------------------------------------------------

func _build() -> void:
	if _built:
		return
	_built = true
	if settings == null:
		settings = GameSettings.current()
	_ppm = UiScale.px_per_mm(get_viewport())
	_dens = _density()
	theme = UiStyle.build_theme(_ppm)

	_dim = ColorRect.new()
	_dim.color = Color(0.02, 0.025, 0.035, 0.66)
	_dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_dim.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(_dim)

	_card = PanelContainer.new()
	add_child(_card)
	var root := VBoxContainer.new()
	root.add_theme_constant_override("separation", int(round(1.6 * _ppm)))
	_card.add_child(root)

	var head := HBoxContainer.new()
	root.add_child(head)
	_title = Label.new()
	_title.text = "Paused"
	_title.add_theme_font_size_override("font_size", int(round(4.4 * _ppm)))
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_title)
	_practice_back = _button("Back")
	_practice_back.visible = false
	_practice_back.pressed.connect(func() -> void: _show_page(Page.MAIN))
	head.add_child(_practice_back)

	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", int(round(3.0 * _ppm)))
	root.add_child(body)

	_actions = VBoxContainer.new()
	_actions.custom_minimum_size.x = 44.0 * _ppm
	body.add_child(_actions)
	_resume_btn = _button("Resume")
	_resume_btn.pressed.connect(_on_resume)
	_actions.add_child(_resume_btn)
	var reset_btn := _button("Reset scenario")
	reset_btn.pressed.connect(_on_reset_scenario)
	_actions.add_child(reset_btn)
	var practice_btn := _button("Practice")
	practice_btn.pressed.connect(_on_practice)
	_actions.add_child(practice_btn)
	var quit_btn := _button("Quit to lab")
	quit_btn.pressed.connect(_on_quit)
	_actions.add_child(quit_btn)
	_dev_btn = _button("Dev")
	_dev_btn.pressed.connect(_on_dev)
	_actions.add_child(_dev_btn)

	var right := Control.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.mouse_filter = Control.MOUSE_FILTER_PASS
	body.add_child(right)

	_settings_scroll = ScrollContainer.new()
	_settings_scroll.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_settings_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	right.add_child(_settings_scroll)
	_settings_box = VBoxContainer.new()
	_settings_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_settings_box.add_theme_constant_override("separation", int(round(0.8 * _ppm)))
	_settings_scroll.add_child(_settings_box)
	_build_settings_rows()

	_practice_scroll = ScrollContainer.new()
	_practice_scroll.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_practice_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_practice_scroll.visible = false
	right.add_child(_practice_scroll)
	_practice_box = VBoxContainer.new()
	_practice_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_practice_scroll.add_child(_practice_box)

	_build_confirm()

	_save_timer = Timer.new()
	_save_timer.one_shot = true
	_save_timer.wait_time = SAVE_DEBOUNCE_SEC
	_save_timer.process_mode = Node.PROCESS_MODE_ALWAYS
	_save_timer.timeout.connect(_flush_save)
	add_child(_save_timer)

	_rebuild_practice_list()
	refresh_from_settings()


func _button(text: String) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(0, 9.6 * _ppm * _dens)
	b.focus_mode = Control.FOCUS_ALL
	return b


func _section(title: String) -> void:
	var l := Label.new()
	l.text = title.to_upper()
	l.add_theme_font_size_override("font_size", int(round(2.3 * _ppm)))
	l.add_theme_color_override("font_color", UiStyle.ACCENT)
	l.mouse_filter = Control.MOUSE_FILTER_PASS
	var m := MarginContainer.new()
	m.add_theme_constant_override("margin_top", int(round(1.6 * _ppm)))
	m.mouse_filter = Control.MOUSE_FILTER_PASS
	m.add_child(l)
	_settings_box.add_child(m)


func _row_height() -> float:
	return 8.4 * _ppm * _dens


func _add_slider(prop: String, label: String, lo: float, hi: float, step: float, fmt: String) -> void:
	var row := HBoxContainer.new()
	row.custom_minimum_size.y = _row_height()
	row.mouse_filter = Control.MOUSE_FILTER_PASS
	var l := Label.new()
	l.text = label
	l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	l.size_flags_stretch_ratio = 1.1
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(l)
	var s := HSlider.new()
	s.min_value = lo
	s.max_value = hi
	s.step = step
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	s.custom_minimum_size = Vector2(26.0 * _ppm, 6.0 * _ppm)
	s.focus_mode = Control.FOCUS_ALL
	row.add_child(s)
	var v := Label.new()
	v.custom_minimum_size.x = 11.0 * _ppm
	v.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	v.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	v.add_theme_color_override("font_color", UiStyle.INK_DIM)
	row.add_child(v)
	_settings_box.add_child(row)
	_bindings[prop] = {"ctl": s, "kind": "slider", "label": v, "fmt": fmt}
	s.value_changed.connect(func(val: float) -> void: _on_slider(prop, val))


func _add_toggle(prop: String, label: String) -> void:
	var c := CheckButton.new()
	c.text = label
	c.custom_minimum_size.y = _row_height()
	c.focus_mode = Control.FOCUS_ALL
	_settings_box.add_child(c)
	_bindings[prop] = {"ctl": c, "kind": "toggle"}
	c.toggled.connect(func(on: bool) -> void: _on_toggle(prop, on))


func _build_settings_rows() -> void:
	_section("Controls")
	_add_slider("control_scale", "Control size", GameSettings.SCALE_MIN, GameSettings.SCALE_MAX, 0.05, "x")
	_add_slider("control_opacity", "Control opacity", GameSettings.OPACITY_MIN, GameSettings.OPACITY_MAX, 0.05, "%")
	# Layout preset (segmented).
	var row := HBoxContainer.new()
	row.custom_minimum_size.y = _row_height()
	row.mouse_filter = Control.MOUSE_FILTER_PASS
	var l := Label.new()
	l.text = "Layout"
	l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(l)
	var group := ButtonGroup.new()
	for i in GameSettings.PRESETS.size():
		var b := Button.new()
		b.text = String(GameSettings.PRESETS[i]).capitalize()
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = Control.FOCUS_ALL
		b.custom_minimum_size = Vector2(15.0 * _ppm, 0)
		var idx := i
		b.pressed.connect(func() -> void: _on_preset(idx))
		row.add_child(b)
		_preset_buttons.append(b)
	_settings_box.add_child(row)
	_add_toggle("left_handed", "Left-handed layout")
	_add_toggle("strong_labels", "Strong button labels")
	_add_slider("camera_sensitivity", "Camera sensitivity", GameSettings.SENS_MIN, GameSettings.SENS_MAX, 0.05, "x")
	_add_toggle("invert_y", "Invert camera Y")

	_section("Comfort and feedback")
	_add_slider("screen_shake", "Screen shake", 0.0, 1.0, 0.05, "%")
	_add_slider("flashes", "Flashes", 0.0, 1.0, 0.05, "%")
	_add_toggle("haptics", "Haptics")
	_add_toggle("reduced_motion", "Reduced motion")
	_add_toggle("slowmo_assist", "Slow-motion assist")

	_section("Developer")
	_add_toggle("show_debug", "Show debug overlay")

	_section("Data")
	var defaults := _button("Reset settings to defaults")
	defaults.pressed.connect(_on_reset_defaults)
	_settings_box.add_child(defaults)
	var prog := _button("Reset progress...")
	prog.add_theme_color_override("font_color", UiStyle.DANGER)
	prog.add_theme_color_override("font_hover_color", UiStyle.DANGER)
	prog.add_theme_color_override("font_focus_color", UiStyle.DANGER)
	prog.pressed.connect(_on_reset_progress_pressed)
	_settings_box.add_child(prog)
	var pad := Control.new()
	pad.custom_minimum_size.y = 2.0 * _ppm
	pad.mouse_filter = Control.MOUSE_FILTER_PASS
	_settings_box.add_child(pad)


func _build_confirm() -> void:
	_confirm_dim = ColorRect.new()
	_confirm_dim.color = Color(0, 0, 0, 0.55)
	_confirm_dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_confirm_dim.mouse_filter = Control.MOUSE_FILTER_STOP
	_confirm_dim.visible = false
	add_child(_confirm_dim)
	_confirm_card = PanelContainer.new()
	_confirm_card.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_confirm_dim.add_child(_confirm_card)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", int(round(1.8 * _ppm)))
	_confirm_card.add_child(v)
	var t := Label.new()
	t.text = "Reset all progress?"
	t.add_theme_font_size_override("font_size", int(round(3.8 * _ppm)))
	v.add_child(t)
	var d := Label.new()
	d.text = "This erases unlocked elements and practice records.\nIt cannot be undone."
	d.add_theme_color_override("font_color", UiStyle.INK_DIM)
	v.add_child(d)
	var h := HBoxContainer.new()
	v.add_child(h)
	_confirm_cancel = _button("Cancel")
	_confirm_cancel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_confirm_cancel.pressed.connect(func() -> void: _confirm_dim.visible = false; _resume_btn.grab_focus())
	h.add_child(_confirm_cancel)
	var ok := _button("Erase progress")
	ok.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ok.add_theme_color_override("font_color", UiStyle.DANGER)
	ok.add_theme_color_override("font_hover_color", UiStyle.DANGER)
	ok.add_theme_color_override("font_focus_color", UiStyle.DANGER)
	ok.pressed.connect(_on_confirm_reset_progress)
	h.add_child(ok)


func _rebuild_practice_list() -> void:
	for c in _practice_box.get_children():
		c.queue_free()
	if _practice_items.is_empty():
		var l := Label.new()
		l.text = "No practice drills available yet."
		l.add_theme_color_override("font_color", UiStyle.INK_DIM)
		_practice_box.add_child(l)
		return
	for item in _practice_items:
		var locked := bool(item.get("locked", false))
		var b := Button.new()
		b.custom_minimum_size = Vector2(0, 12.5 * _ppm * _dens)
		b.disabled = locked
		b.focus_mode = Control.FOCUS_ALL
		var m := MarginContainer.new()
		m.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		m.mouse_filter = Control.MOUSE_FILTER_IGNORE
		m.add_theme_constant_override("margin_left", int(round(2.4 * _ppm)))
		m.add_theme_constant_override("margin_right", int(round(2.4 * _ppm)))
		m.add_theme_constant_override("margin_top", int(round(1.0 * _ppm)))
		m.add_theme_constant_override("margin_bottom", int(round(1.0 * _ppm)))
		b.add_child(m)
		var h := HBoxContainer.new()
		h.mouse_filter = Control.MOUSE_FILTER_IGNORE
		m.add_child(h)
		var v := VBoxContainer.new()
		v.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		v.alignment = BoxContainer.ALIGNMENT_CENTER
		v.mouse_filter = Control.MOUSE_FILTER_IGNORE
		v.add_theme_constant_override("separation", 0)
		h.add_child(v)
		var t := Label.new()
		t.text = str(item.get("title", ""))
		t.mouse_filter = Control.MOUSE_FILTER_IGNORE
		t.modulate.a = 0.45 if locked else 1.0
		v.add_child(t)
		var sub := str(item.get("subtitle", ""))
		if sub != "":
			var s := Label.new()
			s.text = sub
			s.mouse_filter = Control.MOUSE_FILTER_IGNORE
			s.add_theme_font_size_override("font_size", int(round(2.4 * _ppm)))
			s.add_theme_color_override("font_color", UiStyle.INK_DIM)
			s.modulate.a = 0.45 if locked else 1.0
			v.add_child(s)
		if locked:
			var tag := Label.new()
			tag.text = "LOCKED"
			tag.mouse_filter = Control.MOUSE_FILTER_IGNORE
			tag.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
			tag.add_theme_font_size_override("font_size", int(round(2.2 * _ppm)))
			tag.add_theme_color_override("font_color", UiStyle.INK_DIM)
			h.add_child(tag)
		var id: Variant = item.get("id", "")
		b.pressed.connect(func() -> void: _on_practice_chosen(id))
		_practice_box.add_child(b)
		for opt in item.get("options", []):
			_practice_box.add_child(_option_row(opt))


## A labelled row of exclusive buttons under a practice item: {key, label, values, labels, value}.
func _option_row(opt: Dictionary) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", int(round(1.0 * _ppm)))
	var l := Label.new()
	l.text = str(opt.get("label", ""))
	l.custom_minimum_size.x = 14.0 * _ppm
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.add_theme_color_override("font_color", UiStyle.INK_DIM)
	row.add_child(l)
	var flow := HFlowContainer.new()
	flow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	flow.add_theme_constant_override("h_separation", int(round(0.8 * _ppm)))
	flow.add_theme_constant_override("v_separation", int(round(0.8 * _ppm)))
	row.add_child(flow)
	var group := ButtonGroup.new()
	var values: Array = opt.get("values", [])
	var labels: Array = opt.get("labels", values)
	for i in values.size():
		var b := Button.new()
		b.text = str(labels[i])
		b.toggle_mode = true
		b.button_group = group
		b.button_pressed = str(values[i]) == str(opt.get("value", ""))
		b.focus_mode = Control.FOCUS_ALL
		b.custom_minimum_size = Vector2(15.0 * _ppm, 8.4 * _ppm * _dens)
		var key := str(opt.get("key", ""))
		var val := str(values[i])
		b.pressed.connect(func() -> void: practice_option_changed.emit(key, val))
		flow.add_child(b)
	return row


# --- layout -------------------------------------------------------------------------------------------

func _relayout() -> void:
	if not _built:
		return
	var vp := get_viewport()
	var new_ppm := UiScale.px_per_mm(vp)
	if absf(new_ppm - _ppm) > 0.25:
		_ppm = new_ppm
		_dens = _density()
		theme = UiStyle.build_theme(_ppm)
	var vs := vp.get_visible_rect().size
	var ins := UiScale.safe_insets(vp)
	var area := Rect2(ins.x, ins.y, vs.x - ins.x - ins.z, vs.y - ins.y - ins.w)
	var pad := 3.0 * _ppm
	var w := minf(area.size.x - pad * 2.0, 178.0 * _ppm)
	var h := minf(area.size.y - pad * 2.0, 112.0 * _ppm)
	var rect := Rect2(area.position + (area.size - Vector2(w, h)) * 0.5, Vector2(w, h))
	_card.set_anchors_preset(Control.PRESET_TOP_LEFT)
	_card.position = rect.position
	_card.size = rect.size
	_card.custom_minimum_size = rect.size
	_confirm_card.custom_minimum_size.x = minf(area.size.x - pad * 2.0, 96.0 * _ppm)


func _density() -> float:
	var vp := get_viewport()
	var h_mm := vp.get_visible_rect().size.y / maxf(_ppm, 0.1)
	return clampf(h_mm / 100.0, 0.8, 1.0)


func _show_page(p: int) -> void:
	_page = p
	var practice := p == Page.PRACTICE
	_settings_scroll.visible = not practice
	_practice_scroll.visible = practice
	_practice_back.visible = practice
	_title.text = "Practice" if practice else "Paused"
	if practice:
		_practice_scroll.scroll_vertical = 0
		for c in _practice_box.get_children():
			if c is Button and not (c as Button).disabled:
				(c as Button).grab_focus()
				break
	else:
		_settings_scroll.scroll_vertical = 0


# --- callbacks --------------------------------------------------------------------------------------------

func _input(event: InputEvent) -> void:
	if not _open:
		return
	if event.is_action_pressed("ui_cancel"):
		# Ignore the very Esc / B press that opened us.
		if Engine.get_process_frames() == _open_frame:
			get_viewport().set_input_as_handled()
			return
		if _confirm_dim.visible:
			_confirm_dim.visible = false
		elif _page == Page.PRACTICE:
			_show_page(Page.MAIN)
		else:
			_on_resume()
		get_viewport().set_input_as_handled()


func _on_resume() -> void:
	close_panel()
	resume_requested.emit()


func _on_reset_scenario() -> void:
	close_panel()
	reset_requested.emit()


func _on_practice() -> void:
	practice_requested.emit()
	_show_page(Page.PRACTICE)


func _on_practice_chosen(id: Variant) -> void:
	close_panel()
	practice_selected.emit(id)


func _on_dev() -> void:
	close_panel()
	dev_requested.emit()


func _on_quit() -> void:
	close_panel()
	quit_to_lab_requested.emit()


func _on_reset_progress_pressed() -> void:
	_confirm_dim.visible = true
	_confirm_cancel.grab_focus()


func _on_confirm_reset_progress() -> void:
	_confirm_dim.visible = false
	reset_progress_requested.emit()


func _on_reset_defaults() -> void:
	settings.reset_to_defaults()
	refresh_from_settings()
	_queue_save()


func _on_slider(prop: String, val: float) -> void:
	if _updating:
		return
	settings.set(prop, val)
	_update_value_label(prop, float(settings.get(prop)))
	settings.changed.emit()
	_queue_save()


func _on_toggle(prop: String, on: bool) -> void:
	if _updating:
		return
	settings.set(prop, on)
	settings.changed.emit()
	_queue_save()


func _on_preset(idx: int) -> void:
	if _updating:
		return
	settings.layout_preset = GameSettings.PRESETS[idx]
	settings.changed.emit()
	_queue_save()


func _update_value_label(prop: String, v: float) -> void:
	var b: Dictionary = _bindings[prop]
	var fmt: String = b["fmt"]
	var l: Label = b["label"]
	if fmt == "%":
		l.text = "%d%%" % int(round(v * 100.0))
	else:
		l.text = "%.2f×" % v


func _queue_save() -> void:
	if _save_timer != null:
		_save_timer.start(SAVE_DEBOUNCE_SEC)


func _flush_save() -> void:
	if _save_timer != null and not _save_timer.is_stopped():
		_save_timer.stop()
	settings.save()
