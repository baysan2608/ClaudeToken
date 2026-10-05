class_name LabPageMoves
extends LabPage
## Move list page: per element / sub-element, every bound move with the input for the active device, costs,
## S/A/R frames, tier table, a description and a "Try" button that plays it through the real input path.

var element := 0
var sub := 0
var tier_choice := {}      # slot -> tier chosen for Try

var _el_buttons: Array[Button] = []
var _sub_buttons: Array[Button] = []
var _list: VBoxContainer = null
var _dev_label: Label = null


func _build() -> void:
	if panel != null:
		element = int(panel.get("player_element"))
		sub = int(panel.get("player_sub"))
	_el_buttons = options("", UiStyle.ELEMENT_NAMES, element, func(i: int) -> void:
		element = i
		sub = 0
		_sync_subs()
		_fill_list(), 20.0)
	_sub_buttons = options("", Sim.SUB_NAMES[element], sub, func(i: int) -> void:
		sub = i
		_fill_list(), 20.0)
	_dev_label = label("", true, 2.2)
	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_theme_constant_override("separation", int(round(1.2 * ppm)))
	box.add_child(_list)
	_fill_list()


func _sync_subs() -> void:
	for i in 4:
		if i < _sub_buttons.size():
			_sub_buttons[i].text = String(Sim.SUB_NAMES[element][i])
			_sub_buttons[i].set_pressed_no_signal(i == sub)


func refresh() -> void:
	if panel != null:
		var pe := int(panel.get("player_element"))
		var ps := int(panel.get("player_sub"))
		if pe != element or ps != sub:
			element = pe
			sub = ps
			for i in _el_buttons.size():
				_el_buttons[i].set_pressed_no_signal(i == element)
			_sync_subs()
	_fill_list()


func _device() -> String:
	return String(panel.get("device")) if panel != null else "keyboard"


func _fill_list() -> void:
	if _list == null:
		return
	for c in _list.get_children():
		_list.remove_child(c)
		c.queue_free()
	var dev := _device()
	if _dev_label != null:
		_dev_label.text = "Inputs shown for: %s  (S startup / A active / R recovery in 60 Hz frames)" % dev
	for r in MoveListData.rows(element, sub, dev):
		_list.add_child(_row(r))


func _row(r: Dictionary) -> Control:
	var card := PanelContainer.new()
	card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(1, 1, 1, 0.045)
	sb.border_color = Color(UiStyle.element_color(element), 0.35)
	sb.set_border_width_all(1)
	sb.set_corner_radius_all(int(1.2 * ppm))
	sb.set_content_margin_all(1.2 * ppm)
	card.add_theme_stylebox_override("panel", sb)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", int(round(0.4 * ppm)))
	card.add_child(v)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", int(round(1.0 * ppm)))
	v.add_child(top)
	var title := Label.new()
	title.text = "%s  [%s]" % [String(r.name), String(r.slot)]
	title.add_theme_font_size_override("font_size", int(round(2.9 * ppm)))
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.clip_text = true
	top.add_child(title)
	var slot: String = r.slot
	var max_tier: int = int(r.max_tier)
	var tier_btn: Button = null
	if max_tier > 0:
		tier_btn = Button.new()
		tier_btn.text = "T%d" % int(tier_choice.get(slot, 0))
		tier_btn.custom_minimum_size = Vector2(10.0 * ppm, row_h())
		tier_btn.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
		tier_btn.pressed.connect(func() -> void:
			var t := (int(tier_choice.get(slot, 0)) + 1) % (max_tier + 1)
			tier_choice[slot] = t
			tier_btn.text = "T%d" % t)
		top.add_child(tier_btn)
	var tryb := Button.new()
	tryb.text = "Try"
	tryb.custom_minimum_size = Vector2(14.0 * ppm, row_h())
	tryb.add_theme_font_size_override("font_size", int(round(2.8 * ppm)))
	tryb.pressed.connect(func() -> void: do_try(slot, int(tier_choice.get(slot, 0))))
	top.add_child(tryb)
	var info := Label.new()
	info.text = "%s   |   %s   |   %s" % [String(r.input), String(r.cost), String(r.frames)]
	info.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.add_theme_font_size_override("font_size", int(round(2.4 * ppm)))
	info.add_theme_color_override("font_color", UiStyle.ACCENT)
	v.add_child(info)
	if String(r.desc) != "":
		var d := Label.new()
		d.text = String(r.desc)
		d.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		d.add_theme_font_size_override("font_size", int(round(2.3 * ppm)))
		d.add_theme_color_override("font_color", UiStyle.INK_DIM)
		v.add_child(d)
	for t in r.tiers:
		var tl := Label.new()
		tl.text = "T%d  hold %.2f s%s: %s" % [int(t.tier), float(t.hold), (" (%s)" % String(t.name)) if String(t.name) != "" else "", String(t.text)]
		tl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		tl.add_theme_font_size_override("font_size", int(round(2.2 * ppm)))
		tl.add_theme_color_override("font_color", UiStyle.INK_DIM)
		v.add_child(tl)
	if String(r.counter) != "":
		var cl := Label.new()
		cl.text = String(r.counter)
		cl.add_theme_font_size_override("font_size", int(round(2.2 * ppm)))
		cl.add_theme_color_override("font_color", UiStyle.INK_DIM)
		v.add_child(cl)
	return card


## Play the move through the real input path (closes the panel so you can watch).
func do_try(slot: String, tier: int) -> void:
	emit_action("try", {"element": element, "sub": sub, "slot": slot, "tier": tier})
	if panel != null and panel.has_method("close_panel"):
		panel.call("close_panel")
