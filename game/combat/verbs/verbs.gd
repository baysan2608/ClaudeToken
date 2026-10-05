class_name Verbs
extends RefCounted
## Generic move templates (docs/MOVESET.md §15.7; COMBAT_SPEC "Engine" §E6). A def with
## module "verbs" and a `verb` key runs entirely on data:
##   projectile cone beam ground_line barrier zone grip ranged_heat burst summon dash mode stance
## Every tier parameter is read with Charge.param (def.tiers ladder), every cost goes through
## Verbs.pay (spend_focus / pay_heat / waterskin / satchel), every verb emits FxEvents fx cues.
## Kits reuse the pieces from their own modules: VerbProjectile.fire, VerbVolume.cone / beam / burst_at,
## VerbGroundLine.launch, VerbBarrier.*, VerbZone.spawn, VerbGrip.*, VerbHeat.*, VerbMotion.*.
## Per-move code hooks (Callables in registered defs): hook_execute(w, a, inst) -> bool (true =
## handled), hook_tick(w, a, inst, it), hook_impact(w, body, what) -> bool.

const CHANNEL_VERBS := ["grip", "summon", "ranged_heat", "mode"]
const DEFAULT_MAT := ["stone", "water", "flame", "wind"]

static var _ready := false


## Registers the core body ticks the verbs need (fuse zones). Called by Moves.ensure().
static func ensure() -> void:
	if _ready:
		return
	_ready = true
	CombatWorld.register_body_tick(&"fuse", Callable(VerbVolume, "fuse_tick"))


# ------------------------------------------------------------------ lifecycle (module "verbs")

static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.id == "guard":
		VerbBarrier.start(w, a, inst, it)
		return
	var d := inst.def
	var verb := String(d.get("verb", ""))
	inst.data["face"] = w.aim_dir(a, it)
	inst.data["aim"] = w.aim_dir(a, it)
	inst.data["aim_active"] = it.aim_active
	inst.data["aim_point"] = w.aim_point(a, it)
	if not pay(w, a, inst, "start"):
		inst.data["fizzle"] = true
		return
	fx(w, a, inst, "cast")
	match verb:
		"dash":
			VerbMotion.dash_start(w, a, inst, it)
		"stance":
			VerbMotion.stance_start(w, a, inst)


static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	if inst.id == "guard":
		return ActionInst.P.CHANNEL
	if inst.data.get("fizzle", false):
		return ActionInst.P.RECOVERY
	var d := inst.def
	var verb := String(d.get("verb", ""))
	if CHANNEL_VERBS.has(verb):
		match verb:
			"summon":
				VerbZone.summon_start(w, a, inst, it)
			"ranged_heat":
				VerbHeat.start(w, a, inst, it)
			"mode":
				VerbMotion.mode_start(w, a, inst)
		return ActionInst.P.CHANNEL if a.action == inst and not inst.data.get("fizzle", false) else ActionInst.P.RECOVERY
	if verb == "stance":
		return ActionInst.P.CHANNEL if bool(d.get("held", true)) else ActionInst.P.ACTIVE
	if verb == "dash":
		return ActionInst.P.ACTIVE
	if inst.data.get("morph_release", false):
		return ActionInst.P.ACTIVE
	if Sim.ATTACK_SLOTS.has(inst.slot) and Charge.max_tier(d) > 0 and not inst.data.get("released", false):
		return w.attack_after_startup(a, inst, it)
	return ActionInst.P.ACTIVE


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if inst.id == "guard":
		if p == ActionInst.P.RECOVERY:
			VerbBarrier.end(w, a, inst, "release")
		return
	if p == ActionInst.P.ACTIVE:
		execute(w, a, inst)
	elif p == ActionInst.P.RECOVERY:
		w.ledger.spent += take_heat(inst)   # paid heat that never reached a body is spent into the air
		match String(inst.def.get("verb", "")):
			"mode":
				VerbMotion.mode_end(w, a, inst)
			"stance":
				VerbMotion.stance_end(w, a, inst)
			"summon":
				VerbZone.summon_release(w, a, inst)
			"ranged_heat":
				VerbHeat.end(w, a, inst)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.id == "guard":
		VerbBarrier.tick(w, a, inst, it)
		return
	var d := inst.def
	var hook: Variant = d.get("hook_tick")
	if hook is Callable and (hook as Callable).is_valid():
		(hook as Callable).call(w, a, inst, it)
		if a.action != inst:
			return
	if Sim.ATTACK_SLOTS.has(inst.slot):
		if not it.attack_held:
			inst.data["released"] = true
		if inst.phase != ActionInst.P.ACTIVE:
			inst.data["face"] = w.aim_dir(a, it)
			inst.data["aim"] = inst.data.face
			inst.data["aim_point"] = w.aim_point(a, it)
	match inst.phase:
		ActionInst.P.CHARGE:
			var t1 := float(Charge.tier_times(d)[0])
			if inst.data.get("released", false) and inst.total >= t1 - 1e-6:
				inst.data["tier"] = maxi(1, inst.tier())
				if not pay(w, a, inst, "release"):
					inst.data["tier"] = 0
					w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id, "reason": "tier"})
				inst.heavy = inst.tier() > 0
				w.set_phase(a, inst, ActionInst.P.ACTIVE)
		ActionInst.P.CHANNEL:
			match String(d.get("verb", "")):
				"grip":
					VerbGrip.tick(w, a, inst, it)
				"summon":
					VerbZone.summon_tick(w, a, inst, it)
				"ranged_heat":
					VerbHeat.tick(w, a, inst, it)
				"mode":
					VerbMotion.mode_tick(w, a, inst, it)
				"stance":
					VerbMotion.stance_tick(w, a, inst, it)
		ActionInst.P.ACTIVE:
			match String(d.get("verb", "")):
				"dash":
					VerbMotion.dash_tick(w, a, inst)
				"beam":
					VerbVolume.beam_tick(w, a, inst)
				"stance":
					VerbMotion.stance_tick(w, a, inst, it)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	if reason != "morph":
		w.ledger.spent += take_heat(inst)
	if inst.id == "guard":
		VerbBarrier.end(w, a, inst, reason)
		return
	match String(inst.def.get("verb", "")):
		"grip":
			VerbGrip.drop(w, a, inst)
		"mode":
			VerbMotion.mode_end(w, a, inst)
		"stance":
			VerbMotion.stance_end(w, a, inst)
		"summon":
			VerbZone.summon_release(w, a, inst)
		"ranged_heat":
			VerbHeat.end(w, a, inst)
		"dash":
			inst.data["controls_motion"] = false


## The verb's release (ACTIVE entry). Returns nothing; spawned bodies are in inst.data.bodies.
static func execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var d := inst.def
	var hook: Variant = d.get("hook_execute")
	if hook is Callable and (hook as Callable).is_valid() and bool((hook as Callable).call(w, a, inst)):
		return
	match String(d.get("verb", "")):
		"projectile":
			VerbProjectile.fire(w, a, inst)
		"cone":
			VerbVolume.cone(w, a, inst)
		"beam":
			VerbVolume.beam(w, a, inst)
		"burst":
			VerbVolume.burst(w, a, inst)
		"ground_line":
			VerbGroundLine.launch(w, a, inst)
		"zone":
			VerbZone.spawn(w, a, inst)
		"barrier":
			VerbBarrier.raise_free(w, a, inst)


# ------------------------------------------------------------------ costs

## Pays the def's costs for a stage: "start" (cost Focus, heat HU, water kg, metal kg), "release"
## (tier surcharge: tier param cost_add, else heavy_cost - cost at T1+), or any dictionary of
## {focus, heat, water, metal}. Morph credits (paid_focus / paid_hu) are used first. Returns false
## (nothing paid) when a resource is short.
static func pay(w: CombatWorld, a: ActorState, inst: ActionInst, stage: Variant = "start") -> bool:
	var need := {}
	var d := Charge.pdef(inst)
	if stage is Dictionary:
		need = stage
	elif String(stage) == "start":
		need = {"focus": float(Charge.pget(d, 0, "cost", 0.0)), "heat": float(Charge.pget(d, 0, "heat", 0.0)),
			"water": float(Charge.pget(d, 0, "water", 0.0)), "metal": float(Charge.pget(d, 0, "metal", 0.0))}
	else:
		var tier := inst.tier()
		var add := 0.0
		if tier >= 1:
			add = float(Charge.param(inst, "cost_add", maxf(0.0, float(d.get("heavy_cost", d.get("cost", 0.0))) - float(d.get("cost", 0.0)))))
		need = {"focus": add, "heat": float(Charge.param(inst, "heat_add", 0.0))}
	var focus := float(need.get("focus", 0.0))
	if inst.slot == "tech":
		focus *= Status.tech_cost_mult(a)
	var credit := float(inst.data.get("paid_focus", 0.0))
	var use := minf(credit, focus)
	focus -= use
	var hu := float(need.get("heat", 0.0))
	var credit_h := float(inst.data.get("paid_hu", 0.0))
	var use_h := minf(credit_h, hu)
	hu -= use_h
	var water := float(need.get("water", 0.0))
	var metal := float(need.get("metal", 0.0))
	if a.focus + 1e-6 < focus or not w.can_pay_heat(a, hu + focus * Sim.HU_PER_FOCUS) and hu > 0.0:
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		return false
	if water > 0.0 and a.water_carried + 1e-6 < water and not a.in_water:
		w.emit("insufficient", {"actor": a.id, "what": "water", "move": inst.id})
		return false
	if metal > 0.0 and a.metal_carried + 1e-6 < metal:
		w.emit("insufficient", {"actor": a.id, "what": "metal", "move": inst.id})
		return false
	inst.data["paid_focus"] = credit - use
	inst.data["paid_hu"] = credit_h - use_h
	w.spend_focus(a, focus)
	if hu > 0.0:
		# Morph credit (paid_hu) is heat already carried in heat_paid; only new heat is added.
		var paid := w.pay_heat(a, hu, false)
		inst.data["heat_paid"] = float(inst.data.get("heat_paid", 0.0)) + paid
	if water > 0.0 and not a.in_water:
		a.water_carried -= water
		inst.data["water_paid"] = float(inst.data.get("water_paid", 0.0)) + water
	elif water > 0.0:
		_take_pool(w, a, water)
		inst.data["water_paid"] = float(inst.data.get("water_paid", 0.0)) + water
	if metal > 0.0:
		a.metal_carried -= metal
		inst.data["metal_paid"] = float(inst.data.get("metal_paid", 0.0)) + metal
	return true


## Water taken straight from the pool while standing in it (the waterskin stays full).
static func _take_pool(w: CombatWorld, a: ActorState, kg: float) -> void:
	var take := minf(kg, w.pool.mass)
	w.ledger.removed += take * (Sim.WATER_C * (w.pool.temp - Sim.AMBIENT_C) - Sim.WATER_LATENT_FUSION * (1.0 - w.pool.liquid))
	w.pool.mass -= take
	a.water_carried += take


## Heat paid by the action so far (HU) and not yet spent into a body or volume.
static func take_heat(inst: ActionInst) -> float:
	var hu := float(inst.data.get("heat_paid", 0.0))
	inst.data["heat_paid"] = 0.0
	return hu


# ------------------------------------------------------------------ helpers

static func mat_id(name: Variant) -> int:
	if name is int:
		return name
	var i := Sim.MAT_NAMES.find(String(name))
	return i if i >= 0 else Sim.Mat.STONE


## fx `mat` key for the action (def.fx.mat, else the element's default).
static func fx_mat(inst: ActionInst) -> String:
	var f: Dictionary = Charge.pdef(inst).get("fx", {})
	if f.has("mat"):
		return String(f.mat)
	return DEFAULT_MAT[clampi(inst.element, 0, 3)]


## Emits an fx cue for the action: key may be remapped by def.fx[key] (e.g. fx.release = "erupt").
static func fx(w: CombatWorld, a: ActorState, inst: ActionInst, key: String, extra: Dictionary = {}) -> void:
	var f: Dictionary = Charge.pdef(inst).get("fx", {})
	var fx_key := String(f.get(key, key))
	if fx_key == "" or not FxEvents.is_known("fx", fx_key):
		return
	var d := {"shape": String(f.get("shape", ""))}
	d.merge(extra, true)
	FxEvents.fx_for(w, a, inst, fx_key, fx_mat(inst), d)


## Aim point for a release: lock target chest (+ lead) if it is roughly along the aim, else along the aim.
static func target_point(w: CombatWorld, a: ActorState, inst: ActionInst, reach: float = 14.0) -> Vector3:
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	var t := w.get_actor(a.lock_target)
	if t != null:
		var to := t.pos - a.pos
		to.y = 0
		if to.length() < 0.1 or to.normalized().dot(dir) > 0.9 or not inst.data.get("aim_active", false):
			return t.chest() + Vector3(t.vel.x, 0, t.vel.z) * 0.2
	return a.chest() + dir * reach


## Launch velocity reaching `to` with a horizontal speed under a gravity scale (0 = straight line).
static func launch_vel(from: Vector3, to: Vector3, h_speed: float, gscale: float = 1.0) -> Vector3:
	var d := to - from
	var flat := Vector3(d.x, 0, d.z)
	var dist := flat.length()
	if dist < 0.01:
		return Vector3(0, h_speed, 0)
	var t := dist / h_speed
	var vy := d.y / t + 0.5 * Sim.GRAVITY * gscale * t
	return flat / dist * h_speed + Vector3(0, vy, 0)


## Makes a body an attack of the action (new attack instance, owner, cohesion, tier, chain contact).
static func arm(w: CombatWorld, a: ActorState, inst: ActionInst, b: MatBody, damage: float, balance: float) -> void:
	b.controller = -1
	b.authority = 0.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = a.id
	b.hit_set.clear()
	b.hit_set[a.id] = true
	b.damage = damage
	b.balance_damage = balance
	b.tier = inst.tier()
	b.sub = inst.sub
	b.residual_owner = a.id
	b.residual_authority = Interactions.cohesion(b.tier)
	b.props["src_attack"] = inst.attack_id
	b.props["move"] = inst.id
	b.touch(a.id, "release", w.tick)


## Body hit something (CombatWorld._body_impact / projectile hits): props.on_impact behaviour.
static func on_impact(w: CombatWorld, b: MatBody, what: String) -> void:
	VerbProjectile.on_impact(w, b, what)


## A tagged wave ended (budget, blocked, viscous, clash): leaves its zone, settles its material.
static func on_wave_end(w: CombatWorld, b: MatBody, why: String) -> void:
	VerbGroundLine.on_end(w, b, why)


static func leave_trail(w: CombatWorld, b: MatBody) -> void:
	VerbGroundLine.leave_trail(w, b)


## Technique context for grip-verb techniques (HUD label, AI): {mode, body, ok, reason}.
static func grip_preview(w: CombatWorld, a: ActorState, d: Dictionary, dir: Vector3) -> Dictionary:
	return VerbGrip.preview(w, a, d, dir)
