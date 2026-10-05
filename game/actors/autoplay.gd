class_name Autoplay
extends RefCounted
## Scripted player input for capturing gameplay evidence and soak tests.
## It produces InputFrames exactly like the touch layer does, so everything it
## shows goes through the real game path. Usage (user args after `--`):
##   --autoplay=flagship[:seconds]   intercept, melt, pour at the rival, repeat
##   --autoplay=soak[:seconds]       random play across all elements, sub-elements, slots and gestures (perf soak)
##   --autoplay=duel[:seconds]       AI vs AI on the spar scenario (master presets, all four elements; docs/AI.md)
##   --autoplay=tour[:seconds]       element tour vs targets
##   --autoplay=show_<name>[:seconds] scripted showcase (res://actors/showcase_<name>.gd), e.g. show_owner (the owner's
##                                    examples across all four elements), show_earth_magma, show_fire_lightning ...
##   --shots=<dir>                    save screenshots at key events
##   --perf=<file.json>               write perf stats at the end

var mode := "flagship"
var scenario := "molten_exchange"
var duration := 30.0
var shots_dir := ""
var perf_path := ""
var f := InputFrame.new()
var rng := RandomNumberGenerator.new()
var _t := 0.0
var _state := "wait"
var _body := -1
var _shot_flags := {}
var _hold_t := 0.0
var _next := 0.0
var _rounds := 0
var _game: Game
## Duel mode: the brain that plays the player's fighter (its intents are turned into InputFrames).
var duel_ai: AiBrain = null
var _duel_seed := 77
## Element showcase choreography (res://actors/showcase_<name>.gd), used by --autoplay=show_<name>.
## Interface: scenario() -> String, bind(g: Game), frame(g: Game, f: InputFrame, t: float), shot(name) callback.
var show: Object = null


func _init(spec: String) -> void:
	var parts := spec.split(":")
	mode = parts[0]
	if parts.size() > 1:
		duration = float(parts[1])
	if mode.begins_with("show_"):
		var path := "res://actors/showcase_%s.gd" % mode.substr(5)
		if ResourceLoader.exists(path):
			show = (load(path) as GDScript).new()
			scenario = String(show.call("scenario"))
	match mode:
		"flagship":
			scenario = "molten_exchange"
		"soak", "duel":
			scenario = "spar"
		"tour":
			scenario = "conduction"
		_:
			if show == null:
				scenario = mode
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--shots="):
			shots_dir = a.substr(8)
			DirAccess.make_dir_recursive_absolute(shots_dir)
		elif a.begins_with("--perf="):
			perf_path = a.substr(7)
	rng.seed = 42


func bind(g: Game) -> void:
	_game = g
	if show != null and show.has_method("bind"):
		show.call("bind", g)
	if mode == "soak" or mode == "duel":
		g.progress.lab_mode = true
		g.player.kit = g.progress.kit()
		g.player.elements = [true, true, true, true]
	if mode == "duel":
		# Both fighters are driven by the planner: the rival through the game's own brain, the player's
		# fighter through a second brain whose intents go through InputFrame -> PlayerController.
		if g.ai != null and g.ai.has_method("configure"):
			g.ai.configure({"preset": "master", "elements": [0, 1, 2, 3]})
		duel_ai = AiBrain.new(g.world, g.player, {}, _duel_seed)
		duel_ai.configure({"preset": "master", "elements": [0, 1, 2, 3]})
		_duel_seed += 1


func frame(g: Game) -> InputFrame:
	_t += Sim.DT
	f.clear_edges()
	f.tech_aim_active = false
	if show != null:
		show.call("frame", g, f, _t)
		if show.has_method("wants_shot"):
			var sn := String(show.call("wants_shot"))
			if sn != "":
				_shot(sn)
	match mode:
		"flagship":
			_flagship(g)
		"soak":
			_soak(g)
		"duel":
			_duel(g)
		"tour":
			_tour(g)
	if _t >= duration:
		_finish(g)
	return f


func _finish(g: Game) -> void:
	if perf_path != "":
		g.perf.dump(perf_path)
	g.get_tree().quit(0)


func _shot(name: String) -> void:
	if shots_dir == "" or _shot_flags.has(name):
		return
	_shot_flags[name] = true
	_game.get_tree().create_timer(0.05, true, false, true).timeout.connect(func():
		var img := _game.get_viewport().get_texture().get_image()
		img.save_png(shots_dir.path_join("%02d_%s.png" % [_shot_flags.size(), name])))


func _flagship(g: Game) -> void:
	var w := g.world
	var p := g.player
	match _state:
		"wait":
			f.move = Vector2.ZERO
			f.attack_held = false
			for b in w.bodies:
				if b.alive and b.is_projectile() and b.attack_owner != p.id and b.is_stone() and b.mass > 25.0 \
						and b.pos.distance_to(p.chest()) < 4.5:
					# Too much mass to melt with the Focus at hand: sidestep it instead.
					f.move = Vector2(1.0, 0.0)
					f.evade_pressed = true
					_shot("sidestep_heavy")
					break
				if b.alive and b.is_projectile() and b.attack_owner != p.id and b.is_stone() and b.mass <= 25.0 \
						and b.pos.distance_to(p.chest()) < 7.5 + rng.randf() * 1.0:
					_body = b.id
					f.tech_pressed = true
					f.tech_held = true
					_state = "catch"
					_shot("incoming")
					break
		"catch":
			f.tech_held = true
			var b := w.get_body(_body)
			if b and b.controller == p.id:
				_state = "melt"
				_shot("caught")
			elif p.action == null:
				_state = "wait"
		"melt":
			var b := w.get_body(_body)
			f.tech_held = true
			if b == null or b.controller != p.id:
				_state = "wait"
			elif b.phase == Sim.Phase.MOLTEN and (b.liquid >= 0.99 or w.player_focus_low(p)):
				_shot("molten")
				f.tech_held = false
				f.tech_released = true
				_state = "wave"
				_hold_t = _t
			elif b.liquid > 0.3:
				_shot("heating")
		"wave":
			var b := w.get_body(_body)
			if OS.has_environment("AUTOPLAY_TRACE") and b and w.tick % 6 == 0:
				print("trace t=%.2f %s pos=%s v=%.2f budget=%.1f rival=%s" % [_t, b.describe(), str(b.pos), b.vel.length(), b.wave_budget, g.ai.debug_state if g.ai else "-"])
			if b and b.form == Sim.Form.WAVE and _t - _hold_t > 0.6:
				_shot("wave")
			if b and b.last_verb == "draw":
				_shot("rival_draws_heat")
			if b == null or (b.form != Sim.Form.WAVE and _t - _hold_t > 1.0):
				_state = "after"
				_hold_t = _t
		"after":
			var b := w.get_body(_body)
			if b and b.phase == Sim.Phase.SOLID:
				_shot("cooled_rock")
			if _t - _hold_t > 1.6:
				_rounds += 1
				if _rounds % 2 == 0 and b and b.alive and b.phase == Sim.Phase.SOLID and b.pos.distance_to(p.pos) < 9.0:
					# Reuse the cooled rock: seize it with Earth and throw it back.
					f.element_select = Sim.Element.EARTH
					_state = "seize"
					_hold_t = _t
				else:
					_state = "wait"
		"seize":
			if _t - _hold_t > 0.1 and _t - _hold_t < 0.15:
				f.tech_pressed = true
			f.tech_held = _t - _hold_t < 1.2
			if _t - _hold_t >= 1.2:
				f.tech_released = true
				_shot("rock_reused")
				f.element_select = Sim.Element.FIRE
				_state = "wait"
	# Gentle strafing between exchanges keeps the camera honest.
	if _state == "wait":
		f.move = Vector2(sin(_t * 0.7) * 0.35, 0.0)


func _soak(g: Game) -> void:
	if _t < _next:
		# Holds between decisions: keep the guard flick / evade hold going for their tick.
		return
	_next = _t + rng.randf_range(0.15, 0.6)
	f.move = Vector2(rng.randf_range(-1, 1), rng.randf_range(-1, 1)).limit_length(1.0)
	f.cam_delta = Vector2(rng.randf_range(-0.05, 0.05), 0.0)
	var r := rng.randf()
	var guarding := f.guard_held
	f.attack_held = false
	f.guard_held = false
	f.evade_held = false
	if f.tech_held and rng.randf() < 0.5:
		f.tech_held = false
		f.tech_released = true
	if r < 0.06:
		f.element_select = rng.randi_range(0, 3)
	elif r < 0.12:
		f.sub_select = rng.randi_range(0, 3)       # sub-element ring
	elif r < 0.36:
		f.attack_pressed = true
		f.attack_held = rng.randf() < 0.35         # charge: T1..T3 by how long the next decisions keep it
		if rng.randf() < 0.5:
			f.attack_gesture = rng.randi_range(1, 3)   # thrust / ground / sweep flick
	elif r < 0.46:
		f.guard_pressed = true
		f.guard_held = true
	elif r < 0.52 and guarding:
		f.guard_held = true
		f.guard_gesture = Sim.Gesture.UP if rng.randf() < 0.5 else Sim.Gesture.DOWN   # push / sink
	elif r < 0.62:
		f.evade_pressed = true
		f.evade_held = rng.randf() < 0.4           # evade_hold morph after 0.2 s
	elif r < 0.82:
		f.tech_pressed = true
		f.tech_held = true
		if rng.randf() < 0.4:
			f.tech_aim = Vector2(rng.randf_range(-1, 1), rng.randf_range(0.2, 1)).limit_length(1.0)
			f.tech_aim_active = true
	if _t > 2.0 and int(_t) % 60 == 0 and _t - floor(_t) < Sim.DT:
		_shot("soak_%d" % int(_t))


## AI vs AI: the player's brain decides; its ActorIntent is mapped back to screen-space input with the
## camera yaw PlayerController will use, so the real input path drives the fighter.
func _duel(g: Game) -> void:
	if duel_ai == null or duel_ai.w != g.world:
		# The scenario reloaded (KO reset): rebind the player's brain to the new world.
		duel_ai = AiBrain.new(g.world, g.player, {}, _duel_seed)
		duel_ai.configure({"preset": "master", "elements": [0, 1, 2, 3]})
		_duel_seed += 1
	if g.ai != null and not g.ai.planner:
		g.ai.configure({"preset": "master", "elements": [0, 1, 2, 3]})
	var it := duel_ai.think(Sim.DT)
	var yaw: float = g.cam.yaw
	var fwd := Vector3(sin(yaw), 0.0, cos(yaw))
	var right := fwd.cross(Vector3.UP)
	f.move = Vector2(it.move.dot(right), it.move.dot(fwd)).limit_length(1.0)
	f.attack_pressed = it.attack_pressed
	f.attack_held = it.attack_held
	f.attack_released = it.attack_released
	f.guard_pressed = it.guard_pressed
	f.guard_held = it.guard_held
	f.guard_released = false
	f.evade_pressed = it.evade_pressed
	f.evade_held = it.evade_held
	f.tech_pressed = it.tech_pressed
	f.tech_held = it.tech_held
	f.tech_released = it.tech_released
	f.tech_cancel = it.tech_cancel
	f.element_select = it.element_select
	f.sub_select = it.sub_select
	f.attack_gesture = it.attack_gesture
	f.guard_gesture = it.guard_gesture
	f.target_cycle = it.target_cycle
	f.tech_aim_active = it.aim_active
	if it.aim_active:
		f.tech_aim = Vector2(it.aim_dir.dot(right), maxf(it.aim_dir.dot(fwd), -0.2)).limit_length(1.0)
	if int(_t * 60.0) % 300 == 0:
		_shot("duel_%03d" % int(_t))


func _tour(g: Game) -> void:
	# Charge lightning at the target standing in the pool every few seconds.
	var p := g.player
	var cyc := fmod(_t, 4.0)
	f.attack_held = cyc > 0.5 and cyc < 1.4
	f.attack_pressed = cyc > 0.5 and cyc - Sim.DT <= 0.5
	f.attack_released = cyc >= 1.4 and cyc - Sim.DT < 1.4
	if f.attack_pressed:
		p.lock_target = 2
	if cyc > 1.35 and cyc < 1.45:
		_shot("lightning_%d" % int(_t / 4.0))


func after_tick(g: Game, evs: Array[Dictionary]) -> void:
	if show != null and show.has_method("after_tick"):
		show.call("after_tick", g, evs)
	if not OS.has_environment("AUTOPLAY_TRACE"):
		return
	for e in evs:
		if e.get("actor", -99) == g.player.id or e.type in ["transform", "control_lost", "control_fail", "launch"]:
			if e.type in ["action"] and e.get("phase", "") != "startup":
				continue
			print("ev t=%.2f %s foc=%.0f heat=%.0f" % [_t, str(e), g.player.focus, g.player.heat_reserve])
