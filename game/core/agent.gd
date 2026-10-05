class_name Agent
extends RefCounted
## One side of an interaction (docs/MOVESET.md §15.3): a threat (body, volume, hit) or a counter
## (barrier body, actor guard, active move volume, stance, environment). Built by the static
## constructors below; the Interactions engine reads `cls` (threat role), `ccls` (counter role),
## the power channels `ch` and the counter power.

var kind := ""               # body | volume | guard | move | env | stance
var cls: StringName = &""    # threat class (Interactions.classify / volume class)
var ccls: StringName = &""   # counter class
var body: MatBody = null
var actor: ActorState = null # threat: its attacker/owner; counter: the defender
var inst: ActionInst = null
var def: Dictionary = {}     # move def (volumes, moves) or guard spec def
var pos := Vector3.ZERO
var dir := Vector3.ZERO
## Power channels in PU: K kinetic, H heat, C cold, E electric, P pressure/sonic.
var ch := {"K": 0.0, "H": 0.0, "C": 0.0, "E": 0.0, "P": 0.0}
var mass := 0.0
var speed := 0.0
var heat := 0.0              # HU above ambient (volumes: the heat budget still to spend)
var tier := 0
var perfect := false
var power := -1.0            # explicit counter power (PU); < 0 = derived (barrier mass x hardness)
var mat := -1
## The threat is an active attack against the counter (false for loose/inert bodies or own shots).
var hostile := true
var data := {}               # site extras (hit info, damage, ...)


## A material body. As a threat its channels come from mass, speed and heat; as a counter
## (barrier, zone, held shield) its power is mass x hardness (or MatBody.power for zones).
static func of_body(w: CombatWorld, b: MatBody, against: ActorState = null) -> Agent:
	var g := Agent.new()
	g.kind = "body"
	g.body = b
	g.cls = Interactions.classify(b)
	g.ccls = Interactions.counter_class(b, w)
	g.mat = b.mat
	g.pos = b.pos
	g.mass = b.mass
	g.speed = b.vel.length()
	g.dir = b.vel.normalized() if g.speed > 1e-4 else Vector3.ZERO
	g.tier = b.tier
	var owner := b.attack_owner
	if owner < 0:
		owner = b.controller if b.controller >= 0 else b.owner
	if owner < 0 and b.form == Sim.Form.WALL:
		owner = b.last_actor
	g.actor = w.get_actor(owner) if owner >= 0 else null
	g.hostile = b.attack_id != 0 and (against == null or b.attack_owner != against.id)
	var e := b.thermal_energy()
	g.heat = maxf(0.0, e)
	g.ch.K = b.mass * g.speed / 20.0
	g.ch.H = maxf(0.0, e) / 20.0
	g.ch.C = maxf(0.0, -e) / 20.0
	g.ch.E = b.charge
	if b.power > 0.0:
		var chn := String(b.props.get("channel", "P"))
		g.ch[chn] = float(g.ch.get(chn, 0.0)) + b.power
	if b.form == Sim.Form.ZONE or b.form == Sim.Form.CLOUD or b.power > 0.0:
		g.power = b.power if b.power > 0.0 else -1.0
	var hook: Callable = Interactions.channel_hook(b.tag)
	if hook.is_valid():
		hook.call(w, b, g)
	return g


## An instant volume (cone, beam, blast, sound...) of a move. channels: {K,H,C,E,P}; the main
## channel value is also the volume's counter power when it answers a threat.
static func of_volume(w: CombatWorld, a: ActorState, inst: ActionInst, cls: StringName, pos: Vector3, dir: Vector3, channels: Dictionary) -> Agent:
	var g := Agent.new()
	g.kind = "volume"
	g.cls = cls
	g.ccls = cls
	g.actor = a
	g.inst = inst
	g.def = inst.def if inst != null else {}
	g.pos = pos
	g.dir = dir
	g.tier = inst.tier() if inst != null else 0
	var best := 0.0
	for k in channels:
		g.ch[k] = float(channels[k])
		best = maxf(best, float(channels[k]))
	g.power = best
	if channels.has("heat_hu"):
		g.heat = float(channels.heat_hu)
		g.ch.erase("heat_hu")
	g.mat = -1
	g.hostile = true
	if w != null and inst != null:
		var cp := Charge.counter_power(Charge.pdef(inst), g.tier)
		if cp >= 0.0:
			g.power = cp
	return g


## A melee/area hit described by a hit_actor info dictionary (legacy sites). Uses info.agent if set.
static func of_hit(w: CombatWorld, info: Dictionary) -> Agent:
	if info.get("agent") is Agent:
		return info.agent
	var b := w.get_body(int(info.get("body", -1)))
	if b != null and b.alive:
		var gb := of_body(w, b)
		gb.hostile = true
		return gb
	var kind := String(info.get("kind", ""))
	var cls: StringName = Interactions.KIND_CLASS.get(kind, &"blast")
	var chn: String = Interactions.CLASS_CHANNEL.get(cls, "P")
	var g := of_volume(w, w.get_actor(int(info.get("attacker", -1))), null, cls, info.get("from", Vector3.ZERO), Vector3.ZERO,
		{chn: float(info.get("power", info.get("damage", 0.0)))})
	return g


## The guard a fighter holds now (counter). Class from the guard spec (def.counter.cls) or the
## legacy sub-0 guard of the element it started with; power from the spec tier, a held barrier
## body (mass x hardness) or the plain guard (10).
static func of_guard(w: CombatWorld, a: ActorState) -> Agent:
	var g := Agent.new()
	g.kind = "guard"
	g.actor = a
	g.pos = a.chest()
	g.dir = a.forward()
	g.perfect = w.perfect_guard(a)
	var inst := a.action
	var spec := {}
	if inst != null and inst.id == "guard":
		g.inst = inst
		spec = inst.data.get("spec_def", {})
		g.tier = inst.tier()
	g.def = spec
	var c: Dictionary = spec.get("counter", {})
	if c.has("cls"):
		g.ccls = StringName(c.cls)
		g.power = Charge.counter_power(spec, g.tier)
		if g.power < 0.0:
			var hb := w.held(a)
			if hb != null:
				g.body = hb
				g.power = hb.mass * Materials.hardness(hb)
			else:
				g.power = Interactions.PLAIN_GUARD_CP
	else:
		var ge := w.guard_element(a)
		var hb2 := w.held(a)
		match ge:
			Sim.Element.EARTH:
				g.ccls = &"guard_earth"
				g.power = Interactions.PLAIN_GUARD_CP
			Sim.Element.WATER:
				if hb2 != null and hb2.is_water() and inst != null and inst.data.get("shield", false):
					g.ccls = &"shield_water"
					g.body = hb2
					g.power = hb2.mass * Materials.hardness(hb2)
				else:
					g.ccls = &"guard"
					g.power = Interactions.PLAIN_GUARD_CP
			Sim.Element.FIRE:
				g.ccls = &"aura_flame"
				g.power = Interactions.PLAIN_GUARD_CP
			Sim.Element.AIR:
				g.ccls = &"guard_wind"
				g.power = Interactions.WIND_GUARD_CP
	g.cls = g.ccls
	return g


## Hypothetical counter of a move (AI choices, Lab matrix viewer): def.counter {cls, power[tier]}
## or a barrier spec (mass x hardness at the tier). Pure: nothing is spawned.
static func of_move(w: CombatWorld, a: ActorState, move_id: String, tier: int, perfect: bool) -> Agent:
	var g := Agent.new()
	g.kind = "move"
	g.actor = a
	g.tier = tier
	g.perfect = perfect
	g.def = Moves.DEFS.get(move_id, {})
	if a != null:
		g.pos = a.chest()
		g.dir = a.forward()
	var c: Dictionary = g.def.get("counter", {})
	if c.has("cls"):
		g.ccls = StringName(c.cls)
	elif move_id == "guard" or g.def.is_empty():
		g.ccls = &"guard"
	var cp := Charge.counter_power(g.def, tier)
	if cp >= 0.0:
		g.power = cp
	elif String(g.def.get("verb", "")) == "barrier":
		var m := float(Charge.pget(g.def, tier, "mass", 0.0))
		var hard := float(Charge.pget(g.def, tier, "hardness", Materials.prop(_def_mat(g.def), "hardness", 0.25)))
		g.power = m * hard
	elif g.ccls == &"guard":
		g.power = Interactions.PLAIN_GUARD_CP
	g.cls = g.ccls
	return g


static func _def_mat(def: Dictionary) -> int:
	var m: Variant = def.get("mat", "stone")
	if m is int:
		return m
	return maxi(0, Sim.MAT_NAMES.find(String(m)))


## Environment counter (pool, puddle, plate, arena_wall, ground). Effectively unbreakable.
static func of_env(w: CombatWorld, kind: String, pos: Vector3, a: ActorState = null) -> Agent:
	var g := Agent.new()
	g.kind = "env"
	g.ccls = StringName(kind)
	g.cls = g.ccls
	g.pos = pos
	g.actor = a
	g.power = Interactions.ENV_CP
	if kind == "pool" and w != null:
		g.body = w.pool
	elif kind == "puddle" and w != null:
		g.body = w.puddle_at(pos)
	return g


## A fighter's stance as a counter (anchor: Stone Skin, Iron Stance, Anchor, Deep Roots...).
static func of_stance(w: CombatWorld, a: ActorState) -> Agent:
	var g := Agent.new()
	g.kind = "stance"
	g.actor = a
	g.ccls = &"anchor"
	g.cls = g.ccls
	g.pos = a.pos
	var st: Dictionary = a.status.get("anchored", {})
	g.power = float(st.get("mag", 0.0)) if not st.is_empty() else float(a.status.get("stance_cp", {}).get("mag", 0.0))
	if g.power <= 0.0:
		g.power = 30.0 if a.anchored else 0.0
	g.perfect = false
	if w == null:
		return g
	return g


## Threat-power channel total with unit weights (diagnostics).
func total() -> float:
	return float(ch.K) + float(ch.H) + float(ch.C) + float(ch.E) + float(ch.P)
