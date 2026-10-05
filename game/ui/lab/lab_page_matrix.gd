class_name LabPageMatrix
extends LabPage
## Matrix viewer page (docs/MOVESET.md section 14): a threat class (+ mass / speed / heat / tier) against a
## counter move (+ tier, perfect): threat power, counter power, ratio, band and outcome from
## Interactions.predict. "Stage it" spawns the threat at the player with the rival set to throw it.

var threat_id := "stone_45"
var threat_params: Dictionary = {}
var counter_id := "swallow"
var counter_tier := 0
var perfect := false
var last: Dictionary = {}

var _threat_opt: OptionButton = null
var _counter_opt: OptionButton = null
var _param_box: VBoxContainer = null
var _tier_box: VBoxContainer = null
var _result: RichTextLabel = null
var _counters: Array[Dictionary] = []


func _build() -> void:
	section("Result")
	_result = RichTextLabel.new()
	_result.bbcode_enabled = true
	_result.fit_content = true
	_result.scroll_active = false
	_result.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_result.add_theme_font_size_override("normal_font_size", int(round(2.7 * ppm)))
	_result.add_theme_font_size_override("bold_font_size", int(round(3.4 * ppm)))
	box.add_child(_result)
	var btns := hrow()
	button("Stage it", func() -> void: stage(), btns, 30.0)
	button("Stage and close", func() -> void:
		stage()
		if panel != null and panel.has_method("close_panel"):
			panel.call("close_panel"), btns, 38.0)
	section("Threat")
	_threat_opt = OptionButton.new()
	_threat_opt.custom_minimum_size.y = row_h()
	_threat_opt.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
	var entries := SpawnCatalog.entries()
	var sel := 0
	for i in entries.size():
		_threat_opt.add_item("%s: %s" % [String(entries[i].group), String(entries[i].label)], i)
		if entries[i].id == threat_id:
			sel = i
	_threat_opt.select(sel)
	_threat_opt.item_selected.connect(func(i: int) -> void:
		threat_id = String(SpawnCatalog.entries()[i].id)
		threat_params = {}
		_rebuild_threat_params()
		update())
	box.add_child(_threat_opt)
	_param_box = VBoxContainer.new()
	_param_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_param_box.add_theme_constant_override("separation", int(round(0.8 * ppm)))
	box.add_child(_param_box)

	section("Counter")
	_counters = MatrixQuery.counters()
	_counter_opt = OptionButton.new()
	_counter_opt.custom_minimum_size.y = row_h()
	_counter_opt.add_theme_font_size_override("font_size", int(round(2.6 * ppm)))
	var csel := 0
	for i in _counters.size():
		_counter_opt.add_item(String(_counters[i].label), i)
		if _counters[i].id == counter_id:
			csel = i
	_counter_opt.select(csel)
	_counter_opt.item_selected.connect(func(i: int) -> void:
		counter_id = String(_counters[i].id)
		counter_tier = 0
		_rebuild_counter_params()
		update())
	box.add_child(_counter_opt)
	_tier_box = VBoxContainer.new()
	_tier_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_tier_box.add_theme_constant_override("separation", int(round(0.8 * ppm)))
	box.add_child(_tier_box)

	_rebuild_threat_params()
	_rebuild_counter_params()
	update()


func _rebuild_threat_params() -> void:
	for c in _param_box.get_children():
		_param_box.remove_child(c)
		c.queue_free()
	var e := SpawnCatalog.find(threat_id)
	if e.is_empty():
		return
	var specs := SpawnCatalog.param_specs(e)
	var labels := {"mass": ["Mass", "%.0f kg"], "speed": ["Speed", "%.1f m/s"], "temp": ["Temperature", "%.0f C"], "tier": ["Tier", "T%d"]}
	for k in ["mass", "speed", "temp", "tier"]:
		if not specs.has(k):
			continue
		var sp: Array = specs[k]
		var key: String = k
		var step := 1.0 if (k == "tier" or float(sp[2]) > 20.0) else 0.5
		slider(String(labels[k][0]), float(sp[1]), float(sp[2]), step, float(sp[0]), String(labels[k][1]), func(v: float) -> void:
			threat_params[key] = int(v) if key == "tier" else v
			update(), _param_box)


func _rebuild_counter_params() -> void:
	for c in _tier_box.get_children():
		_tier_box.remove_child(c)
		c.queue_free()
	var c := MatrixQuery.find_counter(counter_id)
	if c.is_empty():
		return
	if int(c.max_tier) > 0:
		slider("Counter tier", 0.0, float(c.max_tier), 1.0, float(counter_tier), "T%d", func(v: float) -> void:
			counter_tier = int(v)
			update(), _tier_box)
	var pt := CheckButton.new()
	pt.text = "Perfect timing (x1.5)"
	pt.button_pressed = perfect
	pt.custom_minimum_size.y = row_h()
	pt.add_theme_font_size_override("font_size", int(round(2.5 * ppm)))
	pt.toggled.connect(func(on: bool) -> void:
		perfect = on
		update())
	_tier_box.add_child(pt)


func update() -> void:
	last = MatrixQuery.predict(threat_id, threat_params, counter_id, counter_tier, perfect)
	if _result == null:
		return
	if not bool(last.get("ok", false)):
		_result.text = "[color=#f07a70]%s[/color]" % String(last.get("msg", ""))
		return
	var band := String(last.band)
	var col: String = {"full": "#7fe08a", "partial": "#f3d36b", "fail": "#f07a70", "cond": "#9db4ff", "inert": "#9db4ff", "form": "#9db4ff"}.get(band, "#ffffff")
	var t := "[b][color=%s]%s[/color][/b]  (%s)\n" % [col, String(last.outcome).to_upper(), band]
	t += "threat power  [b]%.1f[/b]    counter power  [b]%.1f[/b] (x mods = %.1f)    ratio  [b]%.2f[/b]\n" % [float(last.tp), float(last.cp), float(last.cp_eff), float(last.ratio)]
	t += "a full block needs counter power of about [b]%.1f[/b]\n" % float(last.needs)
	t += "[color=#9aa7b5]%s  vs  %s%s   rule: %s[/color]\n" % [String(last.threat_cls), String(last.counter_cls), ("  -> " + String(last.to)) if String(last.to) != "" else "", String(last.rule_id) if String(last.rule_id) != "" else "default"]
	t += "[color=#9aa7b5]%s[/color]" % String(last.summary)
	_result.text = t


## Spawn the threat at the player with the rival set to throw it, and equip the counter.
func stage() -> void:
	var c := MatrixQuery.find_counter(counter_id)
	emit_action("stage", {"id": threat_id, "params": threat_params.duplicate(), "counter": counter_id, "tier": counter_tier,
		"perfect": perfect, "element": int(c.get("element", -1)), "sub": int(c.get("sub", 0)), "slot": String(c.get("slot", ""))})


func refresh() -> void:
	update()
