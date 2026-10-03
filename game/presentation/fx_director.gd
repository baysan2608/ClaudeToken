class_name FxDirector
extends Node
## Turns simulation events into effects, sound, haptics and camera feedback, and
## keeps state-driven loops (heating crackle, flowing lava, heat draw, charges)
## running only while their state holds. Feedback is distinct per outcome:
## block (thud, light haptic), deflect (bright ring), perfect (ring + shimmer +
## strong haptic), lost control (strained descending tone), transformation (swell).

var world: CombatWorld
var views: BodyViews
var fighters := {}           # actor id -> FighterView
var audio: AudioDirector
var cam: CameraRig
var player_id := 1
var settings: GameSettings
var hud: Node = null         # receives toast(text) / flash(kind)

var _charge_fx := {}         # actor id -> Node (FireChargeFX / ChargeAimFX)
var _transient: Array[Dictionary] = []   # short-lived driven views (water lash arcs)
var _loop_bodies := {}       # stone body id -> last position (its heat/lava/draw loops may be on)
var _step_t := {}
var _slowmo := 0.0


func bind(w: CombatWorld) -> void:
	world = w
	if views and views.pool:
		for n in _charge_fx.values():
			if is_instance_valid(n):
				views.pool.release(n)
		for tr in _transient:
			if is_instance_valid(tr.node):
				views.pool.release(tr.node)
	_charge_fx.clear()
	_transient.clear()
	_loop_bodies.clear()
	if audio:
		audio.stop_all_loops()


func _fx(key: String) -> Node:
	return views.pool.get_fx(key) if views and views.pool else null


## A node kept across frames (released by this director): never recycled out from under its owner.
func _hold(key: String) -> Node:
	return views.acquire(key) if views and views.pool else null


func _hand(actor_id: int) -> Vector3:
	var fv: FighterView = fighters.get(actor_id, null)
	if fv:
		return fv.hand_position(true)
	var a := world.get_actor(actor_id)
	return a.hand_point() if a else Vector3.ZERO


func _apos(id: int) -> Vector3:
	var a := world.get_actor(id)
	return a.chest() if a else Vector3.ZERO


func _bpos(id: int) -> Vector3:
	var b := world.get_body(id)
	return b.pos if b else Vector3.ZERO


func _haptic(kind: String, actor_id: int) -> void:
	if actor_id == player_id:
		Haptics.play(kind)


func _shake(amount: float) -> void:
	if cam:
		cam.shake(amount)


func _toast(text: String) -> void:
	if hud and hud.has_method("toast"):
		hud.call("toast", text)


func handle(events: Array[Dictionary]) -> void:
	for e in events:
		_event(e)


func _event(e: Dictionary) -> void:
	var a: int = e.get("actor", -1)
	match String(e.type):
		"hit":
			var big: bool = e.get("result", "") == "knockdown" or float(e.get("damage", 0.0)) >= 15.0
			audio.play("hit_heavy" if big else "hit_light", _apos(a))
			if e.get("kind", "") == "lava":
				audio.play("lava_splat", _apos(a))
				_call(_fx("ember"), "play", [_apos(a), Vector3.UP, 1.0])
			_call(_fx("dust_puff"), "play", [world.get_actor(a).pos if world.get_actor(a) else Vector3.ZERO, Vector3.UP, 0.6])
			_haptic("heavy" if big else "light", a)
			_haptic("light", e.get("attacker", -1))
			_shake(0.6 if big else 0.25)
			if e.get("result", "") == "knockdown":
				audio.play("knockdown", _apos(a))
		"block":
			audio.play("block", _apos(a) if a >= 0 else _bpos(e.get("body", -1)))
			_haptic("block", a)
			_shake(0.15)
			if e.get("kind", "") == "fire_water":
				audio.play("steam_hiss", _apos(a))
		"guard_break":
			audio.play("guard_break", _apos(a))
		"deflect":
			audio.play("deflect", _apos(a))
			_haptic("deflect", a)
		"perfect_deflect":
			audio.play("perfect_deflect", _apos(a))
			_haptic("perfect", a)
			_shake(0.2)
			if hud and a == player_id:
				hud.call("flash", "perfect")
			if a == player_id and settings and settings.slowmo_assist:
				_slowmo = 0.22
			var fv: FighterView = fighters.get(a, null)
			if fv and world.get_actor(a) and world.get_actor(a).action != null and world.get_actor(a).action.id == "guard" and world.get_actor(a).wall_body < 0:
				fv.play_one_shot("deflect", 0.3)
		"evaded":
			if a == player_id and hud:
				hud.call("flash", "evade")
		"intercept":
			audio.play("block", _bpos(e.body), -4.0)
			_haptic("light", a)
		"control_won":
			if e.get("verb", "") in ["seize", "magma_grip", "acquire"]:
				audio.play("stone_rip", _bpos(e.body), -6.0)
		"control_fail":
			if e.get("reason", "") == "mass":
				audio.play("control_lost", _apos(a))
				_haptic("lost_control", a)
				if a == player_id:
					_toast("Too heavy to control")
		"control_lost":
			audio.play("control_lost", _bpos(e.body))
			_haptic("lost_control", a)
			if a == player_id:
				_toast("Lost control (%s)" % e.get("reason", ""))
		"insufficient":
			if a == player_id:
				audio.ui("challenge_fail", -8.0)
				var what := String(e.get("what", ""))
				var msg := {"focus": "Not enough Focus", "water": "No water nearby", "target": "Nothing to work",
					"technique": "Technique not learned", "sight": "No line of sight", "mass": "Too heavy"}
				_toast(msg.get(what, what))
		"rip":
			audio.play("stone_rip", _bpos(e.body))
			_call(_fx("dust_puff"), "play", [_bpos(e.body), Vector3.UP, 0.8])
		"telegraph":
			pass
		"launch":
			var kind := String(e.get("kind", ""))
			if kind == "stream":
				audio.play("water_whip", _bpos(e.body))
			elif kind == "ice":
				audio.play("freeze", _bpos(e.body))
			else:
				audio.play("stone_launch", _bpos(e.body))
				audio.play("whoosh_heavy" if e.get("heavy", false) else "whoosh_light", _bpos(e.body), -3.0)
		"impact":
			if float(e.get("speed", 0.0)) > 3.0:
				audio.play("stone_impact_%d" % (1 + randi() % 2), _bpos(e.body))
				_call(_fx("dust_puff"), "play", [_bpos(e.body), Vector3.UP, clampf(float(e.mass) / 30.0, 0.4, 1.2)])
				_shake(clampf(float(e.mass) / 120.0, 0.0, 0.3))
		"wall":
			audio.play("wall_raise", _bpos(e.body))
			_call(_fx("dust_puff"), "play", [_bpos(e.body), Vector3.UP, 1.0])
			_haptic("light", a)
		"wall_crumble":
			audio.play("wall_crumble", _bpos(e.body))
			_call(_fx("dust_puff"), "play", [_bpos(e.body), Vector3.UP, 1.4])
		"transform":
			var to := String(e.get("to", ""))
			var p := _bpos(e.body)
			match to:
				"molten":
					audio.play("melt_rise", p)
					_haptic("transform", world.get_body(e.body).controller if world.get_body(e.body) else -1)
				"wave":
					audio.play("lava_splat", p)
					_call(_fx("ember"), "play", [p, Vector3.UP, 0.8])
				"rock":
					audio.play("crust_hiss", p)
					audio.play("cool_crack", p, -3.0)
					_call(_fx("steam"), "play", [p + Vector3(0, 0.2, 0), 0.5])
				"ice":
					audio.play("freeze", p)
				"water":
					audio.play("ice_melt_drip", p)
				"puddle":
					audio.play("water_splash", p, -4.0)
					_call(_fx("splash"), "play", [p, Vector3.UP, 0.7])
		"shatter":
			audio.play("ice_shatter", _bpos(e.body))
			_call(_fx("splash"), "play", [_bpos(e.body), Vector3.UP, 0.5])
		"phase":
			pass
		"wave_blocked", "wave_drop":
			audio.play("lava_splat", _bpos(e.body))
			_call(_fx("ember"), "play", [_bpos(e.body), Vector3.UP, 0.7])
		"steam", "steam_block":
			audio.play("steam_hiss", _bpos(e.get("body", -1)), -3.0)
			_call(_fx("steam"), "play", [_bpos(e.get("body", -1)) + Vector3(0, 0.3, 0), 0.8])
		"flare":
			var dir: Vector3 = e.dir
			var o := _hand(a)
			_call(_fx("fire_burst"), "play", [o, dir, float(e.range), 1.0 if e.get("heavy", false) else 0.6])
			audio.play("fire_release" if e.get("heavy", false) else "fire_jab", o)
		"vent":
			var o2 := _apos(a)
			_call(_fx("fire_burst"), "play", [o2, Vector3.UP, 2.5, 0.8])
			audio.play("vent_heat", o2)
		"lightning":
			var pts: PackedVector3Array = e.path
			_call(_fx("lightning_arc"), "strike", [pts, world.tick])
			for arc in e.get("arcs", []):
				_call(_fx("lightning_arc"), "strike", [arc, world.tick + 7])
			audio.play("lightning_strike", pts[pts.size() - 1])
			_shake(0.35)
			if hud:
				hud.call("flash", "lightning")
		"lightning_redirect":
			audio.play("lightning_redirect", _apos(a))
			_haptic("perfect", a)
		"conduct":
			audio.play("conduct_buzz", _apos(a))
		"grounded":
			_call(_fx("dust_puff"), "play", [world.get_actor(a).pos, Vector3.UP, 0.6])
		"gust":
			_call(_fx("air_push"), "play", [_apos(a), e.dir, 1.2, float(e.range)])
			audio.play("air_gust" if e.get("heavy", false) else "air_push", _apos(a))
		"disperse":
			pass
		"lash":
			audio.play("water_whip", _apos(a))
			var rib := _hold("water_ribbon")
			if rib:
				_transient.append({"node": rib, "t": 0.0, "dur": 0.28, "actor": a, "dir": e.dir, "range": float(e.range)})
			_call(_fx("splash"), "play", [_apos(a) + (e.dir as Vector3) * 2.5, Vector3.UP, 0.4])
		"shield":
			audio.play("water_splash", _apos(a), -6.0)
		"draw_water":
			pass
		"evade":
			audio.play("dash_air" if e.get("dash", false) else "evade_whoosh", _apos(a))
		"updraft":
			audio.play("air_gust", _apos(a), -4.0)
			_call(_fx("dust_puff"), "play", [world.get_actor(a).pos, Vector3.UP, 0.9])
		"land":
			if float(e.get("speed", 0.0)) > 3.0:
				audio.play("land", world.get_actor(a).pos)
				_call(_fx("dust_puff"), "play", [world.get_actor(a).pos, Vector3.UP, 0.5])
		"element":
			if a == player_id:
				audio.ui("element_" + ["earth", "water", "fire", "air"][int(e.element)])
		"reform":
			_call(_fx("dust_puff"), "play", [_bpos(e.body), Vector3.UP, 0.6])
		"stagger":
			pass
		"burn":
			audio.play("fire_ignite", _apos(a), -6.0)
		"conversion_interrupted":
			audio.play("control_lost", _bpos(e.body), -4.0)
		"reserve_full":
			if a == player_id:
				_toast("Heat reserve full: vent it")


func _call(n: Node, m: String, args: Array) -> void:
	if n != null and n.has_method(m):
		n.callv(m, args)


func _update_transient(dt: float) -> void:
	for k in range(_transient.size() - 1, -1, -1):
		var tr: Dictionary = _transient[k]
		tr.t = float(tr.t) + dt
		var n: Node = tr.node
		if float(tr.t) >= float(tr.dur) or not is_instance_valid(n):
			if is_instance_valid(n):
				views.pool.release(n)
			_transient.remove_at(k)
			continue
		# Water lash: an arc sweeping from the off side across the front.
		var x := float(tr.t) / float(tr.dur)
		var o := _hand(tr.actor)
		var d: Vector3 = tr.dir
		var side := d.cross(Vector3.UP)
		var pts := PackedVector3Array()
		var rad := PackedFloat32Array()
		var reach := float(tr.range) * (0.4 + 0.6 * sin(x * PI))
		for i in 8:
			var u := float(i) / 7.0
			var ang := lerpf(-1.1, 1.1, clampf(x * 1.6 - (1.0 - u) * 0.6, 0.0, 1.0))
			var p := o + (d * cos(ang) + side * sin(ang)) * reach * u + Vector3(0, -0.4 * u, 0)
			pts.append(p)
			rad.append(lerpf(0.05, 0.11, u) * (1.0 - x * 0.5))
		n.call("set_points", pts, rad)
		n.call("set_state", 0.0)


func update_continuous(dt: float) -> void:
	## Per frame: loops and held effects driven by current state.
	_update_transient(dt)
	if _slowmo > 0.0:
		_slowmo -= dt / maxf(Engine.time_scale, 0.1)
		Engine.time_scale = 0.55 if _slowmo > 0.0 else 1.0
	var live := {}
	for b in world.bodies:
		if not b.alive or not b.is_stone():
			continue
		live[b.id] = b.pos
		var key := "b%d" % b.id
		var heating := b.controller >= 0 and b.last_verb == "heat" and world.tick - b.last_tick < 4
		audio.loop(key + "heat", "heat_crackle_loop", heating, b.pos)
		audio.loop(key + "lava", "lava_wave_loop" if b.form == Sim.Form.WAVE else "lava_bubble_loop",
			(b.form == Sim.Form.WAVE and b.vel.length() > 0.3) or (b.form == Sim.Form.BLOB and b.liquid > 0.3), b.pos)
		var drawn := b.last_verb == "draw" and world.tick - b.last_tick < 4
		audio.loop(key + "draw", "heat_draw_loop", drawn, b.pos)
	# A body that died (decay, remnant trim, merge) is never updated again: fade its loops out
	# where it was, or a looping stream would play on forever.
	for id in _loop_bodies:
		if not live.has(id):
			for s in ["heat", "lava", "draw"]:
				audio.loop("b%d%s" % [id, s], "", false, _loop_bodies[id])
	_loop_bodies = live
	for a in world.actors:
		var key := "a%d" % a.id
		var inst := a.action
		var charging := inst != null and inst.phase == ActionInst.P.CHARGE and inst.id == "fire_attack"
		var bolt: bool = charging and inst.data.get("bolt_ready", false)
		audio.loop(key + "charge", "lightning_charge_loop" if bolt else "fire_charge_loop", charging, a.chest(), -4.0)
		audio.loop(key + "glide", "glide_loop", a.gliding, a.chest())
		var drawing_water := inst != null and inst.id == "water_tech" and inst.phase == ActionInst.P.CHANNEL
		audio.loop(key + "water", "water_draw_loop", drawing_water, a.chest(), -6.0)
		_charge_visual(a, charging, bolt)
		# Footsteps from ground speed (cadence follows speed).
		var spd := Vector2(a.vel.x, a.vel.z).length()
		if a.grounded and spd > 0.8 and a.stun <= 0.0:
			_step_t[a.id] = float(_step_t.get(a.id, 0.0)) + dt * (1.2 + spd * 0.45)
			if _step_t[a.id] >= 1.0:
				_step_t[a.id] = 0.0
				audio.step(a.pos)


func _charge_visual(a: ActorState, charging: bool, bolt: bool) -> void:
	var n: Node = _charge_fx.get(a.id, null)
	if charging:
		# The flame shows the fire charge; once the bolt is ready the telegraph becomes the aim line.
		if n != null and bolt != n.has_method("set_aim"):
			views.pool.release(n)
			n = null
		if n == null:
			n = _hold("charge_aim" if bolt else "fire_charge")
			_charge_fx[a.id] = n
		if n != null:
			var t := clampf(a.action.total / 0.65, 0.0, 1.0)
			if n.has_method("set_aim"):
				var tgt := world.get_actor(a.lock_target)
				n.call("set_aim", _hand(a.id), tgt.chest() if tgt else a.chest() + a.forward() * 10.0, t)
			elif n.has_method("set_charge"):
				(n as Node3D).global_position = _hand(a.id)
				n.call("set_charge", t)
	elif n != null:
		views.pool.release(n)
		_charge_fx.erase(a.id)
