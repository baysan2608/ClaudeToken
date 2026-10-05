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
			x.sub_select = -1
			x.attack_gesture = 0
			x.guard_gesture = 0


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


# ---------------------------------------------------------------- moveset engine helpers

## Selects sub-element s of the actor's current element (affects the next action).
func sub(a: ActorState, s: int) -> void:
	it(a).sub_select = s


## A flick gesture (Sim.Gesture) on a button. "attack": with press=true a press + gesture on the same
## tick (desktop keys), press=false a gesture on a running attack (touch morph / charge release).
## "guard": a guard flick (push UP / sink DOWN) while guarding.
func flick(a: ActorState, button: String, gesture: int, press_too: bool = true) -> void:
	var i := it(a)
	match button:
		"attack":
			i.attack_gesture = gesture
			if press_too:
				i.attack_pressed = true
				i.attack_held = true
		"guard":
			i.guard_gesture = gesture
			i.guard_held = true


## Press, hold for `ticks`, release (attack / guard / tech / evade).
func hold(a: ActorState, button: String, ticks: int) -> void:
	press(a, button)
	if button == "evade":
		it(a).evade_held = true
	step(ticks)
	if button == "evade":
		it(a).evade_held = false
	else:
		release(a, button)


## Evade pressed and held for `ticks` (>= 12 morphs into evade_hold when bound).
func evade_hold(a: ActorState, ticks: int) -> void:
	hold(a, "evade", ticks)


## Spawns a body 6 m in front of `target` flying at its chest as an attack of `owner` (or neutral).
## mat: Sim.Mat or name. Returns the body.
func launch_at(target: ActorState, mat: Variant, mass: float, speed: float, temp: float = Sim.AMBIENT_C, tag: String = "",
		owner: ActorState = null, dist: float = 6.0) -> MatBody:
	var m: int = mat if mat is int else Sim.MAT_NAMES.find(String(mat))
	var from := target.chest() + target.forward() * dist
	var b := w.spawn_body(m, Sim.Form.CHUNK, mass, from, "test", temp)
	if m == Sim.Mat.STONE or m == Sim.Mat.SAND or m == Sim.Mat.GLASS:
		w.mass_ledger.ground_taken += mass
	b.tag = StringName(tag)
	if m == Sim.Mat.STONE and temp >= Sim.STONE_MELT_C:
		Thermal.heat(b, 0.0)
	b.vel = (target.chest() - from).normalized() * speed
	b.gravity_scale = 0.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = owner.id if owner != null else -1
	b.damage = 10.0
	b.balance_damage = 20.0
	if owner != null:
		b.hit_set[owner.id] = true
	return b


func spawn_zone(tag: String, pos: Vector3, radius: float, owner: ActorState = null, power: float = 0.0) -> MatBody:
	return w.spawn_zone(StringName(tag), pos, radius, owner.id if owner != null else -1, power)


## Pure prediction of a move (at a tier, perfect or not) answering a threat agent.
func predict(threat: Agent, move_id: String, tier: int = 0, perfect: bool = false, by: ActorState = null) -> Dictionary:
	return Interactions.predict(w, threat, Agent.of_move(w, by, move_id, tier, perfect))


## Saves / restores the static registries (moves, bindings, rules, hooks, statuses) around a test
## that registers test-local moves or rules.
var _saved := {}


func begin_scope() -> void:
	_saved = {"moves": Moves.save_state(), "rules": Interactions.save_state(), "status": Status.SPECS.duplicate(true),
		"ticks": CombatWorld._body_ticks.duplicate(), "zones": CombatWorld._zone_effects.duplicate(),
		"previews": CombatWorld._tech_previews.duplicate()}


func end_scope() -> void:
	if _saved.is_empty():
		return
	Moves.load_state(_saved.moves)
	Interactions.load_state(_saved.rules)
	Status.SPECS = _saved.status
	CombatWorld._body_ticks = _saved.ticks
	CombatWorld._zone_effects = _saved.zones
	CombatWorld._tech_previews = _saved.previews
	_saved = {}


## Inside a scope: drop every kit binding so only the legacy sub-0 bindings remain (engine tests that
## exercise fallbacks with test-local moves, independent of what the element kits bind).
func legacy_bindings_only() -> void:
	Moves.ensure()
	Moves.BINDINGS.clear()
	for e in Moves.LEGACY_BINDINGS:
		for slot in Moves.LEGACY_BINDINGS[e]:
			Moves.BINDINGS["%d/0/%s" % [e, slot]] = Moves.LEGACY_BINDINGS[e][slot]
