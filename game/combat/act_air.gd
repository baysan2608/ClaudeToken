class_name ActAir
extends RefCounted
## Air: palm gust (tap) / cyclone push (hold), updraft + glide technique.
## Air bends trajectories and pushes light things; it is not a universal cancel:
## heavy stones barely move, lava waves are unaffected, fire is only dispersed by an air guard.

const LIGHT_MASS := 30.0
## Pressure of the palm gust / cyclone push (MOVESET §7.13, counter power vs threats).
const POWER := 7.0
const HEAVY_POWER := 11.0


static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	inst.data["face"] = w.aim_dir(a, it)
	match inst.id:
		"air_attack":
			if not w.spend_focus(a, float(inst.def.cost)):
				inst.data["fizzle"] = true
				w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		"air_tech":
			inst.data.erase("face")
			if a.grounded:
				if not w.spend_focus(a, float(inst.def.cost)):
					inst.data["fizzle"] = true
					w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "updraft"})
			else:
				inst.data["airborne_start"] = true
				inst.data["startup"] = 0.0


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	match inst.id:
		"air_attack":
			if inst.data.get("fizzle", false):
				return ActionInst.P.RECOVERY
			return w.attack_after_startup(a, inst, it)
		"air_tech":
			if inst.data.get("fizzle", false):
				return ActionInst.P.RECOVERY
			if not inst.data.get("airborne_start", false):
				a.vel.y = float(inst.def.lift_speed)
				a.grounded = false
				w.emit("updraft", {"actor": a.id})
			return ActionInst.P.CHANNEL
	return ActionInst.P.ACTIVE


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if inst.id == "air_attack" and p == ActionInst.P.ACTIVE:
		_push(w, a, inst)
	if inst.id == "air_tech" and p != ActionInst.P.CHANNEL:
		if a.gliding:
			w.emit("glide_end", {"actor": a.id})
		a.gliding = false


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.id:
		"air_attack":
			if not it.attack_held:
				inst.data["released"] = true
			inst.data["face"] = w.aim_dir(a, it)
			if inst.phase == ActionInst.P.CHARGE and inst.data.get("released", false) and inst.total >= float(inst.def.heavy_min):
				if w.spend_focus(a, float(inst.def.heavy_cost) - float(inst.def.cost)):
					w.set_phase(a, inst, ActionInst.P.ACTIVE)
				else:
					inst.heavy = false
					w.set_phase(a, inst, ActionInst.P.ACTIVE)
		"air_tech":
			if it.tech_cancel and inst.phase == ActionInst.P.STARTUP:
				# A cancel during the crouch never launches the updraft.
				w.emit("cancel", {"actor": a.id, "move": inst.id})
				w.finish_action(a, inst)
				return
			if inst.phase != ActionInst.P.CHANNEL:
				return
			if a.grounded and inst.t > 0.1:
				w.finish_action(a, inst)
				return
			if not it.tech_held or it.tech_cancel:
				if a.gliding:
					w.emit("glide_end", {"actor": a.id})
				a.gliding = false
				w.finish_action(a, inst)
				return
			if a.vel.y < 0.0 and a.has("glide") and not a.gliding:
				a.gliding = true
				w.emit("glide", {"actor": a.id})
			if a.gliding and not w.spend_focus(a, float(inst.def.glide_cost) * Sim.DT):
				a.gliding = false
				w.emit("insufficient", {"actor": a.id, "what": "focus", "move": "glide"})
				w.finish_action(a, inst)


static func on_interrupt(_w: CombatWorld, a: ActorState, _inst: ActionInst, _reason: String) -> void:
	a.gliding = false


static func _push(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var d := inst.def
	var heavy := inst.heavy
	var rng_m := float(d.heavy_range) if heavy else float(d.range)
	var cone := float(d.heavy_cone) if heavy else float(d.cone)
	var knock := float(d.heavy_knock) if heavy else float(d.knock)
	var dir: Vector3 = inst.data.face
	w.emit("gust", {"actor": a.id, "dir": dir, "range": rng_m, "heavy": heavy})
	# The gust is a pressure volume: a threat to fighters, a counter (class "gust") to the bodies it meets.
	var gust := Agent.of_volume(w, a, inst, &"gust", a.chest(), dir, {"P": float(Charge.param(inst, "power", HEAVY_POWER if heavy else POWER))})
	gust.data["knock"] = knock
	FxEvents.fx_for(w, a, inst, "cone", "wind", {"length": rng_m, "angle": cone, "power": gust.power})
	for t in w.actors_in_cone(a, dir, rng_m, cone):
		w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id,
			"damage": d.heavy_damage if heavy else d.damage, "balance": d.heavy_balance if heavy else d.balance,
			"knock": dir * knock + Vector3(0, 1.5, 0), "kind": "air", "from": a.chest(), "agent": gust})
	var cos_lim := cos(deg_to_rad(cone))
	for b in w.bodies:
		if not b.alive or b.controller >= 0 or b.static_body:
			continue
		var to := b.pos - a.chest()
		var dist := to.length()
		if dist > rng_m or (dist > 0.5 and Vector3(to.x, 0, to.z).normalized().dot(dir) < cos_lim):
			continue
		# Legacy cell (*, gust) T0-T1: light hostile shots turn and change owner, heavy ones bend,
		# loose light bodies are pushed, clouds disperse, waves/puddles/walls/pool stay (CoreRules._gust).
		Interactions.resolve(w, Agent.of_body(w, b, a), gust, {"site": "gust"})
