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
var _pour_seen := {}       # body id -> when the foe's pour of it was perceived (its wave's telegraph)
var _next_attack := 2.0
var _strafe := 1.0
var _strafe_t := 0.0
var _hold := ""            # "tech", "guard", "attack" while holding
var _hold_until := 0.0
var _hold_body := -1
var _hold_start := 0.0
var _hold_started := false # tech hold: our technique action has actually begun
var _hold_from: ActionInst = null  # action that was running when the tech hold was pressed
var _guard_attack := 0     # foe attack instance a guard hold answers (kept up while it charges)
var _detour := 1.0         # which way round the pool we turn (+1 / -1)
var _detour_t := 0.0       # > 0 shortly after detouring (probe further: no edge dithering)
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
	if _guard_at >= 0.0 or _hold != "":
		_react_to_threats(foe, true)   # keep perceiving while busy; decide once free
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

func _continue_holds(foe: ActorState) -> void:
	match _hold:
		"guard":
			intent.guard_held = true
			# The strike we guard against is still visibly winding up (charges have no time
			# limit): keep the guard up until it is released.
			if _guard_attack != 0 and foe != null and foe.action != null and foe.action.attack_id == _guard_attack \
					and (foe.action.phase == ActionInst.P.STARTUP or foe.action.phase == ActionInst.P.CHARGE):
				_hold_until = maxf(_hold_until, _t + 0.15)
			if _t >= _hold_until:
				_hold = ""
				_guard_attack = 0
				intent.guard_held = false
		"tech":
			intent.tech_held = true
			var b := w.get_body(_hold_body)
			var act := me.action
			if not _hold_started and act != null and act != _hold_from and (act.id == "fire_tech" or act.id == "earth_tech"):
				_hold_started = true
			var done := _t >= _hold_until
			if not _hold_started:
				# Pressed while busy: the world buffers a press only briefly, so keep pressing
				# until the technique starts (give up after 1 s or once the target is gone).
				done = done or _t - _hold_start > 1.0 or b == null or not b.alive
				intent.tech_pressed = not done
			elif act == null:
				done = true
			elif act.id == "fire_tech" and String(act.data.get("mode", "")) == "DRAW":
				# Keep drawing until the lava has set (or we can't anymore).
				if b == null or not b.alive or (b.phase == Sim.Phase.SOLID and b.temp < 700.0) or me.heat_reserve >= Sim.RESERVE_MAX - 5.0 or me.focus < 2.0:
					done = true
			elif act.id == "earth_tech":
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


func _react_to_threats(foe: ActorState, stamp_only: bool = false) -> bool:
	## stamp_only: busy (holding) - start the reaction clock on new threats, decide nothing yet.
	# The foe's pour wind-up is the wave's telegraph (the wave is the same body).
	if foe != null and foe.action != null and foe.action.id == "pour" and foe.held_body >= 0:
		var pk := "pour%d" % foe.action.attack_id
		_perceived(pk)
		_pour_seen[foe.held_body] = _seen[pk]
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
			if _pour_seen.has(b.id):
				if not _seen.has(key):
					_seen[key] = _pour_seen[b.id]   # seen coming since the pour began
				_pour_seen.erase(b.id)
			if not _perceived(key) or stamp_only:
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
			if not _perceived(key) or stamp_only:
				continue
			if _decided.has(key):
				continue
			_decided[key] = _choose_projectile_response(b, tti)
			return _act_on(_decided[key], b, tti)
	# Visible lightning charge from the foe.
	if foe != null and foe.action != null and foe.action.id == "fire_attack" and foe.action.phase == ActionInst.P.CHARGE and foe.has("lightning"):
		var key := "bolt%d" % foe.action.attack_id
		if _perceived(key) and not stamp_only and not _decided.has(key):
			_decided[key] = "bolt"
			if me.surface != "stone":
				intent.move = Vector3(-1, 0, 0) if me.pos.x > 0.0 else Vector3(1, 0, 0)
				_press("evade")
			elif me.element == Sim.Element.EARTH or _switch_then(Sim.Element.EARTH, "guard"):
				if me.element == Sim.Element.EARTH:
					_press("guard")
				_hold = "guard"
				_hold_until = _t + 0.9
				_guard_attack = foe.action.attack_id
			return true
	# Close-range strikes still winding up: taps (< 0.2 s) end before any reaction, held
	# blaze / gust / lance charges do not. Lightning charges are handled above.
	if foe != null and foe.action != null and foe.pos.distance_to(me.pos) < 5.0:
		var mid: String = foe.action.id
		var winding := foe.action.phase == ActionInst.P.STARTUP \
				or (foe.action.phase == ActionInst.P.CHARGE and not (mid == "fire_attack" and foe.has("lightning")))
		if winding and mid in ["fire_attack", "air_attack", "water_attack"]:
			var key := "m%d" % foe.action.attack_id
			if _perceived(key) and not stamp_only and not _decided.has(key):
				_decided[key] = "melee"
				if rng.randf() < float(cfg.counter):
					_press("guard")
					_hold = "guard"
					_hold_until = _t + 0.4
					_guard_attack = foe.action.attack_id
				else:
					intent.move = (me.pos - foe.pos).normalized()
					_press("evade")
				return true
	# Forget threats perceived long ago (undecided ones too, e.g. taps nobody can react to).
	if _seen.size() > 64:
		for k in _seen.keys():
			if _t - float(_seen[k]) > 8.0:
				_seen.erase(k)
				_decided.erase(k)
	return false


func _choose_wave_response(b: MatBody, dist: float) -> String:
	var counter := rng.randf() < float(cfg.counter)
	if counter and me.has("heat_draw") and cfg.elements.has(Sim.Element.FIRE) and me.focus >= 10.0 and me.heat_reserve < Sim.RESERVE_MAX - 60.0:
		if w.los(me.chest(), b.pos + Vector3(0, 0.3, 0)) and _draw_sets_in_time(b):
			return "draw"
	if counter and cfg.elements.has(Sim.Element.EARTH) and me.focus >= 10.0 and dist > 3.0:
		return "wall"
	return "dodge"


func _draw_sets_in_time(b: MatBody) -> bool:
	## Can a heat draw decided now stop this wave short of us? Tick-by-tick estimate with the
	## sim's own rules: finish the current action (and switch to Fire), wait for draw range,
	## wind up; then the draw (rate falls off with range, bounded by reserve room and Focus)
	## and passive loss cool a scratch copy of the wave while it flows at its fluidity-scaled
	## speed, straight at us (worst case). Lava damage is all-or-nothing: a late draw is no draw.
	var fd: Dictionary = Moves.DEFS.fire_tech
	var c := MatBody.new()
	c.mat = b.mat
	c.mass = b.mass
	c.temp = b.temp
	c.liquid = b.liquid
	var reach := b.wave_width * 0.5 + Sim.ACTOR_RADIUS + 0.1
	var flat := Vector2(me.pos.x - b.pos.x, me.pos.z - b.pos.z).length()
	var dy := me.chest().y - b.pos.y
	var budget := b.wave_budget
	var cap := minf(Sim.RESERVE_MAX - me.heat_reserve, me.focus * Sim.DRAW_HU_PER_FOCUS)
	var free := roundi(_busy_time() / Sim.DT) + (0 if me.element == Sim.Element.FIRE else 1)
	var first := 1 << 30   # tick of the first draw
	for i in 4 * Sim.HZ:
		if first == 1 << 30 and flat <= float(fd.draw_range) - 0.5:
			first = maxi(i, free) + roundi(float(fd.draw_startup) / Sim.DT)
		if i >= first and cap > 0.0:
			var d := sqrt(flat * flat + dy * dy)
			var rate := float(fd.draw_rate) * (1.0 - 0.5 * clampf((d - 3.0) / (float(fd.draw_range) - 3.0), 0.0, 1.0))
			cap += Thermal.heat(c, -minf(rate * Sim.DT, cap))
		Thermal.ambient_step(c, Sim.DT)
		var speed := float(Moves.DEFS.pour.wave_speed) * Thermal.flow_factor(c)
		if speed < 0.35 or budget <= 0.0:
			return true   # set (or spent) short of us
		flat -= speed * Sim.DT
		budget -= speed * Sim.DT
		if flat < reach:
			return false
	return true


func _busy_time() -> float:
	## Seconds until my current action ends and a technique press can start (a guard is
	## cancelled at once; held charges/channels are ours to release).
	var a := me.action
	if a == null or a.id == "guard" or a.phase == ActionInst.P.CHARGE or a.phase == ActionInst.P.CHANNEL:
		return 0.0
	var left := float(a.def.get("recovery", 0.0))
	if a.phase == ActionInst.P.RECOVERY:
		return maxf(0.0, left - a.t)
	left += float(a.data.get("active", a.def.get("active", 0.0)))
	if a.phase == ActionInst.P.ACTIVE:
		return maxf(0.0, left - a.t)
	return left + maxf(0.0, float(a.data.get("startup", a.def.get("startup", 0.0))) - a.total)


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
	_hold_tech(b.id, 4.0)
	if me.element != Sim.Element.FIRE:
		intent.element_select = Sim.Element.FIRE
		_pending_press = "tech"
	else:
		_press("tech")


func _hold_tech(body_id: int, secs: float) -> void:
	## Hold the technique button; _continue_holds re-presses until the action really starts.
	_hold = "tech"
	_hold_body = body_id
	_hold_start = _t
	_hold_until = _t + secs
	_hold_started = false
	_hold_from = me.action


# ------------------------------------------------------------------ opportunities / offense

func _opportunities(foe: ActorState) -> bool:
	# Foe holding molten material near us: maybe draw its heat (contest the conversion).
	# The chance is rolled once per hold (the foe's action instance), not every tick.
	var held := w.get_body(foe.held_body)
	var act := foe.action
	if held != null and act != null and act.id != "pour" and held.is_stone() and held.liquid > 0.3 and me.has("heat_draw") and cfg.elements.has(Sim.Element.FIRE):
		var key := "molten%d:%d" % [held.id, act.attack_id]
		if _perceived(key) and not _decided.has(key) and me.pos.distance_to(held.pos) < 8.5:
			_decided[key] = "draw" if rng.randf() < float(cfg.counter) * 0.35 else "skip"
			if _decided[key] == "draw":
				return _act_on("draw", held)
	return false


func _offense(foe: ActorState) -> void:
	var drill: String = cfg.drill
	var dist := me.pos.distance_to(foe.pos)
	if _t < _next_attack or me.action != null:
		return
	if not w.arena.has_los(me.chest(), foe.chest()):
		# Behind cover: don't throw into it (_move_tactical steps out); a guard wall doesn't count.
		_next_attack = _t + 0.3
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
			if _switch_then(Sim.Element.EARTH, "tech"):
				_hold_tech(loose.id, 0.9)
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
		if _switch_then(Sim.Element.EARTH, "tech"):
			_hold_tech(hot.id, 0.75)
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
	# Cover between us (arena geometry, not guard walls): side-step toward whichever side
	# regains sight and don't flip strafe until it does; pushing toward the foe only grinds into the wall.
	var blind := not w.arena.has_los(me.chest(), foe.chest())
	if blind:
		var probe := dir.cross(Vector3.UP) * 2.0
		var pos_ok := w.arena.has_los(me.chest() + probe, foe.chest())
		if pos_ok != w.arena.has_los(me.chest() - probe, foe.chest()):
			_strafe = 1.0 if pos_ok else -1.0
		side = dir.cross(Vector3.UP) * _strafe
	_strafe_t -= Sim.DT
	if _strafe_t <= 0.0 and not blind:
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
	elif not blind and _wet(side, 2.0) and not _wet(-side, 2.0):
		# Holding range but strafing into the pool: strafe the other way round.
		_strafe = -_strafe
		_strafe_t = maxf(_strafe_t, 1.2)
		side = -side
	want = side if blind else want + side * 0.45
	# Don't wander into the pool unless chasing: steer round it.
	want = _around_pool(want)
	intent.move = want.limit_length(1.0) * amount * (0.55 if dist < hi else 0.9)


func _around_pool(want: Vector3) -> Vector3:
	## Turns `want` away from the pool by the smallest angle that keeps the path ahead dry.
	## It keeps turning the same way round unless the other way is clearly (>= 30 deg) shorter,
	## and probes further while detouring, so the step can't flip back and forth at the edge
	## and a foe across the pool is reached by going round it.
	var reach := 2.0 if _detour_t > 0.0 else 1.5
	_detour_t -= Sim.DT
	if want.length() < 0.01 or not _wet(want, reach):
		return want
	_detour_t = 1.0
	var k := _turn_steps(want, _detour, reach)
	var k_other := _turn_steps(want, -_detour, reach)
	if k_other + 2 <= k:
		_detour = -_detour
		k = k_other
	if k > 12:
		return want   # deep in the pool already: just keep going
	return want.rotated(Vector3.UP, _detour * k * PI / 12.0)


func _wet(v: Vector3, reach: float) -> bool:
	## Do the next `reach` metres along v come within 0.4 m of the pool? (Sampled every
	## 0.25 m with a margin, so corners aren't cut and momentum doesn't carry us in.)
	var a := w.arena
	var n := v.normalized()
	for k in range(1, int(reach / 0.25) + 1):
		var p := me.pos + n * (0.25 * k)
		if p.x > a.pool_min.x - 0.4 and p.x < a.pool_max.x + 0.4 and p.z > a.pool_min.y - 0.4 and p.z < a.pool_max.y + 0.4:
			return true
	return false


func _turn_steps(v: Vector3, sense: float, reach: float) -> int:
	## 15 deg steps v must turn (sense +1 / -1) before its path is dry; 13 = no dry heading.
	for k in range(1, 13):
		if not _wet(v.rotated(Vector3.UP, sense * k * PI / 12.0), reach):
			return k
	return 13
