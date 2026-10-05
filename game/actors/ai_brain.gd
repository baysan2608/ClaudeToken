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
var _pour_body := -1       # body the foe is pouring: its wave is perceived from the pour wind-up
var _pour_seen := 0.0      # when that pour was first seen
var _next_attack := 2.0
var _strafe := 1.0
var _strafe_t := 0.0
var _hold := ""            # "tech", "guard", "attack" while holding
var _hold_until := 0.0
var _hold_body := -1
var _hold_start := 0.0
var _hold_started := false # tech hold: our technique action has actually begun
var _hold_from: ActionInst = null  # action that was running when the tech hold was pressed
var _hold_draw := false    # the tech hold is a heat draw: stand still (_draw_sets_in_time assumes it)
var _guard_attack := 0     # foe attack instance a guard hold answers (kept up while it charges)
var _detour := 1.0         # which way round the pool we turn (+1 / -1)
var _detour_t := 0.0       # > 0 shortly after detouring (probe further: no edge dithering)
var _await_draw := -1      # wave we decided to draw from, waiting for range
var _pending_press := ""   # press after an element switch
var _guard_at := -1.0      # scheduled timed guard press
var _last_mode := ""
var debug_state := ""

# ------------------------------------------------------------------ planner mode (docs/AI.md)
## Difficulty presets (MOVESET §13): AiPresets.TABLE.
const PRESETS := AiPresets.TABLE
const DRILLS := ["", "passive", "stone_rain", "seize", "lightning", "matrix", "element:<e>/<sub>"]
const LEGACY_DRILLS := ["stone_rain", "seize", "lightning"]
## True once configure() ran: matrix-driven counters and offense. False = the legacy rules exactly.
var planner := false
## Preset row (+ overrides) the planner reads: reaction counter misjudge timing_err aggression chain weave punish env tiers.
var prm := {}
## Kit the planner uses: {element: [subs]}.
var kit := {}
## Last counter decision (debug overlay, Lab, tests): option dict + threat, key, tti, err.
var last_plan := {}
var _plan := {}            # counter / offense plan being executed (see _exec_plan)
var _pending_gesture := 0  # attack gesture sent with a pending (post-switch) press
var _drill_i := 0
var _matrix := []          # [[threat class, [moves]], ...] for the "matrix" drill
var _recent := {}          # move id -> recent use weight (offense variety; decays)
var _aim_hold := Vector3.ZERO  # explicit aim kept while an attack is held (bank shots)


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
		if planner:
			_plan = {}
			_guard_at = -1.0
		debug_state = "stunned"
		return intent
	_continue_holds(foe)
	if planner and _aim_hold != Vector3.ZERO:
		if _hold == "attack" or (me.action != null and me.action.phase != ActionInst.P.RECOVERY):
			intent.aim_dir = _aim_hold
			intent.aim_active = true
		else:
			_aim_hold = Vector3.ZERO
	if planner and not _plan.is_empty():
		_exec_plan(foe)
	if _pending_press != "":
		_press(_pending_press)
		if _pending_gesture != 0:
			intent.attack_gesture = _pending_gesture
			_pending_gesture = 0
		_pending_press = ""
		return intent
	if _guard_at >= 0.0 or _hold != "" or _await_draw >= 0 or not _plan.is_empty():
		if _react(foe, true):   # keep perceiving while busy; decide once free
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
		# No walking while drawing: approaching the foe (and so their wave) can make a draw
		# that was estimated to set the lava in time arrive too late.
		_move_tactical(foe, 0.0 if _hold == "tech" and _hold_draw else 0.3)
		return intent
	if not _plan.is_empty():
		_move_tactical(foe, 0.0 if _plan.get("stage", "") == "tech" else 0.3)
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
			if planner and me.sub_of(Sim.Element.FIRE) != 0:
				intent.sub_select = 0
			return intent
	if _react(foe):
		return intent
	if foe == null:
		return intent
	if cfg.drill == "passive":
		debug_state = "passive"
		return intent
	if _opportunities(foe):
		return intent
	if planner:
		if _chain(foe) or _interrupt(foe):
			return intent
		_offense_planner(foe)
	else:
		_offense(foe)
	_move_tactical(foe, 1.0)
	return intent


func _react(foe: ActorState, stamp_only: bool = false) -> bool:
	if planner:
		return _react_planner(foe, stamp_only)
	return _react_to_threats(foe, stamp_only)


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

func _react_to_threats(foe: ActorState, stamp_only: bool = false) -> bool:
	## stamp_only: busy (holding) - start the reaction clock on new threats, decide nothing yet.
	# The foe's pour wind-up is the wave's telegraph (the wave is the same body).
	if foe != null and foe.action != null and foe.action.id == "pour" and foe.held_body >= 0:
		var pk := "pour%d" % foe.action.attack_id
		_perceived(pk)
		_pour_body = foe.held_body
		_pour_seen = _seen[pk]
	# Incoming material attacks.
	for b in w.bodies:
		if not b.alive or b.attack_id == 0 or b.attack_owner == me.id or b.controller >= 0:
			continue
		var key := "b%d:%d" % [b.id, b.attack_id]
		if b.form == Sim.Form.WAVE:
			if b.id == _pour_body:
				_pour_body = -1
				if not _seen.has(key):
					_seen[key] = _pour_seen   # seen coming since the pour began
			var to := me.pos - b.pos
			to.y = 0
			if to.length() > 16.0 or to.normalized().dot(b.wave_dir) < 0.5:
				continue
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
	# Close-range strikes still winding up, within their reach: taps (< 0.2 s) end before any
	# reaction, held blaze / gust / lance charges do not. Lightning charges are handled above.
	if foe != null and foe.action != null:
		var mid: String = foe.action.id
		var charging := foe.action.phase == ActionInst.P.CHARGE
		var winding := foe.action.phase == ActionInst.P.STARTUP or (charging and not (mid == "fire_attack" and foe.has("lightning")))
		var reach := float(foe.action.def.get("heavy_range" if charging else "range", 4.5)) + 0.5
		if winding and mid in ["fire_attack", "air_attack", "water_attack"] and foe.pos.distance_to(me.pos) < reach:
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
	## sim's own rules, standing still from now on: finish the current action (and switch to
	## Fire), wait for draw range, wind up; then the draw (rate falls off with range, bounded
	## by reserve room and Focus) and passive loss cool a scratch copy of the wave while it
	## flows at its fluidity-scaled speed, straight at us (worst case). Lava damage is
	## all-or-nothing: a late draw is no draw.
	var fd: Dictionary = Moves.DEFS.fire_tech
	var c := MatBody.new()
	c.mat = b.mat
	c.mass = b.mass
	c.temp = b.temp
	c.liquid = b.liquid
	var reach := b.wave_width * 0.5 + Sim.ACTOR_RADIUS + 0.1
	# Measured from where I come to rest: I stop walking once committed (no steps while
	# drawing), but momentum still carries me v^2 / 2a further, often toward the wave.
	var hv := Vector3(me.vel.x, 0, me.vel.z)
	var rest := me.pos + hv * (hv.length() / (2.0 * CombatWorld.DECEL))
	var flat := Vector2(rest.x - b.pos.x, rest.z - b.pos.z).length()
	var dy := me.chest().y - b.pos.y
	var budget := b.wave_budget
	var cap := minf(Sim.RESERVE_MAX - me.heat_reserve, me.focus * Sim.DRAW_HU_PER_FOCUS)
	var free := roundi(_busy_time() / Sim.DT) + (0 if me.element == Sim.Element.FIRE else 1)
	var first := -1   # tick of the first draw
	for i in 4 * Sim.HZ:
		if first < 0 and flat <= float(fd.draw_range) - 0.5:
			first = maxi(i, free) + roundi(float(fd.draw_startup) / Sim.DT)
		if first >= 0 and i >= first and cap > 0.0:
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
			if planner:
				# Perfect-timing error of the preset (uniform ±timing_err around the window centre).
				var e := float(prm.get("timing_err", 0.05))
				_guard_at = _t + maxf(0.0, tti - wall_lead - 0.09 + rng.randf_range(-e, e))
				if me.sub_of(Sim.Element.EARTH) != 0:
					intent.sub_select = 0
			else:
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
	_hold_draw = true
	if planner and me.sub_of(Sim.Element.FIRE) != 0:
		intent.element_select = Sim.Element.FIRE
		intent.sub_select = 0
		_pending_press = "tech"
	elif me.element != Sim.Element.FIRE:
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
	_hold_draw = false


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
	if cfg.has("range_lo"):
		lo = float(cfg.range_lo)
		hi = float(cfg.range_hi)
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
	if planner and _needs_water() and dist > 4.0:
		# A water kit with an empty waterskin walks to the pool to refill (wading costs wetness).
		var a := w.arena
		var tgt := Vector3(clampf(me.pos.x, a.pool_min.x + 0.6, a.pool_max.x - 0.6), me.pos.y, clampf(me.pos.z, a.pool_min.y + 0.6, a.pool_max.y - 0.6))
		var to_pool := tgt - me.pos
		to_pool.y = 0.0
		if to_pool.length() > 0.2:
			debug_state = "refill water"
			intent.move = to_pool.normalized() * amount * 0.9
			return
	# Don't wander into the pool unless chasing: steer round it.
	want = _around_pool(want)
	intent.move = want.limit_length(1.0) * amount * (0.55 if dist < hi else 0.9)


func _needs_water() -> bool:
	if not kit.has(Sim.Element.WATER) or me.water_carried >= 1.5 or me.in_water:
		return false
	for e in kit:
		if int(e) != Sim.Element.WATER:
			# Another element to fight with: only a short detour, while fighting as Water
			# and the pool is close (mixed kits used to never refill).
			return me.element == Sim.Element.WATER and _pool_distance() < 5.0
	return true


func _pool_distance() -> float:
	var a := w.arena
	var dx := maxf(maxf(a.pool_min.x - me.pos.x, me.pos.x - a.pool_max.x), 0.0)
	var dz := maxf(maxf(a.pool_min.y - me.pos.z, me.pos.z - a.pool_max.y), 0.0)
	return Vector2(dx, dz).length()


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


# ================================================================== planner mode (docs/AI.md)

## Preset names for the UI ("novice", "adept", "master").
static func preset_names() -> Array:
	return AiPresets.names()


## Configures the brain for the matrix-driven planner (docs/AI.md "Config API"):
## {preset, elements, subs, drill, interval, reaction, counter, misjudge, timing_err, aggression, unlock, kit}.
func configure(opts: Dictionary) -> void:
	planner = true
	var pname := String(opts.get("preset", cfg.get("preset", "adept")))
	prm = AiPresets.get_preset(pname)
	cfg["preset"] = pname
	for k in ["reaction", "counter", "aggression", "misjudge", "timing_err"]:
		cfg[k] = prm[k]
	for k in opts:
		if k in ["preset", "elements", "subs", "kit", "unlock"]:
			continue
		cfg[k] = opts[k]
		if prm.has(k):
			prm[k] = opts[k]
	var unlock := bool(opts.get("unlock", true))
	# Kit breadth: explicit elements, else the preset's count starting from the fighter's element.
	var els: Array = []
	if opts.has("elements"):
		for e in opts.elements:
			if int(e) >= 0 and int(e) < 4 and not els.has(int(e)):
				els.append(int(e))
	else:
		var order: Array = [me.element]
		for e in 4:
			if not order.has(e):
				order.append(e)
		for e in order:
			if els.size() >= int(prm.elements):
				break
			if unlock or me.elements[e]:
				els.append(e)
	var subs_opt: Variant = opts.get("subs", null)
	kit = {}
	for e in els:
		var subs: Array = []
		if subs_opt is Dictionary and (subs_opt as Dictionary).has(e):
			subs = (subs_opt[e] as Array).duplicate()
		elif subs_opt is Dictionary and (subs_opt as Dictionary).has(str(e)):
			subs = (subs_opt[str(e)] as Array).duplicate()
		elif subs_opt is Array and (subs_opt as Array).size() > e and subs_opt[e] is Array:
			subs = (subs_opt[e] as Array).duplicate()
		else:
			for s in int(prm.subs):
				subs.append(s)
		var clean: Array = []
		for s in subs:
			if int(s) >= 0 and int(s) < 4 and not clean.has(int(s)):
				clean.append(int(s))
		kit[e] = clean
	# Drill kits: the drilled element/sub (or every element for the matrix) is part of the kit.
	var drill := String(cfg.get("drill", ""))
	var es := AiPresets.parse_element_drill(drill)
	if es[0] >= 0:
		if not kit.has(es[0]):
			kit[es[0]] = []
		if not (kit[es[0]] as Array).has(es[1]):
			(kit[es[0]] as Array).append(es[1])
	if drill == "matrix":
		_build_matrix()
	if unlock:
		var granted := [false, false, false, false]
		for e in kit:
			granted[e] = true
			for s in kit[e]:
				me.subs_unlocked[e][s] = true
		if drill == "matrix":
			granted = [true, true, true, true]
			for e in 4:
				for s in 4:
					me.subs_unlocked[e][s] = true
		me.elements = granted
		var k: Dictionary = prm.get("kit", {})
		for t in k:
			me.kit[t] = k[t]
		for t in opts.get("kit", {}):
			me.kit[t] = opts.kit[t]
	else:
		for e in kit.keys():
			if not me.elements[e]:
				kit.erase(e)
			else:
				var keep: Array = []
				for s in kit[e]:
					if me.subs_unlocked[e][s]:
						keep.append(s)
				kit[e] = keep
	var el_list: Array = kit.keys()
	el_list.sort()
	cfg["elements"] = el_list
	if not kit.is_empty() and not kit.has(me.element):
		intent.element_select = int(el_list[0])
	_set_ranges()
	_plan = {}
	_drill_i = 0


## Current configuration (UI / Lab / debug overlay).
func describe() -> Dictionary:
	return {"planner": planner, "preset": String(cfg.get("preset", "")), "elements": cfg.get("elements", []),
		"subs": kit.duplicate(true), "drill": String(cfg.get("drill", "")), "reaction": float(cfg.reaction),
		"counter": float(cfg.counter), "aggression": float(cfg.aggression),
		"misjudge": float(cfg.get("misjudge", 0.0)), "timing_err": float(cfg.get("timing_err", 0.0))}


## Preferred spacing from the kit's attack ranges (pokes / finishers), shifted by aggression.
func _set_ranges() -> void:
	var mids: Array = []
	for c in AiPlanner.kit_moves(kit, AiPlanner.OFFENSE_SLOTS):
		var ai: Dictionary = Moves.DEFS[c.id].get("ai", {})
		if String(ai.get("role", "")) in ["poke", "finisher", "zone"]:
			var r: Array = ai.get("range", [2.0, 8.0])
			mids.append(clampf((float(r[0]) + float(r[1])) * 0.5, 3.0, 10.0))
	var pref := 7.5
	if not mids.is_empty():
		mids.sort()
		pref = float(mids[mids.size() / 2])
	var ag := float(cfg.aggression)
	cfg["range_lo"] = clampf(pref - 1.5 - 1.5 * ag, 2.5, 9.0)
	cfg["range_hi"] = clampf(pref + 3.0 - 1.5 * ag, 5.0, 13.0)


func _planner_params() -> Dictionary:
	var p := prm.duplicate()
	for k in ["reaction", "counter", "aggression", "misjudge", "timing_err"]:
		p[k] = float(cfg.get(k, p.get(k, 0.0)))
	p["draw_ok"] = Callable(self, "_draw_sets_in_time")
	p["recent"] = _recent
	return p


# ------------------------------------------------------------------ perception + counter decisions

func _react_planner(foe: ActorState, stamp_only: bool) -> bool:
	# The foe's pour wind-up is the wave's telegraph (the wave is the same body).
	if foe != null and foe.action != null and foe.action.id == "pour" and foe.held_body >= 0:
		var pk := "pour%d" % foe.action.attack_id
		_perceived(pk)
		_pour_body = foe.held_body
		_pour_seen = _seen[pk]
	var best := {}
	for b in w.bodies:
		if not b.alive or b.controller >= 0:
			continue
		if b.attack_id == 0 and b.form != Sim.Form.ZONE and b.form != Sim.Form.CLOUD:
			continue
		var th := AiPlanner.body_threat(w, me, b)
		if th.is_empty():
			continue
		var key := String(th.key)
		if b.form == Sim.Form.WAVE and b.id == _pour_body:
			_pour_body = -1
			if not _seen.has(key):
				_seen[key] = _pour_seen
		if not _perceived(key) or _decided.has(key):
			continue
		if best.is_empty() or float(th.tti) < float(best.tti):
			best = th
	if foe != null and (not Status.hidden(foe) or foe.pos.distance_to(me.pos) < 2.0):
		var vt := AiPlanner.action_threat(w, me, foe)
		if not vt.is_empty() and _perceived(String(vt.key)) and not _decided.has(String(vt.key)):
			if best.is_empty() or float(vt.tti) < float(best.tti):
				best = vt
	if _seen.size() > 64:
		for k in _seen.keys():
			if _t - float(_seen[k]) > 8.0:
				_seen.erase(k)
				_decided.erase(k)
	if best.is_empty():
		return false
	if stamp_only:
		# Busy. An open-ended charge / channel of ours is dropped for an imminent threat (guard and
		# evade cancel charges); otherwise only a waiting (not yet pressed) plan gives way to a more
		# urgent threat.
		var act := me.action
		if (_hold == "attack" or (_hold == "tech" and not _hold_draw)) and _guard_at < 0.0 and float(best.tti) < 0.7 \
				and act != null and (act.phase == ActionInst.P.CHARGE or act.phase == ActionInst.P.CHANNEL) and w.held(me) == null:
			_hold = ""
			_plan = {}
			return _decide(best)
		if _plan.is_empty() or String(_plan.get("stage", "")) != "wait" or _hold != "" or _guard_at >= 0.0:
			return false
		if float(best.tti) >= float(_plan.press_at) - _t:
			return false
		_plan = {}
	return _decide(best)


func _perceived(key: String) -> bool:
	if not _seen.has(key):
		_seen[key] = _t + rng.randf_range(-0.05, 0.08) + (0.2 if planner and Status.has(me, "blinded") else 0.0)
		return false
	return _t - float(_seen[key]) >= float(cfg.reaction)


func _decide(th: Dictionary) -> bool:
	var m := float(cfg.get("misjudge", 0.0))
	var err := rng.randf_range(-m, m)
	var p := _planner_params()
	var opts := AiPlanner.counters(w, me, th, kit, p, err)
	var pick := AiPlanner.choose(opts, p, rng)
	var key := String(th.key)
	if pick.is_empty():
		_decided[key] = "none"
		return false
	_decided[key] = String(pick.label)
	last_plan = pick.duplicate()
	last_plan["threat"] = String(th.cls)
	last_plan["key"] = key
	last_plan["tti"] = float(th.tti)
	last_plan["err"] = err
	last_plan["t"] = _t
	last_plan["options"] = opts.size()
	debug_state = "counter " + String(pick.label)
	_adopt(pick, th)
	return true


## Starts executing an option (counter or offense): switch element/sub, wait for the press time, press.
func _adopt(pick: Dictionary, th: Dictionary = {}) -> void:
	var p := pick.duplicate()
	p["t0"] = _t
	p["stage"] = "wait"
	var b: MatBody = th.get("body")
	p["body"] = b.id if b != null else -1
	p["attack_id"] = int(th.get("attack_id", 0))
	p["tti_abs"] = _t + float(th.get("tti", 0.0))
	p["legacy"] = ""
	if b == null and int(p.get("aim_body", -1)) >= 0:
		p["body"] = int(p.aim_body)   # offense on a body (Smelter on a wall face)
	if p.get("slot", "") == "tech" and String(p.get("mode", "")) == "DRAW" and b != null:
		p.legacy = "draw"
	elif p.get("slot", "") == "guard" and p.id == "guard" and int(p.element) == Sim.Element.EARTH and bool(p.get("perfect", false)) \
			and b != null and b.is_projectile():
		p.legacy = "redirect"
	elif p.get("slot", "") == "evade" and b != null:
		p.legacy = "dodge"
	var press_in := float(p.get("press_in", 0.0))
	if bool(p.get("perfect", false)) and p.legacy == "":
		var e := float(cfg.get("timing_err", 0.05))
		press_in = maxf(press_in, float(th.get("tti", 0.0)) - AiPlanner.PERFECT_LEAD + rng.randf_range(-e, e))
	if p.legacy == "redirect" or p.legacy == "draw":
		press_in = 0.0
	p["press_at"] = _t + press_in
	if p.has("guard_for"):
		p["guard_until"] = _t + float(p.guard_for)
	_plan = p
	_exec_plan(w.get_actor(me.lock_target))


func _end_plan() -> void:
	if _plan.get("stage", "") == "tech":
		intent.tech_held = false
		intent.tech_released = true
	_plan = {}


## Drives the plan stages: wait (switch element/sub, wait for the press time) -> press ->
## guard_gesture (push / sink) | tech (grip, aim, release) | evade_hold; attacks and guards hand over to
## the legacy holds (_hold) once pressed.
func _exec_plan(foe: ActorState) -> void:
	var p := _plan
	if _t > float(p.t0) + 5.0:
		_end_plan()
		return
	match String(p.stage):
		"wait":
			if int(p.element) != me.element or me.sub_of(int(p.element)) != int(p.sub):
				intent.element_select = int(p.element)
				intent.sub_select = int(p.sub)
				p.press_at = maxf(float(p.press_at), _t + Sim.DT)
				return
			if _t + 1e-6 < float(p.press_at):
				return
			if String(p.legacy) != "":
				var b := w.get_body(int(p.body))
				_plan = {}
				if b == null or not b.alive:
					return
				_act_on(String(p.legacy), b, maxf(0.0, float(p.tti_abs) - _t))
				return
			_press_plan(p, foe)
		"guard_gesture":
			intent.guard_held = true
			var a := me.action
			if _t >= float(p.gesture_at) and a != null and a.id == "guard" and a.phase == ActionInst.P.CHANNEL:
				intent.guard_gesture = Sim.Gesture.UP if p.slot == "push" else Sim.Gesture.DOWN
				p.stage = "after_gesture"
				p["gest_t"] = _t
			elif _t > float(p.gesture_at) + 0.5 or (a == null and _t > float(p.press_t) + 0.2):
				_plan = {}
		"after_gesture":
			# Keep the guard button down one more tick, then let the push / sink run.
			if _t - float(p.gest_t) < 0.05:
				intent.guard_held = true
			else:
				_plan = {}
		"tech":
			_tech_stage(p, foe)
		"evade_hold":
			intent.evade_held = true
			intent.move = p.get("move", Vector3.ZERO)
			if _t >= float(p.until):
				_plan = {}


func _press_plan(p: Dictionary, foe: ActorState) -> void:
	p["press_t"] = _t
	match String(p.press):
		"attack":
			_press("attack")
			intent.attack_gesture = int(p.get("gesture", 0))
			_hold = "attack"
			_hold_until = _t + maxf(float(p.get("hold", 0.0)), 0.05)
			_aim_hold = p.get("aim", Vector3.ZERO)
			_plan = {}
		"guard":
			_press("guard")
			if p.slot == "guard":
				_hold = "guard"
				_hold_until = float(p.get("guard_until", _t + 0.6))
				_guard_attack = int(p.get("attack_id", 0))
				_plan = {}
			else:
				p.stage = "guard_gesture"
				p["gesture_at"] = _t + maxf(float(p.get("hold", 0.0)), 2.0 * Sim.DT)
		"tech":
			_press("tech")
			_aim_plan(p, foe)
			p.stage = "tech"
			p["started"] = false
			p["release_at"] = _t + maxf(float(p.get("hold", 0.0)), float(Moves.DEFS[p.id].get("startup", 0.1)) + 0.05)
		"evade":
			var side := me.forward().cross(Vector3.UP)
			if rng.randf() < 0.5:
				side = -side
			intent.move = side if p.slot != "evade_hold" else Vector3.ZERO   # stances are taken standing
			_press("evade")
			if p.slot == "evade_hold":
				intent.evade_held = true
				p.stage = "evade_hold"
				p["move"] = Vector3.ZERO
				p["until"] = _t + 0.25 + maxf(float(p.get("hold", 0.0)), 0.6)
			else:
				_plan = {}
		_:
			_plan = {}


func _aim_plan(p: Dictionary, foe: ActorState) -> void:
	var b := w.get_body(int(p.get("body", -1)))
	var target := Vector3.ZERO
	if b != null and b.alive and w.held(me) == null:
		target = b.pos
	elif foe != null:
		target = foe.pos
	else:
		return
	var d := target - me.pos
	d.y = 0.0
	if d.length() > 0.1:
		intent.aim_dir = d.normalized()
		intent.aim_active = true


func _tech_stage(p: Dictionary, foe: ActorState) -> void:
	intent.tech_held = true
	_aim_plan(p, foe)
	var act := me.action
	if not bool(p.started):
		if act != null and act.slot == "tech" and act.id == String(p.id):
			p.started = true
		elif _t - float(p.press_t) < 0.6:
			intent.tech_pressed = true   # the world buffers a press only briefly: keep pressing
			return
		else:
			_end_plan()
			return
	if act == null or act.slot != "tech":
		_plan = {}
		return
	var held := w.held(me)
	var grip := String(act.def.get("verb", "")) == "grip" or act.id == "earth_tech"
	if held != null and not p.has("caught_at"):
		p["caught_at"] = _t
	var release := false
	if grip:
		if p.has("caught_at"):
			release = _t >= float(p.caught_at) + maxf(0.2, float(p.get("hold", 0.0)) * 0.5)
		else:
			release = _t > float(p.press_t) + 1.6
	else:
		release = _t >= float(p.release_at)
	if release or _t > float(p.press_t) + 3.0:
		if foe != null:
			var d := foe.pos - me.pos
			d.y = 0.0
			if d.length() > 0.1:
				intent.aim_dir = d.normalized()
				intent.aim_active = true
		intent.tech_held = false
		intent.tech_released = true
		_plan = {}


# ------------------------------------------------------------------ offense (planner)

func _offense_planner(foe: ActorState) -> void:
	var drill := String(cfg.drill)
	if _t < _next_attack or me.action != null:
		return
	if LEGACY_DRILLS.has(drill):
		_offense(foe)
		return
	var interval := float(cfg.interval) if drill != "" else lerpf(3.2, 1.1, float(cfg.aggression))
	if drill == "matrix":
		_next_attack = _t + interval * rng.randf_range(0.9, 1.1)
		_drill_matrix(foe)
		return
	var es := AiPresets.parse_element_drill(drill)
	if es[0] >= 0:
		_next_attack = _t + interval * rng.randf_range(0.8, 1.25)
		_drill_element(foe, es[0], es[1])
		return
	if Status.hidden(foe) and me.pos.distance_to(foe.pos) > 2.0:
		_next_attack = _t + 0.3   # can't see it: no aimed attacks into the fog
		return
	var opts := AiPlanner.offense(w, me, foe, kit, _planner_params(), rng)
	if opts.is_empty():
		_next_attack = _t + 0.4
		return
	var pick: Dictionary = opts[0]
	var st := AiPlanner.observe(w, me, foe)
	if bool(st.cover) and not (pick.reasons as Array).has("barrier"):
		# Behind cover: don't throw into it (_move_tactical steps out).
		_next_attack = _t + 0.3
		return
	_next_attack = _t + interval * rng.randf_range(0.8, 1.25)
	debug_state = "offense " + String(pick.label)
	for k in _recent.keys():
		_recent[k] = float(_recent[k]) * 0.7
		if float(_recent[k]) < 0.05:
			_recent.erase(k)
	_recent[pick.id] = float(_recent.get(pick.id, 0.0)) + 1.0
	_adopt(pick)


## Chain (same sub-element) or weave (Master) after an attack made contact, inside the chain window.
func _chain(foe: ActorState) -> bool:
	var a := me.action
	if foe == null or a == null or a.phase != ActionInst.P.RECOVERY or not a.data.get("contact", false):
		return false
	if not Sim.ATTACK_SLOTS.has(a.slot) or not a.def.has("chain"):
		return false
	if int(me.chain.get("n", 0)) >= mini(int(prm.get("chain", 1)), CombatWorld.CHAIN_MAX):
		return false
	var rec := float(a.def.recovery) * Status.recovery_mult(me)
	if rec > 0.0 and a.t / rec < float(a.def.chain):
		return false
	var key := "ch%d" % a.attack_id
	if _decided.has(key):
		return false
	var pick := AiPlanner.chain_follow(w, me, foe, kit, _planner_params(), rng)
	_decided[key] = String(pick.get("label", "none"))
	if pick.is_empty():
		return false
	debug_state = String(pick.label)
	if int(pick.element) != me.element or me.sub_of(int(pick.element)) != int(pick.sub):
		intent.element_select = int(pick.element)
		intent.sub_select = int(pick.sub)
		_pending_press = "attack"
		_pending_gesture = int(pick.gesture)
		_hold = "attack"
		_hold_until = _t + Sim.DT + 0.05
		return true
	_press("attack")
	intent.attack_gesture = int(pick.gesture)
	_hold = "attack"
	_hold_until = _t + 0.05
	return true


## Punish a visible charge (Master / punish presets): Disrupt or the fastest hit that reaches, once per charge.
func _interrupt(foe: ActorState) -> bool:
	if not bool(prm.get("punish", false)) or foe == null or foe.action == null or me.action != null:
		return false
	var fa := foe.action
	if fa.phase != ActionInst.P.CHARGE and not (fa.phase == ActionInst.P.CHANNEL and fa.id != "guard"):
		return false
	var key := "int%d" % fa.attack_id
	if not _perceived(key) or _decided.has(key):
		return false
	_decided[key] = "skip"
	if rng.randf() >= float(cfg.counter):
		return false
	for o in AiPlanner.offense(w, me, foe, kit, _planner_params(), rng):
		var rs: Array = o.reasons
		if rs.has("disrupt") or rs.has("interrupt"):
			# Disrupt needs P >= 6 + 4 x the charge's tier: the lowest tier that clears it (one tier of
			# growth allowed for while we hold), else a quick T0 hit (a hit staggers a charge too).
			var def: Dictionary = Moves.DEFS[o.id]
			var dist := me.pos.distance_to(foe.pos)
			if rs.has("disrupt"):
				for t in range(int(o.tier), Charge.max_tier(def) + 1):
					var grow := 1 if AiPlanner.hold_for(def, t) > 0.3 else 0
					if Charge.counter_power(def, t) >= Interactions.disrupt_threshold(fa.tier() + grow) \
							and AiPlanner.reach_of(def, t) + 0.3 >= dist:
						o.tier = t
						break
			o.hold = AiPlanner.hold_for(def, o.tier) if o.press == "attack" else o.hold
			_decided[key] = String(o.label)
			debug_state = "interrupt " + String(o.label)
			_adopt(o)
			return true
	return false


# ------------------------------------------------------------------ drills

func _drill_moves(e: int, s: int) -> Array:
	var out: Array = []
	for c in AiPlanner.kit_moves({e: [s]}, AiPlanner.OFFENSE_SLOTS):
		var def: Dictionary = Moves.DEFS[c.id]
		var role := String(def.get("ai", {}).get("role", ""))
		if c.slot == "tech" and (c.id in ["air_tech", "water_tech", "fire_tech", "earth_tech"] or role == "mobility" or role == "counter"):
			continue
		if def.has("threat") or role in ["poke", "zone", "finisher", "setup"]:
			out.append(c)
	return out


func _drill_element(foe: ActorState, e: int, s: int) -> void:
	var moves := _drill_moves(e, s)
	if moves.is_empty():
		return
	var c: Dictionary = moves[rng.randi_range(0, moves.size() - 1)]
	_fire_drill_move(c, foe)


func _fire_drill_move(c: Dictionary, foe: ActorState) -> void:
	var def: Dictionary = Moves.DEFS[c.id]
	var tier := rng.randi_range(0, mini(2, Charge.max_tier(def))) if rng.randf() < 0.5 else 0
	var g := 0
	match String(c.slot):
		"thrust":
			g = Sim.Gesture.UP
		"ground":
			g = Sim.Gesture.DOWN
		"sweep":
			g = Sim.Gesture.SIDE
	var hold := AiPlanner.hold_for(def, tier)
	if c.slot == "tech" and hold <= 0.0:
		hold = maxf(0.35, float(def.get("startup", 0.2)) + 0.1)
	var o := {"id": c.id, "element": int(c.element), "sub": int(c.sub), "slot": String(c.slot),
		"press": "tech" if c.slot == "tech" else "attack", "gesture": g, "tier": tier, "hold": hold,
		"label": "drill %s T%d" % [c.id, tier]}
	debug_state = String(o.label)
	_adopt(o)


func _build_matrix() -> void:
	var by := {}
	for e in 4:
		for s in 4:
			for c in _drill_moves(e, s):
				var def: Dictionary = Moves.DEFS[c.id]
				var cls := String(def.get("threat", {}).get("cls", ""))
				if cls == "":
					continue
				if not by.has(cls):
					by[cls] = []
				var dup := false
				for x in by[cls]:
					if x.id == c.id:
						dup = true
				if not dup:
					by[cls].append(c)
	var keys: Array = by.keys()
	keys.sort()
	_matrix = []
	for k in keys:
		_matrix.append([k, by[k]])


## The threat class the matrix drill launches next (Lab HUD).
func matrix_next() -> String:
	if _matrix.is_empty():
		return ""
	return String(_matrix[_drill_i % _matrix.size()][0])


func _drill_matrix(foe: ActorState) -> void:
	if _matrix.is_empty():
		_build_matrix()
	if _matrix.is_empty():
		return
	var row: Array = _matrix[_drill_i % _matrix.size()]
	_drill_i += 1
	var moves: Array = row[1]
	var c: Dictionary = moves[rng.randi_range(0, moves.size() - 1)]
	_fire_drill_move(c, foe)
	debug_state = "matrix %s: %s" % [row[0], c.id]
