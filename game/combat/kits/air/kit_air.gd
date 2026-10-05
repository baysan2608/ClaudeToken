class_name KitAir
extends RefCounted
## Air kit (Gust, Vortex, Vacuum, Sound). Stub created by the core engine; the Air stream owns this file.
##
## register() is called once by Moves.ensure() (after the core rules). Use it to:
##   Moves.register(id, def)                         # def schema: docs/COMBAT_SPEC.md "Engine"
##   Moves.bind(Sim.Element.AIR, sub, slot, id)       # slots: Sim.SLOTS (sub 0 = legacy kit)
##   Interactions.add_rule(threat_cls, counter_cls, rule)   # the cells of YOUR counter classes
##   CombatWorld.register_body_tick(tag, cb) / register_zone_effect(tag, cb)
##   Status.register(name, spec) / CombatWorld.register_tech_preview(element, sub, cb)
## Moves whose def has module "verbs" run entirely on the generic verbs (no code here).
## Moves with module "kit_air" are dispatched to the hooks below (inst.id tells which move;
## for a guard spec inst.id == "guard" and inst.data.spec is your spec id). Hooks can call the
## Verbs helpers (Verbs.on_start(w, a, inst, it) etc.) to reuse the generic lifecycle.


static func register() -> void:
	pass


static func on_start(_w: CombatWorld, _a: ActorState, _inst: ActionInst, _it: ActorIntent) -> void:
	pass


## Returns the phase that follows startup (ActionInst.P.*).
static func after_startup(_w: CombatWorld, _a: ActorState, _inst: ActionInst, _it: ActorIntent) -> int:
	return ActionInst.P.ACTIVE


static func on_phase(_w: CombatWorld, _a: ActorState, _inst: ActionInst, _p: int) -> void:
	pass


static func on_tick(_w: CombatWorld, _a: ActorState, _inst: ActionInst, _it: ActorIntent) -> void:
	pass


static func on_interrupt(_w: CombatWorld, _a: ActorState, _inst: ActionInst, _reason: String) -> void:
	pass
