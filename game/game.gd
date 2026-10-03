class_name Game
extends Node3D
## Root of the playable lab. Runs the authoritative CombatWorld at a fixed 60 Hz
## in _physics_process (input -> intents -> step -> events), and renders views
## interpolated between the last two sim states in _process.

const PLAYER_COLORS := {"cloth_main": Color(0.19, 0.23, 0.31), "cloth_accent": Color(0.62, 0.5, 0.32),
	"wraps": Color(0.86, 0.83, 0.77), "skin": Color(0.78, 0.6, 0.48), "hair": Color(0.12, 0.1, 0.09)}
const RIVAL_COLORS := {"cloth_main": Color(0.4, 0.2, 0.15), "cloth_accent": Color(0.78, 0.58, 0.28),
	"wraps": Color(0.72, 0.68, 0.62), "skin": Color(0.62, 0.45, 0.34), "hair": Color(0.08, 0.07, 0.07)}
const DUMMY_COLORS := {"cloth_main": Color(0.56, 0.5, 0.4), "cloth_accent": Color(0.4, 0.36, 0.3),
	"wraps": Color(0.76, 0.71, 0.62), "skin": Color(0.6, 0.52, 0.42), "hair": Color(0.45, 0.4, 0.33)}

var settings: GameSettings
var progress: Progression
var hub: PlayerInputHub
var cam: CameraRig
var arena_view: ArenaView
var body_views: BodyViews
var fx: FxDirector
var audio: AudioDirector
var hud: Hud
var perf := PerfMonitor.new()

var world: CombatWorld
var player: ActorState
var opponent: ActorState
var ai: AiBrain
var pc := PlayerController.new()
var fighters := {}
var scenario_id := ""
var scen_def := {}
var intents := {}
var autoplay: Autoplay = null
var _launcher := {}
var _launch_t := 0.0
var _launch_i := 0
var _launch_body := -1
var _challenge_n := 0
var _seed := 1
var _paused := false
var quality := 2             # 2 high, 1 medium, 0 low
var _auto_quality := true
var _q_timer := 0.0


func _ready() -> void:
	settings = GameSettings.current()
	progress = Progression.load_from()
	physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	audio = AudioDirector.new()
	add_child(audio)
	arena_view = ArenaView.new()
	add_child(arena_view)
	body_views = BodyViews.new()
	add_child(body_views)
	cam = CameraRig.new()
	add_child(cam)
	hud = Hud.new()
	var hl := CanvasLayer.new()
	hl.layer = 70
	add_child(hl)
	hl.add_child(hud)
	hud.cam = cam
	fx = FxDirector.new()
	add_child(fx)
	fx.audio = audio
	fx.views = body_views
	fx.cam = cam
	fx.hud = hud
	fx.settings = settings
	hub = PlayerInputHub.new()
	hub.force_touch_ui = "--touchui" in OS.get_cmdline_user_args()
	hub.process_mode = Node.PROCESS_MODE_ALWAYS
	add_child(hub)
	hub.paused_requested.connect(_on_pause)
	hub.settings_changed.connect(_apply_settings)
	var sp := hub.settings_panel
	sp.resume_requested.connect(_resume)
	sp.closed.connect(_resume)
	sp.reset_requested.connect(func(): load_scenario(scenario_id); _resume_and_close())
	sp.practice_selected.connect(_on_practice_selected)
	sp.reset_progress_requested.connect(_reset_progress)
	sp.quit_to_lab_requested.connect(func(): load_scenario("molten_exchange"); _resume_and_close())
	_apply_settings()
	audio.ambience(true)
	var start := progress.last_scenario
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--scenario="):
			start = arg.substr(11)
		elif arg.begins_with("--autoplay="):
			autoplay = Autoplay.new(arg.substr(11))
		elif arg == "--debug":
			settings.show_debug = true
		elif arg.begins_with("--seed="):
			_seed = int(arg.substr(7))
		elif arg.begins_with("--quality="):
			quality = int(arg.substr(10))
			_auto_quality = false
	if autoplay:
		start = autoplay.scenario
	load_scenario(start)


func load_scenario(id: String) -> void:
	var r := Scenarios.build(id, progress, _seed)
	scenario_id = r.def.id
	scen_def = r.def
	world = r.world
	player = r.player
	opponent = r.opponent
	ai = AiBrain.new(world, opponent, r.ai_cfg, _seed + 7) if opponent else null
	_launcher = r.launcher
	_launch_t = 1.5
	_launch_i = 0
	_launch_body = -1
	_challenge_n = 0
	intents.clear()
	arena_view.build(world.arena, quality)
	_apply_quality()
	body_views.bind(world)
	for f in fighters.values():
		f.queue_free()
	fighters.clear()
	for a in world.actors:
		var fv := FighterView.new()
		add_child(fv)
		var pal: Dictionary = PLAYER_COLORS if a.team == 0 else (DUMMY_COLORS if a.is_dummy else RIVAL_COLORS)
		fv.setup(a.id, pal.duplicate())
		fv.snap(a)
		fighters[a.id] = fv
	fighters[player.id].set_accent(UiStyle.element_color(player.element))
	fx.bind(world)
	fx.fighters = fighters
	fx.player_id = player.id
	hud.world = world
	hud.player_id = player.id
	hud.objective = String(scen_def.get("objective", ""))
	_update_challenge_text()
	cam.arena = world.arena
	var look := opponent.pos if opponent else player.pos + Vector3(0, 0, -5)
	if world.actors.size() > 1 and opponent == null:
		look = world.actors[1].pos
	cam.snap_to(player.pos, look)
	hub.settings_panel.set_practice_items(Scenarios.practice_items(progress))
	hub.release_all()
	progress.last_scenario = scenario_id
	progress.save()
	if autoplay:
		autoplay.bind(self)


## Quality tiers trade secondary cost (resolution, shadows, glow) before ever touching
## input handling or attack readability.
func _apply_quality() -> void:
	var vp := get_viewport()
	vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
	vp.scaling_3d_scale = [0.7, 0.85, 1.0][quality]
	arena_view.set_quality(quality)
	if arena_view.env and arena_view.env.environment:
		arena_view.env.environment.glow_enabled = quality >= 1
	vp.msaa_3d = Viewport.MSAA_2X if quality >= 2 else Viewport.MSAA_DISABLED


func _adapt_quality(dt: float) -> void:
	## Sustained frame-time p95 over budget for 4 s steps quality down (never up mid-session).
	if not _auto_quality or quality == 0:
		return
	if perf.total_frames < 240:
		return
	var st := perf.stats()
	if float(st.p95_ms) > 18.5:
		_q_timer += dt
		if _q_timer > 4.0:
			quality -= 1
			_q_timer = 0.0
			_apply_quality()
			print("[quality] stepped down to %d (p95 %.1f ms)" % [quality, st.p95_ms])
	else:
		_q_timer = 0.0


func _apply_settings() -> void:
	settings = GameSettings.current()
	cam.sensitivity = settings.camera_sensitivity
	cam.invert_y = settings.invert_y
	cam.shake_scale = settings.screen_shake
	cam.reduced_motion = settings.reduced_motion
	hud.show_debug = settings.show_debug
	fx.settings = settings


func _on_pause() -> void:
	_paused = true
	get_tree().paused = true
	hub.release_all()


func _resume() -> void:
	if not _paused:
		return
	_paused = false
	get_tree().paused = false
	hub.release_all()
	Engine.time_scale = 1.0


func _resume_and_close() -> void:
	if hub.settings_panel.is_open():
		hub.settings_panel.close_panel()
	_resume()


func _on_practice_selected(id: Variant) -> void:
	var sid := String(id)
	if sid == "__lab_mode":
		progress.lab_mode = not progress.lab_mode
		progress.save()
		hub.settings_panel.set_practice_items(Scenarios.practice_items(progress))
		load_scenario(scenario_id)
		hud.toast("Lab mode " + ("on" if progress.lab_mode else "off"))
	else:
		load_scenario(sid)
	_resume_and_close()


func _reset_progress() -> void:
	progress.reset()
	load_scenario(scenario_id)


func _notification(what: int) -> void:
	# OS interruption (call, notification centre, app switch): release every touch
	# and pause, so nothing stays held or keeps charging in the background.
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT or what == NOTIFICATION_APPLICATION_PAUSED:
		if hub and not _paused and autoplay == null:
			hub.release_all()
			hub.open_settings()
			_on_pause()


# ================================================================ simulation tick

func _physics_process(_dt: float) -> void:
	if _paused or world == null:
		return
	var f := hub.poll_frame()
	if autoplay:
		f = autoplay.frame(self)
	cam.add_input(f.cam_delta)
	intents[player.id] = pc.build(f, cam.yaw)
	if ai:
		intents[opponent.id] = ai.think(Sim.DT)
	_scenario_tick()
	var t0 := Time.get_ticks_usec()
	world.step(intents)
	perf.sim_us(Time.get_ticks_usec() - t0)
	var evs := world.take_events()
	fx.handle(evs)
	_challenges(evs)
	for e in evs:
		if e.type == "element" and e.actor == player.id:
			fighters[player.id].set_accent(UiStyle.element_color(player.element))
	for a in world.actors:
		fighters[a.id].push_state(a)
	body_views.push_state()
	if autoplay:
		autoplay.after_tick(self, evs)


func _scenario_tick() -> void:
	if not _launcher.is_empty():
		_launch_t -= Sim.DT
		var lb := world.get_body(_launch_body)
		if lb == null and _launch_t <= 0.6:
			# Telegraph: the next stone appears on the plinth 0.6 s before it fires.
			var masses: Array = _launcher.masses
			var m: float = masses[_launch_i % masses.size()]
			_launch_i += 1
			lb = world.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, m, _launcher.pos, "launcher")
			lb.static_body = true
			_launch_body = lb.id
			world.mass_ledger.ground_taken += m
		if lb != null and lb.static_body and _launch_t <= 0.0:
			lb.static_body = false
			var spd := 15.0 if lb.mass < 100.0 else 11.0
			lb.vel = ActEarth.launch_vel(lb.pos, player.chest(), spd)
			lb.attack_id = world.new_attack_id()
			lb.attack_owner = -1
			lb.damage = 12.0 * sqrt(lb.mass / 20.0)
			lb.balance_damage = minf(90.0, 24.0 * sqrt(lb.mass / 20.0))
			lb.max_life = Sim.REMNANT_LIFETIME
			world.emit("launch", {"actor": -1, "body": lb.id, "speed": spd, "heavy": lb.mass > 40.0})
			_launch_body = -1
			_launch_t = float(_launcher.interval)
	# Vents keep training lava molten until someone draws it solid.
	for b in world.bodies:
		if b.alive and b.origin == "vent" and b.liquid > 0.0 and b.liquid < 1.0 and b.controller < 0:
			var e := 55.0 * Sim.DT
			var used := Thermal.apply_heat(b, e).x
			world.ledger.generated += used


func _challenges(evs: Array[Dictionary]) -> void:
	var ch: Dictionary = scen_def.get("challenge", {})
	if ch.is_empty() or progress.is_done(ch.id):
		return
	var before := _challenge_n
	match String(ch.id):
		"m_stone_reader":
			for e in evs:
				if e.type == "perfect_deflect" and e.actor == player.id and e.get("verb", "") == "redirect":
					_challenge_n += 1
		"m_storm_eye":
			for e in evs:
				if e.type == "conduct" and e.actor == player.id and (e.victims as Array).size() >= 2:
					_challenge_n += 1
		"m_return":
			for e in evs:
				if e.type == "lightning_redirect" and e.actor == player.id:
					_challenge_n += 1
		"m_cold_hands":
			for e in evs:
				if e.type == "transform" and e.to == "rock":
					var b := world.get_body(e.body)
					if b and b.origin == "vent" and b.last_actor == player.id:
						_challenge_n += 1
		"m_updraft":
			var hl := world.arena.solid_named("high_ledge")
			if player.grounded and player.pos.y > 1.7 and player.pos.x < (hl.max as Vector3).x and player.pos.z < (hl.max as Vector3).z:
				_challenge_n = 1
	if _challenge_n != before:
		_update_challenge_text()
		if _challenge_n >= int(ch.count):
			progress.complete(ch.id, ch.unlock)
			audio.ui("unlock")
			Haptics.play("transform")
			hud.toast("Mastered %s · unlocked %s" % [ch.title, String(ch.unlock).replace("_", " ")])
			_update_challenge_text()
			hub.settings_panel.set_practice_items(Scenarios.practice_items(progress))


func _update_challenge_text() -> void:
	var ch: Dictionary = scen_def.get("challenge", {})
	if ch.is_empty():
		hud.challenge_text = ""
	elif progress.is_done(ch.id):
		hud.challenge_text = "✓ %s" % ch.title
	else:
		hud.challenge_text = "%s  %d/%d" % [ch.text, mini(_challenge_n, int(ch.count)), int(ch.count)]


# ================================================================ presentation

func _process(dt: float) -> void:
	if world == null:
		return
	perf.frame(dt)
	_adapt_quality(dt)
	var alpha := Engine.get_physics_interpolation_fraction()
	for a in world.actors:
		fighters[a.id].render(a, alpha, dt)
	body_views.render(alpha)
	var tgt := world.get_actor(player.lock_target)
	var threat: Variant = _threat_pos()
	cam.update_rig(dt, fighters[player.id].global_position, tgt.pos if tgt else null, threat)
	fx.update_continuous(dt)
	hub.set_context(_hud_context(tgt))
	if hud.show_debug:
		hud.debug_lines = _debug_lines()
		hud.perf_text = perf.summary()


func _threat_pos() -> Variant:
	for b in world.bodies:
		if b.alive and b.attack_id != 0 and b.attack_owner != player.id:
			var to := player.chest() - b.pos
			if to.length() < 14.0 and (b.form == Sim.Form.WAVE or b.vel.dot(to) > 0.0):
				return b.pos
	return null


func _hud_context(tgt: ActorState) -> Dictionary:
	var label := ""
	var ok := true
	var marker_pos: Variant = null
	var marker_label := ""
	var held := world.held(player)
	match player.element:
		Sim.Element.EARTH:
			label = "THROW" if held else "LIFT"
		Sim.Element.WATER:
			label = "STREAM" if held else "DRAW"
			ok = held != null or player.water_carried > 0.5 or player.pos.distance_to(Vector3(10, 0, -1)) < 9.0
		Sim.Element.FIRE:
			if held and held.is_stone():
				label = "POUR" if held.phase == Sim.Phase.MOLTEN else "HEAT"
			else:
				var pv := ActFire.preview(world, player, world.aim_dir(player, intents.get(player.id, ActorIntent.new())))
				label = String(pv.mode) if pv.mode != "" else "—"
				ok = pv.ok
				var pb := world.get_body(pv.body)
				if pb:
					marker_pos = cam.world_to_screen(pb.pos)
					marker_label = "%s %d kg%s" % [pv.mode, int(pb.mass), "  too heavy" if pv.reason == "mass" else ""]
		Sim.Element.AIR:
			label = "GLIDE" if not player.grounded and player.has("glide") else "LIFT"
	var unlocked: Array[int] = []
	for i in 4:
		if player.elements[i]:
			unlocked.append(i)
	var ctx := {"element": player.element, "unlocked_elements": unlocked, "tech_label": label,
		"tech_available": ok, "holding": held != null}
	if marker_pos != null:
		ctx["target_screen_pos"] = marker_pos
		ctx["target_label"] = marker_label
	elif tgt != null:
		ctx["target_screen_pos"] = cam.world_to_screen(tgt.chest())
		ctx["target_label"] = tgt.name
	else:
		ctx["target_screen_pos"] = null
	return ctx


func _debug_lines() -> PackedStringArray:
	var out := PackedStringArray()
	out.append("%s  tick %d  bodies %d/%d" % [scenario_id, world.tick, world.alive_count(), Sim.MAX_BODIES])
	for a in world.actors:
		var act := "-" if a.action == null else "%s/%s %.2f" % [a.action.id, a.action.phase_name(), a.action.total]
		out.append("%s hp%.0f bal%.0f foc%.0f heat%.0f %s %s" % [a.name, a.health, a.balance, a.focus, a.heat_reserve,
			Sim.ELEMENT_NAMES[a.element], act if a.stun <= 0.0 else "stun:" + a.stun_kind])
	if ai:
		out.append("AI: " + ai.debug_state)
	for b in world.bodies:
		if b.alive and b.form != Sim.Form.POOL and (b.controller >= 0 or b.attack_id != 0 or b.is_hot()):
			out.append(b.describe() + " " + b.origin + (" lin" + str(b.lineage) if b.lineage.size() > 0 else ""))
	out.append("energy Δ %.1f ledger %.1f" % [world.system_energy(), world.ledger_balance()])
	return out
