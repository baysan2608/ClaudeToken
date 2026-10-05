class_name MoveListData
extends RefCounted
## Data behind the Lab "Move list" page (docs/MOVESET.md section 14): per element and sub-element, every move
## bound to a slot with its name, the input that plays it on the active device, Focus / heat / material costs,
## S/A/R frames (60 Hz), the charge-tier table and a short description. Read dynamically from the
## registry (Moves.resolve / Moves.DEFS), so moves added by kits appear automatically.

const DEVICES := ["touch", "keyboard", "gamepad"]

const INPUTS := {
	"touch": {"strike": "ATTACK tap (hold = charge)", "thrust": "ATTACK flick up", "ground": "ATTACK flick down",
		"sweep": "ATTACK flick left / right", "guard": "GUARD hold (just before contact = perfect)", "push": "GUARD flick up",
		"sink": "GUARD flick down", "tech": "TECH hold, drag, lift (ATTACK tap = shape)", "evade": "EVADE tap",
		"evade_hold": "EVADE hold"},
	"keyboard": {"strike": "J (hold = charge)", "thrust": "U", "ground": "N", "sweep": "H", "guard": "K (tap before contact = perfect)",
		"push": "K + J", "sink": "K + N", "tech": "L hold (J = shape)", "evade": "Space", "evade_hold": "Space hold"},
	"gamepad": {"strike": "X (hold = charge)", "thrust": "Y", "ground": "LT", "sweep": "B", "guard": "RB (tap before contact = perfect)",
		"push": "RB + X", "sink": "RB + LT", "tech": "RT hold (X = shape)", "evade": "A", "evade_hold": "A hold"},
}

## Tier keys worth showing first in the tier table.
const KEY_ORDER := ["count", "mass", "damage", "power", "speed", "radius", "range", "width", "budget", "pieces", "heat_add",
	"cost_add", "balance"]


static func input_text(slot: String, device: String) -> String:
	return String((INPUTS.get(device, INPUTS.keyboard) as Dictionary).get(slot, slot))


## "Focus 7 / heat 60 HU / water 2 kg" of a def ("-" when free).
static func cost_text(def: Dictionary) -> String:
	var bits: Array[String] = []
	if float(def.get("cost", 0.0)) > 0.0:
		bits.append("Focus %s" % _n(float(def.cost)))
	if float(def.get("heat", 0.0)) > 0.0:
		bits.append("heat %s HU" % _n(float(def.heat)))
	elif float(def.get("cost_hu", 0.0)) > 0.0:
		bits.append("heat %s HU" % _n(float(def.cost_hu)))
	if float(def.get("water", 0.0)) > 0.0:
		bits.append("water %s kg" % _n(float(def.water)))
	if float(def.get("metal", 0.0)) > 0.0:
		bits.append("metal %s kg" % _n(float(def.metal)))
	return " / ".join(bits) if not bits.is_empty() else "free"


## S / A / R in 60 Hz frames ("S12 A4 R18").
static func frames_text(def: Dictionary) -> String:
	return "S%d A%d R%d" % [roundi(float(def.get("startup", 0.0)) * Sim.HZ), roundi(float(def.get("active", 0.0)) * Sim.HZ),
		roundi(float(def.get("recovery", 0.0)) * Sim.HZ)]


static func _n(v: float) -> String:
	return "%d" % int(v) if absf(v - roundf(v)) < 0.05 else "%.1f" % v


## The charge-tier table: [{tier, hold, name, text}] for T1..T3 (empty for moves that do not charge).
static func tier_rows(def: Dictionary) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var mt := Charge.max_tier(def)
	if mt <= 0:
		return out
	var times := Charge.tier_times(def)
	var names := String(def.get("name", "")).split(" / ")
	var tiers: Dictionary = def.get("tiers", {})
	for t in range(1, mt + 1):
		var td: Dictionary = tiers.get("t%d" % t, {})
		var bits: Array[String] = []
		for k in KEY_ORDER:
			if td.has(k) and (td[k] is float or td[k] is int):
				bits.append("%s %s" % [String(k).replace("_", " "), _n(float(td[k]))])
		for k in td:
			if bits.size() >= 5:
				break
			if not KEY_ORDER.has(k) and (td[k] is float or td[k] is int):
				bits.append("%s %s" % [String(k).replace("_", " "), _n(float(td[k]))])
			elif not KEY_ORDER.has(k) and td[k] is bool and td[k]:
				bits.append(String(k).replace("_", " "))
		var cp := Charge.counter_power(def, t)
		if cp >= 0.0:
			bits.append("counter %s" % _n(cp))
		out.append({"tier": t, "hold": float(times[t - 1]), "name": names[t] if names.size() > t else "", "text": ", ".join(bits)})
	return out


## One row per bound slot of (element, sub), in slot order. device: touch | keyboard | gamepad.
static func rows(element: int, sub: int, device: String) -> Array[Dictionary]:
	Moves.ensure()
	var out: Array[Dictionary] = []
	for slot in Sim.SLOTS:
		var id := Moves.resolve(element, sub, slot)
		if id == "" or not Moves.DEFS.has(id):
			continue
		var def: Dictionary = Moves.DEFS[id]
		var names := String(def.get("name", id)).split(" / ")
		var counter: Dictionary = def.get("counter", {})
		var ctext := ""
		if counter.has("cls"):
			var p: Variant = counter.get("power")
			ctext = "counters as %s" % String(counter.cls)
			if p is Array and not (p as Array).is_empty():
				var ps: Array[String] = []
				for x in p:
					ps.append(_n(float(x)))
				ctext += " (power T0-T3: %s)" % ", ".join(ps)
			elif p != null:
				ctext += " (power %s)" % _n(float(p))
		out.append({"slot": slot, "id": id, "name": names[0], "tier_names": names, "desc": String(def.get("desc", "")),
			"input": input_text(slot, device), "cost": cost_text(def), "frames": frames_text(def),
			"tiers": tier_rows(def), "counter": ctext, "max_tier": Charge.max_tier(def), "role": String((def.get("ai", {}) as Dictionary).get("role", ""))})
	return out


## Every sub-element's row count (tests: the list covers all 16).
static func count_all(device: String = "keyboard") -> int:
	var n := 0
	for e in 4:
		for s in 4:
			n += rows(e, s, device).size()
	return n
