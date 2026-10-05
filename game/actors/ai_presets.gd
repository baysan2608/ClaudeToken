class_name AiPresets
extends RefCounted
## Difficulty presets of the sparring AI (docs/MOVESET.md §13, docs/AI.md).
##   reaction    s before a perceived threat can be acted on (± jitter)
##   counter     chance to take the best-scoring counter (else a random feasible one / evade)
##   misjudge    ± fraction of threat power misread (perceived TP = TP × (1 + U(-m, m)), one draw per threat)
##   timing_err  ± s error on perfect-window presses (uniform)
##   elements / subs   kit breadth when configure() is not given explicit elements / subs
##   aggression  offense cadence (legacy meaning: interval lerps 3.2 s -> 1.1 s)
##   chain       longest attack string it continues after contact (1 = single moves)
##   weave       may switch element / sub inside a chain window (+6 Focus)
##   punish      reads recoveries and charges (interrupts, Disrupt)
##   env         reads the environment (wet / pool / plate -> lightning, walls -> push)
##   tiers       highest charge tier it holds for on offense
##   kit         technique flags granted when unlock is on

const TABLE := {
	"novice": {"reaction": 0.45, "counter": 0.3, "misjudge": 0.40, "timing_err": 0.12, "elements": 1, "subs": 2,
		"aggression": 0.35, "chain": 1, "weave": false, "punish": false, "env": false, "tiers": 1,
		"kit": {"heat_draw": true}},
	"adept": {"reaction": 0.30, "counter": 0.65, "misjudge": 0.20, "timing_err": 0.07, "elements": 2, "subs": 4,
		"aggression": 0.55, "chain": 2, "weave": false, "punish": false, "env": true, "tiers": 2,
		"kit": {"heat_draw": true, "magma": true, "lightning": true}},
	"master": {"reaction": 0.20, "counter": 0.9, "misjudge": 0.08, "timing_err": 0.03, "elements": 4, "subs": 4,
		"aggression": 0.75, "chain": 3, "weave": true, "punish": true, "env": true, "tiers": 3,
		"kit": {"heat_draw": true, "magma": true, "lightning": true, "redirect_current": true, "glide": true}},
}

const ELEMENT_KEYS := {"earth": 0, "water": 1, "fire": 2, "air": 3}


static func names() -> Array:
	return ["novice", "adept", "master"]


static func get_preset(name: String) -> Dictionary:
	return (TABLE.get(name, TABLE.adept) as Dictionary).duplicate(true)


## "element:<e>/<sub>" drill spec -> [element, sub] (numbers or names: "fire/blue", "2/1"); [-1, -1] if invalid.
static func parse_element_drill(drill: String) -> Array:
	if not drill.begins_with("element:"):
		return [-1, -1]
	var parts := drill.substr(8).split("/")
	var e := _element_index(parts[0])
	if e < 0:
		return [-1, -1]
	var s := 0
	if parts.size() > 1:
		s = _sub_index(e, parts[1])
	if s < 0:
		return [-1, -1]
	return [e, s]


static func _element_index(t: String) -> int:
	var k := t.strip_edges().to_lower()
	if k.is_valid_int():
		var i := int(k)
		return i if i >= 0 and i < 4 else -1
	return int(ELEMENT_KEYS.get(k, -1))


static func _sub_index(e: int, t: String) -> int:
	var k := t.strip_edges().to_lower()
	if k.is_valid_int():
		var i := int(k)
		return i if i >= 0 and i < 4 else -1
	for s in 4:
		if String(Sim.SUB_NAMES[e][s]).to_lower() == k:
			return s
	return -1
