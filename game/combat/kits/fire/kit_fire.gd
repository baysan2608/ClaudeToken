class_name KitFire
extends RefCounted
## Fire kit entry point (Flame, Blue, Lightning, Combustion; docs/MOVESET.md §7.9-7.12, docs/kits/fire.md).
## Moves.ensure() calls register() once, after the core rules. The kit lives in:
##   fire_rules.gd        the counter cells of the Fire counter classes (§8.3), custom outcomes, statuses, tags
##   fire_util.gd         shared helpers (booked heat transfer, fire fields, detonations, def plumbing)
##   fire_flame.gd        sub 0  Flame       (legacy flare / blaze / thermal + Fire Column, Inferno, Fireball ...)
##   fire_blue.gd         sub 1  Blue        (Blue Needle ladder, Comet Flame, Blue Furrow, Corona, Aegis, Smelter ...)
##   fire_lightning.gd    sub 2  Lightning   (Spark -> Skybreak, Rail Arc, Ground Current, Static Ward ...)
##   fire_combustion.gd   sub 3  Combustion  (Pop -> Detonation, Spark Mine, Chain Blasts, Reactive Blast, Fuse ...)
## The legacy Fire moves (fire_attack, fire_tech, pour, lightning, vent) stay in combat/act_fire.gd (module "fire").
## Moves with module "kit_fire" run the generic Verbs lifecycle unless a stage is overridden with
## KitFire.handle(id, {start, after, phase, tick, interrupt}) (same signatures as the hooks below; for a guard
## spec inst.id == "guard" and inst.data.spec is the spec id).

const E := 2   # Sim.Element.FIRE

static var _handlers := {}


static func register() -> void:
	_handlers.clear()
	FireRules.register()
	FireFlame.register()
	FireBlue.register()
	FireLightning.register()
	FireCombustion.register()


## Move-specific lifecycle overrides for module "kit_fire" defs.
static func handle(id: String, spec: Dictionary) -> void:
	_handlers[id] = spec


static func _key(inst: ActionInst) -> String:
	if inst.id == "guard" and inst.data.has("spec"):
		return String(inst.data.spec)
	return inst.id


static func _h(inst: ActionInst, stage: String) -> Callable:
	var spec: Dictionary = _handlers.get(_key(inst), {})
	return spec.get(stage, Callable())


static func _known(inst: ActionInst) -> bool:
	return Moves.DEFS.has(_key(inst))


static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var c := _h(inst, "start")
	if c.is_valid():
		c.call(w, a, inst, it)
	elif _known(inst):
		Verbs.on_start(w, a, inst, it)


## Returns the phase that follows startup (ActionInst.P.*).
static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	var c := _h(inst, "after")
	if c.is_valid():
		return int(c.call(w, a, inst, it))
	if _known(inst):
		return Verbs.after_startup(w, a, inst, it)
	return ActionInst.P.ACTIVE


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	var c := _h(inst, "phase")
	if c.is_valid():
		c.call(w, a, inst, p)
	elif _known(inst):
		Verbs.on_phase(w, a, inst, p)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var c := _h(inst, "tick")
	if c.is_valid():
		c.call(w, a, inst, it)
	elif _known(inst):
		Verbs.on_tick(w, a, inst, it)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	var c := _h(inst, "interrupt")
	if c.is_valid():
		c.call(w, a, inst, reason)
	elif _known(inst):
		Verbs.on_interrupt(w, a, inst, reason)


## Every Fire move id the kit registers, by sub-element (docs, Lab move list, tests).
static func move_ids(sub: int) -> Array[String]:
	var out: Array[String] = []
	for id in Moves.REGISTERED:
		var d: Dictionary = Moves.DEFS.get(id, {})
		if int(d.get("element", -1)) == E and int(d.get("sub", -1)) == sub and String(d.get("module", "")) in ["kit_fire", "verbs"]:
			out.append(id)
	return out
