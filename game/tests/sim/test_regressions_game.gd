extends TestCase
## Regressions for the scenario rules that live in Game (game.gd): KOs (targets stand back up,
## a downed player or rival resets the round), the boulder launcher's seized plinth stone, Cold
## Hands vents re-melting set rock, the Storm's Path count, the water DRAW hint, adaptive quality
## stepping down on stale frames, and the unbounded perf session log. Each failed before its fix.
## Game runs off-tree: _ready, load_scenario and _apply_quality are stubbed.

class GameProbe extends Game:
	var reloads: Array[String] = []
	var applied: Array[int] = []

	func _ready() -> void:
		pass

	func load_scenario(id: String) -> void:
		reloads.append(id)
		_challenge_n = 0

	func _apply_quality() -> void:
		applied.append(quality)


func _game(id: String) -> GameProbe:
	var g := GameProbe.new()
	g.progress = Progression.new()
	g.progress.path = "user://test_regressions_game_progress.cfg"
	g.hud = Hud.new()
	var r := Scenarios.build(id, g.progress, 1)
	g.scenario_id = r.def.id
	g.scen_def = r.def
	g.world = r.world
	g.player = r.player
	g.opponent = r.opponent
	g._launcher = r.launcher
	g._launch_t = 1.5
	return g


func _free(g: GameProbe) -> void:
	g.hud.free()
	g.free()


func _actor(g: GameProbe, nm: String) -> ActorState:
	for a in g.world.actors:
		if a.name == nm:
			return a
	return null


## One Game tick without input devices: scenario props, sim step, KO handling.
func _tick(g: GameProbe, it: ActorIntent) -> Array[Dictionary]:
	g._scenario_tick()
	g.world.step({g.player.id: it})
	g._ko_tick()
	var evs := g.world.take_events()
	it.attack_pressed = false
	it.attack_released = false
	it.tech_pressed = false
	it.tech_released = false
	it.element_select = -1
	return evs


# ================================================================ KO

func test_downed_target_is_knocked_down_then_stands_at_full_health() -> void:
	var g := _game("conduction")
	var d := _actor(g, "Target 1")
	var it := ActorIntent.new()
	d.health = 0.0
	_tick(g, it)
	check(d.stun > 0.0 and d.stun_kind == "knockdown", "a target at 0 HP goes down (stun %.2f %s)" % [d.stun, d.stun_kind])
	for i in 180:
		_tick(g, it)
	near(d.health, Sim.HEALTH_MAX, 0.01, "3 s later the target is back at full health")
	check(g.world._valid_target(g.player, d.id), "it can be targeted and hit again")
	check(g.reloads.is_empty(), "a target going down never resets the round")
	_free(g)


func test_downed_rival_resets_the_round_after_the_knockdown() -> void:
	var g := _game("molten_exchange")
	var it := ActorIntent.new()
	g.opponent.health = 0.0
	_tick(g, it)
	check(g.opponent.stun_kind == "knockdown", "the rival is knocked down")
	check(g.hud._toast == "Rival down", "toast says the rival is down ('%s')" % g.hud._toast)
	var reset_at := -1
	for i in 240:
		g._scenario_tick()
		g.world.step({g.player.id: it})
		if g._ko_tick():
			reset_at = i
			break
	check(g.reloads == ["molten_exchange"], "the round resets once (%s)" % [g.reloads])
	check(reset_at > 60 and reset_at < 180, "after the knockdown has played (tick %d)" % reset_at)
	check(g.opponent.stun_kind == "knockdown", "no getup before the reset")
	_free(g)


func test_downed_player_resets_the_round_keeping_challenge_progress() -> void:
	var g := _game("stone_rain")
	var it := ActorIntent.new()
	g._challenge_n = 2
	g.player.health = 0.0
	_tick(g, it)
	check(g.hud._toast == "You're down", "toast says you are down ('%s')" % g.hud._toast)
	for i in 200:
		_tick(g, it)
		if not g.reloads.is_empty():
			break
	check(g.reloads.size() == 1, "the round resets")
	check(g._challenge_n == 2, "challenge progress survives the reset (%d)" % g._challenge_n)
	check(g.hud.challenge_text.ends_with("2/3"), "HUD shows it ('%s')" % g.hud.challenge_text)
	_free(g)


# ================================================================ boulder launcher

func test_seized_plinth_stone_is_released_by_the_launcher_and_flies_when_thrown() -> void:
	var g := _game("boulder")
	var p := g.player
	var w := g.world
	var it := ActorIntent.new()
	p.pos = Vector3(0, 0, -4.5)
	p.facing = PI
	it.element_select = Sim.Element.EARTH
	var b: MatBody = null
	for i in 120:
		_tick(g, it)
		b = w.get_body(g._launch_body)
		if b != null:
			break
	if not check(b != null and b.static_body, "a stone sits static on the plinth during the telegraph"):
		_free(g)
		return
	it.tech_pressed = true
	it.tech_held = true
	var held := 0
	for i in 60:
		_tick(g, it)
		if b.controller == p.id:
			held += 1
			if held > 18:
				break
	check(b.controller == p.id, "the player seized the plinth stone")
	check(not b.static_body, "a seized stone is no longer static")
	check(g._launch_body == -1, "the launcher lets go of it")
	it.tech_held = false
	it.tech_released = true
	var from := b.pos
	var owners := {}
	for i in 30:
		_tick(g, it)
		owners[b.attack_owner] = true
	check(b.pos.distance_to(from) > 3.0, "the thrown stone flies (moved %.2f m)" % b.pos.distance_to(from))
	check(not owners.has(-1), "the launcher never fires the player's stone (owners %s)" % [owners.keys()])
	_free(g)


# ================================================================ Cold Hands vents

func test_vent_does_not_remelt_lava_drawn_into_rock() -> void:
	var g := _game("cold_hands")
	var p := g.player
	var w := g.world
	var it := ActorIntent.new()
	var b: MatBody = null
	for c in w.bodies:
		if c.alive and c.origin == "vent" and (b == null or c.pos.distance_to(p.pos) < b.pos.distance_to(p.pos)):
			b = c
	var set_rock := false
	for i in 1200:
		var d := b.pos - p.pos
		p.facing = atan2(d.x, d.z)
		it.tech_pressed = p.action == null
		it.tech_held = true
		_tick(g, it)
		if b.phase == Sim.Phase.SOLID:
			set_rock = true
			break
	if not check(set_rock, "the drawn pool sets into rock"):
		_free(g)
		return
	# Released on the very tick it set: the leftover melt fraction is still just above 0.
	it.tech_held = false
	it.tech_released = true
	var peak := b.liquid
	for i in 600:
		_tick(g, it)
		peak = maxf(peak, b.liquid)
	check(b.alive and b.phase == Sim.Phase.SOLID, "the rock stays rock (%s)" % Sim.PHASE_NAMES[b.phase])
	check(peak <= 0.05, "the vent does not re-melt it (peak liquid %.3f)" % peak)
	_free(g)


# ================================================================ Storm's Path

func _bolt(g: GameProbe, target: ActorState) -> Array:
	var it := ActorIntent.new()
	g.player.lock_target = target.id
	it.attack_pressed = true
	it.attack_held = true
	var victims := []
	for i in 90:
		if i == 60:
			it.attack_held = false
			it.attack_released = true
		g.player.lock_target = target.id
		var evs := _tick(g, it)
		for e in evs:
			if e.type == "conduct" and e.actor == g.player.id:
				victims = e.victims
		g._challenges(evs)
	return victims


func test_storm_path_does_not_count_the_caster_as_a_target() -> void:
	var g := _game("conduction")
	var ch: Dictionary = g.scen_def.challenge.duplicate()
	ch.count = 2           # keep it from completing (no audio/menu off-tree)
	g.scen_def = g.scen_def.duplicate()
	g.scen_def.challenge = ch
	g.player.pos = Vector3(-5.6, 0, 2.0)
	var victims := _bolt(g, _actor(g, "Target 5"))
	check(victims.has(g.player.id) and victims.size() == 2, "the caster in the puddle is a conduction victim (%s)" % [victims])
	check(g._challenge_n == 0, "one target hit is not 'hit 2 targets' (count %d)" % g._challenge_n)
	# Two targets in the pool, caster outside it: counts.
	g.player.pos = Vector3(2.0, 0, 1.0)
	g.player.focus = 100.0
	victims = _bolt(g, _actor(g, "Target 1"))
	check(not victims.has(g.player.id) and victims.size() >= 2, "both pool targets are reached (%s)" % [victims])
	check(g._challenge_n == 1, "two targets in one bolt count (count %d)" % g._challenge_n)
	_free(g)


# ================================================================ water DRAW hint

func _water_hint(g: GameProbe, pos: Vector3, facing: float) -> bool:
	g.player.pos = pos
	g.player.facing = facing
	g.player.lock_target = -1
	return bool(g._hud_context(null).tech_available)


func test_water_draw_hint_follows_the_real_draw_sources() -> void:
	var g := _game("water_ice")
	g.player.element = Sim.Element.WATER
	g.player.water_carried = 0.0      # empty waterskin: only the pool / puddles can supply
	check(_water_hint(g, Vector3(0, 0, -1), 0.0), "pool edge 7.0 m away (reach 7.5): DRAW works, hint lit")
	check(not _water_hint(g, Vector3(-3, 0, -1), 0.0), "10 m from the pool, no puddle: hint dimmed")
	g.player.water_carried = 0.7
	check(not _water_hint(g, Vector3(-3, 0, -1), 0.0), "0.7 kg in the waterskin is not enough to draw")
	g.player.water_carried = 1.0
	check(_water_hint(g, Vector3(-3, 0, -1), 0.0), "1 kg in the waterskin draws")
	_free(g)
	g = _game("conduction")
	g.player.element = Sim.Element.WATER
	g.player.water_carried = 0.0
	check(_water_hint(g, Vector3(-5.2, 0, 5.5), PI), "a puddle in the aim cone: hint lit")
	check(not _water_hint(g, Vector3(-5.2, 0, 5.5), 0.0), "the same puddle behind you: hint dimmed")
	_free(g)


# ================================================================ adaptive quality / perf log

func test_quality_steps_down_once_when_the_lower_tier_is_fast() -> void:
	var g := GameProbe.new()
	var t := 0.0
	while g.quality == 2 and t < 30.0:
		g.perf.frame(0.025)
		g._adapt_quality(0.025)
		t += 0.025
	check(g.quality == 1, "sustained 25 ms frames step down (q %d at %.1f s)" % [g.quality, t])
	for i in 2000:
		g.perf.frame(0.011)
		g._adapt_quality(0.011)
	check(g.quality == 1, "11 ms frames at the new tier keep it there (q %d, applied %s)" % [g.quality, g.applied])
	g.free()


func test_quality_still_steps_again_when_the_lower_tier_is_slow() -> void:
	var g := GameProbe.new()
	var t := 0.0
	var at := []
	while g.quality > 0 and t < 60.0:
		var q := g.quality
		g.perf.frame(0.025)
		g._adapt_quality(0.025)
		t += 0.025
		if g.quality != q:
			at.append(t)
	check(g.quality == 0 and at.size() == 2, "two step-downs under sustained slow frames (%s)" % [at])
	if at.size() == 2:
		check(at[1] - at[0] >= 8.0, "the second waits for a fresh window + 4 s (%.1f s)" % (at[1] - at[0]))
	g.free()


func test_perf_session_log_is_kept_only_when_recording() -> void:
	var pm := PerfMonitor.new()
	for i in 600:
		pm.frame(0.2)
	check(pm.session.is_empty(), "no per-second log by default (%d entries)" % pm.session.size())
	pm.record_session = true
	for i in 10:
		pm.frame(0.2)
	check(pm.session.size() == 2, "recording appends one sample per second (%d)" % pm.session.size())
