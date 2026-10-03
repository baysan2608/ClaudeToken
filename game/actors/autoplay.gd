class_name Autoplay
extends RefCounted
## Scripted player input for capturing gameplay evidence and soak tests.
## It produces InputFrames exactly like the touch layer does, so everything it
## shows goes through the real game path. Usage (user args after `--`):
##   --autoplay=flagship[:seconds]   intercept, melt, pour at the rival, repeat
##   --autoplay=soak[:seconds]       random play across all elements (perf soak)
##   --autoplay=tour[:seconds]       element tour vs targets
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


func _init(spec: String) -> void:
	var parts := spec.split(":")
	mode = parts[0]
	if parts.size() > 1:
		duration = float(parts[1])
	match mode:
		"flagship":
			scenario = "molten_exchange"
		"soak":
			scenario = "spar"
		"tour":
			scenario = "conduction"
		_:
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
	if mode == "soak":
		g.progress.lab_mode = true
		g.player.kit = g.progress.kit()


func frame(g: Game) -> InputFrame:
	_t += Sim.DT
	f.clear_edges()
	f.tech_aim_active = false
	match mode:
		"flagship":
			_flagship(g)
		"soak":
			_soak(g)
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
				if b.alive and b.is_projectile() and b.attack_owner != p.id and b.is_stone() and b.pos.distance_to(p.chest()) < 7.5 + rng.randf() * 1.0:
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
		f.move = f.move
		return
	_next = _t + rng.randf_range(0.15, 0.6)
	f.move = Vector2(rng.randf_range(-1, 1), rng.randf_range(-1, 1)).limit_length(1.0)
	f.cam_delta = Vector2(rng.randf_range(-0.05, 0.05), 0.0)
	var r := rng.randf()
	f.attack_held = false
	f.guard_held = false
	if f.tech_held and rng.randf() < 0.5:
		f.tech_held = false
		f.tech_released = true
	if r < 0.08:
		f.element_select = rng.randi_range(0, 3)
	elif r < 0.35:
		f.attack_pressed = true
		f.attack_held = rng.randf() < 0.3
	elif r < 0.5:
		f.guard_pressed = true
		f.guard_held = true
	elif r < 0.6:
		f.evade_pressed = true
	elif r < 0.8:
		f.tech_pressed = true
		f.tech_held = true
	if _t > 2.0 and int(_t) % 60 == 0 and _t - floor(_t) < Sim.DT:
		_shot("soak_%d" % int(_t))


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


func after_tick(_g: Game, _evs: Array[Dictionary]) -> void:
	pass
