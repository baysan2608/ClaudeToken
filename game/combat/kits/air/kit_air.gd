class_name KitAir
extends RefCounted
## Air kit entry point (Gust, Vortex, Vacuum, Sound) - docs/MOVESET.md §7.13-§7.16, §8.4; docs/kits/air.md.
##
## Moves.ensure() calls register() once, after the core rules. The kit lives in:
##   air_gust.gd     sub 0  Gust    (legacy palm gust / updraft / dash + Gale, Crescent, Dust Devil, Crosswind, Wall of Wind ...)
##   air_vortex.gd   sub 1  Vortex  (Twister / Tornado, Spiral Lance, Eddy Ring, Vortex Wall, Unleash, Eye of the Storm ...)
##   air_vacuum.gd   sub 2  Vacuum  (Pressure Palm, Suction Line, Null Bubble, Vacuum Well ...)
##   air_sound.gd    sub 3  Sound   (Clap, Sound Lance, Tremor Hum, Echo Ring, Sound Barrier, Flight ...)
##   air_rules.gd           the Air column of the counter matrix (cells, outcomes, statuses, tag classes)
##   air_util.gd            shared helpers (zones, bodies, arena reflection, infusion, inrush)
## Moves with module "verbs" run entirely on the generic verbs (optionally with hook_* Callables in their def).
## Moves with module "kit_air" run the Verbs lifecycle unless a handler overrides a stage:
## KitAir.handle(id, {start, after, phase, tick, interrupt}) with Callables of the hook signatures below
## (inst.id is the move; for a guard spec inst.id == "guard" and inst.data.spec is the spec id).

const E := 3   # Sim.Element.AIR

static var _handlers := {}


static func register() -> void:
	_handlers.clear()
	AirRules.register()
	AirGust.register()
	AirVortex.register()
	AirVacuum.register()
	AirSound.register()


## Frames (60 Hz) to seconds.
static func f(n: float) -> float:
	return n / 60.0


## Registers a def for Air sub `sub` (module "kit_air" unless the def says otherwise) and binds it to its slot.
## Ids that must stay unbound (context moves like Wind Grip) pass slot "".
static func reg(id: String, sub: int, slot: String, d: Dictionary, bind: bool = true) -> void:
	var def := d.duplicate(true)
	def["element"] = E
	def["sub"] = sub
	def["slot"] = slot
	if not def.has("module"):
		def["module"] = "kit_air"
	Moves.register(id, def)
	if bind and slot != "":
		Moves.bind(E, sub, slot, id)


## Move-specific lifecycle overrides for module "kit_air" defs.
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
