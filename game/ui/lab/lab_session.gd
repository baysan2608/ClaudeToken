class_name LabSession
extends RefCounted
## Lab / dev-panel state that outlives the panel: time scale, frame step, infinite resources, god mode,
## AI settings, overlay flags. The Game applies it every tick (pre_step / post_step) and reads the AI choice
## when a scenario loads. Pure data + small helpers so tests can drive it without a scene.

const AI_PRESETS: Array[String] = ["novice", "adept", "master"]
const AI_PRESET_LABELS: Array[String] = ["Easy", "Normal", "Hard"]
const AI_KITS: Array[String] = ["mixed", "earth", "water", "fire", "air", "all"]
const AI_DRILLS: Array[String] = ["", "passive", "matrix"]
const TIME_SCALES: Array[float] = [0.1, 0.25, 0.5, 1.0]

var time_scale := 1.0
var frozen := false
var infinite := false
var god := false
var overlay := false
var ai_enabled := true
var ai_preset := "adept"
## "mixed" (the scenario's own kit), "earth".."air", or "all"; with ai_sub >= 0 only that sub-element.
var ai_kit := "mixed"
var ai_sub := -1
var ai_drill := ""
var _steps := 0


func request_step(n: int = 1) -> void:
	_steps += n


## True when the game should run one tick even though frozen (consumes one request).
func consume_step() -> bool:
	if _steps > 0:
		_steps -= 1
		return true
	return false


func pending_steps() -> int:
	return _steps


func set_time_scale(v: float) -> void:
	time_scale = clampf(v, 0.1, 1.0)


## Before the sim tick: infinite resources for everyone who fights (not dummies).
func pre_step(w: CombatWorld) -> void:
	if not infinite:
		return
	for a in w.actors:
		if not a.is_dummy:
			SpawnCatalog.top_up(w, a)


## After the sim tick: god mode keeps health and balance full.
func post_step(player: ActorState) -> void:
	if god and player != null:
		player.health = Sim.HEALTH_MAX
		player.balance = Sim.BALANCE_MAX


## Element indices of the AI kit choice (null = leave the scenario's own).
func ai_elements() -> Variant:
	match ai_kit:
		"earth":
			return [0]
		"water":
			return [1]
		"fire":
			return [2]
		"air":
			return [3]
		"all":
			return [0, 1, 2, 3]
	return null


## The dictionary for AiBrain.configure / the legacy constructor.
func ai_config() -> Dictionary:
	var cfg := {"preset": ai_preset, "drill": ai_drill}
	var els: Variant = ai_elements()
	if els != null:
		cfg["elements"] = els
		if ai_sub >= 0 and (els as Array).size() == 1:
			cfg["subs"] = {int((els as Array)[0]): [ai_sub]}
	return cfg


## Legacy AiBrain numbers of a preset (used when AiBrain has no configure()).
static func legacy_cfg(preset: String) -> Dictionary:
	match preset:
		"novice":
			return {"aggression": 0.35, "counter": 0.3, "reaction": 0.45}
		"master":
			return {"aggression": 0.75, "counter": 0.9, "reaction": 0.2}
	return {"aggression": 0.55, "counter": 0.65, "reaction": 0.3}


func to_dict() -> Dictionary:
	return {"time_scale": time_scale, "frozen": frozen, "infinite": infinite, "god": god, "overlay": overlay, "ai_enabled": ai_enabled,
		"ai_preset": ai_preset, "ai_kit": ai_kit, "ai_sub": ai_sub, "ai_drill": ai_drill}


func from_dict(d: Dictionary) -> void:
	time_scale = clampf(float(d.get("time_scale", 1.0)), 0.1, 1.0)
	frozen = bool(d.get("frozen", false))
	infinite = bool(d.get("infinite", false))
	god = bool(d.get("god", false))
	overlay = bool(d.get("overlay", false))
	ai_enabled = bool(d.get("ai_enabled", true))
	ai_preset = String(d.get("ai_preset", "adept"))
	ai_kit = String(d.get("ai_kit", "mixed"))
	ai_sub = int(d.get("ai_sub", -1))
	ai_drill = String(d.get("ai_drill", ""))
