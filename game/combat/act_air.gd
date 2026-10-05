class_name ActAir
extends RefCounted
## Air sub 0 (Gust): palm gust (tap) / cyclone push (hold) / Gale / Hurricane Palm (longer holds), updraft + glide technique.
## Air bends trajectories and pushes light things; it is not a universal cancel:
## heavy stones barely move, lava waves are unaffected until a T3 Hurricane Palm, fire is only dispersed by an air guard.
## T0 / T1 are the legacy numbers exactly; T2 / T3 read the tier data AirGust adds to the live def. The technique is
## context-sensitive: a light body in the aim cone -> Wind Grip (morphs into gust_grip), otherwise the legacy updraft.

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
			if a.grounded and _wind_grip_context(w, a, inst, it):
				return
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
				var tier := maxi(1, inst.tier())
				if w.spend_focus(a, float(inst.def.heavy_cost) - float(inst.def.cost)):
					# Longer holds (Gale T2 / Hurricane Palm T3) pay their own surcharge; short of Focus they fire a tier lower.
					while tier >= 2 and not w.spend_focus(a, float(Charge.pget(inst.def, tier, "cost_add", 0.0))):
						tier -= 1
					inst.data["tier"] = tier
					w.set_phase(a, inst, ActionInst.P.ACTIVE)
				else:
					inst.heavy = false
					inst.data["tier"] = 0
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


## Context technique: a light body in the aim cone within 9 m -> Wind Grip (the action morphs into gust_grip, the
## Focus for it is paid there). False = the legacy updraft.
static func _wind_grip_context(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> bool:
	if not Moves.DEFS.has("gust_grip") or a.focus < float(Moves.DEFS.gust_grip.get("cost", 6.0)):
		return false
	if AirGust.grip_target(w, a, w.aim_dir(a, it)) == null:
		return false
	return w.morph_action(a, "gust_grip", "tech", it) != null


static func _push(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var d := inst.def
	var heavy := inst.heavy
	var tier := inst.tier()
	var strong := tier >= 2                      # Gale / Hurricane Palm: tier data from AirGust
	var rng_m := float(Charge.param(inst, "range", 8.0)) if strong else (float(d.heavy_range) if heavy else float(d.range))
	var cone := float(Charge.param(inst, "cone", 50.0)) if strong else (float(d.heavy_cone) if heavy else float(d.cone))
	var knock := float(Charge.param(inst, "knock", 14.0)) if strong else (float(d.heavy_knock) if heavy else float(d.knock))
	var dmg := float(Charge.param(inst, "damage", 9.0)) if strong else (float(d.heavy_damage) if heavy else float(d.damage))
	var bal := float(Charge.param(inst, "balance", 44.0)) if strong else (float(d.heavy_balance) if heavy else float(d.balance))
	var dir: Vector3 = inst.data.face
	w.emit("gust", {"actor": a.id, "dir": dir, "range": rng_m, "heavy": heavy, "tier": tier})
	# The gust is a pressure volume: a threat to fighters, a counter (class "gust") to the bodies it meets.
	var gust := Agent.of_volume(w, a, inst, &"gust", a.chest(), dir, {"P": float(Charge.param(inst, "power", HEAVY_POWER if heavy else POWER))})
	gust.data["knock"] = knock
	FxEvents.fx_for(w, a, inst, "cone", "wind", {"length": rng_m, "angle": cone, "power": gust.power})
	for t in w.actors_in_cone(a, dir, rng_m, cone):
		# Fighters held up by wind (flight) are the easiest to blow around: x1.5 balance.
		var b_dmg := bal * (1.5 if Status.has(t, "flight") else 1.0)
		w.hit_actor(t, {"attacker": a.id, "attack_id": inst.attack_id,
			"damage": dmg, "balance": b_dmg,
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
		# T2-T3 cells (lava rule, fire bands, light solids ...) are the Air kit's (AirRules).
		Interactions.resolve(w, Agent.of_body(w, b, a), gust, {"site": "gust"}, Interactions.PASS_RULE if strong else Interactions.DEFAULT_RULE)
