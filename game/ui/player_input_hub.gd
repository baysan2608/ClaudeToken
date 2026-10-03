class_name PlayerInputHub
extends Node
## Owns every input device and the HUD overlay. The game calls
## `poll_frame()` once per 60 Hz physics tick and gets one InputFrame
## (a reused object: edges latched since the last poll, then cleared).
##
## Add it to the scene tree once. It creates (as children):
##   - CanvasLayer 80: TouchControls + TargetMarker
##   - CanvasLayer 120 (process ALWAYS): SettingsPanel (opens on pause)
##   - DesktopInput (keyboard / mouse / gamepad)
##
## set_context(ctx) keys (all optional, absent = unchanged):
##   "element": int, "unlocked_elements": Array[int], "tech_label": String,
##   "tech_available": bool, "holding": bool,
##   "target_screen_pos": Vector2 or null (null hides the marker),
##   "target_label": String

signal paused_requested
signal settings_changed

## Show the touch HUD even without a touchscreen (desktop layout testing).
@export var force_touch_ui: bool = false
## Desktop only: let the mouse act as finger #15 on the touch HUD.
@export var emulate_touch_with_mouse: bool = false:
	set(v):
		emulate_touch_with_mouse = v
		_apply_mouse_emulation()
## Open the SettingsPanel automatically when pause is requested.
@export var auto_open_settings_panel: bool = true

## What the game wants: false hides the touch HUD (cutscenes, tutorials).
var controls_visible: bool = true:
	set(v):
		controls_visible = v
		_refresh_touch_visibility()

var settings: GameSettings
var touch: TouchControls
var desktop: DesktopInput
var settings_panel: SettingsPanel
var target_marker: TargetMarker

var _frame := InputFrame.new()
var _ft := InputFrame.new()
var _fd := InputFrame.new()
var _touch_wanted := false
var _gamepad_active := false
var _hud_layer: CanvasLayer
var _panel_layer: CanvasLayer


func _ready() -> void:
	settings = GameSettings.current()
	DesktopInput.register_actions()

	_hud_layer = CanvasLayer.new()
	_hud_layer.layer = 80
	_hud_layer.name = "HudLayer"
	add_child(_hud_layer)
	touch = TouchControls.new()
	touch.name = "TouchControls"
	touch.settings = settings
	_hud_layer.add_child(touch)
	target_marker = TargetMarker.new()
	target_marker.name = "TargetMarker"
	_hud_layer.add_child(target_marker)

	desktop = DesktopInput.new()
	desktop.name = "DesktopInput"
	desktop.settings = settings
	add_child(desktop)

	_panel_layer = CanvasLayer.new()
	_panel_layer.layer = 120
	_panel_layer.name = "PanelLayer"
	_panel_layer.process_mode = Node.PROCESS_MODE_ALWAYS
	add_child(_panel_layer)
	settings_panel = SettingsPanel.new()
	settings_panel.name = "SettingsPanel"
	settings_panel.settings = settings
	_panel_layer.add_child(settings_panel)

	touch.pause_requested.connect(_on_pause)
	desktop.pause_requested.connect(_on_pause)
	settings.changed.connect(_on_settings_changed)

	_touch_wanted = force_touch_ui or DisplayServer.is_touchscreen_available() or OS.has_feature("mobile")
	_apply_mouse_emulation()
	_refresh_touch_visibility()


func _input(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		if not event.device == InputEvent.DEVICE_ID_EMULATION:
			# A real finger: show the touch HUD even on a "desktop" iPad build.
			if not _touch_wanted or _gamepad_active:
				_touch_wanted = true
				_gamepad_active = false
				_refresh_touch_visibility()
	elif event is InputEventJoypadButton and OS.has_feature("mobile") and not force_touch_ui:
		if not _gamepad_active:
			_gamepad_active = true
			_refresh_touch_visibility()


## Once per simulation tick. The returned object is reused: copy it if you
## need to keep it past the next poll.
func poll_frame() -> InputFrame:
	touch.fill_frame(_ft)
	desktop.fill_frame(_fd)
	_frame.copy_from(_ft)
	_frame.merge_from(_fd)
	return _frame


func set_context(ctx: Dictionary) -> void:
	touch.set_context(ctx)
	if ctx.has("unlocked_elements"):
		desktop.set_unlocked(ctx["unlocked_elements"])
	if ctx.has("element"):
		target_marker.set_tint(UiStyle.element_color(int(ctx["element"])))
	if ctx.has("target_screen_pos"):
		target_marker.set_target(ctx["target_screen_pos"], ctx["target_label"] if ctx.has("target_label") else null)
	elif ctx.has("target_label"):
		target_marker.set_label(str(ctx["target_label"]))


## Release every held control (scene change, cutscene start).
func release_all() -> void:
	touch.release_all(true)
	desktop.cancel_all()


## True while the touch HUD is actually shown and listening.
func touch_ui_active() -> bool:
	return touch != null and touch.visible


func open_settings() -> void:
	if settings_panel != null:
		settings_panel.open_panel()


func _on_pause() -> void:
	paused_requested.emit()
	if auto_open_settings_panel and settings_panel != null:
		settings_panel.open_panel()


func _on_settings_changed() -> void:
	settings_changed.emit()


func _refresh_touch_visibility() -> void:
	if touch == null:
		return
	var show := controls_visible and _touch_wanted and not _gamepad_active
	if touch.visible != show:
		touch.visible = show


func _apply_mouse_emulation() -> void:
	if touch == null or desktop == null:
		return
	var emu := emulate_touch_with_mouse
	touch.mouse_emulation = emu
	desktop.mouse_enabled = not emu
	if emu:
		_touch_wanted = true
		_refresh_touch_visibility()
