class_name LabPageDev
extends LabPage
## Dev page: time scale, frame step, cheats (infinite resources, god mode), AI (on / off, difficulty, kit,
## drill), reset / clear / heal, scenario shortcuts and the debug overlay.

const SCENARIOS := [["lab", "Lab"], ["spar", "Free Spar"], ["molten_exchange", "Molten Exchange"]]

var _time_slider: Dictionary = {}
var _freeze_btn: CheckButton = null
var _status: Label = null
var _kit_buttons: Array[Button] = []
var _sub_buttons: Array[Button] = []
var _preset_buttons: Array[Button] = []
var _drill_buttons: Array[Button] = []
var _ai_toggle: CheckButton = null


func _build() -> void:
	section("Time")
	_time_slider = slider("Time scale", 0.1, 1.0, 0.05, session.time_scale, "x%.2f", func(v: float) -> void:
		session.set_time_scale(v)
		emit_action("time"))
	var presets := hrow()
	for ts in LabSession.TIME_SCALES:
		var v: float = ts
		button("x%.2f" % v, func() -> void:
			(_time_slider.slider as HSlider).value = v, presets, 12.0)
	_freeze_btn = toggle("Freeze simulation", session.frozen, func(on: bool) -> void:
		session.frozen = on
		emit_action("time"))
	var steps := hrow()
	button("Step 1 tick", func() -> void:
		session.frozen = true
		_freeze_btn.set_pressed_no_signal(true)
		session.request_step(1), steps, 20.0)
	button("Step 6", func() -> void:
		session.frozen = true
		_freeze_btn.set_pressed_no_signal(true)
		session.request_step(6), steps, 16.0)
	button("Step 30", func() -> void:
		session.frozen = true
		_freeze_btn.set_pressed_no_signal(true)
		session.request_step(30), steps, 16.0)
	_status = label("", true, 2.2)

	section("Cheats")
	toggle("Infinite Focus, heat, water and metal", session.infinite, func(on: bool) -> void: session.infinite = on)
	toggle("God mode (health and balance never drop)", session.god, func(on: bool) -> void: session.god = on)
	toggle("Debug overlay (bodies, zones, power)", session.overlay, func(on: bool) -> void:
		session.overlay = on
		emit_action("overlay"))

	section("Rival AI")
	_ai_toggle = toggle("AI acts (off: the rival stands still and only throws what you spawn)", session.ai_enabled, func(on: bool) -> void:
		session.ai_enabled = on
		emit_action("ai"))
	_preset_buttons = options("Difficulty", LabSession.AI_PRESET_LABELS, maxi(LabSession.AI_PRESETS.find(session.ai_preset), 0), func(i: int) -> void:
		session.ai_preset = LabSession.AI_PRESETS[i]
		emit_action("ai"), 14.0)
	var kit_names: Array = ["Scenario kit", "Earth", "Water", "Fire", "Air", "All four"]
	_kit_buttons = options("Kit", kit_names, maxi(LabSession.AI_KITS.find(session.ai_kit), 0), func(i: int) -> void:
		session.ai_kit = LabSession.AI_KITS[i]
		session.ai_sub = -1
		_refresh_sub_buttons()
		emit_action("ai"), 15.0)
	_sub_buttons = options("Sub-element (single-element kits)", ["Any", "Sub 1", "Sub 2", "Sub 3", "Sub 4"], session.ai_sub + 1, func(i: int) -> void:
		session.ai_sub = i - 1
		emit_action("ai"), 13.0)
	_drill_buttons = options("Behaviour", ["Free sparring", "Passive", "Matrix drill"], maxi(LabSession.AI_DRILLS.find(session.ai_drill), 0), func(i: int) -> void:
		session.ai_drill = LabSession.AI_DRILLS[i]
		emit_action("ai"), 18.0)
	_refresh_sub_buttons()

	section("World")
	var w1 := hrow()
	button("Reset scenario", func() -> void: emit_action("reset"), w1, 28.0)
	button("Clear bodies", func() -> void: emit_action("clear"), w1, 28.0)
	button("Heal and refill", func() -> void: emit_action("heal"), w1, 28.0)
	label("Load a scenario", true, 2.3)
	var w2 := hrow()
	for sc in SCENARIOS:
		var sid: String = sc[0]
		button(String(sc[1]), func() -> void: emit_action("load", {"id": sid}), w2, 26.0)


func _refresh_sub_buttons() -> void:
	var single := session.ai_kit in ["earth", "water", "fire", "air"]
	for b in _sub_buttons:
		b.disabled = not single
	var el: int = ["earth", "water", "fire", "air"].find(session.ai_kit)
	if el >= 0:
		for i in 4:
			_sub_buttons[i + 1].text = String(Sim.SUB_NAMES[el][i])
		_sub_buttons[0].text = "Any"
	else:
		for i in 4:
			_sub_buttons[i + 1].text = "Sub %d" % (i + 1)


func refresh() -> void:
	if _time_slider.is_empty():
		return
	(_time_slider.slider as HSlider).set_value_no_signal(session.time_scale)
	(_time_slider.value as Label).text = "x%.2f" % session.time_scale
	_freeze_btn.set_pressed_no_signal(session.frozen)
	_ai_toggle.set_pressed_no_signal(session.ai_enabled)
	for i in _preset_buttons.size():
		_preset_buttons[i].set_pressed_no_signal(LabSession.AI_PRESETS[i] == session.ai_preset)
	for i in _kit_buttons.size():
		_kit_buttons[i].set_pressed_no_signal(LabSession.AI_KITS[i] == session.ai_kit)
	for i in _sub_buttons.size():
		_sub_buttons[i].set_pressed_no_signal(i - 1 == session.ai_sub)
	for i in _drill_buttons.size():
		_drill_buttons[i].set_pressed_no_signal(LabSession.AI_DRILLS[i] == session.ai_drill)
	_refresh_sub_buttons()
	_update_status()


func _update_status() -> void:
	if _status != null:
		_status.text = "%s  |  scenario: %s%s" % ["FROZEN" if session.frozen else "running", panel.scenario_id if panel != null else "", ("  |  %d tuning changes" % LabTuning.change_count()) if LabTuning.change_count() > 0 else ""]


func live(_dt: float) -> void:
	_update_status()
