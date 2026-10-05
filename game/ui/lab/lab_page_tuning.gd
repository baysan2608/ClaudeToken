class_name LabPageTuning
extends LabPage
## Live tuning page: every numeric field of every move def (and the numeric thresholds of the counter-rule
## cells) with a slider, plus save / load / reset / export of user://tuning.cfg.

var move_id := ""
var rule_key := ""
var rule_idx := 0
var filter := ""
var mode := 0           # 0 moves, 1 rules

var _move_opt: OptionButton = null
var _rule_opt: OptionButton = null
var _fields: VBoxContainer = null
var _status: Label = null
var _move_ids: Array[String] = []
var _rule_cells: Array[Dictionary] = []
var _mode_buttons: Array[Button] = []


func _build() -> void:
	var bar := hrow()
	button("Save", func() -> void:
		var err := LabTuning.save()
		_say("saved %d changes to user://tuning.cfg" % LabTuning.change_count() if err == OK else "save failed (%d)" % err), bar, 16.0)
	button("Load", func() -> void:
		var n := LabTuning.load_file()
		_say("loaded %d fields" % n if n >= 0 else "no tuning.cfg yet")
		_rebuild_fields(), bar, 16.0)
	button("Reset all", func() -> void:
		LabTuning.reset_all()
		_say("everything restored")
		_rebuild_fields(), bar, 20.0)
	button("Export", func() -> void:
		var txt := LabTuning.export_file()
		DisplayServer.clipboard_set(txt)
		_say("exported to user://tuning_export.txt and the clipboard (%d lines)" % txt.count("\n")), bar, 18.0)
	_status = label("", true, 2.2)
	_mode_buttons = options("", ["Move fields", "Counter-rule thresholds"], 0, func(i: int) -> void:
		mode = i
		_show_mode(), 34.0)
	_move_ids.clear()
	for id in Moves.DEFS:
		_move_ids.append(String(id))
	_move_ids.sort_custom(func(a: String, b: String) -> bool:
		var da: Dictionary = Moves.DEFS[a]
		var db: Dictionary = Moves.DEFS[b]
		var ka := "%d%d%s" % [int(da.get("element", 9)), int(da.get("sub", 0)), a]
		var kb := "%d%d%s" % [int(db.get("element", 9)), int(db.get("sub", 0)), b]
		return ka < kb)
	_move_opt = OptionButton.new()
	_move_opt.custom_minimum_size.y = row_h()
	_move_opt.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
	for i in _move_ids.size():
		var d: Dictionary = Moves.DEFS[_move_ids[i]]
		var el := int(d.get("element", -1))
		var tag := "%s/%s" % [Sim.ELEMENT_NAMES[el].substr(0, 1), String(Sim.SUB_NAMES[el][int(d.get("sub", 0))])] if el >= 0 and el < 4 else "-"
		_move_opt.add_item("%s: %s (%s)" % [tag, String(d.get("name", _move_ids[i])).split(" / ")[0], _move_ids[i]], i)
	if move_id == "" and not _move_ids.is_empty():
		move_id = _move_ids[0]
	_move_opt.select(maxi(_move_ids.find(move_id), 0))
	_move_opt.item_selected.connect(func(i: int) -> void:
		move_id = _move_ids[i]
		_rebuild_fields())
	box.add_child(_move_opt)
	_rule_opt = OptionButton.new()
	_rule_opt.custom_minimum_size.y = row_h()
	_rule_opt.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
	_rule_cells = LabTuning.rule_cells()
	for i in _rule_cells.size():
		_rule_opt.add_item("%s  %s" % [String(_rule_cells[i].label), String(_rule_cells[i].id)], i)
	if not _rule_cells.is_empty():
		rule_key = String(_rule_cells[0].key)
		rule_idx = int(_rule_cells[0].idx)
		_rule_opt.select(0)
	_rule_opt.item_selected.connect(func(i: int) -> void:
		rule_key = String(_rule_cells[i].key)
		rule_idx = int(_rule_cells[i].idx)
		_rebuild_fields())
	box.add_child(_rule_opt)
	_fields = VBoxContainer.new()
	_fields.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_fields.add_theme_constant_override("separation", int(round(0.6 * ppm)))
	box.add_child(_fields)
	_show_mode()


func _say(t: String) -> void:
	if _status != null:
		_status.text = t
	if panel != null and panel.has_method("toast"):
		panel.call("toast", t)


func _show_mode() -> void:
	_move_opt.visible = mode == 0
	_rule_opt.visible = mode == 1
	_rebuild_fields()


func refresh() -> void:
	_rebuild_fields()


func _rebuild_fields() -> void:
	if _fields == null:
		return
	for c in _fields.get_children():
		_fields.remove_child(c)
		c.queue_free()
	if mode == 0:
		_move_fields()
	else:
		_rule_fields()


## A slider range around a value (x0.25 .. x3, or +-1 for zero).
static func range_for(v: float) -> Vector2:
	if absf(v) < 1e-6:
		return Vector2(-1.0, 1.0)
	var hi := absf(v) * 3.0
	var lo := 0.0 if v > 0.0 else -hi
	return Vector2(lo, hi) if v > 0.0 else Vector2(-hi, 0.0)


static func step_for(hi: float) -> float:
	if hi <= 2.0:
		return 0.01
	if hi <= 20.0:
		return 0.05
	if hi <= 200.0:
		return 0.25
	return 1.0


func _move_fields() -> void:
	if move_id == "":
		return
	var def: Dictionary = Moves.DEFS.get(move_id, {})
	var head := Label.new()
	head.text = "%s  -  %s" % [String(def.get("name", move_id)), move_id]
	head.add_theme_font_size_override("font_size", int(round(2.8 * ppm)))
	_fields.add_child(head)
	for f in LabTuning.numeric_fields(move_id):
		var path: String = f.path
		var orig: Variant = LabTuning.original_value(move_id, path)
		var base := float(orig) if orig != null else float(f.value)
		var rg := range_for(base)
		var fmt := "%.2f" if absf(base) < 20.0 else "%.1f"
		var s := slider(path, rg.x, rg.y, step_for(maxf(absf(rg.x), absf(rg.y))), clampf(float(f.value), rg.x, rg.y), fmt, func(v: float) -> void:
			LabTuning.set_value(move_id, path, v), _fields)
		if LabTuning.is_changed(move_id, path):
			(s.value as Label).add_theme_color_override("font_color", UiStyle.ACCENT)
		else:
			(s.value as Label).add_theme_color_override("font_color", UiStyle.INK_DIM)


func _rule_fields() -> void:
	if rule_key == "":
		return
	var head := Label.new()
	head.text = "rule %s #%d" % [rule_key, rule_idx]
	head.add_theme_font_size_override("font_size", int(round(2.8 * ppm)))
	_fields.add_child(head)
	for f in LabTuning.rule_fields(rule_key, rule_idx):
		var path: String = f.path
		var base := float(f.value)
		var rg := range_for(base if absf(base) > 1e-6 else 1.0)
		slider(path, rg.x, rg.y, 0.01 if rg.y <= 10.0 else 0.1, clampf(base, rg.x, rg.y), "%.2f", func(v: float) -> void:
			LabTuning.set_rule_value(rule_key, rule_idx, path, v), _fields)
