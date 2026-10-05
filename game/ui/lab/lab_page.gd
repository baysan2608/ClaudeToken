class_name LabPage
extends ScrollContainer
## Base of the Lab dev-panel pages: a scrolling column plus code-built row helpers sized in millimetres
## (rows >= 8 mm, text >= 2.4 mm) so every control is a touch target.

var panel: Control = null          # the LabPanel (typed loosely: pages are built before the panel is in the tree)
var session: LabSession = null
var ppm := 8.0
var box: VBoxContainer = null


func _init() -> void:
	horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	box = VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_theme_constant_override("separation", 4)
	add_child(box)


## Called once by the panel after the session / ppm are set. Build the rows here.
func setup(p: Control, s: LabSession, new_ppm: float) -> void:
	panel = p
	session = s
	ppm = new_ppm
	box.add_theme_constant_override("separation", int(round(0.8 * ppm)))
	_build()


func _build() -> void:
	pass


## The page became visible: re-read live state.
func refresh() -> void:
	pass


## Called every frame while visible.
func live(_dt: float) -> void:
	pass


func emit_action(action: String, args: Dictionary = {}) -> void:
	if panel != null and panel.has_signal("action"):
		panel.emit_signal("action", action, args)


# --- helpers -----------------------------------------------------------------------------------

func row_h() -> float:
	return 8.4 * ppm


func section(title: String) -> Label:
	var l := Label.new()
	l.text = title.to_upper()
	l.add_theme_font_size_override("font_size", int(round(2.1 * ppm)))
	l.add_theme_color_override("font_color", UiStyle.ACCENT)
	var m := MarginContainer.new()
	m.add_theme_constant_override("margin_top", int(round(1.4 * ppm)))
	m.mouse_filter = Control.MOUSE_FILTER_PASS
	m.add_child(l)
	box.add_child(m)
	return l


func label(text: String, dim: bool = false, size_mm: float = 2.5, parent: Node = null) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.add_theme_font_size_override("font_size", int(round(size_mm * ppm)))
	l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	l.mouse_filter = Control.MOUSE_FILTER_PASS
	if dim:
		l.add_theme_color_override("font_color", UiStyle.INK_DIM)
	(parent if parent != null else box).add_child(l)
	return l


func button(text: String, cb: Callable, parent: Node = null, min_w_mm: float = 0.0) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(min_w_mm * ppm, row_h())
	b.focus_mode = Control.FOCUS_ALL
	b.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
	b.pressed.connect(cb)
	(parent if parent != null else box).add_child(b)
	return b


func toggle(text: String, value: bool, cb: Callable) -> CheckButton:
	var c := CheckButton.new()
	c.text = text
	c.button_pressed = value
	c.custom_minimum_size.y = row_h()
	c.focus_mode = Control.FOCUS_ALL
	c.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
	c.toggled.connect(cb)
	box.add_child(c)
	return c


## A labelled slider row. fmt: "%.2f" style format string. Returns {row, slider, value}.
func slider(text: String, lo: float, hi: float, step: float, value: float, fmt: String, cb: Callable, parent: Node = null) -> Dictionary:
	var row := HBoxContainer.new()
	row.custom_minimum_size.y = row_h()
	row.mouse_filter = Control.MOUSE_FILTER_PASS
	var l := Label.new()
	l.text = text
	l.custom_minimum_size.x = 24.0 * ppm
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.add_theme_font_size_override("font_size", int(round(2.5 * ppm)))
	l.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(l)
	var s := HSlider.new()
	s.min_value = lo
	s.max_value = hi
	s.step = step
	s.value = value
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	s.custom_minimum_size = Vector2(22.0 * ppm, 6.0 * ppm)
	s.focus_mode = Control.FOCUS_ALL
	row.add_child(s)
	var v := Label.new()
	v.custom_minimum_size.x = 14.0 * ppm
	v.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	v.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	v.add_theme_font_size_override("font_size", int(round(2.5 * ppm)))
	v.add_theme_color_override("font_color", UiStyle.INK_DIM)
	v.text = fmt % value
	v.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(v)
	s.value_changed.connect(func(x: float) -> void:
		v.text = fmt % x
		cb.call(x))
	(parent if parent != null else box).add_child(row)
	return {"row": row, "slider": s, "value": v}


## A wrapping row of exclusive buttons. Returns the buttons (index = option). cb(index).
func options(text: String, labels: Array, selected: int, cb: Callable, min_w_mm: float = 14.0) -> Array[Button]:
	if text != "":
		label(text, true, 2.3)
	var flow := HFlowContainer.new()
	flow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	flow.add_theme_constant_override("h_separation", int(round(0.8 * ppm)))
	flow.add_theme_constant_override("v_separation", int(round(0.8 * ppm)))
	box.add_child(flow)
	var group := ButtonGroup.new()
	var out: Array[Button] = []
	for i in labels.size():
		var b := Button.new()
		b.text = str(labels[i])
		b.toggle_mode = true
		b.button_group = group
		b.button_pressed = i == selected
		b.focus_mode = Control.FOCUS_ALL
		b.custom_minimum_size = Vector2(min_w_mm * ppm, row_h())
		b.add_theme_font_size_override("font_size", int(round(2.5 * ppm)))
		var idx := i
		b.pressed.connect(func() -> void: cb.call(idx))
		flow.add_child(b)
		out.append(b)
	return out


func hrow(parent: Node = null) -> HBoxContainer:
	var h := HBoxContainer.new()
	h.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_theme_constant_override("separation", int(round(0.8 * ppm)))
	(parent if parent != null else box).add_child(h)
	return h


func clear_box() -> void:
	for c in box.get_children():
		box.remove_child(c)
		c.queue_free()
