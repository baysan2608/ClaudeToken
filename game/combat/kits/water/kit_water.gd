class_name KitWater
extends RefCounted
## Water kit entry point (Water, Ice, Mist, Plant). Moves.ensure() calls register() once, after the core
## rules. The kit lives in:
##   water_water.gd  sub 0  Water   (legacy lash / lance / draw-shape / shield + Torrent, Maelstrom, Bullet, Tidal Rush ...)
##   water_ice.gd    sub 1  Ice     (Frost Shard, Rime Path, Ice Wall, Skate ...)
##   water_mist.gd   sub 2  Mist    (Scald Puff, Creeping Fog, Steam Screen, Vapor Draw ...)
##   water_plant.gd  sub 3  Plant   (Bramble Lash, Root Snare, Living Lattice, Vinegrip ...)
##   water_rules.gd         the counter cells of the Water counter classes, statuses, body/zone hooks
##   water_util.gd          shared helpers (water sources, freezing, steam, evades, zones)
## Moves with module "verbs" run entirely on the generic verbs (optionally with hook_* Callables in their
## def). Moves with module "kit_water" run the Verbs lifecycle unless a handler overrides a stage:
## KitWater.handle(id, {start, after, phase, tick, interrupt}) with Callables of the same signatures as
## the hooks below (inst.id is the move; for a guard spec inst.id == "guard" and inst.data.spec is the spec id).

static var _handlers := {}


static func register() -> void:
	_handlers.clear()
	WaterRules.register()
	WaterWater.register()
	WaterIce.register()
	WaterMist.register()
	WaterPlant.register()


## Move-specific lifecycle overrides for module "kit_water" defs.
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
