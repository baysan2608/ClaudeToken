class_name KitEarth
extends RefCounted
## Earth kit (Stone, Metal, Sand, Magma) - docs/MOVESET.md §7.1-§7.4, §8.1; docs/kits/earth.md.
##
## register() is called once by Moves.ensure() (after the core rules). Each sub-element lives in its own
## file and registers its moves, bindings, body ticks, zone effects and statuses:
##   EarthStone (sub 0, legacy kit + new slots), EarthMetal (sub 1), EarthSand (sub 2), EarthMagma (sub 3),
##   EarthRules (the Earth column of the counter matrix: cells of the Earth counter classes + outcomes).
## Moves with module "verbs" run on the generic verbs (data + hook_execute / hook_impact callables).
## Moves with module "kit_earth" are dispatched here by their def key `part` (guard specs: the spec's part).

const FPS := 60.0


static func register() -> void:
	EarthRules.register()
	EarthStone.register()
	EarthMetal.register()
	EarthSand.register()
	EarthMagma.register()


## Frames (60 Hz) to seconds.
static func f(n: float) -> float:
	return n / FPS


## Registers a def for Earth sub `sub` and binds it to its slot.
static func reg(id: String, sub: int, slot: String, d: Dictionary) -> void:
	var def := d.duplicate(true)
	def["element"] = Sim.Element.EARTH
	def["sub"] = sub
	def["slot"] = slot
	# Every Earth move dispatches through this kit (unknown ids fall through to the generic verbs), so the
	# kit can clean up after push / sink moves flicked out of a guard (a kept plate goes back home).
	def["module"] = "kit_earth"
	Moves.register(id, def)
	Moves.bind(Sim.Element.EARTH, sub, slot, id)


static func _part(inst: ActionInst) -> String:
	if inst.id == "guard":
		return String(inst.data.get("spec_def", {}).get("part", ""))
	return String(inst.def.get("part", ""))


static func on_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match _part(inst):
		"stone": EarthStone.on_start(w, a, inst, it)
		"metal": EarthMetal.on_start(w, a, inst, it)
		"sand": EarthSand.on_start(w, a, inst, it)
		"magma": EarthMagma.on_start(w, a, inst, it)
		_: Verbs.on_start(w, a, inst, it)
	if not inst.data.get("fizzle", false) and a.action == inst and inst.data.get("mode", "") != "burrow":
		_aura(w, a, inst, true)


## Returns the phase that follows startup (ActionInst.P.*).
static func after_startup(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	match _part(inst):
		"stone": return EarthStone.after_startup(w, a, inst, it)
		"metal": return EarthMetal.after_startup(w, a, inst, it)
		"sand": return EarthSand.after_startup(w, a, inst, it)
		"magma": return EarthMagma.after_startup(w, a, inst, it)
	return Verbs.after_startup(w, a, inst, it)


static func on_phase(w: CombatWorld, a: ActorState, inst: ActionInst, p: int) -> void:
	if p == ActionInst.P.RECOVERY:
		_aura(w, a, inst, false)
		_return_kept(w, a, inst)
	match _part(inst):
		"stone": EarthStone.on_phase(w, a, inst, p)
		"metal": EarthMetal.on_phase(w, a, inst, p)
		"sand": EarthSand.on_phase(w, a, inst, p)
		"magma": EarthMagma.on_phase(w, a, inst, p)
		_: Verbs.on_phase(w, a, inst, p)


static func on_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match _part(inst):
		"stone": EarthStone.on_tick(w, a, inst, it)
		"metal": EarthMetal.on_tick(w, a, inst, it)
		"sand": EarthSand.on_tick(w, a, inst, it)
		"magma": EarthMagma.on_tick(w, a, inst, it)
		_: Verbs.on_tick(w, a, inst, it)


static func on_interrupt(w: CombatWorld, a: ActorState, inst: ActionInst, reason: String) -> void:
	_aura(w, a, inst, false)
	_return_kept(w, a, inst)
	match _part(inst):
		"stone": EarthStone.on_interrupt(w, a, inst, reason)
		"metal": EarthMetal.on_interrupt(w, a, inst, reason)
		"sand": EarthSand.on_interrupt(w, a, inst, reason)
		"magma": EarthMagma.on_interrupt(w, a, inst, reason)
		_: Verbs.on_interrupt(w, a, inst, reason)


# ------------------------------------------------------------------ shared helpers

## A push / sink move flicked out of a guard keeps the guard's held material; if the move did not use it
## (fizzled, or a move that doesn't throw it), a metal plate goes back into the satchel.
static func _return_kept(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	if not inst.data.get("from_guard", false):
		return
	var b := w.held(a)
	if b != null and b.id == int(inst.data.get("held", -1)) and b.mat == Sim.Mat.METAL:
		EarthMetal.to_satchel(w, a, b, "returned")

## Stance / mode moves (def key aura_mat): one aura on/off cue with a catalogued material (the verbs'
## own aura cue is muted with fx.aura = "" because it carries the stance name as its shape).
static func _aura(w: CombatWorld, a: ActorState, inst: ActionInst, on: bool) -> void:
	if inst.id == "guard" or not inst.def.has("aura_mat"):
		return
	if bool(inst.data.get("aura_on", false)) == on:
		return
	inst.data["aura_on"] = on
	FxEvents.fx_for(w, a, inst, "aura", String(inst.def.aura_mat), {"on": on})


static func flat(v: Vector3) -> Vector3:
	return Vector3(v.x, 0.0, v.z)


static func flat_dist(p: Vector3, q: Vector3) -> float:
	return Vector2(p.x - q.x, p.z - q.z).length()


## Fizzles an action (nothing to act on / resource short): the action goes to recovery.
static func fizzle(w: CombatWorld, a: ActorState, inst: ActionInst, what: String) -> void:
	inst.data["fizzle"] = true
	w.emit("insufficient", {"actor": a.id, "what": what, "move": inst.id})


## The tier a push/sink move inherits from the guard it was flicked out of (guard hold time on the
## move's own tier clock, or the guard spec's tier, whichever is higher).
static func guard_tier(inst: ActionInst) -> int:
	var t := int(inst.data.get("tier", 0))
	if inst.data.has("guard_t"):
		t = maxi(t, Charge.tier_for(inst.def, float(inst.data.guard_t)))
	return clampi(t, 0, Charge.max_tier(inst.def))


## Turns a molten (or softened) stone body into a lava WAVE of `a` flowing along `dir` (flagship wave
## rules: bends toward the target while fluid, walls and rises stop it, water quenches it). Budget is
## the pour rule (6 m + 0.3 m/kg) x mult. Returns the wave.
static func pour_wave(w: CombatWorld, a: ActorState, inst: ActionInst, b: MatBody, dir: Vector3, mult: float, why: String) -> MatBody:
	var d: Dictionary = Moves.DEFS.pour
	dir = flat(dir)
	dir = dir.normalized() if dir.length() > 0.01 else a.forward()
	if b.controller >= 0:
		var h := w.get_actor(b.controller)
		if h != null and h.held_body == b.id:
			h.held_body = -1
		b.controller = -1
	if b.form == Sim.Form.ZONE:
		FxEvents.zone(w, b, "close")
		b.zone_radius = 0.0
	b.static_body = false
	b.captured_by = -1
	b.props.erase("face_of")
	var old_form := b.form
	b.form = Sim.Form.WAVE
	b.tag = &""
	b.pos.y = w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4)
	b.vel = Vector3.ZERO
	b.gravity_scale = 1.0
	b.wave_dir = dir
	b.wave_budget = (float(d.base_budget) + float(d.budget_per_kg) * b.mass) * mult
	b.wave_width = 1.1 + b.mass * 0.025
	b.wave_path = PackedVector3Array([b.pos])
	b.wave_stalled = false
	b.max_life = -1.0
	b.age = 0.0
	b.update_radius()
	var sc := clampf(sqrt(b.mass / Sim.STONE_SHOT_MASS), 0.5, 1.6)
	Verbs.arm(w, a, inst, b, float(d.damage) * sc, float(d.balance) * sc)
	b.touch(a.id, "pour", w.tick)
	w.emit("transform", {"body": b.id, "at": b.pos, "from": Sim.FORM_NAMES[old_form], "to": "wave", "why": why})
	return b


## Ground point `dist` metres in front of the fighter along dir.
static func ground_point(w: CombatWorld, a: ActorState, dir: Vector3, dist: float) -> Vector3:
	var p := a.pos + flat(dir).normalized() * dist
	p.y = w.arena.ground_height(p.x, p.z, a.pos.y + 0.4)
	return p
