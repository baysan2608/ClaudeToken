class_name Haptics
extends RefCounted
## Semantic haptic pulses. `Haptics.play("block")` etc. Never continuous: every
## pulse is a single short Input.vibrate_handheld(duration_ms, amplitude) call,
## at most one per MIN_GAP_MS, and nothing fires when settings.haptics is off.
##
## Kinds: "light" (UI tick / light hit), "block", "deflect" (timed guard),
## "perfect" (perfect deflect / perfect evade), "heavy" (taking a heavy hit),
## "lost_control" (technique lost control), "transform" (phase / form change).
##
## LIMITS (documented, not hidden): Godot exposes only duration + amplitude.
## True Core Haptics patterns (transient + continuous events, sharpness
## curves, AHAP files) need a native iOS plugin (a small Swift GDExtension /
## iOS plugin wrapping CHHapticEngine). This class is the single seam: swap
## `_emit()` for the plugin call later and every caller keeps working.

const MIN_GAP_MS := 60

## kind -> [duration_ms, amplitude 0..1]
const KINDS := {
	"light": [12, 0.30],
	"block": [26, 0.55],
	"deflect": [20, 0.70],
	"perfect": [38, 0.95],
	"heavy": [48, 1.0],
	"lost_control": [60, 0.80],
	"transform": [70, 0.60],
}

static var _last_ms: int = -1000000
## Test / diagnostics hooks.
static var pulse_count: int = 0
static var last_kind: String = ""
## When false the actual OS call is skipped (headless tests, desktop).
static var hardware_enabled: bool = true


## Returns true when a pulse was actually emitted.
static func play(kind: String) -> bool:
	if not KINDS.has(kind):
		return false
	if not GameSettings.current().haptics:
		return false
	var now := Time.get_ticks_msec()
	if now - _last_ms < MIN_GAP_MS:
		return false
	_last_ms = now
	var spec: Array = KINDS[kind]
	pulse_count += 1
	last_kind = kind
	_emit(int(spec[0]), float(spec[1]))
	return true


static func _emit(duration_ms: int, amplitude: float) -> void:
	if hardware_enabled:
		Input.vibrate_handheld(duration_ms, amplitude)


## Forget rate-limit history (tests).
static func reset_for_tests() -> void:
	_last_ms = -1000000
	pulse_count = 0
	last_kind = ""
