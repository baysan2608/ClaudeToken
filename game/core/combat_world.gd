class_name CombatWorld
extends RefCounted
## Authoritative, fixed-step combat simulation (60 Hz). Owns actors, material
## bodies, contests, hits and the energy/mass ledgers. No scene-tree access:
## the game, the AI, the tests and recorded-input replays all drive step().
##
## Order of one tick (stable, documented in docs/COMBAT_SPEC.md):
##  1 timers  2 intents -> action starts  3 action ticks  4 actor movement
##  5 grip contests (sorted body id, strength desc, actor id)  6 bodies
##  7 body/actor contacts (sorted)  8 regen  9 cleanup.

const GRIP_MARGIN := 0.12        # challenger must beat the holder by this much
const RESIDUAL_START := 0.6      # thrower's leftover authority on release
const RESIDUAL_DECAY := 1.7      # per second
const TURN_RATE := 14.0          # rad/s free
const RUN_SPEED := 5.5
const RUN_MIN := 4.0
const WALK_MAX := 1.8
const RUN_STICK := 0.62
const ACCEL := 34.0
const DECEL := 42.0
const WAVE_TURN_RATE := 32.0     # deg/s a fluid wave bends toward its owner's target

var arena: ArenaMap
var actors: Array[ActorState] = []
var bodies: Array[MatBody] = []
var tick := 0
var rng := RandomNumberGenerator.new()
var events: Array[Dictionary] = []
var pool: MatBody

var ledger := {
	"generated": 0.0,          # HU created from Focus
	"ambient": 0.0,            # HU exchanged with air (negative = lost)
	"vapor": 0.0,              # HU carried away by steam
	"reserve_dissipated": 0.0,
	"vented": 0.0,
	"spent": 0.0,              # attack heat that went into actors / air
	"freeze_dump": 0.0,        # HU dumped to the environment by freezing techniques (negative)
	"removed": 0.0,            # thermal energy of bodies that left the sim (decay)
}
var mass_ledger := {
	"ground_taken": 0.0,       # stone mass pulled out of the ground
	"ground_returned": 0.0,    # stone mass returned (walls sinking, remnant decay)
	"vapor": 0.0,              # water mass lost as steam that dissipated
	"evaporated": 0.0,         # puddles evaporated at the cap
	# --- moveset engine (docs/COMBAT_SPEC.md "Engine" §E5) ---
	"metal_taken": 0.0,        # metal ripped from the arena plate (scrap)
	"metal_returned": 0.0,     # metal that left the sim (decay)
	"water_to_plant": 0.0,     # water converted into vines (1 kg -> 1 kg)
	"plant_from_ground": 0.0,  # vines grown from the ground
	"plant_returned": 0.0,     # vines that withered back into the ground
	"moisture_taken": 0.0,     # ambient moisture condensed into ice/water (fantasy, booked)
	"burned": 0.0,             # plant mass burned away
	"sand_to_glass": 0.0,      # booked conversions (earth_mass() counts all three materials)
	"glass_to_sand": 0.0,
	"sand_to_sandstone": 0.0,
}

## Registered hooks (static: kits register them once from register()). See register_* below.
static var _body_ticks := {}       # tag -> Callable(w, b, dt) -> bool (true = handled the motion)
static var _zone_effects := {}     # tag -> Callable(w, zone, dt)
static var _tech_previews := {}    # "e/s" -> Callable(w, a, dir) -> Dictionary
var _ix_last := {}                 # interaction event rate limiter (continuous contacts)
var _zone_pairs := {}              # "zone|body" -> last tick (body <-> zone interactions, rate-limited)

var _next_body := 1
var _next_attack := 1
var _grips: Array[Dictionary] = []
var _intents := {}
var _body_by_id := {}
var _actor_by_id := {}
var _null_intent := ActorIntent.new()
var _record_events := true


func _init(seed_value: int = 1) -> void:
	Moves.ensure()
	arena = ArenaMap.make_lab()
	rng.seed = seed_value
	pool = spawn_body(Sim.Mat.WATER, Sim.Form.POOL, 2000.0, Vector3(10.0, arena.pool_level, -1.0), "pool")
	pool.static_body = true
	pool.phase = Sim.Phase.LIQUID
	pool.liquid = 1.0


# ============================================================== registry

func add_actor(nm: String, p: Vector3, team: int, kit: Dictionary = {}, element: int = Sim.Element.EARTH) -> ActorState:
	var a := ActorState.new()
	a.id = actors.size() + 1
	a.name = nm
	a.team = team
	a.pos = p
	a.kit = kit.duplicate()
	a.element = element
	a.ground_y = arena.ground_height(p.x, p.z, p.y)
	a.pos.y = a.ground_y
	actors.append(a)
	_actor_by_id[a.id] = a
	return a


func get_actor(id: int) -> ActorState:
	return _actor_by_id.get(id, null)


func get_body(id: int) -> MatBody:
	return _body_by_id.get(id, null)


func spawn_body(mat: int, form: int, mass: float, p: Vector3, origin: String, temp: float = Sim.AMBIENT_C) -> MatBody:
	_enforce_cap()
	var b := MatBody.new()
	b.id = _next_body
	_next_body += 1
	b.mat = mat
	b.form = form
	b.mass = mass
	b.pos = p
	b.temp = temp
	b.origin = origin
	b.born_tick = tick
	if mat == Sim.Mat.WATER:
		b.phase = Sim.Phase.LIQUID
		b.liquid = 1.0
	elif mat == Sim.Mat.FIRE or mat == Sim.Mat.AIR or mat == Sim.Mat.STEAM:
		b.phase = Sim.Phase.GAS
	b.update_radius()
	bodies.append(b)
	_body_by_id[b.id] = b
	if _record_events:
		emit("spawn", {"body": b.id, "origin": origin})
	return b


## Explicit split: the child takes `mass` from the parent with the same intensive
## state (temperature, liquid fraction). Provenance is recorded on the child.
func split_body(parent: MatBody, mass: float, p: Vector3) -> MatBody:
	mass = minf(mass, parent.mass)
	var c := spawn_body(parent.mat, parent.form, mass, p, "split:%d" % parent.id, parent.temp)
	c.parent_id = parent.id
	c.lineage = parent.lineage.duplicate()
	c.lineage.append(parent.id)
	c.liquid = parent.liquid
	c.phase = parent.phase
	c.tag = parent.tag
	c.sub = parent.sub
	c.tier = parent.tier
	c.owner = parent.owner
	if parent.heat_payload > 0.0 and parent.mass > 0.0:
		var hp := parent.heat_payload * mass / parent.mass
		c.heat_payload = hp
		parent.heat_payload -= hp
	parent.mass -= mass
	parent.update_radius()
	c.update_radius()
	emit("split", {"parent": parent.id, "child": c.id, "mass": mass})
	return c


## Merge: `into` absorbs `other`, conserving mass, heat and momentum.
func merge_bodies(into: MatBody, other: MatBody) -> void:
	var e := into.thermal_energy() + other.thermal_energy()
	var m := into.mass + other.mass
	into.vel = (into.vel * into.mass + other.vel * other.mass) / m
	into.liquid = (into.liquid * into.mass + other.liquid * other.mass) / m
	if into.mat != other.mat:
		push_warning("merge_bodies: %s into %s (different materials)" % [other.describe(), into.describe()])
	into.heat_payload += other.heat_payload
	other.heat_payload = 0.0
	into.mass = m
	into.absorbed.append(other.id)
	into.update_radius()
	# Re-derive temperature so the merged thermal energy is exact.
	_set_energy(into, e)
	other.mass = 0.0
	other.alive = false
	emit("merge", {"into": into.id, "absorbed": other.id})


func _set_energy(b: MatBody, e: float) -> void:
	if b.mat == Sim.Mat.FIRE:
		b.heat_payload = maxf(0.0, e)
		return
	e -= b.heat_payload
	if Materials.is_fusible(b.mat) and b.mat != Sim.Mat.STONE:
		var sc := Materials.c(b.mat)
		var mc := Materials.melt(b.mat)
		var lat := Materials.latent(b.mat)
		var sens := b.mass * sc * (mc - Sim.AMBIENT_C)
		if e > sens and lat > 0.0:
			b.temp = mc
			b.liquid = clampf((e - sens) / (b.mass * lat), 0.0, 1.0)
			b.temp += (e - sens - b.liquid * b.mass * lat) / (b.mass * sc)
		else:
			b.liquid = 0.0
			b.temp = Sim.AMBIENT_C + e / (b.mass * sc)
		return
	if b.mat == Sim.Mat.PLANT:
		b.temp = Sim.AMBIENT_C + e / (b.mass * Materials.c(b.mat))
		return
	if b.mat == Sim.Mat.STONE:
		if b.liquid > 0.0:
			b.temp = Sim.STONE_MELT_C
			var latent := b.mass * Sim.STONE_LATENT
			b.liquid = clampf((e - b.mass * Sim.STONE_C * (Sim.STONE_MELT_C - Sim.AMBIENT_C)) / latent, 0.0, 1.0)
			var rest := e - b.mass * Sim.STONE_C * (Sim.STONE_MELT_C - Sim.AMBIENT_C) - b.liquid * latent
			b.temp += rest / (b.mass * Sim.STONE_C)
		else:
			b.temp = Sim.AMBIENT_C + e / (b.mass * Sim.STONE_C)
	elif b.mat == Sim.Mat.WATER:
		# e = m c (T - amb) - Lf m (1 - liquid)
		b.temp = Sim.AMBIENT_C + (e + Sim.WATER_LATENT_FUSION * b.mass * (1.0 - b.liquid)) / (b.mass * Sim.WATER_C)


func remove_body(b: MatBody, reason: String) -> void:
	if not b.alive:
		return
	b.alive = false
	if b.controller >= 0:
		var h := get_actor(b.controller)
		if h and h.held_body == b.id:
			h.held_body = -1
	emit("despawn", {"body": b.id, "reason": reason})


func new_attack_id() -> int:
	_next_attack += 1
	return _next_attack


func emit(type: String, d: Dictionary = {}) -> void:
	if not _record_events:
		return
	d["type"] = type
	d["tick"] = tick
	events.append(d)


func take_events() -> Array[Dictionary]:
	var e := events
	events = []
	return e


# ============================================================== step

func step(intents: Dictionary) -> void:
	_intents = intents
	for a in actors:
		_timers(a)
	for a in actors:
		_process_intent(a, _intent_for(a))
	for a in actors:
		_tick_action(a, _intent_for(a))
	for a in actors:
		_move_actor(a, _intent_for(a))
	_separate_actors()
	_resolve_grips()
	_update_bodies()
	_contacts()
	for a in actors:
		_regen(a)
	_cleanup()
	tick += 1


func _intent_for(a: ActorState) -> ActorIntent:
	if a.is_dummy:
		return _null_intent
	return _intents.get(a.id, _null_intent)


func _timers(a: ActorState) -> void:
	a.iframes = maxf(0.0, a.iframes - Sim.DT)
	a.burn_cd = maxf(0.0, a.burn_cd - Sim.DT)
	a.wetness = maxf(0.0, a.wetness - 0.05 * Sim.DT)
	Status.tick(self, a, Sim.DT)
	if a.stun > 0.0:
		a.stun -= Sim.DT
		if a.stun <= 0.0:
			a.stun = 0.0
			if a.stun_kind == "knockdown":
				a.stun = 0.75
				a.stun_kind = "getup"
				a.iframes = 0.75
				emit("getup", {"actor": a.id})
			else:
				a.stun_kind = ""
	# prune dedup table
	if tick % 120 == 0:
		for k in a.hits_taken.keys():
			if tick - int(a.hits_taken[k]) > 600:
				a.hits_taken.erase(k)


# ============================================================== intents & actions

const MORPH_WINDOW := 0.12       # a gesture this early in an attack startup morphs it (MOVESET §3)
const EVADE_HOLD_TIME := 0.2     # evade held this long morphs into evade_hold (when bound)
const WEAVE_COST := 6.0          # MOVESET §9.1
const COUNTER_CANCEL_COST := 8.0
const COUNTER_CANCEL_WINDOW := 0.5
const CHAIN_MAX := 3


static func gesture_slot(g: int) -> String:
	match g:
		Sim.Gesture.UP:
			return "thrust"
		Sim.Gesture.DOWN:
			return "ground"
		Sim.Gesture.SIDE:
			return "sweep"
	return ""


func _process_intent(a: ActorState, it: ActorIntent) -> void:
	if a.is_dummy:
		return
	if it.element_select >= 0 and it.element_select < 4 and a.elements[it.element_select] and a.element != it.element_select:
		# Switching only affects the NEXT action; running actions keep their element.
		a.element = it.element_select
		emit("element", {"actor": a.id, "element": a.element, "sub": a.sub()})
	if it.sub_select >= 0 and it.sub_select < 4 and a.subs_unlocked[a.element][it.sub_select] and a.sub() != it.sub_select:
		# Sub-element switching mirrors element switching: the next action only.
		a.subs[a.element] = it.sub_select
		emit("element", {"actor": a.id, "element": a.element, "sub": a.sub()})
	if it.target_cycle:
		_cycle_target(a)
	if a.lock_target < 0 or not _valid_target(a, a.lock_target):
		a.lock_target = _auto_target(a)
	# A technique cancel is an abort, never a commit: it drops a buffered technique press, and a
	# press that arrives with it (focus loss right after touch-down) never starts one.
	if it.tech_cancel and a.buffered == "tech":
		a.buffered = ""
	var press := ""
	if it.evade_pressed:
		press = "evade"
	elif it.guard_pressed:
		press = "guard"
	elif it.tech_pressed and not it.tech_cancel:
		press = "tech"
	elif it.attack_pressed:
		press = "attack"
	var slot := gesture_slot(it.attack_gesture) if press == "attack" else ""
	if press != "":
		if _try_start(a, press, it, slot):
			a.buffered = ""   # the newest explicit press supersedes an older buffered one
		else:
			a.buffered = press
			a.buffered_tick = tick
			a.buffered_slot = slot
	elif a.buffered != "" and tick - a.buffered_tick <= int(Moves.BUFFER_TIME * Sim.HZ):
		if _try_start(a, a.buffered, it, a.buffered_slot):
			a.buffered = ""
	else:
		a.buffered = ""
	# Guard held without an action (e.g. pressed during an uncancelable recovery and still held)
	if it.guard_held and a.action == null and a.stun <= 0.0:
		_try_start(a, "guard", it)
	# Gestures and holds on the running action (MOVESET §3).
	if a.action != null and a.stun <= 0.0:
		if it.attack_gesture != Sim.Gesture.NONE and press != "attack":
			_gesture_morph(a, it)
		if it.guard_gesture == Sim.Gesture.UP or it.guard_gesture == Sim.Gesture.DOWN:
			_guard_gesture(a, it)
		if a.action != null and a.action.slot == "evade":
			_evade_hold(a, it)


## A gesture in the first MORPH_WINDOW of an attack startup, or with the release of a charge, morphs
## the running attack into the gesture's move once (elapsed time, paid Focus/heat and tier carry over).
func _gesture_morph(a: ActorState, it: ActorIntent) -> void:
	var inst := a.action
	if inst.data.get("morphed", false) or not Sim.ATTACK_SLOTS.has(inst.slot):
		return
	var slot := gesture_slot(it.attack_gesture)
	if slot == "" or slot == inst.slot:
		return
	var in_startup := inst.phase == ActionInst.P.STARTUP and inst.total <= MORPH_WINDOW + 1e-6
	var at_release := inst.phase == ActionInst.P.CHARGE
	if not in_startup and not at_release:
		return
	var id := Moves.resolve(inst.element, inst.sub, slot)
	if id == "" or id == inst.id:
		return
	morph_action(a, id, slot, it, at_release)


## Replaces the running action with `id` keeping elapsed time, paid cost and tier (gesture morphs,
## evade -> evade_hold). Kits may call it for their own morphs. Returns the new action.
func morph_action(a: ActorState, id: String, slot: String, it: ActorIntent, at_release: bool = false, extra: Dictionary = {}) -> ActionInst:
	var inst := a.action
	if inst == null or not Moves.DEFS.has(id):
		return null
	var pre := {"slot": slot, "morphed": true, "morph_from": inst.id, "element": inst.element, "sub": inst.sub,
		"paid_focus": maxf(0.0, float(inst.data.get("focus0", a.focus)) - a.focus - float(inst.data.get("heat_paid", 0.0)) / Sim.HU_PER_FOCUS) + float(inst.data.get("paid_focus", 0.0)),
		"paid_hu": float(inst.data.get("heat_paid", 0.0)), "heat_paid": float(inst.data.get("heat_paid", 0.0)),
		"tier": inst.tier(), "charge_t": float(inst.data.get("charge_t", inst.total)), "morph_body": a.held_body}
	if at_release:
		pre["released"] = true
		pre["morph_release"] = true
	pre.merge(extra, true)
	var total := inst.total
	inst.interrupted = true
	_dispatch_interrupt(a, inst, "morph")
	a.action = null
	emit("morph", {"actor": a.id, "from": inst.id, "to": id, "slot": slot, "tier": inst.tier(), "at": "release" if at_release else "startup"})
	var n := start_action(a, id, it, pre)
	if n != null and a.action == n:
		n.total = total
		n.heavy = inst.heavy
		if at_release and n.phase == ActionInst.P.STARTUP:
			_enter_after_startup(a, n, it)
	return n


## GUARD flick while a guard channels: UP = push, DOWN = sink (the guard's material, wall and tier carry over).
func _guard_gesture(a: ActorState, it: ActorIntent) -> void:
	var inst := a.action
	if inst.id != "guard" or inst.phase != ActionInst.P.CHANNEL:
		return
	var slot := "push" if it.guard_gesture == Sim.Gesture.UP else "sink"
	var id := Moves.resolve(inst.element, inst.sub, slot)
	if id == "" or not Moves.DEFS.has(id):
		return
	var pre := {"slot": slot, "from_guard": true, "guard_t": inst.total, "tier": inst.tier(), "guard_spec": inst.data.get("spec", ""),
		"wall": a.wall_body, "keep_wall": a.wall_body, "held": a.held_body, "element": inst.element, "sub": inst.sub,
		"charge_t": float(inst.data.get("charge_t", inst.total)), "charge_frozen": true}
	inst.interrupted = true
	_dispatch_interrupt(a, inst, slot)
	a.guarding = false
	a.action = null
	emit("morph", {"actor": a.id, "from": "guard", "to": id, "slot": slot, "tier": inst.tier(), "at": "guard"})
	start_action(a, id, it, pre)


func _evade_hold(a: ActorState, it: ActorIntent) -> void:
	var inst := a.action
	if inst.data.get("morphed", false) or inst.data.get("evade_released", false):
		return
	if not it.evade_held:
		inst.data["evade_released"] = true
		return
	if inst.total + 1e-6 < EVADE_HOLD_TIME:
		return
	inst.data["morphed"] = true
	var id := Moves.resolve(inst.element, inst.sub, "evade_hold")
	if id == "" or id == inst.id or not Moves.DEFS.has(id):
		return
	morph_action(a, id, "evade_hold", it)


func can_cancel(a: ActorState, into: String, slot: String = "") -> bool:
	if a.stun > 0.0:
		return false
	if a.action == null:
		return true
	var inst := a.action
	var d := inst.def
	if inst.id == "guard":
		return into != "guard"
	if inst.phase == ActionInst.P.CHANNEL or inst.phase == ActionInst.P.CHARGE:
		# Holding a technique / charge: guard and evade are explicit cancels.
		return into == "guard" or into == "evade"
	if inst.phase == ActionInst.P.RECOVERY:
		var rec: float = float(d.recovery) * Status.recovery_mult(a)
		if d.has("cancel") and (rec <= 0.0 or inst.t / rec >= float(d.cancel)) and (into == "guard" or into == "evade"):
			return true
		# Moves without these data keys keep today's cancel rules exactly (MOVESET §9.1).
		if into == "attack" and d.has("chain") and _chain_ok(a, inst, slot if slot != "" else "strike", rec):
			return true
		if (into == "tech" or into == "guard") and d.get("counter_cancel", false) and _counter_cancel_ok(a, into):
			return true
	return false


## Chain (same sub-element) from the move's `chain` fraction of recovery after contact (whiffs only
## after `cancel`); a weave (another element/sub) costs WEAVE_COST, once per string; <= CHAIN_MAX moves,
## each slot once.
func _chain_ok(a: ActorState, inst: ActionInst, slot: String, rec: float) -> bool:
	var d := inst.def
	var frac := inst.t / rec if rec > 0.0 else 1.0
	var contact: bool = inst.data.get("contact", false)
	var need := float(d.chain) if contact else float(d.get("cancel", 1.0))
	if frac < need - 1e-6:
		return false
	var ch: Dictionary = a.chain
	if int(ch.get("n", 0)) >= CHAIN_MAX or (ch.get("slots", []) as Array).has(slot):
		return false
	var weave := a.element != inst.element or a.sub() != inst.sub
	if weave:
		if ch.get("weaved", false) or not contact or a.focus < WEAVE_COST:
			return false
	ch["pending"] = {"weave": weave}
	return true


## Counter cancel into technique/guard from any recovery when a hostile body will reach the fighter
## within COUNTER_CANCEL_WINDOW and (technique) the context would reclaim/transform it.
func _counter_cancel_ok(a: ActorState, into: String) -> bool:
	if a.focus < COUNTER_CANCEL_COST or incoming_threat(a, COUNTER_CANCEL_WINDOW) == null:
		return false
	if into == "tech":
		var pv := tech_preview(a, aim_dir(a, _intent_for(a)))
		if not pv.get("ok", false):
			return false
	a.chain["counter_cancel"] = true
	return true


## The first hostile projectile that passes within 1.2 m of the fighter in the next `within` seconds.
func incoming_threat(a: ActorState, within: float) -> MatBody:
	var best: MatBody = null
	var best_t := INF
	for b in bodies:
		if not b.alive or not b.is_projectile() or b.attack_owner == a.id or b.vel.length() < 0.5:
			continue
		var rel := a.chest() - b.pos
		var v := b.vel
		var tc := clampf(rel.dot(v) / maxf(v.length_squared(), 1e-6), 0.0, within)
		if (b.pos + v * tc).distance_to(a.chest()) < 1.2 + b.radius and tc < best_t:
			best_t = tc
			best = b
	return best


func _try_start(a: ActorState, press: String, it: ActorIntent, slot: String = "") -> bool:
	if not can_cancel(a, press, slot):
		return false
	var pending: Dictionary = a.chain.get("pending", {})
	a.chain.erase("pending")
	var counter_cancel: bool = a.chain.get("counter_cancel", false)
	a.chain.erase("counter_cancel")
	if a.action != null:
		interrupt_action(a, "cancel:" + press)
	if counter_cancel:
		spend_focus(a, COUNTER_CANCEL_COST)
		emit("counter_cancel", {"actor": a.id, "into": press})
	match press:
		"evade":
			var id := Moves.resolve(a.element, a.sub(), "evade")
			if id == "" or not Moves.DEFS.has(id):
				id = "evade"
			if a.focus < float(Moves.DEFS[id].get("cost", 0.0)) * 0.5:
				emit("insufficient", {"actor": a.id, "what": "focus"})
				id = "evade"
			start_action(a, id, it, {"slot": "evade"})
		"guard":
			start_action(a, "guard", it, {"slot": "guard", "spec": Moves.resolve(a.element, a.sub(), "guard")})
		"tech":
			var tid := Moves.resolve(a.element, a.sub(), "tech")
			if tid != "" and Moves.DEFS.has(tid):
				start_action(a, tid, it, {"slot": "tech"})
		"attack":
			var sl := slot if slot != "" else "strike"
			var aid := Moves.resolve(a.element, a.sub(), sl)
			if (aid == "" or not Moves.DEFS.has(aid)) and sl != "strike":
				sl = "strike"
				aid = Moves.resolve(a.element, a.sub(), sl)
			if aid == "" or not Moves.DEFS.has(aid):
				return true
			if pending.is_empty():
				a.chain = {"n": 1, "slots": [sl], "weaved": false}
			else:
				a.chain.n = int(a.chain.get("n", 0)) + 1
				(a.chain.slots as Array).append(sl)
				if pending.get("weave", false):
					a.chain.weaved = true
					spend_focus(a, WEAVE_COST)
					emit("weave", {"actor": a.id, "element": a.element, "sub": a.sub()})
				emit("chain", {"actor": a.id, "n": a.chain.n, "slot": sl})
			start_action(a, aid, it, {"slot": sl})
	return true


func start_action(a: ActorState, id: String, it: ActorIntent, pre: Dictionary = {}) -> ActionInst:
	var inst := ActionInst.new()
	inst.id = id
	inst.def = Moves.DEFS[id]
	inst.element = int(pre.get("element", inst.def.get("element", a.element)))
	inst.sub = int(pre.get("sub", a.sub_of(inst.element)))
	inst.slot = String(pre.get("slot", ""))
	inst.attack_id = new_attack_id()
	inst.data.merge(pre, true)
	if not inst.data.has("focus0"):
		inst.data["focus0"] = a.focus
		inst.data["reserve0"] = a.heat_reserve
	if pre.has("spec") and id == "guard":
		var sp := String(pre.spec)
		inst.data["spec_def"] = Moves.DEFS.get(sp, {}) if sp != "" else {}
		inst.data["spec_module"] = String(inst.data.spec_def.get("module", "common")) if sp != id else "common"
	a.action = inst
	a.attack_hold = 0.0
	emit("action", {"actor": a.id, "move": id, "phase": "startup", "sub": inst.sub, "slot": inst.slot, "tier": inst.tier()})
	_dispatch_start(a, inst, it)
	if a.action == inst and inst.phase == ActionInst.P.STARTUP and float(inst.def.startup) <= 0.0:
		_enter_after_startup(a, inst, it)
	return inst


func set_phase(a: ActorState, inst: ActionInst, p: int) -> void:
	inst.phase = p
	inst.t = 0.0
	emit("action", {"actor": a.id, "move": inst.id, "phase": inst.phase_name(), "heavy": inst.heavy,
		"sub": inst.sub, "slot": inst.slot, "tier": inst.tier()})
	if p == ActionInst.P.DONE:
		if a.action == inst:
			a.action = null
		return
	_dispatch_phase(a, inst, p)


func finish_action(a: ActorState, inst: ActionInst) -> void:
	set_phase(a, inst, ActionInst.P.DONE)


func interrupt_action(a: ActorState, reason: String) -> void:
	var inst := a.action
	if inst == null:
		return
	inst.interrupted = true
	_dispatch_interrupt(a, inst, reason)
	a.guarding = false
	a.gliding = false
	emit("interrupt", {"actor": a.id, "move": inst.id, "reason": reason})
	a.action = null


func _enter_after_startup(a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var nxt: int = _dispatch_after_startup(a, inst, it)
	if a.action == inst and nxt != ActionInst.P.STARTUP:
		set_phase(a, inst, nxt)


func _tick_action(a: ActorState, it: ActorIntent) -> void:
	var inst := a.action
	if inst == null:
		return
	inst.t += Sim.DT
	inst.total += Sim.DT
	if it.attack_held:
		a.attack_hold += Sim.DT
	var d := inst.def
	match inst.phase:
		ActionInst.P.STARTUP:
			_dispatch_tick(a, inst, it)
			if a.action == inst and inst.phase == ActionInst.P.STARTUP and inst.total >= _startup_of(a, inst) - 1e-6:
				_enter_after_startup(a, inst, it)
		ActionInst.P.CHARGE, ActionInst.P.CHANNEL:
			Charge.tick(self, a, inst, it)
			if a.action == inst:
				_dispatch_tick(a, inst, it)
		ActionInst.P.ACTIVE:
			_dispatch_tick(a, inst, it)
			if a.action == inst and inst.phase == ActionInst.P.ACTIVE and inst.t >= _active_of(inst):
				set_phase(a, inst, ActionInst.P.RECOVERY)
		ActionInst.P.RECOVERY:
			_dispatch_tick(a, inst, it)
			if a.action == inst and inst.phase == ActionInst.P.RECOVERY and inst.t >= float(d.recovery) * Status.recovery_mult(a):
				finish_action(a, inst)


func _startup_of(_a: ActorState, inst: ActionInst) -> float:
	return float(inst.data.get("startup", inst.def.startup))


func _active_of(inst: ActionInst) -> float:
	return float(inst.data.get("active", inst.def.active))


# ---- module dispatch (explicit, no reflection) -------------------------------
# Modules: legacy common/earth/water/fire/air, the generic "verbs", and one per element kit.

func module_start(module: String, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match module:
		"common": ActCommon.on_start(self, a, inst, it)
		"earth": ActEarth.on_start(self, a, inst, it)
		"water": ActWater.on_start(self, a, inst, it)
		"fire": ActFire.on_start(self, a, inst, it)
		"air": ActAir.on_start(self, a, inst, it)
		"verbs": Verbs.on_start(self, a, inst, it)
		"kit_earth": KitEarth.on_start(self, a, inst, it)
		"kit_water": KitWater.on_start(self, a, inst, it)
		"kit_fire": KitFire.on_start(self, a, inst, it)
		"kit_air": KitAir.on_start(self, a, inst, it)


func module_after_startup(module: String, a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	match module:
		"common": return ActCommon.after_startup(self, a, inst, it)
		"earth": return ActEarth.after_startup(self, a, inst, it)
		"water": return ActWater.after_startup(self, a, inst, it)
		"fire": return ActFire.after_startup(self, a, inst, it)
		"air": return ActAir.after_startup(self, a, inst, it)
		"verbs": return Verbs.after_startup(self, a, inst, it)
		"kit_earth": return KitEarth.after_startup(self, a, inst, it)
		"kit_water": return KitWater.after_startup(self, a, inst, it)
		"kit_fire": return KitFire.after_startup(self, a, inst, it)
		"kit_air": return KitAir.after_startup(self, a, inst, it)
	return ActionInst.P.ACTIVE


func module_phase(module: String, a: ActorState, inst: ActionInst, p: int) -> void:
	match module:
		"common": ActCommon.on_phase(self, a, inst, p)
		"earth": ActEarth.on_phase(self, a, inst, p)
		"water": ActWater.on_phase(self, a, inst, p)
		"fire": ActFire.on_phase(self, a, inst, p)
		"air": ActAir.on_phase(self, a, inst, p)
		"verbs": Verbs.on_phase(self, a, inst, p)
		"kit_earth": KitEarth.on_phase(self, a, inst, p)
		"kit_water": KitWater.on_phase(self, a, inst, p)
		"kit_fire": KitFire.on_phase(self, a, inst, p)
		"kit_air": KitAir.on_phase(self, a, inst, p)


func module_tick(module: String, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match module:
		"common": ActCommon.on_tick(self, a, inst, it)
		"earth": ActEarth.on_tick(self, a, inst, it)
		"water": ActWater.on_tick(self, a, inst, it)
		"fire": ActFire.on_tick(self, a, inst, it)
		"air": ActAir.on_tick(self, a, inst, it)
		"verbs": Verbs.on_tick(self, a, inst, it)
		"kit_earth": KitEarth.on_tick(self, a, inst, it)
		"kit_water": KitWater.on_tick(self, a, inst, it)
		"kit_fire": KitFire.on_tick(self, a, inst, it)
		"kit_air": KitAir.on_tick(self, a, inst, it)


func module_interrupt(module: String, a: ActorState, inst: ActionInst, reason: String) -> void:
	match module:
		"common": ActCommon.on_interrupt(self, a, inst, reason)
		"earth": ActEarth.on_interrupt(self, a, inst, reason)
		"water": ActWater.on_interrupt(self, a, inst, reason)
		"fire": ActFire.on_interrupt(self, a, inst, reason)
		"air": ActAir.on_interrupt(self, a, inst, reason)
		"verbs": Verbs.on_interrupt(self, a, inst, reason)
		"kit_earth": KitEarth.on_interrupt(self, a, inst, reason)
		"kit_water": KitWater.on_interrupt(self, a, inst, reason)
		"kit_fire": KitFire.on_interrupt(self, a, inst, reason)
		"kit_air": KitAir.on_interrupt(self, a, inst, reason)


func _dispatch_start(a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	module_start(String(inst.def.module), a, inst, it)


func _dispatch_after_startup(a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	return module_after_startup(String(inst.def.module), a, inst, it)


func _dispatch_phase(a: ActorState, inst: ActionInst, p: int) -> void:
	module_phase(String(inst.def.module), a, inst, p)


func _dispatch_tick(a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	module_tick(String(inst.def.module), a, inst, it)


func _dispatch_interrupt(a: ActorState, inst: ActionInst, reason: String) -> void:
	module_interrupt(String(inst.def.module), a, inst, reason)


## Shared tap/hold resolution for attack buttons. Returns the phase that follows startup.
func attack_after_startup(a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	var decided_light: bool = not it.attack_held or inst.data.get("released", false)
	if decided_light:
		inst.heavy = false
		return ActionInst.P.ACTIVE
	if inst.total < Moves.HOLD_THRESHOLD - 1e-6:
		# Not decided yet: stay in startup until release or the hold threshold.
		inst.data["startup"] = Moves.HOLD_THRESHOLD
		return ActionInst.P.STARTUP
	inst.heavy = true
	return ActionInst.P.CHARGE


# ============================================================== actor movement

func _move_actor(a: ActorState, it: ActorIntent) -> void:
	var dt := Sim.DT
	var inst := a.action
	var want := Vector3.ZERO
	var speed_scale := 1.0
	var turn_scale := 1.0
	var controlled_motion := false
	if a.stun > 0.0:
		speed_scale = 0.0
		turn_scale = 0.0
	elif inst != null:
		match inst.phase:
			ActionInst.P.STARTUP:
				speed_scale = float(inst.def.get("move_startup", 0.25))
				turn_scale = 0.6
			ActionInst.P.CHARGE, ActionInst.P.CHANNEL:
				speed_scale = float(inst.data.get("move_scale", inst.def.get("move_channel", 0.45)))
				turn_scale = 0.8
			ActionInst.P.ACTIVE:
				speed_scale = float(inst.data.get("move_scale", 0.0)) if inst.data.has("move_scale") else 0.0
				turn_scale = 0.0 if speed_scale <= 0.0 else 0.8
			ActionInst.P.RECOVERY:
				speed_scale = 0.25
				turn_scale = 0.3
		if inst.id == "guard":
			speed_scale = float(inst.data.get("spec_def", {}).get("move_channel", 0.35))
			turn_scale = 1.0
		if inst.data.get("controls_motion", false):
			controlled_motion = true
	if a.in_water:
		speed_scale *= 0.7
	if not a.status.is_empty():
		speed_scale *= Status.speed_mult(a)
		if Status.rooted(a):
			speed_scale = 0.0
	var mv := it.move
	mv.y = 0.0
	if mv.length() > 1.0:
		mv = mv.normalized()
	# Two clear gaits (matches the authored walk/run strides, no slow-motion runs):
	# stick < RUN_STICK walks up to WALK_MAX, beyond it runs from RUN_MIN to RUN_SPEED.
	var m := mv.length()
	var gait_speed := 0.0
	if m > 0.05:
		gait_speed = lerpf(0.0, WALK_MAX, m / RUN_STICK) if m < RUN_STICK else lerpf(RUN_MIN, RUN_SPEED, (m - RUN_STICK) / (1.0 - RUN_STICK))
	var running := m >= RUN_STICK and inst == null and a.stun <= 0.0
	want = (mv / maxf(m, 1e-4)) * gait_speed * speed_scale
	if a.gliding:
		var gs: float = float(inst.data.get("glide_speed", Moves.DEFS.air_tech.glide_speed)) if inst != null else float(Moves.DEFS.air_tech.glide_speed)
		want = mv * gs
	# Modes (verbs: flight / hover) hold the fighter at a height above the ground.
	var hover := inst != null and inst.data.has("hover_height") and a.stun <= 0.0
	var hv := Vector3(a.vel.x, 0.0, a.vel.z)
	if not controlled_motion:
		var air_ctl := 1.0 if a.grounded else 0.35
		if a.gliding or hover:
			air_ctl = 0.8
		var rate := (ACCEL if want.length() > hv.length() else DECEL) * air_ctl
		if a.stun > 0.0:
			rate = 9.0  # knockback slides out
		var fr := _friction_at(a)
		if fr != 1.0:
			rate *= fr
		hv = hv.move_toward(want, rate * dt)
		a.vel.x = hv.x
		a.vel.z = hv.z
	# Facing: lock target while fighting, else movement direction.
	# Facing: actions face their aim; running faces the run direction; otherwise
	# (walking, guarding) face the locked target and strafe.
	var face_dir := Vector3.ZERO
	var tgt := get_actor(a.lock_target)
	if running:
		face_dir = mv
	elif tgt != null and a.pos.distance_to(tgt.pos) < 22.0:
		face_dir = tgt.pos - a.pos
	elif mv.length() > 0.1:
		face_dir = mv
	if inst != null and inst.data.has("face"):
		face_dir = inst.data.face
		if inst.phase == ActionInst.P.STARTUP:
			turn_scale = maxf(turn_scale, 1.4)   # commit toward the aim during anticipation
	face_dir.y = 0.0
	if face_dir.length() > 0.01 and turn_scale > 0.0:
		var target_yaw := atan2(face_dir.x, face_dir.z)
		var diff := wrapf(target_yaw - a.facing, -PI, PI)
		var max_turn := TURN_RATE * turn_scale * dt
		a.facing = wrapf(a.facing + clampf(diff, -max_turn, max_turn), -PI, PI)
	# Vertical
	if hover:
		var hg := _ground_under(a.pos, a.pos.y + 3.0)
		var target_y := hg + float(inst.data.hover_height)
		a.grounded = false
		a.vel.y = clampf((target_y - a.pos.y) * 6.0, -4.0, 6.0)
	elif not a.grounded:
		a.vel.y -= Sim.GRAVITY * dt
		if a.gliding:
			var gf: float = float(inst.data.get("glide_fall", Moves.DEFS.air_tech.glide_fall)) if inst != null else float(Moves.DEFS.air_tech.glide_fall)
			a.vel.y = maxf(a.vel.y, -gf)
	var np := a.pos + a.vel * dt
	np = arena.push_out(np, Sim.ACTOR_RADIUS)
	np = _push_out_walls(np, Sim.ACTOR_RADIUS)
	var g := _ground_under(np, a.pos.y)
	if np.y <= g + 0.001 and a.vel.y <= 0.0:
		if not a.grounded:
			a.grounded = true
			a.gliding = false
			emit("land", {"actor": a.id, "speed": -a.vel.y})
		np.y = g
		a.vel.y = 0.0
	elif np.y > g + 0.05:
		if a.grounded and a.vel.y <= 0.0 and np.y - g < Sim.STEP_HEIGHT + 0.05 and g >= a.ground_y - Sim.STEP_HEIGHT:
			np.y = g  # step down small drops while walking
		else:
			a.grounded = false
	a.ground_y = g
	var lim := arena.half_size - 0.5
	np.x = clampf(np.x, -lim, lim)
	np.z = clampf(np.z, -lim, lim)
	a.pos = np
	_update_surface(a)


## Walkable height under p: the arena, raised by ZONE surfaces that declare props.walk_height
## (e.g. an ice floor over the pool is walkable at pool level).
func _ground_under(p: Vector3, from_y: float) -> float:
	var g := arena.ground_height(p.x, p.z, from_y)
	for z in bodies:
		if z.alive and z.form == Sim.Form.ZONE and z.props.has("walk_height"):
			if Vector2(p.x - z.pos.x, p.z - z.pos.z).length() <= z.zone_radius:
				var top := z.pos.y + float(z.props.walk_height)
				if top > g and top <= from_y + Sim.STEP_HEIGHT:
					g = top
	return g


## The ZONE surface under a grounded fighter (or null).
func zone_surface_at(p: Vector3) -> MatBody:
	for z in bodies:
		if z.alive and z.form == Sim.Form.ZONE and (z.props.has("walk_height") or z.props.has("friction")):
			if Vector2(p.x - z.pos.x, p.z - z.pos.z).length() <= z.zone_radius and absf(p.y - (z.pos.y + float(z.props.get("walk_height", 0.0)))) < 0.35:
				return z
	return null


## Acceleration multiplier from statuses (slick, muddy) and the surface zone (ice floor 0.1, mud 1.3).
func _friction_at(a: ActorState) -> float:
	var f := 1.0
	if not a.status.is_empty():
		f *= Status.friction_mult(a)
	if a.grounded and a.surface.begins_with("zone:"):
		var z := zone_surface_at(a.pos)
		if z != null:
			f *= float(z.props.get("friction", 1.0))
	return f


func _update_surface(a: ActorState) -> void:
	var zs: MatBody = null
	if a.grounded:
		zs = zone_surface_at(a.pos)
	a.in_water = arena.in_pool(a.pos.x, a.pos.z) and a.pos.y < arena.pool_level and a.grounded and zs == null
	if a.in_water:
		a.wetness = 1.0
		var take := minf(6.0 - a.water_carried, pool.mass)
		if take > 0.0:
			# The waterskin holds water at ambient: the heat the drawn kg carried leaves the ledger.
			ledger.removed += take * (Sim.WATER_C * (pool.temp - Sim.AMBIENT_C) - Sim.WATER_LATENT_FUSION * (1.0 - pool.liquid))
			a.water_carried += take
			pool.mass -= take
	a.surface = arena.surface_at(a.pos.x, a.pos.z, a.pos.y)
	if zs != null:
		a.surface = "zone:" + String(zs.props.get("surface", zs.tag))
		return
	if a.surface == "stone" and a.grounded:
		var pd := puddle_at(a.pos)
		if pd != null:
			a.surface = "puddle"
			a.wetness = maxf(a.wetness, 0.5)


func _separate_actors() -> void:
	for i in actors.size():
		for j in range(i + 1, actors.size()):
			var a := actors[i]
			var b := actors[j]
			var d := Vector3(b.pos.x - a.pos.x, 0, b.pos.z - a.pos.z)
			var l := d.length()
			var minl := Sim.ACTOR_RADIUS * 2.0
			if l < minl and absf(a.pos.y - b.pos.y) < 1.5:
				var n := d / l if l > 1e-4 else Vector3(1, 0, 0)
				var push := (minl - l) * 0.5
				a.pos -= n * push
				b.pos += n * push


func _push_out_walls(p: Vector3, r: float) -> Vector3:
	for b in bodies:
		if b.alive and b.form == Sim.Form.WALL and b.wall_rise > 0.3:
			p = _push_out_obb(p, r, b)
	return p


func _push_out_obb(p: Vector3, r: float, w: MatBody) -> Vector3:
	var c := cos(w.wall_yaw)
	var s := sin(w.wall_yaw)
	var rel := p - w.pos
	# local: x along wall, z through wall
	var lx := rel.x * c - rel.z * s
	var lz := rel.x * s + rel.z * c
	var hx := w.wall_half.x
	var hz := w.wall_half.z
	var cx := clampf(lx, -hx, hx)
	var cz := clampf(lz, -hz, hz)
	var dx := lx - cx
	var dz := lz - cz
	var d2 := dx * dx + dz * dz
	if d2 >= r * r:
		return p
	if d2 > 1e-8:
		var d := sqrt(d2)
		lx = cx + dx / d * r
		lz = cz + dz / d * r
	else:
		lz = (hz + r) * (1.0 if lz >= 0.0 else -1.0)
	return w.pos + Vector3(lx * c + lz * s, rel.y, -lx * s + lz * c)


func point_in_wall(p: Vector3, w: MatBody, pad: float = 0.0) -> bool:
	var c := cos(w.wall_yaw)
	var s := sin(w.wall_yaw)
	var rel := p - w.pos
	var lx := rel.x * c - rel.z * s
	var lz := rel.x * s + rel.z * c
	return absf(lx) <= w.wall_half.x + pad and absf(lz) <= w.wall_half.z + pad and rel.y >= -0.2 and rel.y <= w.wall_half.y * 2.0 * w.wall_rise + pad


# ============================================================== targeting

func _valid_target(a: ActorState, id: int) -> bool:
	var t := get_actor(id)
	if t == null or t.team == a.team or t.health <= 0.0:
		return false
	if not t.status.is_empty() or not a.status.is_empty():
		return _lockable(a, t)
	return true


## Statuses: a blinded fighter can't lock on; a concealed one can only be locked within 2 m.
func _lockable(a: ActorState, t: ActorState) -> bool:
	if Status.lock_blocked(a):
		return false
	if Status.hidden(t) and a.pos.distance_to(t.pos) > Status.HIDDEN_RANGE:
		return false
	return true


func _auto_target(a: ActorState) -> int:
	var best := -1
	var bd := INF
	for t in actors:
		if t.team == a.team or t.health <= 0.0:
			continue
		if (not t.status.is_empty() or not a.status.is_empty()) and not _lockable(a, t):
			continue
		var d := a.pos.distance_to(t.pos)
		if d < bd:
			bd = d
			best = t.id
	return best


func _cycle_target(a: ActorState) -> void:
	var ids: Array[int] = []
	for t in actors:
		if t.team != a.team and t.health > 0.0:
			ids.append(t.id)
	if ids.is_empty():
		return
	var i := ids.find(a.lock_target)
	a.lock_target = ids[(i + 1) % ids.size()]
	emit("target", {"actor": a.id, "target": a.lock_target})


## World-space aim direction for an action: explicit drag aim, else lock target, else facing.
func aim_dir(a: ActorState, it: ActorIntent) -> Vector3:
	if it.aim_active and it.aim_dir.length() > 0.1:
		return Vector3(it.aim_dir.x, 0, it.aim_dir.z).normalized()
	var t := get_actor(a.lock_target)
	if t != null:
		var d := t.pos - a.pos
		d.y = 0
		if d.length() > 0.1:
			return d.normalized()
	return a.forward()


func aim_point(a: ActorState, it: ActorIntent) -> Vector3:
	var t := get_actor(a.lock_target)
	if t != null and not it.aim_active:
		return t.chest()
	return a.chest() + aim_dir(a, it) * 12.0


## Best body for a verb inside a cone in front of the actor. Incoming projectiles
## are preferred, then the closest. Deterministic tie-break by id.
func find_body(a: ActorState, dir: Vector3, reach: float, cone_deg: float, filter: Callable) -> MatBody:
	var best: MatBody = null
	var best_score := INF
	var cos_lim := cos(deg_to_rad(cone_deg))
	for b in bodies:
		if not b.alive or not filter.call(b):
			continue
		var to := b.pos - a.chest()
		var flat := Vector3(to.x, 0, to.z)
		var d := flat.length()
		if d > reach:
			continue
		if d > 0.6 and flat.normalized().dot(dir) < cos_lim:
			continue
		var score := d
		if b.is_projectile() and b.attack_owner != a.id and b.vel.dot(-to) > 0.0:
			score -= 6.0
		if score < best_score - 1e-6 or (absf(score - best_score) <= 1e-6 and best != null and b.id < best.id):
			best = b
			best_score = score
	return best


func puddle_at(p: Vector3) -> MatBody:
	for b in bodies:
		if b.alive and b.form == Sim.Form.PUDDLE:
			if Vector2(p.x - b.pos.x, p.z - b.pos.z).length() < b.radius and absf(p.y - b.pos.y) < 0.3:
				return b
	return null


# ============================================================== resources

## Pays heat for an action: reserve first, then Focus at HU_PER_FOCUS. Returns HU paid.
func pay_heat(a: ActorState, hu: float, allow_partial: bool = true) -> float:
	var from_res := minf(a.heat_reserve, hu)
	var rest := hu - from_res
	var focus_need := rest / Sim.HU_PER_FOCUS
	if focus_need > a.focus + 1e-6:
		if not allow_partial:
			return 0.0
		focus_need = a.focus
		rest = focus_need * Sim.HU_PER_FOCUS
	a.heat_reserve -= from_res
	spend_focus(a, focus_need)
	ledger.generated += rest
	return from_res + rest


func can_pay_heat(a: ActorState, hu: float) -> bool:
	return a.heat_reserve + a.focus * Sim.HU_PER_FOCUS >= hu - 1e-6


func spend_focus(a: ActorState, f: float) -> bool:
	if f <= 0.0:
		return true
	if a.focus + 1e-6 < f:
		return false
	a.focus = maxf(0.0, a.focus - f)
	a.focus_idle = 0.0
	return true


func _regen(a: ActorState) -> void:
	a.focus_idle += Sim.DT
	a.balance_idle += Sim.DT
	if a.focus_idle > Sim.FOCUS_REGEN_DELAY and a.action == null:
		a.focus = minf(Sim.FOCUS_MAX, a.focus + Sim.FOCUS_REGEN * Sim.DT)
	elif a.focus_idle > Sim.FOCUS_REGEN_DELAY:
		a.focus = minf(Sim.FOCUS_MAX, a.focus + Sim.FOCUS_REGEN * 0.4 * Sim.DT)
	if a.balance_idle > Sim.BALANCE_REGEN_DELAY:
		a.balance = minf(Sim.BALANCE_MAX, a.balance + Sim.BALANCE_REGEN * Sim.DT)
	if a.heat_reserve > 0.0:
		var d := minf(a.heat_reserve, Sim.RESERVE_DISSIPATE * Sim.DT)
		a.heat_reserve -= d
		ledger.reserve_dissipated += d


# ============================================================== hits

## Applies a hit to an actor. info: attacker, attack_id, damage, balance, knock (Vector3),
## kind, from (Vector3). Returns "dup", "evaded", "perfect", "block", "hit", "knockdown".
func hit_actor(t: ActorState, info: Dictionary) -> String:
	var aid: int = info.attack_id
	if t.hits_taken.has(aid):
		return "dup"
	t.hits_taken[aid] = tick
	_mark_contact(info)
	var from: Vector3 = info.get("from", t.pos + t.forward())
	var to_src := from - t.pos
	to_src.y = 0
	var facing_ok := to_src.length() < 0.01 or t.forward().dot(to_src.normalized()) > -0.15
	var dmg: float = info.damage
	var bal: float = info.balance
	var kind: String = info.get("kind", "")
	if t.iframes > 0.0:
		emit("evaded", {"actor": t.id, "attack": aid, "kind": kind})
		t.last_result = "evaded"
		return "evaded"
	var agent: Agent = null
	var knock_scale := 1.0
	if t.guarding and not info.get("unblockable", false):
		# The guard is a counter: the rule cell (threat class x guard class) decides (Interactions).
		agent = Agent.of_hit(self, info)
		if info.has("facing_vel") and not bool(info.facing_vel) and perfect_guard(t):
			# Legacy: a perfect guard turns a projectile only when it faces the incoming velocity;
			# otherwise the hit is answered like a melee hit (no body redirect).
			var vi := info.duplicate()
			vi.erase("agent")
			vi.erase("body")
			agent = Agent.of_hit(self, vi)
		var counter := Agent.of_guard(self, t)
		var rl := Interactions.rule(agent.cls, counter.ccls, counter.tier)
		var gface := facing_ok or (counter.perfect and bool(info.get("facing_vel", false)))
		if gface or rl.get("aura", false):
			var res := Interactions.resolve(self, agent, counter, {"info": info, "target": t})
			if String(res.result) != "":
				return String(res.result)
			# pass / weaken / overwhelm: what is left of the threat lands.
			dmg *= float(res.pass_scale)
			bal *= float(res.pass_scale)
			knock_scale = float(res.pass_scale)
	if kind == "lightning" and t.wetness > 0.3:
		dmg *= 1.5
	var knock: Vector3 = info.get("knock", Vector3.ZERO) * knock_scale
	if t.anchored or not t.status.is_empty() or t.armor > 0.0:
		if agent == null:
			agent = Agent.of_hit(self, info)
		if Interactions.family(agent.cls) == &"pressure" and (t.anchored or t.status.has("anchored")):
			var sres := Interactions.resolve(self, agent, Agent.of_stance(self, t), {"info": info})
			knock *= float(sres.knock_scale)
		if agent.ch.K > 0.0 or kind == "stone" or kind == "metal":
			dmg *= 1.0 - Status.armor(t)
		if Status.immune(t, "knockback"):
			knock = Vector3(0.0, knock.y, 0.0)
		if Status.immune(t, "lift"):
			knock.y = minf(knock.y, 0.0)
	t.health = maxf(0.0, t.health - dmg)
	t.balance -= bal
	t.balance_idle = 0.0
	t.vel += knock
	if knock.y > 0.0:
		t.grounded = false
	t.last_hit_dir = -to_src.normalized() if to_src.length() > 0.01 else -t.forward()
	var res2 := "hit"
	# Armor stances (Stone Skin, Iron Stance, rooted / anchored modes) take the hit without flinching:
	# damage and balance still land, only a knockdown (balance broken) ends the stance.
	var armored := t.stance != "" and (t.armor > 0.0 or t.anchored) and t.balance > 0.0
	if t.balance <= 0.0:
		_stagger(t, "knockdown", 1.1, info)
		t.balance = 45.0
		res2 = "knockdown"
	elif armored:
		pass   # the hit event carries armored = true (views play a hit-react without interrupting)
	elif bal >= 25.0:
		_stagger(t, "heavy", 0.5, info)
	else:
		_stagger(t, "light", 0.26, info)
	var ev := {"actor": t.id, "attacker": info.get("attacker", -1), "damage": dmg, "kind": kind,
		"result": res2, "body": info.get("body", -1), "armored": armored and res2 == "hit"}
	ev.merge(_hit_meta(info, agent, t), false)
	emit("hit", ev)
	t.last_result = res2
	return res2


## power / mat / tier / dir for hit and block events (MOVESET §15.6).
func _hit_meta(info: Dictionary, agent: Agent, t: ActorState) -> Dictionary:
	var g := agent
	if g == null and info.get("agent") is Agent:
		g = info.agent
	var d: Vector3 = info.get("knock", Vector3.ZERO)
	if d.length() < 1e-4:
		d = t.pos - Vector3(info.get("from", t.pos))
	d.y = 0.0
	var mat := String(info.get("mat", ""))
	var power := float(info.get("power", info.get("damage", 0.0)))
	var tier := int(info.get("tier", 0))
	if g != null:
		power = g.total()
		tier = maxi(tier, g.tier)
		if mat == "" and g.body != null:
			mat = FxEvents.mat_of(g.body)
	if mat == "":
		mat = String(MAT_OF_KIND.get(String(info.get("kind", "")), ""))
	return {"power": power, "mat": mat, "tier": tier, "dir": d.normalized() if d.length() > 1e-4 else Vector3.ZERO}


const MAT_OF_KIND := {"stone": "stone", "water": "water", "lava": "magma", "fire": "flame", "air": "wind", "lightning": "lightning",
	"blast": "blast", "sound": "sound", "ice": "ice", "sand": "sand", "steam": "steam", "vacuum": "vacuum", "metal": "metal",
	"plant": "plant", "blue": "blue", "frost": "ice"}


## Chain windows (MOVESET §9.1) open on contact: the attacker's running action made contact.
func _mark_contact(info: Dictionary) -> void:
	var att := get_actor(int(info.get("attacker", -1)))
	if att == null or att.action == null:
		return
	var aid := int(info.get("attack_id", 0))
	if att.action.attack_id == aid or int(info.get("src_attack", -1)) == att.action.attack_id:
		att.action.data["contact"] = true


## Plain guard block (legacy numbers from the rule: chip 12 % damage, 55 % balance, 35 % knock);
## guard break at 0 balance. A rule with chip/bal/knock all 0 is a clean block (e.g. air guard vs flare).
func guard_chip(t: ActorState, info: Dictionary, rule: Dictionary, threat: Agent = null) -> String:
	var kind := String(rule.get("kind", info.get("kind", "")))
	var chip := float(rule.get("chip", 0.12))
	var balm := float(rule.get("bal", 0.55))
	var kn := float(rule.get("knock", 0.35))
	var ev := {"actor": t.id, "attacker": info.get("attacker", -1), "kind": kind}
	ev.merge(_hit_meta(info, threat, t), false)
	if chip == 0.0 and balm == 0.0 and kn == 0.0:
		emit("block", ev)
		return "block"
	var dmg: float = info.get("damage", 0.0)
	var bal: float = info.get("balance", 0.0)
	t.health -= dmg * chip
	t.balance -= bal * balm
	t.balance_idle = 0.0
	var kb: Vector3 = info.get("knock", Vector3.ZERO)
	t.vel += kb * kn
	emit("block", ev)
	t.last_result = "block"
	if t.balance <= 0.0:
		_stagger(t, "guard_break", 0.7, info)
		t.balance = 35.0
		return "guard_break"
	return "block"


func _stagger(t: ActorState, kind: String, dur: float, _info: Dictionary) -> void:
	if t.action != null:
		interrupt_action(t, "hit")
	t.guarding = false
	t.gliding = false
	t.stun = maxf(t.stun, dur)
	t.stun_kind = kind
	t.buffered = ""
	emit("stagger", {"actor": t.id, "kind": kind})


func perfect_guard(t: ActorState) -> bool:
	if not t.guarding or t.guard_tick < 0:
		return false
	if t.action != null and t.action.data.get("mashed", false):
		return false
	return float(tick - t.guard_tick) * Sim.DT <= Moves.PERFECT_WINDOW


## Element whose guard rules apply: a running guard keeps the element it started with
## (switching mid-guard only affects the next action).
func guard_element(a: ActorState) -> int:
	if a.action != null and a.action.id == "guard":
		return a.action.element
	return a.element


## Cone query helper for melee-range elemental strikes.
func actors_in_cone(a: ActorState, dir: Vector3, rng_m: float, cone_deg: float) -> Array[ActorState]:
	var out: Array[ActorState] = []
	var cos_lim := cos(deg_to_rad(cone_deg))
	for t in actors:
		if t == a or t.team == a.team or t.health <= 0.0:
			continue
		var to := t.pos - a.pos
		to.y = 0
		var d := to.length()
		if d > rng_m + Sim.ACTOR_RADIUS:
			continue
		if d > 0.5 and to.normalized().dot(dir) < cos_lim:
			continue
		if not arena.has_los(a.chest(), t.chest()):
			continue
		if _wall_between(a.chest(), t.chest()):
			continue
		out.append(t)
	return out


func _wall_between(p0: Vector3, p1: Vector3) -> bool:
	return wall_hit(p0, p1) >= 0.0


## Exact segment test against raised earth walls (oriented boxes). Returns t in 0..1 or -1.
func wall_hit(p0: Vector3, p1: Vector3) -> float:
	var best := -1.0
	for b in bodies:
		if not b.alive or b.form != Sim.Form.WALL or b.wall_rise <= 0.5:
			continue
		var t := wall_segment_t(p0, p1, b)
		if t >= 0.0 and (best < 0.0 or t < best):
			best = t
	return best


## Segment p0->p1 against one wall's oriented box: t in 0..1 or -1.
func wall_segment_t(p0: Vector3, p1: Vector3, b: MatBody) -> float:
	var c := cos(b.wall_yaw)
	var s := sin(b.wall_yaw)
	var r0 := p0 - b.pos
	var r1 := p1 - b.pos
	var l0 := Vector3(r0.x * c - r0.z * s, r0.y, r0.x * s + r0.z * c)
	var l1 := Vector3(r1.x * c - r1.z * s, r1.y, r1.x * s + r1.z * c)
	var mn := Vector3(-b.wall_half.x, -0.2, -b.wall_half.z)
	var mx := Vector3(b.wall_half.x, b.wall_half.y * 2.0 * b.wall_rise, b.wall_half.z)
	return ArenaMap._slab(l0, l1 - l0, mn, mx)


func los(p0: Vector3, p1: Vector3) -> bool:
	return arena.has_los(p0, p1) and not _wall_between(p0, p1)


# ============================================================== grips / control

## Queue a control attempt; resolved after all actors acted this tick.
func request_grip(a: ActorState, b: MatBody, strength: float, verb: String) -> void:
	if b.mass > a.max_control_mass:
		emit("control_fail", {"actor": a.id, "body": b.id, "reason": "mass", "mass": b.mass})
		return
	_grips.append({"actor": a.id, "body": b.id, "strength": strength, "verb": verb, "inst": a.action})


func grip_strength(a: ActorState, b: MatBody, base: float, reach: float) -> float:
	var d := a.chest().distance_to(b.pos)
	var falloff := 1.0 - 0.45 * clampf((d - 2.0) / maxf(reach - 2.0, 0.1), 0.0, 1.0)
	var mass_pen := 0.35 * clampf(b.mass / a.max_control_mass, 0.0, 1.0)
	var focus_pen := 0.0 if a.focus > 5.0 else 0.3
	return maxf(0.0, base * falloff - mass_pen - focus_pen)


func _resolve_grips() -> void:
	if _grips.is_empty():
		return
	_grips.sort_custom(func(x, y):
		if x.body != y.body:
			return x.body < y.body
		if not is_equal_approx(x.strength, y.strength):
			return x.strength > y.strength
		return x.actor < y.actor)
	var done := {}
	for g in _grips:
		var b := get_body(g.body)
		var a := get_actor(g.actor)
		if b == null or not b.alive or a == null:
			continue
		# The request is withdrawn unless the action that reached is still channelling: a whiff,
		# release or cancel later in the tick, or a hit from a later actor, never leaves an orphan hold.
		if a.stun > 0.0 or a.action == null or a.action != g.inst or a.action.phase != ActionInst.P.CHANNEL:
			continue
		if done.has(b.id):
			if b.controller != a.id:
				emit("control_fail", {"actor": a.id, "body": b.id, "reason": "contest"})
			continue
		done[b.id] = true
		if b.controller == a.id:
			b.authority = g.strength
			continue
		var holder := 0.0
		if b.controller >= 0:
			holder = b.authority
		elif b.residual_owner >= 0 and b.residual_owner != a.id:
			holder = b.residual_authority
		if g.strength > holder + (GRIP_MARGIN if holder > 0.0 else 0.0) and g.strength > 0.05:
			var prev := b.controller
			if prev >= 0:
				var pa := get_actor(prev)
				if pa != null:
					pa.held_body = -1
					emit("control_lost", {"actor": prev, "body": b.id, "reason": "contest", "by": a.id})
			take_control(a, b, g.strength, g.verb)
		else:
			emit("control_fail", {"actor": a.id, "body": b.id, "reason": "contest"})
	_grips.clear()


func take_control(a: ActorState, b: MatBody, strength: float, verb: String) -> void:
	# Catching a moving body: its momentum goes into the catcher (pushback + Focus).
	var impulse := b.vel * b.mass
	if impulse.length() > 20.0:
		var push := impulse / Sim.ACTOR_MASS * 0.45
		push.y = 0.0
		a.vel += push
		spend_focus(a, minf(a.focus, impulse.length() * 0.02))
		emit("intercept", {"actor": a.id, "body": b.id, "impulse": impulse.length(), "verb": verb})
	if a.held_body >= 0 and a.held_body != b.id:
		release_body(a, Vector3.ZERO, false)
	if b.form != Sim.Form.WAVE and b.wave_path.size() > 0:
		b.wave_path = PackedVector3Array()   # a lifted ridge gathers into a chunk
		emit("reform", {"body": b.id})
	b.controller = a.id
	b.authority = strength
	b.residual_owner = -1
	b.residual_authority = 0.0
	b.attack_id = 0
	b.attack_owner = -1
	b.hit_set.clear()
	b.on_ground = false
	b.rest_time = 0.0
	b.age = 0.0                      # remnant lifetime / trim order count from the last hold
	b.hold_point = a.hand_point()    # never home to a stale point before the action sets one
	b.touch(a.id, verb, tick)
	a.held_body = b.id
	emit("control_won", {"actor": a.id, "body": b.id, "verb": verb})


## Releases the held body; with a velocity it becomes a projectile (new attack instance).
func release_body(a: ActorState, v: Vector3, as_attack: bool, damage: float = 0.0, balance: float = 0.0) -> MatBody:
	var b := get_body(a.held_body)
	a.held_body = -1
	if b == null or not b.alive:
		return null
	b.controller = -1
	b.authority = 0.0
	b.vel = v
	b.residual_owner = a.id
	b.residual_authority = Interactions.cohesion(b.tier)   # 0.6 + 0.1·tier (legacy 0.6 at T0)
	b.touch(a.id, "release", tick)
	if as_attack:
		b.attack_id = new_attack_id()
		b.attack_owner = a.id
		b.hit_set.clear()
		b.hit_set[a.id] = true
		b.damage = damage
		b.balance_damage = balance
	emit("release", {"actor": a.id, "body": b.id, "attack": as_attack})
	return b


func held(a: ActorState) -> MatBody:
	var b := get_body(a.held_body)
	if b == null or not b.alive or b.controller != a.id:
		a.held_body = -1
		return null
	return b


# ============================================================== bodies

func _update_bodies() -> void:
	var dt := Sim.DT
	for b in bodies:
		if not b.alive:
			continue
		b.age += dt
		if b.residual_authority > 0.0:
			b.residual_authority = maxf(0.0, b.residual_authority - RESIDUAL_DECAY * dt)
			if b.residual_authority <= 0.0:
				b.residual_owner = -1
		var held_by := get_actor(b.controller) if b.controller >= 0 else null
		# --- thermal
		var insulated := held_by != null and b.is_stone() and b.liquid > 0.0 and (held_by.has("magma") or held_by.has("heat_draw"))
		if insulated:
			if not spend_focus(held_by, Sim.HOLD_UPKEEP_FOCUS * dt):
				emit("control_lost", {"actor": held_by.id, "body": b.id, "reason": "focus"})
				release_body(held_by, Vector3.ZERO, false)
				held_by = null
		elif not b.static_body and b.form != Sim.Form.CLOUD:
			ledger.ambient += Thermal.ambient_step(b, dt)
		var old_phase := b.phase
		if Thermal.update_phase(b):
			emit("phase", {"body": b.id, "from": Sim.PHASE_NAMES[old_phase], "to": Sim.PHASE_NAMES[b.phase]})
			_on_phase_changed(b, old_phase)
		if not b.alive:
			continue
		if b.mat >= Sim.Mat.METAL:
			_material_tick(b, dt)
			if not b.alive:
				continue
		# --- captured inside another body (vortex, wave carry): rides along
		if b.captured_by >= 0:
			var cap := get_body(b.captured_by)
			if cap != null and cap.alive:
				var off: Vector3 = b.props.get("capture_off", Vector3.ZERO)
				if cap.spin != 0.0:
					off = off.rotated(Vector3.UP, cap.spin * dt)
					b.props["capture_off"] = off
				b.pos = cap.pos + off
				b.vel = cap.vel
				continue
			b.captured_by = -1
		# --- motion
		if held_by != null:
			var to := b.hold_point - b.pos
			var v := to * 11.0
			if v.length() > 22.0:
				v = v.normalized() * 22.0
			b.vel = v
			b.pos += v * dt
			if b.form == Sim.Form.WAVE:
				b.form = Sim.Form.CHUNK
			if held_by.chest().distance_to(b.pos) > 11.0:
				emit("control_lost", {"actor": held_by.id, "body": b.id, "reason": "range"})
				release_body(held_by, b.vel, false)
			continue
		# Kit body behaviours by tag (custom motion: return true to skip the default motion).
		if b.tag != &"" and _body_ticks.has(b.tag):
			var cb: Callable = _body_ticks[b.tag]
			if cb.is_valid() and bool(cb.call(self, b, dt)):
				continue
			if not b.alive:
				continue
		match b.form:
			Sim.Form.WAVE:
				_update_wave(b, dt)
			Sim.Form.WALL:
				_update_wall(b, dt)
			Sim.Form.POOL, Sim.Form.PUDDLE:
				pass
			Sim.Form.ZONE:
				_update_zone(b, dt)
			Sim.Form.CLOUD:
				b.pos += (b.vel + Vector3(0, 0.6, 0)) * dt
				b.vel *= 0.96
				if b.age > b.max_life:
					if b.mat == Sim.Mat.STEAM or b.mat == Sim.Mat.WATER:
						mass_ledger.vapor += b.mass
					else:
						_return_mass(b)
					release_captured(b)
					remove_body(b, "dissipated")
			_:
				_update_ballistic(b, dt)
		if b.max_life > 0.0 and b.age > b.max_life and b.alive and b.form != Sim.Form.CLOUD and b.form != Sim.Form.ZONE and b.attack_id == 0:
			decay_body(b, "lifetime")   # a live projectile is never removed mid-flight


## New materials: fire payload burns out, plant burns away above its ignition point.
func _material_tick(b: MatBody, dt: float) -> void:
	if b.mat == Sim.Mat.FIRE:
		if b.heat_payload < 0.5 and b.form != Sim.Form.ZONE and b.attack_id == 0 and b.age > 0.1:
			decay_body(b, "burned_out")
	elif b.mat == Sim.Mat.PLANT:
		if b.temp >= float(Materials.prop(Sim.Mat.PLANT, "ignite", 250.0)):
			burn_plant(b, float(Materials.prop(Sim.Mat.PLANT, "burn_rate", 1.5)) * dt)


func _on_phase_changed(b: MatBody, old_phase: int) -> void:
	if Materials.is_fusible(b.mat):
		var solid_name: String = "rock" if b.is_stone() else String(Sim.MAT_NAMES[b.mat])
		var molten_name: String = "molten" if b.is_stone() else "molten_" + String(Sim.MAT_NAMES[b.mat])
		if b.phase == Sim.Phase.SOLID and b.form == Sim.Form.WAVE:
			b.form = Sim.Form.CHUNK
			b.vel = Vector3.ZERO
			b.attack_id = 0
			b.on_ground = true
			b.max_life = Sim.REMNANT_LIFETIME
			b.age = 0.0
			release_captured(b)
			emit("transform", {"body": b.id, "at": b.pos, "from": "wave", "to": solid_name, "why": "cooled"})
		elif b.phase == Sim.Phase.SOLID and b.form == Sim.Form.BLOB:
			b.form = Sim.Form.CHUNK
			b.max_life = Sim.REMNANT_LIFETIME
			b.age = 0.0
			emit("transform", {"body": b.id, "at": b.pos, "from": "lava" if b.is_stone() else molten_name, "to": solid_name, "why": "cooled"})
		elif b.phase == Sim.Phase.MOLTEN and old_phase != Sim.Phase.MOLTEN and b.form == Sim.Form.CHUNK:
			b.form = Sim.Form.BLOB
			emit("transform", {"body": b.id, "at": b.pos, "from": "stone" if b.is_stone() else Sim.MAT_NAMES[b.mat], "to": molten_name, "why": "heated"})
		# Fused sand sets as glass when it cools back to solid.
		if b.mat == Sim.Mat.SAND and b.phase == Sim.Phase.SOLID and old_phase != Sim.Phase.SOLID:
			convert_mat(b, Sim.Mat.GLASS, "sand_to_glass")
			emit("transform", {"body": b.id, "at": b.pos, "from": "sand", "to": "glass", "why": "cooled"})
	elif b.is_water():
		if b.phase == Sim.Phase.LIQUID and old_phase == Sim.Phase.FROZEN:
			if b.form == Sim.Form.SHARD or b.form == Sim.Form.CHUNK:
				b.form = Sim.Form.PUDDLE
				b.vel = Vector3.ZERO
				b.attack_id = 0
				b.pos.y = arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.2)
				b.update_radius_puddle()
				emit("transform", {"body": b.id, "at": b.pos, "from": "ice", "to": "water", "why": "melted"})
				_merge_puddle(b)
			elif b.form == Sim.Form.PUDDLE:
				emit("transform", {"body": b.id, "at": b.pos, "from": "ice", "to": "water", "why": "melted"})
		elif b.phase == Sim.Phase.FROZEN:
			emit("transform", {"body": b.id, "at": b.pos, "from": "water", "to": "ice", "why": "frozen"})


func _update_ballistic(b: MatBody, dt: float) -> void:
	if b.static_body:
		return
	if b.on_ground and b.vel.length() < 0.05:
		b.rest_time += dt
		_quench_contact(b, dt)   # lava at rest in water still quenches
		return
	if b.attack_id != 0 and b.props.has("homing"):
		_home(b, dt)
	b.vel.y -= Sim.GRAVITY * b.gravity_scale * dt
	if b.vel.length() > b.max_speed:
		b.vel = b.vel.normalized() * b.max_speed
	var np := b.pos + b.vel * dt
	var t := arena.segment_hit(b.pos, np, b.radius * 0.8)
	var hit_wall_body: MatBody = null
	for w in bodies:
		if w.alive and w.form == Sim.Form.WALL and w.wall_rise > 0.3 and w != b and point_in_wall(np, w, b.radius * 0.7):
			if b.attack_id != 0:
				if w.last_actor == b.attack_owner:
					continue   # a fighter's own wall launches, never blocks, their shot
				hit_wall_body = w
				break
			# Inert bodies are never stuck in or grinding a wall: they're pushed clear of it.
			np = _push_out_obb(np, b.radius, w)
	if hit_wall_body != null:
		var wres := _body_hits_wall(b, hit_wall_body)
		if not b.alive or b.attack_id == 0 or not wres.get("counter_broken", false):
			return
		np = b.pos + b.vel * dt   # the wall broke (overwhelm / shatter): the shot carries on
	if t >= 0.0:
		np = b.pos.lerp(np, maxf(0.0, t - 0.02))
		if b.attack_id != 0 and int(b.props.get("ricochet", 0)) > 0:
			_ricochet(b, np)
		elif b.attack_id != 0:
			# Arena solids are an environment counter (legacy: impact, the shot drops).
			var at := Agent.of_body(self, b)
			b.pos = np
			Interactions.resolve(self, at, Agent.of_env(self, "arena_wall", np), {"site": "arena"})
			if not b.alive:
				return
			np = b.pos
		else:
			_body_impact(b, "wall")
			b.vel = Vector3(-b.vel.x * 0.15, maxf(b.vel.y, 0.0) * 0.2, -b.vel.z * 0.15)
	var g := arena.ground_height(np.x, np.z, np.y + 0.3)
	var bottom := b.radius * (0.25 if b.form == Sim.Form.BLOB else 0.8)
	if np.y - bottom <= g:
		np.y = g + bottom
		if b.vel.y < -2.0 and b.attack_id != 0:
			_body_impact(b, "ground")
			if not b.alive:
				return
		if b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.LIQUID:
			b.pos = np
			_water_to_puddle(b)
			return
		if b.form == Sim.Form.SHARD:
			b.pos = np
			_shatter(b)
			return
		b.vel.y = 0.0
		b.vel *= 0.82 if b.form != Sim.Form.BLOB else 0.3
		b.on_ground = true
		if b.vel.length() < 0.4:
			b.vel = Vector3.ZERO
			b.attack_id = 0
	else:
		b.on_ground = false
	b.pos = np
	_quench_contact(b, dt)


## Homing projectiles turn toward their owner's target (props.homing deg/s).
func _home(b: MatBody, dt: float) -> void:
	var owner := get_actor(b.attack_owner)
	if owner == null:
		return
	var tgt := get_actor(int(b.props.get("homing_target", owner.lock_target)))
	if tgt == null:
		return
	var to := tgt.chest() - b.pos
	var flat := Vector3(to.x, 0, to.z)
	var hv := Vector3(b.vel.x, 0, b.vel.z)
	if flat.length() < 0.5 or hv.length() < 0.5:
		return
	var cur := atan2(hv.x, hv.z)
	var want := atan2(flat.x, flat.z)
	var diff := wrapf(want - cur, -PI, PI)
	var mx := deg_to_rad(float(b.props.homing)) * dt
	cur += clampf(diff, -mx, mx)
	var sp := hv.length()
	b.vel = Vector3(sin(cur) * sp, b.vel.y, cos(cur) * sp)


## Bounce off an arena solid (props.ricochet counts the bounces left).
func _ricochet(b: MatBody, at: Vector3) -> void:
	b.props["ricochet"] = int(b.props.ricochet) - 1
	var n := Vector3.ZERO
	for s in arena.solids:
		var mn: Vector3 = s.min - Vector3.ONE * (b.radius + 0.1)
		var mx: Vector3 = s.max + Vector3.ONE * (b.radius + 0.1)
		if at.x >= mn.x and at.x <= mx.x and at.z >= mn.z and at.z <= mx.z and at.y <= mx.y:
			var c: Vector3 = (Vector3(s.min) + Vector3(s.max)) * 0.5
			var hx: float = (s.max.x - s.min.x) * 0.5
			var hz: float = (s.max.z - s.min.z) * 0.5
			var dx := (at.x - c.x) / maxf(hx, 0.01)
			var dz := (at.z - c.z) / maxf(hz, 0.01)
			n = Vector3(signf(dx), 0, 0) if absf(dx) > absf(dz) else Vector3(0, 0, signf(dz))
			break
	if n == Vector3.ZERO:
		n = -Vector3(b.vel.x, 0, b.vel.z).normalized()
	b.vel = b.vel - 2.0 * b.vel.dot(n) * n
	b.vel *= 0.9
	b.pos = at + n * 0.05
	b.hit_set.clear()
	if b.attack_owner >= 0:
		b.hit_set[b.attack_owner] = true
	emit("ricochet", {"body": b.id, "at": at, "dir": b.vel.normalized()})


## Molten/softened stone touching the pool or a puddle is quenched, moving or at rest
## (legacy cell: transform -> rock at 1400 HU/s, the water flash-boils).
func _quench_contact(b: MatBody, _dt: float) -> void:
	if not Materials.is_fusible(b.mat) or b.liquid <= 0.0:
		return
	if arena.in_pool(b.pos.x, b.pos.z) and b.pos.y < arena.pool_level + 0.1:
		Interactions.resolve(self, Agent.of_body(self, b), Agent.of_env(self, "pool", b.pos), {"continuous": true})
		return
	var pd := puddle_at(b.pos)
	if pd != null:
		var env := Agent.of_env(self, "puddle", b.pos)
		env.body = pd
		Interactions.resolve(self, Agent.of_body(self, b), env, {"continuous": true})


func _body_impact(b: MatBody, what: String) -> void:
	emit("impact", {"body": b.id, "on": what, "speed": b.vel.length(), "mass": b.mass,
		"power": b.mass * b.vel.length() / 20.0, "mat": FxEvents.mat_of(b), "tier": b.tier, "dir": b.vel.normalized()})
	if b.props.has("on_impact") and b.attack_id != 0:
		Verbs.on_impact(self, b, what)
		if not b.alive:
			return
	if b.form == Sim.Form.SHARD:
		_shatter(b)
		return
	b.attack_id = 0


## A projectile meets a raised wall: the wall is a counter (rule cell threat x wall class).
func _body_hits_wall(b: MatBody, w: MatBody) -> Dictionary:
	if b.attack_id == 0:
		return {}
	var owner := get_actor(w.controller if w.controller >= 0 else w.last_actor)
	var counter := Agent.of_body(self, w)
	counter.actor = owner
	counter.perfect = owner != null and owner.guarding and owner.wall_body == w.id and perfect_guard(owner)
	return Interactions.resolve(self, Agent.of_body(self, b, owner), counter, {"site": "wall"})


func _crumble_wall(w: MatBody) -> void:
	if not w.alive:
		return
	emit("wall_crumble", {"body": w.id})
	var owner := get_actor(w.last_actor)
	if owner != null and owner.wall_body == w.id:
		owner.wall_body = -1
	release_captured(w)
	# Rubble: two usable 20 kg stones split off, the rest sinks back into the ground.
	var piece := minf(Sim.STONE_SHOT_MASS, w.mass * 0.25)
	for k in 2:
		if w.mass <= piece * 0.5:
			break
		var off := Vector3(cos(w.wall_yaw), 0, -sin(w.wall_yaw)) * (0.5 if k == 0 else -0.5)
		var c := split_body(w, piece, w.pos + off + Vector3(0, 0.4, 0))
		c.form = Sim.Form.CHUNK
		c.vel = Vector3(0, 2.0, 0)
		c.max_life = Sim.REMNANT_LIFETIME
		c.update_radius()
	_return_mass(w)
	ledger.removed += w.thermal_energy()
	w.mass = 0.0
	remove_body(w, "crumbled")


func _update_wall(w: MatBody, dt: float) -> void:
	var owner := get_actor(w.last_actor)
	var keep := owner != null and owner.wall_body == w.id and owner.guarding
	if not keep and owner != null and owner.action != null and int(owner.action.data.get("keep_wall", -1)) == w.id:
		keep = true   # a push/sink move started from the guard keeps maintaining it
	if not keep and w.props.get("standing", 0.0) > 0.0:
		keep = w.age < float(w.props.standing)   # free-standing walls (spikes, ridges) live their time
	if keep:
		w.wall_rise = minf(1.0, w.wall_rise + dt / float(w.props.get("rise_time", 0.14)))
		if not w.props.has("standing"):
			w.age = 0.0
	else:
		w.wall_rise -= dt / 0.35
		if w.wall_rise <= 0.0:
			_return_mass(w)
			ledger.removed += w.thermal_energy()
			if owner != null and owner.wall_body == w.id:
				owner.wall_body = -1
			release_captured(w)
			remove_body(w, "sank")


func _update_wave(b: MatBody, dt: float) -> void:
	var speed := 0.0
	if b.tag != &"" and b.props.has("speed"):
		speed = float(b.props.speed) * (Thermal.flow_factor(b) if b.props.get("viscous", false) else 1.0)
	else:
		var pour_def: Dictionary = Moves.DEFS.pour
		speed = float(pour_def.wave_speed) * Thermal.flow_factor(b)
	if b.wave_budget <= 0.0 or speed < 0.35:
		_settle_wave(b, "budget" if b.wave_budget <= 0.0 else "viscous")
		return
	# The pouring fighter keeps bending the wave toward their target while it is fluid
	# (fantasy rule: limited turn rate, fades as it cools). Tagged waves steer at props.steer deg/s.
	var owner := get_actor(b.attack_owner)
	var steer := float(b.props.get("steer", WAVE_TURN_RATE)) if b.tag != &"" else WAVE_TURN_RATE
	var fluid := b.liquid > 0.4 if b.tag == &"" else steer > 0.0
	if owner != null and fluid:
		var tgt := get_actor(owner.lock_target)
		if tgt != null:
			var to := tgt.pos - b.pos
			to.y = 0.0
			if to.length() > 1.0:
				var want := atan2(to.x, to.z)
				var cur := atan2(b.wave_dir.x, b.wave_dir.z)
				var diff := wrapf(want - cur, -PI, PI)
				var max_turn := deg_to_rad(steer) * dt * (Thermal.flow_factor(b) if b.tag == &"" else 1.0)
				if absf(diff) < deg_to_rad(70.0):
					cur += clampf(diff, -max_turn, max_turn)
					b.wave_dir = Vector3(sin(cur), 0.0, cos(cur))
	var stepv := b.wave_dir * speed * dt
	var np := b.pos + stepv
	var g0 := b.pos.y
	var raw_top := arena.ground_height(np.x, np.z, g0, 100.0)
	var blocked := raw_top > g0 + Sim.WAVE_STEP
	for w in bodies:
		if w.alive and w.form == Sim.Form.WALL and w.wall_rise > 0.3 and w != b and point_in_wall(np + Vector3(0, 0.2, 0), w, b.wave_width * 0.3):
			if w.last_actor == b.attack_owner and b.tag != &"" and b.props.get("own_walls_pass", false):
				continue
			var counter := Agent.of_body(self, w)
			counter.actor = get_actor(w.last_actor)
			var res := Interactions.resolve(self, Agent.of_body(self, b), counter, {"site": "wave_wall"})
			if not b.alive or b.form != Sim.Form.WAVE:
				return
			if res.stopped:
				blocked = true
			break
	if blocked:
		emit("wave_blocked", {"body": b.id, "at": b.pos})
		_settle_wave(b, "blocked")
		return
	var g1 := arena.ground_height(np.x, np.z, g0, Sim.WAVE_STEP)
	if g1 < g0 - 0.1:
		b.wave_budget -= 1.0
		emit("wave_drop", {"body": b.id, "from": g0, "to": g1})
	np.y = g1
	b.vel = stepv / dt
	b.pos = np
	b.wave_budget -= stepv.length()
	if b.wave_path.is_empty() or b.wave_path[b.wave_path.size() - 1].distance_to(np) > 0.35:
		b.wave_path.append(np)
		if b.wave_path.size() > 28:
			b.wave_path.remove_at(0)
		if b.props.has("trail_zone") and b.wave_path.size() % 4 == 0:
			Verbs.leave_trail(self, b)
	if b.tag != &"":
		_wave_sweep(b)
		if not b.alive or b.form != Sim.Form.WAVE:
			return
	if arena.in_pool(np.x, np.z):
		Interactions.resolve(self, Agent.of_body(self, b), Agent.of_env(self, "pool", np), {"continuous": true}, Interactions.PASS_RULE)
		if not b.alive or b.form != Sim.Form.WAVE:
			return
	var pd := puddle_at(np)
	if pd != null:
		var env := Agent.of_env(self, "puddle", np)
		env.body = pd
		Interactions.resolve(self, Agent.of_body(self, b), env, {"continuous": true}, Interactions.PASS_RULE)


## A tagged wave meets the loose bodies and shots on its front (rules: e.g. a water wave captures a
## stone and carries it back, a sand surge buries puddles). Rate-limited per pair.
func _wave_sweep(wv: MatBody) -> void:
	for o in bodies:
		if o == wv or not o.alive or o.static_body or o.controller >= 0 or o.captured_by >= 0:
			continue
		if o.form == Sim.Form.WAVE or o.form == Sim.Form.ZONE or o.form == Sim.Form.WALL or o.form == Sim.Form.POOL:
			continue
		if Vector2(o.pos.x - wv.pos.x, o.pos.z - wv.pos.z).length() > wv.wave_width * 0.5 + o.radius or o.pos.y > wv.pos.y + 2.0:
			continue
		var key := "%d|%d" % [wv.id, o.id]
		if tick - int(_zone_pairs.get(key, -100000)) < 6:
			continue
		_zone_pairs[key] = tick
		var counter := Agent.of_body(self, wv)
		counter.actor = get_actor(wv.attack_owner)
		Interactions.resolve(self, Agent.of_body(self, o, counter.actor), counter, {"continuous": true, "site": "wave"}, Interactions.PASS_RULE)
		if not wv.alive or wv.form != Sim.Form.WAVE:
			return


func _settle_wave(b: MatBody, why: String) -> void:
	b.form = Sim.Form.BLOB if b.liquid > 0.0 else Sim.Form.CHUNK
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.on_ground = true
	b.max_life = Sim.REMNANT_LIFETIME
	b.age = 0.0
	emit("wave_settle", {"body": b.id, "why": why})
	release_captured(b)
	if b.tag != &"":
		Verbs.on_wave_end(self, b, why)


## Lava touching water: rapid heat loss, water flashes to steam (bounded by both masses).
func _quench(lava: MatBody, water: MatBody, dt: float) -> void:
	quench_energy(lava, water, Sim.QUENCH_RATE * dt)


## Removes up to `q_max` HU from a molten body into water (flash boil, ledger vapor).
func quench_energy(lava: MatBody, water: MatBody, q_max: float) -> void:
	var q := minf(q_max, lava.thermal_energy())
	if q <= 0.0 or water.mass <= 0.0:
		return
	var applied := -Thermal.heat(lava, -q)
	var kg := boil_water(water, applied, lava.pos + Vector3(0, 0.3, 0))
	if water.form == Sim.Form.PUDDLE and water.mass <= 0.05:
		decay_body(water, "boiled")
	if tick % 6 == 0:
		emit("steam", {"body": lava.id, "water": water.id, "kg": kg})


## Applies heat to any body with full ledger accounting (vapour leaving water is
## recorded and spawns steam). Returns HU applied.
func heat_body(b: MatBody, energy: float) -> float:
	var applied := Thermal.heat(b, energy)
	var kg := Thermal.last_vapor
	if kg > 0.0:
		ledger.vapor += Thermal.vapor_energy(kg)
		_spawn_steam(b.pos + Vector3(0, 0.3, 0), kg)
		if b.mass <= 0.05 and b.form != Sim.Form.POOL:
			decay_body(b, "boiled")
	return applied


## Flash-boils water with `energy` HU at a contact surface; energy the water can't
## take (not enough mass) is lost to the air. Returns vaporised kg.
func boil_water(water: MatBody, energy: float, at: Vector3) -> float:
	var kg := Thermal.boil(water, energy)
	ledger.vapor += Thermal.vapor_energy(kg)
	ledger.ambient -= energy - Thermal.last_used
	_spawn_steam(at, kg)
	return kg


func _spawn_steam(p: Vector3, kg: float) -> void:
	if kg <= 0.0:
		return
	# Merge into a nearby young cloud to bound body count.
	for c in bodies:
		if c.alive and c.form == Sim.Form.CLOUD and c.mat == Sim.Mat.STEAM and c.pos.distance_to(p) < 2.0 and c.age < 1.5:
			c.mass += kg
			return
	var c := spawn_body(Sim.Mat.STEAM, Sim.Form.CLOUD, kg, p, "steam")
	c.max_life = 2.6
	c.phase = Sim.Phase.GAS


func _water_to_puddle(b: MatBody) -> void:
	b.form = Sim.Form.PUDDLE
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.on_ground = true
	b.pos.y = arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3)
	if arena.in_pool(b.pos.x, b.pos.z):
		merge_bodies(pool, b)
		return
	b.update_radius_puddle()
	emit("transform", {"body": b.id, "at": b.pos, "from": "stream", "to": "puddle", "why": "landed"})
	_merge_puddle(b)


func _merge_puddle(b: MatBody) -> void:
	if not b.alive:
		return
	if arena.in_pool(b.pos.x, b.pos.z):
		merge_bodies(pool, b)
		return
	for o in bodies:
		if o != b and o.alive and o.form == Sim.Form.PUDDLE and o.phase == b.phase:
			if Vector2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() < (o.radius + b.radius) * 0.8:
				var into := o if o.mass >= b.mass else b
				var other := b if into == o else o
				merge_bodies(into, other)
				into.update_radius_puddle()
				return
	# cap puddles
	var puddles: Array[MatBody] = []
	for o in bodies:
		if o.alive and o.form == Sim.Form.PUDDLE:
			puddles.append(o)
	if puddles.size() > Sim.MAX_PUDDLES:
		var oldest := puddles[0]
		mass_ledger.evaporated += oldest.mass
		ledger.removed += oldest.thermal_energy()
		remove_body(oldest, "evaporated")


func _shatter(b: MatBody) -> void:
	# Ice shard breaks into two fragments with exact mass division.
	emit("shatter", {"body": b.id, "mass": b.mass})
	b.attack_id = 0
	b.form = Sim.Form.CHUNK
	b.vel = Vector3.ZERO
	b.on_ground = true
	b.pos.y = arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3) + 0.05
	if b.mass >= 1.0:
		var c := split_body(b, b.mass * 0.5, b.pos + Vector3(0.35, 0, 0.2))
		c.form = Sim.Form.CHUNK
		c.on_ground = true
		c.max_life = Sim.REMNANT_LIFETIME
	b.max_life = Sim.REMNANT_LIFETIME


## Books a body's mass as leaving the sim, by material (ground / metal / plant ledgers).
func _return_mass(b: MatBody) -> void:
	match b.mat:
		Sim.Mat.STONE, Sim.Mat.SAND, Sim.Mat.GLASS:
			mass_ledger.ground_returned += b.mass
		Sim.Mat.WATER:
			mass_ledger.evaporated += b.mass
		Sim.Mat.STEAM:
			mass_ledger.vapor += b.mass   # its heat was booked as vapour when it boiled off
		Sim.Mat.METAL:
			mass_ledger.metal_returned += b.mass
		Sim.Mat.PLANT:
			mass_ledger.plant_returned += b.mass


## Removes a body that leaves the simulation, recording its mass and heat in the ledgers.
func decay_body(b: MatBody, why: String) -> void:
	if not b.alive:
		return
	_return_mass(b)
	ledger.removed += b.thermal_energy()
	release_captured(b)
	remove_body(b, why)


## Changes a body's material keeping its thermal energy exact (sand -> glass -> sand, sand -> sandstone).
## ledger_key counts the converted mass (mass_ledger); earth_mass() sums all earth materials.
func convert_mat(b: MatBody, new_mat: int, ledger_key: String = "") -> void:
	var e := b.thermal_energy()
	b.mat = new_mat
	if not Materials.is_fusible(new_mat):
		b.liquid = 0.0
	_set_energy(b, e)
	Thermal.update_phase(b)
	b.update_radius()
	if ledger_key != "":
		mass_ledger[ledger_key] = float(mass_ledger.get(ledger_key, 0.0)) + b.mass
	emit("convert", {"body": b.id, "to": Sim.MAT_NAMES[new_mat], "mass": b.mass})


## Booked conversion water -> plant (1 kg -> 1 kg, ledger water_to_plant): takes kg from a water body
## (or the actor's waterskin when src is null) and grows a PLANT body at p. Returns it (null if no water).
func grow_plant(src: MatBody, kg: float, p: Vector3, a: ActorState = null) -> MatBody:
	var take := kg
	if src != null:
		take = minf(kg, src.mass)
		var e := src.thermal_energy() * take / maxf(src.mass, 1e-9)
		ledger.removed += e   # the water's own heat leaves with it (the vine grows at ambient)
		src.mass -= take
		if src.form == Sim.Form.PUDDLE:
			src.update_radius_puddle()
		if src.mass <= 0.01:
			decay_body(src, "grown")
	elif a != null:
		take = minf(kg, a.water_carried)
		a.water_carried -= take
	if take <= 0.0:
		return null
	mass_ledger.water_to_plant += take
	var v := spawn_body(Sim.Mat.PLANT, Sim.Form.CHUNK, take, p, "grow")
	return v


## Burns `kg` of a plant body away (ledger burned; its heat share leaves as removed).
func burn_plant(b: MatBody, kg: float) -> void:
	var m := minf(kg, b.mass)
	if m <= 0.0:
		return
	var e_share := b.thermal_energy() * m / maxf(b.mass, 1e-9)
	ledger.removed += e_share
	b.mass -= m
	mass_ledger.burned += m
	if tick % 10 == 0:
		emit("burn_plant", {"body": b.id, "kg": m})
	if b.mass <= 0.05:
		mass_ledger.burned += b.mass
		ledger.removed += b.thermal_energy()
		b.mass = 0.0
		release_captured(b)
		remove_body(b, "burned")
	else:
		b.update_radius()


## Lets go of every body a captor holds (vortex, carrying wave). They leave along the captor's
## motion (or props.release_speed) as the captor owner's attack when the captor was one.
func release_captured(captor: MatBody) -> void:
	if captor.captured.is_empty():
		return
	var dir := captor.vel.normalized() if captor.vel.length() > 0.1 else captor.wave_dir
	for id in captor.captured:
		var c := get_body(id)
		if c == null or not c.alive or c.captured_by != captor.id:
			continue
		c.captured_by = -1
		var spd := float(c.props.get("release_speed", 0.0))
		c.vel = dir * spd + Vector3(0, 1.5, 0) if spd > 0.0 else captor.vel
		var own := captor.attack_owner if captor.attack_owner >= 0 else captor.owner
		if spd > 0.0 and own >= 0:
			c.attack_id = new_attack_id()
			c.attack_owner = own
			c.hit_set.clear()
			c.hit_set[own] = true
			c.damage = maxf(c.damage, float(c.props.get("release_damage", 8.0)))
			c.balance_damage = maxf(c.balance_damage, 20.0)
		c.on_ground = false
		emit("release_captured", {"body": c.id, "by": captor.id})
	captor.captured.clear()


# ============================================================== zones

## Spawns a ZONE body (field / barrier): emits zone open. mass > 0 with a material books it like any body
## (the caller books where it came from).
func spawn_zone(tag: StringName, p: Vector3, radius: float, owner_id: int, power: float = 0.0, mat: int = Sim.Mat.AIR,
		mass: float = 0.0, life: float = -1.0, origin: String = "zone") -> MatBody:
	var z := spawn_body(mat, Sim.Form.ZONE, mass, p, origin)
	z.tag = tag
	z.zone_radius = radius
	z.radius = radius
	z.owner = owner_id
	z.power = power
	z.max_life = life
	z.static_body = false
	if mat == Sim.Mat.WATER:
		z.phase = Sim.Phase.LIQUID
	FxEvents.zone(self, z, "open")
	return z


## Ends a zone (expired / broken / neutralized): zone close, captured bodies released, mass booked.
func close_zone(z: MatBody, why: String) -> void:
	if not z.alive:
		return
	FxEvents.zone(self, z, "close")
	decay_body(z, why)


func _update_zone(z: MatBody, dt: float) -> void:
	var att := int(z.props.get("attach", -1))
	if att >= 0:
		var a := get_actor(att)
		if a == null:
			close_zone(z, "detached")
			return
		z.pos = a.pos + Vector3(z.props.get("attach_off", Vector3.ZERO))
		z.vel = a.vel
	elif float(z.props.get("walk_speed", 0.0)) > 0.0:
		var tgt := get_actor(int(z.props.get("walk_target", -1)))
		if tgt != null:
			var to := tgt.pos - z.pos
			to.y = 0.0
			if to.length() > 0.3:
				z.vel = to.normalized() * float(z.props.walk_speed)
				z.pos += z.vel * dt
	elif z.vel.length() > 0.01:
		z.pos += z.vel * dt
		z.vel *= 1.0 - minf(1.0, float(z.props.get("drag", 0.0)) * dt)
	if z.max_life > 0.0 and z.age > z.max_life:
		close_zone(z, "expired")


## Per tick for every zone: kit effect hooks, data-driven actor effects (props.actor_status,
## props.dps), and body <-> zone interactions through the rules (rate-limited per pair).
func _zone_pass() -> void:
	var dt := Sim.DT
	var n := bodies.size()
	for zi in n:
		var z := bodies[zi]
		if not z.alive or z.form != Sim.Form.ZONE:
			continue
		if z.tag != &"" and _zone_effects.has(z.tag):
			var cb: Callable = _zone_effects[z.tag]
			if cb.is_valid():
				cb.call(self, z, dt)
			if not z.alive:
				continue
		if z.props.has("actor_status") or z.props.has("dps"):
			for a in actors:
				if a.health <= 0.0 or (z.props.get("spare_owner", true) and a.id == z.owner):
					continue
				if not _in_zone(z, a.pos + Vector3(0, 0.9, 0), Sim.ACTOR_RADIUS):
					continue
				if z.props.has("actor_status"):
					Status.apply(self, a, String(z.props.actor_status), float(z.props.get("status_t", 0.5)), float(z.props.get("status_mag", 1.0)), z.owner)
				if z.props.has("dps") and not (z.props.get("ground_only", false) and Status.immune(a, "ground")):
					a.health = maxf(0.0, a.health - float(z.props.dps) * dt)
		var rate := int(maxf(1.0, float(z.props.get("rate", 0.1)) * Sim.HZ))
		for bi in n:
			var b := bodies[bi]
			if b == z or not b.alive or b.static_body or b.controller >= 0 or b.captured_by == z.id:
				continue
			if not _in_zone(z, b.pos, b.radius):
				continue
			var key := "%d|%d" % [z.id, b.id]
			if tick - int(_zone_pairs.get(key, -100000)) < rate:
				continue
			_zone_pairs[key] = tick
			var counter := Agent.of_body(self, z)
			counter.actor = get_actor(z.owner)
			Interactions.resolve(self, Agent.of_body(self, b, counter.actor), counter, {"continuous": true, "site": "zone"}, Interactions.PASS_RULE)
			if not z.alive:
				break
	if tick % 120 == 0 and _zone_pairs.size() > 256:
		_zone_pairs.clear()


func _in_zone(z: MatBody, p: Vector3, r: float) -> bool:
	var h := float(z.props.get("height", 2.5))
	if p.y < z.pos.y - 0.5 or p.y > z.pos.y + h:
		return false
	return Vector2(p.x - z.pos.x, p.z - z.pos.z).length() <= z.zone_radius + r


## Bodies inside a zone (kits' zone effects).
func bodies_in_zone(z: MatBody) -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in bodies:
		if b != z and b.alive and _in_zone(z, b.pos, b.radius):
			out.append(b)
	return out


func actors_in_zone(z: MatBody) -> Array[ActorState]:
	var out: Array[ActorState] = []
	for a in actors:
		if a.health > 0.0 and _in_zone(z, a.pos + Vector3(0, 0.9, 0), Sim.ACTOR_RADIUS):
			out.append(a)
	return out


# ============================================================== contacts

func _contacts() -> void:
	for b in bodies:
		if not b.alive:
			continue
		if b.is_projectile() and b.vel.length() > 2.0:
			for a in actors:
				if b.hit_set.has(a.id) or a.health <= 0.0:
					continue
				if _touches_actor(b, a, 0.0):
					_projectile_hits_actor(b, a)
					if not b.alive or b.attack_id == 0:
						break
		elif b.form == Sim.Form.WAVE and b.attack_id != 0:
			for a in actors:
				if b.hit_set.has(a.id) or a.health <= 0.0:
					continue
				var flat := Vector2(a.pos.x - b.pos.x, a.pos.z - b.pos.z).length()
				if flat < b.wave_width * 0.5 + Sim.ACTOR_RADIUS and a.pos.y < b.pos.y + 0.45:
					if b.tag != &"" and Status.immune(a, "ground"):
						continue   # flying / levitating fighters pass over ground lines
					b.hit_set[a.id] = true
					var kn := b.wave_dir * 4.0 + Vector3(0, 3.0, 0)
					var kind := "lava"
					if b.tag != &"":
						kn = b.wave_dir * float(b.props.get("knock", 4.0)) + Vector3(0, float(b.props.get("lift", 3.0)), 0)
						kind = String(b.props.get("kind", FxEvents.mat_of(b)))
					hit_actor(a, {"attacker": b.attack_owner, "attack_id": b.attack_id, "damage": b.damage,
						"balance": b.balance_damage, "knock": kn, "kind": kind, "from": b.pos - b.wave_dir, "body": b.id,
						"src_attack": int(b.props.get("src_attack", -1))})
					if b.tag != &"" and b.props.has("hit_status"):
						Status.apply(self, a, String(b.props.hit_status), float(b.props.get("hit_status_t", 1.0)), 1.0, b.attack_owner)
		if b.is_stone() and b.temp >= Sim.HOT_ROCK_C and b.controller < 0 and b.form != Sim.Form.WAVE:
			for a in actors:
				if a.burn_cd <= 0.0 and _touches_actor(b, a, 0.15):
					if Status.immune(a, "burn"):
						continue
					a.burn_cd = 0.8
					a.health = maxf(0.0, a.health - 3.0)
					emit("burn", {"actor": a.id, "body": b.id})
	_clash_pass()
	_zone_pass()


## Projectile <-> projectile and wave <-> wave of different owners (MOVESET §10.1 momentum):
## the pair is resolved through the rules with the clash default.
func _clash_pass() -> void:
	var movers: Array[MatBody] = []
	for b in bodies:
		if b.alive and b.attack_id != 0 and b.controller < 0 and (b.form == Sim.Form.WAVE or (b.is_projectile() and b.vel.length() > 2.0)):
			movers.append(b)
	if movers.size() < 2:
		return
	for i in movers.size():
		var a := movers[i]
		for j in range(i + 1, movers.size()):
			var b := movers[j]
			if not a.alive or not b.alive or a.attack_id == 0 or b.attack_id == 0:
				continue
			if a.attack_owner == b.attack_owner or (a.form == Sim.Form.WAVE) != (b.form == Sim.Form.WAVE):
				continue
			var reach := (a.wave_width + b.wave_width) * 0.5 if a.form == Sim.Form.WAVE else a.radius + b.radius
			var d := a.pos.distance_to(b.pos) if a.form != Sim.Form.WAVE else Vector2(a.pos.x - b.pos.x, a.pos.z - b.pos.z).length()
			if d > reach:
				continue
			var ta := Agent.of_body(self, a)
			var tb := Agent.of_body(self, b)
			tb.power = tb.total()
			Interactions.resolve(self, ta, tb, {"site": "clash"}, Interactions.CLASH_RULE)


func _touches_actor(b: MatBody, a: ActorState, pad: float) -> bool:
	var lo := a.pos.y + 0.2
	var hi := a.pos.y + Sim.ACTOR_HEIGHT
	var cy := clampf(b.pos.y, lo, hi)
	var d := Vector3(b.pos.x - a.pos.x, b.pos.y - cy, b.pos.z - a.pos.z).length()
	return d < b.radius + Sim.ACTOR_RADIUS + pad


func _projectile_hits_actor(b: MatBody, a: ActorState) -> void:
	b.hit_set[a.id] = true
	if a.iframes > 0.0:
		emit("evaded", {"actor": a.id, "body": b.id})
		return
	# The guard (if any) answers inside hit_actor through the rule cell (body class x guard class):
	# legacy perfect Earth redirect, perfect deflect, wind guard deflect, plain block with chip.
	var to_src := -b.vel
	to_src.y = 0
	var facing_vel := a.forward().dot(to_src.normalized()) > -0.15 if to_src.length() > 0.01 else true
	var res := hit_actor(a, {"attacker": b.attack_owner, "attack_id": b.attack_id, "damage": b.damage, "facing_vel": facing_vel,
		"balance": b.balance_damage, "knock": b.vel.normalized() * minf(b.mass * b.vel.length() / 70.0, 9.0),
		"kind": "stone" if b.is_stone() else ("water" if b.is_water() else FxEvents.mat_of(b)), "from": b.pos - b.vel.normalized(), "body": b.id,
		"agent": Agent.of_body(self, b, a), "src_attack": int(b.props.get("src_attack", -1))})
	if res == "perfect" or res == "deflect" or res == "redirected":
		return
	if b.is_water() and b.phase == Sim.Phase.LIQUID:
		a.wetness = 1.0
	if b.is_stone() and b.temp >= Sim.HOT_ROCK_C and res != "block":
		a.health = maxf(0.0, a.health - 4.0)
		emit("burn", {"actor": a.id, "body": b.id})
	if b.props.has("hit_status") and (res == "hit" or res == "knockdown"):
		Status.apply(self, a, String(b.props.hit_status), float(b.props.get("hit_status_t", 1.0)), 1.0, b.attack_owner)
	if (res == "hit" or res == "knockdown") and int(b.props.get("pierce", 0)) > 0:
		b.props["pierce"] = int(b.props.pierce) - 1
		emit("pierce", {"body": b.id, "actor": a.id})
		return
	if res == "block" or res == "hit" or res == "knockdown" or res == "guard_break":
		if b.props.has("on_impact"):
			Verbs.on_impact(self, b, "actor")
			if not b.alive:
				return
		b.vel = -b.vel * 0.15 + Vector3(0, 1.5, 0)
		b.attack_id = 0
		if b.form == Sim.Form.SHARD:
			_shatter(b)
		elif b.is_water() and b.phase == Sim.Phase.LIQUID:
			_water_to_puddle(b)


# ============================================================== cleanup & caps

func _cleanup() -> void:
	if tick % 30 == 0:
		trim_remnants()
	var any_dead := false
	for b in bodies:
		if not b.alive:
			any_dead = true
			break
	if any_dead:
		var keep: Array[MatBody] = []
		for b in bodies:
			if b.alive:
				keep.append(b)
			else:
				_body_by_id.erase(b.id)
		bodies = keep


func alive_count() -> int:
	var n := 0
	for b in bodies:
		if b.alive:
			n += 1
	return n


func _enforce_cap() -> void:
	if alive_count() < Sim.MAX_BODIES:
		return
	# Decay the oldest inert remnant (never a held/attacking/static body); zones and captured bodies last.
	for pass_i in 2:
		for b in bodies:
			if b.alive and b.controller < 0 and b.attack_id == 0 and not b.static_body and b.form != Sim.Form.WALL:
				if pass_i == 0 and (b.form == Sim.Form.ZONE or b.captured_by >= 0):
					continue
				if b.form == Sim.Form.ZONE:
					close_zone(b, "cap")
				else:
					decay_body(b, "cap")
				return


## Counts remnants and trims the oldest beyond the cap (called by scenarios / periodically).
func trim_remnants() -> void:
	var rem: Array[MatBody] = []
	for b in bodies:
		if b.alive and b.is_stone() and b.controller < 0 and b.attack_id == 0 and b.on_ground and b.form != Sim.Form.WALL:
			rem.append(b)
	if rem.size() <= Sim.MAX_REMNANTS:
		return
	# Oldest first by age, which restarts whenever a body is seized or becomes rock: a reused
	# stone that just landed or freshly cooled lava outlives rubble that has lain untouched.
	rem.sort_custom(func(x: MatBody, y: MatBody) -> bool:
		if x.age != y.age:
			return x.age > y.age
		return x.id < y.id)
	while rem.size() > Sim.MAX_REMNANTS:
		decay_body(rem.pop_front(), "remnant_cap")


# ============================================================== accounting

func player_focus_low(a: ActorState) -> bool:
	return a.focus < 8.0


func system_energy() -> float:
	var e := 0.0
	for b in bodies:
		if b.alive:
			e += b.thermal_energy()
	for a in actors:
		e += a.heat_reserve
		if a.action != null:
			e += float(a.action.data.get("heat_paid", 0.0))   # heat a move paid and still carries (verbs)
	return e


func ledger_balance() -> float:
	## Expected system energy change from the ledger (should equal actual change).
	return ledger.generated + ledger.ambient - ledger.vapor - ledger.reserve_dissipated - ledger.vented - ledger.spent + ledger.freeze_dump - ledger.removed


func water_mass() -> float:
	var m := 0.0
	for b in bodies:
		if b.alive and (b.mat == Sim.Mat.WATER or b.mat == Sim.Mat.STEAM):
			m += b.mass
	for a in actors:
		m += a.water_carried
	return m + mass_ledger.vapor + mass_ledger.evaporated + mass_ledger.water_to_plant - mass_ledger.moisture_taken


func stone_mass() -> float:
	var m := 0.0
	for b in bodies:
		if b.alive and b.mat == Sim.Mat.STONE:
			m += b.mass
	return m + mass_ledger.ground_returned - mass_ledger.ground_taken


## Stone, sand and glass (and sandstone): every earth material taken from / returned to the ground.
func earth_mass() -> float:
	var m := 0.0
	for b in bodies:
		if b.alive and (b.mat == Sim.Mat.STONE or b.mat == Sim.Mat.SAND or b.mat == Sim.Mat.GLASS):
			m += b.mass
	return m + mass_ledger.ground_returned - mass_ledger.ground_taken


## Metal on the field + every satchel + returned - ripped from the plate (conserved).
func metal_mass() -> float:
	var m := 0.0
	for b in bodies:
		if b.alive and b.mat == Sim.Mat.METAL:
			m += b.mass
	for a in actors:
		m += a.metal_carried
	return m + mass_ledger.metal_returned - mass_ledger.metal_taken


## Vines on the field + burned + withered - grown (from water or the ground) (conserved at 0 + start).
func plant_mass() -> float:
	var m := 0.0
	for b in bodies:
		if b.alive and b.mat == Sim.Mat.PLANT:
			m += b.mass
	return m + mass_ledger.burned + mass_ledger.plant_returned - mass_ledger.water_to_plant - mass_ledger.plant_from_ground


# ============================================================== hooks (kits register once, from register())

## Custom per-tick behaviour for bodies with this tag: cb(w, b, dt) -> bool (true = it moved the body;
## the default motion is skipped).
static func register_body_tick(tag: StringName, cb: Callable) -> void:
	_body_ticks[tag] = cb


## Per-tick effect of zones with this tag: cb(w, zone, dt) (statuses, pulls, damage, spawning).
static func register_zone_effect(tag: StringName, cb: Callable) -> void:
	_zone_effects[tag] = cb


static func register_status(nm: String, spec: Dictionary) -> void:
	Status.register(nm, spec)


## Technique context for (element, sub): cb(w, a, dir) -> {mode, body, ok, reason, label?} (HUD + AI).
static func register_tech_preview(element: int, sub: int, cb: Callable) -> void:
	_tech_previews["%d/%d" % [element, sub]] = cb


static func unregister_hooks(tags: Array = []) -> void:
	for t in tags:
		_body_ticks.erase(t)
		_zone_effects.erase(t)


## What the fighter's technique would do now (HUD button label, AI): a registered preview for the
## current (element, sub), else the legacy Fire thermal preview (HEAT / DRAW / VENT), else {}.
func tech_preview(a: ActorState, dir: Vector3) -> Dictionary:
	var k := "%d/%d" % [a.element, a.sub()]
	if _tech_previews.has(k):
		var cb: Callable = _tech_previews[k]
		if cb.is_valid():
			return cb.call(self, a, dir)
	if a.element == Sim.Element.FIRE and a.sub() == 0:
		return ActFire.preview(self, a, dir)
	var tid := Moves.resolve(a.element, a.sub(), "tech")
	var d: Dictionary = Moves.DEFS.get(tid, {})
	if String(d.get("verb", "")) == "grip":
		return Verbs.grip_preview(self, a, d, dir)
	return {}
