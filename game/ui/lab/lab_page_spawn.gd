class_name LabPageSpawn
extends LabPage
## Spawner page: pick any material / state of docs/MOVESET.md section 14, set mass / speed / temperature / tier,
## then spawn it inert at the aim point or have the rival throw it at you.

var selected := "stone_20"
var launch := true
var params: Dictionary = {}

var _entry_buttons := {}
var _param_box: VBoxContainer = null
var _summary: Label = null
var _mode_buttons: Array[Button] = []


func _build() -> void:
	var entries := SpawnCatalog.entries()
	if not SpawnCatalog.find(selected).is_empty():
		pass
	elif not entries.is_empty():
		selected = String(entries[0].id)
	_mode_buttons = options("Delivery", ["Rival throws it at me", "Inert at the aim point"], 0 if launch else 1, func(i: int) -> void:
		launch = i == 0
		_refresh_mode(), 38.0)
	for g in SpawnCatalog.GROUPS:
		var any := false
		for e in entries:
			if e.group == g:
				any = true
		if not any:
			continue
		section(g)
		var flow := HFlowContainer.new()
		flow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		flow.add_theme_constant_override("h_separation", int(round(0.8 * ppm)))
		flow.add_theme_constant_override("v_separation", int(round(0.8 * ppm)))
		box.add_child(flow)
		var group := ButtonGroup.new()
		for e in entries:
			if e.group != g:
				continue
			var b := Button.new()
			b.text = String(e.label)
			b.toggle_mode = true
			b.button_group = group
			b.button_pressed = e.id == selected
			b.custom_minimum_size = Vector2(0, row_h())
			b.focus_mode = Control.FOCUS_ALL
			b.add_theme_font_size_override("font_size", int(round(2.5 * ppm)))
			var eid: String = e.id
			b.pressed.connect(func() -> void: _select(eid))
			flow.add_child(b)
			_entry_buttons[eid] = b
	section("Parameters")
	_summary = label("", true, 2.3)
	_param_box = VBoxContainer.new()
	_param_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_param_box.add_theme_constant_override("separation", int(round(0.8 * ppm)))
	box.add_child(_param_box)
	var go := hrow()
	button("Spawn", func() -> void: do_spawn(false), go, 30.0)
	button("Spawn and close", func() -> void: do_spawn(true), go, 36.0)
	_select(selected)


func _select(id: String) -> void:
	selected = id
	params = {}
	if _entry_buttons.has(id):
		(_entry_buttons[id] as Button).set_pressed_no_signal(true)
	_rebuild_params()
	_refresh_mode()


func _refresh_mode() -> void:
	var e := SpawnCatalog.find(selected)
	if e.is_empty():
		return
	var io: bool = bool(e.get("inert_only", false))
	var lo: bool = bool(e.get("launch_only", false))
	if io:
		launch = false
	if lo:
		launch = true
	if _mode_buttons.size() == 2:
		_mode_buttons[0].disabled = io
		_mode_buttons[1].disabled = lo
		_mode_buttons[0].set_pressed_no_signal(launch)
		_mode_buttons[1].set_pressed_no_signal(not launch)


func _rebuild_params() -> void:
	for c in _param_box.get_children():
		_param_box.remove_child(c)
		c.queue_free()
	var e := SpawnCatalog.find(selected)
	if e.is_empty():
		return
	var specs := SpawnCatalog.param_specs(e)
	var labels := {"mass": ["Mass", "%.0f kg", 1.0], "speed": ["Speed", "%.1f m/s", 0.5], "temp": ["Temperature", "%.0f C", 10.0], "tier": ["Tier", "T%d", 1.0]}
	for k in ["mass", "speed", "temp", "tier"]:
		if not specs.has(k):
			continue
		var sp: Array = specs[k]
		var l: Array = labels[k]
		var key: String = k
		var step: float = l[2]
		if k == "mass":
			step = 1.0 if float(sp[2]) > 20.0 else 0.5
		slider(String(l[0]), float(sp[1]), float(sp[2]), step, float(sp[0]), String(l[1]), func(v: float) -> void:
			params[key] = int(v) if key == "tier" else v
			_update_summary(), _param_box)
	_update_summary()


func _update_summary() -> void:
	var e := SpawnCatalog.find(selected)
	if e.is_empty() or _summary == null:
		return
	var kind: String = {"body": "a body", "verb": "the kit's own move", "zone": "a field", "perform": "the rival performs the move"}.get(String(e.build), "")
	_summary.text = "%s: %s (%s)" % [String(e.label), SpawnCatalog.describe(e, params), kind]


## Ask the game to spawn the selected entry; closes the panel when asked.
func do_spawn(close_after: bool) -> void:
	emit_action("spawn", {"id": selected, "params": params.duplicate(), "launch": launch})
	if close_after and panel != null and panel.has_method("close_panel"):
		panel.call("close_panel")
