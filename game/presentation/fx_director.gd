class_name FxDirector
extends Node
## Turns simulation events into effects, sound, haptics and camera feedback, and
## keeps state-driven loops (heating crackle, flowing lava, heat draw, charges)
## running only while their state holds. Feedback is distinct per outcome:
## block (thud, light haptic), deflect (bright ring), perfect (ring + shimmer +
## strong haptic), lost control (strained descending tone), transformation (swell).
##
## Moveset events (fx, interaction, charge, status, zone, clash, morph ...) go to FxCues; moveset
## actions FighterView does not animate go through MoveAnimBridge. Game feel (docs/MOVESET.md §10.2):
## hit-stop is a short global Engine.time_scale dip (HITSTOP_SCALE for N real frames, capped at
## HITSTOP_CAP frames per rolling second, merged with the slow-mo assist); camera shake / kick / FOV
## punch / transformation zoom run in real time in CameraRig; reduced_motion, flashes and
## screen_shake are respected (settings).

var world: CombatWorld
var views: BodyViews
var fighters := {}           # actor id -> FighterView
var audio: AudioDirector
var cam: CameraRig
var player_id := 1
var settings: GameSettings
var hud: Node = null         # receives toast(text) / flash(kind)

## Seconds a water-draw stream outlives the latest "draw_water" report (reported every 8 ticks).
const DRAW_HOLD := 0.2

var _charge_fx := {}         # actor id -> Node (FireChargeFX / ChargeAimFX)
## Short-lived driven views: water lash arcs, water-draw streams (kind "draw"), dash trails ("dash").
var _transient: Array[Dictionary] = []
var _loop_bodies := {}       # stone body id -> last position (its heat/lava/draw loops may be on)
var _step_t := {}
var _slowmo := 0.0

## Hit-stop: global time scale while frozen, frames-per-second cap, and a master switch (tests).
const HITSTOP_SCALE := 0.05
const HITSTOP_CAP := 12
var hitstop_enabled := true
var _hs_frames := 0
var _hs_hist: Array[int] = []        # Time.get_ticks_msec() of each frozen frame (rolling 1 s)
var _scale_applied := 1.0
## Moveset cue handling and the animation bridge.
var cues: FxCues
## Test / debug hook: an Array that records every moveset cue sound name (null = off).
var sfx_log: Variant = null
var bridge := MoveAnimBridge.new()
## MOVESET §10.2 table: kind -> [hit-stop frames, shake, kick m, fov punch deg, haptic]
const FEEL := {
	"t0": [3, 0.25, 0.0, 0.0, "light"], "t1": [5, 0.45, 0.1, 0.0, "light"], "t2": [7, 0.7, 0.1, 0.0, "heavy"],
	"t3": [9, 1.0, 0.12, -3.0, "heavy"], "block": [2, 0.2, 0.0, 0.0, "block"], "block_heavy": [4, 0.38, 0.05, 0.0, "block"],
	"perfect": [6, 0.5, 0.0, 0.0, "perfect"], "clash": [4, 0.38, 0.0, 0.0, "clash"], "shatter": [3, 0.3, 0.0, 0.0, "shatter"],
	"transform": [0, 0.0, 0.0, 0.0, "transform"], "boom": [5, 0.9, 0.0, 0.0, "boom"],
}


func _init() -> void:
	cues = FxCues.new(self)


func bind(w: CombatWorld) -> void:
	world = w
	if cues:
		cues.clear()
	bridge.clear()
	_hs_frames = 0
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


## A charged throw announces its tier on release (MOVESET §11.2): T2 a ground shockwave ring at the thrower's
## feet, T3 a double white-hot ring, a dust burst and a small shake.
func _launch_tier(a: int, tier: int, at: Vector3) -> void:
	if tier < 2:
		return
	var ac := world.get_actor(a)
	var feet := ac.pos if ac else at
	var col := VfxPalette.tier_color(tier)
	_call(_fx("ring"), "play", [feet + Vector3(0, 0.05, 0), Vector3.UP, 0.4, 1.6 + 0.9 * float(tier - 2), 0.35 + 0.1 * float(tier - 2), col,
		{"count": 2 if tier >= 3 else 1, "width": 0.09, "cover": 0.5, "glow": 1.6 + 0.6 * float(tier - 2)}])
	if tier >= 3:
		_call(_fx("dust_puff"), "play", [feet, Vector3.UP, 1.3])
		_shake(0.18)


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


## One game-feel beat from the §10.2 table: hit-stop frames, camera shake (distance falloff, real
## time), a kick along `dir`, the T3 FOV punch, and the kind's haptic for `haptic_actor`.
func feel(kind: String, pos: Vector3, dir: Variant = Vector3.ZERO, haptic_actor: int = -1, scale: float = 1.0) -> void:
	var f: Array = FEEL.get(kind, FEEL.t0)
	hitstop(int(f[0]))
	if cam:
		if float(f[1]) > 0.0:
			if cam.has_method("shake_at"):
				cam.shake_at(float(f[1]) * clampf(scale, 0.5, 2.0), pos, 0.2 + 0.05 * float(f[0]) / 3.0)
			else:
				cam.shake(float(f[1]))
		var dv: Vector3 = dir if dir is Vector3 else Vector3.ZERO
		if float(f[2]) > 0.0 and dv.length_squared() > 1e-6 and cam.has_method("kick"):
			cam.kick(dv, float(f[2]))
		if float(f[3]) != 0.0 and cam.has_method("fov_punch"):
			cam.fov_punch(float(f[3]), 0.25)
	if haptic_actor >= 0:
		_haptic(String(f[4]), haptic_actor)
	if kind == "transform":
		_zoom(pos)


## Transformation moments: a 3 % zoom toward the event for 0.3 s (no hit-stop).
func _zoom(pos: Vector3) -> void:
	if cam and cam.has_method("zoom_to"):
		cam.zoom_to(pos, 0.03, 0.3)


## Request a hit-stop of `frames` rendered frames (the longest pending request wins; never more than
## HITSTOP_CAP frozen frames in any second; reduced motion caps one request at 3 frames).
func hitstop(frames: int) -> void:
	if not hitstop_enabled or frames <= 0:
		return
	if settings and settings.reduced_motion:
		frames = mini(frames, 3)
	var now := Time.get_ticks_msec()
	while not _hs_hist.is_empty() and now - _hs_hist[0] >= 1000:
		_hs_hist.pop_front()
	var budget := HITSTOP_CAP - _hs_hist.size()
	_hs_frames = clampi(maxi(_hs_frames, frames), 0, maxi(budget, 0))


## The time scale this frame wants (hit-stop and the slow-mo assist combined).
func time_scale_wanted() -> float:
	var sc := 1.0
	if _hs_frames > 0:
		sc = HITSTOP_SCALE
	if _slowmo > 0.0:
		sc = minf(sc, 0.55)
	return sc


func hitstop_pending() -> int:
	return _hs_frames


## Material-aware impact on a hit (debris in the threat's material, sparks for metal / energy).
func _impact_cue(e: Dictionary, a: int) -> void:
	var mat := String(e.get("mat", ""))
	if mat == "" or mat == "stone":
		return
	var b := _fx("burst")
	if b:
		var st := String(FxCues.MAT_BURST.get(mat, "dust"))
		var dv: Vector3 = e.get("dir", Vector3.ZERO)
		b.call("play", _apos(a), -dv if dv.length_squared() > 1e-6 else Vector3.UP, 0.6 + 0.15 * int(e.get("tier", 0)), st)


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
			var tier := int(e.get("tier", 0))
			if e.get("result", "") == "knockdown":
				tier = 3
			elif big:
				tier = maxi(tier, 1)
			feel("t%d" % clampi(tier, 0, 3), _apos(a), e.get("dir", Vector3.ZERO), -1)
			_impact_cue(e, a)
			if e.get("result", "") == "knockdown":
				audio.play("knockdown", _apos(a))
		"block":
			audio.play("block", _apos(a) if a >= 0 else _bpos(e.get("body", -1)))
			_haptic("block", a)
			var heavy_block := float(e.get("power", 0.0)) >= 25.0
			feel("block_heavy" if heavy_block else "block", _apos(a) if a >= 0 else _bpos(e.get("body", -1)), e.get("dir", Vector3.ZERO), -1)
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
			feel("perfect", _apos(a), Vector3.ZERO, -1)
			if hud and a == player_id and (settings == null or settings.flashes > 0.05):
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
			if views and int(e.get("tier", 0)) > 0:
				views.tier_hint[int(e.body)] = int(e.tier)
			_launch_tier(a, int(e.get("tier", 0)), _bpos(e.body))
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
			# The body may already be merged away; actor transforms (Ice's frost: wet -> frozen) carry no body.
			var tb := int(e.get("body", -1))
			var p: Vector3 = e["at"] if e.has("at") else _bpos(tb)
			match to:
				"molten":
					audio.play("melt_rise", p)
					_haptic("transform", world.get_body(tb).controller if world.get_body(tb) else -1)
					_zoom(p)
				"wave":
					audio.play("lava_splat", p)
					_call(_fx("ember"), "play", [p, Vector3.UP, 0.8])
				"rock":
					_zoom(p)
					audio.play("crust_hiss", p)
					audio.play("cool_crack", p, -3.0)
					_call(_fx("steam"), "play", [p + Vector3(0, 0.2, 0), 0.5])
				"ice":
					audio.play("freeze", p)
					_zoom(p)
				"frozen":
					audio.play("freeze", p, -3.0)   # a soaked fighter frosts over (the status cue draws the rime)
				"water":
					audio.play("ice_melt_drip", p)
					# The melted ice slumps into a puddle (often merging into one nearby).
					_call(_fx("splash"), "play", [p, Vector3.UP, 0.35])
				"puddle":
					audio.play("water_splash", p, -4.0)
					_call(_fx("splash"), "play", [p, Vector3.UP, 0.7])
		"shatter":
			audio.play("ice_shatter", _bpos(e.body))
			_call(_fx("splash"), "play", [_bpos(e.body), Vector3.UP, 0.5])
			var shd := _fx("shards")
			if shd:
				shd.call("play", _bpos(e.body), Vector3.UP, 0.8, "ice", int(e.body) * 31 + world.tick, _bpos(e.body).y - 1.0)
			feel("shatter", _bpos(e.body), Vector3.ZERO, -1)
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
			# Tap: a narrow palm push. Hold (cyclone): a wide, long blast doubled by a tighter inner
			# cone, with dust torn up in front of the feet, so it reads as clearly the bigger move.
			var heavy: bool = e.get("heavy", false)
			var o3 := _apos(a)
			var gd: Vector3 = e.dir
			var rng := float(e.range)
			if heavy:
				_call(_fx("air_push"), "play", [o3, gd, 2.4, rng * 1.15])
				_call(_fx("air_push"), "play", [o3, gd, 1.3, rng * 0.8])
				var ga := world.get_actor(a)
				if ga and ga.grounded:
					_call(_fx("dust_puff"), "play", [ga.pos + gd * 1.0, (gd + Vector3.UP).normalized(), 1.2])
			else:
				_call(_fx("air_push"), "play", [o3, gd, 0.75, rng * 0.8])
			audio.play("air_gust" if heavy else "air_push", o3)
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
			_draw_stream(a, e.get("at", _apos(a)), int(e.get("body", -1)))
		"evade":
			audio.play("dash_air" if e.get("dash", false) else "evade_whoosh", _apos(a))
			if e.get("dash", false):
				_dash_trail(a, e.get("dir", Vector3.ZERO))
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
		"fx":
			cues.fx_event(e)
		"interaction":
			cues.interaction(e)
		"charge":
			cues.charge(e)
		"status":
			cues.status(e)
		"zone":
			cues.zone(e)
		"clash":
			cues.clash(e)
		"morph", "chain", "weave", "counter_cancel", "slump", "convert", "capture", "ricochet", "stance", "mode", \
				"inrush", "extinguish", "current_grounded", "fork", "stick":
			cues.misc(e)


func _call(n: Node, m: String, args: Array) -> void:
	if n != null and n.has_method(m):
		n.callv(m, args)


func _update_transient(dt: float) -> void:
	for k in range(_transient.size() - 1, -1, -1):
		var tr: Dictionary = _transient[k]
		tr.t = float(tr.t) + dt
		var n: Node = tr.node
		var kind := String(tr.get("kind", "lash"))
		if float(tr.t) >= float(tr.dur) or not is_instance_valid(n):
			if is_instance_valid(n):
				if kind == "dash":
					n.call("end")   # the trail fades out, then returns itself to the pool
				else:
					views.pool.release(n)
			_transient.remove_at(k)
			continue
		if kind == "draw":
			_draw_ribbon(tr, n)
			continue
		if kind == "dash":
			var fv: FighterView = fighters.get(tr.actor, null)
			n.call("push", fv.global_position + Vector3(0, 0.95, 0) if fv else _apos(tr.actor))
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


## Drawing water: a thin stream arcs from the source (pool / puddle surface) up into the held orb.
## The sim reports the draw every 8 ticks; each report keeps the actor's one stream alive a bit
## longer, so it lasts exactly as long as water is flowing and then thins away.
func _draw_stream(a: int, at: Vector3, body: int) -> void:
	for tr in _transient:
		if String(tr.get("kind", "")) == "draw" and int(tr.actor) == a:
			tr.dur = float(tr.t) + DRAW_HOLD
			tr.at = at
			tr.body = body
			if float(tr.t) - float(tr.splash) > 0.45:
				tr.splash = tr.t
				_call(_fx("splash"), "play", [at, Vector3.UP, 0.25])
			return
	var rib := _hold("water_ribbon")
	if rib == null:
		return
	_transient.append({"node": rib, "t": 0.0, "dur": DRAW_HOLD, "actor": a, "kind": "draw", "at": at,
		"body": body, "splash": 0.0})
	_call(_fx("splash"), "play", [at, Vector3.UP, 0.3])


func _draw_ribbon(tr: Dictionary, n: Node) -> void:
	var t := float(tr.t)
	var b := world.get_body(int(tr.body))
	var held := b != null and b.alive and b.controller == int(tr.actor)
	if not held:
		# Released (or dropped): the stream lets go at once instead of chasing the thrown water.
		tr.dur = minf(float(tr.dur), t + 0.06)
	var end := _hand(tr.actor)
	if held:
		var vn: Node3D = views.view_of(b.id) if views else null
		end = vn.global_position if vn != null and vn.is_inside_tree() else b.pos
	var start: Vector3 = tr.at
	var d := end - start
	var side := d.cross(Vector3.UP)
	side = side.normalized() if side.length_squared() > 1e-6 else Vector3.RIGHT
	var lift := 0.25 + 0.12 * d.length()
	var r_end := clampf(b.radius * 0.5, 0.04, 0.12) if held else 0.04
	var env := clampf(t / 0.1, 0.0, 1.0) * clampf((float(tr.dur) - t) / 0.06, 0.0, 1.0)
	var pts := PackedVector3Array()
	var rad := PackedFloat32Array()
	for i in 10:
		var u := float(i) / 9.0
		var arc := sin(u * PI)
		pts.append(start.lerp(end, u) + Vector3.UP * arc * lift + side * sin(u * 6.0 - t * 9.0) * 0.05 * arc)
		# Wide where it leaves the surface, thin in flight, swelling into the orb; pulses run upward.
		var r := 0.035 + 0.04 * (1.0 - smoothstep(0.0, 0.25, u)) + (r_end - 0.035) * u * u
		rad.append(r * env * (1.0 + 0.3 * sin(u * 14.0 - t * 22.0)))
	n.call("set_points", pts, rad)
	n.call("set_state", 0.0)


## Air dash: a faint wind streak along the dash path plus a puff where it pushed off the ground.
func _dash_trail(a: int, dir: Vector3) -> void:
	var ac := world.get_actor(a)
	if ac and ac.grounded:
		_call(_fx("dust_puff"), "play", [ac.pos, (Vector3.UP - dir * 0.8).normalized(), 0.35])
	var tr := _hold("glide_trail")
	if tr == null:
		return
	tr.call("set_width", 0.18)
	tr.call("begin", null)
	_transient.append({"node": tr, "t": 0.0, "dur": 0.28, "actor": a, "kind": "dash"})


func update_continuous(dt: float) -> void:
	## Per frame: loops and held effects driven by current state.
	_update_transient(dt)
	if _slowmo > 0.0:
		_slowmo -= dt / maxf(Engine.time_scale, 0.1)
	if _hs_frames > 0:
		_hs_frames -= 1
		_hs_hist.append(Time.get_ticks_msec())
	_apply_time_scale()
	if cues:
		cues.update(dt)
	bridge.update(world, fighters, dt)
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


## Engine.time_scale follows hit-stop + slow-mo; only touched while one of them runs (and once to
## restore 1.0), and only in a live scene tree (headless unit tests never change it).
func _apply_time_scale() -> void:
	var want := time_scale_wanted()
	if want == _scale_applied:
		return
	_scale_applied = want
	if is_inside_tree():
		Engine.time_scale = want


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
