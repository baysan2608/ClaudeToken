class_name Moves
extends RefCounted
## Data-driven move definitions shared by player and AI. Times in seconds.
## startup = anticipation (input is acknowledged on the first frame; the strike
## lands at the end of startup), active = hit window, recovery = committed.
## cancel: phase fraction of RECOVERY after which guard/evade may cancel.
## anim / anim_active: clip names (game/assets/characters/fighter_clips.json);
## the view time-scales the clip so its "contact" lands at the end of startup.

const HOLD_THRESHOLD := 0.18     # attack held this long becomes a charge
const BUFFER_TIME := 0.15        # press buffer
const PERFECT_WINDOW := 0.18     # guard press this close before contact = perfect
const GUARD_MASH_LOCK := 0.35    # a new perfect window needs this gap since the last press

## Today's moves, values unchanged (tests pin them). Moves.DEFS is built from these plus kit
## registrations plus Lab live-tuning overrides.
const BASE_DEFS := {
	# ---------------------------------------------------------------- common
	"evade": {"module": "common", "startup": 0.0, "active": 0.30, "recovery": 0.12,
		"distance": 2.8, "iframes": 0.14, "cost": 4.0, "anim": "evade"},
	"air_dash": {"module": "common", "startup": 0.0, "active": 0.26, "recovery": 0.10,
		"distance": 4.6, "iframes": 0.18, "cost": 9.0, "anim": "air_dash"},
	"guard": {"module": "common", "startup": 0.0, "active": 0.0, "recovery": 0.10, "anim": "guard"},
	"vent": {"module": "fire", "startup": 0.12, "active": 0.25, "recovery": 0.2, "anim": "fire_release"},
	# ---------------------------------------------------------------- earth
	"earth_attack": {"module": "earth", "element": 0, "startup": 0.24, "active": 0.06, "recovery": 0.30,
		"heavy_min": 0.55, "cancel": 0.6, "cost": 7.0, "heavy_cost": 14.0,
		"mass": 20.0, "heavy_mass": 45.0, "speed": 17.0, "heavy_speed": 14.0,
		"damage": 12.0, "balance": 24.0, "heavy_damage": 20.0, "heavy_balance": 50.0,
		"anim": "earth_lift", "anim_active": "earth_throw", "anim_heavy": "earth_heavy"},
	"earth_tech": {"module": "earth", "element": 0, "startup": 0.12, "active": 0.06, "recovery": 0.28,
		"cancel": 0.5, "reach": 7.5, "rip_time": 0.28, "cost": 6.0, "speed": 18.0,
		"damage": 13.0, "balance": 26.0, "anim": "earth_hold", "anim_active": "earth_throw"},
	# ---------------------------------------------------------------- water
	"water_attack": {"module": "water", "element": 1, "startup": 0.16, "active": 0.12, "recovery": 0.28,
		"heavy_min": 0.45, "cancel": 0.6, "cost": 5.0, "heavy_cost": 10.0,
		"range": 4.6, "arc": 70.0, "damage": 8.0, "balance": 16.0, "knock": 2.5,
		"shard_mass": 4.0, "shard_speed": 24.0, "heavy_damage": 15.0, "heavy_balance": 26.0,
		"anim": "water_whip", "anim_heavy": "water_freeze"},
	"water_tech": {"module": "water", "element": 1, "startup": 0.15, "active": 0.08, "recovery": 0.3,
		"cancel": 0.5, "reach": 7.5, "draw_rate": 14.0, "max_draw": 12.0, "speed": 16.0,
		"damage": 10.0, "balance": 32.0, "cost": 6.0, "anim": "water_draw", "anim_hold": "water_hold", "anim_active": "water_whip"},
	# ---------------------------------------------------------------- fire
	"fire_attack": {"module": "fire", "element": 2, "startup": 0.10, "active": 0.08, "recovery": 0.22,
		"heavy_min": 0.40, "cancel": 0.55, "cost_hu": 60.0, "heavy_cost_hu": 160.0,
		"range": 4.6, "cone": 18.0, "heavy_range": 6.5, "heavy_cone": 26.0,
		"damage": 7.0, "balance": 10.0, "heavy_damage": 15.0, "heavy_balance": 30.0,
		"heat": 60.0, "heavy_heat": 160.0, "lightning_min": 0.65,
		"anim": "fire_jab", "anim_charge": "fire_charge", "anim_active": "fire_release"},
	"lightning": {"module": "fire", "element": 2, "startup": 0.0, "active": 0.12, "recovery": 0.40,
		"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4,
		"cost": 22.0, "anim": "lightning_release"},
	"fire_tech": {"module": "fire", "element": 2, "startup": 0.12, "draw_startup": 0.35, "active": 0.08, "recovery": 0.26,
		"cancel": 0.5, "reach": 5.0, "draw_range": 9.0, "heat_rate": 650.0, "draw_rate": 260.0,
		"grip": 0.9, "speed": 15.0, "damage": 13.0, "balance": 26.0,
		"anim": "magma_hold", "anim_draw": "heat_draw", "anim_active": "pour"},
	"pour": {"module": "fire", "element": 2, "startup": 0.25, "active": 0.05, "recovery": 0.35,
		"wave_speed": 7.5, "base_budget": 6.0, "budget_per_kg": 0.3, "damage": 18.0, "balance": 55.0,
		"anim": "pour"},
	# ---------------------------------------------------------------- air
	"air_attack": {"module": "air", "element": 3, "startup": 0.12, "active": 0.10, "recovery": 0.24,
		"heavy_min": 0.40, "cancel": 0.6, "cost": 5.0, "heavy_cost": 12.0,
		"range": 5.5, "cone": 35.0, "heavy_range": 7.0, "heavy_cone": 45.0,
		"knock": 7.0, "heavy_knock": 11.0, "damage": 3.0, "balance": 20.0,
		"heavy_damage": 6.0, "heavy_balance": 36.0, "anim": "air_push", "anim_heavy": "air_gust"},
	"air_tech": {"module": "air", "element": 3, "startup": 0.10, "active": 0.0, "recovery": 0.15,
		"lift_speed": 8.6, "glide_fall": 1.6, "glide_speed": 6.0, "cost": 15.0, "glide_cost": 6.0,
		"anim": "jump", "anim_hold": "glide"},
}

## Techniques that can be unlocked (mastery) and what they do.
const TECHNIQUES := {
	"magma": "Fire technique on a stone: seize it and melt it; release to pour a lava wave.",
	"heat_draw": "Fire technique on lava or hot rock: extract heat into your reserve.",
	"lightning": "Fire: hold attack to charge a lightning strike that follows conductors.",
	"redirect_current": "Fire guard timed against lightning sends it back.",
	"glide": "Air technique: keep holding after an updraft to glide.",
}


## Live move table: BASE_DEFS + kit registrations (register) + Lab overrides (set_override).
## `Moves.DEFS.pour` etc. keep working. Never assign to it; use the API below.
static var DEFS: Dictionary = BASE_DEFS.duplicate(true)

## Slot bindings "element/sub/slot" -> move id. Sub-0 bindings are the legacy ids.
static var BINDINGS := {}
## Ids registered by kits (in registration order).
static var REGISTERED: Array[String] = []
## Lab live tuning: id -> {key: original value} (only for overridden keys) and id -> {key: value}.
static var _orig := {}
static var _over := {}
static var _ensured := false

## Legacy (sub 0) bindings: today's moves exactly.
const LEGACY_BINDINGS := {
	0: {"strike": "earth_attack", "guard": "guard", "tech": "earth_tech", "evade": "evade"},
	1: {"strike": "water_attack", "guard": "guard", "tech": "water_tech", "evade": "evade"},
	2: {"strike": "fire_attack", "guard": "guard", "tech": "fire_tech", "evade": "evade"},
	3: {"strike": "air_attack", "guard": "guard", "tech": "air_tech", "evade": "air_dash"},
}
## Keys every def has after register() (docs/MOVESET.md §15.2 schema; kits may add more).
const DEF_DEFAULTS := {"module": "verbs", "startup": 0.0, "active": 0.0, "recovery": 0.0}


static func get_def(id: String) -> Dictionary:
	return DEFS[id]


## Idempotent: sets up the legacy bindings, the core interaction rules and every kit's
## registrations. CombatWorld._init calls it; tests and tools may call it any time.
static func ensure() -> void:
	if _ensured:
		return
	_ensured = true
	for e in LEGACY_BINDINGS:
		for slot in LEGACY_BINDINGS[e]:
			BINDINGS[_key(e, 0, slot)] = LEGACY_BINDINGS[e][slot]
	Interactions.ensure()
	Status.ensure()
	Verbs.ensure()
	KitEarth.register()
	KitWater.register()
	KitFire.register()
	KitAir.register()


static func _key(element: int, sub: int, slot: String) -> String:
	return "%d/%d/%s" % [element, sub, slot]


## Registers (or replaces) a move def. Missing schema keys get DEF_DEFAULTS; `name` defaults to the id.
## Registering an id of BASE_DEFS is refused (legacy moves are fixed; bind a new id instead).
static func register(id: String, def: Dictionary) -> void:
	if BASE_DEFS.has(id):
		push_error("Moves.register: '%s' is a legacy move and cannot be replaced" % id)
		return
	var d := def.duplicate(true)
	for k in DEF_DEFAULTS:
		if not d.has(k):
			d[k] = DEF_DEFAULTS[k]
	if not d.has("name"):
		d["name"] = id
	d["id"] = id
	if not REGISTERED.has(id):
		REGISTERED.append(id)
	DEFS[id] = d
	# Re-apply live overrides that target this id.
	for k in _over.get(id, {}):
		_orig[id][k] = d.get(k)
		d[k] = _over[id][k]


## Removes a registered (non-legacy) move and every binding to it.
static func unregister(id: String) -> void:
	if BASE_DEFS.has(id) or not DEFS.has(id):
		return
	DEFS.erase(id)
	REGISTERED.erase(id)
	for k in BINDINGS.keys():
		if BINDINGS[k] == id:
			BINDINGS.erase(k)


## Binds a move id to (element, sub, slot). Slots: Sim.SLOTS. Binding sub 0 replaces a legacy
## binding (only do that with a def that keeps the legacy behaviour; tests pin it).
static func bind(element: int, sub: int, slot: String, id: String) -> void:
	if not Sim.SLOTS.has(slot):
		push_error("Moves.bind: unknown slot '%s'" % slot)
		return
	BINDINGS[_key(element, sub, slot)] = id


static func unbind(element: int, sub: int, slot: String) -> void:
	BINDINGS.erase(_key(element, sub, slot))
	if sub == 0 and LEGACY_BINDINGS.get(element, {}).has(slot):
		BINDINGS[_key(element, 0, slot)] = LEGACY_BINDINGS[element][slot]


## Move id for (element, sub, slot): the exact binding, else the sub-0 binding, else the legacy id,
## else "" (nothing bound: callers fall back, e.g. an unbound thrust plays the strike).
static func resolve(element: int, sub: int, slot: String) -> String:
	var k := _key(element, sub, slot)
	if BINDINGS.has(k) and DEFS.has(BINDINGS[k]):
		return BINDINGS[k]
	var k0 := _key(element, 0, slot)
	if BINDINGS.has(k0) and DEFS.has(BINDINGS[k0]):
		return BINDINGS[k0]
	return String(LEGACY_BINDINGS.get(element, {}).get(slot, ""))


## Every move id bound for (element, sub) in slot order (unbound slots skipped; sub-0 fallbacks included).
static func list(element: int, sub: int) -> Array[String]:
	var out: Array[String] = []
	for slot in Sim.SLOTS:
		var id := resolve(element, sub, slot)
		if id != "" and not out.has(id):
			out.append(id)
	return out


## The slot an id is bound to for (element, sub), or "".
static func slot_of(element: int, sub: int, id: String) -> String:
	for slot in Sim.SLOTS:
		if resolve(element, sub, slot) == id:
			return slot
	return ""


## Lab live tuning: change one numeric (or any) field of a def. No effect until called.
static func set_override(id: String, key: String, value: Variant) -> void:
	if not DEFS.has(id):
		return
	var d: Dictionary = DEFS[id]
	if not _orig.has(id):
		_orig[id] = {}
		_over[id] = {}
	if not _orig[id].has(key):
		_orig[id][key] = d.get(key)
	_over[id][key] = value
	d[key] = value


## Restores every overridden field to its registered/base value.
static func clear_overrides() -> void:
	for id in _orig:
		if not DEFS.has(id):
			continue
		var d: Dictionary = DEFS[id]
		for key in _orig[id]:
			if _orig[id][key] == null:
				d.erase(key)
			else:
				d[key] = _orig[id][key]
	_orig.clear()
	_over.clear()


## Current overrides: {id: {key: value}}.
static func overrides() -> Dictionary:
	return _over.duplicate(true)


## Snapshot / restore of registrations, bindings and overrides (tests and Lab presets).
static func save_state() -> Dictionary:
	return {"defs": DEFS.duplicate(true), "bindings": BINDINGS.duplicate(), "registered": REGISTERED.duplicate(),
		"orig": _orig.duplicate(true), "over": _over.duplicate(true)}


static func load_state(st: Dictionary) -> void:
	DEFS = st.defs.duplicate(true)
	BINDINGS = st.bindings.duplicate()
	REGISTERED.assign(st.registered)
	_orig = st.orig.duplicate(true)
	_over = st.over.duplicate(true)
