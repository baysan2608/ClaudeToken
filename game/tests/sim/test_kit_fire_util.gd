class_name FireKitTest
extends TestCase
## Shared helpers of the Fire kit tests (test_kit_fire_*.gd). This file has no test_ methods itself.

var h: SimHarness

const MODE_SHAPES := ["glide", "surf", "skate", "flight", "hover", "burrow", "run", "walk", "grounding", "stance"]


## Energy and every mass ledger of a world, as one snapshot (exact conservation checks).
static func snap(w: CombatWorld) -> Dictionary:
	return {"energy": w.system_energy() - w.ledger_balance(), "water": w.water_mass(), "earth": w.earth_mass(), "metal": w.metal_mass(),
		"plant": w.plant_mass()}


func ledgers_ok(base: Dictionary, label: String, eps: float = 1e-4) -> bool:
	var s := snap(h.w)
	var ok := true
	for k in base:
		ok = near(float(s[k]), float(base[k]), eps, "%s: %s ledger" % [label, k]) and ok
	return ok


## Fire fighter F (sub s) facing a rival R `dist` m away (both on open flat ground), after the turn-to-face ticks.
func duel(sub: int = 0, rival_element: int = Sim.Element.EARTH, seed_value: int = 5, dist: float = 8.0, kit: Dictionary = {}) -> Array:
	h = SimHarness.new(seed_value)
	var f := h.actor("F", Vector3(0, 0, 3.0 + dist * 0.5), 0, kit, Sim.Element.FIRE)
	var r := h.actor("R", Vector3(0, 0, 3.0 - dist * 0.5), 1, {}, rival_element)
	f.subs[Sim.Element.FIRE] = sub
	h.step(20)
	h.log.clear()
	return [f, r]


## Starts `id` directly at `tier` (charge frozen) with the buttons held `hold_ticks`, then released; steps `after` more.
func run_move(p: ActorState, id: String, tier: int, hold_ticks: int = 30, after: int = 90) -> ActionInst:
	var d: Dictionary = Moves.DEFS[id]
	var slot := String(d.get("slot", "strike"))
	var it := h.it(p)
	var inst := h.w.start_action(p, "guard" if slot == "guard" else id, it, {"slot": slot, "tier": tier, "charge_frozen": true,
		"released": tier > 0 and Sim.ATTACK_SLOTS.has(slot), "spec": id if slot == "guard" else ""})
	if tier > 0 and Sim.ATTACK_SLOTS.has(slot) and inst.phase == ActionInst.P.STARTUP:
		inst.data["tier"] = tier
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


func bodies_of(mat: int, form: int = -1) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in h.w.bodies:
		if b.alive and b.mat == mat and (form < 0 or b.form == form):
			out.append(b)
	return out


func zones_tagged(tag: String) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in h.w.bodies:
		if b.alive and b.form == Sim.Form.ZONE and String(b.tag) == tag:
			out.append(b)
	return out


func events_of(type: String, key: String, value: Variant) -> Array[Dictionary]:
	return h.events(type).filter(func(e): return e.get(key) == value)


## A synthetic threat agent: class, threat power (PU on the main channel), mass.
static func threat(w: CombatWorld, cls: String, tp: float, mass: float = 10.0, channel: String = "K") -> Agent:
	var g := Agent.new()
	g.kind = "volume"
	g.cls = StringName(cls)
	g.ccls = StringName(cls)
	g.mass = mass
	g.hostile = true
	g.ch[channel] = tp
	return g


## A stone wall (Bulwark, 120 kg, risen) owned by `owner` at `p` facing yaw.
func bulwark(owner: ActorState, p: Vector3, yaw: float, mass: float = 120.0) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, mass, p, "test_wall")
	h.w.mass_ledger.ground_taken += mass
	b.wall_yaw = yaw
	b.wall_half = Vector3(1.1, 0.75, 0.28)
	b.wall_rise = 1.0
	b.static_body = true
	b.props["standing"] = 30.0
	b.touch(owner.id if owner != null else -1, "wall", h.w.tick)
	return b


## Every fx / interaction key logged by the harness is catalogued (docs/MOVESET.md §15.6).
func fx_catalogued(label: String) -> void:
	var bad := {}
	for e in h.log:
		if e.type == "fx":
			if not FxEvents.is_known("fx", String(e.fx)):
				bad["fx:" + String(e.fx)] = true
			if not FxEvents.is_known("mat", String(e.mat)):
				bad["mat:" + String(e.mat)] = true
			if not (FxEvents.is_known("shape", String(e.shape)) or MODE_SHAPES.has(String(e.shape))):
				bad["shape:" + String(e.shape)] = true
		elif e.type == "interaction":
			if not FxEvents.is_known("outcome", String(e.outcome)):
				bad["outcome:" + String(e.outcome)] = true
	check(bad.is_empty(), "%s: uncatalogued keys %s" % [label, bad.keys()])
