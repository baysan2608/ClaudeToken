class_name AirKitTest
extends TestCase
## Shared helpers of the Air kit tests (test_kit_air_*.gd). This file has no test_ methods itself.

var h: SimHarness

const E := Sim.Element.AIR
## Fx shapes the verbs emit as the shape of an aura / cast cue (mode kinds and T+A kinds).
const MODE_SHAPES := ["glide", "surf", "skate", "flight", "hover", "burrow", "run", "walk", "roots", "stone_skin", "iron", "anchor",
	"split", "freeze", "compress", "cool", "condense", "retag", "ground", "down", "wind", "storm", "sound", "vacuum"]


## Energy and every mass ledger of a world, as one snapshot (exact conservation checks).
static func snap(w: CombatWorld) -> Dictionary:
	return {"energy": w.system_energy() - w.ledger_balance(), "water": w.water_mass(), "earth": w.earth_mass(), "metal": w.metal_mass(),
		"plant": w.plant_mass()}


func ledgers_ok(base: Dictionary, label: String, eps: float = 1e-5) -> void:
	var s := snap(h.w)
	for k in base:
		near(float(s[k]), float(base[k]), eps, "%s: %s ledger" % [label, k])


## Air fighter A (sub s) at (0,0,z0) facing a passive rival R of `rival_element` 10 m away, after the turn-to-face ticks.
func duel(sub: int = 0, rival_element: int = Sim.Element.EARTH, seed_value: int = 3, dist: float = 10.0) -> Array:
	h = SimHarness.new(seed_value)
	var a := h.actor("A", Vector3(0, 0, 3.0 + dist * 0.5), 0, {"glide": true}, Sim.Element.AIR)
	var r := h.actor("R", Vector3(0, 0, 3.0 - dist * 0.5), 1, {}, rival_element)
	r.is_dummy = true
	a.subs[3] = sub
	h.step(20)
	h.log.clear()
	return [a, r]


## Starts `id` directly at `tier` (charge frozen) with the buttons held `hold_ticks`, then released; steps `after` more ticks.
## Returns the action instance.
func run_move(p: ActorState, id: String, tier: int, hold_ticks: int = 30, after: int = 90) -> ActionInst:
	var d: Dictionary = Moves.DEFS[id]
	var slot := Moves.slot_of(int(d.get("element", 3)), int(d.get("sub", 0)), id)
	if slot == "":
		slot = String(d.get("slot", "strike"))
	var it := h.it(p)
	var inst := h.w.start_action(p, "guard" if slot == "guard" else id, it, {"slot": slot, "tier": tier, "charge_frozen": true,
		"spec": id if slot == "guard" else ""})
	it.attack_held = true
	it.guard_held = true
	it.tech_held = true
	it.evade_held = true
	h.step(hold_ticks)
	it.attack_held = false
	it.guard_held = false
	it.tech_held = false
	it.evade_held = false
	h.step(after)
	return inst


## Starts `id` at `tier` and calls `on_ready` (spawns the threat) at the last moment: T0 right after the press, higher tiers
## after the hold (frozen charge, `hold` ticks), then releases and steps `after` ticks. Returns the action instance.
func run_when(p: ActorState, id: String, tier: int, on_ready: Callable, hold: int = 28, after: int = 40) -> ActionInst:
	var d: Dictionary = Moves.DEFS[id]
	var slot := Moves.slot_of(int(d.get("element", 3)), int(d.get("sub", 0)), id)
	if slot == "":
		slot = String(d.get("slot", "strike"))
	var it := h.it(p)
	var inst := h.w.start_action(p, "guard" if slot == "guard" else id, it, {"slot": slot, "tier": tier, "charge_frozen": true,
		"spec": id if slot == "guard" else ""})
	if tier > 0:
		it.attack_held = true
		it.guard_held = true
		it.tech_held = true
		h.step(hold)
	if on_ready.is_valid():
		on_ready.call()
	it.attack_held = false
	it.guard_held = false
	it.tech_held = false
	h.step(after)
	return inst


func bodies_of(mat: int, form: int = -1) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in h.w.bodies:
		if b.alive and b.mat == mat and (form < 0 or b.form == form):
			out.append(b)
	return out


func bodies_tagged(tag: String) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in h.w.bodies:
		if b.alive and String(b.tag) == tag:
			out.append(b)
	return out


func zones_tagged(tag: String) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in h.w.bodies:
		if b.alive and b.form == Sim.Form.ZONE and String(b.tag) == tag:
			out.append(b)
	return out


## A synthetic threat agent: class, threat power (PU on the main channel K unless given), mass.
static func threat(_w: CombatWorld, cls: String, tp: float, mass: float = 10.0, channel: String = "K") -> Agent:
	var g := Agent.new()
	g.kind = "volume"
	g.cls = StringName(cls)
	g.ccls = StringName(cls)
	g.mass = mass
	g.hostile = true
	g.ch[channel] = tp
	return g


## A live 20 kg lava wave (396 HU, the flagship's poured wave) heading +z; mass scales the stone.
func lava_wave(mass: float, pos: Vector3 = Vector3.ZERO) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, pos, "test")
	h.w.mass_ledger.ground_taken += mass
	h.w.ledger.generated += Thermal.heat(b, mass * (Sim.STONE_C * 980.0 + Sim.STONE_LATENT))
	Thermal.update_phase(b)
	b.wave_dir = Vector3(0, 0, 1)
	b.wave_budget = 12.0
	b.vel = Vector3(0, 0, 7.5)
	b.attack_id = h.w.new_attack_id()
	return b


## Every fx / interaction key logged by the harness is catalogued (docs/MOVESET.md §15.6).
func fx_catalogued(label: String) -> void:
	for e in h.log:
		if e.type == "fx":
			check(FxEvents.is_known("fx", String(e.fx)), "%s: fx key '%s' catalogued" % [label, e.fx])
			check(FxEvents.is_known("mat", String(e.mat)), "%s: fx mat '%s' catalogued" % [label, e.mat])
			check(FxEvents.is_known("shape", String(e.shape)) or MODE_SHAPES.has(String(e.shape)), "%s: fx shape '%s' catalogued" % [label, e.shape])
		elif e.type == "interaction":
			check(FxEvents.is_known("outcome", String(e.outcome)), "%s: outcome '%s' catalogued" % [label, e.outcome])


## True when no NaN / infinite value is in the actor or any live body.
func finite_world(label: String) -> void:
	for a in h.w.actors:
		check(a.pos.is_finite() and a.vel.is_finite(), "%s: actor %s finite" % [label, a.name])
	for b in h.w.bodies:
		if b.alive:
			check(b.pos.is_finite() and b.vel.is_finite() and is_finite(b.mass), "%s: body #%d finite" % [label, b.id])
