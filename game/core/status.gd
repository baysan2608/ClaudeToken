class_name Status
extends RefCounted
## Actor statuses (docs/MOVESET.md §15.8). ActorState.status: name -> {t, mag, src}; t < 0 = until removed.
## A spec declares modifiers and immunities:
##   speed (x move speed), recovery (x recovery time), friction (x accel/decel), armor (fraction of
##   K damage removed), tech_cost (x technique Focus), dps (health/s), rooted (no walking),
##   immune: [burn, conduct, lift, knockback, pull, ground], no_lock (can't lock on), hidden (can't be
##   locked from beyond 2 m), wet (counts as wet for lightning).
## Kits add statuses with Status.register(name, spec). Events: status {actor, status, on, t, mag}.

const CORE := {
	"wet": {"wet": true},
	"burning": {"dps": 3.0},
	"chilled": {"speed": 0.7, "recovery": 1.15},
	"frozen": {"speed": 0.0, "rooted": true, "recovery": 1.3},
	"rooted": {"speed": 0.0, "rooted": true},
	"slowed": {"speed": 0.6},
	"muddy": {"speed": 0.6, "friction": 1.3},
	"slick": {"friction": 0.12},
	"blinded": {"no_lock": true},
	"concealed": {"hidden": true},
	"deafened": {"tech_cost": 1.5},
	"shocked": {"speed": 0.8, "recovery": 1.2},
	"anchored": {"immune": ["knockback", "pull", "lift"]},
	"armored": {"armor": 0.4},
	"levitating": {"immune": ["ground"]},
	"charged": {"speed": 1.3, "recovery": 0.8},
}
const WET_AT := 0.3          # ActorState.wetness above this is the "wet" status (legacy x1.5 lightning)
const HIDDEN_RANGE := 2.0    # concealed actors can only be locked within this distance

static var SPECS := {}


static func ensure() -> void:
	if not SPECS.is_empty():
		return
	for k in CORE:
		SPECS[k] = CORE[k].duplicate(true)


static func register(nm: String, spec: Dictionary) -> void:
	ensure()
	SPECS[nm] = spec.duplicate(true)


static func spec(nm: String) -> Dictionary:
	return SPECS.get(nm, {})


## Applies (or refreshes to the longer duration / larger magnitude) a status. dur < 0 = until removed.
static func apply(w: CombatWorld, a: ActorState, nm: String, dur: float, mag: float = 1.0, src: int = -1) -> void:
	if nm == "wet":
		a.wetness = maxf(a.wetness, clampf(mag, WET_AT + 0.01, 1.0))
	if immune(a, "status:" + nm):
		return
	var cur: Dictionary = a.status.get(nm, {})
	if cur.is_empty():
		a.status[nm] = {"t": dur, "mag": mag, "src": src}
		FxEvents.status(w, a, nm, true, dur, mag)
		return
	if dur < 0.0 or (float(cur.t) >= 0.0 and dur > float(cur.t)):
		cur.t = dur
	cur.mag = maxf(float(cur.mag), mag)
	cur.src = src


static func remove(w: CombatWorld, a: ActorState, nm: String) -> void:
	if a.status.has(nm):
		a.status.erase(nm)
		FxEvents.status(w, a, nm, false, 0.0, 0.0)
	if nm == "wet":
		a.wetness = minf(a.wetness, WET_AT * 0.5)


static func has(a: ActorState, nm: String) -> bool:
	if nm == "wet":
		return a.wetness > WET_AT
	return a.status.has(nm)


## Per tick: timers, damage over time, the derived "wet" status (mirrors ActorState.wetness).
static func tick(w: CombatWorld, a: ActorState, dt: float) -> void:
	var wet_now := a.wetness > WET_AT
	if wet_now != a.status.has("wet"):
		if wet_now:
			a.status["wet"] = {"t": -1.0, "mag": a.wetness, "src": -1}
		else:
			a.status.erase("wet")
		FxEvents.status(w, a, "wet", wet_now, -1.0, a.wetness)
	if a.status.is_empty():
		return
	var ended: Array = []
	for nm in a.status:
		if nm == "wet":
			continue
		var s: Dictionary = a.status[nm]
		var sp: Dictionary = SPECS.get(nm, {})
		if sp.has("dps") and not immune(a, "burn" if nm == "burning" else "dot"):
			a.health = maxf(0.0, a.health - float(sp.dps) * float(s.mag) * dt)
		if float(s.t) >= 0.0:
			s.t = float(s.t) - dt
			if float(s.t) <= 0.0:
				ended.append(nm)
	for nm in ended:
		a.status.erase(nm)
		FxEvents.status(w, a, nm, false, 0.0, 0.0)


static func _mult(a: ActorState, key: String) -> float:
	var m := 1.0
	for nm in a.status:
		var sp: Dictionary = SPECS.get(nm, {})
		if sp.has(key):
			m *= float(sp[key])
	return m


static func speed_mult(a: ActorState) -> float:
	if a.status.is_empty():
		return 1.0
	return _mult(a, "speed")


static func recovery_mult(a: ActorState) -> float:
	if a.status.is_empty():
		return 1.0
	return _mult(a, "recovery")


static func friction_mult(a: ActorState) -> float:
	if a.status.is_empty():
		return 1.0
	return _mult(a, "friction")


static func tech_cost_mult(a: ActorState) -> float:
	if a.status.is_empty():
		return 1.0
	return _mult(a, "tech_cost")


static func rooted(a: ActorState) -> bool:
	for nm in a.status:
		if SPECS.get(nm, {}).get("rooted", false):
			return true
	return false


## Fraction of kinetic damage removed (statuses + ActorState.armor).
static func armor(a: ActorState) -> float:
	var r := a.armor
	for nm in a.status:
		var sp: Dictionary = SPECS.get(nm, {})
		if sp.has("armor"):
			r = maxf(r, float(sp.armor))
	return clampf(r, 0.0, 0.95)


## what: burn, conduct, lift, knockback, pull, ground, or "status:<name>".
static func immune(a: ActorState, what: String) -> bool:
	if a.anchored and (what == "knockback" or what == "pull" or what == "lift"):
		return true
	if a.flying and what == "ground":
		return true
	for nm in a.status:
		var sp: Dictionary = SPECS.get(nm, {})
		if (sp.get("immune", []) as Array).has(what):
			return true
	return false


static func lock_blocked(a: ActorState) -> bool:
	for nm in a.status:
		if SPECS.get(nm, {}).get("no_lock", false):
			return true
	return false


static func hidden(a: ActorState) -> bool:
	for nm in a.status:
		if SPECS.get(nm, {}).get("hidden", false):
			return true
	return false
