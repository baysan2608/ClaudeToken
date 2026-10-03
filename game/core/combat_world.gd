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
const RUN_SPEED := 5.6
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
}

var _next_body := 1
var _next_attack := 1
var _grips: Array[Dictionary] = []
var _intents := {}
var _body_by_id := {}
var _actor_by_id := {}
var _null_intent := ActorIntent.new()
var _record_events := true


func _init(seed_value: int = 1) -> void:
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
	into.mass = m
	into.absorbed.append(other.id)
	into.update_radius()
	# Re-derive temperature so the merged thermal energy is exact.
	_set_energy(into, e)
	other.mass = 0.0
	other.alive = false
	emit("merge", {"into": into.id, "absorbed": other.id})


func _set_energy(b: MatBody, e: float) -> void:
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

func _process_intent(a: ActorState, it: ActorIntent) -> void:
	if a.is_dummy:
		return
	if it.element_select >= 0 and it.element_select < 4 and a.elements[it.element_select] and a.element != it.element_select:
		# Switching only affects the NEXT action; running actions keep their element.
		a.element = it.element_select
		emit("element", {"actor": a.id, "element": a.element})
	if it.target_cycle:
		_cycle_target(a)
	if a.lock_target < 0 or not _valid_target(a, a.lock_target):
		a.lock_target = _auto_target(a)
	var press := ""
	if it.evade_pressed:
		press = "evade"
	elif it.guard_pressed:
		press = "guard"
	elif it.tech_pressed:
		press = "tech"
	elif it.attack_pressed:
		press = "attack"
	if press != "":
		if not _try_start(a, press, it):
			a.buffered = press
			a.buffered_tick = tick
	elif a.buffered != "" and tick - a.buffered_tick <= int(Moves.BUFFER_TIME * Sim.HZ):
		if _try_start(a, a.buffered, it):
			a.buffered = ""
	else:
		a.buffered = ""
	# Guard held without an action (e.g. pressed during an uncancelable recovery and still held)
	if it.guard_held and a.action == null and a.stun <= 0.0:
		_try_start(a, "guard", it)


func can_cancel(a: ActorState, into: String) -> bool:
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
	if inst.phase == ActionInst.P.RECOVERY and d.has("cancel"):
		var rec: float = d.recovery
		if rec <= 0.0 or inst.t / rec >= float(d.cancel):
			return into == "guard" or into == "evade"
	return false


func _try_start(a: ActorState, press: String, it: ActorIntent) -> bool:
	if not can_cancel(a, press):
		return false
	if a.action != null:
		interrupt_action(a, "cancel:" + press)
	match press:
		"evade":
			var id := "air_dash" if a.element == Sim.Element.AIR else "evade"
			if a.focus < float(Moves.DEFS[id].cost) * 0.5:
				emit("insufficient", {"actor": a.id, "what": "focus"})
				id = "evade"
			start_action(a, id, it)
		"guard":
			start_action(a, "guard", it)
		"tech":
			var tid: String = ["earth_tech", "water_tech", "fire_tech", "air_tech"][a.element]
			start_action(a, tid, it)
		"attack":
			var aid: String = ["earth_attack", "water_attack", "fire_attack", "air_attack"][a.element]
			start_action(a, aid, it)
	return true


func start_action(a: ActorState, id: String, it: ActorIntent, pre: Dictionary = {}) -> ActionInst:
	var inst := ActionInst.new()
	inst.id = id
	inst.def = Moves.DEFS[id]
	inst.element = int(inst.def.get("element", a.element))
	inst.attack_id = new_attack_id()
	inst.data.merge(pre, true)
	a.action = inst
	a.attack_hold = 0.0
	emit("action", {"actor": a.id, "move": id, "phase": "startup"})
	_dispatch_start(a, inst, it)
	if a.action == inst and inst.phase == ActionInst.P.STARTUP and float(inst.def.startup) <= 0.0:
		_enter_after_startup(a, inst, it)
	return inst


func set_phase(a: ActorState, inst: ActionInst, p: int) -> void:
	inst.phase = p
	inst.t = 0.0
	emit("action", {"actor": a.id, "move": inst.id, "phase": inst.phase_name(), "heavy": inst.heavy})
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
			_dispatch_tick(a, inst, it)
		ActionInst.P.ACTIVE:
			_dispatch_tick(a, inst, it)
			if a.action == inst and inst.phase == ActionInst.P.ACTIVE and inst.t >= _active_of(inst):
				set_phase(a, inst, ActionInst.P.RECOVERY)
		ActionInst.P.RECOVERY:
			_dispatch_tick(a, inst, it)
			if a.action == inst and inst.phase == ActionInst.P.RECOVERY and inst.t >= float(d.recovery):
				finish_action(a, inst)


func _startup_of(_a: ActorState, inst: ActionInst) -> float:
	return float(inst.data.get("startup", inst.def.startup))


func _active_of(inst: ActionInst) -> float:
	return float(inst.data.get("active", inst.def.active))


# ---- module dispatch (explicit, no reflection) -------------------------------

func _dispatch_start(a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.def.module:
		"common": ActCommon.on_start(self, a, inst, it)
		"earth": ActEarth.on_start(self, a, inst, it)
		"water": ActWater.on_start(self, a, inst, it)
		"fire": ActFire.on_start(self, a, inst, it)
		"air": ActAir.on_start(self, a, inst, it)


func _dispatch_after_startup(a: ActorState, inst: ActionInst, it: ActorIntent) -> int:
	match inst.def.module:
		"common": return ActCommon.after_startup(self, a, inst, it)
		"earth": return ActEarth.after_startup(self, a, inst, it)
		"water": return ActWater.after_startup(self, a, inst, it)
		"fire": return ActFire.after_startup(self, a, inst, it)
		"air": return ActAir.after_startup(self, a, inst, it)
	return ActionInst.P.ACTIVE


func _dispatch_phase(a: ActorState, inst: ActionInst, p: int) -> void:
	match inst.def.module:
		"common": ActCommon.on_phase(self, a, inst, p)
		"earth": ActEarth.on_phase(self, a, inst, p)
		"water": ActWater.on_phase(self, a, inst, p)
		"fire": ActFire.on_phase(self, a, inst, p)
		"air": ActAir.on_phase(self, a, inst, p)


func _dispatch_tick(a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	match inst.def.module:
		"common": ActCommon.on_tick(self, a, inst, it)
		"earth": ActEarth.on_tick(self, a, inst, it)
		"water": ActWater.on_tick(self, a, inst, it)
		"fire": ActFire.on_tick(self, a, inst, it)
		"air": ActAir.on_tick(self, a, inst, it)


func _dispatch_interrupt(a: ActorState, inst: ActionInst, reason: String) -> void:
	match inst.def.module:
		"common": ActCommon.on_interrupt(self, a, inst, reason)
		"earth": ActEarth.on_interrupt(self, a, inst, reason)
		"water": ActWater.on_interrupt(self, a, inst, reason)
		"fire": ActFire.on_interrupt(self, a, inst, reason)
		"air": ActAir.on_interrupt(self, a, inst, reason)


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
				speed_scale = float(inst.def.get("move_channel", 0.45))
				turn_scale = 0.8
			ActionInst.P.ACTIVE:
				speed_scale = 0.0
				turn_scale = 0.0
			ActionInst.P.RECOVERY:
				speed_scale = 0.25
				turn_scale = 0.3
		if inst.id == "guard":
			speed_scale = 0.35
			turn_scale = 1.0
		if inst.data.get("controls_motion", false):
			controlled_motion = true
	if a.in_water:
		speed_scale *= 0.7
	var mv := it.move
	mv.y = 0.0
	if mv.length() > 1.0:
		mv = mv.normalized()
	want = mv * RUN_SPEED * speed_scale
	if a.gliding:
		want = mv * float(Moves.DEFS.air_tech.glide_speed)
	var hv := Vector3(a.vel.x, 0.0, a.vel.z)
	if not controlled_motion:
		var air_ctl := 1.0 if a.grounded else 0.35
		if a.gliding:
			air_ctl = 0.8
		var rate := (ACCEL if want.length() > hv.length() else DECEL) * air_ctl
		if a.stun > 0.0:
			rate = 9.0  # knockback slides out
		hv = hv.move_toward(want, rate * dt)
		a.vel.x = hv.x
		a.vel.z = hv.z
	# Facing: lock target while fighting, else movement direction.
	var face_dir := Vector3.ZERO
	var tgt := get_actor(a.lock_target)
	if tgt != null and a.pos.distance_to(tgt.pos) < 22.0:
		face_dir = tgt.pos - a.pos
	elif mv.length() > 0.1:
		face_dir = mv
	if inst != null and inst.data.has("face"):
		face_dir = inst.data.face
	face_dir.y = 0.0
	if face_dir.length() > 0.01 and turn_scale > 0.0:
		var target_yaw := atan2(face_dir.x, face_dir.z)
		var diff := wrapf(target_yaw - a.facing, -PI, PI)
		var max_turn := TURN_RATE * turn_scale * dt
		a.facing = wrapf(a.facing + clampf(diff, -max_turn, max_turn), -PI, PI)
	# Vertical
	if not a.grounded:
		a.vel.y -= Sim.GRAVITY * dt
		if a.gliding:
			a.vel.y = maxf(a.vel.y, -float(Moves.DEFS.air_tech.glide_fall))
	var np := a.pos + a.vel * dt
	np = arena.push_out(np, Sim.ACTOR_RADIUS)
	np = _push_out_walls(np, Sim.ACTOR_RADIUS)
	var g := arena.ground_height(np.x, np.z, a.pos.y)
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


func _update_surface(a: ActorState) -> void:
	a.in_water = arena.in_pool(a.pos.x, a.pos.z) and a.pos.y < arena.pool_level and a.grounded
	if a.in_water:
		a.wetness = 1.0
		var take := minf(6.0 - a.water_carried, pool.mass)
		if take > 0.0:
			a.water_carried += take
			pool.mass -= take
	a.surface = arena.surface_at(a.pos.x, a.pos.z, a.pos.y)
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
	return t != null and t.team != a.team and t.health > 0.0


func _auto_target(a: ActorState) -> int:
	var best := -1
	var bd := INF
	for t in actors:
		if t.team == a.team or t.health <= 0.0:
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
	if t.guarding and facing_ok and not info.get("unblockable", false):
		if perfect_guard(t):
			emit("perfect_deflect", {"actor": t.id, "attacker": info.get("attacker", -1), "kind": kind})
			t.last_result = "perfect"
			var att := get_actor(info.get("attacker", -1))
			if att != null and att.pos.distance_to(t.pos) < 3.0:
				att.balance -= 18.0
				att.balance_idle = 0.0
			return "perfect"
		t.health -= dmg * 0.12
		t.balance -= bal * 0.55
		t.balance_idle = 0.0
		var kb: Vector3 = info.get("knock", Vector3.ZERO)
		t.vel += kb * 0.35
		emit("block", {"actor": t.id, "attacker": info.get("attacker", -1), "kind": kind})
		t.last_result = "block"
		if t.balance <= 0.0:
			_stagger(t, "guard_break", 0.7, info)
			t.balance = 35.0
			return "guard_break"
		return "block"
	if kind == "lightning" and t.wetness > 0.3:
		dmg *= 1.5
	t.health = maxf(0.0, t.health - dmg)
	t.balance -= bal
	t.balance_idle = 0.0
	var knock: Vector3 = info.get("knock", Vector3.ZERO)
	t.vel += knock
	if knock.y > 0.0:
		t.grounded = false
	t.last_hit_dir = -to_src.normalized() if to_src.length() > 0.01 else -t.forward()
	var res := "hit"
	if t.balance <= 0.0:
		_stagger(t, "knockdown", 1.1, info)
		t.balance = 45.0
		res = "knockdown"
	elif bal >= 25.0:
		_stagger(t, "heavy", 0.5, info)
	else:
		_stagger(t, "light", 0.26, info)
	emit("hit", {"actor": t.id, "attacker": info.get("attacker", -1), "damage": dmg, "kind": kind,
		"result": res, "body": info.get("body", -1)})
	t.last_result = res
	return res


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
	for b in bodies:
		if b.alive and b.form == Sim.Form.WALL and b.wall_rise > 0.5:
			for k in range(1, 8):
				if point_in_wall(p0.lerp(p1, k / 8.0), b):
					return true
	return false


func los(p0: Vector3, p1: Vector3) -> bool:
	return arena.has_los(p0, p1) and not _wall_between(p0, p1)


# ============================================================== grips / control

## Queue a control attempt; resolved after all actors acted this tick.
func request_grip(a: ActorState, b: MatBody, strength: float, verb: String) -> void:
	if b.mass > a.max_control_mass:
		emit("control_fail", {"actor": a.id, "body": b.id, "reason": "mass", "mass": b.mass})
		return
	_grips.append({"actor": a.id, "body": b.id, "strength": strength, "verb": verb})


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
	b.controller = a.id
	b.authority = strength
	b.residual_owner = -1
	b.residual_authority = 0.0
	b.attack_id = 0
	b.attack_owner = -1
	b.hit_set.clear()
	b.on_ground = false
	b.rest_time = 0.0
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
	b.residual_authority = RESIDUAL_START
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
		match b.form:
			Sim.Form.WAVE:
				_update_wave(b, dt)
			Sim.Form.WALL:
				_update_wall(b, dt)
			Sim.Form.POOL, Sim.Form.PUDDLE:
				pass
			Sim.Form.CLOUD:
				b.pos += (b.vel + Vector3(0, 0.6, 0)) * dt
				b.vel *= 0.96
				if b.age > b.max_life:
					mass_ledger.vapor += b.mass
					remove_body(b, "dissipated")
			_:
				_update_ballistic(b, dt)
		if b.max_life > 0.0 and b.age > b.max_life and b.alive and b.form != Sim.Form.CLOUD:
			_decay_body(b, "lifetime")


func _on_phase_changed(b: MatBody, old_phase: int) -> void:
	if b.is_stone():
		if b.phase == Sim.Phase.SOLID and b.form == Sim.Form.WAVE:
			b.form = Sim.Form.CHUNK
			b.vel = Vector3.ZERO
			b.attack_id = 0
			b.on_ground = true
			b.max_life = Sim.REMNANT_LIFETIME
			b.age = 0.0
			emit("transform", {"body": b.id, "from": "wave", "to": "rock", "why": "cooled"})
		elif b.phase == Sim.Phase.SOLID and b.form == Sim.Form.BLOB:
			b.form = Sim.Form.CHUNK
			b.max_life = Sim.REMNANT_LIFETIME
			b.age = 0.0
			emit("transform", {"body": b.id, "from": "lava", "to": "rock", "why": "cooled"})
		elif b.phase == Sim.Phase.MOLTEN and old_phase != Sim.Phase.MOLTEN and b.form == Sim.Form.CHUNK:
			b.form = Sim.Form.BLOB
			emit("transform", {"body": b.id, "from": "stone", "to": "molten", "why": "heated"})
	elif b.is_water():
		if b.phase == Sim.Phase.LIQUID and old_phase == Sim.Phase.FROZEN:
			if b.form == Sim.Form.SHARD or b.form == Sim.Form.CHUNK:
				b.form = Sim.Form.PUDDLE
				b.vel = Vector3.ZERO
				b.attack_id = 0
				b.pos.y = arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.2)
				b.update_radius_puddle()
				emit("transform", {"body": b.id, "from": "ice", "to": "water", "why": "melted"})
				_merge_puddle(b)
			elif b.form == Sim.Form.PUDDLE:
				emit("transform", {"body": b.id, "from": "ice", "to": "water", "why": "melted"})
		elif b.phase == Sim.Phase.FROZEN:
			emit("transform", {"body": b.id, "from": "water", "to": "ice", "why": "frozen"})


func _update_ballistic(b: MatBody, dt: float) -> void:
	if b.on_ground and b.vel.length() < 0.05:
		b.rest_time += dt
		return
	b.vel.y -= Sim.GRAVITY * dt
	if b.vel.length() > b.max_speed:
		b.vel = b.vel.normalized() * b.max_speed
	var np := b.pos + b.vel * dt
	var t := arena.segment_hit(b.pos, np, b.radius * 0.8)
	var hit_wall_body: MatBody = null
	for w in bodies:
		if w.alive and w.form == Sim.Form.WALL and w.wall_rise > 0.3 and w != b and point_in_wall(np, w, b.radius * 0.7):
			hit_wall_body = w
			break
	if hit_wall_body != null:
		_body_hits_wall(b, hit_wall_body)
		return
	if t >= 0.0:
		np = b.pos.lerp(np, maxf(0.0, t - 0.02))
		_body_impact(b, "wall")
		b.vel = Vector3(-b.vel.x * 0.15, maxf(b.vel.y, 0.0) * 0.2, -b.vel.z * 0.15)
	var g := arena.ground_height(np.x, np.z, np.y + 0.3)
	var bottom := b.radius * (0.25 if b.form == Sim.Form.BLOB else 0.8)
	if np.y - bottom <= g:
		np.y = g + bottom
		if b.vel.y < -2.0 and b.attack_id != 0:
			_body_impact(b, "ground")
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
	if arena.in_pool(b.pos.x, b.pos.z) and b.pos.y < arena.pool_level + 0.1 and b.is_stone() and b.liquid > 0.0:
		_quench(b, pool, dt)


func _body_impact(b: MatBody, what: String) -> void:
	emit("impact", {"body": b.id, "on": what, "speed": b.vel.length(), "mass": b.mass})
	if b.form == Sim.Form.SHARD:
		_shatter(b)
		return
	b.attack_id = 0


func _body_hits_wall(b: MatBody, w: MatBody) -> void:
	var owner := get_actor(w.controller if w.controller >= 0 else w.last_actor)
	var perfect := owner != null and owner.guarding and owner.wall_body == w.id and perfect_guard(owner)
	if perfect and b.is_stone() and b.mass <= owner.max_control_mass and b.attack_owner != owner.id:
		var tgt := get_actor(b.attack_owner)
		var dir := (tgt.chest() - b.pos).normalized() if tgt != null else owner.forward()
		var spd := maxf(b.vel.length(), 12.0) * 1.05
		b.vel = dir * spd
		b.attack_id = new_attack_id()
		b.attack_owner = owner.id
		b.hit_set.clear()
		b.hit_set[owner.id] = true
		b.residual_owner = owner.id
		b.residual_authority = RESIDUAL_START
		b.pos += dir * 0.3
		b.touch(owner.id, "redirect", tick)
		emit("perfect_deflect", {"actor": owner.id, "body": b.id, "verb": "redirect", "kind": "stone"})
		return
	var momentum := b.vel.length() * b.mass
	w.wall_damage_add(momentum / 900.0)
	b.vel = -b.vel * 0.12
	b.vel.y = 1.0
	b.attack_id = 0
	emit("block", {"actor": owner.id if owner != null else -1, "body": b.id, "kind": "wall", "wall": w.id})
	if b.is_stone() and b.liquid > 0.0:
		pass
	if w.wall_damage >= 1.0:
		_crumble_wall(w)


func _crumble_wall(w: MatBody) -> void:
	emit("wall_crumble", {"body": w.id})
	var owner := get_actor(w.last_actor)
	if owner != null and owner.wall_body == w.id:
		owner.wall_body = -1
	# Rubble: two usable 20 kg stones split off, the rest sinks back into the ground.
	for k in 2:
		var off := Vector3(cos(w.wall_yaw), 0, -sin(w.wall_yaw)) * (0.5 if k == 0 else -0.5)
		var c := split_body(w, Sim.STONE_SHOT_MASS, w.pos + off + Vector3(0, 0.4, 0))
		c.form = Sim.Form.CHUNK
		c.vel = Vector3(0, 2.0, 0)
		c.max_life = Sim.REMNANT_LIFETIME
		c.update_radius()
	mass_ledger.ground_returned += w.mass
	ledger.removed += w.thermal_energy()
	w.mass = 0.0
	remove_body(w, "crumbled")


func _update_wall(w: MatBody, dt: float) -> void:
	var owner := get_actor(w.last_actor)
	var keep := owner != null and owner.wall_body == w.id and owner.guarding
	if keep:
		w.wall_rise = minf(1.0, w.wall_rise + dt / 0.14)
		w.age = 0.0
	else:
		w.wall_rise -= dt / 0.35
		if w.wall_rise <= 0.0:
			mass_ledger.ground_returned += w.mass
			ledger.removed += w.thermal_energy()
			if owner != null and owner.wall_body == w.id:
				owner.wall_body = -1
			remove_body(w, "sank")


func _update_wave(b: MatBody, dt: float) -> void:
	var pour_def: Dictionary = Moves.DEFS.pour
	var speed := float(pour_def.wave_speed) * Thermal.flow_factor(b)
	if b.wave_budget <= 0.0 or speed < 0.35:
		_settle_wave(b, "budget" if b.wave_budget <= 0.0 else "viscous")
		return
	# The pouring fighter keeps bending the wave toward their target while it is fluid
	# (fantasy rule: limited turn rate, fades as it cools).
	var owner := get_actor(b.attack_owner)
	if owner != null and b.liquid > 0.4:
		var tgt := get_actor(owner.lock_target)
		if tgt != null:
			var to := tgt.pos - b.pos
			to.y = 0.0
			if to.length() > 1.0:
				var want := atan2(to.x, to.z)
				var cur := atan2(b.wave_dir.x, b.wave_dir.z)
				var diff := wrapf(want - cur, -PI, PI)
				var max_turn := deg_to_rad(WAVE_TURN_RATE) * dt * Thermal.flow_factor(b)
				if absf(diff) < deg_to_rad(70.0):
					cur += clampf(diff, -max_turn, max_turn)
					b.wave_dir = Vector3(sin(cur), 0.0, cos(cur))
	var stepv := b.wave_dir * speed * dt
	var np := b.pos + stepv
	var g0 := b.pos.y
	var raw_top := arena.ground_height(np.x, np.z, g0, 100.0)
	var blocked := raw_top > g0 + Sim.WAVE_STEP
	for w in bodies:
		if w.alive and w.form == Sim.Form.WALL and w.wall_rise > 0.3 and point_in_wall(np + Vector3(0, 0.2, 0), w, b.wave_width * 0.3):
			blocked = true
			emit("block", {"actor": w.last_actor, "body": b.id, "kind": "wave_wall", "wall": w.id})
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
	if arena.in_pool(np.x, np.z):
		_quench(b, pool, dt)
	var pd := puddle_at(np)
	if pd != null:
		_quench(b, pd, dt)


func _settle_wave(b: MatBody, why: String) -> void:
	b.form = Sim.Form.BLOB if b.liquid > 0.0 else Sim.Form.CHUNK
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.on_ground = true
	b.max_life = Sim.REMNANT_LIFETIME
	b.age = 0.0
	emit("wave_settle", {"body": b.id, "why": why})


## Lava touching water: rapid heat loss, water flashes to steam (bounded by both masses).
func _quench(lava: MatBody, water: MatBody, dt: float) -> void:
	var q := minf(Sim.QUENCH_RATE * dt, lava.thermal_energy())
	if q <= 0.0 or water.mass <= 0.0:
		return
	var applied := -Thermal.apply_heat(lava, -q).x
	var kg := boil_water(water, applied, lava.pos + Vector3(0, 0.3, 0))
	if water.form == Sim.Form.PUDDLE and water.mass <= 0.05:
		remove_body(water, "boiled")
	if tick % 6 == 0:
		emit("steam", {"body": lava.id, "water": water.id, "kg": kg})


## Flash-boils water with `energy` HU at a contact surface; energy the water can't
## take (not enough mass) is lost to the air. Returns vaporised kg.
func boil_water(water: MatBody, energy: float, at: Vector3) -> float:
	var r := Thermal.flash_boil(water, energy)
	ledger.vapor += Thermal.vapor_energy(r.x)
	ledger.ambient -= energy - r.y
	_spawn_steam(at, r.x)
	return r.x


func _spawn_steam(p: Vector3, kg: float) -> void:
	if kg <= 0.0:
		return
	# Merge into a nearby young cloud to bound body count.
	for c in bodies:
		if c.alive and c.form == Sim.Form.CLOUD and c.pos.distance_to(p) < 2.0 and c.age < 1.5:
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
	emit("transform", {"body": b.id, "from": "stream", "to": "puddle", "why": "landed"})
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


func _decay_body(b: MatBody, why: String) -> void:
	if b.is_stone():
		mass_ledger.ground_returned += b.mass
	elif b.is_water():
		mass_ledger.evaporated += b.mass
	ledger.removed += b.thermal_energy()
	remove_body(b, why)


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
					b.hit_set[a.id] = true
					hit_actor(a, {"attacker": b.attack_owner, "attack_id": b.attack_id, "damage": b.damage,
						"balance": b.balance_damage, "knock": b.wave_dir * 4.0 + Vector3(0, 3.0, 0),
						"kind": "lava", "from": b.pos - b.wave_dir, "body": b.id})
		if b.is_stone() and b.temp >= Sim.HOT_ROCK_C and b.controller < 0 and b.form != Sim.Form.WAVE:
			for a in actors:
				if a.burn_cd <= 0.0 and _touches_actor(b, a, 0.15):
					a.burn_cd = 0.8
					a.health = maxf(0.0, a.health - 3.0)
					emit("burn", {"actor": a.id, "body": b.id})


func _touches_actor(b: MatBody, a: ActorState, pad: float) -> bool:
	var lo := a.pos.y + 0.2
	var hi := a.pos.y + Sim.ACTOR_HEIGHT
	var cy := clampf(b.pos.y, lo, hi)
	var d := Vector3(b.pos.x - a.pos.x, b.pos.y - cy, b.pos.z - a.pos.z).length()
	return d < b.radius + Sim.ACTOR_RADIUS + pad


func _projectile_hits_actor(b: MatBody, a: ActorState) -> void:
	b.hit_set[a.id] = true
	var to_src := -b.vel
	to_src.y = 0
	var facing_ok := a.forward().dot(to_src.normalized()) > -0.15 if to_src.length() > 0.01 else true
	if a.iframes > 0.0:
		emit("evaded", {"actor": a.id, "body": b.id})
		return
	if a.guarding and facing_ok:
		var perfect := perfect_guard(a)
		if perfect and a.element == Sim.Element.EARTH and b.is_stone() and b.mass <= a.max_control_mass:
			var tgt := get_actor(b.attack_owner)
			var dir := (tgt.chest() - b.pos).normalized() if tgt != null else a.forward()
			b.vel = dir * maxf(b.vel.length(), 12.0) * 1.05
			b.attack_id = new_attack_id()
			b.attack_owner = a.id
			b.hit_set.clear()
			b.hit_set[a.id] = true
			b.touch(a.id, "redirect", tick)
			emit("perfect_deflect", {"actor": a.id, "body": b.id, "verb": "redirect", "kind": "stone"})
			return
		if perfect or (a.element == Sim.Element.AIR and b.mass < 30.0):
			var side := a.forward().cross(Vector3.UP).normalized()
			if side.dot(b.vel) < 0.0:
				side = -side
			b.vel = side * b.vel.length() * 0.5 + Vector3(0, 2.5, 0)
			b.attack_id = 0
			emit("deflect" if not perfect else "perfect_deflect", {"actor": a.id, "body": b.id, "verb": "deflect", "kind": "stone"})
			return
	var res := hit_actor(a, {"attacker": b.attack_owner, "attack_id": b.attack_id, "damage": b.damage,
		"balance": b.balance_damage, "knock": b.vel.normalized() * minf(b.mass * b.vel.length() / 70.0, 9.0),
		"kind": "stone" if b.is_stone() else "water", "from": b.pos - b.vel.normalized(), "body": b.id})
	if b.is_water() and b.phase == Sim.Phase.LIQUID:
		a.wetness = 1.0
	if b.is_stone() and b.temp >= Sim.HOT_ROCK_C and res != "block":
		a.health = maxf(0.0, a.health - 4.0)
		emit("burn", {"actor": a.id, "body": b.id})
	if res == "block" or res == "hit" or res == "knockdown" or res == "guard_break":
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
	# Decay the oldest inert remnant (never a held/attacking/static body).
	for b in bodies:
		if b.alive and b.controller < 0 and b.attack_id == 0 and not b.static_body and b.form != Sim.Form.WALL:
			_decay_body(b, "cap")
			return


## Counts remnants and trims the oldest beyond the cap (called by scenarios / periodically).
func trim_remnants() -> void:
	var rem: Array[MatBody] = []
	for b in bodies:
		if b.alive and b.is_stone() and b.controller < 0 and b.attack_id == 0 and b.on_ground and b.form != Sim.Form.WALL:
			rem.append(b)
	while rem.size() > Sim.MAX_REMNANTS:
		_decay_body(rem.pop_front(), "remnant_cap")


# ============================================================== accounting

func system_energy() -> float:
	var e := 0.0
	for b in bodies:
		if b.alive:
			e += b.thermal_energy()
	for a in actors:
		e += a.heat_reserve
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
	return m + mass_ledger.vapor + mass_ledger.evaporated


func stone_mass() -> float:
	var m := 0.0
	for b in bodies:
		if b.alive and b.mat == Sim.Mat.STONE:
			m += b.mass
	return m + mass_ledger.ground_returned - mass_ledger.ground_taken
