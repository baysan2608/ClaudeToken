class_name AiBrain
extends RefCounted
## Sparring opponent. Reads only observable world state (positions, bodies in
## flight, visible actions/telegraphs), reacts after a human-like delay, pays the
## same costs and is limited to its configured kit. Produces an ActorIntent per tick.
##
## cfg keys: aggression 0..1, counter 0..1 (chance to use the best counter),
## reaction s, elements Array[int], drill String ("" = free sparring,
## "stone_rain", "passive", "lightning"), interval s (drills).

var w: CombatWorld
var me: ActorState
var cfg := {"aggression": 0.55, "counter": 0.75, "reaction": 0.28, "elements": [0, 2], "drill": "", "interval": 2.6}
var intent := ActorIntent.new()
var rng := RandomNumberGenerator.new()

var _t := 0.0
var _seen := {}            # threat key -> time first perceived
var _decided := {}         # threat key -> decision string (decide once per threat)
var _next_attack := 2.0
var _strafe := 1.0
var _strafe_t := 0.0
var _hold := ""            # "tech", "guard", "attack" while holding
var _hold_until := 0.0
var _hold_body := -1
var _hold_start := 0.0
var _await_draw := -1      # wave we decided to draw from, waiting for range
var _pending_press := ""   # press after an element switch
var _guard_at := -1.0      # scheduled timed guard press
var _last_mode := ""
var debug_state := ""


func _init(world: CombatWorld, actor: ActorState, config: Dictionary = {}, seed_value: int = 3) -> void:
	w = world
	me = actor
	cfg.merge(config, true)
	rng.seed = seed_value
	_next_attack = 1.5 + rng.randf()


func think(dt: float) -> ActorIntent:
	_t += dt
	intent.clear()
	var foe := w.get_actor(me.lock_target)
	if me.health <= 0.0:
		return intent
	if me.stun > 0.0:
		_hold = ""
		_pending_press = ""
		debug_state = "stunned"
		return intent
	_continue_holds(foe)
	if _pending_press != "":
		_press(_pending_press)
		_pending_press = ""
		return intent
	if _guard_at >= 0.0:
		if _t >= _guard_at:
			_press("guard")
			_hold = "guard"
			_hold_until = _t + 0.45
			_guard_at = -1.0
		_move_tactical(foe, 0.0)
		return intent
	if _hold != "":
		_move_tactical(foe, 0.3)
		return intent
	if _await_draw >= 0:
		var wb := w.get_body(_await_draw)
		if wb == null or not wb.alive or wb.form != Sim.Form.WAVE:
			_await_draw = -1
		elif me.pos.distance_to(wb.pos) <= float(Moves.DEFS.fire_tech.draw_range) - 0.5:
			_await_draw = -1
			_start_draw(wb)
			return intent
		else:
			if me.element != Sim.Element.FIRE:
				intent.element_select = Sim.Element.FIRE
			return intent
	if _react_to_threats(foe):
		return intent
	if foe == null:
		return intent
	if cfg.drill == "passive":
		debug_state = "passive"
		return intent
	if _opportunities(foe):
		return intent
	_offense(foe)
	_move_tactical(foe, 1.0)
	return intent


# ------------------------------------------------------------------ holds

func _continue_holds(_foe: ActorState) -> void:
	match _hold:
		"guard":
			intent.guard_held = true
			if _t >= _hold_until:
				_hold = ""
				intent.guard_held = false
		"tech":
			intent.tech_held = true
			var b := w.get_body(_hold_body)
			var done := _t >= _hold_until or (me.action == null and _t - _hold_start > 0.2)
			if me.action != null and me.action.id == "fire_tech" and String(me.action.data.get("mode", "")) == "DRAW":
				# Keep drawing until the lava has set (or we can't anymore).
				if b == null or not b.alive or (b.phase == Sim.Phase.SOLID and b.temp < 700.0) or me.heat_reserve >= Sim.RESERVE_MAX - 5.0 or me.focus < 2.0:
					done = true
			elif me.action != null and me.action.id == "earth_tech":
				done = done or (w.held(me) != null and _t >= _hold_until - 0.0)
			if done:
				_hold = ""
				intent.tech_held = false
				intent.tech_released = true
		"attack":
			intent.attack_held = true
			if _t >= _hold_until:
				_hold = ""
				intent.attack_held = false
				intent.attack_released = true


func _press(what: String) -> void:
	_hold_start = _t
	match what:
		"attack":
			intent.attack_pressed = true
			intent.attack_held = true
			intent.attack_released = false
		"guard":
			intent.guard_pressed = true
			intent.guard_held = true
		"tech":
			intent.tech_pressed = true
			intent.tech_held = true
		"evade":
			intent.evade_pressed = true


func _switch_then(element: int, what: String) -> bool:
	## Switch element (if allowed) and press on the next tick. Returns false if unavailable.
	if not cfg.elements.has(element) or not me.elements[element]:
		return false
	if me.element != element:
		intent.element_select = element
		_pending_press = what
	else:
		_press(what)
	return true


# ------------------------------------------------------------------ threats

func _perceived(key: String) -> bool:
	if not _seen.has(key):
		_seen[key] = _t + rng.randf_range(-0.05, 0.08)
		return false
	return _t - float(_seen[key]) >= float(cfg.reaction)


func _react_to_threats(foe: ActorState) -> bool:
	# Incoming material attacks.
	for b in w.bodies:
		if not b.alive or b.attack_id == 0 or b.attack_owner == me.id or b.controller >= 0:
			continue
		var key := "b%d:%d" % [b.id, b.attack_id]
		if b.form == Sim.Form.WAVE:
			var to := me.pos - b.pos
			to.y = 0
			if to.length() > 16.0 or to.normalized().dot(b.wave_dir) < 0.5:
				continue
			if not _perceived(key):
				continue
			if _decided.has(key):
				continue
			_decided[key] = _choose_wave_response(b, to.length())
			return _act_on(_decided[key], b)
		elif b.is_projectile():
			var rel := me.chest() - b.pos
			var closing := b.vel.dot(rel.normalized())
			if closing < 2.0 or rel.length() > 18.0:
				continue
			# Will it pass close to me?
			var tti := rel.length() / closing
			var miss := (b.pos + b.vel * tti - me.chest())
			miss.y *= 0.5
			if miss.length() > 1.6:
				continue
			if not _perceived(key):
				continue
			if _decided.has(key):
				continue
			_decided[key] = _choose_projectile_response(b, tti)
			return _act_on(_decided[key], b, tti)
	# Visible lightning charge from the foe.
	if foe != null and foe.action != null and foe.action.id == "fire_attack" and foe.action.phase == ActionInst.P.CHARGE and foe.has("lightning"):
		var key := "bolt%d" % foe.action.attack_id
		if _perceived(key) and not _decided.has(key):
			_decided[key] = "bolt"
			if me.surface != "stone":
				intent.move = Vector3(-1, 0, 0) if me.pos.x > 0.0 else Vector3(1, 0, 0)
				_press("evade")
			elif me.element == Sim.Element.EARTH or _switch_then(Sim.Element.EARTH, "guard"):
				if me.element == Sim.Element.EARTH:
					_press("guard")
				_hold = "guard"
				_hold_until = _t + 0.9
			return true
	# Close-range strikes in startup.
	if foe != null and foe.action != null and foe.action.phase == ActionInst.P.STARTUP and foe.pos.distance_to(me.pos) < 5.0:
		var mid: String = foe.action.id
		if mid in ["fire_attack", "air_attack", "water_attack"]:
			var key := "m%d" % foe.action.attack_id
			if _perceived(key) and not _decided.has(key):
				_decided[key] = "melee"
				if rng.randf() < float(cfg.counter):
					_press("guard")
					_hold = "guard"
					_hold_until = _t + 0.4
				else:
					intent.move = (me.pos - foe.pos).normalized()
					_press("evade")
				return true
	if _decided.size() > 64:
		_decided.clear()
		_seen.clear()
	return false


func _choose_wave_response(b: MatBody, dist: float) -> String:
	var counter := rng.randf() < float(cfg.counter)
	if counter and me.has("heat_draw") and cfg.elements.has(Sim.Element.FIRE) and me.focus >= 10.0 and me.heat_reserve < Sim.RESERVE_MAX - 60.0:
		if w.los(me.chest(), b.pos + Vector3(0, 0.3, 0)):
			return "draw"
	if counter and cfg.elements.has(Sim.Element.EARTH) and me.focus >= 10.0 and dist > 3.0:
		return "wall"
	return "dodge"


func _choose_projectile_response(b: MatBody, tti: float) -> String:
	var r := rng.randf()
	var counter := float(cfg.counter)
	if b.is_stone() and b.mass <= me.max_control_mass and cfg.elements.has(Sim.Element.EARTH) and r < counter * 0.55 and tti > 0.18:
		return "redirect"
	if r < counter * 0.85:
		return "block"
	return "dodge"


func _act_on(decision: String, b: MatBody, tti: float = 1.0) -> bool:
	debug_state = decision
	match decision:
		"draw":
			if me.pos.distance_to(b.pos) > float(Moves.DEFS.fire_tech.draw_range) - 0.5:
				_await_draw = b.id   # wait for range (switching to Fire meanwhile)
				if me.element != Sim.Element.FIRE:
					intent.element_select = Sim.Element.FIRE
				return true
			_start_draw(b)
			return true
		"wall":
			_switch_then(Sim.Element.EARTH, "guard")
			_hold = "guard"
			_hold_until = _t + 1.6
			return true
		"redirect":
			if me.element != Sim.Element.EARTH:
				intent.element_select = Sim.Element.EARTH
			# Time the guard press so the stone meets the rising wall inside the perfect window.
			var wall_lead := (1.25 / maxf(b.vel.length(), 1.0))
			_guard_at = _t + maxf(0.0, tti - wall_lead - 0.09 - rng.randf_range(0.0, 0.06))
			return true
		"block":
			_press("guard")
			_hold = "guard"
			_hold_until = _t + tti + 0.25
			return true
		"dodge":
			var side := me.forward().cross(Vector3.UP)
			if rng.randf() < 0.5:
				side = -side
			intent.move = side
			_press("evade")
			return true
	return false


func _start_draw(b: MatBody) -> void:
	debug_state = "draw"
	_hold = "tech"
	_hold_body = b.id
	_hold_start = _t
	_hold_until = _t + 4.0
	if me.element != Sim.Element.FIRE:
		intent.element_select = Sim.Element.FIRE
		_pending_press = "tech"
	else:
		_press("tech")


# ------------------------------------------------------------------ opportunities / offense

func _opportunities(foe: ActorState) -> bool:
	# Foe holding molten material near us: try to draw its heat (contest the conversion).
	var held := w.get_body(foe.held_body)
	if held != null and held.is_stone() and held.liquid > 0.3 and me.has("heat_draw") and cfg.elements.has(Sim.Element.FIRE):
		var key := "molten%d" % held.id
		if _perceived(key) and not _decided.has(key) and rng.randf() < float(cfg.counter) * 0.35 and me.pos.distance_to(held.pos) < 8.5:
			_decided[key] = "draw"
			return _act_on("draw", held)
	return false


func _offense(foe: ActorState) -> void:
	var drill: String = cfg.drill
	var dist := me.pos.distance_to(foe.pos)
	if _t < _next_attack or me.action != null:
		return
	var interval := float(cfg.interval) if drill != "" else lerpf(3.2, 1.1, float(cfg.aggression))
	_next_attack = _t + interval * rng.randf_range(0.8, 1.25)
	if drill == "stone_rain":
		_switch_then(Sim.Element.EARTH, "attack")
		_hold = "attack"
		_hold_until = _t + (0.6 if rng.randf() < 0.25 else 0.05)
		return
	if drill == "seize":
		var loose := _nearest_loose(9.0)
		if loose != null:
			debug_state = "seize"
			_switch_then(Sim.Element.EARTH, "tech")
			_hold = "tech"
			_hold_body = loose.id
			_hold_until = _t + 0.9
		return
	if drill == "lightning" and me.has("lightning"):
		_switch_then(Sim.Element.FIRE, "attack")
		_hold = "attack"
		_hold_until = _t + 0.85
		return
	# Free sparring: pick by range and resources.
	var hot := _nearby_rock()
	if hot != null and cfg.elements.has(Sim.Element.EARTH) and rng.randf() < 0.6:
		debug_state = "seize rock"
		_switch_then(Sim.Element.EARTH, "tech")
		_hold = "tech"
		_hold_body = hot.id
		_hold_until = _t + 0.75
		return
	if dist < 4.2 and cfg.elements.has(Sim.Element.FIRE) and me.focus > 8.0:
		debug_state = "flare"
		_switch_then(Sim.Element.FIRE, "attack")
		_hold = "attack"
		_hold_until = _t + (0.5 if rng.randf() < 0.3 else 0.05)
		return
	if cfg.elements.has(Sim.Element.EARTH) and me.focus > 10.0:
		debug_state = "stone"
		_switch_then(Sim.Element.EARTH, "attack")
		_hold = "attack"
		_hold_until = _t + (0.65 if rng.randf() < 0.25 * float(cfg.aggression) + 0.1 else 0.05)
		return
	if cfg.elements.has(Sim.Element.WATER) and me.water_carried > 2.0:
		_switch_then(Sim.Element.WATER, "attack")
		_hold = "attack"
		_hold_until = _t + 0.05
	elif cfg.elements.has(Sim.Element.AIR):
		_switch_then(Sim.Element.AIR, "attack")
		_hold = "attack"
		_hold_until = _t + 0.05


func _nearest_loose(r: float) -> MatBody:
	var best: MatBody = null
	for b in w.bodies:
		if b.alive and b.is_stone() and b.controller != me.id and b.attack_id == 0 and b.form == Sim.Form.CHUNK and b.mass <= me.max_control_mass:
			var d := me.pos.distance_to(b.pos)
			if d < r and (best == null or d < me.pos.distance_to(best.pos)):
				best = b
	return best


func _nearby_rock() -> MatBody:
	for b in w.bodies:
		if b.alive and b.is_stone() and b.controller < 0 and b.attack_id == 0 and b.on_ground and b.phase == Sim.Phase.SOLID \
				and b.form == Sim.Form.CHUNK and b.mass <= me.max_control_mass and me.pos.distance_to(b.pos) < 6.5:
			var to := b.pos - me.pos
			to.y = 0
			if to.normalized().dot(me.forward()) > 0.3:
				return b
	return null


func _move_tactical(foe: ActorState, amount: float) -> void:
	if foe == null or amount <= 0.0:
		return
	var to := foe.pos - me.pos
	to.y = 0
	var dist := to.length()
	if dist < 0.01:
		return
	var dir := to / dist
	var side := dir.cross(Vector3.UP) * _strafe
	_strafe_t -= Sim.DT
	if _strafe_t <= 0.0:
		_strafe_t = rng.randf_range(1.2, 2.8)
		if rng.randf() < 0.5:
			_strafe = -_strafe
	var want := Vector3.ZERO
	var lo := 6.5 - 2.0 * float(cfg.aggression)
	var hi := 11.0 - 2.0 * float(cfg.aggression)
	if dist > hi:
		want = dir
	elif dist < lo:
		want = -dir
	want += side * 0.45
	# Don't wander into the pool unless chasing.
	var ahead := me.pos + want.normalized() * 1.5
	if w.arena.in_pool(ahead.x, ahead.z):
		want = -want
	intent.move = want.limit_length(1.0) * amount * (0.55 if dist < hi else 0.9)
