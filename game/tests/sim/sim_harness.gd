class_name SimHarness
extends RefCounted
## Drives a CombatWorld with scripted intents for tests and recorded scenarios.

var w: CombatWorld
var intents := {}
var log: Array[Dictionary] = []


func _init(seed_value: int = 1) -> void:
	w = CombatWorld.new(seed_value)


func actor(nm: String, p: Vector3, team: int, kit: Dictionary, element: int) -> ActorState:
	var a := w.add_actor(nm, p, team, kit, element)
	intents[a.id] = ActorIntent.new()
	return a


func it(a: ActorState) -> ActorIntent:
	return intents[a.id]


func press(a: ActorState, what: String) -> void:
	var i := it(a)
	match what:
		"attack":
			i.attack_pressed = true
			i.attack_held = true
		"guard":
			i.guard_pressed = true
			i.guard_held = true
		"tech":
			i.tech_pressed = true
			i.tech_held = true
		"evade":
			i.evade_pressed = true


func release(a: ActorState, what: String) -> void:
	var i := it(a)
	match what:
		"attack":
			i.attack_held = false
			i.attack_released = true
		"guard":
			i.guard_held = false
		"tech":
			i.tech_held = false
			i.tech_released = true


func cancel_tech(a: ActorState) -> void:
	var i := it(a)
	i.tech_cancel = true
	i.tech_held = false


func aim(a: ActorState, dir: Vector3) -> void:
	var i := it(a)
	i.aim_dir = dir.normalized()
	i.aim_active = dir.length() > 0.0


func element(a: ActorState, e: int) -> void:
	it(a).element_select = e


func step(n: int = 1) -> void:
	for k in n:
		w.step(intents)
		log.append_array(w.take_events())
		for i in intents.values():
			var x: ActorIntent = i
			x.attack_pressed = false
			x.attack_released = false
			x.guard_pressed = false
			x.evade_pressed = false
			x.tech_pressed = false
			x.tech_released = false
			x.tech_cancel = false
			x.element_select = -1
			x.target_cycle = false


## Steps until cond returns true (checked after each tick). Returns ticks used or -1.
func until(cond: Callable, max_ticks: int = 600) -> int:
	for k in max_ticks:
		step()
		if cond.call():
			return k + 1
	return -1


func events(type: String) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for e in log:
		if e.type == type:
			out.append(e)
	return out


func last_event(type: String) -> Dictionary:
	for k in range(log.size() - 1, -1, -1):
		if log[k].type == type:
			return log[k]
	return {}


func has_event(type: String, key: String = "", value: Variant = null) -> bool:
	for e in log:
		if e.type == type and (key == "" or e.get(key) == value):
			return true
	return false
