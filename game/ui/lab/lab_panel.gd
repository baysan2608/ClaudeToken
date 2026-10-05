class_name LabPanel
extends Control
## The Lab dev panel (docs/MOVESET.md section 14): backquote / F2 on desktop, "Dev" in the pause menu on touch.
## Six pages: Dev (time, step, cheats, AI, reset), Spawn, Moves, Combos, Matrix, Tuning.
## The sim keeps running behind it (freeze from the Dev page); the Game ignores player input while it is open.
##
## Signals:
##   action(name, args)   a request the Game carries out against the live world:
##       "spawn" {id, params, launch}           "stage" {id, params, counter, tier, perfect}
##       "try" {element, sub, slot, tier}        "combo_start" {id}  "combo_stop" {}
##       "clear" {}  "reset" {}  "heal" {}  "ai" {}  "load" {id}  "time" {}  "step" {n}  "overlay" {}
##   closed

signal action(name: String, args: Dictionary)
signal closed
signal opened

const PAGES := ["Dev", "Spawn", "Moves", "Combos", "Matrix", "Tuning"]

var session: LabSession = null
## Input device the move list shows: touch | keyboard | gamepad.
var device := "keyboard"
## The player's current selection (the Moves page opens on it).
var player_element := 0
var player_sub := 0
## Live combo tracker (the Combos page reads it every frame).
var tracker: ComboTracker = null
## Current scenario id (Dev page highlights it).
var scenario_id := ""

var _built := false
var _open := false
var _ppm := 8.0
var _dim: ColorRect
var _card: PanelContainer
var _title: Label
var _tab_buttons: Array[Button] = []
var _pages: Array[LabPage] = []
var _body: Control
var _page_idx := 0
var _toast_label: Label
var _toast_t := 0.0


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	visible = false


func _ready() -> void:
	if session == null:
		session = LabSession.new()
	build()
	get_viewport().size_changed.connect(_relayout)


func is_open() -> bool:
	return _open


func page(index: int) -> LabPage:
	build()
	return _pages[clampi(index, 0, _pages.size() - 1)]


func page_index() -> int:
	return _page_idx


func open_panel(page_index: int = -1) -> void:
	build()
	_open = true
	visible = true
	_relayout()
	if page_index >= 0:
		show_page(page_index)
	else:
		show_page(_page_idx)
	opened.emit()


func close_panel() -> void:
	if not _open:
		return
	_open = false
	visible = false
	closed.emit()


func toggle() -> void:
	if _open:
		close_panel()
	else:
		open_panel()


func set_player(element: int, sub: int) -> void:
	player_element = element
	player_sub = sub


func toast(text: String) -> void:
	if _toast_label != null:
		_toast_label.text = text
		_toast_label.visible = text != ""
	_toast_t = 2.2


func show_page(i: int) -> void:
	build()
	_page_idx = clampi(i, 0, PAGES.size() - 1)
	for k in _pages.size():
		_pages[k].visible = k == _page_idx
		_tab_buttons[k].button_pressed = k == _page_idx
	_pages[_page_idx].refresh()


func build() -> void:
	if _built:
		return
	_built = true
	if session == null:
		session = LabSession.new()
	_ppm = _compute_ppm()
	theme = _make_theme(_ppm)

	_dim = ColorRect.new()
	_dim.color = Color(0.02, 0.025, 0.035, 0.52)
	_dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_dim.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(_dim)

	_card = PanelContainer.new()
	add_child(_card)
	var root := VBoxContainer.new()
	root.add_theme_constant_override("separation", int(round(0.9 * _ppm)))
	_card.add_child(root)

	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", int(round(1.2 * _ppm)))
	root.add_child(head)
	_title = Label.new()
	_title.text = "Lab"
	_title.add_theme_font_size_override("font_size", int(round(3.8 * _ppm)))
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_title)
	_toast_label = Label.new()
	_toast_label.add_theme_font_size_override("font_size", int(round(2.4 * _ppm)))
	_toast_label.add_theme_color_override("font_color", UiStyle.ACCENT)
	_toast_label.visible = false
	head.add_child(_toast_label)
	var close := Button.new()
	close.text = "Close"
	close.custom_minimum_size = Vector2(16.0 * _ppm, 8.4 * _ppm)
	close.add_theme_font_size_override("font_size", int(round(2.8 * _ppm)))
	close.pressed.connect(close_panel)
	head.add_child(close)

	var tabs := HBoxContainer.new()
	tabs.add_theme_constant_override("separation", int(round(0.8 * _ppm)))
	root.add_child(tabs)
	var group := ButtonGroup.new()
	for i in PAGES.size():
		var b := Button.new()
		b.text = PAGES[i]
		b.toggle_mode = true
		b.button_group = group
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.custom_minimum_size = Vector2(0, 8.4 * _ppm)
		b.add_theme_font_size_override("font_size", int(round(2.7 * _ppm)))
		var idx := i
		b.pressed.connect(func() -> void: show_page(idx))
		tabs.add_child(b)
		_tab_buttons.append(b)

	_body = Control.new()
	_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_body.clip_contents = true
	root.add_child(_body)
	var scripts: Array[GDScript] = [LabPageDev, LabPageSpawn, LabPageMoves, LabPageCombos, LabPageMatrix, LabPageTuning]
	for sc in scripts:
		var pg: LabPage = sc.new()
		pg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		_body.add_child(pg)
		pg.setup(self, session, _ppm)
		pg.visible = false
		_pages.append(pg)
	_relayout()
	show_page(0)


func _compute_ppm() -> float:
	var vp := get_viewport()
	if vp == null:
		return 8.0
	return _scaled_ppm(vp)


## Physical px per mm shrunk a little on short screens (phones) so a page shows enough rows.
static func _scaled_ppm(vp: Viewport) -> float:
	var real := UiScale.px_per_mm(vp)
	var h_mm := vp.get_visible_rect().size.y / maxf(real, 0.1)
	return real * clampf(h_mm / 110.0, 0.8, 1.0)


func _make_theme(ppm: float) -> Theme:
	var t := UiStyle.build_theme(ppm)
	var fs := int(round(2.6 * ppm))
	t.default_font_size = fs
	for c in ["Label", "Button", "CheckButton", "OptionButton"]:
		t.set_font_size("font_size", c, fs)
	return t


func _relayout() -> void:
	if not _built:
		return
	var vp := get_viewport()
	var new_ppm := _scaled_ppm(vp)
	if absf(new_ppm - _ppm) > 0.25:
		_ppm = new_ppm
		theme = _make_theme(_ppm)
	var vs := vp.get_visible_rect().size
	var ins := UiScale.safe_insets(vp)
	var area := Rect2(ins.x, ins.y, vs.x - ins.x - ins.z, vs.y - ins.y - ins.w)
	var pad := 2.0 * _ppm
	var w := minf(area.size.x - pad * 2.0, 190.0 * _ppm)
	var h := minf(area.size.y - pad * 2.0, 118.0 * _ppm)
	var rect := Rect2(area.position + (area.size - Vector2(w, h)) * 0.5, Vector2(w, h))
	_card.set_anchors_preset(Control.PRESET_TOP_LEFT)
	_card.position = rect.position
	_card.size = rect.size
	_card.custom_minimum_size = rect.size


func _process(dt: float) -> void:
	if not _open:
		return
	if _toast_t > 0.0:
		_toast_t -= dt
		if _toast_t <= 0.0:
			_toast_label.visible = false
	_pages[_page_idx].live(dt)


func _input(event: InputEvent) -> void:
	if not _open:
		return
	if event.is_action_pressed("ui_cancel"):
		close_panel()
		get_viewport().set_input_as_handled()
