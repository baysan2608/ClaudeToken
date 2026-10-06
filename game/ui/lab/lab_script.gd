class_name LabScript
extends RefCounted
## A scripted input sequence, one dictionary of InputFrame / ActorIntent fields per 60 Hz tick.
## The Lab's "Try" button, the combo trainer's demo and the spawner's "the rival throws it" all drive the
## real input path with it: the player's script is applied to the InputFrame before PlayerController,
## the rival's to its ActorIntent. Keys are field names the two share (attack_pressed, attack_held,
## attack_released, attack_gesture, guard_pressed, guard_held, guard_gesture, tech_pressed, tech_held,
## tech_released, evade_pressed, evade_held, element_select, sub_select).

const GESTURE_OF_SLOT := {"strike": 0, "thrust": 1, "ground": 2, "sweep": 3}
## Ticks between the element / sub switch and the first press (the switch affects the next action).
const LEAD := 2

var frames: Array[Dictionary] = []
var tick := 0
var label := ""


func is_done() -> bool:
	return tick >= frames.size()


func length() -> int:
	return frames.size()


## The fields to apply this tick ({} once finished); advances.
func next() -> Dictionary:
	if tick >= frames.size():
		return {}
	var d := frames[tick]
	tick += 1
	return d


func _at(t: int) -> Dictionary:
	while frames.size() <= t:
		frames.append({})
	return frames[t]


func _hold(from_t: int, to_t: int, key: String) -> void:
	for t in range(from_t, to_t + 1):
		_at(t)[key] = true


## Sets every field of `d` on `o` (an InputFrame or an ActorIntent); unknown fields are skipped.
static func apply_dict(o: Object, d: Dictionary) -> void:
	for k in d:
		if k in o:
			o.set(k, d[k])


## Hold time (s) that reaches charge tier `tier` of a move def (0 = a tap), with a little margin.
static func hold_seconds(def: Dictionary, tier: int) -> float:
	if tier <= 0:
		return 0.0
	var times := Charge.tier_times(def)
	return float(times[clampi(tier - 1, 0, 2)]) + 0.10


## The script that performs `element`/`sub`'s move in `slot` at charge `tier` through the input path.
static func for_move(element: int, sub: int, slot: String, tier: int = 0) -> LabScript:
	var s := LabScript.new()
	var id := Moves.resolve(element, sub, slot)
	var def: Dictionary = Moves.DEFS.get(id, {})
	s.label = "%s %s" % [String(def.get("name", id)).split(" / ")[0], ("T%d" % tier) if tier > 0 else ""]
	var first := s._at(0)
	first["element_select"] = element
	first["sub_select"] = sub
	var t0 := LEAD
	match slot:
		"strike", "thrust", "ground", "sweep":
			var hold_ticks := ceili(hold_seconds(def, tier) / Sim.DT) if tier > 0 else 4
			var f := s._at(t0)
			f["attack_pressed"] = true
			f["attack_gesture"] = GESTURE_OF_SLOT[slot]
			s._hold(t0, t0 + hold_ticks, "attack_held")
			s._at(t0 + hold_ticks + 1)["attack_released"] = true
		"guard":
			var gh := maxi(48, ceili(hold_seconds(def, tier) / Sim.DT) + 10)
			s._at(t0)["guard_pressed"] = true
			s._hold(t0, t0 + gh, "guard_held")
		"push", "sink":
			# The push / sink tier comes from how long the guard was held before the flick (KitEarth.guard_tier):
			# hold the guard for the tier's hold time, flick, then keep it held while the move plays.
			var gh2 := maxi(10, ceili(hold_seconds(def, tier) / Sim.DT)) if tier > 0 else 10
			s._at(t0)["guard_pressed"] = true
			s._hold(t0, t0 + gh2 + 32, "guard_held")
			s._at(t0 + gh2)["guard_gesture"] = Sim.Gesture.UP if slot == "push" else Sim.Gesture.DOWN
		"tech":
			var th := maxi(40, ceili(hold_seconds(def, tier) / Sim.DT) + 4)
			s._at(t0)["tech_pressed"] = true
			s._hold(t0, t0 + th, "tech_held")
			s._at(t0 + th + 1)["tech_released"] = true
		"evade":
			s._at(t0)["evade_pressed"] = true
		"evade_hold":
			s._at(t0)["evade_pressed"] = true
			s._hold(t0, t0 + 40, "evade_held")
	return s


## Total ticks the script needs for a move (tests).
static func ticks_for(element: int, sub: int, slot: String, tier: int = 0) -> int:
	return for_move(element, sub, slot, tier).length()
