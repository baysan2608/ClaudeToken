class_name GameSettings
extends RefCounted
## Player-facing settings persisted to user://settings.cfg (ConfigFile).
##
## One shared instance lives behind GameSettings.current(); the settings panel,
## PlayerInputHub, TouchControls, DesktopInput, Haptics and the presentation
## layer (screen shake / flashes / reduced motion) all read it. Every setter
## clamps, so a hand-edited or corrupted file can never produce an
## out-of-range value. Emit `changed` (the panel does) after a batch of edits.

signal changed

const PATH := "user://settings.cfg"
const PRESETS: Array[String] = ["default", "compact", "wide"]

const SCALE_MIN := 0.8
const SCALE_MAX := 1.4
const OPACITY_MIN := 0.3
const OPACITY_MAX := 1.0
const SENS_MIN := 0.3
const SENS_MAX := 2.5

## Touch control size multiplier (buttons, stick, cluster spread).
var control_scale: float = 1.0:
	set(v):
		control_scale = clampf(v, SCALE_MIN, SCALE_MAX)
## Idle opacity of touch controls (pressed controls always brighten).
var control_opacity: float = 0.8:
	set(v):
		control_opacity = clampf(v, OPACITY_MIN, OPACITY_MAX)
## "default", "compact" or "wide" button-cluster layout.
var layout_preset: String = "default":
	set(v):
		layout_preset = v if PRESETS.has(v) else "default"
## Mirror the layout: stick on the right, buttons on the left.
var left_handed: bool = false
## Draw text labels on every touch button (ATTACK / GUARD / EVADE / tech name).
var strong_labels: bool = false
## Camera drag / stick-look sensitivity multiplier.
var camera_sensitivity: float = 1.0:
	set(v):
		camera_sensitivity = clampf(v, SENS_MIN, SENS_MAX)
var invert_y: bool = false
## 0..1 scale for camera shake (presentation layer reads this).
var screen_shake: float = 1.0:
	set(v):
		screen_shake = clampf(v, 0.0, 1.0)
## 0..1 scale for full-screen flashes / bright strobing effects.
var flashes: float = 1.0:
	set(v):
		flashes = clampf(v, 0.0, 1.0)
var haptics: bool = true
var reduced_motion: bool = false
## Gentle slow-motion on decisive moments (accessibility timing assist).
var slowmo_assist: bool = false
var show_debug: bool = false

## Where save() writes by default (set by load_from; tests point it elsewhere).
var storage_path: String = PATH

static var _current: GameSettings = null


## Shared, lazily loaded instance.
static func current() -> GameSettings:
	if _current == null:
		_current = load_from(PATH)
	return _current


## Replace the shared instance (tests, or after an external reload).
static func set_current(s: GameSettings) -> void:
	_current = s


## Load settings from `path`. A missing or damaged file yields defaults.
static func load_from(path: String = PATH) -> GameSettings:
	var s := GameSettings.new()
	s.storage_path = path
	var cfg := ConfigFile.new()
	if cfg.load(path) != OK:
		return s
	s.control_scale = _f(cfg, "controls", "control_scale", s.control_scale)
	s.control_opacity = _f(cfg, "controls", "control_opacity", s.control_opacity)
	s.layout_preset = str(cfg.get_value("controls", "layout_preset", s.layout_preset))
	s.left_handed = _b(cfg, "controls", "left_handed", s.left_handed)
	s.strong_labels = _b(cfg, "controls", "strong_labels", s.strong_labels)
	s.camera_sensitivity = _f(cfg, "controls", "camera_sensitivity", s.camera_sensitivity)
	s.invert_y = _b(cfg, "controls", "invert_y", s.invert_y)
	s.screen_shake = _f(cfg, "comfort", "screen_shake", s.screen_shake)
	s.flashes = _f(cfg, "comfort", "flashes", s.flashes)
	s.haptics = _b(cfg, "comfort", "haptics", s.haptics)
	s.reduced_motion = _b(cfg, "comfort", "reduced_motion", s.reduced_motion)
	s.slowmo_assist = _b(cfg, "comfort", "slowmo_assist", s.slowmo_assist)
	s.show_debug = _b(cfg, "debug", "show_debug", s.show_debug)
	return s


## Write settings to `path` (default: storage_path). Returns an Error code.
func save(path: String = "") -> int:
	var target := path if path != "" else storage_path
	var cfg := ConfigFile.new()
	cfg.set_value("controls", "control_scale", control_scale)
	cfg.set_value("controls", "control_opacity", control_opacity)
	cfg.set_value("controls", "layout_preset", layout_preset)
	cfg.set_value("controls", "left_handed", left_handed)
	cfg.set_value("controls", "strong_labels", strong_labels)
	cfg.set_value("controls", "camera_sensitivity", camera_sensitivity)
	cfg.set_value("controls", "invert_y", invert_y)
	cfg.set_value("comfort", "screen_shake", screen_shake)
	cfg.set_value("comfort", "flashes", flashes)
	cfg.set_value("comfort", "haptics", haptics)
	cfg.set_value("comfort", "reduced_motion", reduced_motion)
	cfg.set_value("comfort", "slowmo_assist", slowmo_assist)
	cfg.set_value("debug", "show_debug", show_debug)
	return cfg.save(target)


## Persist the shared instance.
static func save_current() -> int:
	return current().save()


func reset_to_defaults() -> void:
	control_scale = 1.0
	control_opacity = 0.8
	layout_preset = "default"
	left_handed = false
	strong_labels = false
	camera_sensitivity = 1.0
	invert_y = false
	screen_shake = 1.0
	flashes = 1.0
	haptics = true
	reduced_motion = false
	slowmo_assist = false
	show_debug = false
	changed.emit()


func copy_from(o: GameSettings) -> void:
	control_scale = o.control_scale
	control_opacity = o.control_opacity
	layout_preset = o.layout_preset
	left_handed = o.left_handed
	strong_labels = o.strong_labels
	camera_sensitivity = o.camera_sensitivity
	invert_y = o.invert_y
	screen_shake = o.screen_shake
	flashes = o.flashes
	haptics = o.haptics
	reduced_motion = o.reduced_motion
	slowmo_assist = o.slowmo_assist
	show_debug = o.show_debug


func equals(o: GameSettings) -> bool:
	return (
		is_equal_approx(control_scale, o.control_scale)
		and is_equal_approx(control_opacity, o.control_opacity)
		and layout_preset == o.layout_preset
		and left_handed == o.left_handed
		and strong_labels == o.strong_labels
		and is_equal_approx(camera_sensitivity, o.camera_sensitivity)
		and invert_y == o.invert_y
		and is_equal_approx(screen_shake, o.screen_shake)
		and is_equal_approx(flashes, o.flashes)
		and haptics == o.haptics
		and reduced_motion == o.reduced_motion
		and slowmo_assist == o.slowmo_assist
		and show_debug == o.show_debug
	)


static func _f(cfg: ConfigFile, section: String, key: String, fallback: float) -> float:
	var v: Variant = cfg.get_value(section, key, fallback)
	if v is float or v is int:
		return float(v)
	return fallback


static func _b(cfg: ConfigFile, section: String, key: String, fallback: bool) -> bool:
	var v: Variant = cfg.get_value(section, key, fallback)
	if v is bool:
		return v
	if v is int:
		return v != 0
	return fallback
