extends TestCase
## Shared helpers for the Earth kit tests (test_kit_earth_*.gd). No tests here.

const TIER_HOLD := [1, 40, 70, 118]      # attack / guard hold ticks that reach T0..T3 (earth_attack: 0.55 / 1.1 / 1.9 s)
const SLOT_GESTURE := {"thrust": Sim.Gesture.UP, "ground": Sim.Gesture.DOWN, "sweep": Sim.Gesture.SIDE}


## A duel: Earth fighter A (sub `sub`) at z = 4 facing a dummy target T at z = -3 (7 m apart).
static func duel(h: SimHarness, sub: int, t_elem: int = Sim.Element.FIRE, dist: float = 7.0) -> Array:
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.EARTH)
	var t := h.actor("T", Vector3(0, 0, 4.0 - dist), 1, {}, t_elem)
	t.is_dummy = true
	a.facing = PI
	t.facing = 0.0
	h.sub(a, sub)
	h.step(3)
	a.facing = PI
	return [a, t]


## Every fx event of the actor uses catalogued keys (fx, mat, shape).
static func bad_fx(h: SimHarness, actor_id: int) -> Array:
	var bad := []
	for e in h.events("fx"):
		if int(e.get("actor", -1)) != actor_id:
			continue
		if not FxEvents.is_known("fx", String(e.fx)) or not FxEvents.is_known("mat", String(e.mat)) or not FxEvents.is_known("shape", String(e.get("shape", ""))):
			bad.append("%s/%s/%s" % [e.fx, e.mat, e.get("shape", "")])
	return bad


## Steps n ticks, tracking the lowest Focus (+ reserve as Focus) seen in st.min when st is given.
static func steps(h: SimHarness, a: ActorState, n: int, st: Dictionary = {}) -> void:
	for k in n:
		h.step()
		if st.has("min"):
			st.min = minf(float(st.min), a.focus + a.heat_reserve / Sim.HU_PER_FOCUS)


## Performs the input of a slot at a tier (hold time) and releases it.
static func perform(h: SimHarness, a: ActorState, slot: String, tier: int, st: Dictionary = {}) -> void:
	var hold: int = TIER_HOLD[clampi(tier, 0, 3)]
	match slot:
		"strike":
			h.press(a, "attack")
			steps(h, a, hold, st)
			h.release(a, "attack")
		"thrust", "ground", "sweep":
			h.flick(a, "attack", SLOT_GESTURE[slot])
			steps(h, a, hold, st)
			h.release(a, "attack")
		"guard":
			h.press(a, "guard")
			steps(h, a, maxi(hold, 20), st)
			h.release(a, "guard")
		"push", "sink":
			h.press(a, "guard")
			steps(h, a, maxi(hold, 14), st)
			h.flick(a, "guard", Sim.Gesture.UP if slot == "push" else Sim.Gesture.DOWN)
			steps(h, a, 1, st)
			h.release(a, "guard")
		"tech":
			h.press(a, "tech")
			steps(h, a, 40, st)
			h.it(a).attack_pressed = true
			steps(h, a, 4, st)
			h.release(a, "tech")
		"evade":
			h.press(a, "evade")
			steps(h, a, 1, st)
		"evade_hold":
			h.it(a).move = Vector3.ZERO
			h.press(a, "evade")
			h.it(a).evade_held = true
			steps(h, a, 40, st)
			h.it(a).evade_held = false


## Runs a slot at a tier on a fresh duel and returns {move, started, paid, ended, tier, bad_fx, a, t, h}.
static func run_move(sub: int, slot: String, tier: int, prep: Callable = Callable()) -> Dictionary:
	var h := SimHarness.new(7)
	var s := duel(h, sub)
	var a: ActorState = s[0]
	var t: ActorState = s[1]
	if prep.is_valid():
		prep.call(h, a, t)
	h.log.clear()
	var f0 := a.focus + a.heat_reserve / Sim.HU_PER_FOCUS
	var m0 := a.metal_carried
	var id := Moves.resolve(Sim.Element.EARTH, sub, slot)
	var st := {"min": f0}
	perform(h, a, slot, tier, st)
	st.min = minf(float(st.min), a.focus + a.heat_reserve / Sim.HU_PER_FOCUS)
	var done := func() -> bool:
		st.min = minf(float(st.min), a.focus + a.heat_reserve / Sim.HU_PER_FOCUS)
		return a.action == null or (a.action.id != id and a.action.id != "guard")
	var ended := h.until(done, 400) >= 0
	h.step(2)
	var min_focus := minf(float(st.min), a.focus + a.heat_reserve / Sim.HU_PER_FOCUS)
	var started := h.events("action").any(func(e): return e.actor == a.id and e.move == id and e.phase == "startup") \
		or (slot == "guard" and h.events("guard").any(func(e): return e.actor == a.id and String(e.get("spec", "")) == id))
	var tiers := h.events("charge").filter(func(e): return e.actor == a.id).map(func(e): return int(e.tier))
	var top := 0
	for x in tiers:
		top = maxi(top, x)
	var paid := min_focus < f0 - 1e-6 or a.metal_carried < m0 - 1e-6 or h.log.any(func(e): return e.type == "insufficient" and e.get("actor", -1) == a.id)
	return {"move": id, "started": started, "paid": paid, "ended": ended, "tier": top, "bad_fx": bad_fx(h, a.id), "a": a, "t": t, "h": h,
		"fx": h.events("fx").filter(func(e): return e.actor == a.id).size()}


## A molten stone body at p (booked: ground_taken + generated heat).
static func lava(h: SimHarness, mass: float, p: Vector3, liquid: float = 1.0) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.BLOB, mass, p, "test")
	h.w.mass_ledger.ground_taken += mass
	h.w.ledger.generated += Thermal.heat(b, mass * (Sim.STONE_C * (Sim.STONE_MELT_C - Sim.AMBIENT_C) + Sim.STONE_LATENT * liquid))
	Thermal.update_phase(b)
	b.on_ground = true
	return b


## A lava wave of `owner` at p flowing along dir.
static func lava_wave(h: SimHarness, mass: float, p: Vector3, dir: Vector3, owner: ActorState = null) -> MatBody:
	var b := lava(h, mass, p)
	b.form = Sim.Form.WAVE
	b.wave_dir = dir.normalized()
	b.wave_budget = 12.0
	b.wave_width = 1.1 + mass * 0.025
	b.wave_path = PackedVector3Array([p])
	b.vel = b.wave_dir * 7.5
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = owner.id if owner != null else -1
	b.damage = 18.0
	b.balance_damage = 55.0
	if owner != null:
		b.hit_set[owner.id] = true
	return b


## A standing wall body (mat/tag/mass) raised by `owner` at p facing yaw.
static func wall(h: SimHarness, mat: int, tag: String, mass: float, p: Vector3, owner: ActorState = null, yaw: float = 0.0) -> MatBody:
	var b := h.w.spawn_body(mat, Sim.Form.WALL, mass, p, "test")
	if mat == Sim.Mat.STONE or mat == Sim.Mat.SAND or mat == Sim.Mat.GLASS:
		h.w.mass_ledger.ground_taken += mass
	b.tag = StringName(tag)
	b.wall_half = Vector3(1.1, 0.75, 0.28)
	b.wall_rise = 1.0
	b.wall_yaw = yaw
	b.static_body = true
	b.props["standing"] = 999.0
	if owner != null:
		b.last_actor = owner.id
	return b


## Predicts a threat body against a counter (move at a tier, or a body).
static func pr(h: SimHarness, threat: MatBody, counter: Variant, tier: int = 0, perfect: bool = false, by: ActorState = null) -> Dictionary:
	var c: Agent
	if counter is MatBody:
		c = Agent.of_body(h.w, counter)
		c.perfect = perfect
	else:
		c = Agent.of_move(h.w, by, String(counter), tier, perfect)
	return Interactions.predict(h.w, Agent.of_body(h.w, threat, by), c)


## Predicts a volume (class, channels) against a counter body / move.
static func prv(h: SimHarness, cls: String, ch: Dictionary, counter: Variant, tier: int = 0, by: ActorState = null) -> Dictionary:
	var v := Agent.of_volume(h.w, null, null, StringName(cls), Vector3(0, 1, -3), Vector3(0, 0, 1), ch)
	var c: Agent
	if counter is MatBody:
		c = Agent.of_body(h.w, counter)
	else:
		c = Agent.of_move(h.w, by, String(counter), tier, false)
	return Interactions.predict(h.w, v, c)


## A loose body of `mat` at p moving with vel as an attack of owner (or neutral). No ledger booking for
## metal (call metal_body for satchel-less metal: books metal_taken).
static func shot(h: SimHarness, mat: int, mass: float, p: Vector3, vel: Vector3, owner: ActorState = null, tag: String = "", temp: float = Sim.AMBIENT_C) -> MatBody:
	var b := h.w.spawn_body(mat, Sim.Form.CHUNK, mass, p, "test", temp)
	match mat:
		Sim.Mat.STONE, Sim.Mat.SAND, Sim.Mat.GLASS:
			h.w.mass_ledger.ground_taken += mass
		Sim.Mat.METAL:
			h.w.mass_ledger.metal_taken += mass
	b.tag = StringName(tag)
	b.vel = vel
	b.gravity_scale = 0.0
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = owner.id if owner != null else -1
	b.damage = 10.0
	b.balance_damage = 20.0
	if owner != null:
		b.hit_set[owner.id] = true
	return b


static func energy_drift(h: SimHarness, e0: float) -> float:
	return absf(h.w.system_energy() - h.w.ledger_balance() - e0)


static func e0(h: SimHarness) -> float:
	return h.w.system_energy() - h.w.ledger_balance()
